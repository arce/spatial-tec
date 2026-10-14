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

#include "../include/core/spatial_csv.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

// Tolerance used when comparing a measure increment against the planar
// distance between two consecutive vertices.
static const double MEASURE_TOL = 1e-3;

struct LRSVertex {
  double x, y, m;
};

struct LRSRoute {
  std::string route_id;
  std::vector<LRSVertex> vertices;
  double declared_length;  // total_length column of the @ROUTES section
};

struct LRSFile {
  std::vector<LRSRoute> routes;
  std::string crs = "(not specified)";
  int skipped_lines = 0;
};

struct Issue {
  std::string route_id;
  int vertex;  // 1-based vertex index the issue refers to; 0 = whole route
  std::string severity;  // ERROR | WARNING
  std::string code;
  std::string detail;
};

void printUsage() {
  std::cerr << "spatial_lrs_info - LRS network information and validation\n\n";
  std::cerr << "Usage: spatial_lrs_info <network.lrs> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -route <id>       Show detail (every vertex and its measure) for a single route\n";
  std::cerr << "  -validate         Also validate geometry and M-values (gaps, overlaps, out-of-range measures)\n";
  std::cerr << "  -report <file>    Save the issues found as a CSV (only meaningful with -validate)\n\n";
  std::cerr << "Validation checks:\n";
  std::cerr << "  DUPLICATE_ROUTE        route_id appears more than once\n";
  std::cerr << "  TOO_FEW_VERTICES       route has fewer than 2 vertices\n";
  std::cerr << "  NEGATIVE_MEASURE       a vertex has a negative measure\n";
  std::cerr << "  MEASURE_OVERLAP        measure does not increase (decreases or repeats)\n";
  std::cerr << "  MEASURE_GAP            measure grows more than the distance between vertices\n";
  std::cerr << "  MEASURE_SHORT          measure grows less than the distance between vertices\n";
  std::cerr << "  LENGTH_MISMATCH        last measure differs from the declared route length\n";
  std::cerr << "  START_NOT_ZERO         (warning) first measure is not 0\n";
  std::cerr << "  ZERO_LENGTH_SEGMENT    (warning) two consecutive vertices share the same position\n\n";
  std::cerr << "Exit code: 0 = OK, 1 = usage/IO error or validation errors found.\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_lrs_info roads.lrs\n";
  std::cerr << "  spatial_lrs_info roads.lrs -route 101\n";
  std::cerr << "  spatial_lrs_info roads.lrs -validate -report lrs_errors.csv\n";
}

static bool loadLRS(const std::string& filename, LRSFile& out) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file: " << filename << "\n";
    return false;
  }

  std::string line;
  std::string section;
  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty())
      continue;

    if (line[0] == '#') {
      std::string body = trim(line.substr(1));
      if (body.rfind("CRS:", 0) == 0)
        out.crs = trim(body.substr(4));
      continue;
    }

    if (line[0] == '@') {
      section = line.substr(1);
      continue;
    }

    if (section != "ROUTES")
      continue;

    auto parts = splitCSV(line);
    // Header row of the section
    if (!parts.empty() && toLower(parts[0]) == "route_id")
      continue;
    if (parts.size() < 4) {
      out.skipped_lines++;
      continue;
    }

    LRSRoute route;
    route.route_id = parts[0];
    try {
      route.declared_length = std::stod(parts[2]);
    } catch (...) {
      route.declared_length = std::numeric_limits<double>::quiet_NaN();
    }

    std::string geom = parts[3];
    if (geom.size() >= 2 && geom.front() == '"' && geom.back() == '"')
      geom = geom.substr(1, geom.length() - 2);

    size_t start = geom.find('(');
    size_t end = geom.rfind(')');
    if (start == std::string::npos || end == std::string::npos || end <= start) {
      out.skipped_lines++;
      continue;
    }

    for (const auto& pt : split(geom.substr(start + 1, end - start - 1), ',')) {
      auto c = split(trim(pt), ' ');
      if (c.size() >= 3) {
        try {
          route.vertices.push_back({std::stod(c[0]), std::stod(c[1]), std::stod(c[2])});
        } catch (...) {
        }
      }
    }
    out.routes.push_back(route);
  }
  return true;
}

