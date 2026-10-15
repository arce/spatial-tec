#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../include/core/spatial_string.hpp"

struct Point2D {
  double x, y;
  Point2D() : x(0), y(0) {}
  Point2D(double x, double y) : x(x), y(y) {}

  double distanceTo(const Point2D& other) const {
    return std::sqrt((x - other.x) * (x - other.x) + (y - other.y) * (y - other.y));
  }
};

struct LRSVertex {
  double x = 0, y = 0, measure = 0;
};

struct LRSRoute {
  std::string route_id;
  std::vector<LRSVertex> vertices;
};

struct LRSNetwork {
  std::vector<LRSRoute> routes;
  std::map<std::string, int> route_index;
};

struct LinearEvent {
  std::string id;
  std::string route_id;
  double from_measure = 0;
  double to_measure = 0;
  double offset = 0;
  bool valid = false;
  std::map<std::string, std::string> attributes;
  std::vector<std::string> source_ids;  // original event ids represented by this event
};

struct Segment {
  std::string id;
  std::string route_id;
  double from_measure, to_measure, offset, length;
  std::vector<Point2D> geometry;
  std::map<std::string, std::string> attributes;
  std::string source_events;
};

static std::string csvQuote(const std::string& s) {
  if (s.find_first_of(",\"\n") == std::string::npos)
    return s;
  std::string out = "\"";
  for (char c : s) {
    if (c == '"')
      out += '"';
    out += c;
  }
  return out + "\"";
}

static bool loadLRSNetwork(const std::string& filename, LRSNetwork& network) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file: " << filename << "\n";
    return false;
  }

  std::string line, section;
  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#')
      continue;
    if (line[0] == '@') {
      section = line.substr(1);
      continue;
    }
    if (section != "ROUTES")
      continue;

    auto parts = splitCSV(line);
    if (parts.size() < 4 || toLower(parts[0]) == "route_id")
      continue;

    std::string geom = parts[3];
    if (geom.size() >= 2 && geom.front() == '"' && geom.back() == '"')
      geom = geom.substr(1, geom.length() - 2);
    size_t start = geom.find('(');
    size_t end = geom.rfind(')');
    if (start == std::string::npos || end == std::string::npos || end <= start)
      continue;

    LRSRoute route;
    route.route_id = parts[0];
    for (const auto& pt : split(geom.substr(start + 1, end - start - 1), ',')) {
      auto c = split(trim(pt), ' ');
      if (c.size() >= 3) {
        try {
          route.vertices.push_back({std::stod(c[0]), std::stod(c[1]), std::stod(c[2])});
        } catch (...) {
        }
      }
    }
    if (!route.vertices.empty()) {
      network.routes.push_back(route);
      network.route_index[route.route_id] = (int)network.routes.size() - 1;
    }
  }
  return true;
}

// Reads the events. attr_names receives the extra columns in file order.
static bool readLinearEvents(const std::string& filename, std::vector<LinearEvent>& events,
                             std::vector<std::string>& attr_names, int& skipped, bool& point_mode) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file: " << filename << "\n";
    return false;
  }

  std::string line;
  std::vector<std::string> headers;
  bool is_header = true;
  int col_id = -1, col_route = -1, col_from = -1, col_to = -1, col_offset = -1, col_measure = -1;
  std::vector<int> attr_cols;
  skipped = 0;

  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#')
      continue;
    auto values = splitCSV(line);

    if (is_header) {
      headers = values;
      for (size_t i = 0; i < headers.size(); ++i) {
        std::string lower = toLower(trim(headers[i]));
        if (lower == "id" || lower == "event_id")
          col_id = (int)i;
        else if (lower == "route_id" || lower == "route")
          col_route = (int)i;
        else if (lower == "from_measure" || lower == "from" || lower == "m_from")
          col_from = (int)i;
        else if (lower == "to_measure" || lower == "to" || lower == "m_to")
          col_to = (int)i;
        else if (lower == "measure" || lower == "m")
          col_measure = (int)i;
        else if (lower == "offset")
          col_offset = (int)i;
        else {
          attr_cols.push_back((int)i);
          attr_names.push_back(trim(headers[i]));
        }
      }
      if (col_measure >= 0 && (col_from >= 0 || col_to >= 0)) {
        std::cerr << "Error: The events file has both 'measure' and from_measure/to_measure; use one kind of event\n";
        return false;
      }
      point_mode = col_measure >= 0;
      if (col_route < 0 || (!point_mode && (col_from < 0 || col_to < 0))) {
        std::cerr << "Error: The events file needs the columns route_id and either measure (point events) or "
                     "from_measure and to_measure (linear events)\n";
        return false;
      }
      is_header = false;
      continue;
    }

    auto get = [&](int c) -> std::string {
      return (c >= 0 && c < (int)values.size()) ? trim(values[c]) : "";
    };

    LinearEvent ev;
    ev.id = col_id >= 0 && !get(col_id).empty() ? get(col_id) : std::to_string(events.size() + skipped + 1);
    ev.route_id = get(col_route);
    if (point_mode) {
      if (!isNumber(get(col_measure))) {
        skipped++;
        continue;
      }
      ev.from_measure = ev.to_measure = std::stod(get(col_measure));
    } else {
      if (!isNumber(get(col_from)) || !isNumber(get(col_to))) {
        skipped++;
        continue;
      }
      ev.from_measure = std::stod(get(col_from));
      ev.to_measure = std::stod(get(col_to));
    }
    if (col_offset >= 0 && isNumber(get(col_offset)))
      ev.offset = std::stod(get(col_offset));
    ev.valid = ev.from_measure <= ev.to_measure;
    for (size_t k = 0; k < attr_cols.size(); ++k)
      ev.attributes[attr_names[k]] = get(attr_cols[k]);
    ev.source_ids.push_back(ev.id);
    events.push_back(ev);
  }
  return !is_header;
}

