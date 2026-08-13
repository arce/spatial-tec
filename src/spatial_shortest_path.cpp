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

#include "../include/core/spatial_string.hpp"

struct Point2D {
  double x, y;
  Point2D() : x(0), y(0) {}
  Point2D(double x, double y) : x(x), y(y) {}
};

struct NetworkData {
  std::vector<std::vector<double>> distance_matrix;
  std::vector<std::vector<int>> next_matrix;
  std::map<int, double> node_x;
  std::map<int, double> node_y;
  std::map<int, std::string> node_name;
  int n_nodes;
  std::string crs;
};

bool isInteger(const std::string& s) {
  if (s.empty())
    return false;
  try {
    std::stoi(s);
    return true;
  } catch (...) {
    return false;
  }
}

NetworkData loadNetwork(const std::string& filename) {
  NetworkData net;
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file: " << filename << "\n";
    return net;
  }

  std::string line;
  std::string section;
  std::map<int, double> node_x, node_y;
  std::map<int, std::string> node_name;
  int max_id = 0;

  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty())
      continue;

    if (line[0] == '#')
      continue;

    if (line[0] == '@') {
      section = line.substr(1);
      continue;
    }

    if (section == "NODES") {
      auto parts = split(line, ',');

      if (parts.size() >= 5 && isInteger(parts[0])) {
        try {
          int id = std::stoi(parts[0]);
          double x = std::stod(parts[1]);
          double y = std::stod(parts[2]);
          node_x[id] = x;
          node_y[id] = y;
          if (parts.size() >= 6) {
            node_name[id] = parts[5];
          }
          if (id > max_id)
            max_id = id;
        } catch (const std::exception& e) {
        }
      }
    }
  }

  net.n_nodes = max_id + 1;
  net.distance_matrix.assign(
      net.n_nodes, std::vector<double>(net.n_nodes, std::numeric_limits<double>::infinity()));
  net.next_matrix.assign(net.n_nodes, std::vector<int>(net.n_nodes, -1));
  net.node_x = node_x;
  net.node_y = node_y;
  net.node_name = node_name;

  file.clear();
  file.seekg(0, std::ios::beg);
  section = "";

  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty())
      continue;

    if (line[0] == '#')
      continue;

    if (line[0] == '@') {
      section = line.substr(1);
      continue;
    }

    if (section == "DISTANCE_MATRIX") {
      auto parts = split(line, ',');
      if (parts.size() >= 3 && isInteger(parts[0]) && isInteger(parts[1])) {
        try {
          int from = std::stoi(parts[0]);
          int to = std::stoi(parts[1]);
          double dist = std::stod(parts[2]);
          if (from < net.n_nodes && to < net.n_nodes) {
            net.distance_matrix[from][to] = dist;
          }
        } catch (const std::exception& e) {
        }
      }
    } else if (section == "NEXT_MATRIX") {
      auto parts = split(line, ',');
      if (parts.size() >= 3 && isInteger(parts[0]) && isInteger(parts[1])) {
        try {
          int from = std::stoi(parts[0]);
          int to = std::stoi(parts[1]);
          int next = std::stoi(parts[2]);
          if (from < net.n_nodes && to < net.n_nodes) {
            net.next_matrix[from][to] = next;
          }
        } catch (const std::exception& e) {
        }
      }
    } else if (section == "CRS") {
      net.crs = line;
    }
  }

  for (int i = 0; i < net.n_nodes; ++i) {
    net.distance_matrix[i][i] = 0;
    net.next_matrix[i][i] = i;
  }

  if (net.n_nodes <= 0) {
    std::cerr << "Error: No nodes found in network file\n";
    return net;
  }

  return net;
}

std::vector<int> reconstructPath(const NetworkData& net, int start, int end) {
  std::vector<int> path;

  if (start < 0 || start >= net.n_nodes || end < 0 || end >= net.n_nodes) {
    return path;
  }

  if (!std::isfinite(net.distance_matrix[start][end])) {
    return path;
  }

  int current = start;
  while (current != end) {
    path.push_back(current);
    int next = net.next_matrix[current][end];
    if (next == -1 || next == current) {
      break;
    }
    current = next;
  }
  path.push_back(end);

  return path;
}

double calculatePathLength(const std::vector<int>& path, const NetworkData& net) {
  double total = 0;
  for (size_t i = 0; i < path.size() - 1; ++i) {
    int from = path[i];
    int to = path[i + 1];
    if (from < net.n_nodes && to < net.n_nodes) {
      total += net.distance_matrix[from][to];
    }
  }
  return total;
}

