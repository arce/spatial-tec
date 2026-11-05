// spatial_network_service -- areas de servicio sobre una red .network.
//
//   -mode alloc : tramos de calle alcanzables desde un nodo con costo
//                 acumulado <= -max_cost (los tramos se cortan donde se agota
//                 el costo). Salida: una linea por tramo.
//   -mode iso   : isocronas. Para cada valor de -intervals, un poligono que
//                 envuelve los tramos alcanzables (zona a menos de -buffer de
//                 la red alcanzada, calculada sobre una cuadricula).

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "../include/core/spatial_network.hpp"
#include "../include/core/spatial_string.hpp"

using namespace SpatialNet;

namespace {

void printUsage() {
  std::cerr << "spatial_network_service - Service areas and isochrones from a source node\n\n";
  std::cerr << "Usage: spatial_network_service <network.network> -from <node> -mode <alloc|iso> [options]\n\n";
  std::cerr << "Modes:\n";
  std::cerr << "  -mode alloc    Street segments reachable under -max_cost (cut where the cost runs out)\n";
  std::cerr << "  -mode iso      Isochrones: one polygon per value in -intervals\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -from <node>          Origin node, ID or name (required)\n";
  std::cerr << "  -mode <alloc|iso>     Service mode (required)\n";
  std::cerr << "  -cost <kind>          time (minutes, default) or length (metres)\n";
  std::cerr << "  -max_cost <value>     Maximum accumulated cost (mode alloc; default: no limit)\n";
  std::cerr << "  -intervals <list>     Comma-separated cost breaks, e.g. 5,10,15 (mode iso; required)\n";
  std::cerr << "  -buffer <dist>        Mode iso: distance (CRS units) the polygon extends around the\n";
  std::cerr << "                        reached streets (default: about 1/60 of the reached area's diagonal)\n";
  std::cerr << "  -output <file>        Output CSV file (default: service_result.csv)\n";
  std::cerr << "  -verbose              Show more details\n";
  std::cerr << "  -help                 Show this help\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_network_service roads.network -from 0 -mode alloc -max_cost 10 -output service_area.csv\n";
  std::cerr << "  spatial_network_service roads.network -from 0 -mode iso -intervals 5,10,15 -output isochrones.csv\n";
}

struct Piece {
  double x1, y1, x2, y2;
  int edge;
  int from_node, to_node;  // nodos de la arista
  double cost_min, cost_max;
  double length;
  bool partial;
};

struct Coverage {
  std::vector<Piece> pieces;
  size_t nodes = 0, edges = 0, full = 0, partial = 0;
  double length = 0;
};

Coverage coverage(const Network& net, const PathTree& tree, CostKind kind, double limit) {
  Coverage cov;
  const double inf = kInf;
  auto reach = [&](int v) { return std::isfinite(tree.dist[v]) && tree.dist[v] <= limit; };
  for (size_t v = 0; v < net.nodes.size(); ++v)
    if (reach(static_cast<int>(v)))
      cov.nodes++;

  for (size_t ei = 0; ei < net.edges.size(); ++ei) {
    const Edge& e = net.edges[ei];
    const double w = edgeCost(e, kind);
    if (e.source == e.target || !std::isfinite(w) || w < 0)
      continue;
    const Node &nu = net.nodes[e.source], &nv = net.nodes[e.target];
    const bool ru = reach(e.source), rv = reach(e.target) && !e.oneway;
    const double du = ru ? tree.dist[e.source] : inf;
    const double dv = rv ? tree.dist[e.target] : inf;
    auto frac = [&](double d) { return w > 0 ? (limit - d) / w : inf; };
    const double fu = ru ? frac(du) : -1.0, fv = rv ? frac(dv) : -1.0;
    const bool full = (ru && fu >= 1) || (rv && fv >= 1) || (ru && rv && fu + fv >= 1);

    auto add = [&](double x1, double y1, double x2, double y2, double cmin, double cmax,
                   double len, bool partial) {
      Piece p{x1, y1, x2, y2, e.id >= 0 ? e.id : static_cast<int>(ei), nu.id, nv.id, cmin, cmax, len, partial};
      cov.pieces.push_back(p);
      cov.length += len;
      (partial ? cov.partial : cov.full)++;
    };
    const size_t before = cov.pieces.size();
    if (full) {
      double cmin, cmax;
      if (ru && rv) {
        cmin = std::min(du, dv);
        cmax = (du + dv + w) / 2.0;
      } else if (ru) {
        cmin = du;
        cmax = du + w;
      } else {
        cmin = dv;
        cmax = dv + w;
      }
      add(nu.x, nu.y, nv.x, nv.y, cmin, cmax, e.length, false);
    } else {
      if (ru && fu > 0)
        add(nu.x, nu.y, nu.x + fu * (nv.x - nu.x), nu.y + fu * (nv.y - nu.y), du, limit,
            fu * e.length, true);
      if (rv && fv > 0)
        add(nv.x, nv.y, nv.x + fv * (nu.x - nv.x), nv.y + fv * (nu.y - nv.y), dv, limit,
            fv * e.length, true);
    }
    if (cov.pieces.size() > before)
      cov.edges++;
  }
  return cov;
}

std::string lineWkt(const Piece& p) {
  return "LINESTRING(" + num(p.x1, 12) + " " + num(p.y1, 12) + ", " + num(p.x2, 12) + " " +
         num(p.y2, 12) + ")";
}

// ---------------------------------------------------------- isocronas (cuadricula)

double distPointSeg(double px, double py, double ax, double ay, double bx, double by) {
  const double dx = bx - ax, dy = by - ay;
  const double len2 = dx * dx + dy * dy;
  double t = len2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0;
  t = std::min(1.0, std::max(0.0, t));
  return std::hypot(px - (ax + t * dx), py - (ay + t * dy));
}

struct IPt {
  int x, y;
};
struct DEdge {
  IPt a, b;
  bool used = false;
};

long long ringArea2(const std::vector<IPt>& r) {  // 2 * area con signo
  long long s = 0;
  for (size_t i = 0; i < r.size(); ++i) {
    const IPt &p = r[i], &q = r[(i + 1) % r.size()];
    s += static_cast<long long>(p.x) * q.y - static_cast<long long>(q.x) * p.y;
  }
  return s;
}

bool pointInRing(double px, double py, const std::vector<IPt>& r) {
  bool in = false;
  for (size_t i = 0, j = r.size() - 1; i < r.size(); j = i++) {
    if ((r[i].y > py) != (r[j].y > py) &&
        px < (r[j].x - r[i].x) * (py - r[i].y) / static_cast<double>(r[j].y - r[i].y) + r[i].x)
      in = !in;
  }
  return in;
}

// Contornos de las celdas activas: anillos exteriores (antihorarios) y huecos.
std::string gridToWkt(const std::vector<char>& on, int nx, int ny, double x0, double y0, double cell) {
  auto filled = [&](int i, int j) { return i >= 0 && j >= 0 && i < nx && j < ny && on[static_cast<size_t>(j) * nx + i]; };
  std::vector<DEdge> edges;
  for (int j = 0; j < ny; ++j)
    for (int i = 0; i < nx; ++i) {
      if (!filled(i, j))
        continue;
      if (!filled(i, j - 1)) edges.push_back({{i, j}, {i + 1, j}});
      if (!filled(i + 1, j)) edges.push_back({{i + 1, j}, {i + 1, j + 1}});
      if (!filled(i, j + 1)) edges.push_back({{i + 1, j + 1}, {i, j + 1}});
      if (!filled(i - 1, j)) edges.push_back({{i, j + 1}, {i, j}});
    }
  if (edges.empty())
    return "";

  auto vkey = [&](IPt p) { return static_cast<long long>(p.y) * (nx + 1) + p.x; };
  std::unordered_map<long long, std::vector<int>> out;
  for (size_t k = 0; k < edges.size(); ++k)
    out[vkey(edges[k].a)].push_back(static_cast<int>(k));

  std::vector<std::vector<IPt>> outers, holes;
  for (size_t e0 = 0; e0 < edges.size(); ++e0) {
    if (edges[e0].used)
      continue;
    std::vector<IPt> ring;
    int cur = static_cast<int>(e0);
    while (true) {
      edges[cur].used = true;
      ring.push_back(edges[cur].a);
      const IPt v = edges[cur].b;
      const int dx = v.x - edges[cur].a.x, dy = v.y - edges[cur].a.y;
      int next = -1;
      for (int cand : out[vkey(v)]) {
        if (edges[cand].used && cand != static_cast<int>(e0))
          continue;
        if (next < 0) {
          next = cand;
        } else {
          // en un vertice compartido por dos celdas diagonales se gira a la
          // izquierda, para que cada celda conserve su propio anillo
          const int cx = edges[cand].b.x - edges[cand].a.x, cy = edges[cand].b.y - edges[cand].a.y;
          if (dx * cy - dy * cx > 0)
            next = cand;
        }
      }
      if (next < 0 || next == static_cast<int>(e0))
        break;
      cur = next;
    }
    // quita vertices colineales
    std::vector<IPt> r;
    const size_t m = ring.size();
    for (size_t i = 0; i < m; ++i) {
      const IPt &p = ring[(i + m - 1) % m], &c = ring[i], &q = ring[(i + 1) % m];
      if ((c.x - p.x) * (q.y - c.y) - (c.y - p.y) * (q.x - c.x) != 0)
        r.push_back(c);
    }
    if (r.size() < 4)
      continue;
    (ringArea2(r) > 0 ? outers : holes).push_back(r);
  }

  std::vector<std::vector<std::vector<IPt>>> polys;  // [poligono][0 = exterior, 1.. = huecos]
  std::vector<long long> areas;
  for (const auto& o : outers) {
    polys.push_back({o});
    areas.push_back(ringArea2(o));
  }
  for (const auto& h : holes) {
    const double mx = (h[0].x + h[1].x) / 2.0, my = (h[0].y + h[1].y) / 2.0;
    int best = -1;
    for (size_t k = 0; k < polys.size(); ++k)
      if (pointInRing(mx, my, polys[k][0]) && (best < 0 || areas[k] < areas[best]))
        best = static_cast<int>(k);
    if (best >= 0)
      polys[best].push_back(h);
  }

  auto ringWkt = [&](const std::vector<IPt>& r) {
    std::string s = "(";
    for (size_t i = 0; i <= r.size(); ++i) {
      const IPt& p = r[i % r.size()];
      if (i)
        s += ", ";
      s += num(x0 + p.x * cell, 12) + " " + num(y0 + p.y * cell, 12);
    }
    return s + ")";
  };
  auto polyWkt = [&](const std::vector<std::vector<IPt>>& p) {
    std::string s = "(";
    for (size_t i = 0; i < p.size(); ++i)
      s += (i ? ", " : "") + ringWkt(p[i]);
    return s + ")";
  };
  if (polys.size() == 1)
    return "POLYGON" + polyWkt(polys[0]);
  std::string s = "MULTIPOLYGON(";
  for (size_t k = 0; k < polys.size(); ++k)
    s += (k ? ", " : "") + polyWkt(polys[k]);
  return s + ")";
}

std::string isochroneWkt(const std::vector<Piece>& pieces, double ox, double oy, double buffer,
                         double cell) {
  double minx = ox, maxx = ox, miny = oy, maxy = oy;
  for (const auto& p : pieces) {
    minx = std::min({minx, p.x1, p.x2});
    maxx = std::max({maxx, p.x1, p.x2});
    miny = std::min({miny, p.y1, p.y2});
    maxy = std::max({maxy, p.y1, p.y2});
  }
  const double pad = buffer + 2 * cell;
  const double x0 = minx - pad, y0 = miny - pad;
  // limite de tamano de la cuadricula (celdas totales)
  const double max_cells = 4.0e6;
  double c = cell;
  double fx = (maxx + pad - x0) / c, fy = (maxy + pad - y0) / c;
  if (fx * fy > max_cells)
    c *= std::sqrt(fx * fy / max_cells);
  const int nx = static_cast<int>((maxx + pad - x0) / c) + 1;
  const int ny = static_cast<int>((maxy + pad - y0) / c) + 1;
  std::vector<char> on(static_cast<size_t>(nx) * ny, 0);

  auto mark = [&](double ax, double ay, double bx, double by) {
    const int i0 = std::max(0, static_cast<int>((std::min(ax, bx) - buffer - x0) / c));
    const int i1 = std::min(nx - 1, static_cast<int>((std::max(ax, bx) + buffer - x0) / c));
    const int j0 = std::max(0, static_cast<int>((std::min(ay, by) - buffer - y0) / c));
    const int j1 = std::min(ny - 1, static_cast<int>((std::max(ay, by) + buffer - y0) / c));
    for (int j = j0; j <= j1; ++j)
      for (int i = i0; i <= i1; ++i)
        if (distPointSeg(x0 + (i + 0.5) * c, y0 + (j + 0.5) * c, ax, ay, bx, by) <= buffer)
          on[static_cast<size_t>(j) * nx + i] = 1;
  };
  mark(ox, oy, ox, oy);
  for (const auto& p : pieces)
    mark(p.x1, p.y1, p.x2, p.y2);
  return gridToWkt(on, nx, ny, x0, y0, c);
}

}  // namespace

