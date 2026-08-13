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

struct LRSNode {
  int node_id;
  double x, y;
  double measure;
  std::string route_id;
};

struct LRSNetwork {
  std::vector<LRSRoute> routes;
  std::vector<LRSNode> nodes;
  std::map<std::string, int> route_index;
  std::string crs;
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

LRSNetwork readRoutes(const std::string& filename) {
  LRSNetwork network;
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file: " << filename << "\n";
    return network;
  }

  std::string line;
  std::vector<std::string> headers;
  bool is_header = true;
  int line_num = 0;

  int col_route_id = -1;
  int col_geometry = -1;
  int col_from_measure = -1;
  int col_to_measure = -1;
  std::vector<int> attr_cols;

  while (std::getline(file, line)) {
    line_num++;
    line = trim(line);
    if (line.empty() || line[0] == '#')
      continue;

    auto values = splitCSV(line);

    if (is_header) {
      headers = values;

      for (size_t i = 0; i < headers.size(); ++i) {
        std::string lower = toLower(headers[i]);
        if (lower == "route_id" || lower == "route" || lower == "id") {
          col_route_id = i;
        } else if (lower == "geometry" || lower == "geom" || lower == "wkt") {
          col_geometry = i;
        } else if (lower == "from_measure" || lower == "from" || lower == "m_from") {
          col_from_measure = i;
        } else if (lower == "to_measure" || lower == "to" || lower == "m_to") {
          col_to_measure = i;
        } else {
          attr_cols.push_back(i);
        }
      }

      is_header = false;
      continue;
    }

    if (col_route_id == -1 || col_geometry == -1) {
      std::cerr << "Error: Missing required columns (route_id, geometry)\n";
      break;
    }

    std::string route_id =
        (col_route_id >= 0 && col_route_id < (int)values.size()) ? values[col_route_id] : "";

    if (route_id.empty())
      continue;

    std::vector<Point2D> points;
    if (col_geometry >= 0 && col_geometry < (int)values.size()) {
      points = parseWKTLineString(values[col_geometry]);
    }

    if (points.size() < 2)
      continue;

    LRSRoute route;
    route.route_id = route_id;

    double accumulated = 0;

    double from_measure = 0;
    double to_measure = 0;
    bool has_from = (col_from_measure >= 0 && col_from_measure < (int)values.size() &&
                     isNumber(values[col_from_measure]));
    bool has_to = (col_to_measure >= 0 && col_to_measure < (int)values.size() &&
                   isNumber(values[col_to_measure]));

    if (has_from) {
      from_measure = std::stod(values[col_from_measure]);
    }
    if (has_to) {
      to_measure = std::stod(values[col_to_measure]);
    }

    double total_length = 0;
    for (size_t i = 0; i < points.size() - 1; ++i) {
      total_length += points[i].distanceTo(points[i + 1]);
    }

    if (has_from && has_to && total_length > 0) {
      double range = to_measure - from_measure;
      double scale = range / total_length;

      for (size_t i = 0; i < points.size(); ++i) {
        double m;
        if (i == 0) {
          m = from_measure;
        } else {
          double dist = 0;
          for (size_t j = 0; j < i; ++j) {
            dist += points[j].distanceTo(points[j + 1]);
          }
          m = from_measure + dist * scale;
        }
        route.vertices.push_back(LRSVertex(points[i].x, points[i].y, m));
      }
    } else {
      accumulated = 0;
      for (size_t i = 0; i < points.size(); ++i) {
        if (i == 0) {
          route.vertices.push_back(LRSVertex(points[i].x, points[i].y, 0));
        } else {
          accumulated += points[i - 1].distanceTo(points[i]);
          route.vertices.push_back(LRSVertex(points[i].x, points[i].y, accumulated));
        }
      }
    }

    route.total_length = total_length;

    for (int idx : attr_cols) {
      if (idx < (int)values.size()) {
        route.attributes[headers[idx]] = values[idx];
      }
    }

    network.routes.push_back(route);
    network.route_index[route_id] = network.routes.size() - 1;
  }

  return network;
}