void writePathCSV(const std::vector<int>& path, const NetworkData& net,
                  const std::string& filename) {
  std::ofstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not create file: " << filename << "\n";
    return;
  }

  double total_cost = calculatePathLength(path, net);

  file << "# Shortest path\n";
  file << "# Nodes: " << path.size() << "\n";
  file << "# Start node: " << path.front() << "\n";
  file << "# End node: " << path.back() << "\n";
  file << "# Total cost: " << std::fixed << std::setprecision(6) << total_cost << "\n";
  file << "# Geometry column: geometry\n";
  file << "id,node_start,node_end,total_cost,node_count,geometry\n";

  std::string wkt = "LINESTRING(";
  for (size_t i = 0; i < path.size(); ++i) {
    int node_id = path[i];
    auto it_x = net.node_x.find(node_id);
    auto it_y = net.node_y.find(node_id);
    if (it_x != net.node_x.end() && it_y != net.node_y.end()) {
      if (i > 0)
        wkt += ", ";
      wkt += std::to_string(it_x->second) + " " + std::to_string(it_y->second);
    }
  }
  wkt += ")";

  file << "1," << path.front() << "," << path.back() << "," << std::fixed << std::setprecision(6)
       << total_cost << "," << path.size() << ","
       << "\"" << wkt << "\"\n";
}

int findNodeId(const NetworkData& net, const std::string& query) {
  if (isInteger(query)) {
    try {
      int id = std::stoi(query);
      if (id >= 0 && id < net.n_nodes) {
        return id;
      }
    } catch (...) {
    }
  }

  for (const auto& [id, name] : net.node_name) {
    if (toLower(name).find(toLower(query)) != std::string::npos) {
      return id;
    }
  }

  return -1;
}

void printUsage() {
  std::cerr << "spatial_shortest_path - Find shortest path between two nodes\n\n";
  std::cerr << "Usage: spatial_shortest_path <network.network> -from <origin> -to <destination> "
               "[options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -from <node>     Origin node (ID or name)\n";
  std::cerr << "  -to <node>       Destination node (ID or name)\n";
  std::cerr << "  -output <file>   Output CSV file with path (default: path.csv)\n";
  std::cerr << "  -verbose         Show detailed path information\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_shortest_path roads.network -from 0 -to 10\n";
  std::cerr << "  spatial_shortest_path roads.network -from \"San Jose\" -to \"Alajuela\"\n";
  std::cerr << "  spatial_shortest_path roads.network -from 0 -to 10 -output route.csv -verbose\n";
}

int main(int argc, char* argv[]) {
  if (argc < 5) {
    printUsage();
    return 1;
  }

  std::string network_file;
  std::string origin_str;
  std::string dest_str;
  std::string output_file = "path.csv";
  bool verbose = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-from" && i + 1 < argc) {
      origin_str = argv[++i];
    } else if (arg == "-to" && i + 1 < argc) {
      dest_str = argv[++i];
    } else if (arg == "-output" && i + 1 < argc) {
      output_file = argv[++i];
    } else if (arg == "-verbose") {
      verbose = true;
    } else if (network_file.empty()) {
      network_file = arg;
    }
  }

  if (network_file.empty() || origin_str.empty() || dest_str.empty()) {
    std::cerr << "Error: Missing required arguments\n";
    return 1;
  }

  std::cout << "========================================\n";
  std::cout << "  SHORTEST PATH\n";
  std::cout << "========================================\n\n";
  std::cout << "Network: " << network_file << "\n";
  std::cout << "From: " << origin_str << "\n";
  std::cout << "To: " << dest_str << "\n\n";

  NetworkData net = loadNetwork(network_file);

  if (net.n_nodes <= 0) {
    std::cerr << "Error: Failed to load network or no nodes found\n";
    return 1;
  }

  std::cout << "Network loaded: " << net.n_nodes << " nodes\n";

  int start = findNodeId(net, origin_str);
  int end = findNodeId(net, dest_str);

  if (start == -1) {
    std::cerr << "Error: Origin not found: " << origin_str << "\n";
    std::cerr << "Available node IDs: 0-" << (net.n_nodes - 1) << "\n";
    return 1;
  }
  if (end == -1) {
    std::cerr << "Error: Destination not found: " << dest_str << "\n";
    std::cerr << "Available node IDs: 0-" << (net.n_nodes - 1) << "\n";
    return 1;
  }

  std::cout << "Origin node: " << start << "\n";
  std::cout << "Destination node: " << end << "\n\n";

  if (!std::isfinite(net.distance_matrix[start][end])) {
    std::cerr << "Error: No path found between nodes " << start << " and " << end << "\n";
    return 1;
  }

  auto path = reconstructPath(net, start, end);
  double total_length = calculatePathLength(path, net);

  std::cout << "Path found with " << path.size() << " nodes\n";
  std::cout << "Total length: " << std::fixed << std::setprecision(4) << total_length << "\n\n";

  if (verbose) {
    std::cout << "Path nodes: ";
    for (int node : path) {
      std::cout << node << " ";
    }
    std::cout << "\n\n";

    std::cout << "Segment details:\n";
    for (size_t i = 0; i < path.size() - 1; ++i) {
      int from = path[i];
      int to = path[i + 1];
      double dist = net.distance_matrix[from][to];
      std::cout << "  " << from << " -> " << to << ": " << std::fixed << std::setprecision(4)
                << dist << "\n";
    }
    std::cout << "\n";
  }

  writePathCSV(path, net, output_file);
  std::cout << "Path written to: " << output_file << "\n";

  std::cout << "\n========================================\n";
  std::cout << "  PATH COMPLETE\n";
  std::cout << "========================================\n";

  return 0;
}

