#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../include/core/spatial_csv.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

struct Point2D {
  double x, y;
  Point2D() : x(0), y(0) {}
  Point2D(double x, double y) : x(x), y(y) {}

  double distanceTo(const Point2D& other) const {
    double dx = x - other.x;
    double dy = y - other.y;
    return std::sqrt(dx * dx + dy * dy);
  }
};

struct LRSVertex {
  double x, y;
  double measure;

  LRSVertex() : x(0), y(0), measure(0) {}
  LRSVertex(double x, double y, double m) : x(x), y(y), measure(m) {}
};

struct LRSRoute {
  std::string route_id;
  std::vector<LRSVertex> vertices;
  double total_length;
  std::map<std::string, std::string> attributes;
};

struct LRSNetwork {
  std::vector<LRSRoute> routes;
  std::map<std::string, int> route_index;
  std::string crs;
};

struct LinearEvent {
  std::string id;
  std::string route_id;
  double from_measure;
  double to_measure;
  double offset;
  bool valid;
  std::map<std::string, std::string> attributes;
};

struct Segment {
  std::string id;
  std::string route_id;
  double from_measure;
  double to_measure;
  double offset;
  std::vector<Point2D> geometry;
  std::map<std::string, std::string> attributes;
  std::string source_event_id;
};

LRSNetwork loadLRSNetwork(const std::string& filename) {
  LRSNetwork network;
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file: " << filename << "\n";
    return network;
  }

  std::string line;
  std::string section;
  bool in_routes = false;

  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty())
      continue;

    if (line[0] == '#')
      continue;

    if (line[0] == '@') {
      section = line.substr(1);
      in_routes = (section == "ROUTES");
      continue;
    }

    if (in_routes && section == "ROUTES") {
      auto parts = splitCSV(line);
      if (parts.size() >= 4) {
        std::string route_id = parts[0];

        std::string geom = parts[3];
        if (geom.front() == '"' && geom.back() == '"') {
          geom = geom.substr(1, geom.length() - 2);
        }

        size_t start = geom.find('(');
        size_t end = geom.rfind(')');
        if (start != std::string::npos && end != std::string::npos) {
          std::string inner = geom.substr(start + 1, end - start - 1);
          auto points = split(inner, ',');

          LRSRoute route;
          route.route_id = route_id;

          for (const auto& pt : points) {
            auto coords = split(trim(pt), ' ');
            if (coords.size() >= 3) {
              try {
                double x = std::stod(coords[0]);
                double y = std::stod(coords[1]);
                double m = std::stod(coords[2]);
                route.vertices.push_back(LRSVertex(x, y, m));
              } catch (...) {
              }
            }
          }

          if (!route.vertices.empty()) {
            route.total_length = route.vertices.back().measure - route.vertices.front().measure;
            network.routes.push_back(route);
            network.route_index[route_id] = network.routes.size() - 1;
          }
        }
      }
    }
  }

  return network;
}

std::vector<LinearEvent> readLinearEvents(const std::string& filename) {
  std::vector<LinearEvent> events;
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file: " << filename << "\n";
    return events;
  }

  std::string line;
  std::vector<std::string> headers;
  bool is_header = true;

  int col_id = -1;
  int col_route_id = -1;
  int col_from_measure = -1;
  int col_to_measure = -1;
  int col_offset = -1;
  std::vector<int> attr_cols;

  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#')
      continue;

    auto values = splitCSV(line);

    if (is_header) {
      headers = values;

      for (size_t i = 0; i < headers.size(); ++i) {
        std::string lower = toLower(headers[i]);
        if (lower == "id" || lower == "event_id") {
          col_id = i;
        } else if (lower == "route_id" || lower == "route") {
          col_route_id = i;
        } else if (lower == "from_measure" || lower == "from" || lower == "m_from") {
          col_from_measure = i;
        } else if (lower == "to_measure" || lower == "to" || lower == "m_to") {
          col_to_measure = i;
        } else if (lower == "offset") {
          col_offset = i;
        } else {
          attr_cols.push_back(i);
        }
      }

      is_header = false;
      continue;
    }

    LinearEvent event;
    event.valid = false;
    event.offset = 0;

    if (col_id >= 0 && col_id < (int)values.size()) {
      event.id = values[col_id];
    } else {
      event.id = std::to_string(events.size() + 1);
    }

    if (col_route_id >= 0 && col_route_id < (int)values.size()) {
      event.route_id = values[col_route_id];
    }

    if (col_from_measure >= 0 && col_from_measure < (int)values.size() &&
        isNumber(values[col_from_measure])) {
      event.from_measure = std::stod(values[col_from_measure]);
    } else {
      continue;
    }

    if (col_to_measure >= 0 && col_to_measure < (int)values.size() &&
        isNumber(values[col_to_measure])) {
      event.to_measure = std::stod(values[col_to_measure]);
    } else {
      continue;
    }

    if (col_offset >= 0 && col_offset < (int)values.size() && isNumber(values[col_offset])) {
      event.offset = std::stod(values[col_offset]);
    }

    if (event.from_measure <= event.to_measure) {
      event.valid = true;
    }

    for (int idx : attr_cols) {
      if (idx < (int)values.size()) {
        event.attributes[headers[idx]] = values[idx];
      }
    }

    events.push_back(event);
  }

  return events;
}

