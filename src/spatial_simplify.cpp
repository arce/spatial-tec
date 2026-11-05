// spatial_simplify -- reduce el numero de vertices de lineas y poligonos.
//
// Metodos (-method):
//   douglas      Douglas-Peucker: conserva los vertices que se alejan mas de
//                -tolerance (unidades del CRS) del segmento que une los vertices
//                vecinos ya conservados. Se resuelve con una cola de prioridad
//                (sin recursion), de modo que tambien permite fijar el numero de
//                vertices que se conservan (-factor).
//   visvalingam  Visvalingam-Whyatt: elimina primero el vertice cuyo triangulo con
//                sus vecinos tiene menor area. -tolerance es un area minima
//                (unidades del CRS al cuadrado); -factor fija el numero de vertices.
//   radial       Distancia radial: elimina los vertices a menos de -tolerance del
//                ultimo vertice conservado.
//   nth          Conserva uno de cada -n vertices.
//
// Cada linea o anillo conserva siempre su primer y ultimo vertice (en un anillo
// son el mismo punto) y un minimo de vertices: 2 en una linea y 3 distintos en
// un anillo cerrado, para que un poligono pequeno no degenere.
//
// Con -topology los vertices compartidos por varias lineas o anillos (cruces,
// bordes comunes entre poligonos vecinos) se tratan como nodos: cada tramo entre
// dos nodos se simplifica una sola vez en una orientacion canonica, de modo que
// los poligonos vecinos siguen compartiendo exactamente el mismo borde.
//
// Nota: el lector de CSV del proyecto une los anillos de un POLYGON (exterior y
// huecos) en una sola lista de coordenadas; aqui se separan por cierre (el anillo
// termina en la primera repeticion de su vertice inicial).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../include/core/spatial_csv.hpp"
#include "../include/core/spatial_geom.hpp"
#include "../include/core/spatial_io.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

namespace {

using Feature = Spatial::VectorFeature;
using GType = Spatial::VectorFeature::GeometryType;

constexpr double kInf = std::numeric_limits<double>::infinity();

struct Pt {
  double x = 0, y = 0;
};

inline bool samePt(const Pt& a, const Pt& b) {
  return a.x == b.x && a.y == b.y;
}

enum class Method { Douglas, Visvalingam, Radial, Nth };

struct Params {
  Method method = Method::Douglas;
  bool has_tolerance = false;
  double tolerance = 0;
  bool has_factor = false;
  double factor = 0;
  int n = 0;
  bool topology = false;
};

const char* methodName(Method m) {
  switch (m) {
    case Method::Douglas:
      return "douglas";
    case Method::Visvalingam:
      return "visvalingam";
    case Method::Radial:
      return "radial";
    case Method::Nth:
      return "nth";
  }
  return "";
}

// ------------------------------------------------------------------ geometria

double pointDistance(const Pt& a, const Pt& b) {
  return std::hypot(a.x - b.x, a.y - b.y);
}

// Distancia de p al segmento a-b (a la punta mas cercana si cae fuera).
double segmentDistance(const Pt& p, const Pt& a, const Pt& b) {
  const double dx = b.x - a.x, dy = b.y - a.y;
  const double len2 = dx * dx + dy * dy;
  if (len2 < 1e-24)
    return pointDistance(p, a);
  double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
  t = std::max(0.0, std::min(1.0, t));
  return pointDistance(p, {a.x + t * dx, a.y + t * dy});
}

double triangleArea(const Pt& a, const Pt& b, const Pt& c) {
  return std::fabs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) / 2.0;
}

// ------------------------------------------------------------ algoritmos de linea

// Orden de seleccion de Douglas-Peucker: primero y ultimo vertice, y luego los
// demas de mayor a menor distancia al segmento de su tramo. 'stop(count, d)'
// decide cuando parar, con 'count' vertices ya elegidos y 'd' la distancia del
// siguiente candidato. Con una tolerancia fija da el mismo resultado que la
// version recursiva clasica.
template <class Stop>
std::vector<int> douglasOrder(const std::vector<Pt>& P, Stop stop) {
  const int n = static_cast<int>(P.size());
  std::vector<int> order;
  if (n == 0)
    return order;
  order.push_back(0);
  if (n == 1)
    return order;
  order.push_back(n - 1);

  struct Seg {
    double d;
    int a, b, f;
  };
  auto cmp = [](const Seg& l, const Seg& r) {
    if (l.d != r.d)
      return l.d < r.d;
    if (l.a != r.a)
      return l.a > r.a;
    return l.b > r.b;
  };
  std::priority_queue<Seg, std::vector<Seg>, decltype(cmp)> pq(cmp);
  auto push = [&](int a, int b) {
    if (b <= a + 1)
      return;
    double best = -1;
    int far = -1;
    for (int i = a + 1; i < b; ++i) {
      const double d = segmentDistance(P[i], P[a], P[b]);
      if (d > best) {
        best = d;
        far = i;
      }
    }
    if (far >= 0)
      pq.push({best, a, b, far});
  };
  push(0, n - 1);
  while (!pq.empty()) {
    const Seg s = pq.top();
    if (stop(static_cast<int>(order.size()), s.d))
      break;
    pq.pop();
    order.push_back(s.f);
    push(s.a, s.f);
    push(s.f, s.b);
  }
  return order;
}