static Point2D pointAtMeasure(const LRSVertex& a, const LRSVertex& b, double m) {
  double t = (b.measure > a.measure) ? (m - a.measure) / (b.measure - a.measure) : 0.0;
  return Point2D(a.x + t * (b.x - a.x), a.y + t * (b.y - a.y));
}

// Part of the route between two measures, interpolating the end points.
static std::vector<Point2D> extractSegmentGeometry(const LRSRoute& route, double from, double to) {
  std::vector<Point2D> pts;
  const auto& v = route.vertices;
  auto push = [&](const Point2D& p) {
    if (pts.empty() || pts.back().distanceTo(p) > 1e-12)
      pts.push_back(p);
  };
  for (size_t i = 0; i + 1 < v.size(); ++i) {
    double m0 = v[i].measure, m1 = v[i + 1].measure;
    if (m1 <= m0)
      continue;
    double lo = std::max(from, m0), hi = std::min(to, m1);
    if (lo > hi)
      continue;
    push(pointAtMeasure(v[i], v[i + 1], lo));
    push(pointAtMeasure(v[i], v[i + 1], hi));
  }
  return pts;
}

// Position of a measure on the route, plus the unit direction of the segment it falls on.
// Returns false when the measure is outside the measure range of the route.
static bool locateMeasure(const LRSRoute& route, double m, Point2D& pos, double& dx, double& dy) {
  const auto& v = route.vertices;
  const double tol = 1e-9;
  for (size_t i = 0; i + 1 < v.size(); ++i) {
    double m0 = v[i].measure, m1 = v[i + 1].measure;
    if (m1 <= m0 || m < m0 - tol || m > m1 + tol)
      continue;
    double mm = std::max(m0, std::min(m1, m));
    pos = pointAtMeasure(v[i], v[i + 1], mm);
    dx = v[i + 1].x - v[i].x;
    dy = v[i + 1].y - v[i].y;
    double len = std::sqrt(dx * dx + dy * dy);
    if (len > 1e-12) {
      dx /= len;
      dy /= len;
    }
    return true;
  }
  return false;
}

struct PointResult {
  std::string id, route_id;
  double measure, offset, x, y;
  std::map<std::string, std::string> attributes;
};

static bool writePointsCSV(const std::vector<PointResult>& points, const std::vector<std::string>& attr_names,
                           const std::string& filename) {
  std::ofstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not create file: " << filename << "\n";
    return false;
  }
  file << "# LRS Point events\n# Geometry column: geometry\n# Points: " << points.size() << "\n";
  file << "id,route_id,measure,offset";
  for (const auto& a : attr_names)
    file << "," << csvQuote(a);
  file << ",x,y,geometry\n";
  for (const auto& p : points) {
    file << csvQuote(p.id) << "," << csvQuote(p.route_id) << "," << std::fixed << std::setprecision(6) << p.measure
         << "," << p.offset;
    for (const auto& a : attr_names) {
      auto it = p.attributes.find(a);
      file << "," << (it != p.attributes.end() ? csvQuote(it->second) : "");
    }
    file << "," << p.x << "," << p.y << ",POINT(" << p.x << " " << p.y << ")\n";
  }
  return true;
}