std::vector<Point2D> extractSegmentGeometry(const LRSRoute& route, double from_measure,
                                            double to_measure) {
  std::vector<Point2D> segment_points;

  if (route.vertices.size() < 2)
    return segment_points;

  size_t start_idx = 0;
  for (size_t i = 0; i < route.vertices.size(); ++i) {
    if (route.vertices[i].measure >= from_measure) {
      start_idx = i;
      break;
    }
  }

  size_t end_idx = route.vertices.size() - 1;
  for (size_t i = route.vertices.size() - 1; i > 0; --i) {
    if (route.vertices[i].measure <= to_measure) {
      end_idx = i;
      break;
    }
  }

  if (start_idx > end_idx)
    return segment_points;

  if (start_idx > 0 && route.vertices[start_idx].measure > from_measure) {
    const auto& v1 = route.vertices[start_idx - 1];
    const auto& v2 = route.vertices[start_idx];
    double t = (from_measure - v1.measure) / (v2.measure - v1.measure);
    double x = v1.x + t * (v2.x - v1.x);
    double y = v1.y + t * (v2.y - v1.y);
    segment_points.push_back(Point2D(x, y));
  } else {
    segment_points.push_back(Point2D(route.vertices[start_idx].x, route.vertices[start_idx].y));
  }

  for (size_t i = start_idx; i <= end_idx; ++i) {
    if (i == start_idx && route.vertices[i].measure < from_measure)
      continue;
    if (i == end_idx && route.vertices[i].measure > to_measure)
      continue;
    segment_points.push_back(Point2D(route.vertices[i].x, route.vertices[i].y));
  }

  if (end_idx < route.vertices.size() - 1 && route.vertices[end_idx].measure < to_measure) {
    const auto& v1 = route.vertices[end_idx];
    const auto& v2 = route.vertices[end_idx + 1];
    double t = (to_measure - v1.measure) / (v2.measure - v1.measure);
    double x = v1.x + t * (v2.x - v1.x);
    double y = v1.y + t * (v2.y - v1.y);
    segment_points.push_back(Point2D(x, y));
  }

  return segment_points;
}

std::vector<Point2D> applyOffset(const std::vector<Point2D>& points, double offset) {
  if (std::abs(offset) < 1e-12 || points.size() < 2)
    return points;

  std::vector<Point2D> result;

  for (size_t i = 0; i < points.size(); ++i) {
    double dx = 0, dy = 0;
    if (i == 0 && points.size() > 1) {
      dx = points[1].x - points[0].x;
      dy = points[1].y - points[0].y;
    } else if (i == points.size() - 1 && points.size() > 1) {
      dx = points[i].x - points[i - 1].x;
      dy = points[i].y - points[i - 1].y;
    } else if (i > 0 && i < points.size() - 1) {
      double dx1 = points[i].x - points[i - 1].x;
      double dy1 = points[i].y - points[i - 1].y;
      double dx2 = points[i + 1].x - points[i].x;
      double dy2 = points[i + 1].y - points[i].y;
      dx = (dx1 + dx2) / 2.0;
      dy = (dy1 + dy2) / 2.0;
    }

    double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-12) {
      result.push_back(points[i]);
      continue;
    }

    double nx = -dy / len;
    double ny = dx / len;

    double x = points[i].x + nx * offset;
    double y = points[i].y + ny * offset;
    result.push_back(Point2D(x, y));
  }

  return result;
}

bool segmentsOverlap(const LinearEvent& e1, const LinearEvent& e2) {
  if (e1.route_id != e2.route_id)
    return false;
  return !(e1.to_measure < e2.from_measure || e2.to_measure < e1.from_measure);
}

