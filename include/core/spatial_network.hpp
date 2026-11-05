#pragma once
// spatial_network.hpp -- modelo compartido del archivo .network (version 2).
//
// Lo usan spatial_network (construye), spatial_shortest_path (rutas),
// spatial_network_info (informacion/validacion) y spatial_network_service
// (areas de servicio / isocronas).
//
// Formato (texto, secciones '@'):
//
//   # Network file
//   # Version: 2
//   # CRS: EPSG:4326
//   @META
//   key,value
//   version,2
//   crs,EPSG:4326
//   length_unit,meters            (meters | crs_units)
//   @NODES
//   node_id,x,y,name,degree,degree_centrality,betweenness,closeness
//   @EDGES
//   edge_id,source,target,length,travel_time,speed,oneway
//
// - length: metros si la red es geografica (haversine) o ya esta en metros;
//   unidades del CRS en archivos antiguos (version 1).
// - travel_time: minutos.
// - oneway=1: solo se puede recorrer source -> target.
// - Las matrices todo-contra-todo de la version 1 ya no se guardan: las rutas
//   se calculan con Dijkstra bajo demanda. El lector de esta cabecera acepta
//   archivos version 1 (ignora las matrices).

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "spatial_string.hpp"

namespace SpatialNet {

inline constexpr double kEarthRadiusM = 6371008.8;
inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kInf = std::numeric_limits<double>::infinity();

// ---------------------------------------------------------------- utilidades

inline double haversineMeters(double lon1, double lat1, double lon2, double lat2) {
  const double p1 = lat1 * kPi / 180.0;
  const double p2 = lat2 * kPi / 180.0;
  const double dp = p2 - p1;
  const double dl = (lon2 - lon1) * kPi / 180.0;
  const double s1 = std::sin(dp / 2.0);
  const double s2 = std::sin(dl / 2.0);
  double a = s1 * s1 + std::cos(p1) * std::cos(p2) * s2 * s2;
  a = std::min(1.0, std::max(0.0, a));
  return 2.0 * kEarthRadiusM * std::asin(std::sqrt(a));
}

// true si el CRS (p.ej. "EPSG:4326", "WGS84") esta en grados.
inline bool isGeographicCRS(const std::string& crs) {
  const std::string c = toLower(trim(crs));
  if (c.empty())
    return false;
  const size_t p = c.find("epsg:");
  if (p != std::string::npos) {
    const int code = std::atoi(c.c_str() + p + 5);
    static const int geographic[] = {4326, 4269, 4258, 4283, 4617, 4674, 4019,
                                     4322, 4167, 4230, 4267, 4979};
    for (int g : geographic)
      if (g == code)
        return true;
    return false;
  }
  static const char* words[] = {"wgs84", "wgs 84", "wgs_84", "longlat", "lonlat",
                                "latlon", "geographic", "degree"};
  for (const char* w : words)
    if (c.find(w) != std::string::npos)
      return true;
  return false;
}

// Lectura estricta de numeros: toda la cadena debe ser el numero.
inline bool parseDouble(const std::string& s, double& out) {
  const std::string t = trim(s);
  if (t.empty())
    return false;
  char* end = nullptr;
  errno = 0;
  const double v = std::strtod(t.c_str(), &end);
  if (end == t.c_str() || *end != '\0' || errno == ERANGE || !std::isfinite(v))
    return false;
  out = v;
  return true;
}

inline bool parseInt(const std::string& s, int& out) {
  const std::string t = trim(s);
  if (t.empty())
    return false;
  char* end = nullptr;
  errno = 0;
  const long v = std::strtol(t.c_str(), &end, 10);
  if (end == t.c_str() || *end != '\0' || errno == ERANGE || v < std::numeric_limits<int>::min() ||
      v > std::numeric_limits<int>::max())
    return false;
  out = static_cast<int>(v);
  return true;
}

inline bool isUnsignedInteger(const std::string& s) {
  const std::string t = trim(s);
  if (t.empty())
    return false;
  for (char c : t)
    if (c < '0' || c > '9')
      return false;
  return true;
}

inline std::string num(double v, int precision = 10) {
  std::ostringstream o;
  o << std::setprecision(precision) << v;
  return o.str();
}

// Campo CSV: el lector del proyecto (splitCSV) no entiende comillas escapadas,
// asi que las comillas dobles internas se sustituyen por simples.
inline std::string csvField(std::string s) {
  std::replace(s.begin(), s.end(), '"', '\'');
  std::replace(s.begin(), s.end(), '\n', ' ');
  std::replace(s.begin(), s.end(), '\r', ' ');
  if (s.find(',') != std::string::npos)
    return "\"" + s + "\"";
  return s;
}

// ------------------------------------------------------------------- modelo

struct Node {
  int id = 0;
  double x = 0, y = 0;
  std::string name;
  int degree = 0;
  double dc = std::nan("");  // degree_centrality
  double bc = std::nan("");  // betweenness (normalizada)
  double cc = std::nan("");  // closeness (normalizada)
};

struct Edge {
  int id = -1;     // edge_id del archivo (o posicion si no lo trae)
  int source = 0;  // indices en Network::nodes
  int target = 0;
  double length = 0;       // metros (o unidades del CRS en archivos v1)
  double travel_time = 0;  // minutos
  double speed = 0;        // km/h (0 = desconocida)
  bool oneway = false;     // solo source -> target
};

struct Issue {
  std::string severity;  // "error" | "warning"
  std::string issue;
  std::string element;
  std::string id;
  std::string detail;
};

struct Network {
  std::string crs = "EPSG:4326";
  bool geographic = true;
  bool legacy = false;  // archivo version 1
  std::string length_unit = "meters";
  std::vector<Node> nodes;
  std::vector<Edge> edges;
  std::unordered_map<int, int> id_to_index;
  std::vector<Issue> load_issues;  // problemas detectados al leer
};

enum class CostKind { Length, Time };

inline bool parseCostKind(const std::string& s, CostKind& k) {
  const std::string t = toLower(trim(s));
  if (t == "length" || t == "distance") {
    k = CostKind::Length;
    return true;
  }
  if (t == "time" || t == "travel_time") {
    k = CostKind::Time;
    return true;
  }
  return false;
}

inline double edgeCost(const Edge& e, CostKind k) {
  return k == CostKind::Length ? e.length : e.travel_time;
}

inline std::string costUnit(const Network& n, CostKind k) {
  if (k == CostKind::Time)
    return "min";
  return n.length_unit == "meters" ? "m" : "units";
}

inline std::string costName(CostKind k) {
  return k == CostKind::Length ? "length" : "time";
}

inline void computeDegrees(Network& net) {
  for (auto& n : net.nodes)
    n.degree = 0;
  for (const auto& e : net.edges) {
    net.nodes[e.source].degree++;
    net.nodes[e.target].degree++;
  }
  const double denom = net.nodes.size() > 1 ? static_cast<double>(net.nodes.size() - 1) : 0.0;
  for (auto& n : net.nodes)
    n.dc = denom > 0 ? n.degree / denom : 0.0;
}

// -------------------------------------------------------------- lectura/escritura

inline bool writeNetwork(const Network& net, const std::string& filename, std::string& err) {
  std::ofstream f(filename);
  if (!f.is_open()) {
    err = "could not create file: " + filename;
    return false;
  }
  f << "# Network file\n";
  f << "# Version: 2\n";
  f << "# CRS: " << net.crs << "\n";
  f << "# Nodes: " << net.nodes.size() << "\n";
  f << "# Edges: " << net.edges.size() << "\n\n";

  f << "@META\nkey,value\n";
  f << "version,2\n";
  f << "crs," << csvField(net.crs) << "\n";
  f << "length_unit," << net.length_unit << "\n\n";

  f << "@NODES\n";
  f << "node_id,x,y,name,degree,degree_centrality,betweenness,closeness\n";
  for (const auto& n : net.nodes) {
    f << n.id << "," << num(n.x, 12) << "," << num(n.y, 12) << "," << csvField(n.name) << ","
      << n.degree << ",";
    f << (std::isnan(n.dc) ? "" : num(n.dc, 8)) << ",";
    f << (std::isnan(n.bc) ? "" : num(n.bc, 8)) << ",";
    f << (std::isnan(n.cc) ? "" : num(n.cc, 8)) << "\n";
  }
  f << "\n@EDGES\n";
  f << "edge_id,source,target,length,travel_time,speed,oneway\n";
  for (size_t i = 0; i < net.edges.size(); ++i) {
    const auto& e = net.edges[i];
    f << (e.id >= 0 ? e.id : static_cast<int>(i)) << "," << net.nodes[e.source].id << "," << net.nodes[e.target].id << ","
      << num(e.length, 10) << "," << num(e.travel_time, 10) << "," << num(e.speed, 6) << ","
      << (e.oneway ? 1 : 0) << "\n";
  }
  f.flush();
  if (!f.good()) {
    err = "error while writing file: " + filename;
    return false;
  }
  return true;
}

namespace detail {
inline int colIndex(const std::vector<std::string>& cols, std::initializer_list<const char*> names) {
  for (const char* n : names)
    for (size_t i = 0; i < cols.size(); ++i)
      if (cols[i] == n)
        return static_cast<int>(i);
  return -1;
}
inline bool truthy(const std::string& s) {
  const std::string t = toLower(trim(s));
  return t == "1" || t == "true" || t == "yes" || t == "y";
}
struct RawEdge {
  int id, source_id, target_id, row;
  double length, travel_time, speed;
  bool oneway;
};
}  // namespace detail

// Lee un .network (version 2, o version 1 sin las matrices). Los problemas de
// filas concretas no son fatales: se guardan en net.load_issues.
inline bool readNetwork(const std::string& filename, Network& net, std::string& err) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    err = "could not open file: " + filename;
    return false;
  }
  net = Network();

  std::string line, section, crs_comment, meta_crs, meta_unit;
  bool header_pending = false, has_version = false;
  std::vector<std::string> cols;
  std::vector<detail::RawEdge> raw_edges;
  size_t lineno = 0;
  int n_idx = -1, nx_idx = -1, ny_idx = -1, nname_idx = -1, ndc_idx = -1, nbc_idx = -1, ncc_idx = -1;
  int e_id = -1, e_src = -1, e_tgt = -1, e_len = -1, e_time = -1, e_speed = -1, e_one = -1;

  auto addIssue = [&](const char* sev, const char* issue, const char* element, const std::string& id,
                      const std::string& detail) {
    net.load_issues.push_back({sev, issue, element, id, detail});
  };

  while (std::getline(file, line)) {
    ++lineno;
    if (lineno == 1 && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
        static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF)
      line.erase(0, 3);
    line = trim(line);
    if (line.empty())
      continue;
    if (line[0] == '#') {
      const size_t p = line.find("CRS:");
      if (p != std::string::npos)
        crs_comment = trim(line.substr(p + 4));
      continue;
    }
    if (line[0] == '@') {
      section = toUpper(trim(line.substr(1)));
      header_pending = true;
      cols.clear();
      continue;
    }
    if (section == "CRS") {  // version 1 antigua: @CRS + una linea
      crs_comment = line;
      continue;
    }
    if (section != "META" && section != "NODES" && section != "EDGES")
      continue;  // matrices de la version 1, secciones desconocidas

    auto parts = splitCSV(line);
    if (header_pending) {
      header_pending = false;
      cols.clear();
      for (const auto& c : parts)
        cols.push_back(toLower(c));
      if (section == "NODES") {
        n_idx = detail::colIndex(cols, {"node_id", "id"});
        nx_idx = detail::colIndex(cols, {"x", "longitude", "lon"});
        ny_idx = detail::colIndex(cols, {"y", "latitude", "lat"});
        nname_idx = detail::colIndex(cols, {"name"});
        ndc_idx = detail::colIndex(cols, {"degree_centrality"});
        nbc_idx = detail::colIndex(cols, {"betweenness"});
        ncc_idx = detail::colIndex(cols, {"closeness"});
      } else if (section == "EDGES") {
        e_id = detail::colIndex(cols, {"edge_id", "id"});
        e_src = detail::colIndex(cols, {"source", "from"});
        e_tgt = detail::colIndex(cols, {"target", "to"});
        e_len = detail::colIndex(cols, {"length", "distance"});
        e_time = detail::colIndex(cols, {"travel_time", "time"});
        e_speed = detail::colIndex(cols, {"speed"});
        e_one = detail::colIndex(cols, {"oneway"});
      }
      continue;
    }

    const std::string where = "line " + std::to_string(lineno);
    if (section == "META") {
      if (parts.size() >= 2) {
        const std::string k = toLower(parts[0]);
        if (k == "version")
          has_version = true;
        else if (k == "crs")
          meta_crs = parts[1];
        else if (k == "length_unit")
          meta_unit = parts[1];
      }
    } else if (section == "NODES") {
      if (n_idx < 0 || nx_idx < 0 || ny_idx < 0) {
        err = "@NODES section without node_id/x/y columns";
        return false;
      }
      const size_t need = static_cast<size_t>(std::max({n_idx, nx_idx, ny_idx})) + 1;
      Node nd;
      if (parts.size() < need || !parseInt(parts[n_idx], nd.id) ||
          !parseDouble(parts[nx_idx], nd.x) || !parseDouble(parts[ny_idx], nd.y)) {
        addIssue("error", "BAD_ROW", "node", where, "unreadable node row, skipped");
        continue;
      }
      if (nname_idx >= 0 && static_cast<size_t>(nname_idx) < parts.size())
        nd.name = parts[nname_idx];
      // centralidades: vacias = no calculadas (se quedan en NaN)
      auto optional = [&](int idx, double& out) {
        if (idx >= 0 && static_cast<size_t>(idx) < parts.size())
          parseDouble(parts[idx], out);
      };
      optional(nbc_idx, nd.bc);
      optional(ncc_idx, nd.cc);
      (void)ndc_idx;  // degree_centrality se recalcula con computeDegrees()
      if (net.id_to_index.count(nd.id)) {
        addIssue("error", "DUPLICATE_NODE_ID", "node", std::to_string(nd.id),
                 "node_id appears more than once (" + where + "), later row ignored");
        continue;
      }
      net.id_to_index[nd.id] = static_cast<int>(net.nodes.size());
      net.nodes.push_back(nd);
    } else {  // EDGES
      if (e_src < 0 || e_tgt < 0 || e_len < 0) {
        err = "@EDGES section without source/target/length columns";
        return false;
      }
      detail::RawEdge r{static_cast<int>(raw_edges.size()), 0, 0, static_cast<int>(lineno), 0, std::nan(""), 0, false};
      const size_t need = static_cast<size_t>(std::max({e_src, e_tgt, e_len})) + 1;
      if (parts.size() < need || !parseInt(parts[e_src], r.source_id) ||
          !parseInt(parts[e_tgt], r.target_id) || !parseDouble(parts[e_len], r.length)) {
        addIssue("error", "BAD_ROW", "edge", where, "unreadable edge row, skipped");
        continue;
      }
      if (e_id >= 0 && static_cast<size_t>(e_id) < parts.size())
        parseInt(parts[e_id], r.id);
      if (e_time >= 0 && static_cast<size_t>(e_time) < parts.size() &&
          !parseDouble(parts[e_time], r.travel_time))
        r.travel_time = std::nan("");
      if (e_time < 0)
        r.travel_time = std::nan("");
      if (e_speed >= 0 && static_cast<size_t>(e_speed) < parts.size())
        parseDouble(parts[e_speed], r.speed);
      if (e_one >= 0 && static_cast<size_t>(e_one) < parts.size())
        r.oneway = detail::truthy(parts[e_one]);
      raw_edges.push_back(r);
    }
  }

  if (net.nodes.empty()) {
    err = "no nodes found in network file";
    return false;
  }

  net.legacy = !has_version;
  net.crs = !meta_crs.empty() ? meta_crs : (!crs_comment.empty() ? crs_comment : "EPSG:4326");
  net.geographic = isGeographicCRS(net.crs);
  net.length_unit = net.legacy ? "crs_units" : (meta_unit.empty() ? "meters" : meta_unit);

  for (const auto& r : raw_edges) {
    auto s = net.id_to_index.find(r.source_id);
    auto t = net.id_to_index.find(r.target_id);
    if (s == net.id_to_index.end() || t == net.id_to_index.end()) {
      addIssue("error", "UNKNOWN_NODE", "edge", "line " + std::to_string(r.row),
               "edge " + std::to_string(r.source_id) + " -> " + std::to_string(r.target_id) +
                   " references a node that does not exist, skipped");
      continue;
    }
    Edge e;
    e.id = r.id;
    e.source = s->second;
    e.target = t->second;
    e.length = r.length;
    e.travel_time = r.travel_time;
    e.speed = r.speed;
    e.oneway = r.oneway;
    net.edges.push_back(e);
  }
  computeDegrees(net);
  return true;
}