static std::vector<Point2D> applyOffset(const std::vector<Point2D>& points, double offset) {
  if (std::abs(offset) < 1e-12 || points.size() < 2)
    return points;

  std::vector<Point2D> result;
  for (size_t i = 0; i < points.size(); ++i) {
    double dx, dy;
    if (i == 0) {
      dx = points[1].x - points[0].x;
      dy = points[1].y - points[0].y;
    } else if (i == points.size() - 1) {
      dx = points[i].x - points[i - 1].x;
      dy = points[i].y - points[i - 1].y;
    } else {
      dx = (points[i].x - points[i - 1].x + points[i + 1].x - points[i].x) / 2.0;
      dy = (points[i].y - points[i - 1].y + points[i + 1].y - points[i].y) / 2.0;
    }
    double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-12) {
      result.push_back(points[i]);
      continue;
    }
    result.push_back(Point2D(points[i].x - dy / len * offset, points[i].y + dx / len * offset));
  }
  return result;
}

enum class MergeMode { Both, Concatenate, Dissolve };

// Merges the events of each group (same route and same value in every merge_by
// field) whose measure ranges touch (concatenate) and/or overlap (dissolve).
static std::vector<LinearEvent> mergeEvents(const std::vector<LinearEvent>& events,
                                            const std::vector<std::string>& merge_by, MergeMode mode) {
  const double tol = 1e-9;

  std::vector<std::string> route_order;
  std::map<std::string, std::vector<const LinearEvent*>> groups;
  std::vector<std::string> group_order;
  for (const auto& e : events) {
    if (std::find(route_order.begin(), route_order.end(), e.route_id) == route_order.end())
      route_order.push_back(e.route_id);
    std::string key = e.route_id;
    for (const auto& f : merge_by) {
      auto it = e.attributes.find(f);
      key += '\x1f' + (it == e.attributes.end() ? std::string() : it->second);
    }
    if (!groups.count(key))
      group_order.push_back(key);
    groups[key].push_back(&e);
  }

  std::vector<LinearEvent> merged;
  for (const auto& key : group_order) {
    auto list = groups[key];
    std::stable_sort(list.begin(), list.end(), [](const LinearEvent* a, const LinearEvent* b) {
      return a->from_measure < b->from_measure;
    });

    LinearEvent cur = *list[0];
    int count = 1;
    double offset_sum = cur.offset;
    auto flush = [&]() {
      cur.offset = offset_sum / count;
      merged.push_back(cur);
    };

    for (size_t i = 1; i < list.size(); ++i) {
      const LinearEvent& nx = *list[i];
      bool adjacent = std::abs(nx.from_measure - cur.to_measure) <= tol;
      bool overlap = nx.from_measure < cur.to_measure - tol;
      bool join = (mode == MergeMode::Concatenate && adjacent) || (mode == MergeMode::Dissolve && overlap) ||
                  (mode == MergeMode::Both && (adjacent || overlap));
      if (!join) {
        flush();
        cur = nx;
        count = 1;
        offset_sum = cur.offset;
        continue;
      }
      cur.to_measure = std::max(cur.to_measure, nx.to_measure);
      for (const auto& id : nx.source_ids)
        cur.source_ids.push_back(id);
      // An attribute that differs between the merged events is left empty.
      for (auto& [k, v] : cur.attributes) {
        auto it = nx.attributes.find(k);
        if (it == nx.attributes.end() || it->second != v)
          v.clear();
      }
      offset_sum += nx.offset;
      count++;
    }
    flush();
  }

  std::stable_sort(merged.begin(), merged.end(), [&](const LinearEvent& a, const LinearEvent& b) {
    auto ia = std::find(route_order.begin(), route_order.end(), a.route_id) - route_order.begin();
    auto ib = std::find(route_order.begin(), route_order.end(), b.route_id) - route_order.begin();
    if (ia != ib)
      return ia < ib;
    return a.from_measure < b.from_measure;
  });

  int n = 0;
  for (auto& e : merged)
    e.id = std::to_string(++n);
  return merged;
}

