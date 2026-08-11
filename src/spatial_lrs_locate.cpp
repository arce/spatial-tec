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

struct PointEvent {
  std::string id;
  std::string route_id;
  double x, y;
  double measure;
  double offset;
  bool located;
  std::map<std::string, std::string> attributes;
};

std::vector<Point2D> parseWKTLineString(const std::string& wkt) {
  std::vector<Point2D> points;
  std::string str = trim(wkt);

  size_t start = str.find('(');
  if (start == std::string::npos)
    return points;

  size_t end = str.rfind(')');
  if (end == std::string::npos || end <= start)
    return points;

  std::string inner = str.substr(start + 1, end - start - 1);
  auto coord_strings = split(inner, ',');

  for (const auto& cs : coord_strings) {
    auto parts = split(trim(cs), ' ');
    if (parts.size() >= 2) {
      try {
        double x = std::stod(parts[0]);
        double y = std::stod(parts[1]);
        points.push_back(Point2D(x, y));
      } catch (...) {
      }
    }
  }

  return points;
}

LRSNetwork loadLRSNetwork(const std::string& filename) {
  LRSNetwork network;
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file: " << filename << "\n";
    return network;
  }

  std::string line;
  std::string section;
  LRSRoute current_route;
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

std::vector<PointEvent> readPointEvents(const std::string& filename) {
  std::vector<PointEvent> events;
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
  int col_geometry = -1;
  int col_x = -1;
  int col_y = -1;
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
        } else if (lower == "geometry" || lower == "geom" || lower == "wkt") {
          col_geometry = i;
        } else if (lower == "x" || lower == "longitude" || lower == "lon") {
          col_x = i;
        } else if (lower == "y" || lower == "latitude" || lower == "lat") {
          col_y = i;
        } else {
          attr_cols.push_back(i);
        }
      }

      is_header = false;
      continue;
    }

    PointEvent event;
    event.located = false;
    event.measure = 0;
    event.offset = 0;

    if (col_id >= 0 && col_id < (int)values.size()) {
      event.id = values[col_id];
    } else {
      event.id = std::to_string(events.size() + 1);
    }

    if (col_route_id >= 0 && col_route_id < (int)values.size()) {
      event.route_id = values[col_route_id];
    }

    if (col_geometry >= 0 && col_geometry < (int)values.size()) {
      auto points = parseWKTLineString(values[col_geometry]);
      if (!points.empty()) {
        event.x = points[0].x;
        event.y = points[0].y;
      }
    } else if (col_x >= 0 && col_x < (int)values.size() && col_y >= 0 &&
               col_y < (int)values.size()) {
      try {
        event.x = std::stod(values[col_x]);
        event.y = std::stod(values[col_y]);
      } catch (...) {
      }
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

double pointToSegmentMeasure(const Point2D& point, const LRSVertex& v1, const LRSVertex& v2) {
  double dx = v2.x - v1.x;
  double dy = v2.y - v1.y;
  double len2 = dx * dx + dy * dy;

  if (len2 < 1e-12) {
    return v1.measure;
  }

  double t = ((point.x - v1.x) * dx + (point.y - v1.y) * dy) / len2;
  t = std::max(0.0, std::min(1.0, t));

  return v1.measure + t * (v2.measure - v1.measure);
}

double pointToSegmentDistance(const Point2D& point, const LRSVertex& v1, const LRSVertex& v2) {
  double dx = v2.x - v1.x;
  double dy = v2.y - v1.y;
  double len2 = dx * dx + dy * dy;

  if (len2 < 1e-12) {
    return point.distanceTo(Point2D(v1.x, v1.y));
  }

  double t = ((point.x - v1.x) * dx + (point.y - v1.y) * dy) / len2;
  t = std::max(0.0, std::min(1.0, t));

  double px = v1.x + t * dx;
  double py = v1.y + t * dy;

  return point.distanceTo(Point2D(px, py));
}

bool locatePointEvent(PointEvent& event, const LRSRoute& route, double max_distance = 0.01) {
  if (route.vertices.size() < 2)
    return false;

  Point2D point(event.x, event.y);
  double min_dist = std::numeric_limits<double>::max();
  double best_measure = 0;
  int best_segment = -1;

  for (size_t i = 0; i < route.vertices.size() - 1; ++i) {
    double dist = pointToSegmentDistance(point, route.vertices[i], route.vertices[i + 1]);
    if (dist < min_dist) {
      min_dist = dist;
      best_measure = pointToSegmentMeasure(point, route.vertices[i], route.vertices[i + 1]);
      best_segment = i;
    }
  }

  if (max_distance > 0 && min_dist > max_distance) {
    return false;
  }

  event.measure = best_measure;
  event.offset = min_dist;
  event.located = true;

  return true;
}

void writeLocatedEvents(const std::vector<PointEvent>& events, const std::string& filename) {
  std::ofstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not create file: " << filename << "\n";
    return;
  }

  file << "# Located events\n";
  file << "# Geometry column: geometry\n";

  std::set<std::string> all_attrs;
  for (const auto& event : events) {
    for (const auto& [key, value] : event.attributes) {
      all_attrs.insert(key);
    }
  }

  file << "id,route_id,measure,offset,status";
  for (const auto& attr : all_attrs) {
    file << "," << attr;
  }
  file << ",x,y,geometry\n";

  for (const auto& event : events) {
    file << event.id << "," << event.route_id << "," << std::fixed << std::setprecision(6)
         << event.measure << "," << std::fixed << std::setprecision(6) << event.offset << ","
         << (event.located ? "located" : "not_found");

    for (const auto& attr : all_attrs) {
      auto it = event.attributes.find(attr);
      if (it != event.attributes.end()) {
        file << "," << it->second;
      } else {
        file << ",";
      }
    }

    file << "," << std::fixed << std::setprecision(6) << event.x << "," << std::fixed
         << std::setprecision(6) << event.y << ","
         << "POINT(" << std::fixed << std::setprecision(6) << event.x << " " << std::fixed
         << std::setprecision(6) << event.y << ")\n";
  }
}