std::vector<char> douglasKeep(const std::vector<Pt>& P, bool by_target, double tol, size_t target,
                              size_t min_keep) {
  const int mk = static_cast<int>(min_keep);
  const int tg = static_cast<int>(target);
  const std::vector<int> order = douglasOrder(P, [&](int count, double d) {
    if (by_target)
      return count >= tg || (count >= mk && d <= 0);
    return count >= mk && d <= tol;
  });
  std::vector<char> keep(P.size(), 0);
  for (int i : order)
    keep[i] = 1;
  return keep;
}

std::vector<char> visvalingamKeep(const std::vector<Pt>& P, bool by_target, double tol, size_t target,
                                  size_t min_keep) {
  const int n = static_cast<int>(P.size());
  std::vector<char> keep(n, 1);
  if (n <= 2 || static_cast<size_t>(n) <= min_keep)
    return keep;
  std::vector<int> prev(n), next(n);
  std::vector<unsigned> version(n, 0);
  std::vector<char> removed(n, 0);
  for (int i = 0; i < n; ++i) {
    prev[i] = i - 1;
    next[i] = i + 1;
  }
  struct E {
    double area;
    int i;
    unsigned v;
  };
  auto cmp = [](const E& l, const E& r) {
    if (l.area != r.area)
      return l.area > r.area;
    return l.i > r.i;
  };
  std::priority_queue<E, std::vector<E>, decltype(cmp)> pq(cmp);
  for (int i = 1; i < n - 1; ++i)
    pq.push({triangleArea(P[i - 1], P[i], P[i + 1]), i, 0});

  size_t alive = n;
  double last = 0;  // area efectiva: nunca decrece, como en el algoritmo original
  while (!pq.empty() && alive > min_keep) {
    const E e = pq.top();
    if (removed[e.i] || e.v != version[e.i]) {
      pq.pop();
      continue;
    }
    if (by_target) {
      if (alive <= target)
        break;
    } else if (e.area >= tol) {
      break;
    }
    pq.pop();
    removed[e.i] = 1;
    keep[e.i] = 0;
    alive--;
    last = std::max(last, e.area);
    const int p = prev[e.i], q = next[e.i];
    next[p] = q;
    prev[q] = p;
    for (int v : {p, q}) {
      if (v <= 0 || v >= n - 1)
        continue;
      const double a = std::max(triangleArea(P[prev[v]], P[v], P[next[v]]), last);
      version[v]++;
      pq.push({a, v, version[v]});
    }
  }
  return keep;
}

// Si quedaron menos de 'min_keep' vertices, agrega los mas importantes segun
// Douglas-Peucker.
void ensureMinimum(const std::vector<Pt>& P, std::vector<char>& keep, size_t min_keep) {
  size_t count = std::count(keep.begin(), keep.end(), 1);
  if (count >= min_keep)
    return;
  const int mk = static_cast<int>(std::min(min_keep, P.size()));
  const std::vector<int> order = douglasOrder(P, [&](int c, double) { return c >= mk; });
  for (int i : order) {
    if (count >= min_keep)
      break;
    if (!keep[i]) {
      keep[i] = 1;
      count++;
    }
  }
}

std::vector<char> radialKeep(const std::vector<Pt>& P, double tol, size_t min_keep) {
  const int n = static_cast<int>(P.size());
  std::vector<char> keep(n, 0);
  keep[0] = keep[n - 1] = 1;
  int last_kept = 0;
  for (int i = 1; i < n - 1; ++i) {
    if (pointDistance(P[i], P[last_kept]) >= tol) {
      keep[i] = 1;
      last_kept = i;
    }
  }
  // el ultimo vertice siempre se conserva: si el anterior conservado queda muy cerca, se quita
  if (last_kept != 0 && pointDistance(P[n - 1], P[last_kept]) < tol)
    keep[last_kept] = 0;
  ensureMinimum(P, keep, min_keep);
  return keep;
}