// ------------------------------------------------------------------- grafo

struct Arc {
  int to;
  int edge;
  double w;
};

// Lista de adyacencia compacta (CSR). 'topology' ignora el costo (para
// componentes conexas); si no, se omiten los arcos con costo invalido.
struct Graph {
  int n = 0;
  std::vector<int> start;
  std::vector<Arc> arcs;
};

inline Graph buildGraph(const Network& net, CostKind kind, bool reversed = false,
                        bool topology = false) {
  Graph g;
  g.n = static_cast<int>(net.nodes.size());
  g.start.assign(g.n + 1, 0);
  auto usable = [&](const Edge& e) {
    if (e.source == e.target)
      return false;
    if (topology)
      return true;
    const double w = edgeCost(e, kind);
    return std::isfinite(w) && w >= 0;
  };
  for (const auto& e : net.edges) {
    if (!usable(e))
      continue;
    int a = e.source, b = e.target;
    if (reversed)
      std::swap(a, b);
    g.start[a + 1]++;
    if (!e.oneway)
      g.start[b + 1]++;
  }
  for (int i = 0; i < g.n; ++i)
    g.start[i + 1] += g.start[i];
  g.arcs.resize(g.start[g.n]);
  std::vector<int> pos(g.start.begin(), g.start.end() - 1);
  for (size_t i = 0; i < net.edges.size(); ++i) {
    const auto& e = net.edges[i];
    if (!usable(e))
      continue;
    int a = e.source, b = e.target;
    if (reversed)
      std::swap(a, b);
    const double w = topology ? 0.0 : edgeCost(e, kind);
    g.arcs[pos[a]++] = {b, static_cast<int>(i), w};
    if (!e.oneway)
      g.arcs[pos[b]++] = {a, static_cast<int>(i), w};
  }
  return g;
}