void printUsage() {
  std::cerr << "spatial_lrs_locate - Locate point events along LRS network\n\n";
  std::cerr << "Usage: spatial_lrs_locate <network.lrs> <events.csv> <output.csv> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -max_dist <value>  Maximum distance to locate (default: 0.01)\n";
  std::cerr << "  -route_id <col>    Column name for route ID (default: route_id)\n";
  std::cerr << "  -verbose           Show detailed information\n\n";
  std::cerr << "Events CSV must contain:\n";
  std::cerr << "  id: Event identifier\n";
  std::cerr << "  geometry: POINT WKT geometry OR x,y columns\n";
  std::cerr << "  route_id: Route identifier\n\n";
  std::cerr << "Example:\n";
  std::cerr << "  spatial_lrs_locate lrs_network.lrs accidents.csv located_accidents.csv\n";
}

int main(int argc, char* argv[]) {
  if (argc < 4) {
    printUsage();
    return 1;
  }

  std::string network_file = argv[1];
  std::string events_file = argv[2];
  std::string output_file = argv[3];
  double max_distance = 0.01;
  bool verbose = false;

  for (int i = 4; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-max_dist" && i + 1 < argc) {
      max_distance = std::stod(argv[++i]);
    } else if (arg == "-verbose") {
      verbose = true;
    }
  }

  std::cout << "========================================\n";
  std::cout << "  LRS POINT LOCATION\n";
  std::cout << "========================================\n\n";
  std::cout << "Network: " << network_file << "\n";
  std::cout << "Events: " << events_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Max distance: " << max_distance << "\n\n";

  LRSNetwork network = loadLRSNetwork(network_file);

  if (network.routes.empty()) {
    std::cerr << "Error: No routes found in network file\n";
    return 1;
  }

  std::cout << "Routes loaded: " << network.routes.size() << "\n";

  std::vector<PointEvent> events = readPointEvents(events_file);

  if (events.empty()) {
    std::cerr << "Error: No events found\n";
    return 1;
  }

  std::cout << "Events loaded: " << events.size() << "\n\n";

  int located = 0;
  int not_found = 0;
  int no_route = 0;

  for (auto& event : events) {
    auto it = network.route_index.find(event.route_id);
    if (it == network.route_index.end()) {
      no_route++;
      continue;
    }

    const LRSRoute& route = network.routes[it->second];

    if (locatePointEvent(event, route, max_distance)) {
      located++;
    } else {
      not_found++;
    }
  }

  writeLocatedEvents(events, output_file);

  std::cout << "========================================\n";
  std::cout << "  LOCATION COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Events processed: " << events.size() << "\n";
  std::cout << "Located: " << located << "\n";
  std::cout << "Not found (distance): " << not_found << "\n";
  std::cout << "No route found: " << no_route << "\n";
  std::cout << "Output written to: " << output_file << "\n";

  if (verbose) {
    std::cout << "\nSample located events:\n";
    int count = 0;
    for (const auto& event : events) {
      if (event.located && count < 5) {
        std::cout << "  " << event.id << " -> Route: " << event.route_id << ", M: " << std::fixed
                  << std::setprecision(4) << event.measure << ", Offset: " << std::fixed
                  << std::setprecision(4) << event.offset << "\n";
        count++;
      }
    }
  }

  return 0;
}