std::vector<char> nthKeep(const std::vector<Pt>& P, int step, size_t min_keep) {
  const int n = static_cast<int>(P.size());
  std::vector<char> keep(n, 0);
  for (int i = 0; i < n; i += step)
    keep[i] = 1;
  keep[n - 1] = 1;
  ensureMinimum(P, keep, min_keep);
  return keep;
}

// Marca los vertices que se conservan. min_keep: minimo de vertices (contando
// el cierre de un anillo).
std::vector<char> simplifyPath(const std::vector<Pt>& P, const Params& prm, size_t min_keep) {
  const size_t n = P.size();
  if (n <= 2 || n <= min_keep)
    return std::vector<char>(n, 1);
  size_t target = 0;
  if (prm.has_factor) {
    target = static_cast<size_t>(std::llround(static_cast<double>(n) * (1.0 - prm.factor)));
    target = std::min(n, std::max(min_keep, target));
  }
  switch (prm.method) {
    case Method::Douglas:
      return douglasKeep(P, prm.has_factor, prm.tolerance, target, min_keep);
    case Method::Visvalingam:
      return visvalingamKeep(P, prm.has_factor, prm.tolerance, target, min_keep);
    case Method::Radial:
      return radialKeep(P, prm.tolerance, min_keep);
    case Method::Nth:
      return nthKeep(P, prm.n, min_keep);
  }
  return std::vector<char>(n, 1);
}

// ------------------------------------------------------------------ rutas

// Una linea, una parte de linea o un anillo de poligono.
struct Path {
  std::vector<Pt> pts;
  bool closed = false;     // primer vertice == ultimo
  size_t min_keep = 2;
  std::vector<char> keep;  // vertices que se conservan
  size_t feature = 0;
  size_t part = 0;
  int type = 0;  // 1 = linea, 2 = poligono
};

std::string typeName(const Feature& f) {
  switch (f.type) {
    case GType::LINESTRING:
    case GType::MULTILINESTRING:
      return "line";
    case GType::POLYGON:
    case GType::MULTIPOLYGON:
      return "polygon";
    default:
      return "point";
  }
}

// Separa en anillos las coordenadas de una parte de poligono [begin, end).
void splitRings(const Feature& f, size_t begin, size_t end, size_t feature_idx, size_t part_idx,
                std::vector<Path>& out) {
  auto pt = [&](size_t i) { return Pt{f.coordinates[i * 2], f.coordinates[i * 2 + 1]}; };
  size_t i = begin;
  while (i < end) {
    size_t j = i + 3;
    bool found = false;
    for (; j < end; ++j)
      if (samePt(pt(j), pt(i))) {
        found = true;
        break;
      }
    const size_t last = found ? j : end - 1;
    Path p;
    for (size_t k = i; k <= last; ++k)
      p.pts.push_back(pt(k));
    p.closed = found;
    p.min_keep = found ? 4 : 3;
    p.feature = feature_idx;
    p.part = part_idx;
    p.type = 2;
    out.push_back(std::move(p));
    i = last + 1;
  }
}

void collectPaths(const std::vector<Feature>& features, const std::set<std::string>& types,
                  std::vector<Path>& paths) {
  for (size_t fi = 0; fi < features.size(); ++fi) {
    const Feature& f = features[fi];
    const std::string tn = typeName(f);
    if (tn == "point" || types.find(tn) == types.end())
      continue;
    const auto ranges = featurePartRanges(f);
    for (size_t pi = 0; pi < ranges.size(); ++pi) {
      const auto& r = ranges[pi];
      if (r.second <= r.first)
        continue;
      if (tn == "line") {
        Path p;
        for (size_t k = r.first; k < r.second; ++k)
          p.pts.push_back({f.coordinates[k * 2], f.coordinates[k * 2 + 1]});
        // una linea cerrada (inicio == fin) se trata como un anillo: no debe colapsar a un punto
        const size_t np = p.pts.size();
        const bool loop = np >= 3 && samePt(p.pts.front(), p.pts.back());
        p.closed = loop && np >= 4;
        p.min_keep = loop ? (np >= 4 ? 4 : 3) : 2;
        p.feature = fi;
        p.part = pi;
        p.type = 1;
        paths.push_back(std::move(p));
      } else {
        splitRings(f, r.first, r.second, fi, pi, paths);
      }
    }
  }
}

// ------------------------------------------------------------------ topologia