bool segmentsAdjacent(const LinearEvent& e1, const LinearEvent& e2, double tolerance = 1e-9) {
  if (e1.route_id != e2.route_id)
    return false;
  return (std::abs(e1.to_measure - e2.from_measure) < tolerance) ||
         (std::abs(e2.to_measure - e1.from_measure) < tolerance);
}

LinearEvent mergeEvents(const LinearEvent& e1, const LinearEvent& e2) {
  LinearEvent merged;
  merged.id = e1.id + "_" + e2.id;
  merged.route_id = e1.route_id;
  merged.from_measure = std::min(e1.from_measure, e2.from_measure);
  merged.to_measure = std::max(e1.to_measure, e2.to_measure);
  merged.offset = (e1.offset + e2.offset) / 2.0;
  merged.valid = true;

  for (const auto& [key, value] : e1.attributes) {
    merged.attributes[key] = value;
  }
  for (const auto& [key, value] : e2.attributes) {
    merged.attributes[key] = value;
  }
  merged.attributes["merged_from"] = e1.id + "," + e2.id;

  return merged;
}

std::vector<LinearEvent> mergeOverlappingEvents(const std::vector<LinearEvent>& events) {
  if (events.empty())
    return events;

  std::vector<LinearEvent> merged_events;
  std::vector<bool> processed(events.size(), false);

  for (size_t i = 0; i < events.size(); ++i) {
    if (processed[i])
      continue;

    LinearEvent current = events[i];
    processed[i] = true;

    bool changed = true;
    while (changed) {
      changed = false;
      for (size_t j = 0; j < events.size(); ++j) {
        if (processed[j])
          continue;

        if (segmentsOverlap(current, events[j]) || segmentsAdjacent(current, events[j])) {
          current = mergeEvents(current, events[j]);
          processed[j] = true;
          changed = true;
        }
      }
    }

    merged_events.push_back(current);
  }

  return merged_events;
}

std::string pointsToWKT(const std::vector<Point2D>& points) {
  if (points.size() < 2)
    return "";

  std::string wkt = "LINESTRING(";
  for (size_t i = 0; i < points.size(); ++i) {
    if (i > 0)
      wkt += ", ";
    wkt += std::to_string(points[i].x) + " " + std::to_string(points[i].y);
  }
  wkt += ")";

  return wkt;
}

void writeSegmentsCSV(const std::vector<Segment>& segments, const std::string& filename) {
  std::ofstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not create file: " << filename << "\n";
    return;
  }

  file << "# LRS Segments\n";
  file << "# Geometry column: geometry\n";
  file << "# Segments: " << segments.size() << "\n";

  std::set<std::string> all_attrs;
  for (const auto& seg : segments) {
    for (const auto& [key, value] : seg.attributes) {
      all_attrs.insert(key);
    }
  }

  file << "id,route_id,from_measure,to_measure,offset,length";
  for (const auto& attr : all_attrs) {
    file << "," << attr;
  }
  file << ",source_event,geometry\n";

  for (const auto& seg : segments) {
    double length = 0;
    for (size_t i = 0; i < seg.geometry.size() - 1; ++i) {
      length += seg.geometry[i].distanceTo(seg.geometry[i + 1]);
    }

    file << seg.id << "," << seg.route_id << "," << std::fixed << std::setprecision(6)
         << seg.from_measure << "," << std::fixed << std::setprecision(6) << seg.to_measure << ","
         << std::fixed << std::setprecision(6) << seg.offset << "," << std::fixed
         << std::setprecision(6) << length;

    for (const auto& attr : all_attrs) {
      auto it = seg.attributes.find(attr);
      if (it != seg.attributes.end()) {
        file << "," << it->second;
      } else {
        file << ",";
      }
    }

    file << "," << seg.source_event_id << ","
         << "\"" << pointsToWKT(seg.geometry) << "\"\n";
  }
}

void printUsage() {
  std::cerr << "spatial_lrs_segment - Create segments from linear events\n\n";
  std::cerr << "Usage: spatial_lrs_segment <network.lrs> <events.csv> <output.csv> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -offset <value>   Default offset (lateral displacement)\n";
  std::cerr << "  -merge            Merge overlapping/adjacent segments\n";
  std::cerr << "  -verbose          Show detailed information\n\n";
  std::cerr << "Events CSV must contain:\n";
  std::cerr << "  id: Event identifier\n";
  std::cerr << "  route_id: Route identifier\n";
  std::cerr << "  from_measure: Start measure\n";
  std::cerr << "  to_measure: End measure\n";
  std::cerr << "  offset: Lateral offset (optional)\n\n";
  std::cerr << "Example:\n";
  std::cerr << "  spatial_lrs_segment lrs_network.lrs conditions.csv segments.csv -offset 3\n";
  std::cerr << "  spatial_lrs_segment lrs_network.lrs conditions.csv segments.csv -merge\n";
}