int main(int argc, char* argv[]) {
  std::string network_file, from_str, mode, intervals_str, output_file = "service_result.csv";
  CostKind kind = CostKind::Time;
  double max_cost = kInf, buffer = -1.0;
  bool verbose = false, max_cost_given = false;

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
      if (!v) return 1;
      from_str = v;
    } else if (arg == "-mode") {
      const char* v = value("-mode");
      if (!v) return 1;
      mode = v;
    } else if (arg == "-cost") {
      const char* v = value("-cost");
      if (!v) return 1;
      if (!parseCostKind(v, kind)) {
        std::cerr << "Error: -cost must be 'time' or 'length', not '" << v << "'\n";
        return 1;
      }
    } else if (arg == "-max_cost") {
      const char* v = value("-max_cost");
      if (!v) return 1;
      if (!parseDouble(v, max_cost) || max_cost <= 0) {
        std::cerr << "Error: invalid value for -max_cost: " << v << " (must be a positive number)\n";
        return 1;
      }
      max_cost_given = true;
    } else if (arg == "-intervals") {
      const char* v = value("-intervals");
      if (!v) return 1;
      intervals_str = v;
    } else if (arg == "-buffer") {
      const char* v = value("-buffer");
      if (!v) return 1;
      if (!parseDouble(v, buffer) || buffer <= 0) {
        std::cerr << "Error: invalid value for -buffer: " << v << " (must be a positive number)\n";
        return 1;
      }
    } else if (arg == "-output") {
      const char* v = value("-output");
      if (!v) return 1;
      output_file = v;
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
  if (from_str.empty()) {
    std::cerr << "Error: -from is required\n";
    printUsage();
    return 1;
  }
  if (mode != "alloc" && mode != "iso") {
    std::cerr << "Error: -mode is required and must be one of: alloc, iso\n";
    printUsage();
    return 1;
  }
  std::vector<double> intervals;
  if (mode == "iso") {
    if (intervals_str.empty()) {
      std::cerr << "Error: -intervals is required for -mode iso\n";
      printUsage();
      return 1;
    }
    for (const auto& tok : split(intervals_str, ',')) {
      double v = 0;
      if (!parseDouble(tok, v) || v <= 0) {
        std::cerr << "Error: invalid interval '" << tok << "' in -intervals (positive numbers separated by commas)\n";
        return 1;
      }
      intervals.push_back(v);
    }
    std::sort(intervals.begin(), intervals.end());
    intervals.erase(std::unique(intervals.begin(), intervals.end()), intervals.end());
    if (max_cost_given)
      std::cerr << "Warning: -max_cost is ignored in -mode iso (use -intervals)\n";
  } else {
    if (!intervals_str.empty())
      std::cerr << "Warning: -intervals is ignored in -mode alloc (use -max_cost)\n";
    if (buffer > 0)
      std::cerr << "Warning: -buffer is ignored in -mode alloc\n";
  }

  Network net;
  std::string err;
  if (!readNetwork(network_file, net, err)) {
    std::cerr << "Error: " << err << "\n";
    return 1;
  }
  int origin = -1;
  if (!findNode(net, from_str, origin, err)) {
    std::cerr << "Error: -from: " << err << "\n";
    return 1;
  }

  const std::string unit = costUnit(net, kind);
  std::cout << "========================================\n";
  std::cout << "  NETWORK SERVICE: " << mode << "\n";
  std::cout << "========================================\n\n";
  std::cout << "Network: " << network_file << " (" << net.nodes.size() << " nodes, " << net.edges.size() << " edges)\n";
  std::cout << "Origin node: " << net.nodes[origin].id
            << (net.nodes[origin].name.empty() ? "" : " (" + net.nodes[origin].name + ")") << "\n";
  std::cout << "Cost: " << costName(kind) << " (" << unit << ")\n";

  const double limit = mode == "iso" ? intervals.back() : max_cost;
  const Graph graph = buildGraph(net, kind);
  PathTree tree;
  dijkstra(graph, origin, tree, -1, limit);

  std::ofstream out(output_file);
  if (!out.is_open()) {
    std::cerr << "Error: Could not create file: " << output_file << "\n";
    return 1;
  }
  out << "# Service area\n# CRS: " << net.crs << "\n# Mode: " << mode << "\n# Origin node: "
      << net.nodes[origin].id << "\n# Cost: " << costName(kind) << " (" << unit << ")\n";

  const std::string len_unit = net.length_unit == "meters" ? "m" : "units";

  if (mode == "alloc") {
    const Coverage cov = coverage(net, tree, kind, limit);
    out << "# Max cost: " << (std::isfinite(limit) ? num(limit) : "none") << "\n";
    out << "# Reached nodes: " << cov.nodes << "\n# Edges: " << cov.full << " full, " << cov.partial << " partial\n";
    out << "# Geometry column: geometry\n";
    out << "edge_id,source,target,cost_min,cost_max,length,partial,geometry\n";
    for (const auto& p : cov.pieces)
      out << p.edge << "," << p.from_node << "," << p.to_node << "," << num(p.cost_min) << "," << num(p.cost_max)
          << "," << num(p.length) << "," << (p.partial ? 1 : 0) << ",\"" << lineWkt(p) << "\"\n";
    std::cout << "Max cost: " << (std::isfinite(limit) ? num(limit) + " " + unit : std::string("no limit")) << "\n\n";
    std::cout << "Reached nodes: " << cov.nodes << "\n";
    std::cout << "Street segments: " << cov.full << " full, " << cov.partial << " partial\n";
    std::cout << "Network length covered: " << num(cov.length) << " " << len_unit << "\n";
  } else {
    // buffer y tamano de celda comunes a todos los intervalos
    const Coverage biggest = coverage(net, tree, kind, intervals.back());
    const Node& o = net.nodes[origin];
    double minx = o.x, maxx = o.x, miny = o.y, maxy = o.y;
    for (const auto& p : biggest.pieces) {
      minx = std::min({minx, p.x1, p.x2});
      maxx = std::max({maxx, p.x1, p.x2});
      miny = std::min({miny, p.y1, p.y2});
      maxy = std::max({maxy, p.y1, p.y2});
    }
    double buf = buffer;
    if (buf <= 0) {
      buf = std::hypot(maxx - minx, maxy - miny) / 60.0;
      if (!(buf > 0)) {  // solo el origen: usa el largo medio de las aristas
        double s = 0;
        for (const auto& e : net.edges)
          s += std::hypot(net.nodes[e.source].x - net.nodes[e.target].x, net.nodes[e.source].y - net.nodes[e.target].y);
        buf = net.edges.empty() ? 1.0 : s / net.edges.size();
        if (!(buf > 0)) buf = 1.0;
      }
    }
    const double cell = buf / 3.0;
    out << "# Buffer: " << num(buf, 8) << " (CRS units); polygons follow a grid of " << num(cell, 8) << "\n";
    out << "# Geometry column: geometry\n";
    out << "interval,cost_limit,reached_nodes,reached_edges,network_length,geometry\n";
    std::cout << "Intervals: " << intervals_str << " (" << unit << ")\n";
    std::cout << "Buffer: " << num(buf, 8) << " CRS units\n\n";
    for (size_t k = 0; k < intervals.size(); ++k) {
      const Coverage cov = coverage(net, tree, kind, intervals[k]);
      const std::string wkt = isochroneWkt(cov.pieces, o.x, o.y, buf, cell);
      if (wkt.empty()) {
        std::cerr << "Warning: interval " << intervals[k] << " produced an empty polygon\n";
        continue;
      }
      out << (k + 1) << "," << num(intervals[k]) << "," << cov.nodes << "," << cov.edges << ","
          << num(cov.length) << ",\"" << wkt << "\"\n";
      std::cout << "Interval " << num(intervals[k]) << " " << unit << ": " << cov.nodes << " nodes, "
                << cov.edges << " edges, " << num(cov.length) << " " << len_unit << " of network\n";
      if (verbose)
        std::cout << "  polygon: " << (wkt.rfind("MULTI", 0) == 0 ? "multipart" : "single part") << "\n";
    }
  }

  out.flush();
  if (!out.good()) {
    std::cerr << "Error: error while writing file: " << output_file << "\n";
    return 1;
  }
  std::cout << "\nOutput written to: " << output_file << "\n";
  std::cout << "\n========================================\n";
  std::cout << "  SERVICE COMPLETE\n";
  std::cout << "========================================\n";
  return 0;
}
