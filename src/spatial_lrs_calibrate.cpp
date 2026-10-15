#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../include/core/spatial_string.hpp"

struct Vertex {
  double x = 0, y = 0, m = 0;
};

struct Route {
  std::string id;
  std::vector<Vertex> v;
  double declared_length = 0;
  bool calibrated = false;
};

struct NetworkFile {
  std::string crs = "EPSG:4326";
  std::vector<Route> routes;
  std::vector<std::string> route_attribute_lines;  // @ROUTE_ATTRIBUTES section, copied as is
};

struct CalPoint {
  std::string id;
  double x = 0, y = 0;
  double measure = 0;
  // filled when projected onto the route
  double s = 0;   // distance along the route geometry
  double sx = 0, sy = 0;
  double dist = 0;
};

static bool readNetwork(const std::string& filename, NetworkFile& net) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file: " << filename << "\n";
    return false;
  }
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
    if (section == "ROUTE_ATTRIBUTES") {
      net.route_attribute_lines.push_back(t);
      continue;
    }
    if (section != "ROUTES")
      continue;

    auto parts = splitCSV(t);
    if (parts.size() < 4 || toLower(parts[0]) == "route_id")
      continue;
    std::string geom = parts[3];
    if (geom.size() >= 2 && geom.front() == '"' && geom.back() == '"')
      geom = geom.substr(1, geom.length() - 2);
    size_t a = geom.find('('), b = geom.rfind(')');
    if (a == std::string::npos || b == std::string::npos || b <= a)
      continue;

    Route r;
    r.id = parts[0];
    try {
      r.declared_length = std::stod(parts[2]);
    } catch (...) {
    }
    r.calibrated = parts.size() >= 5 && trim(parts[4]) == "1";
    for (const auto& pt : split(geom.substr(a + 1, b - a - 1), ',')) {
      auto c = split(trim(pt), ' ');
      if (c.size() >= 3) {
        try {
          r.v.push_back({std::stod(c[0]), std::stod(c[1]), std::stod(c[2])});
        } catch (...) {
        }
      }
    }
    if (!r.v.empty())
      net.routes.push_back(r);
  }
  return true;
}

static bool readPoints(const std::string& filename, const std::string& route_col, const std::string& measure_col,
                       std::map<std::string, std::vector<CalPoint>>& by_route, int& total, int& invalid) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file: " << filename << "\n";
    return false;
  }
  std::string line;
  bool is_header = true;
  int col_id = -1, col_route = -1, col_measure = -1, col_geom = -1, col_x = -1, col_y = -1;
  total = invalid = 0;

  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#')
      continue;
    auto values = splitCSV(line);
    if (is_header) {
      for (size_t i = 0; i < values.size(); ++i) {
        std::string h = toLower(trim(values[i]));
        if (h == "id")
          col_id = (int)i;
        else if (h == toLower(route_col))
          col_route = (int)i;
        else if (h == toLower(measure_col))
          col_measure = (int)i;
        else if (h == "geometry" || h == "geom" || h == "wkt")
          col_geom = (int)i;
        else if (h == "x" || h == "longitude" || h == "lon")
          col_x = (int)i;
        else if (h == "y" || h == "latitude" || h == "lat")
          col_y = (int)i;
      }
      if (col_route < 0 || col_measure < 0) {
        std::cerr << "Error: The points file needs the columns '" << route_col << "' and '" << measure_col
                  << "' (see -route_id and -measure)\n";
        return false;
      }
      if (col_geom < 0 && (col_x < 0 || col_y < 0)) {
        std::cerr << "Error: The points file needs a geometry column (POINT WKT) or x,y columns\n";
        return false;
      }
      is_header = false;
      continue;
    }

    total++;
    auto get = [&](int c) -> std::string {
      return (c >= 0 && c < (int)values.size()) ? trim(values[c]) : "";
    };
    CalPoint p;
    p.id = col_id >= 0 ? get(col_id) : std::to_string(total);
    bool ok = isNumber(get(col_measure));
    if (ok)
      p.measure = std::stod(get(col_measure));
    bool have_xy = false;
    if (col_geom >= 0) {
      std::string g = get(col_geom);
      size_t a = g.find('('), b = g.rfind(')');
      if (a != std::string::npos && b != std::string::npos && b > a) {
        auto c = split(trim(g.substr(a + 1, b - a - 1)), ' ');
        if (c.size() >= 2 && isNumber(c[0]) && isNumber(c[1])) {
          p.x = std::stod(c[0]);
          p.y = std::stod(c[1]);
          have_xy = true;
        }
      }
    }
    if (!have_xy && col_x >= 0 && col_y >= 0 && isNumber(get(col_x)) && isNumber(get(col_y))) {
      p.x = std::stod(get(col_x));
      p.y = std::stod(get(col_y));
      have_xy = true;
    }
    if (!ok || !have_xy || get(col_route).empty()) {
      invalid++;
      continue;
    }
    by_route[get(col_route)].push_back(p);
  }
  return !is_header;
}

