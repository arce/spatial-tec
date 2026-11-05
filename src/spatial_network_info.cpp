// spatial_network_info -- informacion y validacion de una red .network.
//
// Sin -validate resume la red. Con -validate revisa la integridad y la
// conectividad (nodos aislados, componentes desconectadas, aristas repetidas,
// costos invalidos, ...) y devuelve 1 si encuentra errores.

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <cmath>
#include <set>
#include <tuple>
#include <string>
#include <unordered_map>
#include <vector>

#include "../include/core/spatial_network.hpp"
#include "../include/core/spatial_string.hpp"

using namespace SpatialNet;

namespace {

void printUsage() {
  std::cerr << "spatial_network_info - Network information and connectivity validation\n\n";
  std::cerr << "Usage: spatial_network_info <network.network> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -validate         Also validate the network (isolated nodes, disconnected components,\n";
  std::cerr << "                    duplicate or zero-length edges, invalid costs)\n";
  std::cerr << "  -report <file>    With -validate, save the issues as a CSV\n";
  std::cerr << "                    (severity,issue,element,id,detail)\n";
  std::cerr << "  -verbose          List every component and every issue\n";
  std::cerr << "  -help             Show this help\n\n";
  std::cerr << "Exit status: 0 if the file was read and no validation errors were found; 1 otherwise.\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_network_info roads.network\n";
  std::cerr << "  spatial_network_info roads.network -validate -report issues.csv\n";
}

std::string csvQuote(const std::string& s) {
  return csvField(s);
}

}  // namespace