static double dist(const LRSVertex& a, const LRSVertex& b) {
  return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

static std::string num(double v, int prec = 4) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(prec) << v;
  return os.str();
}

static void validateRoutes(const LRSFile& lrs, std::vector<Issue>& issues) {
  std::set<std::string> seen;
  for (const auto& r : lrs.routes) {
    if (!seen.insert(r.route_id).second)
      issues.push_back({r.route_id, 0, "ERROR", "DUPLICATE_ROUTE", "route_id appears more than once"});

    const auto& v = r.vertices;
    if (v.size() < 2) {
      issues.push_back({r.route_id, 0, "ERROR", "TOO_FEW_VERTICES",
                        "route has " + std::to_string(v.size()) + " vertices (minimum is 2)"});
      continue;
    }

    for (size_t i = 0; i < v.size(); ++i) {
      if (v[i].m < 0)
        issues.push_back({r.route_id, (int)i + 1, "ERROR", "NEGATIVE_MEASURE", "measure " + num(v[i].m)});
    }

    if (std::fabs(v.front().m) > MEASURE_TOL)
      issues.push_back({r.route_id, 1, "WARNING", "START_NOT_ZERO", "first measure is " + num(v.front().m)});

    for (size_t i = 1; i < v.size(); ++i) {
      double dm = v[i].m - v[i - 1].m;
      double d = dist(v[i - 1], v[i]);
      std::string where = "vertices " + std::to_string(i) + "-" + std::to_string(i + 1) + ": ";

      if (d < 1e-12)
        issues.push_back({r.route_id, (int)i + 1, "WARNING", "ZERO_LENGTH_SEGMENT",
                          where + "consecutive vertices at the same position"});

      if (dm <= 0) {
        issues.push_back({r.route_id, (int)i + 1, "ERROR", "MEASURE_OVERLAP",
                          where + "measure goes from " + num(v[i - 1].m) + " to " + num(v[i].m)});
      } else if (dm - d > MEASURE_TOL) {
        issues.push_back({r.route_id, (int)i + 1, "ERROR", "MEASURE_GAP",
                          where + "measure grows " + num(dm) + " over a distance of " + num(d)});
      } else if (d - dm > MEASURE_TOL) {
        issues.push_back({r.route_id, (int)i + 1, "ERROR", "MEASURE_SHORT",
                          where + "measure grows " + num(dm) + " over a distance of " + num(d)});
      }
    }

    double len = v.back().m - v.front().m;
    if (!std::isnan(r.declared_length) && std::fabs(len - r.declared_length) > MEASURE_TOL)
      issues.push_back({r.route_id, 0, "ERROR", "LENGTH_MISMATCH",
                        "last measure gives length " + num(len) + " but declared length is " + num(r.declared_length)});
  }
}

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