void writeLRSNetwork(const LRSNetwork& network, const std::string& filename,
                     const std::string& crs = "EPSG:4326") {
  std::ofstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not create file: " << filename << "\n";
    return;
  }

  file << "# LRS Network file\n";
  file << "# CRS: " << crs << "\n";
  file << "# Routes: " << network.routes.size() << "\n";
  file << "# Format: routes|nodes\n\n";

  file << "@ROUTES\n";
  file << "route_id,vertex_count,total_length,geometry\n";

  for (const auto& route : network.routes) {
    std::string wkt = "LINESTRING M(";
    for (size_t i = 0; i < route.vertices.size(); ++i) {
      if (i > 0)
        wkt += ", ";
      wkt += std::to_string(route.vertices[i].x) + " " + std::to_string(route.vertices[i].y) + " " +
             std::to_string(route.vertices[i].measure);
    }
    wkt += ")";

    file << route.route_id << "," << route.vertices.size() << "," << std::fixed
         << std::setprecision(6) << route.total_length << ","
         << "\"" << wkt << "\"\n";
  }
  file << "\n";

  file << "@NODES\n";
  file << "node_id,route_id,x,y,measure\n";

  int node_id = 0;
  for (const auto& route : network.routes) {
    for (const auto& vertex : route.vertices) {
      file << node_id++ << "," << route.route_id << "," << std::fixed << std::setprecision(6)
           << vertex.x << "," << std::fixed << std::setprecision(6) << vertex.y << "," << std::fixed
           << std::setprecision(6) << vertex.measure << "\n";
    }
  }
  file << "\n";

  file << "@ROUTE_ATTRIBUTES\n";

  std::set<std::string> all_attrs;
  for (const auto& route : network.routes) {
    for (const auto& [key, value] : route.attributes) {
      all_attrs.insert(key);
    }
  }

  file << "route_id";
  for (const auto& attr : all_attrs) {
    file << "," << attr;
  }
  file << "\n";

  for (const auto& route : network.routes) {
    file << route.route_id;
    for (const auto& attr : all_attrs) {
      auto it = route.attributes.find(attr);
      if (it != route.attributes.end()) {
        file << "," << it->second;
      } else {
        file << ",";
      }
    }
    file << "\n";
  }
}

void printNetworkStats(const LRSNetwork& network) {
  std::cout << "\nNetwork Statistics:\n";
  std::cout << "  Routes: " << network.routes.size() << "\n";

  double total_length = 0;
  int total_vertices = 0;

  for (const auto& route : network.routes) {
    total_length += route.total_length;
    total_vertices += route.vertices.size();
  }

  std::cout << "  Total length: " << std::fixed << std::setprecision(2) << total_length << "\n";
  std::cout << "  Total vertices: " << total_vertices << "\n";

  if (!network.routes.empty()) {
    std::cout << "\nSample routes:\n";
    for (size_t i = 0; i < std::min(network.routes.size(), size_t(5)); ++i) {
      const auto& route = network.routes[i];
      std::cout << "  " << route.route_id << ": " << route.vertices.size() << " vertices"
                << ", length: " << std::fixed << std::setprecision(4) << route.total_length
                << ", M-range: " << std::fixed << std::setprecision(2)
                << route.vertices.front().measure << " - " << route.vertices.back().measure << "\n";
    }
  }
}

void printUsage() {
  std::cerr << "spatial_lrs_create - Create Linear Referencing System (LRS) network\n\n";
  std::cerr << "Usage: spatial_lrs_create <input.csv> <output.lrs> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -crs <value>     Coordinate system (default: EPSG:4326)\n";
  std::cerr << "  -verbose         Show detailed information\n\n";
  std::cerr << "Input CSV must contain:\n";
  std::cerr << "  route_id: Unique route identifier\n";
  std::cerr << "  geometry: LINESTRING WKT geometry\n";
  std::cerr << "  Optional: from_measure, to_measure (for custom M-values)\n\n";
  std::cerr << "Example:\n";
  std::cerr << "  spatial_lrs_create streets.csv lrs_network.lrs -crs EPSG:5367\n";
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printUsage();
    return 1;
  }

  std::string input_file = argv[1];
  std::string output_file = argv[2];
  std::string crs = "EPSG:4326";
  bool verbose = false;

  for (int i = 3; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-crs" && i + 1 < argc) {
      crs = argv[++i];
    } else if (arg == "-verbose") {
      verbose = true;
    }
  }

  std::cout << "========================================\n";
  std::cout << "  LRS NETWORK CREATION\n";
  std::cout << "========================================\n\n";
  std::cout << "Input: " << input_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "CRS: " << crs << "\n\n";

  LRSNetwork network = readRoutes(input_file);

  if (network.routes.empty()) {
    std::cerr << "Error: No routes found in input file\n";
    return 1;
  }

  std::cout << "Routes loaded: " << network.routes.size() << "\n";

  if (verbose) {
    printNetworkStats(network);
  }

  writeLRSNetwork(network, output_file, crs);

  std::cout << "\n========================================\n";
  std::cout << "  LRS CREATION COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Output file: " << output_file << "\n";
  std::cout << "Routes: " << network.routes.size() << "\n";

  return 0;
}

