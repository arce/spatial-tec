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

  bool operator<(const Point2D& other) const {
    if (std::abs(x - other.x) > 1e-9)
      return x < other.x;
    return y < other.y;
  }
};

struct Node {
  int id;
  double x, y;
  double lon, lat;
  double dc;
  double ic;
  double cc;
};

struct Edge {
  int source;
  int target;
  double length;
  double travel_time;
  double speed;
  bool oneway;
};

struct Network {
  std::vector<Node> nodes;
  std::vector<Edge> edges;
  std::vector<std::vector<double>> distance_matrix;
  std::vector<std::vector<double>> travel_time_matrix;
  std::vector<std::vector<int>> next_matrix;
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

std::vector<Node> extractNodes(const std::vector<std::vector<Point2D>>& lines) {
  std::vector<Node> nodes;
  std::map<Point2D, int> coord_to_id;
  int next_id = 0;

  for (const auto& line : lines) {
    for (const auto& p : line) {
      Point2D key(std::round(p.x * 1e9) / 1e9, std::round(p.y * 1e9) / 1e9);
      if (coord_to_id.find(key) == coord_to_id.end()) {
        coord_to_id[key] = next_id;
        Node node;
        node.id = next_id++;
        node.x = p.x;
        node.y = p.y;
        node.lon = p.x;
        node.lat = p.y;
        node.dc = 0;
        node.ic = 0;
        node.cc = 0;
        nodes.push_back(node);
      }
    }
  }

  return nodes;
}

std::vector<Edge> extractEdges(const std::vector<std::vector<Point2D>>& lines,
                               const std::vector<Node>& nodes, double speed_default = 50.0) {
  std::vector<Edge> edges;
  std::map<Point2D, int> coord_to_id;

  for (const auto& node : nodes) {
    Point2D key(std::round(node.x * 1e9) / 1e9, std::round(node.y * 1e9) / 1e9);
    coord_to_id[key] = node.id;
  }

  for (const auto& line : lines) {
    for (size_t i = 0; i < line.size() - 1; ++i) {
      Point2D key1(std::round(line[i].x * 1e9) / 1e9, std::round(line[i].y * 1e9) / 1e9);
      Point2D key2(std::round(line[i + 1].x * 1e9) / 1e9, std::round(line[i + 1].y * 1e9) / 1e9);

      if (coord_to_id.find(key1) != coord_to_id.end() &&
          coord_to_id.find(key2) != coord_to_id.end()) {
        Edge edge;
        edge.source = coord_to_id[key1];
        edge.target = coord_to_id[key2];
        edge.length = line[i].distanceTo(line[i + 1]);
        edge.speed = speed_default;

        double dist_km = edge.length * 111.0;
        edge.travel_time = (dist_km / speed_default) * 60.0;
        edge.oneway = false;
        edges.push_back(edge);
      }
    }
  }

  return edges;
}

std::vector<double> dijkstra(const Network& network, int source) {
  int n = network.nodes.size();
  std::vector<double> dist(n, std::numeric_limits<double>::infinity());
  std::vector<int> prev(n, -1);
  std::priority_queue<std::pair<double, int>, std::vector<std::pair<double, int>>,
                      std::greater<std::pair<double, int>>>
      pq;

  dist[source] = 0;
  pq.push({0, source});

  std::map<int, std::vector<std::pair<int, double>>> adj;
  for (const auto& edge : network.edges) {
    adj[edge.source].push_back({edge.target, edge.length});
    if (!edge.oneway) {
      adj[edge.target].push_back({edge.source, edge.length});
    }
  }

  while (!pq.empty()) {
    auto [d, u] = pq.top();
    pq.pop();

    if (d > dist[u])
      continue;

    for (const auto& [v, w] : adj[u]) {
      if (dist[u] + w < dist[v]) {
        dist[v] = dist[u] + w;
        prev[v] = u;
        pq.push({dist[v], v});
      }
    }
  }

  return dist;
}

void computeAllPairsShortestPaths(Network& network) {
  int n = network.nodes.size();

  std::cout << "Computing all-pairs shortest paths using Dijkstra...\n";
  std::cout << "Nodes: " << n << "\n";

  network.distance_matrix.assign(n,
                                 std::vector<double>(n, std::numeric_limits<double>::infinity()));
  network.travel_time_matrix.assign(
      n, std::vector<double>(n, std::numeric_limits<double>::infinity()));
  network.next_matrix.assign(n, std::vector<int>(n, -1));

  for (int i = 0; i < n; ++i) {
    auto dist = dijkstra(network, i);
    for (int j = 0; j < n; ++j) {
      if (std::isfinite(dist[j])) {
        network.distance_matrix[i][j] = dist[j];
        network.travel_time_matrix[i][j] = dist[j];
        if (i != j) {
          network.next_matrix[i][j] = j;
        }
      }
    }

    if ((i + 1) % 100 == 0) {
      std::cout << "  Progress: " << (i + 1) << "/" << n << "\n";
    }
  }

  std::cout << "  Dijkstra complete\n";
}

void computeCentralities(Network& network) {
  int n = network.nodes.size();

  std::vector<int> degrees(n, 0);
  for (const auto& edge : network.edges) {
    degrees[edge.source]++;
    if (!edge.oneway) {
      degrees[edge.target]++;
    }
  }

  for (int i = 0; i < n; ++i) {
    network.nodes[i].dc = (n > 1) ? (double)degrees[i] / (n - 1) : 0;
  }

  for (int i = 0; i < n; ++i) {
    if (degrees[i] == 0)
      continue;
    double betweenness = 0;
    int total_pairs = 0;
    for (int s = 0; s < n; ++s) {
      for (int t = 0; t < n; ++t) {
        if (s == i || t == i || s == t)
          continue;
        if (std::isfinite(network.distance_matrix[s][t])) {
          total_pairs++;

          if (std::isfinite(network.distance_matrix[s][i]) &&
              std::isfinite(network.distance_matrix[i][t]) &&
              std::abs(network.distance_matrix[s][i] + network.distance_matrix[i][t] -
                       network.distance_matrix[s][t]) < 1e-9) {
            betweenness++;
          }
        }
      }
    }
    network.nodes[i].ic = (total_pairs > 0) ? betweenness / total_pairs : 0;
  }

  for (int i = 0; i < n; ++i) {
    if (degrees[i] == 0)
      continue;
    double sum_dist = 0;
    int reachable = 0;
    for (int j = 0; j < n; ++j) {
      if (i == j)
        continue;
      if (std::isfinite(network.distance_matrix[i][j])) {
        sum_dist += network.distance_matrix[i][j];
        reachable++;
      }
    }
    if (reachable > 0 && sum_dist > 0) {
      network.nodes[i].cc = (double)reachable / sum_dist;
    } else {
      network.nodes[i].cc = 0;
    }
  }
}

void writeNetworkFile(const Network& network, const std::string& filename,
                      const std::string& crs = "EPSG:4326") {
  std::ofstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not create file: " << filename << "\n";
    return;
  }