int main(int argc, char* argv[]) {
  std::string network_file, report_file;
  bool validate = false, verbose = false;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-help" || arg == "-h") {
      printUsage();
      return 0;
    } else if (arg == "-validate") {
      validate = true;
    } else if (arg == "-report") {
      if (i + 1 >= argc) {
        std::cerr << "Error: -report needs a value\n";
        return 1;
      }
      report_file = argv[++i];
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
  if (network_file.empty()) {
    printUsage();
    return 1;
  }
  if (!report_file.empty() && !validate)
    std::cerr << "Warning: -report only has an effect together with -validate\n";

  Network net;
  std::string err;
  if (!readNetwork(network_file, net, err)) {
    std::cerr << "Error: " << err << "\n";
    return 1;
  }

  const size_t n = net.nodes.size(), m = net.edges.size();
  const std::string len_unit = net.length_unit == "meters" ? "m" : "units";

  // ---- estadisticas
  double minx = kInf, miny = kInf, maxx = -kInf, maxy = -kInf;
  for (const auto& nd : net.nodes) {
    minx = std::min(minx, nd.x);
    maxx = std::max(maxx, nd.x);
    miny = std::min(miny, nd.y);
    maxy = std::max(maxy, nd.y);
  }
  double total_len = 0, min_len = kInf, max_len = 0, total_time = 0;
  size_t oneway = 0, finite_edges = 0;
  for (const auto& e : net.edges) {
    if (e.oneway)
      oneway++;
    if (std::isfinite(e.length) && e.length >= 0) {
      total_len += e.length;
      min_len = std::min(min_len, e.length);
      max_len = std::max(max_len, e.length);
      finite_edges++;
    }
    if (std::isfinite(e.travel_time) && e.travel_time >= 0)
      total_time += e.travel_time;
  }
  std::map<int, size_t> degree_hist;  // 5 = 5 o mas
  size_t named = 0, with_centrality = 0;
  for (const auto& nd : net.nodes) {
    degree_hist[std::min(nd.degree, 5)]++;
    if (!nd.name.empty())
      named++;
    if (!std::isnan(nd.bc))
      with_centrality++;
  }
  const Components weak = weakComponents(net);

  std::cout << "========================================\n";
  std::cout << "  NETWORK INFO" << (validate ? " + VALIDATION" : "") << "\n";
  std::cout << "========================================\n\n";
  std::cout << "File: " << network_file << (net.legacy ? " (format version 1)" : " (format version 2)") << "\n";
  std::cout << "CRS: " << net.crs << (net.geographic ? " (geographic)" : "") << "\n";
  std::cout << "Length unit: " << net.length_unit << "\n";
  std::cout << "Nodes: " << n << " (" << named << " named)\n";
  std::cout << "Edges: " << m << " (" << oneway << " one-way, " << (m - oneway) << " two-way)\n";
  std::cout << "Bounding box: " << num(minx, 10) << ", " << num(miny, 10) << " - " << num(maxx, 10) << ", "
            << num(maxy, 10) << "\n";
  if (finite_edges) {
    std::cout << "Total length: " << num(total_len) << " " << len_unit << "\n";
    std::cout << "Edge length: min " << num(min_len) << ", mean " << num(total_len / finite_edges)
              << ", max " << num(max_len) << " " << len_unit << "\n";
  }
  std::cout << "Total travel time (all edges): " << num(total_time) << " min\n";
  std::cout << "Node degree:";
  for (const auto& kv : degree_hist)
    std::cout << "  " << kv.first << (kv.first == 5 ? "+" : "") << ": " << kv.second;
  std::cout << "\n";
  std::cout << "Connected components: " << weak.count << " (largest: " << weak.size[0] << " of " << n
            << " nodes, " << num(100.0 * weak.size[0] / n, 4) << "%)\n";
  if (with_centrality) {
    double max_bc = 0, max_cc = 0;
    int bc_node = net.nodes[0].id, cc_node = net.nodes[0].id;
    for (const auto& nd : net.nodes) {
      if (!std::isnan(nd.bc) && nd.bc > max_bc) {
        max_bc = nd.bc;
        bc_node = nd.id;
      }
      if (!std::isnan(nd.cc) && nd.cc > max_cc) {
        max_cc = nd.cc;
        cc_node = nd.id;
      }
    }
    std::cout << "Centralities: max betweenness " << num(max_bc, 6) << " (node " << bc_node
              << "), max closeness " << num(max_cc, 6) << " (node " << cc_node << ")\n";
  }

  if (verbose) {
    std::cout << "\nComponents (largest first):\n";
    for (int c = 0; c < weak.count; ++c) {
      int rep = -1;
      for (size_t i = 0; i < n; ++i)
        if (weak.label[i] == c) {
          rep = net.nodes[i].id;
          break;
        }
      std::cout << "  #" << (c + 1) << ": " << weak.size[c] << " nodes, " << weak.edge_count[c]
                << " edges (e.g. node " << rep << ")\n";
      if (c >= 49 && weak.count > 50) {
        std::cout << "  ... " << (weak.count - 50) << " more\n";
        break;
      }
    }
  }

  if (!validate)
    return 0;

  // ---- validacion
  std::vector<Issue> issues = net.load_issues;
  auto add = [&](const char* sev, const char* issue, const char* element, const std::string& id,
                 const std::string& detail) { issues.push_back({sev, issue, element, id, detail}); };

  // nodos: aislados, coincidentes
  std::map<std::pair<long long, long long>, int> seen_xy;
  for (size_t i = 0; i < n; ++i) {
    const Node& nd = net.nodes[i];
    if (nd.degree == 0)
      add("warning", "ISOLATED_NODE", "node", std::to_string(nd.id),
          "node without edges at " + num(nd.x, 12) + ", " + num(nd.y, 12));
    const auto key = std::make_pair(std::llround(nd.x * 1e8), std::llround(nd.y * 1e8));
    auto it = seen_xy.find(key);
    if (it != seen_xy.end())
      add("warning", "COINCIDENT_NODES", "node", std::to_string(nd.id),
          "same position as node " + std::to_string(it->second) + " (missing -snap?)");
    else
      seen_xy[key] = nd.id;
  }

  // aristas: costos, longitud cero, bucles, repetidas
  std::set<std::tuple<int, int, int>> seen_edge;
  for (size_t i = 0; i < m; ++i) {
    const Edge& e = net.edges[i];
    const std::string id = std::to_string(e.id >= 0 ? e.id : static_cast<int>(i));
    const std::string ends = std::to_string(net.nodes[e.source].id) + " -> " + std::to_string(net.nodes[e.target].id);
    if (!std::isfinite(e.length) || e.length < 0)
      add("error", "BAD_COST", "edge", id, ends + ": invalid length");
    else if (e.length == 0)
      add("warning", "ZERO_LENGTH_EDGE", "edge", id, ends + ": length 0");
    if (!std::isfinite(e.travel_time) || e.travel_time < 0)
      add("error", "BAD_COST", "edge", id, ends + ": invalid travel_time");
    if (e.source == e.target) {
      add("warning", "SELF_LOOP", "edge", id, "edge starts and ends at node " + std::to_string(net.nodes[e.source].id));
      continue;
    }
    const auto key = e.oneway ? std::make_tuple(e.source, e.target, 1)
                              : std::make_tuple(std::min(e.source, e.target), std::max(e.source, e.target), 0);
    if (!seen_edge.insert(key).second)
      add("warning", "DUPLICATE_EDGE", "edge", id, ends + ": another edge joins the same nodes");
  }

  // componentes desconectadas
  for (int c = 1; c < weak.count; ++c) {
    if (weak.size[c] == 1 && weak.edge_count[c] == 0)
      continue;  // ya reportado como ISOLATED_NODE
    int rep = -1;
    for (size_t i = 0; i < n; ++i)
      if (weak.label[i] == c) {
        rep = static_cast<int>(i);
        break;
      }
    add("warning", "DISCONNECTED_COMPONENT", "component", std::to_string(c + 1),
        std::to_string(weak.size[c]) + " nodes, " + std::to_string(weak.edge_count[c]) +
            " edges, not connected to the main network (e.g. node " + std::to_string(net.nodes[rep].id) +
            " at " + num(net.nodes[rep].x, 12) + ", " + num(net.nodes[rep].y, 12) + ")");
  }

  // conectividad dirigida, dentro de la componente conexa principal (las
  // componentes sueltas ya se reportaron arriba)
  if (oneway > 0) {
    const Components strong = strongComponents(net);
    std::map<int, int> in_main;  // componente fuerte -> nodos dentro de la principal
    int main_nodes = 0;
    for (size_t i = 0; i < n; ++i)
      if (weak.label[i] == 0) {
        in_main[strong.label[i]]++;
        main_nodes++;
      }
    int largest = 0;
    for (const auto& kv : in_main)
      largest = std::max(largest, kv.second);
    if (in_main.size() > 1)
      add("warning", "NOT_STRONGLY_CONNECTED", "network", "-",
          "in the main connected component (" + std::to_string(main_nodes) + " nodes) one-way edges leave " +
              std::to_string(in_main.size()) + " groups of nodes that cannot all reach each other (largest: " +
              std::to_string(largest) + " nodes); " + std::to_string(main_nodes - largest) +
              " node(s) are outside it");
  }

  size_t errors = 0, warnings = 0;
  std::map<std::string, size_t> by_issue;
  for (const auto& is : issues) {
    (is.severity == "error" ? errors : warnings)++;
    by_issue[is.issue]++;
  }

  std::cout << "\n----------------------------------------\n";
  std::cout << "  VALIDATION\n";
  std::cout << "----------------------------------------\n";
  if (issues.empty()) {
    std::cout << "No issues found.\n";
  } else {
    std::map<std::string, size_t> printed;
    for (const auto& is : issues) {
      if (!verbose && printed[is.issue]++ >= 10)
        continue;
      std::cout << (is.severity == "error" ? "[ERROR]   " : "[WARNING] ") << is.issue << " ("
                << is.element << " " << is.id << "): " << is.detail << "\n";
    }
    if (!verbose)
      for (const auto& kv : by_issue)
        if (kv.second > 10)
          std::cout << "  ... " << (kv.second - 10) << " more " << kv.first << " (use -verbose or -report)\n";
    std::cout << "\nSummary: " << errors << " error(s), " << warnings << " warning(s)\n";
    for (const auto& kv : by_issue)
      std::cout << "  " << kv.first << ": " << kv.second << "\n";
  }

  if (!report_file.empty()) {
    std::ofstream rep(report_file);
    if (!rep.is_open()) {
      std::cerr << "Error: could not create report file: " << report_file << "\n";
      return 1;
    }
    rep << "severity,issue,element,id,detail\n";
    for (const auto& is : issues)
      rep << is.severity << "," << is.issue << "," << is.element << "," << csvQuote(is.id) << ","
          << csvQuote(is.detail) << "\n";
    rep.flush();
    if (!rep.good()) {
      std::cerr << "Error: error while writing report file: " << report_file << "\n";
      return 1;
    }
    std::cout << "Report written to: " << report_file << "\n";
  }

  return errors > 0 ? 1 : 0;
}