struct PathTree {
  std::vector<double> dist;
  std::vector<int> prev_node;  // predecesor en el camino minimo (-1 = ninguno)
  std::vector<int> prev_edge;  // arista usada para llegar al nodo
};

// Dijkstra desde 'source'. Con 'target' >= 0 se detiene al fijarlo; con
// 'max_cost' no explora mas alla de ese costo acumulado. Con 'stop_set'
// (marca por nodo) se detiene cuando se han fijado 'stop_count' nodos marcados
// (el origen cuenta si esta marcado); sirve para matrices de costos entre un
// subconjunto de nodos sin explorar toda la red.
inline void dijkstra(const Graph& g, int source, PathTree& t, int target = -1,
                     double max_cost = kInf, const std::vector<char>* stop_set = nullptr,
                     int stop_count = 0) {
  t.dist.assign(g.n, kInf);
  t.prev_node.assign(g.n, -1);
  t.prev_edge.assign(g.n, -1);
  using QE = std::pair<double, int>;
  std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
  t.dist[source] = 0;
  pq.push({0.0, source});
  while (!pq.empty()) {
    const QE top = pq.top();
    pq.pop();
    const double d = top.first;
    const int u = top.second;
    if (d > t.dist[u])
      continue;
    if (u == target)
      break;
    if (stop_set && (*stop_set)[u] && --stop_count <= 0)
      break;
    for (int i = g.start[u]; i < g.start[u + 1]; ++i) {
      const Arc& a = g.arcs[i];
      const double nd = d + a.w;
      if (nd > max_cost)
        continue;
      if (nd < t.dist[a.to]) {
        t.dist[a.to] = nd;
        t.prev_node[a.to] = u;
        t.prev_edge[a.to] = a.edge;
        pq.push({nd, a.to});
      }
    }
  }
}

