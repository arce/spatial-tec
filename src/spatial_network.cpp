// spatial_network -- construye una red ruteable (nodos y aristas) desde lineas
// de calles. Formato de salida y logica comun: include/core/spatial_network.hpp.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "../include/core/spatial_csv.hpp"
#include "../include/core/spatial_network.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

using namespace SpatialNet;

namespace {

struct Pt {
  double x = 0, y = 0;
};

// Un tramo recto de una calle. dir: 0 doble sentido, 1 a->b, -1 b->a.
struct Segment {
  Pt a, b;
  double speed = 0;
  int dir = 0;
  int feature = 0;  // indice en dataset.features (para el reporte)
};

struct Options {
  double speed = 50.0;
  std::string crs = "EPSG:4326";
  bool crs_given = false;
  bool oneway = false;
  double snap = 0.0;
  bool split = false;
  std::string names_file;
  double names_tol = -1.0;
  std::string report_file;
  bool centrality = false;
  double unit_scale = 1.0;
  bool verbose = false;
};

void printUsage() {
  std::cerr << "spatial_network - Build a routable network from street lines\n\n";
  std::cerr << "Usage: spatial_network <streets.csv> <output.network> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -speed <value>       Default speed in km/h (default: 50)\n";
  std::cerr << "  -crs <value>         Coordinate system (default: the CRS of the input, else EPSG:4326)\n";
  std::cerr << "  -oneway              Streets are one-way (digitizing direction) unless a 'oneway' column says otherwise\n";
  std::cerr << "  -snap <dist>         Merge vertices closer than <dist> (CRS units; default: exact match)\n";
  std::cerr << "  -split               Add a node where two streets cross or touch without sharing a vertex\n";
  std::cerr << "  -report <file>       Save a CSV listing every crossing, T junction, overlap and snapped vertex found\n";
  std::cerr << "  -names <points.csv>  Name nodes with the nearest point of a CSV that has a 'name' column\n";
  std::cerr << "  -names_tol <dist>    Ignore named points farther than <dist> (CRS units) from every node\n";
  std::cerr << "  -unit_scale <value>  Metres per CRS unit when the CRS is projected (default: 1)\n";
  std::cerr << "  -centrality          Also compute betweenness and closeness (slow on large networks)\n";
  std::cerr << "  -verbose             Print more details\n";
  std::cerr << "  -help                Show this help\n\n";
  std::cerr << "Optional street columns: 'speed' (km/h) or 'maxspeed', and 'oneway'\n";
  std::cerr << "  (yes|true|1|forward|ft = forward, -1|reverse|backward|tf = reverse, no|false|0|both = two-way).\n\n";
  std::cerr << "Output:\n";
  std::cerr << "  <output.network>  Nodes and edges (lengths in metres, times in minutes)\n\n";
  std::cerr << "Example:\n";
  std::cerr << "  spatial_network streets.csv roads.network -speed 40 -split -names towns.csv\n";
}

std::string findColumn(const std::vector<std::string>& columns,
                       std::initializer_list<const char*> names) {
  for (const char* n : names)
    for (const auto& c : columns)
      if (toLower(trim(c)) == n)
        return c;
  return "";
}

// "50", "50 km/h", "30 mph" -> km/h
bool parseSpeed(const std::string& raw, double& kmh) {
  const std::string s = toLower(trim(raw));
  if (s.empty())
    return false;
  char* end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  if (end == s.c_str() || !std::isfinite(v) || v <= 0)
    return false;
  const std::string unit = trim(end);
  if (unit.empty() || unit == "km/h" || unit == "kmh" || unit == "kph") {
    kmh = v;
    return true;
  }
  if (unit == "mph") {
    kmh = v * 1.609344;
    return true;
  }
  return false;
}

// -2 = valor no reconocido; -1 inverso; 0 doble sentido; 1 directo
int parseOneway(const std::string& raw) {
  const std::string s = toLower(trim(raw));
  if (s == "yes" || s == "true" || s == "1" || s == "forward" || s == "ft")
    return 1;
  if (s == "-1" || s == "reverse" || s == "reversed" || s == "backward" || s == "tf")
    return -1;
  if (s == "no" || s == "false" || s == "0" || s == "both" || s == "bidirectional" || s == "b")
    return 0;
  return -2;
}

double hypotPt(Pt a, Pt b) {
  return std::hypot(a.x - b.x, a.y - b.y);
}

// ------------------------------------------------------------- nodos por snapping

struct CellKey {
  int64_t x, y;
  bool operator==(const CellKey& o) const { return x == o.x && y == o.y; }
};
struct CellKeyHash {
  size_t operator()(const CellKey& k) const {
    return std::hash<int64_t>()(k.x) * 1000003u ^ std::hash<int64_t>()(k.y);
  }
};

// Une vertices a menos de 'tol' (tamano de celda = tol, se miran las 8 vecinas).
class NodeIndex {
 public:
  explicit NodeIndex(double tol) : tol_(tol) {}
  // 'moved' (opcional) recibe la distancia a la que se unio el vertice a un nodo existente.
  int findOrAdd(Pt p, std::vector<Node>& nodes, double* moved = nullptr) {
    if (moved)
      *moved = 0;
    const int64_t cx = static_cast<int64_t>(std::floor(p.x / tol_));
    const int64_t cy = static_cast<int64_t>(std::floor(p.y / tol_));
    for (int64_t dx = -1; dx <= 1; ++dx)
      for (int64_t dy = -1; dy <= 1; ++dy) {
        auto it = cells_.find({cx + dx, cy + dy});
        if (it == cells_.end())
          continue;
        for (int idx : it->second) {
          const double d = std::hypot(nodes[idx].x - p.x, nodes[idx].y - p.y);
          if (d <= tol_) {
            if (moved)
              *moved = d;
            return idx;
          }
        }
      }
    Node n;
    n.id = static_cast<int>(nodes.size());
    n.x = p.x;
    n.y = p.y;
    nodes.push_back(n);
    cells_[{cx, cy}].push_back(n.id);
    return n.id;
  }