  file << "# Network file\n";
  file << "# CRS: " << crs << "\n";
  file << "# Nodes: " << network.nodes.size() << "\n";
  file << "# Edges: " << network.edges.size() << "\n";
  file << "# Format: nodes|edges|distance_matrix|travel_time_matrix|node_costs\n\n";

  file << "@NODES\n";
  file << "node_id,longitude,latitude,x,y,dc,ic,cc\n";
  for (const auto& node : network.nodes) {
    file << node.id << "," << std::fixed << std::setprecision(6) << node.lon << "," << std::fixed
         << std::setprecision(6) << node.lat << "," << std::fixed << std::setprecision(6) << node.x
         << "," << std::fixed << std::setprecision(6) << node.y << "," << std::fixed
         << std::setprecision(6) << node.dc << "," << std::fixed << std::setprecision(6) << node.ic
         << "," << std::fixed << std::setprecision(6) << node.cc << "\n";
  }
  file << "\n";

  file << "@EDGES\n";
  file << "source,target,length,travel_time,oneway\n";
  for (const auto& edge : network.edges) {
    file << edge.source << "," << edge.target << "," << std::fixed << std::setprecision(6)
         << edge.length << "," << std::fixed << std::setprecision(4) << edge.travel_time << ","
         << (edge.oneway ? 1 : 0) << "\n";
  }
  file << "\n";

  file << "@DISTANCE_MATRIX\n";
  file << "from,to,distance\n";
  for (size_t i = 0; i < network.nodes.size(); ++i) {
    for (size_t j = 0; j < network.nodes.size(); ++j) {
      if (std::isfinite(network.distance_matrix[i][j]) && i != j) {
        file << i << "," << j << "," << std::fixed << std::setprecision(6)
             << network.distance_matrix[i][j] << "\n";
      }
    }
  }
  file << "\n";