struct Key {
  double x, y;
  bool operator==(const Key& o) const { return x == o.x && y == o.y; }
};

Key makeKey(const Pt& p) {
  return {p.x == 0 ? 0.0 : p.x, p.y == 0 ? 0.0 : p.y};
}

struct KeyHash {
  size_t operator()(const Key& k) const {
    uint64_t a, b;
    std::memcpy(&a, &k.x, 8);
    std::memcpy(&b, &k.y, 8);
    uint64_t h = a * 0x9E3779B97F4A7C15ULL;
    h ^= (b + 0x7F4A7C15ULL + (h << 6) + (h >> 2)) * 0xC2B2AE3D27D4EB4FULL;
    h ^= h >> 29;
    return static_cast<size_t>(h);
  }
};

struct VertexInfo {
  int uses = 0;
  bool path_end = false;  // extremo de una linea abierta
  std::vector<Key> neighbours;
};

// Un tramo entre dos nodos (o un anillo completo). 'idx' son indices de vertices del camino.
struct Arc {
  std::vector<int> idx;
  bool loop = false;       // empieza y termina en el mismo vertice
  bool canonical = true;   // se simplifica en orientacion canonica
};

bool lexLess(const std::vector<Pt>& a, const std::vector<Pt>& b) {
  const size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; ++i) {
    if (a[i].x != b[i].x)
      return a[i].x < b[i].x;
    if (a[i].y != b[i].y)
      return a[i].y < b[i].y;
  }
  return a.size() < b.size();
}

uint64_t hashPoints(const std::vector<Pt>& pts) {
  uint64_t h = 1469598103934665603ULL;
  for (const auto& p : pts) {
    uint64_t a, b;
    const Key k = makeKey(p);
    std::memcpy(&a, &k.x, 8);
    std::memcpy(&b, &k.y, 8);
    h = (h ^ a) * 1099511628211ULL;
    h = (h ^ b) * 1099511628211ULL;
  }
  return h;
}

// Simplifica un tramo y devuelve las marcas por posicion del tramo.
std::vector<char> simplifyArc(const Path& path, const Arc& arc, const Params& prm,
                              const std::unordered_set<uint64_t>& protect, uint64_t* id_out) {
  std::vector<Pt> pts;
  pts.reserve(arc.idx.size());
  for (int i : arc.idx)
    pts.push_back(path.pts[i]);
  bool reversed = false;
  if (arc.canonical) {
    std::vector<Pt> rev(pts.rbegin(), pts.rend());
    if (lexLess(rev, pts)) {
      pts.swap(rev);
      reversed = true;
    }
  }
  const uint64_t id = hashPoints(pts);
  if (id_out)
    *id_out = id;
  size_t min_keep = arc.loop ? 4 : 2;
  if (!arc.loop && protect.count(id))
    min_keep = 3;
  std::vector<char> flags = simplifyPath(pts, prm, min_keep);
  if (reversed)
    std::reverse(flags.begin(), flags.end());
  return flags;
}

struct TopologyStats {
  size_t nodes = 0;
  size_t arcs = 0;
  size_t rescued_rings = 0;
};

