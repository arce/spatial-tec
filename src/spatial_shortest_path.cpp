// spatial_shortest_path -- ruta de costo minimo entre dos nodos de una red
// creada con spatial_network. Dijkstra bajo demanda sobre nodos y aristas
// (respeta 'oneway'); no usa matrices precalculadas.

#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "../include/core/spatial_network.hpp"
#include "../include/core/spatial_string.hpp"

#include <fstream>

using namespace SpatialNet;

namespace {

void printUsage() {
  std::cerr << "spatial_shortest_path - Find the shortest path between two nodes\n\n";
  std::cerr << "Usage: spatial_shortest_path <network.network> -from <origin> -to <destination> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -from <node>     Origin node (ID or name)\n";
  std::cerr << "  -to <node>       Destination node (ID or name)\n";
  std::cerr << "  -cost <kind>     Cost to minimise: length (metres, default) or time (minutes)\n";
  std::cerr << "  -output <file>   Output CSV file with the path (default: path.csv)\n";
  std::cerr << "  -verbose         Show the path node by node\n";
  std::cerr << "  -help            Show this help\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_shortest_path roads.network -from 0 -to 10\n";
  std::cerr << "  spatial_shortest_path roads.network -from \"San Jose\" -to \"Alajuela\" -cost time\n";
  std::cerr << "  spatial_shortest_path roads.network -from 0 -to 10 -output route.csv -verbose\n";
}

bool writePathCSV(const Network& net, const Path& path, CostKind kind, double total_cost,
                  double total_length, double total_time, const std::string& filename,
                  std::string& err) {
  std::ofstream file(filename);
  if (!file.is_open()) {
    err = "could not create file: " + filename;
    return false;
  }
  const Node& first = net.nodes[path.nodes.front()];
  const Node& last = net.nodes[path.nodes.back()];

  file << "# Shortest path\n";
  file << "# CRS: " << net.crs << "\n";
  file << "# Cost: " << costName(kind) << " (" << costUnit(net, kind) << ")\n";
  file << "# Nodes: " << path.nodes.size() << "\n";
  file << "# Start node: " << first.id << "\n";
  file << "# End node: " << last.id << "\n";
  file << "# Geometry column: geometry\n";
  file << "id,node_start,node_end,total_cost,total_length,total_time,node_count,geometry\n";

  std::string wkt;
  if (path.nodes.size() == 1) {
    wkt = "POINT(" + num(first.x, 12) + " " + num(first.y, 12) + ")";
  } else {
    wkt = "LINESTRING(";
    for (size_t i = 0; i < path.nodes.size(); ++i) {
      const Node& n = net.nodes[path.nodes[i]];
      if (i > 0)
        wkt += ", ";
      wkt += num(n.x, 12) + " " + num(n.y, 12);
    }
    wkt += ")";
  }
  file << "1," << first.id << "," << last.id << "," << num(total_cost) << "," << num(total_length)
       << "," << num(total_time) << "," << path.nodes.size() << ",\"" << wkt << "\"\n";
  file.flush();
  if (!file.good()) {
    err = "error while writing file: " + filename;
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char* argv[]) {
  std::string network_file, origin_str, dest_str, output_file = "path.csv";
  CostKind kind = CostKind::Length;
  bool verbose = false;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto value = [&](const char* name) -> const char* {
      if (i + 1 >= argc) {
        std::cerr << "Error: " << name << " needs a value\n";
        return nullptr;
      }
      return argv[++i];
    };
    if (arg == "-help" || arg == "-h") {
      printUsage();
      return 0;
    } else if (arg == "-from") {
      const char* v = value("-from");
      if (!v)
        return 1;
      origin_str = v;
    } else if (arg == "-to") {
      const char* v = value("-to");
      if (!v)
        return 1;
      dest_str = v;
    } else if (arg == "-output") {
      const char* v = value("-output");
      if (!v)
        return 1;
      output_file = v;
    } else if (arg == "-cost") {
      const char* v = value("-cost");
      if (!v)
        return 1;
      if (!parseCostKind(v, kind)) {
        std::cerr << "Error: -cost must be 'length' or 'time', not '" << v << "'\n";
        return 1;
      }
    } else if (arg == "-verbose") {
      verbose = true;
    } else if (!arg.empty() && arg[0] == '-') {
      std::cerr << "Error: Unknown option: " << arg << "\n";
      printUsage();
      return 1;
    } else if (network_file.empty()) {
      network_file = arg;
    } else {
      std::cerr << "Error: Unexpected argument: " << arg << "\n";
      printUsage();
      return 1;
    }
  }

  if (network_file.empty() || origin_str.empty() || dest_str.empty()) {
    std::cerr << "Error: Missing required arguments (network file, -from and -to)\n";
    printUsage();
    return 1;
  }

  Network net;
  std::string err;
  if (!readNetwork(network_file, net, err)) {
    std::cerr << "Error: " << err << "\n";
    return 1;
  }
  size_t load_errors = 0;
  for (const auto& is : net.load_issues)
    if (is.severity == "error")
      load_errors++;
  if (load_errors)
    std::cerr << "Warning: " << load_errors << " problem(s) while reading " << network_file
              << " (run spatial_network_info -validate for details)\n";

  int start = -1, end = -1;
  if (!findNode(net, origin_str, start, err)) {
    std::cerr << "Error: Origin: " << err << "\n";
    return 1;
  }
  if (!findNode(net, dest_str, end, err)) {
    std::cerr << "Error: Destination: " << err << "\n";
    return 1;
  }

  std::cout << "========================================\n";
  std::cout << "  SHORTEST PATH\n";
  std::cout << "========================================\n\n";
  std::cout << "Network: " << network_file << " (" << net.nodes.size() << " nodes, "
            << net.edges.size() << " edges)\n";
  std::cout << "Cost: " << costName(kind) << " (" << costUnit(net, kind) << ")\n";
  std::cout << "Origin node: " << net.nodes[start].id
            << (net.nodes[start].name.empty() ? "" : " (" + net.nodes[start].name + ")") << "\n";
  std::cout << "Destination node: " << net.nodes[end].id
            << (net.nodes[end].name.empty() ? "" : " (" + net.nodes[end].name + ")") << "\n\n";

  const Graph graph = buildGraph(net, kind);
  PathTree tree;
  dijkstra(graph, start, tree, end);

  Path path;
  if (!buildPath(tree, start, end, path)) {
    std::cerr << "Error: No path found from node " << net.nodes[start].id << " to node "
              << net.nodes[end].id << (net.edges.empty() ? "" : " (the network may be disconnected or have one-way edges in the way)")
              << "\n";
    return 1;
  }

  double total_length = 0, total_time = 0;
  for (int ei : path.edges) {
    total_length += net.edges[ei].length;
    total_time += net.edges[ei].travel_time;
  }
  const double total_cost = tree.dist[end];

  std::cout << "Path found with " << path.nodes.size() << " nodes\n";
  std::cout << "Total length: " << num(total_length) << " " << (net.length_unit == "meters" ? "m" : "units") << "\n";
  std::cout << "Total time: " << num(total_time) << " min\n\n";

  if (verbose) {
    std::cout << "Path nodes:";
    for (int v : path.nodes)
      std::cout << " " << net.nodes[v].id;
    std::cout << "\n\nSegment details:\n";
    for (size_t i = 0; i < path.edges.size(); ++i) {
      const Edge& e = net.edges[path.edges[i]];
      std::cout << "  " << net.nodes[path.nodes[i]].id << " -> " << net.nodes[path.nodes[i + 1]].id
                << ": length " << num(e.length) << ", time " << num(e.travel_time) << " min\n";
    }
    std::cout << "\n";
  }

  if (!writePathCSV(net, path, kind, total_cost, total_length, total_time, output_file, err)) {
    std::cerr << "Error: " << err << "\n";
    return 1;
  }
  std::cout << "Path written to: " << output_file << "\n";

  std::cout << "\n========================================\n";
  std::cout << "  PATH COMPLETE\n";
  std::cout << "========================================\n";
  return 0;
}