static void printUsage() {
  std::cerr << "spatial_lrs_calibrate - Calibrate the measures of an LRS network with points of known measure\n\n";
  std::cerr << "Usage: spatial_lrs_calibrate <network.lrs> <points.csv> <output.lrs> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -max_dist <value>   Maximum distance from a calibration point to its route (default: 1.0)\n";
  std::cerr << "  -route_id <col>     Column with the route identifier (default: route_id)\n";
  std::cerr << "  -measure <col>      Column with the known measure of each point (default: measure)\n";
  std::cerr << "  -verbose            Show the result for every route\n\n";
  std::cerr << "Points CSV must contain:\n";
  std::cerr << "  route_id, measure, and geometry (POINT WKT) or x,y columns; optional: id\n\n";
  std::cerr << "For every route with calibration points, a vertex is created where each point projects\n";
  std::cerr << "onto the route and receives the measure of the point. The measures of the other vertices\n";
  std::cerr << "are interpolated between calibration points (proportionally to the distance along the\n";
  std::cerr << "route) and extrapolated before the first and after the last point, using the slope of the\n";
  std::cerr << "nearest calibrated interval (or 1 measure unit per distance unit with a single point).\n";
  std::cerr << "Calibration points of a route must have increasing measures along its direction.\n";
  std::cerr << "Routes without calibration points are copied unchanged.\n\n";
  std::cerr << "Example:\n";
  std::cerr << "  spatial_lrs_calibrate roads.lrs milestones.csv roads_km.lrs\n";
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
  std::cout << "  LRS CALIBRATION\n";
  std::cout << "========================================\n\n";
  std::cout << "Network: " << network_file << "\n";
  std::cout << "Calibration points: " << points_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Max distance: " << max_dist << "\n\n";

  NetworkFile net;
  if (!readNetwork(network_file, net))
    return 1;
  if (net.routes.empty()) {
    std::cerr << "Error: No routes found in " << network_file << "\n";
    return 1;
  }

  std::map<std::string, std::vector<CalPoint>> by_route;
  int total = 0, invalid = 0;
  if (!readPoints(points_file, route_col, measure_col, by_route, total, invalid))
    return 1;
  std::cout << "Routes loaded: " << net.routes.size() << "\n";
  std::cout << "Calibration points loaded: " << total << "\n";
  if (invalid > 0)
    std::cout << "Points skipped (no route, measure or coordinates): " << invalid << "\n";

  int calibrated_routes = 0, failed_routes = 0, points_used = 0, points_rejected = 0, vertices_added = 0;

  for (auto& route : net.routes) {
    auto it = by_route.find(route.id);
    if (it == by_route.end())
      continue;

    auto& pts = it->second;
    const auto& v = route.v;
    if (v.size() < 2) {
      std::cerr << "  Route " << route.id << ": fewer than 2 vertices, not calibrated\n";
      failed_routes++;
      continue;
    }

    std::vector<double> cum(v.size(), 0.0);
    for (size_t i = 1; i < v.size(); ++i)
      cum[i] = cum[i - 1] + std::hypot(v[i].x - v[i - 1].x, v[i].y - v[i - 1].y);

    // Project every calibration point onto the route.
    std::vector<CalPoint> ok;
    for (auto p : pts) {
      double best = 1e300;
      for (size_t i = 0; i + 1 < v.size(); ++i) {
        double dx = v[i + 1].x - v[i].x, dy = v[i + 1].y - v[i].y;
        double len2 = dx * dx + dy * dy;
        double u = len2 > 1e-24 ? ((p.x - v[i].x) * dx + (p.y - v[i].y) * dy) / len2 : 0;
        u = std::max(0.0, std::min(1.0, u));
        double qx = v[i].x + u * dx, qy = v[i].y + u * dy;
        double d = std::hypot(p.x - qx, p.y - qy);
        if (d < best) {
          best = d;
          p.s = cum[i] + u * std::sqrt(len2);
          p.sx = qx;
          p.sy = qy;
        }
      }
      p.dist = best;
      if (best > max_dist) {
        points_rejected++;
        std::cerr << "  Route " << route.id << ": point " << p.id << " is " << std::fixed << std::setprecision(3)
                  << best << " from the route (more than -max_dist), ignored\n";
        continue;
      }
      ok.push_back(p);
    }
    if (ok.empty()) {
      std::cerr << "  Route " << route.id << ": no calibration point within -max_dist, not calibrated\n";
      failed_routes++;
      continue;
    }

    std::stable_sort(ok.begin(), ok.end(), [](const CalPoint& a, const CalPoint& b) { return a.s < b.s; });
    // Remove exact duplicates; conflicting duplicates make the route fail.
    std::vector<CalPoint> cal;
    bool bad = false;
    for (const auto& p : ok) {
      if (!cal.empty() && std::abs(p.s - cal.back().s) < 1e-9) {
        if (std::abs(p.measure - cal.back().measure) > 1e-9) {
          std::cerr << "  Route " << route.id << ": points " << cal.back().id << " and " << p.id
                    << " are at the same place but have different measures, not calibrated\n";
          bad = true;
          break;
        }
        continue;
      }
      if (!cal.empty() && p.measure <= cal.back().measure) {
        std::cerr << "  Route " << route.id << ": measure of point " << p.id << " (" << p.measure
                  << ") is not greater than that of point " << cal.back().id << " (" << cal.back().measure
                  << ") although it lies farther along the route, not calibrated\n";
        bad = true;
        break;
      }
      cal.push_back(p);
    }
    if (bad) {
      failed_routes++;
      continue;
    }

    // Vertices of the calibrated route: original vertices plus one per calibration point.
    struct NewV {
      double s, x, y;
      bool is_cal;
      double m;
    };
    std::vector<NewV> nv;
    for (size_t i = 0; i < v.size(); ++i)
      nv.push_back({cum[i], v[i].x, v[i].y, false, 0});
    for (const auto& c : cal) {
      bool merged = false;
      for (auto& n : nv)
        if (std::abs(n.s - c.s) < 1e-9) {
          n.is_cal = true;
          n.m = c.measure;
          merged = true;
          break;
        }
      if (!merged) {
        nv.push_back({c.s, c.sx, c.sy, true, c.measure});
        vertices_added++;
      }
    }
    std::sort(nv.begin(), nv.end(), [](const NewV& a, const NewV& b) { return a.s < b.s; });

    auto slope = [&](size_t k) {  // slope of the interval [cal[k], cal[k+1]]
      return (cal[k + 1].measure - cal[k].measure) / (cal[k + 1].s - cal[k].s);
    };

    bool negative = false;
    for (auto& n : nv) {
      if (n.is_cal)
        continue;
      if (n.s < cal.front().s) {
        double sl = cal.size() > 1 ? slope(0) : 1.0;
        n.m = cal.front().measure - sl * (cal.front().s - n.s);
      } else if (n.s > cal.back().s) {
        double sl = cal.size() > 1 ? slope(cal.size() - 2) : 1.0;
        n.m = cal.back().measure + sl * (n.s - cal.back().s);
      } else {
        for (size_t k = 0; k + 1 < cal.size(); ++k)
          if (n.s >= cal[k].s && n.s <= cal[k + 1].s) {
            n.m = cal[k].measure + slope(k) * (n.s - cal[k].s);
            break;
          }
      }
      if (n.m < 0)
        negative = true;
    }

    route.v.clear();
    for (const auto& n : nv)
      route.v.push_back({n.x, n.y, n.m});
    route.calibrated = true;
    calibrated_routes++;
    points_used += (int)cal.size();

    if (negative)
      std::cerr << "  Route " << route.id << ": warning, extrapolation gives negative measures at the start\n";
    if (verbose)
      std::cout << "  Route " << route.id << ": " << cal.size() << " points, " << route.v.size()
                << " vertices, M-range " << std::fixed << std::setprecision(3) << route.v.front().m << " - "
                << route.v.back().m << "\n";
  }

  for (const auto& kv : by_route) {
    bool known = false;
    for (const auto& r : net.routes)
      if (r.id == kv.first)
        known = true;
    if (!known)
      std::cerr << "  Warning: calibration points refer to route '" << kv.first << "', which is not in the network\n";
  }

  if (calibrated_routes == 0) {
    std::cerr << "Error: No route could be calibrated; no output written\n";
    return 1;
  }

  std::ofstream out(output_file);
  if (!out.is_open()) {
    std::cerr << "Error: Could not create file: " << output_file << "\n";
    return 1;
  }
  out << "# LRS Network file\n# CRS: " << net.crs << "\n# Routes: " << net.routes.size()
      << "\n# Format: routes|nodes\n\n@ROUTES\nroute_id,vertex_count,total_length,geometry,calibrated\n";
  for (const auto& r : net.routes) {
    double len = 0;
    for (size_t i = 1; i < r.v.size(); ++i)
      len += std::hypot(r.v[i].x - r.v[i - 1].x, r.v[i].y - r.v[i - 1].y);
    out << r.id << "," << r.v.size() << "," << std::fixed << std::setprecision(6) << len << ",\"LINESTRING M(";
    for (size_t i = 0; i < r.v.size(); ++i)
      out << (i ? ", " : "") << r.v[i].x << " " << r.v[i].y << " " << r.v[i].m;
    out << ")\"," << (r.calibrated ? 1 : 0) << "\n";
  }
  out << "\n@NODES\nnode_id,route_id,x,y,measure\n";
  int node = 0;
  for (const auto& r : net.routes)
    for (const auto& v : r.v)
      out << node++ << "," << r.id << "," << std::fixed << std::setprecision(6) << v.x << "," << v.y << ","
          << v.m << "\n";
  out << "\n@ROUTE_ATTRIBUTES\n";
  for (const auto& l : net.route_attribute_lines)
    out << l << "\n";

  std::cout << "\n========================================\n";
  std::cout << "  CALIBRATION COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Routes calibrated: " << calibrated_routes << "\n";
  std::cout << "Routes not calibrated (with points): " << failed_routes << "\n";
  std::cout << "Calibration points used: " << points_used << "\n";
  if (points_rejected > 0)
    std::cout << "Calibration points rejected (too far): " << points_rejected << "\n";
  std::cout << "Vertices added: " << vertices_added << "\n";
  std::cout << "Output written to: " << output_file << "\n";
  return failed_routes > 0 ? 1 : 0;
}