struct Path {
  std::vector<int> nodes;  // origen ... destino
  std::vector<int> edges;  // edges[i] une nodes[i] con nodes[i+1]
};

inline bool buildPath(const PathTree& t, int source, int target, Path& path) {
  path.nodes.clear();
  path.edges.clear();
  if (!std::isfinite(t.dist[target]))
    return false;
  for (int v = target; v != -1; v = t.prev_node[v]) {
    path.nodes.push_back(v);
    if (v != source)
      path.edges.push_back(t.prev_edge[v]);
    if (v == source)
      break;
  }
  std::reverse(path.nodes.begin(), path.nodes.end());
  std::reverse(path.edges.begin(), path.edges.end());
  return !path.nodes.empty() && path.nodes.front() == source;
}

// ------------------------------------------------------------- componentes

struct Components {
  int count = 0;
  std::vector<int> label;       // componente de cada nodo (0 = la mas grande)
  std::vector<int> size;        // nodos por componente
  std::vector<int> edge_count;  // aristas por componente (solo conexas debiles)
};

namespace detail {
inline Components relabel(const std::vector<int>& raw, int raw_count) {
  Components c;
  std::vector<int> size(raw_count, 0), first(raw_count, -1);
  for (size_t i = 0; i < raw.size(); ++i) {
    size[raw[i]]++;
    if (first[raw[i]] < 0)
      first[raw[i]] = static_cast<int>(i);
  }
  std::vector<int> order(raw_count);
  for (int i = 0; i < raw_count; ++i)
    order[i] = i;
  std::sort(order.begin(), order.end(), [&](int a, int b) {
    if (size[a] != size[b])
      return size[a] > size[b];
    return first[a] < first[b];
  });
  std::vector<int> map(raw_count, 0);
  c.count = raw_count;
  c.size.resize(raw_count);
  for (int i = 0; i < raw_count; ++i) {
    map[order[i]] = i;
    c.size[i] = size[order[i]];
  }
  c.label.resize(raw.size());
  for (size_t i = 0; i < raw.size(); ++i)
    c.label[i] = map[raw[i]];
  return c;
}
}  // namespace detail