int main(int argc, char* argv[]) {
  if (argc < 4) {
    printUsage();
    return 1;
  }

  std::string network_file = argv[1];
  std::string events_file = argv[2];
  std::string output_file = argv[3];
  double default_offset = 0;
  bool merge = false;
  bool verbose = false;

  for (int i = 4; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-offset" && i + 1 < argc) {
      default_offset = std::stod(argv[++i]);
    } else if (arg == "-merge") {
      merge = true;
    } else if (arg == "-verbose") {
      verbose = true;
    }
  }

  std::cout << "========================================\n";
  std::cout << "  LRS SEGMENT CREATION\n";
  std::cout << "========================================\n\n";
  std::cout << "Network: " << network_file << "\n";
  std::cout << "Events: " << events_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Default offset: " << default_offset << "\n";
  std::cout << "Merge: " << (merge ? "yes" : "no") << "\n\n";

  LRSNetwork network = loadLRSNetwork(network_file);

  if (network.routes.empty()) {
    std::cerr << "Error: No routes found in network file\n";
    return 1;
  }

  std::cout << "Routes loaded: " << network.routes.size() << "\n";

  std::vector<LinearEvent> events = readLinearEvents(events_file);

  if (events.empty()) {
    std::cerr << "Error: No events found\n";
    return 1;
  }

  std::cout << "Events loaded: " << events.size() << "\n";

  std::vector<LinearEvent> valid_events;
  for (const auto& event : events) {
    if (event.valid) {
      valid_events.push_back(event);
    }
  }

  std::cout << "Valid events: " << valid_events.size() << "\n";

  if (valid_events.empty()) {
    std::cerr << "Error: No valid events found\n";
    return 1;
  }

  std::vector<LinearEvent> processed_events;
  if (merge) {
    std::cout << "\nMerging overlapping/adjacent segments...\n";
    processed_events = mergeOverlappingEvents(valid_events);
    std::cout << "Events after merging: " << processed_events.size() << "\n";
  } else {
    processed_events = valid_events;
  }

  std::vector<Segment> segments;
  int created = 0;
  int errors = 0;

  std::cout << "\nCreating segments...\n";

  for (const auto& event : processed_events) {
    auto it = network.route_index.find(event.route_id);
    if (it == network.route_index.end()) {
      errors++;
      continue;
    }

    const LRSRoute& route = network.routes[it->second];

    std::vector<Point2D> geom = extractSegmentGeometry(route, event.from_measure, event.to_measure);

    if (geom.size() < 2) {
      errors++;
      continue;
    }

    double offset = (event.offset != 0) ? event.offset : default_offset;
    if (std::abs(offset) > 1e-12) {
      geom = applyOffset(geom, offset);
    }

    Segment seg;
    seg.id = event.id;
    seg.route_id = event.route_id;
    seg.from_measure = event.from_measure;
    seg.to_measure = event.to_measure;
    seg.offset = offset;
    seg.geometry = geom;
    seg.attributes = event.attributes;
    seg.source_event_id = event.id;

    segments.push_back(seg);
    created++;

    if (verbose && created % 10 == 0) {
      std::cout << "\r  Progress: " << created << "/" << processed_events.size() << std::flush;
    }
  }

  if (verbose) {
    std::cout << "\r  Progress: " << processed_events.size() << "/" << processed_events.size()
              << "\n";
  }

  writeSegmentsCSV(segments, output_file);

  std::cout << "\n========================================\n";
  std::cout << "  SEGMENT CREATION COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Events processed: " << processed_events.size() << "\n";
  std::cout << "Segments created: " << created << "\n";
  std::cout << "Errors: " << errors << "\n";
  std::cout << "Output written to: " << output_file << "\n";

  if (verbose) {
    std::cout << "\nSample segments:\n";
    int count = 0;
    for (const auto& seg : segments) {
      if (count < 5) {
        std::cout << "  " << seg.id << " -> Route: " << seg.route_id << ", M: " << std::fixed
                  << std::setprecision(4) << seg.from_measure << " - " << std::fixed
                  << std::setprecision(4) << seg.to_measure << ", Offset: " << std::fixed
                  << std::setprecision(2) << seg.offset << ", Length: " << std::fixed
                  << std::setprecision(4) << seg.geometry.size() << " vertices\n";
        count++;
      }
    }
  }

  return 0;
}