static std::string pointsToWKT(const std::vector<Point2D>& points) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(6) << "LINESTRING(";
  for (size_t i = 0; i < points.size(); ++i) {
    if (i > 0)
      os << ", ";
    os << points[i].x << " " << points[i].y;
  }
  os << ")";
  return os.str();
}

static bool writeSegmentsCSV(const std::vector<Segment>& segments, const std::vector<std::string>& attr_names,
                             const std::string& filename) {
  std::ofstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not create file: " << filename << "\n";
    return false;
  }

  file << "# LRS Segments\n# Geometry column: geometry\n# Segments: " << segments.size() << "\n";
  file << "id,route_id,from_measure,to_measure,offset,length";
  for (const auto& a : attr_names)
    file << "," << csvQuote(a);
  file << ",source_event,geometry\n";

  for (const auto& seg : segments) {
    file << csvQuote(seg.id) << "," << csvQuote(seg.route_id) << "," << std::fixed << std::setprecision(6)
         << seg.from_measure << "," << seg.to_measure << "," << seg.offset << "," << seg.length;
    for (const auto& a : attr_names) {
      auto it = seg.attributes.find(a);
      file << "," << (it != seg.attributes.end() ? csvQuote(it->second) : "");
    }
    file << "," << csvQuote(seg.source_events) << ",\"" << pointsToWKT(seg.geometry) << "\"\n";
  }
  return true;
}

static void printUsage() {
  std::cerr << "spatial_lrs_segment - Create line segments (linear events) or points (point events) from events\n\n";
  std::cerr << "Usage: spatial_lrs_segment <network.lrs> <events.csv> <output.csv> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -offset <value>        Default lateral offset (used when the event has no offset)\n";
  std::cerr << "  -merge                 Merge events of the same route that touch or overlap\n";
  std::cerr << "  -merge_by <f1,f2,...>  Merge only events with the same value in all these columns\n";
  std::cerr << "                         (implies -merge)\n";
  std::cerr << "  -mode <mode>           What to merge (implies -merge):\n";
  std::cerr << "                           concatenate  only adjacent events (end = next start)\n";
  std::cerr << "                           dissolve     only overlapping events\n";
  std::cerr << "                           both         adjacent and overlapping (default)\n";
  std::cerr << "  -keep <f1,f2,...>      Keep only these attribute columns in the output\n";
  std::cerr << "                         (splits a multi-attribute event table into separate tables)\n";
  std::cerr << "  -verbose               Show detailed information\n\n";
  std::cerr << "Events CSV must contain:\n";
  std::cerr << "  route_id and either from_measure + to_measure (linear events -> LINESTRING segments)\n";
  std::cerr << "  or measure (point events -> POINT geometry); optional: id, offset and any attribute columns\n\n";
  std::cerr << "Point events (measure column): the output has the columns id, route_id, measure, offset, the\n";
  std::cerr << "attribute columns, x, y and geometry. Events whose measure is outside the measure range of the\n";
  std::cerr << "route are reported as errors. -merge, -merge_by and -mode do not apply to point events.\n\n";
  std::cerr << "With merging, the output events are numbered 1, 2, 3... in route and measure order; the\n";
  std::cerr << "column source_event lists the original ids (separated by ;). When merged events differ in an attribute that is not in\n";
  std::cerr << "-merge_by, that attribute is left empty.\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_lrs_segment network.lrs conditions.csv segments.csv -offset 3\n";
  std::cerr << "  spatial_lrs_segment network.lrs signs.csv signs_points.csv\n";
  std::cerr << "  spatial_lrs_segment network.lrs pavement.csv concrete.csv -merge_by material -mode concatenate\n";
  std::cerr << "  spatial_lrs_segment network.lrs pavement.csv lanes.csv -keep lanes -merge_by lanes\n";
}

static std::vector<std::string> splitList(const std::string& s) {
  std::vector<std::string> out;
  for (const auto& t : split(s, ','))
    out.push_back(trim(t));
  return out;
}