// Componentes conexas ignorando la direccion de las aristas.
inline Components weakComponents(const Network& net) {
  const int n = static_cast<int>(net.nodes.size());
  std::vector<int> parent(n);
  for (int i = 0; i < n; ++i)
    parent[i] = i;
  auto find = [&](int x) {
    while (parent[x] != x) {
      parent[x] = parent[parent[x]];
      x = parent[x];
    }
    return x;
  };
  for (const auto& e : net.edges) {
    const int a = find(e.source), b = find(e.target);
    if (a != b)
      parent[a] = b;
  }
  std::vector<int> raw(n), root_id(n, -1);
  int k = 0;
  for (int i = 0; i < n; ++i) {
    const int r = find(i);
    if (root_id[r] < 0)
      root_id[r] = k++;
    raw[i] = root_id[r];
  }
  Components c = detail::relabel(raw, k);
  c.edge_count.assign(c.count, 0);
  for (const auto& e : net.edges)
    c.edge_count[c.label[e.source]]++;
  return c;
}

// Componentes fuertemente conexas (respeta 'oneway'). Kosaraju iterativo.
inline Components strongComponents(const Network& net) {
  const int n = static_cast<int>(net.nodes.size());
  const Graph fwd = buildGraph(net, CostKind::Length, false, true);
  const Graph rev = buildGraph(net, CostKind::Length, true, true);
  std::vector<int> order;
  order.reserve(n);
  std::vector<char> seen(n, 0);
  std::vector<std::pair<int, int>> stack;
  for (int s = 0; s < n; ++s) {
    if (seen[s])
      continue;
    seen[s] = 1;
    stack.push_back({s, fwd.start[s]});
    while (!stack.empty()) {
      const int u = stack.back().first;
      int& i = stack.back().second;
      if (i < fwd.start[u + 1]) {
        const int v = fwd.arcs[i++].to;
        if (!seen[v]) {
          seen[v] = 1;
          stack.push_back({v, fwd.start[v]});
        }
      } else {
        order.push_back(u);
        stack.pop_back();
      }
    }
  }
  std::vector<int> raw(n, -1);
  int k = 0;
  std::vector<int> work;
  for (int idx = n - 1; idx >= 0; --idx) {
    const int s = order[idx];
    if (raw[s] >= 0)
      continue;
    raw[s] = k;
    work.push_back(s);
    while (!work.empty()) {
      const int u = work.back();
      work.pop_back();
      for (int i = rev.start[u]; i < rev.start[u + 1]; ++i) {
        const int v = rev.arcs[i].to;
        if (raw[v] < 0) {
          raw[v] = k;
          work.push_back(v);
        }
      }
    }
    ++k;
  }
  return detail::relabel(raw, k);
}

