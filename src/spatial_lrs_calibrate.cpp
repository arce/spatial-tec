#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../include/core/spatial_csv.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

// A projected calibration point closer than this to an existing vertex reuses
// that vertex instead of creating a new one.
static const double VERTEX_SNAP = 1e-6;

struct Vertex {
  double x, y, m;
};

struct Route {
  std::string route_id;
  std::vector<Vertex> vertices;
  std::string total_length;  // total_length column, copied as text
  bool calibrated = false;
  int calib_points = 0;
};

struct Network {
  std::vector<Route> routes;
  std::string crs = "EPSG:4326";
  std::vector<std::string> attribute_lines;  // raw @ROUTE_ATTRIBUTES section
};

struct CalibPoint {
  int input_row;  // 1-based data row in the points file
  std::string route_id;
  double x, y, measure;
};

struct Anchor {
  size_t vertex;  // index in the (extended) vertex list
  double s;       // distance along the route
  double m;       // measure
};

void printUsage() {
  std::cerr << "spatial_lrs_calibrate - Calibrate LRS route measures using points of known measure\n\n";
  std::cerr << "Usage: spatial_lrs_calibrate <network.lrs> <points.csv> <output.lrs> [options]\n\n";
  std::cerr << "The points file needs a route column, a measure column and the position of each\n";
  std::cerr << "point, either as a 'geometry' column (POINT in WKT) or as 'x' and 'y' columns.\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -max_dist <value>   Maximum distance from a point to its route (default: 1.0);\n";
  std::cerr << "                      farther points are ignored with a warning\n";
  std::cerr << "  -route_id <col>     Name of the route column in the points file (default: route_id)\n";
  std::cerr << "  -measure <col>      Name of the measure column in the points file (default: measure)\n";
  std::cerr << "  -verbose            Show the result for every route\n\n";
  std::cerr << "How it works:\n";
  std::cerr << "  Each point is projected onto its route and a vertex is created there (unless it\n";
  std::cerr << "  coincides with an existing vertex) with the measure of the point. The other\n";
  std::cerr << "  vertices get measures interpolated by distance along the route between the two\n";
  std::cerr << "  nearest calibration points; before the first and after the last point the slope\n";
  std::cerr << "  of the nearest calibrated interval is used (one unit of measure per unit of\n";
  std::cerr << "  distance when the route has a single point).\n\n";
  std::cerr << "  The measures of the points of a route must increase along the route. A route\n";
  std::cerr << "  that violates this is copied unchanged and reported; routes without points are\n";
  std::cerr << "  copied unchanged.\n\n";
  std::cerr << "Exit code: 0 = OK, 1 = usage/IO error or some route could not be calibrated.\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_lrs_calibrate roads.lrs mileposts.csv roads_km.lrs -max_dist 5\n";
  std::cerr << "  spatial_lrs_calibrate roads.lrs mileposts.csv roads_km.lrs -measure km -verbose\n";
}

static std::string fmt(double v, int prec) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(prec) << v;
  return os.str();
}

static std::string unquote(std::string s) {
  s = trim(s);
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
    s = s.substr(1, s.size() - 2);
  return s;
}

// Reads the numbers x y [m] of every vertex inside the outermost parentheses.
static std::vector<Vertex> parseVertices(const std::string& geom) {
  std::vector<Vertex> out;
  size_t start = geom.find('(');
  size_t end = geom.rfind(')');
  if (start == std::string::npos || end == std::string::npos || end <= start)
    return out;
  for (const auto& pt : split(geom.substr(start + 1, end - start - 1), ',')) {
    std::istringstream is(trim(pt));
    double x, y, m = 0;
    if (!(is >> x >> y))
      continue;
    is >> m;
    out.push_back({x, y, m});
  }
  return out;
}