int main(int argc, char* argv[]) {
  if (argc < 2) {
    printUsage();
    return 1;
  }

  std::string first = argv[1];
  if (first == "-help" || first == "-h") {
    printUsage();
    return 0;
  }

  std::string network_file = first;
  std::string route_id;
  bool validate = false;
  std::string report_file;

  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-route" && i + 1 < argc) {
      route_id = argv[++i];
    } else if (arg == "-validate") {
      validate = true;
    } else if (arg == "-report" && i + 1 < argc) {
      report_file = argv[++i];
    } else if (arg == "-help" || arg == "-h") {
      printUsage();
      return 0;
    } else {
      std::cerr << "Error: Unknown option: " << arg << "\n";
      printUsage();
      return 1;
    }
  }

  LRSFile lrs;
  if (!loadLRS(network_file, lrs))
    return 1;
  if (lrs.routes.empty()) {
    std::cerr << "Error: No routes found in " << network_file << " (is it a .lrs file created by spatial_lrs_create?)\n";
    return 1;
  }

  const LRSRoute* selected = nullptr;
  if (!route_id.empty()) {
    for (const auto& r : lrs.routes)
      if (r.route_id == route_id) {
        selected = &r;
        break;
      }
    if (!selected) {
      std::cerr << "Error: Route '" << route_id << "' not found in " << network_file << "\n";
      return 1;
    }
  }

  std::cout << "========================================\n";
  std::cout << "  LRS NETWORK INFO" << (validate ? " + VALIDATION" : "") << "\n";
  std::cout << "========================================\n\n";
  std::cout << "Network: " << network_file << "\n";
  std::cout << "CRS: " << lrs.crs << "\n";

  double total_len = 0, minx = 1e300, miny = 1e300, maxx = -1e300, maxy = -1e300;
  size_t total_vertices = 0;
  for (const auto& r : lrs.routes) {
    total_vertices += r.vertices.size();
    if (r.vertices.size() >= 2)
      total_len += r.vertices.back().m - r.vertices.front().m;
    for (const auto& v : r.vertices) {
      minx = std::min(minx, v.x);
      maxx = std::max(maxx, v.x);
      miny = std::min(miny, v.y);
      maxy = std::max(maxy, v.y);
    }
  }

  std::cout << "Routes: " << lrs.routes.size() << "\n";
  std::cout << "Total vertices: " << total_vertices << "\n";
  std::cout << "Total length: " << num(total_len, 2) << "\n";
  std::cout << "Bounding box: (" << num(minx, 2) << ", " << num(miny, 2) << ") - (" << num(maxx, 2) << ", "
            << num(maxy, 2) << ")\n";
  if (lrs.skipped_lines > 0)
    std::cout << "Unreadable route lines skipped: " << lrs.skipped_lines << "\n";

  if (selected) {
    std::cout << "\nRoute " << selected->route_id << ":\n";
    std::cout << "  Vertices: " << selected->vertices.size() << "\n";
    if (!selected->vertices.empty())
      std::cout << "  M-range: " << num(selected->vertices.front().m, 2) << " - "
                << num(selected->vertices.back().m, 2) << "\n";
    std::cout << "  Declared length: " << num(selected->declared_length, 4) << "\n\n";
    std::cout << "  #      X            Y            M\n";
    for (size_t i = 0; i < selected->vertices.size(); ++i) {
      const auto& v = selected->vertices[i];
      std::cout << "  " << std::left << std::setw(6) << (i + 1) << std::right << std::setw(11) << num(v.x, 4)
                << "  " << std::setw(11) << num(v.y, 4) << "  " << std::setw(11) << num(v.m, 4) << "\n";
    }
  } else {
    std::cout << "\n  Route   Vertices   Length        M-range\n";
    for (const auto& r : lrs.routes) {
      double len = r.vertices.size() >= 2 ? r.vertices.back().m - r.vertices.front().m : 0;
      std::cout << "  " << std::left << std::setw(8) << r.route_id << std::setw(11) << r.vertices.size()
                << std::setw(14) << num(len, 4);
      if (!r.vertices.empty())
        std::cout << num(r.vertices.front().m, 2) << " - " << num(r.vertices.back().m, 2);
      std::cout << "\n";
    }
  }

  int errors = 0;
  if (validate) {
    std::vector<Issue> issues;
    validateRoutes(lrs, issues);
    int warnings = 0;
    for (const auto& is : issues)
      (is.severity == "ERROR" ? errors : warnings)++;

    std::cout << "\nValidation:\n";
    if (issues.empty()) {
      std::cout << "  No issues found.\n";
    } else {
      for (const auto& is : issues) {
        std::cout << "  [" << is.severity << "] route " << is.route_id << ": " << is.code << " - " << is.detail
                  << "\n";
      }
    }
    std::cout << "  Errors: " << errors << ", Warnings: " << warnings << "\n";

    if (!report_file.empty()) {
      std::ofstream rep(report_file);
      if (!rep.is_open()) {
        std::cerr << "Error: Could not write report: " << report_file << "\n";
        return 1;
      }
      rep << "route_id,vertex,severity,issue,detail\n";
      for (const auto& is : issues)
        rep << csvQuote(is.route_id) << "," << is.vertex << "," << is.severity << "," << is.code << ","
            << csvQuote(is.detail) << "\n";
      std::cout << "  Report written to: " << report_file << "\n";
    }
  } else if (!report_file.empty()) {
    std::cerr << "Warning: -report is only used together with -validate; ignored.\n";
  }

  std::cout << "\n";
  return errors > 0 ? 1 : 0;
}