// ------------------------------------------------------------- centralidades

// Intermediacion (Brandes) y cercania (Wasserman-Faust) normalizadas; usa los
// costos 'kind'. O(n*m*log n): se pide de forma explicita (-centrality).
inline void computeCentralities(Network& net, CostKind kind,
                                const std::function<void(int, int)>& progress = nullptr) {
  computeDegrees(net);
  const Graph g = buildGraph(net, kind);
  const int n = g.n;
  std::vector<double> bc(n, 0.0), cc(n, 0.0), dist(n), sigma(n), delta(n);
  std::vector<std::vector<int>> pred(n);
  std::vector<int> order;
  std::vector<char> done(n);
  using QE = std::pair<double, int>;

  for (int s = 0; s < n; ++s) {
    std::fill(dist.begin(), dist.end(), kInf);
    std::fill(sigma.begin(), sigma.end(), 0.0);
    std::fill(delta.begin(), delta.end(), 0.0);
    std::fill(done.begin(), done.end(), 0);
    for (auto& p : pred)
      p.clear();
    order.clear();
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
    dist[s] = 0;
    sigma[s] = 1;
    pq.push({0.0, s});
    while (!pq.empty()) {
      const QE top = pq.top();
      pq.pop();
      const int v = top.second;
      if (done[v] || top.first > dist[v])
        continue;
      done[v] = 1;
      order.push_back(v);
      for (int i = g.start[v]; i < g.start[v + 1]; ++i) {
        const Arc& a = g.arcs[i];
        const double nd = dist[v] + a.w;
        const double eps = 1e-9 * std::max(1.0, nd);
        if (nd < dist[a.to] - eps) {
          dist[a.to] = nd;
          sigma[a.to] = sigma[v];
          pred[a.to].assign(1, v);
          pq.push({nd, a.to});
        } else if (!done[a.to] && std::fabs(nd - dist[a.to]) <= eps) {
          sigma[a.to] += sigma[v];
          pred[a.to].push_back(v);
        }
      }
    }
    double sum = 0;
    for (size_t k = order.size(); k-- > 1;) {
      const int w = order[k];
      sum += dist[w];
      for (int p : pred[w])
        delta[p] += sigma[p] / sigma[w] * (1.0 + delta[w]);
      bc[w] += delta[w];
    }
    const double reached = static_cast<double>(order.size()) - 1.0;
    if (reached > 0 && sum > 0 && n > 1)
      cc[s] = (reached / (n - 1.0)) * (reached / sum);
    if (progress && ((s + 1) % 100 == 0 || s + 1 == n))
      progress(s + 1, n);
  }
  const double norm = n > 2 ? 1.0 / ((n - 1.0) * (n - 2.0)) : 0.0;
  for (int i = 0; i < n; ++i) {
    net.nodes[i].bc = bc[i] * norm;
    net.nodes[i].cc = cc[i];
  }
}