int main(int argc, char* argv[]) {
  if (argc >= 2 && (std::string(argv[1]) == "-help" || std::string(argv[1]) == "-h")) {
    printUsage();
    return 0;
  }
  if (argc < 4) {
    printUsage();
    return 1;
  }

  std::string network_file = argv[1];
  std::string events_file = argv[2];
  std::string output_file = argv[3];
  double default_offset = 0;
  bool merge = false, verbose = false;
  std::vector<std::string> merge_by, keep;
  bool has_keep = false;
  MergeMode mode = MergeMode::Both;

  for (int i = 4; i < argc; ++i) {
    std::string arg = argv[i];
    try {
      if (arg == "-offset" && i + 1 < argc) {
        default_offset = std::stod(argv[++i]);
      } else if (arg == "-merge") {
        merge = true;
      } else if (arg == "-merge_by" && i + 1 < argc) {
        merge = true;
        merge_by = splitList(argv[++i]);
      } else if (arg == "-mode" && i + 1 < argc) {
        merge = true;
        std::string m = toLower(argv[++i]);
        if (m == "concatenate")
          mode = MergeMode::Concatenate;
        else if (m == "dissolve")
          mode = MergeMode::Dissolve;
        else if (m == "both")
          mode = MergeMode::Both;
        else {
          std::cerr << "Error: -mode must be concatenate, dissolve or both\n";
          return 1;
        }
      } else if (arg == "-keep" && i + 1 < argc) {
        has_keep = true;
        keep = splitList(argv[++i]);
      } else if (arg == "-verbose") {
        verbose = true;
      } else if (arg == "-help" || arg == "-h") {
        printUsage();
        return 0;
      } else {
        std::cerr << "Error: Unknown option or missing value: " << arg << "\n";
        printUsage();
        return 1;
      }
    } catch (const std::exception&) {
      std::cerr << "Error: Invalid numeric value for " << arg << "\n";
      return 1;
    }
  }

  std::cout << "========================================\n";
  std::cout << "  LRS SEGMENT CREATION\n";
  std::cout << "========================================\n\n";
  std::cout << "Network: " << network_file << "\n";
  std::cout << "Events: " << events_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Default offset: " << default_offset << "\n";
  std::cout << "Merge: " << (merge ? "yes" : "no");
  if (merge) {
    std::cout << " (" << (mode == MergeMode::Both ? "concatenate + dissolve"
                                                   : mode == MergeMode::Concatenate ? "concatenate" : "dissolve");
    if (!merge_by.empty()) {
      std::cout << ", same value of";
      for (const auto& f : merge_by)
        std::cout << " " << f;
    }
    std::cout << ")";
  }
  std::cout << "\n\n";

  LRSNetwork network;
  if (!loadLRSNetwork(network_file, network))
    return 1;
  if (network.routes.empty()) {
    std::cerr << "Error: No routes found in network file\n";
    return 1;
  }
  std::cout << "Routes loaded: " << network.routes.size() << "\n";

  std::vector<LinearEvent> events;
  std::vector<std::string> attr_names;
  int skipped = 0;
  bool point_mode = false;
  if (!readLinearEvents(events_file, events, attr_names, skipped, point_mode))
    return 1;
  if (point_mode && merge) {
    std::cerr << "Error: -merge, -merge_by and -mode only apply to linear events (from_measure/to_measure)\n";
    return 1;
  }
  std::cout << "Event type: " << (point_mode ? "point (measure)" : "linear (from_measure/to_measure)") << "\n";
  if (events.empty()) {
    std::cerr << "Error: No events found\n";
    return 1;
  }
  std::cout << "Events loaded: " << events.size() << "\n";
  if (skipped > 0)
    std::cout << "Events skipped (non-numeric measures): " << skipped << "\n";

  for (const auto& f : merge_by)
    if (std::find(attr_names.begin(), attr_names.end(), f) == attr_names.end()) {
      std::cerr << "Error: -merge_by column '" << f << "' is not a column of " << events_file << "\n";
      return 1;
    }
  for (const auto& f : keep)
    if (std::find(attr_names.begin(), attr_names.end(), f) == attr_names.end()) {
      std::cerr << "Error: -keep column '" << f << "' is not an attribute column of " << events_file << "\n";
      return 1;
    }

  if (point_mode) {
    std::vector<std::string> out_pt_attrs = has_keep ? keep : attr_names;
    std::vector<PointResult> results;
    int errors_pt = 0;
    for (const auto& event : events) {
      auto it = network.route_index.find(event.route_id);
      if (it == network.route_index.end()) {
        errors_pt++;
        std::cerr << "  Event " << event.id << ": route '" << event.route_id << "' not in the network\n";
        continue;
      }
      const LRSRoute& route = network.routes[it->second];
      Point2D pos;
      double dx = 0, dy = 0;
      if (!locateMeasure(route, event.from_measure, pos, dx, dy)) {
        errors_pt++;
        std::cerr << "  Event " << event.id << ": measure " << event.from_measure << " is outside the measure range of route "
                  << event.route_id << "\n";
        continue;
      }
      PointResult r;
      r.id = event.id;
      r.route_id = event.route_id;
      r.measure = event.from_measure;
      r.offset = (event.offset != 0) ? event.offset : default_offset;
      r.x = pos.x - dy * r.offset;
      r.y = pos.y + dx * r.offset;
      r.attributes = event.attributes;
      results.push_back(r);
      if (verbose)
        std::cout << "  " << r.id << " -> route " << r.route_id << ", M: " << std::fixed << std::setprecision(4)
                  << r.measure << ", point (" << r.x << ", " << r.y << ")\n";
    }
    if (!writePointsCSV(results, out_pt_attrs, output_file))
      return 1;
    std::cout << "\n========================================\n";
    std::cout << "  POINT CREATION COMPLETE\n";
    std::cout << "========================================\n";
    std::cout << "Events processed: " << events.size() << "\n";
    std::cout << "Points created: " << results.size() << "\n";
    std::cout << "Errors: " << errors_pt << "\n";
    std::cout << "Output written to: " << output_file << "\n";
    return 0;
  }

  std::vector<LinearEvent> valid_events;
  for (const auto& e : events)
    if (e.valid)
      valid_events.push_back(e);
  std::cout << "Valid events: " << valid_events.size() << "\n";
  if (valid_events.empty()) {
    std::cerr << "Error: No valid events found (from_measure must not exceed to_measure)\n";
    return 1;
  }

  std::vector<LinearEvent> processed = valid_events;
  if (merge) {
    processed = mergeEvents(valid_events, merge_by, mode);
    std::cout << "Events after merging: " << processed.size() << "\n";
  }

  std::vector<std::string> out_attrs = has_keep ? keep : attr_names;

  std::vector<Segment> segments;
  int errors = 0, clipped = 0;
  for (const auto& event : processed) {
    auto it = network.route_index.find(event.route_id);
    if (it == network.route_index.end()) {
      errors++;
      std::cerr << "  Event " << event.id << ": route '" << event.route_id << "' not in the network\n";
      continue;
    }
    const LRSRoute& route = network.routes[it->second];
    std::vector<Point2D> geom = extractSegmentGeometry(route, event.from_measure, event.to_measure);
    if (geom.size() < 2) {
      errors++;
      std::cerr << "  Event " << event.id << ": measures " << event.from_measure << " - " << event.to_measure
                << " give no segment on route " << event.route_id << "\n";
      continue;
    }

    if (event.from_measure < route.vertices.front().measure - 1e-9 ||
        event.to_measure > route.vertices.back().measure + 1e-9)
      clipped++;

    Segment seg;
    seg.id = event.id;
    seg.route_id = event.route_id;
    seg.from_measure = event.from_measure;
    seg.to_measure = event.to_measure;
    seg.length = 0;
    for (size_t i = 0; i + 1 < geom.size(); ++i)
      seg.length += geom[i].distanceTo(geom[i + 1]);
    seg.offset = (event.offset != 0) ? event.offset : default_offset;
    seg.geometry = applyOffset(geom, seg.offset);
    seg.attributes = event.attributes;
    for (size_t k = 0; k < event.source_ids.size(); ++k)
      seg.source_events += (k ? ";" : "") + event.source_ids[k];
    segments.push_back(seg);

    if (verbose)
      std::cout << "  " << seg.id << " -> route " << seg.route_id << ", M: " << std::fixed << std::setprecision(4)
                << seg.from_measure << " - " << seg.to_measure << ", length " << seg.length << ", "
                << seg.geometry.size() << " vertices\n";
  }

  if (!writeSegmentsCSV(segments, out_attrs, output_file))
    return 1;

  std::cout << "\n========================================\n";
  std::cout << "  SEGMENT CREATION COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Events processed: " << processed.size() << "\n";
  std::cout << "Segments created: " << segments.size() << "\n";
  if (clipped > 0)
    std::cout << "Segments clipped to the measure range of the route: " << clipped << "\n";
  std::cout << "Errors: " << errors << "\n";
  std::cout << "Output written to: " << output_file << "\n";
  return 0;
}