static bool loadNetwork(const std::string& filename, Network& net) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file: " << filename << "\n";
    return false;
  }

  std::map<std::string, int> calibrated_points;
  std::string line, section;
  while (std::getline(file, line)) {
    std::string t = trim(line);
    if (t.empty())
      continue;
    if (t[0] == '#') {
      std::string body = trim(t.substr(1));
      if (body.rfind("CRS:", 0) == 0)
        net.crs = trim(body.substr(4));
      continue;
    }
    if (t[0] == '@') {
      section = t.substr(1);
      continue;
    }

    if (section == "ROUTES") {
      auto parts = splitCSV(t);
      if (!parts.empty() && toLower(parts[0]) == "route_id")
        continue;
      if (parts.size() < 4)
        continue;
      Route r;
      r.route_id = parts[0];
      r.total_length = parts[2];
      r.vertices = parseVertices(unquote(parts[3]));
      if (r.vertices.size() >= 2)
        net.routes.push_back(r);
    } else if (section == "ROUTE_ATTRIBUTES") {
      net.attribute_lines.push_back(t);
    } else if (section == "CALIBRATED") {
      auto parts = splitCSV(t);
      if (parts.size() >= 2 && toLower(parts[0]) != "route_id") {
        try {
          calibrated_points[parts[0]] = std::stoi(parts[1]);
        } catch (...) {
        }
      }
    }
  }

  for (auto& r : net.routes) {
    auto it = calibrated_points.find(r.route_id);
    if (it != calibrated_points.end()) {
      r.calibrated = true;
      r.calib_points = it->second;
    }
  }
  return true;
}

static bool readPoints(const std::string& filename, const std::string& route_col, const std::string& measure_col,
                       std::vector<CalibPoint>& points, int& skipped) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file: " << filename << "\n";
    return false;
  }

  std::string line;
  std::vector<std::string> headers;
  int c_route = -1, c_measure = -1, c_geom = -1, c_x = -1, c_y = -1;
  bool have_header = false;
  int row = 0;
  skipped = 0;

  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#')
      continue;
    auto v = splitCSV(line);

    if (!have_header) {
      headers = v;
      for (size_t i = 0; i < headers.size(); ++i) {
        std::string h = toLower(trim(headers[i]));
        if (h == toLower(route_col))
          c_route = (int)i;
        else if (h == toLower(measure_col))
          c_measure = (int)i;
        else if (h == "geometry" || h == "geom" || h == "wkt")
          c_geom = (int)i;
        else if (h == "x")
          c_x = (int)i;
        else if (h == "y")
          c_y = (int)i;
      }
      if (c_route < 0) {
        std::cerr << "Error: Route column '" << route_col << "' not found in " << filename << " (use -route_id)\n";
        return false;
      }
      if (c_measure < 0) {
        std::cerr << "Error: Measure column '" << measure_col << "' not found in " << filename << " (use -measure)\n";
        return false;
      }
      if (c_geom < 0 && (c_x < 0 || c_y < 0)) {
        std::cerr << "Error: " << filename << " needs a 'geometry' column (POINT) or 'x' and 'y' columns\n";
        return false;
      }
      have_header = true;
      continue;
    }

    ++row;
    CalibPoint p;
    p.input_row = row;
    bool ok = (int)v.size() > std::max(c_route, c_measure);
    if (ok) {
      p.route_id = trim(v[c_route]);
      try {
        p.measure = std::stod(trim(v[c_measure]));
        if (c_geom >= 0 && c_geom < (int)v.size()) {
          auto vs = parseVertices(unquote(v[c_geom]));
          if (vs.empty())
            ok = false;
          else {
            p.x = vs[0].x;
            p.y = vs[0].y;
          }
        } else if (c_x >= 0 && c_y >= 0 && c_x < (int)v.size() && c_y < (int)v.size()) {
          p.x = std::stod(trim(v[c_x]));
          p.y = std::stod(trim(v[c_y]));
        } else {
          ok = false;
        }
      } catch (...) {
        ok = false;
      }
    }
    if (!ok) {
      std::cerr << "Warning: row " << row << " of " << filename << " is not valid (route, measure or position); ignored\n";
      ++skipped;
      continue;
    }
    points.push_back(p);
  }

  if (!have_header) {
    std::cerr << "Error: " << filename << " is empty\n";
    return false;
  }
  return true;
}