// ------------------------------------------------------------- busqueda de nodos

// Resuelve un nodo por ID o por nombre (sin distinguir mayusculas): primero ID,
// luego nombre exacto, luego fragmento del nombre. Si hay varios, es ambiguo.
inline bool findNode(const Network& net, const std::string& query, int& index, std::string& err) {
  const std::string q = trim(query);
  if (q.empty()) {
    err = "empty node";
    return false;
  }
  if (isUnsignedInteger(q)) {
    int id = 0;
    if (parseInt(q, id)) {
      auto it = net.id_to_index.find(id);
      if (it != net.id_to_index.end()) {
        index = it->second;
        return true;
      }
    }
  }
  const std::string lq = toLower(q);
  std::vector<int> exact, partial;
  for (size_t i = 0; i < net.nodes.size(); ++i) {
    const std::string ln = toLower(net.nodes[i].name);
    if (ln.empty())
      continue;
    if (ln == lq)
      exact.push_back(static_cast<int>(i));
    else if (ln.find(lq) != std::string::npos)
      partial.push_back(static_cast<int>(i));
  }
  const std::vector<int>& hits = !exact.empty() ? exact : partial;
  if (hits.size() == 1) {
    index = hits[0];
    return true;
  }
  if (hits.empty()) {
    err = "node not found: " + q;
    if (isUnsignedInteger(q))
      err += " (node ids go from " + std::to_string(net.nodes.front().id) + " to " +
             std::to_string(net.nodes.back().id) + ")";
    return false;
  }
  err = "ambiguous node '" + q + "', it matches:";
  for (size_t i = 0; i < hits.size() && i < 5; ++i)
    err += " " + std::to_string(net.nodes[hits[i]].id) + " (" + net.nodes[hits[i]].name + ")";
  if (hits.size() > 5)
    err += " ...";
  return false;
}