void simplifyWithTopology(std::vector<Path>& paths, const Params& prm, TopologyStats& ts) {
  std::unordered_map<Key, VertexInfo, KeyHash> graph;
  graph.reserve(paths.size() * 16);
  auto addNeighbour = [&](const Key& a, const Key& b) {
    auto& v = graph[a];
    for (const auto& k : v.neighbours)
      if (k == b)
        return;
    v.neighbours.push_back(b);
  };
  for (const auto& p : paths) {
    const int n = static_cast<int>(p.pts.size());
    const int distinct = p.closed ? n - 1 : n;
    for (int i = 0; i < distinct; ++i)
      graph[makeKey(p.pts[i])].uses++;
    if (!p.closed && n > 0) {
      graph[makeKey(p.pts.front())].path_end = true;
      graph[makeKey(p.pts.back())].path_end = true;
    }
    for (int i = 0; i + 1 < n; ++i) {
      const Key a = makeKey(p.pts[i]), b = makeKey(p.pts[i + 1]);
      if (a == b)
        continue;
      addNeighbour(a, b);
      addNeighbour(b, a);
    }
  }
  // nodo: cruce, bifurcacion, extremo de una linea abierta o vertice de otro tipo que no tenga
  // exactamente dos vecinos
  auto isNode = [&](const Pt& pt) {
    const VertexInfo& v = graph[makeKey(pt)];
    return v.neighbours.size() != 2 || v.path_end;
  };

  std::vector<std::vector<Arc>> arcs(paths.size());
  for (size_t pi = 0; pi < paths.size(); ++pi) {
    const Path& p = paths[pi];
    const int n = static_cast<int>(p.pts.size());
    if (!p.closed) {
      std::vector<int> nodes;
      for (int i = 0; i < n; ++i)
        if (i == 0 || i == n - 1 || isNode(p.pts[i]))
          nodes.push_back(i);
      for (size_t k = 0; k + 1 < nodes.size(); ++k) {
        Arc a;
        for (int i = nodes[k]; i <= nodes[k + 1]; ++i)
          a.idx.push_back(i);
        arcs[pi].push_back(std::move(a));
      }
      ts.nodes += nodes.size();
      continue;
    }
    const int m = n - 1;  // vertices distintos del anillo
    std::vector<int> nodes;
    for (int i = 0; i < m; ++i)
      if (isNode(p.pts[i]))
        nodes.push_back(i);
    ts.nodes += nodes.size();
    if (nodes.empty()) {
      bool shared = false;
      for (int i = 0; i < m && !shared; ++i)
        shared = graph[makeKey(p.pts[i])].uses > 1;
      Arc a;
      a.loop = true;
      if (!shared) {
        a.canonical = false;
        for (int i = 0; i < n; ++i)
          a.idx.push_back(i);
      } else {
        // anillo compartido completo: se parte del vertice menor, en sentido canonico
        int s = 0;
        for (int i = 1; i < m; ++i)
          if (p.pts[i].x < p.pts[s].x || (p.pts[i].x == p.pts[s].x && p.pts[i].y < p.pts[s].y))
            s = i;
        for (int k = 0; k <= m; ++k)
          a.idx.push_back((s + k) % m);
      }
      arcs[pi].push_back(std::move(a));
      continue;
    }
    if (nodes.size() == 1) {
      Arc a;
      a.loop = true;
      for (int k = 0; k <= m; ++k)
        a.idx.push_back((nodes[0] + k) % m);
      arcs[pi].push_back(std::move(a));
      continue;
    }
    for (size_t k = 0; k < nodes.size(); ++k) {
      const int from = nodes[k], to = nodes[(k + 1) % nodes.size()];
      Arc a;
      for (int i = from;; i = (i + 1) % m) {
        a.idx.push_back(i);
        if (i == to && a.idx.size() > 1)
          break;
      }
      arcs[pi].push_back(std::move(a));
    }
  }
  for (const auto& v : arcs)
    ts.arcs += v.size();

  std::unordered_set<uint64_t> protect;
  for (int pass = 0; pass < 2; ++pass) {
    bool any_degenerate = false;
    std::vector<std::vector<uint64_t>> ids(paths.size());
    for (size_t pi = 0; pi < paths.size(); ++pi) {
      Path& p = paths[pi];
      p.keep.assign(p.pts.size(), 0);
      ids[pi].assign(arcs[pi].size(), 0);
      for (size_t ai = 0; ai < arcs[pi].size(); ++ai) {
        const Arc& a = arcs[pi][ai];
        const std::vector<char> flags = simplifyArc(p, a, prm, protect, &ids[pi][ai]);
        for (size_t k = 0; k < a.idx.size(); ++k)
          if (flags[k])
            p.keep[a.idx[k]] = 1;
      }
      if (p.closed) {
        p.keep[p.pts.size() - 1] = p.keep[0];
        size_t distinct_kept = 0;
        for (size_t i = 0; i + 1 < p.pts.size(); ++i)
          distinct_kept += p.keep[i];
        if (distinct_kept < 3 && pass == 0) {
          any_degenerate = true;
          ts.rescued_rings++;
          for (uint64_t id : ids[pi])
            protect.insert(id);
        }
      }
    }
    if (!any_degenerate)
      break;
  }
}

// ----------------------------------------------------------------- reensamblado

struct Stats {
  size_t features = 0;
  size_t processed = 0;
  size_t original_vertices = 0;
  size_t simplified_vertices = 0;
  size_t rings_at_minimum = 0;
  size_t lines_at_minimum = 0;
  std::map<std::string, std::pair<size_t, size_t>> by_type;  // original, simplificado
};