struct Projection {
  size_t segment = 0;
  double t = 0;
  double px = 0, py = 0;
  double dist = 0;
  double s = 0;  // distance along the route
};

static Projection project(const std::vector<Vertex>& v, const std::vector<double>& cum, double x, double y) {
  Projection best;
  best.dist = 1e300;
  for (size_t i = 0; i + 1 < v.size(); ++i) {
    double dx = v[i + 1].x - v[i].x, dy = v[i + 1].y - v[i].y;
    double len2 = dx * dx + dy * dy;
    double t = len2 < 1e-24 ? 0.0 : ((x - v[i].x) * dx + (y - v[i].y) * dy) / len2;
    t = std::max(0.0, std::min(1.0, t));
    double px = v[i].x + t * dx, py = v[i].y + t * dy;
    double d = std::sqrt((x - px) * (x - px) + (y - py) * (y - py));
    if (d < best.dist) {
      best.segment = i;
      best.t = t;
      best.px = px;
      best.py = py;
      best.dist = d;
      best.s = cum[i] + t * std::sqrt(len2);
    }
  }
  return best;
}

static std::vector<double> cumulative(const std::vector<Vertex>& v) {
  std::vector<double> cum(v.size(), 0.0);
  for (size_t i = 1; i < v.size(); ++i)
    cum[i] = cum[i - 1] + std::sqrt((v[i].x - v[i - 1].x) * (v[i].x - v[i - 1].x) +
                                    (v[i].y - v[i - 1].y) * (v[i].y - v[i - 1].y));
  return cum;
}

static double geometryLength(const std::vector<Vertex>& v) {
  return cumulative(v).back();
}