// ------------------------------------------------------------- nodo mas cercano

// Cuadricula para buscar el nodo mas cercano a un punto.
class NodeGrid {
 public:
  explicit NodeGrid(const std::vector<Node>& nodes) : nodes_(nodes) {
    double minx = kInf, miny = kInf, maxx = -kInf, maxy = -kInf;
    for (const auto& n : nodes) {
      minx = std::min(minx, n.x);
      maxx = std::max(maxx, n.x);
      miny = std::min(miny, n.y);
      maxy = std::max(maxy, n.y);
    }
    x0_ = minx;
    y0_ = miny;
    const double w = maxx - minx, h = maxy - miny;
    const double dims = std::min(2048.0, std::max(1.0, std::ceil(std::sqrt(nodes.size() / 2.0))));
    cell_ = std::max(w, h) / dims;
    if (!(cell_ > 0))
      cell_ = 1.0;
    nx_ = static_cast<int>(w / cell_) + 1;
    ny_ = static_cast<int>(h / cell_) + 1;
    cells_.assign(static_cast<size_t>(nx_) * ny_, {});
    for (size_t i = 0; i < nodes.size(); ++i)
      cells_[index(cx(nodes[i].x), cy(nodes[i].y))].push_back(static_cast<int>(i));
  }

  int nearest(double px, double py, double& best_dist) const {
    const int c0 = cx(px), r0 = cy(py);
    int best = -1;
    best_dist = kInf;
    const int max_ring = std::max(nx_, ny_);
    for (int ring = 0; ring <= max_ring; ++ring) {
      for (int c = c0 - ring; c <= c0 + ring; ++c)
        for (int r = r0 - ring; r <= r0 + ring; ++r) {
          if (std::max(std::abs(c - c0), std::abs(r - r0)) != ring)
            continue;
          if (c < 0 || r < 0 || c >= nx_ || r >= ny_)
            continue;
          for (int idx : cells_[index(c, r)]) {
            const double d = std::hypot(nodes_[idx].x - px, nodes_[idx].y - py);
            if (d < best_dist) {
              best_dist = d;
              best = idx;
            }
          }
        }
      if (best >= 0 && best_dist <= ring * cell_)
        break;
    }
    return best;
  }

 private:
  int cx(double x) const { return std::min(nx_ - 1, std::max(0, static_cast<int>((x - x0_) / cell_))); }
  int cy(double y) const { return std::min(ny_ - 1, std::max(0, static_cast<int>((y - y0_) / cell_))); }
  size_t index(int c, int r) const { return static_cast<size_t>(r) * nx_ + c; }
  const std::vector<Node>& nodes_;
  double x0_ = 0, y0_ = 0, cell_ = 1;
  int nx_ = 1, ny_ = 1;
  std::vector<std::vector<int>> cells_;
};

}  // namespace SpatialNet