void rebuild(const Spatial::VectorDataset& input, Spatial::VectorDataset& output,
             std::vector<Path>& paths, const std::set<std::string>& types, Stats& stats) {
  output.columns = input.columns;
  output.geometry_column = input.geometry_column;
  output.crs = input.crs;

  // kept points per (feature, part)
  std::map<std::pair<size_t, size_t>, std::vector<double>> result;
  std::map<std::pair<size_t, size_t>, bool> touched;
  for (auto& p : paths) {
    std::vector<size_t> kept;
    const size_t n = p.pts.size();
    if (p.closed) {
      for (size_t i = 0; i + 1 < n; ++i)
        if (p.keep[i])
          kept.push_back(i);
      if (!kept.empty())
        kept.push_back(kept.front());
    } else {
      for (size_t i = 0; i < n; ++i)
        if (p.keep[i])
          kept.push_back(i);
    }
    auto& dst = result[{p.feature, p.part}];
    for (size_t i : kept) {
      dst.push_back(p.pts[i].x);
      dst.push_back(p.pts[i].y);
    }
    touched[{p.feature, p.part}] = true;
    const size_t distinct_kept = p.closed ? (kept.empty() ? 0 : kept.size() - 1) : kept.size();
    if (p.closed && distinct_kept <= 3 && n - 1 > 3)
      stats.rings_at_minimum++;
    if (!p.closed && p.type == 1 && kept.size() == 2 && n > 2)
      stats.lines_at_minimum++;
    const std::string tn = p.type == 1 ? "line" : "polygon";
    stats.by_type[tn].second += kept.size();
  }

  for (size_t fi = 0; fi < input.features.size(); ++fi) {
    const Feature& f = input.features[fi];
    stats.features++;
    const std::string tn = typeName(f);
    const size_t nverts = f.coordinates.size() / 2;
    const bool processed = tn != "point" && types.find(tn) != types.end() && nverts > 0;
    Feature out = f;
    if (processed) {
      stats.processed++;
      stats.original_vertices += nverts;
      stats.by_type[tn].first += nverts;
      const auto ranges = featurePartRanges(f);
      std::vector<double> coords;
      std::vector<size_t> starts;
      for (size_t pi = 0; pi < ranges.size(); ++pi) {
        auto it = result.find({fi, pi});
        if (it == result.end())
          continue;
        starts.push_back(coords.size() / 2);
        coords.insert(coords.end(), it->second.begin(), it->second.end());
      }
      out.coordinates = coords;
      if (!f.part_starts.empty())
        out.part_starts = starts;
      stats.simplified_vertices += coords.size() / 2;
    }
    output.features.push_back(std::move(out));
  }
  output.feature_count = output.features.size();
}

// ------------------------------------------------------------------------ CLI

void printUsage() {
  std::cerr << "spatial_simplify - Reduce the number of vertices of lines and polygons\n\n";
  std::cerr << "Usage: spatial_simplify <input> <output> [options]\n\n";
  std::cerr << "Methods (-method):\n";
  std::cerr << "  douglas       Douglas-Peucker (default): keeps the vertices that stray more than\n";
  std::cerr << "                -tolerance from the simplified line\n";
  std::cerr << "  visvalingam   Visvalingam-Whyatt: removes the vertex whose triangle with its\n";
  std::cerr << "                neighbours has the smallest area; -tolerance is a minimum area\n";
  std::cerr << "  radial        Radial distance: removes vertices closer than -tolerance to the\n";
  std::cerr << "                last vertex kept\n";
  std::cerr << "  nth           Keeps one vertex out of every -n\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -tolerance <v>  douglas: distance (CRS units); visvalingam: area (CRS units squared);\n";
  std::cerr << "                  radial: distance (CRS units)\n";
  std::cerr << "  -factor <f>     douglas, visvalingam: fraction of the vertices of every line and ring\n";
  std::cerr << "                  to remove (0 < f <= 1; 0.7 keeps about 30%). Instead of -tolerance\n";
  std::cerr << "  -n <k>          nth: keep every k-th vertex (k >= 2)\n";
  std::cerr << "  -types <list>   Types to simplify: line,polygon (default: line,polygon);\n";
  std::cerr << "                  points are never changed\n";
  std::cerr << "  -topology       Keep shared borders and line junctions identical, so neighbouring\n";
  std::cerr << "                  polygons do not open gaps or overlap\n";
  std::cerr << "  -verbose        Show detailed statistics\n";
  std::cerr << "  -help           Show this help\n\n";
  std::cerr << "Lines always keep their first and last vertex (at least 2 vertices) and rings at least\n";
  std::cerr << "3 different vertices, so no geometry collapses.\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_simplify rivers.csv rivers_s.csv -tolerance 50\n";
  std::cerr << "  spatial_simplify districts.csv districts_s.csv -tolerance 0.001 -topology\n";
  std::cerr << "  spatial_simplify coast.csv coast_s.csv -method visvalingam -factor 0.8\n";
  std::cerr << "  spatial_simplify tracks.csv tracks_s.csv -method radial -tolerance 25\n";
  std::cerr << "  spatial_simplify tracks.csv tracks_n.csv -method nth -n 5\n";
}