static void writeNetwork(const Network& net, const std::string& filename) {
  std::ofstream file(filename);
  file << "# LRS Network file\n";
  file << "# CRS: " << net.crs << "\n";
  file << "# Routes: " << net.routes.size() << "\n";
  file << "# Format: routes|nodes\n\n";

  file << "@ROUTES\n";
  file << "route_id,vertex_count,total_length,geometry\n";
  for (const auto& r : net.routes) {
    std::string wkt = "LINESTRING M(";
    for (size_t i = 0; i < r.vertices.size(); ++i) {
      if (i > 0)
        wkt += ", ";
      wkt += fmt(r.vertices[i].x, 6) + " " + fmt(r.vertices[i].y, 6) + " " + fmt(r.vertices[i].m, 6);
    }
    wkt += ")";
    file << r.route_id << "," << r.vertices.size() << "," << r.total_length << ",\"" << wkt << "\"\n";
  }
  file << "\n@NODES\nnode_id,route_id,x,y,measure\n";
  int node_id = 0;
  for (const auto& r : net.routes)
    for (const auto& v : r.vertices)
      file << node_id++ << "," << r.route_id << "," << fmt(v.x, 6) << "," << fmt(v.y, 6) << "," << fmt(v.m, 6) << "\n";

  file << "\n@ROUTE_ATTRIBUTES\n";
  if (net.attribute_lines.empty())
    file << "route_id\n";
  for (const auto& l : net.attribute_lines)
    file << l << "\n";

  file << "\n@CALIBRATED\nroute_id,points\n";
  for (const auto& r : net.routes)
    if (r.calibrated)
      file << r.route_id << "," << r.calib_points << "\n";
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

  std::string network_file = argv[1], points_file = argv[2], output_file = argv[3];
  double max_dist = 1.0;
  std::string route_col = "route_id", measure_col = "measure";
  bool verbose = false;

  for (int i = 4; i < argc; ++i) {
    std::string arg = argv[i];
    try {
      if (arg == "-max_dist" && i + 1 < argc) {
        max_dist = std::stod(argv[++i]);
      } else if (arg == "-route_id" && i + 1 < argc) {
        route_col = argv[++i];
      } else if (arg == "-measure" && i + 1 < argc) {
        measure_col = argv[++i];
      } else if (arg == "-verbose") {
        verbose = true;
      } else if (arg == "-help" || arg == "-h") {
        printUsage();
        return 0;
      } else {
        std::cerr << "Error: Unknown option: " << arg << "\n";
        printUsage();
        return 1;
      }
    } catch (...) {
      std::cerr << "Error: Invalid value for " << arg << "\n";
      return 1;
    }
  }
  if (max_dist < 0) {
    std::cerr << "Error: -max_dist must not be negative\n";
    return 1;
  }

  Network net;
  if (!loadNetwork(network_file, net))
    return 1;
  if (net.routes.empty()) {
    std::cerr << "Error: No routes found in " << network_file << " (is it a .lrs file created by spatial_lrs_create?)\n";
    return 1;
  }

  std::vector<CalibPoint> points;
  int skipped = 0;
  if (!readPoints(points_file, route_col, measure_col, points, skipped))
    return 1;

  std::map<std::string, size_t> index;
  for (size_t i = 0; i < net.routes.size(); ++i)
    index[net.routes[i].route_id] = i;

  std::map<std::string, std::vector<CalibPoint>> by_route;
  for (const auto& p : points) {
    if (!index.count(p.route_id)) {
      std::cerr << "Warning: point " << p.input_row << " refers to route '" << p.route_id
                << "', which is not in the network; ignored\n";
      continue;
    }
    by_route[p.route_id].push_back(p);
  }

  if (verbose)
    std::cout << "Calibrating " << network_file << " with " << points.size() << " points (max_dist " << max_dist << ")\n";

  int routes_ok = 0, routes_failed = 0, points_used = 0, vertices_added = 0;

  for (auto& r : net.routes) {
    auto it = by_route.find(r.route_id);
    if (it == by_route.end()) {
      if (verbose)
        std::cout << "  Route " << r.route_id << ": no points, copied unchanged\n";
      continue;
    }

    std::vector<Vertex> verts = r.vertices;
    std::vector<double> cum = cumulative(verts);

    struct Proj {
      CalibPoint p;
      Projection pr;
    };
    std::vector<Proj> projs;
    for (const auto& p : it->second) {
      Projection pr = project(verts, cum, p.x, p.y);
      if (pr.dist > max_dist) {
        std::cerr << "Warning: point " << p.input_row << " (route " << r.route_id << ") is " << fmt(pr.dist, 3)
                  << " from its route, more than -max_dist " << max_dist << "; ignored\n";
        continue;
      }
      projs.push_back({p, pr});
    }

    if (projs.empty()) {
      std::cerr << "Error: route " << r.route_id << ": no calibration point within -max_dist; route not calibrated\n";
      ++routes_failed;
      continue;
    }

    std::stable_sort(projs.begin(), projs.end(), [](const Proj& a, const Proj& b) { return a.pr.s < b.pr.s; });

    bool bad = false;
    for (size_t k = 1; k < projs.size() && !bad; ++k) {
      if (projs[k].p.measure <= projs[k - 1].p.measure) {
        std::cerr << "Error: route " << r.route_id << ": measure of point " << projs[k].p.input_row << " ("
                  << projs[k].p.measure << ") is not greater than that of point " << projs[k - 1].p.input_row << " ("
                  << projs[k - 1].p.measure << ") although it lies farther along the route; route not calibrated\n";
        bad = true;
      } else if (projs[k].pr.s - projs[k - 1].pr.s < VERTEX_SNAP) {
        std::cerr << "Error: route " << r.route_id << ": points " << projs[k - 1].p.input_row << " and "
                  << projs[k].p.input_row << " project to the same location; route not calibrated\n";
        bad = true;
      }
    }
    if (bad) {
      ++routes_failed;
      continue;
    }

    // Insert a vertex for each point (unless it coincides with an existing one).
    // Points are processed from the last to the first so that earlier segment
    // indices stay valid.
    std::vector<size_t> anchor_vertex(projs.size());
    int added_here = 0;
    for (size_t k = projs.size(); k-- > 0;) {
      const Projection& pr = projs[k].pr;
      size_t seg = pr.segment;
      auto d2 = [&](const Vertex& v) { return std::sqrt((v.x - pr.px) * (v.x - pr.px) + (v.y - pr.py) * (v.y - pr.py)); };
      if (d2(verts[seg]) < VERTEX_SNAP) {
        anchor_vertex[k] = seg;
      } else if (d2(verts[seg + 1]) < VERTEX_SNAP) {
        anchor_vertex[k] = seg + 1;
      } else {
        verts.insert(verts.begin() + seg + 1, Vertex{pr.px, pr.py, 0});
        anchor_vertex[k] = seg + 1;
        ++added_here;
        // later anchors (higher vertex indices) shift by one
        for (size_t j = k + 1; j < projs.size(); ++j)
          ++anchor_vertex[j];
      }
    }

    cum = cumulative(verts);
    std::vector<Anchor> anchors;
    for (size_t k = 0; k < projs.size(); ++k)
      anchors.push_back({anchor_vertex[k], cum[anchor_vertex[k]], projs[k].p.measure});

    for (size_t i = 0; i < verts.size(); ++i) {
      double s = cum[i];
      double m;
      if (anchors.size() == 1) {
        m = anchors[0].m + (s - anchors[0].s);
      } else if (s <= anchors.front().s) {
        double slope = (anchors[1].m - anchors[0].m) / (anchors[1].s - anchors[0].s);
        m = anchors[0].m + slope * (s - anchors[0].s);
      } else if (s >= anchors.back().s) {
        const Anchor& a = anchors[anchors.size() - 2];
        const Anchor& b = anchors.back();
        double slope = (b.m - a.m) / (b.s - a.s);
        m = b.m + slope * (s - b.s);
      } else {
        size_t k = 0;
        while (k + 2 < anchors.size() && s > anchors[k + 1].s)
          ++k;
        const Anchor& a = anchors[k];
        const Anchor& b = anchors[k + 1];
        m = a.m + (b.m - a.m) * (s - a.s) / (b.s - a.s);
      }
      verts[i].m = m;
    }
    for (const auto& a : anchors)
      verts[a.vertex].m = a.m;  // exact measure at the calibration points

    r.vertices = verts;
    r.total_length = fmt(geometryLength(verts), 6);
    r.calibrated = true;
    r.calib_points = (int)projs.size();
    ++routes_ok;
    points_used += (int)projs.size();
    vertices_added += added_here;

    if (verbose)
      std::cout << "  Route " << r.route_id << ": " << projs.size() << " points, " << verts.size()
                << " vertices, M-range " << fmt(verts.front().m, 3) << " - " << fmt(verts.back().m, 3) << "\n";
  }

  if (routes_ok == 0) {
    std::cerr << "Error: No route was calibrated; output not written\n";
    return 1;
  }

  writeNetwork(net, output_file);
  std::ifstream check(output_file);
  if (!check.good()) {
    std::cerr << "Error: Could not create file: " << output_file << "\n";
    return 1;
  }

  std::cout << "\nRoutes calibrated: " << routes_ok << "\n";
  if (routes_failed > 0)
    std::cout << "Routes not calibrated: " << routes_failed << "\n";
  std::cout << "Calibration points used: " << points_used << "\n";
  std::cout << "Vertices added: " << vertices_added << "\n";
  std::cout << "Output written to: " << output_file << "\n";
  return routes_failed > 0 ? 1 : 0;
}