  file << "@TRAVEL_TIME_MATRIX\n";
  file << "from,to,travel_time\n";
  for (size_t i = 0; i < network.nodes.size(); ++i) {
    for (size_t j = 0; j < network.nodes.size(); ++j) {
      if (std::isfinite(network.travel_time_matrix[i][j]) && i != j) {
        file << i << "," << j << "," << std::fixed << std::setprecision(4)
             << network.travel_time_matrix[i][j] << "\n";
      }
    }
  }
  file << "\n";

  file << "@NEXT_MATRIX\n";
  file << "from,to,next_node\n";
  for (size_t i = 0; i < network.nodes.size(); ++i) {
    for (size_t j = 0; j < network.nodes.size(); ++j) {
      if (network.next_matrix[i][j] != -1 && i != j) {
        file << i << "," << j << "," << network.next_matrix[i][j] << "\n";
      }
    }
  }
  file << "\n";

  file << "@NODE_COSTS\n";
  file << "node_id,dc,ic,cc\n";
  for (const auto& node : network.nodes) {
    file << node.id << "," << std::fixed << std::setprecision(6) << node.dc << "," << std::fixed
         << std::setprecision(6) << node.ic << "," << std::fixed << std::setprecision(6) << node.cc
         << "\n";
  }
}

void printUsage() {
  std::cerr << "spatial_network - Build network from street lines\n\n";
  std::cerr << "Usage: spatial_network <streets.csv> <output.network> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -speed <value>   Default speed (km/h) (default: 50)\n";
  std::cerr << "  -crs <value>     Coordinate system (default: EPSG:4326)\n";
  std::cerr << "  -oneway          Treat edges as one-way\n\n";
  std::cerr << "Output:\n";
  std::cerr << "  <output.network>  Single file with all network data\n\n";
  std::cerr << "Example:\n";
  std::cerr << "  spatial_network streets.csv roads.network -speed 40\n";
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printUsage();
    return 1;
  }

  std::string streets_file = argv[1];
  std::string output_file = argv[2];
  double default_speed = 50.0;
  std::string crs = "EPSG:4326";
  bool oneway = false;

  for (int i = 3; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-speed" && i + 1 < argc) {
      default_speed = std::stod(argv[++i]);
    } else if (arg == "-crs" && i + 1 < argc) {
      crs = argv[++i];
    } else if (arg == "-oneway") {
      oneway = true;
    }
  }

  std::cout << "========================================\n";
  std::cout << "  NETWORK CONSTRUCTION\n";
  std::cout << "========================================\n\n";
  std::cout << "Input: " << streets_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Speed: " << default_speed << " km/h\n";
  std::cout << "CRS: " << crs << "\n\n";

  Spatial::SpatialCSVReader reader;
  Spatial::VectorDataset dataset;

  if (!reader.read(streets_file, dataset)) {
    std::cerr << "Error: Could not read streets file\n";
    return 1;
  }

  std::cout << "Street features: " << dataset.features.size() << "\n";

  std::vector<std::vector<Point2D>> lines;
  for (const auto& feature : dataset.features) {
    std::vector<Point2D> points;
    for (size_t i = 0; i < feature.coordinates.size(); i += 2) {
      points.push_back(Point2D(feature.coordinates[i], feature.coordinates[i + 1]));
    }
    if (points.size() >= 2) {
      lines.push_back(points);
    }
  }

  std::cout << "Line geometries: " << lines.size() << "\n";

  if (lines.empty()) {
    std::cerr << "Error: No LINESTRING geometries found\n";
    return 1;
  }

  Network network;
  network.nodes = extractNodes(lines);
  std::cout << "Nodes: " << network.nodes.size() << "\n";

  network.edges = extractEdges(lines, network.nodes, default_speed);
  std::cout << "Edges: " << network.edges.size() << "\n";

  computeAllPairsShortestPaths(network);

  std::cout << "Computing node centralities...\n";
  computeCentralities(network);

  writeNetworkFile(network, output_file, crs);

  std::cout << "\n========================================\n";
  std::cout << "  NETWORK COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Output file: " << output_file << "\n";
  std::cout << "Nodes: " << network.nodes.size() << "\n";
  std::cout << "Edges: " << network.edges.size() << "\n";
  std::cout << "Matrix size: " << network.nodes.size() << "x" << network.nodes.size() << "\n";

  return 0;
}