std::string num6(double v) {
  std::ostringstream s;
  s << std::setprecision(12) << v;
  return s.str();
}

bool parseNumber(const char* s, double& out) {
  char* end = nullptr;
  out = std::strtod(s, &end);
  return end != s && *end == '\0' && std::isfinite(out);
}

}  // namespace

int main(int argc, char* argv[]) {
  std::string input_file, output_file;
  Params prm;
  std::set<std::string> types = {"line", "polygon"};
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
    } else if (arg == "-tolerance") {
      const char* v = value("-tolerance");
      if (!v)
        return 1;
      if (!parseNumber(v, prm.tolerance) || prm.tolerance <= 0) {
        std::cerr << "Error: -tolerance must be a number greater than 0, not '" << v << "'\n";
        return 1;
      }
      prm.has_tolerance = true;
    } else if (arg == "-factor") {
      const char* v = value("-factor");
      if (!v)
        return 1;
      if (!parseNumber(v, prm.factor) || prm.factor <= 0 || prm.factor > 1) {
        std::cerr << "Error: -factor must be greater than 0 and at most 1, not '" << v << "'\n";
        return 1;
      }
      prm.has_factor = true;
    } else if (arg == "-n") {
      const char* v = value("-n");
      if (!v)
        return 1;
      double d = 0;
      if (!parseNumber(v, d) || d < 2 || d != std::floor(d) || d > 1e9) {
        std::cerr << "Error: -n must be a whole number of at least 2, not '" << v << "'\n";
        return 1;
      }
      prm.n = static_cast<int>(d);
    } else if (arg == "-method") {
      const char* v = value("-method");
      if (!v)
        return 1;
      const std::string m = toLower(trim(v));
      if (m == "douglas" || m == "d")
        prm.method = Method::Douglas;
      else if (m == "visvalingam" || m == "v")
        prm.method = Method::Visvalingam;
      else if (m == "radial" || m == "r")
        prm.method = Method::Radial;
      else if (m == "nth" || m == "n")
        prm.method = Method::Nth;
      else {
        std::cerr << "Error: Unknown method: " << v << " (use douglas, visvalingam, radial or nth)\n";
        return 1;
      }
    } else if (arg == "-types") {
      const char* v = value("-types");
      if (!v)
        return 1;
      types.clear();
      for (const auto& t : split(v, ',')) {
        const std::string type = toLower(trim(t));
        if (type == "point" || type == "line" || type == "polygon")
          types.insert(type);
      }
      if (types.empty()) {
        std::cerr << "Error: No valid types specified (use line, polygon or point)\n";
        return 1;
      }
    } else if (arg == "-topology") {
      prm.topology = true;
    } else if (arg == "-verbose") {
      verbose = true;
    } else if (arg.size() > 1 && arg[0] == '-') {
      std::cerr << "Error: Unknown option: " << arg << "\n\n";
      printUsage();
      return 1;
    } else if (input_file.empty()) {
      input_file = arg;
    } else if (output_file.empty()) {
      output_file = arg;
    } else {
      std::cerr << "Error: Unexpected argument: " << arg << "\n";
      return 1;
    }
  }

  if (input_file.empty() || output_file.empty()) {
    printUsage();
    return 1;
  }

  // Parametros segun el metodo.
  switch (prm.method) {
    case Method::Douglas:
    case Method::Visvalingam:
      if (prm.has_tolerance == prm.has_factor) {
        std::cerr << "Error: " << methodName(prm.method)
                  << " needs exactly one of -tolerance or -factor\n";
        return 1;
      }
      if (prm.n) {
        std::cerr << "Error: -n only applies to -method nth\n";
        return 1;
      }
      break;
    case Method::Radial:
      if (!prm.has_tolerance || prm.has_factor || prm.n) {
        std::cerr << "Error: radial needs -tolerance (and does not use -factor or -n)\n";
        return 1;
      }
      break;
    case Method::Nth:
      if (!prm.n || prm.has_tolerance || prm.has_factor) {
        std::cerr << "Error: nth needs -n (and does not use -tolerance or -factor)\n";
        return 1;
      }
      break;
  }

  std::cout << "========================================\n";
  std::cout << "  SPATIAL SIMPLIFY\n";
  std::cout << "========================================\n\n";
  std::cout << "Input: " << input_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Method: " << methodName(prm.method) << "\n";
  std::ostringstream param_desc;
  if (prm.has_tolerance) {
    param_desc << "Tolerance: " << prm.tolerance;
    if (prm.method == Method::Visvalingam)
      param_desc << " (minimum triangle area)";
    param_desc << "\n";
  }
  if (prm.has_factor)
    param_desc << "Factor: " << prm.factor << " (fraction of vertices removed per line/ring)\n";
  if (prm.n)
    param_desc << "n: " << prm.n << "\n";
  std::cout << param_desc.str();
  if (prm.topology)
    std::cout << "Topology: shared borders and junctions are kept identical\n";
  std::cout << "Types: ";
  for (const auto& t : types)
    std::cout << t << " ";
  std::cout << "\n\n";

  Spatial::SpatialCSVReader reader;
  Spatial::VectorDataset dataset;
  try {
    if (!reader.read(input_file, dataset)) {
      std::cerr << "Error: Could not read input file: " << input_file << "\n";
      return 1;
    }
  } catch (const std::exception& e) {
    (void)e;
    std::cerr << "Error: Malformed geometry in " << input_file << " (a coordinate is not a valid number)\n";
    return 1;
  }
  size_t total_vertices = 0;
  for (const auto& f : dataset.features)
    total_vertices += f.coordinates.size() / 2;
  std::cout << "Input features: " << dataset.features.size() << "\n";
  std::cout << "Input vertices: " << total_vertices << "\n\n";

  std::vector<Path> paths;
  collectPaths(dataset.features, types, paths);

  TopologyStats ts;
  if (prm.topology) {
    simplifyWithTopology(paths, prm, ts);
  } else {
    for (auto& p : paths)
      p.keep = simplifyPath(p.pts, prm, p.min_keep);
  }

  Spatial::VectorDataset output;
  Stats stats;
  rebuild(dataset, output, paths, types, stats);

  std::vector<std::string> before = {std::string("# Simplified using: ") + methodName(prm.method)};
  if (prm.has_tolerance)
    before.push_back("# Tolerance: " + num6(prm.tolerance));
  if (prm.has_factor)
    before.push_back("# Factor: " + num6(prm.factor));
  if (prm.n)
    before.push_back("# n: " + std::to_string(prm.n));
  if (prm.topology)
    before.push_back("# Topology: preserved");
  writeVectorCSV(output, output_file, before,
                 {"# Total features: " + std::to_string(output.features.size())});
  {
    std::ifstream check(output_file);
    if (!check.good()) {
      std::cerr << "Error: Could not write the output file: " << output_file << "\n";
      return 1;
    }
  }

  const double reduction =
      stats.original_vertices > 0
          ? (1.0 - static_cast<double>(stats.simplified_vertices) / stats.original_vertices) * 100.0
          : 0.0;
  std::cout << "========================================\n";
  std::cout << "  SIMPLIFY COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Features processed: " << stats.processed << "\n";
  std::cout << "Features skipped: " << stats.features - stats.processed << "\n";
  std::cout << "Original vertices: " << stats.original_vertices << "\n";
  std::cout << "Simplified vertices: " << stats.simplified_vertices << "\n";
  std::cout << "Reduction: " << std::fixed << std::setprecision(1) << reduction << "%\n";
  std::cout << "Output features: " << output.features.size() << "\n";
  std::cout << "Output written to: " << output_file << "\n";

  if (stats.rings_at_minimum || stats.lines_at_minimum)
    std::cout << "\nNote: " << stats.rings_at_minimum << " ring(s) were reduced to a triangle and "
              << stats.lines_at_minimum
              << " line(s) to their two end points (the minimum that keeps them valid)\n";

  if (verbose) {
    std::cout << "\nDetailed statistics by type:\n";
    for (const auto& kv : stats.by_type) {
      const double red = kv.second.first > 0
                             ? (1.0 - static_cast<double>(kv.second.second) / kv.second.first) * 100.0
                             : 0.0;
      std::cout << "  " << kv.first << ": " << kv.second.first << " -> " << kv.second.second << " ("
                << std::fixed << std::setprecision(1) << red << "%)\n";
    }
    if (prm.topology)
      std::cout << "Topology: " << ts.nodes << " node(s), " << ts.arcs << " arc(s) simplified"
                << (ts.rescued_rings ? ", " + std::to_string(ts.rescued_rings) +
                                           " ring(s) kept above the minimum"
                                     : "")
                << "\n";
  }
  return 0;
}