 private:
  double tol_;
  std::unordered_map<CellKey, std::vector<int>, CellKeyHash> cells_;
};

// ------------------------------------------- cruces entre calles (-split, -report)

struct Cut {
  double t;
  Pt p;
};

// Hallazgo para el reporte. issue: CROSSING | T_JUNCTION | OVERLAP | SNAPPED_VERTEX
struct Event {
  std::string issue;
  Pt at;
  int feature_a = -1, feature_b = -1;  // indices en dataset.features
  size_t count = 1;                    // OVERLAP: pares de tramos superpuestos
  double value = 0;                    // OVERLAP: largo (m); SNAPPED_VERTEX: distancia (CRS)
};

double cross2(Pt a, Pt b) {
  return a.x * b.y - a.y * b.x;
}

double distPointSegment(Pt p, Pt a, Pt b, double& t) {
  const double dx = b.x - a.x, dy = b.y - a.y;
  const double len2 = dx * dx + dy * dy;
  t = len2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.0;
  t = std::min(1.0, std::max(0.0, t));
  return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

// Busca donde un tramo cruza a otro (cruce propio), donde un extremo ajeno cae
// sobre su interior (T) y donde dos tramos van uno sobre otro (superposicion).
// No modifica nada: devuelve los cortes necesarios (por tramo) y los hallazgos.
// 'measure' da el largo de un trozo en metros. Los extremos compartidos no son
// hallazgos: ya los une el snapping.
void scanCrossings(const std::vector<Segment>& segs, double tol,
                   const std::function<double(Pt, Pt)>& measure,
                   std::vector<std::vector<Cut>>& cuts, std::vector<Event>& events) {
  const size_t n = segs.size();
  cuts.assign(n, {});
  if (n < 2)
    return;

  std::vector<double> minx(n), maxx(n), miny(n), maxy(n);
  double total = 0;
  for (size_t i = 0; i < n; ++i) {
    minx[i] = std::min(segs[i].a.x, segs[i].b.x) - tol;
    maxx[i] = std::max(segs[i].a.x, segs[i].b.x) + tol;
    miny[i] = std::min(segs[i].a.y, segs[i].b.y) - tol;
    maxy[i] = std::max(segs[i].a.y, segs[i].b.y) + tol;
    total += hypotPt(segs[i].a, segs[i].b);
  }
  const double cell = std::max({total / static_cast<double>(n), tol * 4.0, 1e-12});
  auto cellOf = [&](double v) { return static_cast<int64_t>(std::floor(v / cell)); };

  std::unordered_map<CellKey, std::vector<int>, CellKeyHash> grid;
  for (size_t i = 0; i < n; ++i)
    for (int64_t cx = cellOf(minx[i]); cx <= cellOf(maxx[i]); ++cx)
      for (int64_t cy = cellOf(miny[i]); cy <= cellOf(maxy[i]); ++cy)
        grid[{cx, cy}].push_back(static_cast<int>(i));

  auto interior = [&](double t, double len) { return t * len > tol && (1.0 - t) * len > tol; };

  auto processPair = [&](int i, int j) {
    const Segment &A = segs[i], &B = segs[j];
    const double la = hypotPt(A.a, A.b), lb = hypotPt(B.a, B.b);
    if (la <= 0 || lb <= 0)
      return;
    const Pt r{A.b.x - A.a.x, A.b.y - A.a.y}, s{B.b.x - B.a.x, B.b.y - B.a.y};
    auto lineDist = [&](Pt p) { return std::fabs(cross2(r, Pt{p.x - A.a.x, p.y - A.a.y})) / la; };
    const bool collinear = lineDist(B.a) <= tol && lineDist(B.b) <= tol;

    double t = 0;
    bool touched = false;
    for (Pt q : {B.a, B.b})
      if (distPointSegment(q, A.a, A.b, t) <= tol && interior(t, la)) {
        cuts[i].push_back(Cut{t, q});
        if (!collinear) {
          events.push_back(Event{"T_JUNCTION", q, B.feature, A.feature, 1, 0});
          touched = true;
        }
      }
    for (Pt p : {A.a, A.b})
      if (distPointSegment(p, B.a, B.b, t) <= tol && interior(t, lb)) {
        cuts[j].push_back(Cut{t, p});
        if (!collinear) {
          events.push_back(Event{"T_JUNCTION", p, A.feature, B.feature, 1, 0});
          touched = true;
        }
      }

    if (collinear) {  // mismo trazo: la parte comun es una superposicion
      const double u1 = ((B.a.x - A.a.x) * r.x + (B.a.y - A.a.y) * r.y) / la;
      const double u2 = ((B.b.x - A.a.x) * r.x + (B.b.y - A.a.y) * r.y) / la;
      const double lo = std::max(0.0, std::min(u1, u2)), hi = std::min(la, std::max(u1, u2));
      if (hi - lo > tol) {
        const Pt p1{A.a.x + r.x * lo / la, A.a.y + r.y * lo / la};
        const Pt p2{A.a.x + r.x * hi / la, A.a.y + r.y * hi / la};
        events.push_back(Event{"OVERLAP", Pt{(p1.x + p2.x) / 2, (p1.y + p2.y) / 2}, A.feature,
                               B.feature, 1, measure(p1, p2)});
      }
      return;
    }
    if (touched)
      return;

    const double rxs = cross2(r, s);
    if (std::fabs(rxs) > 1e-12 * la * lb) {
      const Pt qp{B.a.x - A.a.x, B.a.y - A.a.y};
      const double ta = cross2(qp, s) / rxs, tb = cross2(qp, r) / rxs;
      if (interior(ta, la) && interior(tb, lb)) {
        const Pt X{A.a.x + ta * r.x, A.a.y + ta * r.y};
        cuts[i].push_back(Cut{ta, X});
        cuts[j].push_back(Cut{tb, X});
        events.push_back(Event{"CROSSING", X, A.feature, B.feature, 1, 0});
      }
    }
  };

  for (const auto& kv : grid) {
    const auto& ids = kv.second;
    for (size_t p = 0; p < ids.size(); ++p)
      for (size_t q = p + 1; q < ids.size(); ++q) {
        const int i = ids[p], j = ids[q];
        if (minx[i] > maxx[j] || minx[j] > maxx[i] || miny[i] > maxy[j] || miny[j] > maxy[i])
          continue;
        // cada par se procesa una sola vez: en la primera celda que comparten
        if (cellOf(std::max(minx[i], minx[j])) != kv.first.x ||
            cellOf(std::max(miny[i], miny[j])) != kv.first.y)
          continue;
        processPair(i, j);
      }
  }
}

// Parte cada tramo en los puntos de corte.
std::vector<Segment> applyCuts(const std::vector<Segment>& segs,
                               std::vector<std::vector<Cut>>& cuts, size_t& n_cuts) {
  std::vector<Segment> out;
  out.reserve(segs.size());
  n_cuts = 0;
  for (size_t k = 0; k < segs.size(); ++k) {
    if (cuts[k].empty()) {
      out.push_back(segs[k]);
      continue;
    }
    n_cuts += cuts[k].size();
    std::sort(cuts[k].begin(), cuts[k].end(),
              [](const Cut& l, const Cut& r) { return l.t < r.t; });
    Pt prev = segs[k].a;
    for (const auto& c : cuts[k]) {
      Segment s = segs[k];
      s.a = prev;
      s.b = c.p;
      out.push_back(s);
      prev = c.p;
    }
    Segment last = segs[k];
    last.a = prev;
    out.push_back(last);
  }
  return out;
}

// Quita repetidos (un vertice que toca a otra calle genera un hallazgo por cada
// tramo que llega a el) y junta las superposiciones: una fila por par de calles.
std::vector<Event> consolidateEvents(const std::vector<Event>& in) {
  std::vector<Event> out;
  std::map<std::tuple<std::string, long long, long long, int, int>, bool> seen;
  std::map<std::pair<int, int>, size_t> overlap_of;
  for (Event e : in) {
    if (e.issue == "OVERLAP") {
      const std::pair<int, int> key{std::min(e.feature_a, e.feature_b), std::max(e.feature_a, e.feature_b)};
      auto it = overlap_of.find(key);
      if (it != overlap_of.end()) {
        out[it->second].count += 1;
        out[it->second].value += e.value;
      } else {
        e.feature_a = key.first;
        e.feature_b = key.second;
        overlap_of[key] = out.size();
        out.push_back(e);
      }
      continue;
    }
    if (e.issue == "CROSSING" && e.feature_a > e.feature_b)
      std::swap(e.feature_a, e.feature_b);
    const auto key = std::make_tuple(e.issue, std::llround(e.at.x * 1e6), std::llround(e.at.y * 1e6),
                                     e.feature_a, e.feature_b);
    if (seen.emplace(key, true).second)
      out.push_back(e);
  }
  std::sort(out.begin(), out.end(), [](const Event& l, const Event& r) {
    return std::tie(l.issue, l.feature_a, l.feature_b, l.at.x, l.at.y) <
           std::tie(r.issue, r.feature_a, r.feature_b, r.at.x, r.at.y);
  });
  return out;
}

// --------------------------------------------------------------- nombres de nodo

bool assignNames(Network& net, const Options& opt, int& assigned, int& skipped, std::string& err) {
  Spatial::SpatialCSVReader reader;
  Spatial::VectorDataset ds;
  if (!reader.read(opt.names_file, ds)) {
    err = "could not read names file: " + opt.names_file;
    return false;
  }
  const std::string name_col = findColumn(ds.columns, {"name", "nombre"});
  if (name_col.empty()) {
    err = "names file has no 'name' column: " + opt.names_file;
    return false;
  }
  NodeGrid grid(net.nodes);
  assigned = skipped = 0;
  for (const auto& f : ds.features) {
    auto it = f.attributes.find(name_col);
    if (it == f.attributes.end() || trim(it->second).empty() ||
        f.type != Spatial::VectorFeature::GeometryType::POINT || f.coordinates.size() < 2) {
      skipped++;
      continue;
    }
    double d = 0;
    const int idx = grid.nearest(f.coordinates[0], f.coordinates[1], d);
    if (idx < 0 || (opt.names_tol >= 0 && d > opt.names_tol)) {
      skipped++;
      std::cerr << "Warning: '" << it->second << "' is " << num(d, 6)
                << " from the nearest node (> -names_tol), ignored\n";
      continue;
    }
    if (!net.nodes[idx].name.empty()) {
      skipped++;
      std::cerr << "Warning: node " << idx << " is already named '" << net.nodes[idx].name
                << "'; '" << it->second << "' ignored\n";
      continue;
    }
    net.nodes[idx].name = trim(it->second);
    assigned++;
  }
  return true;
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc >= 2 && (std::string(argv[1]) == "-help" || std::string(argv[1]) == "-h")) {
    printUsage();
    return 0;
  }
  if (argc < 3) {
    printUsage();
    return 1;
  }

  const std::string streets_file = argv[1];
  const std::string output_file = argv[2];
  Options opt;

  for (int i = 3; i < argc; ++i) {
    const std::string arg = argv[i];
    auto needValue = [&](const char* name) -> const char* {
      if (i + 1 >= argc) {
        std::cerr << "Error: " << name << " needs a value\n";
        return nullptr;
      }
      return argv[++i];
    };
    auto numberArg = [&](const char* name, double& out, bool allow_zero) {
      const char* v = needValue(name);
      if (!v)
        return false;
      if (!parseDouble(v, out) || out < 0 || (!allow_zero && out == 0)) {
        std::cerr << "Error: invalid value for " << name << ": " << v << "\n";
        return false;
      }
      return true;
    };
    if (arg == "-speed") {
      if (!numberArg("-speed", opt.speed, false))
        return 1;
    } else if (arg == "-crs") {
      const char* v = needValue("-crs");
      if (!v)
        return 1;
      opt.crs = v;
      opt.crs_given = true;
    } else if (arg == "-oneway") {
      opt.oneway = true;
    } else if (arg == "-snap") {
      if (!numberArg("-snap", opt.snap, true))
        return 1;
    } else if (arg == "-split") {
      opt.split = true;
    } else if (arg == "-report") {
      const char* v = needValue("-report");
      if (!v)
        return 1;
      opt.report_file = v;
    } else if (arg == "-names") {
      const char* v = needValue("-names");
      if (!v)
        return 1;
      opt.names_file = v;
    } else if (arg == "-names_tol") {
      if (!numberArg("-names_tol", opt.names_tol, true))
        return 1;
    } else if (arg == "-unit_scale") {
      if (!numberArg("-unit_scale", opt.unit_scale, false))
        return 1;
    } else if (arg == "-centrality") {
      opt.centrality = true;
    } else if (arg == "-verbose") {
      opt.verbose = true;
    } else if (arg == "-help" || arg == "-h") {
      printUsage();
      return 0;
    } else {
      std::cerr << "Error: Unknown option: " << arg << "\n";
      printUsage();
      return 1;
    }
  }

  if (!opt.report_file.empty()) {  // falla pronto si el reporte no se podra escribir
    std::ofstream probe(opt.report_file);
    if (!probe.is_open()) {
      std::cerr << "Error: could not create report file: " << opt.report_file << "\n";
      return 1;
    }
  }

  std::cout << "========================================\n";
  std::cout << "  NETWORK CONSTRUCTION\n";
  std::cout << "========================================\n\n";

  Spatial::SpatialCSVReader reader;
  Spatial::VectorDataset dataset;
  if (!reader.read(streets_file, dataset)) {
    std::cerr << "Error: Could not read streets file: " << streets_file << "\n";
    return 1;
  }

  Network net;
  net.crs = opt.crs_given ? opt.crs : (!dataset.crs.empty() ? dataset.crs : opt.crs);
  net.geographic = isGeographicCRS(net.crs);
  net.length_unit = "meters";

  std::cout << "Input: " << streets_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "CRS: " << net.crs << (net.geographic ? " (geographic: lengths by haversine)" : "")
            << "\n";
  std::cout << "Default speed: " << opt.speed << " km/h\n\n";

  // ---- tramos rectos a partir de las geometrias lineales
  const std::string speed_col = findColumn(dataset.columns, {"speed", "maxspeed", "max_speed", "speed_kmh"});
  const std::string oneway_col = findColumn(dataset.columns, {"oneway", "one_way"});
  std::vector<Segment> segs;
  size_t skipped_geom = 0, bad_speed = 0, bad_oneway = 0, lines_used = 0;
  bool out_of_range = false;

  for (size_t fi = 0; fi < dataset.features.size(); ++fi) {
    const auto& f = dataset.features[fi];
    const bool is_line = f.type == Spatial::VectorFeature::GeometryType::LINESTRING;
    const bool is_multi = f.type == Spatial::VectorFeature::GeometryType::MULTILINESTRING;
    if (!is_line && !is_multi) {
      skipped_geom++;
      continue;
    }
    double speed = opt.speed;
    if (!speed_col.empty()) {
      auto it = f.attributes.find(speed_col);
      if (it != f.attributes.end() && !trim(it->second).empty()) {
        double v = 0;
        if (parseSpeed(it->second, v))
          speed = v;
        else
          bad_speed++;
      }
    }
    int dir = opt.oneway ? 1 : 0;
    if (!oneway_col.empty()) {
      auto it = f.attributes.find(oneway_col);
      if (it != f.attributes.end() && !trim(it->second).empty()) {
        const int d = parseOneway(it->second);
        if (d == -2)
          bad_oneway++;
        else
          dir = d;
      }
    }

    const size_t npts = f.coordinates.size() / 2;
    std::vector<std::pair<size_t, size_t>> parts;
    if (is_multi && f.part_starts.size() > 1) {
      for (size_t k = 0; k < f.part_starts.size(); ++k)
        parts.push_back({f.part_starts[k], k + 1 < f.part_starts.size() ? f.part_starts[k + 1] : npts});
    } else {
      parts.push_back({0, npts});
    }
    bool used = false;
    for (const auto& pr : parts) {
      for (size_t k = pr.first; k + 1 < pr.second; ++k) {
        Segment s;
        s.a = {f.coordinates[2 * k], f.coordinates[2 * k + 1]};
        s.b = {f.coordinates[2 * k + 2], f.coordinates[2 * k + 3]};
        if (!std::isfinite(s.a.x) || !std::isfinite(s.a.y) || !std::isfinite(s.b.x) ||
            !std::isfinite(s.b.y))
          continue;
        if (net.geographic && (std::fabs(s.a.x) > 180 || std::fabs(s.b.x) > 180 ||
                               std::fabs(s.a.y) > 90 || std::fabs(s.b.y) > 90))
          out_of_range = true;
        s.speed = speed;
        s.dir = dir;
        s.feature = static_cast<int>(fi);
        segs.push_back(s);
        used = true;
      }
    }
    if (used)
      lines_used++;
  }

  if (opt.verbose) {
    std::cout << "Speed column: " << (speed_col.empty() ? "(none)" : speed_col) << "\n";
    std::cout << "Oneway column: " << (oneway_col.empty() ? "(none)" : oneway_col)
              << (opt.oneway ? " (default direction: one-way)" : " (default direction: two-way)") << "\n";
    std::cout << "Vertex snapping tolerance: " << num(std::max(opt.snap, 1e-9), 6) << " CRS units\n";
  }
  std::cout << "Street features: " << dataset.features.size() << "\n";
  std::cout << "Line features used: " << lines_used << "\n";
  if (skipped_geom)
    std::cerr << "Warning: " << skipped_geom << " feature(s) are not LINESTRING/MULTILINESTRING, ignored\n";
  if (bad_speed)
    std::cerr << "Warning: " << bad_speed << " feature(s) with an unreadable '" << speed_col
              << "' value use the default speed\n";
  if (bad_oneway)
    std::cerr << "Warning: " << bad_oneway << " feature(s) with an unrecognized '" << oneway_col
              << "' value use the default direction\n";
  if (segs.empty()) {
    std::cerr << "Error: No LINESTRING geometries found\n";
    return 1;
  }
  if (out_of_range) {
    std::cerr << "Error: coordinates are outside [-180,180]x[-90,90] but the CRS (" << net.crs
              << ") is geographic; pass the real CRS with -crs (e.g. -crs EPSG:5367)\n";
    return 1;
  }

  const double tol = std::max(opt.snap, 1e-9);

  // ---- cruces sin vertice comun: se buscan con -split o -report; solo -split corta
  const std::string id_col = findColumn(dataset.columns, {"id"});
  std::vector<Event> events;
  auto measure = [&](Pt p, Pt q) {
    return net.geographic ? haversineMeters(p.x, p.y, q.x, q.y) : hypotPt(p, q) * opt.unit_scale;
  };
  if (opt.split || !opt.report_file.empty()) {
    std::vector<std::vector<Cut>> cuts;
    scanCrossings(segs, tol, measure, cuts, events);
    events = consolidateEvents(events);
    size_t n_cross = 0, n_t = 0, n_over = 0;
    for (const auto& e : events) {
      n_cross += e.issue == "CROSSING";
      n_t += e.issue == "T_JUNCTION";
      n_over += e.issue == "OVERLAP";
    }
    std::cout << "Crossings: " << n_cross << ", T junctions: " << n_t << ", overlapping street pairs: " << n_over << "\n";
    if (opt.split) {
      size_t n_cuts = 0;
      segs = applyCuts(segs, cuts, n_cuts);
      std::cout << "Cuts made (-split): " << n_cuts << "\n";
    }
  }

  // ---- nodos y aristas
  NodeIndex index(tol);
  struct Key {
    int a, b, oneway;
    bool operator<(const Key& o) const { return std::tie(a, b, oneway) < std::tie(o.a, o.b, o.oneway); }
  };
  std::map<Key, size_t> edge_of;
  size_t zero_len = 0, duplicates = 0;

  std::vector<int> node_feature;  // calle que creo cada nodo (para el reporte)
  size_t snapped = 0;
  auto addNode = [&](Pt p, int feature) {
    const size_t before = net.nodes.size();
    double moved = 0;
    const int idx = index.findOrAdd(p, net.nodes, &moved);
    if (net.nodes.size() > before) {
      node_feature.push_back(feature);
    } else if (moved > 1e-9) {
      snapped++;
      if (!opt.report_file.empty())
        events.push_back(Event{"SNAPPED_VERTEX", p, feature, node_feature[idx], 1, moved});
    }
    return idx;
  };

  for (const auto& s : segs) {
    int u = addNode(s.a, s.feature);
    int v = addNode(s.b, s.feature);
    if (u == v) {
      zero_len++;
      continue;
    }
    int dir = s.dir;
    if (dir == -1) {
      std::swap(u, v);
      dir = 1;
    }
    Edge e;
    e.source = u;
    e.target = v;
    e.oneway = dir == 1;
    e.speed = s.speed;
    const Node &nu = net.nodes[u], &nv = net.nodes[v];
    e.length = net.geographic ? haversineMeters(nu.x, nu.y, nv.x, nv.y)
                              : std::hypot(nu.x - nv.x, nu.y - nv.y) * opt.unit_scale;
    e.travel_time = e.length / 1000.0 / s.speed * 60.0;

    const Key key = e.oneway ? Key{u, v, 1} : Key{std::min(u, v), std::max(u, v), 0};
    auto it = edge_of.find(key);
    if (it != edge_of.end()) {
      duplicates++;
      if (e.travel_time < net.edges[it->second].travel_time)
        net.edges[it->second] = e;
      continue;
    }
    edge_of[key] = net.edges.size();
    net.edges.push_back(e);
  }

  // nodos que se quedaron sin aristas (tramos colapsados por el snapping)
  {
    std::vector<int> remap(net.nodes.size(), -1);
    for (const auto& e : net.edges)
      remap[e.source] = remap[e.target] = 0;
    std::vector<Node> kept;
    for (size_t i = 0; i < net.nodes.size(); ++i)
      if (remap[i] == 0) {
        remap[i] = static_cast<int>(kept.size());
        Node n = net.nodes[i];
        n.id = remap[i];
        kept.push_back(n);
      }
    for (auto& e : net.edges) {
      e.source = remap[e.source];
      e.target = remap[e.target];
    }
    net.nodes.swap(kept);
  }
  for (size_t i = 0; i < net.nodes.size(); ++i)
    net.id_to_index[net.nodes[i].id] = static_cast<int>(i);

  if (net.edges.empty()) {
    std::cerr << "Error: no usable edges (all segments have zero length)\n";
    return 1;
  }
  for (size_t i = 0; i < net.edges.size(); ++i)
    net.edges[i].id = static_cast<int>(i);
  computeDegrees(net);

  std::cout << "Nodes: " << net.nodes.size() << "\n";
  std::cout << "Edges: " << net.edges.size() << "\n";
  if (snapped)
    std::cout << "Vertices joined by -snap: " << snapped << "\n";
  if (zero_len)
    std::cout << "Zero-length segments dropped: " << zero_len << "\n";
  if (duplicates)
    std::cout << "Duplicate edges merged: " << duplicates << "\n";

  // ---- nombres
  if (!opt.names_file.empty()) {
    int assigned = 0, skipped = 0;
    std::string err;
    if (!assignNames(net, opt, assigned, skipped, err)) {
      std::cerr << "Error: " << err << "\n";
      return 1;
    }
    std::cout << "Named nodes: " << assigned << (skipped ? " (" + std::to_string(skipped) + " point(s) ignored)" : "") << "\n";
  }

  // ---- centralidades (opcionales)
  if (opt.centrality) {
    std::cout << "Computing centralities (betweenness, closeness)...\n";
    computeCentralities(net, CostKind::Length, [](int done, int total) {
      std::cerr << "  Progress: " << done << "/" << total << "\r";
      if (done == total)
        std::cerr << "\n";
    });
  }

  std::string err;
  if (!writeNetwork(net, output_file, err)) {
    std::cerr << "Error: " << err << "\n";
    return 1;
  }

  if (!opt.report_file.empty()) {
    std::ofstream rep(opt.report_file);
    if (!rep.is_open()) {
      std::cerr << "Error: could not create report file: " << opt.report_file << "\n";
      return 1;
    }
    auto idOf = [&](int f) -> std::string {
      if (f < 0 || id_col.empty())
        return "";
      auto it = dataset.features[f].attributes.find(id_col);
      return it == dataset.features[f].attributes.end() ? "" : csvField(it->second);
    };
    auto rowOf = [&](int f) { return f < 0 ? std::string() : std::to_string(f + 1); };
    std::vector<Event> all = events;
    std::stable_sort(all.begin(), all.end(), [](const Event& l, const Event& r) { return l.issue < r.issue; });
    rep << "issue,x,y,row_a,row_b,id_a,id_b,action,detail\n";
    std::map<std::string, size_t> counts;
    for (const auto& e : all) {
      counts[e.issue]++;
      std::string action, detail;
      if (e.issue == "CROSSING") {
        action = opt.split ? "split" : "none";
        detail = "streets cross without a shared vertex";
      } else if (e.issue == "T_JUNCTION") {
        action = opt.split ? "split" : "none";
        detail = "the end of street A lies on the interior of street B";
      } else if (e.issue == "OVERLAP") {
        action = opt.split ? "split" : "none";
        detail = std::to_string(e.count) + " piece(s) run over each other for " + num(e.value, 8) + " m";
      } else {
        action = "merged";
        detail = "vertex moved " + num(e.value, 6) + " (CRS units) to join an existing node";
      }
      rep << e.issue << "," << num(e.at.x, 12) << "," << num(e.at.y, 12) << "," << rowOf(e.feature_a) << ","
          << rowOf(e.feature_b) << "," << idOf(e.feature_a) << "," << idOf(e.feature_b) << "," << action
          << "," << csvField(detail) << "\n";
    }
    rep.flush();
    if (!rep.good()) {
      std::cerr << "Error: error while writing report file: " << opt.report_file << "\n";
      return 1;
    }
    std::cout << "Report: " << all.size() << " finding(s)";
    for (const auto& kv : counts)
      std::cout << ", " << kv.first << " " << kv.second;
    std::cout << " -> " << opt.report_file << "\n";
  }

  double total_len = 0;
  size_t oneway_edges = 0;
  for (const auto& e : net.edges) {
    total_len += e.length;
    oneway_edges += e.oneway ? 1 : 0;
  }
  std::cout << "\n========================================\n";
  std::cout << "  NETWORK COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Output file: " << output_file << "\n";
  std::cout << "Nodes: " << net.nodes.size() << "\n";
  std::cout << "Edges: " << net.edges.size() << " (" << oneway_edges << " one-way)\n";
  std::cout << "Total length: " << num(total_len, 10) << " m\n";
  return 0;
}
