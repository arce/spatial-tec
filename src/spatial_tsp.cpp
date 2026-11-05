// spatial_tsp -- problema del agente viajero (TSP) sobre una red creada con
// spatial_network. Dado un conjunto de paradas, calcula el orden de visita de
// costo minimo usando los caminos minimos reales de la red (respeta 'oneway').
//
// Pasos:
//   1. Las paradas se resuelven a nodos (puntos -> nodo mas cercano, o IDs /
//      nombres de nodo).
//   2. Matriz de costos k x k: un Dijkstra desde cada parada con parada
//      anticipada al fijar todas las demas. La matriz es asimetrica si hay
//      calles de un solo sentido (TSP asimetrico).
//   3. Resolucion: Held-Karp (exacto) para pocas paradas; si no, vecino mas
//      cercano + busqueda local (2-opt y Or-opt, validos para costos
//      asimetricos) con reinicios perturbados (ILS) y semilla fija.
//   4. Cada tramo se reconstruye sobre la red y se escribe con su geometria.
//
// Rutas abiertas (-open / -end) se reducen a un ciclo agregando un nodo
// ficticio con costo cero hacia/desde las paradas permitidas.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "../include/core/spatial_csv.hpp"
#include "../include/core/spatial_network.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

using namespace SpatialNet;

namespace {

using Matrix = std::vector<std::vector<double>>;

constexpr int kMaxExactStops = 18;
constexpr int kAutoExactStops = 15;
constexpr double kAcceptSlack = 0.0015;
constexpr int kResetEvery = 300;
constexpr int kMaxSegment = 5;  // Or-opt: nodos consecutivos que se mueven a la vez

struct Options {
  std::string network_file;
  std::string stops_file;
  std::string nodes_list;
  std::string output_file = "tour.csv";
  std::string start_query;
  std::string end_query;
  CostKind kind = CostKind::Time;
  bool open_route = false;
  std::string method = "auto";
  int iterations = -1;  // -1 = segun el tamano
  int seed = 1;
  double time_limit = 0.0;  // segundos (0 = sin limite)
  double stop_tol = -1.0;
  bool verbose = false;
};

struct Stop {
  int node = -1;       // indice en Network::nodes
  std::string label;   // nombre mostrado
  double snap = -1.0;  // distancia al nodo (unidades del CRS); -1 si se dio el nodo
};

void printUsage() {
  std::cerr << "spatial_tsp - Travelling salesman: best order to visit a set of stops on a network\n\n";
  std::cerr << "Usage: spatial_tsp <network.network> (-stops <stops.csv> | -nodes <list>) [options]\n\n";
  std::cerr << "Stops (give exactly one):\n";
  std::cerr << "  -stops <file>    CSV with POINT geometries (snapped to the nearest node) or a 'node'\n";
  std::cerr << "                   column (node ID or name); an optional 'name' column labels the stops\n";
  std::cerr << "  -nodes <list>    Comma-separated node IDs or names, e.g. \"3,Hospital,17\"\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -cost <kind>     Cost to minimise: time (minutes, default) or length (metres)\n";
  std::cerr << "  -start <stop>    First stop: a stop name from the file, or a node ID/name; added to the stops\n";
  std::cerr << "                   if missing (default: the first stop)\n";
  std::cerr << "  -end <stop>      Last stop of an open route (same forms as -start); implies -open\n";
  std::cerr << "  -open            Do not return to the start (the end stop is free unless -end is given)\n";
  std::cerr << "  -method <m>      auto (default: exact up to " << kAutoExactStops << " stops), exact (up to "
            << kMaxExactStops << " stops) or heuristic\n";
  std::cerr << "  -iterations <n>  Perturbation rounds of the heuristic (default: automatic, up to 20000, stopping early\n";
  std::cerr << "                   when there is no progress; 0 = local search only)\n";
  std::cerr << "  -seed <n>        Random seed of the heuristic (default: 1)\n";
  std::cerr << "  -time_limit <s>  Stop the heuristic after <s> seconds and keep the best tour found (default: no limit;\n";
  std::cerr << "                   with a limit the result may differ between runs and machines)\n";
  std::cerr << "  -stop_tol <d>    Fail if a stop point is farther than <d> (CRS units) from its nearest node\n";
  std::cerr << "  -output <file>   Output CSV with one row per leg (default: tour.csv)\n";
  std::cerr << "  -verbose         Print the stops, the cost matrix (up to 15 stops) and the whole order\n";
  std::cerr << "  -help            Show this help\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_tsp roads.network -stops clients.csv -start Depot -output tour.csv\n";
  std::cerr << "  spatial_tsp roads.network -nodes \"Escuela,Hospital,Terminal,Mercado\" -cost length\n";
  std::cerr << "  spatial_tsp roads.network -stops clients.csv -start Depot -end Airport -open\n";
}

std::string findColumn(const std::vector<std::string>& columns,
                       std::initializer_list<const char*> names) {
  for (const char* n : names)
    for (const auto& c : columns)
      if (toLower(trim(c)) == n)
        return c;
  return "";
}

std::string nodeLabel(const Network& net, int idx) {
  const Node& n = net.nodes[idx];
  return n.name.empty() ? "node " + std::to_string(n.id) : n.name;
}

// ------------------------------------------------------------- lectura de paradas

struct Row {
  std::unordered_map<std::string, std::string> attrs;
  bool has_point = false;
  bool other_geometry = false;
  double x = 0, y = 0;
};

bool readStopRows(const std::string& file, std::vector<std::string>& columns,
                  std::vector<Row>& rows, std::string& err) {
  std::ifstream in(file);
  if (!in.is_open()) {
    err = "could not read stops file: " + file;
    return false;
  }
  std::string line, header;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#')
      continue;
    header = line;
    break;
  }
  if (header.empty()) {
    err = "stops file is empty: " + file;
    return false;
  }
  columns = splitCSV(header);
  bool has_geometry = false;
  for (const auto& c : columns) {
    const std::string l = toLower(trim(c));
    if (l == "geometry" || l == "geom" || l == "wkt")
      has_geometry = true;
  }

  if (has_geometry) {
    in.close();
    Spatial::SpatialCSVReader reader;
    Spatial::VectorDataset ds;
    if (!reader.read(file, ds)) {
      err = "could not read stops file: " + file;
      return false;
    }
    for (const auto& f : ds.features) {
      Row r;
      for (const auto& kv : f.attributes)
        r.attrs[kv.first] = trim(kv.second);
      if (f.coordinates.empty()) {
        // fila sin geometria: se usara la columna 'node'
      } else if (f.type == Spatial::VectorFeature::GeometryType::POINT && f.coordinates.size() >= 2) {
        r.has_point = true;
        r.x = f.coordinates[0];
        r.y = f.coordinates[1];
      } else {
        r.other_geometry = true;
      }
      rows.push_back(std::move(r));
    }
    return true;
  }

  // Sin columna de geometria: tabla simple (el lector del proyecto tomaria la
  // ultima columna como geometria).
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#')
      continue;
    const std::vector<std::string> values = splitCSV(line);
    Row r;
    for (size_t i = 0; i < columns.size() && i < values.size(); ++i)
      r.attrs[columns[i]] = trim(values[i]);
    rows.push_back(std::move(r));
  }
  return true;
}

bool loadStopsFile(const Network& net, const Options& opt, std::vector<Stop>& stops,
                   std::string& err) {
  std::vector<std::string> columns;
  std::vector<Row> rows;
  if (!readStopRows(opt.stops_file, columns, rows, err))
    return false;
  const std::string name_col = findColumn(columns, {"name", "nombre"});
  const std::string node_col = findColumn(columns, {"node", "node_id", "nodo"});

  std::unique_ptr<NodeGrid> grid;
  for (size_t i = 0; i < rows.size(); ++i) {
    const Row& r = rows[i];
    const std::string where = "stop " + std::to_string(i + 1);
    std::string given_name;
    if (!name_col.empty()) {
      auto it = r.attrs.find(name_col);
      if (it != r.attrs.end())
        given_name = trim(it->second);
    }
    std::string node_text;
    if (!node_col.empty()) {
      auto it = r.attrs.find(node_col);
      if (it != r.attrs.end())
        node_text = trim(it->second);
    }

    Stop s;
    if (!node_text.empty()) {
      std::string e;
      if (!findNode(net, node_text, s.node, e)) {
        err = where + ": " + e;
        return false;
      }
    } else if (r.has_point) {
      if (!grid)
        grid.reset(new NodeGrid(net.nodes));
      double d = 0;
      s.node = grid->nearest(r.x, r.y, d);
      if (s.node < 0) {
        err = where + ": the network has no nodes";
        return false;
      }
      if (opt.stop_tol >= 0 && d > opt.stop_tol) {
        err = where + (given_name.empty() ? "" : " '" + given_name + "'") + " is " + num(d, 6) +
              " from the nearest node (> -stop_tol " + num(opt.stop_tol, 6) + ")";
        return false;
      }
      s.snap = d;
    } else if (r.other_geometry) {
      err = where + ": the geometry is not a POINT";
      return false;
    } else {
      err = where + ": it has neither a POINT geometry nor a 'node' value";
      return false;
    }
    s.label = !given_name.empty() ? given_name : nodeLabel(net, s.node);

    bool duplicate = false;
    for (const auto& prev : stops)
      if (prev.node == s.node) {
        std::cerr << "Warning: " << where << " ('" << s.label << "') falls on the same node as '"
                  << prev.label << "'; merged\n";
        duplicate = true;
        break;
      }
    if (!duplicate)
      stops.push_back(s);
  }
  return true;
}

bool loadStopsList(const Network& net, const std::string& list, std::vector<Stop>& stops,
                   std::string& err) {
  std::vector<std::string> items;
  std::string cur;
  for (char c : list) {
    if (c == ',') {
      items.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  items.push_back(cur);
  for (const auto& raw : items) {
    const std::string q = trim(raw);
    if (q.empty())
      continue;
    Stop s;
    std::string e;
    if (!findNode(net, q, s.node, e)) {
      err = e;
      return false;
    }
    s.label = nodeLabel(net, s.node);
    bool duplicate = false;
    for (const auto& prev : stops)
      if (prev.node == s.node) {
        std::cerr << "Warning: '" << q << "' repeats node " << net.nodes[s.node].id << "; merged\n";
        duplicate = true;
        break;
      }
    if (!duplicate)
      stops.push_back(s);
  }
  return true;
}

// ------------------------------------------------------------------- solucion

double tourCost(const Matrix& M, const std::vector<int>& t) {
  double c = 0;
  const size_t n = t.size();
  for (size_t i = 0; i < n; ++i)
    c += M[t[i]][t[(i + 1) % n]];
  return c;
}

// Held-Karp con 'anchor' como origen del ciclo. M finita (sin infinitos).
std::vector<int> solveExact(const Matrix& M, int anchor) {
  const int N = static_cast<int>(M.size());
  const int m = N - 1;
  if (m <= 0)
    return {anchor};
  std::vector<int> o;
  for (int i = 0; i < N; ++i)
    if (i != anchor)
      o.push_back(i);
  const size_t full = static_cast<size_t>(1) << m;
  std::vector<double> dp(full * m, kInf);
  std::vector<signed char> par(full * m, -1);
  for (int j = 0; j < m; ++j)
    dp[(static_cast<size_t>(1) << j) * m + j] = M[anchor][o[j]];
  for (size_t mask = 1; mask < full; ++mask) {
    for (int j = 0; j < m; ++j) {
      if (!((mask >> j) & 1))
        continue;
      const double cur = dp[mask * m + j];
      if (!(cur < kInf))
        continue;
      for (int t = 0; t < m; ++t) {
        if ((mask >> t) & 1)
          continue;
        const size_t nm = mask | (static_cast<size_t>(1) << t);
        const double v = cur + M[o[j]][o[t]];
        if (v < dp[nm * m + t]) {
          dp[nm * m + t] = v;
          par[nm * m + t] = static_cast<signed char>(j);
        }
      }
    }
  }
  double best = kInf;
  int bj = 0;
  for (int j = 0; j < m; ++j) {
    const double v = dp[(full - 1) * m + j] + M[o[j]][anchor];
    if (v < best) {
      best = v;
      bj = j;
    }
  }
  std::vector<int> seq;
  size_t mask = full - 1;
  int j = bj;
  while (j >= 0) {
    seq.push_back(o[j]);
    const int pj = par[mask * m + j];
    mask ^= (static_cast<size_t>(1) << j);
    j = pj;
  }
  std::reverse(seq.begin(), seq.end());
  std::vector<int> tour;
  tour.push_back(anchor);
  tour.insert(tour.end(), seq.begin(), seq.end());
  return tour;
}

std::vector<int> nearestNeighbour(const Matrix& M, int anchor) {
  const int N = static_cast<int>(M.size());
  std::vector<int> t;
  std::vector<char> used(N, 0);
  t.push_back(anchor);
  used[anchor] = 1;
  while (static_cast<int>(t.size()) < N) {
    const int u = t.back();
    int best = -1;
    for (int v = 0; v < N; ++v)
      if (!used[v] && (best < 0 || M[u][v] < M[u][best]))
        best = v;
    t.push_back(best);
    used[best] = 1;
  }
  return t;
}

// Busqueda local para costos asimetricos: 2-opt (la inversion de un tramo
// cambia el costo interno, que se obtiene con sumas acumuladas) y Or-opt
// (mover 1-3 nodos consecutivos, con o sin invertirlos). Solo se examinan los
// nodos "activos" (los extremos de aristas que cambiaron), como en las listas
// "don't look bits": tras una perturbacion local basta revisar su entorno.
class LocalSearch {
 public:
  LocalSearch(const Matrix& M, double eps)
      : M_(M), N_(static_cast<int>(M.size())), eps_(eps), pos_(N_), act_(N_, 0) {
    // Con costos simetricos, invertir un tramo no cambia sus arcos internos.
    symmetric_ = true;
    for (int i = 0; i < N_ && symmetric_; ++i)
      for (int j = i + 1; j < N_; ++j)
        if (std::fabs(M[i][j] - M[j][i]) > eps) {
          symmetric_ = false;
          break;
        }
  }

  // Optimizacion completa: todos los nodos activos.
  void optimizeAll(std::vector<int>& t) {
    if (N_ < 4)
      return;
    t_ = &t;
    refresh();
    for (int i = N_ - 1; i >= 0; --i)
      activate(t[i]);
    run();
  }

  // Solo los nodos 'seeds' estan activos al comenzar.
  void optimizeFrom(std::vector<int>& t, const std::vector<int>& seeds) {
    if (N_ < 4)
      return;
    t_ = &t;
    refresh();
    for (int v : seeds)
      activate(v);
    run();
  }

 private:
  void activate(int v) {
    if (!act_[v]) {
      act_[v] = 1;
      todo_.push_back(v);
    }
  }

  void run() {
    while (!todo_.empty()) {
      const int a = todo_.back();
      todo_.pop_back();
      act_[a] = 0;
      if (!twoOptNode(a))
        orOptNode(a);
    }
  }

  void refresh() {
    const std::vector<int>& t = *t_;
    F_.assign(N_, 0.0);
    R_.assign(N_, 0.0);
    for (int k = 0; k < N_; ++k)
      pos_[t[k]] = k;
    for (int k = 0; k + 1 < N_; ++k) {
      F_[k + 1] = F_[k] + M_[t[k]][t[k + 1]];
      R_[k + 1] = R_[k] + M_[t[k + 1]][t[k]];
    }
    W_ = M_[t[N_ - 1]][t[0]];
    WR_ = M_[t[0]][t[N_ - 1]];
  }

  // Costo de recorrer el tramo de posiciones p..q (p != q, ciclico) hacia adelante / invertido.
  double cycF(int p, int q) const { return p < q ? F_[q] - F_[p] : (F_[N_ - 1] - F_[p]) + W_ + F_[q]; }
  double cycR(int p, int q) const { return p < q ? R_[q] - R_[p] : (R_[N_ - 1] - R_[p]) + WR_ + R_[q]; }

  bool twoOptNode(int a) {
    std::vector<int>& t = *t_;
    const int i = pos_[a];
    const int p = (i + 1) % N_;
    const int b = t[p];
    for (int off = 2; off <= N_ - 1; ++off) {
      const int j = (i + off) % N_;
      const int c = t[j], d = t[(j + 1) % N_];
      const double delta = (M_[a][c] + cycR(p, j) + M_[b][d]) - (M_[a][b] + cycF(p, j) + M_[c][d]);
      if (delta < -eps_) {
        for (int s = 0; s < off / 2; ++s)
          std::swap(t[(p + s) % N_], t[(p + off - 1 - s) % N_]);
        refresh();
        activate(a);
        activate(b);
        activate(c);
        activate(d);
        // Con costos asimetricos, invertir el tramo cambia el costo de todos sus arcos.
        if (!symmetric_)
          for (int s = 0; s < off; ++s)
            activate(t[(p + s) % N_]);
        return true;
      }
    }
    return false;
  }

  bool orOptNode(int a) {
    for (int L = 1; L <= kMaxSegment && L <= N_ - 3; ++L) {
      if (tryOrOpt(pos_[a], L))
        return true;
      if (L > 1 && tryOrOpt((pos_[a] - L + 1 + N_) % N_, L))
        return true;
    }
    return false;
  }

  // Mueve el tramo de L nodos que empieza en la posicion s a su mejor lugar.
  bool tryOrOpt(int s, int L) {
    std::vector<int>& t = *t_;
    seg_.assign(L, 0);
    for (int l = 0; l < L; ++l)
      seg_[l] = t[(s + l) % N_];
    const int p = t[(s - 1 + N_) % N_];
    const int q = t[(s + L) % N_];
    double intf = 0, intr = 0;
    for (int l = 0; l + 1 < L; ++l) {
      intf += M_[seg_[l]][seg_[l + 1]];
      intr += M_[seg_[l + 1]][seg_[l]];
    }
    const double removal = M_[p][seg_[0]] + intf + M_[seg_[L - 1]][q] - M_[p][q];
    rest_.clear();
    for (int l = 0; l < N_ - L; ++l)
      rest_.push_back(t[(s + L + l) % N_]);
    double best_delta = -eps_;
    int best_r = -1;
    bool best_rev = false;
    for (int r = 0; r + 1 < N_ - L; ++r) {
      const int u = rest_[r], v = rest_[r + 1];
      const double base = M_[u][v];
      const double d1 = M_[u][seg_[0]] + intf + M_[seg_[L - 1]][v] - base - removal;
      if (d1 < best_delta) {
        best_delta = d1;
        best_r = r;
        best_rev = false;
      }
      if (L > 1) {
        const double d2 = M_[u][seg_[L - 1]] + intr + M_[seg_[0]][v] - base - removal;
        if (d2 < best_delta) {
          best_delta = d2;
          best_r = r;
          best_rev = true;
        }
      }
    }
    if (best_r < 0)
      return false;
    const int u = rest_[best_r], v = rest_[best_r + 1];
    std::vector<int> nt;
    nt.reserve(N_);
    nt.insert(nt.end(), rest_.begin(), rest_.begin() + best_r + 1);
    if (best_rev)
      nt.insert(nt.end(), seg_.rbegin(), seg_.rend());
    else
      nt.insert(nt.end(), seg_.begin(), seg_.end());
    nt.insert(nt.end(), rest_.begin() + best_r + 1, rest_.end());
    t.swap(nt);
    refresh();
    activate(p);
    activate(q);
    activate(u);
    activate(v);
    activate(seg_.front());
    activate(seg_.back());
    return true;
  }

  const Matrix& M_;
  int N_;
  double eps_;
  std::vector<int>* t_ = nullptr;
  std::vector<int> pos_;
  std::vector<char> act_;
  std::vector<int> todo_, seg_, rest_;
  std::vector<double> F_, R_;
  double W_ = 0, WR_ = 0;
  bool symmetric_ = false;
};

struct Solution {
  std::vector<int> tour;  // permutacion de 0..N-1 (ciclo)
  double initial_cost = 0;
  int iterations = 0;
  bool exact = false;
  bool timed_out = false;
};

// iterations < 0: presupuesto segun el tamano, con parada anticipada si la mejor
// solucion no mejora durante muchas rondas seguidas. iterations >= 0: exactamente
// esas rondas.
Solution solveHeuristic(const Matrix& M, int anchor, int iterations, int seed, double eps,
                        double time_limit) {
  Solution sol;
  const int N = static_cast<int>(M.size());
  std::vector<int> t = nearestNeighbour(M, anchor);
  sol.initial_cost = tourCost(M, t);
  LocalSearch ls(M, eps);
  ls.optimizeAll(t);
  std::vector<int> best = t, cur = t, cand, seeds;
  double best_cost = tourCost(M, best), cur_cost = best_cost;
  sol.iterations = 0;
  const auto t_start = std::chrono::steady_clock::now();
  if (N >= 8) {
    const bool automatic = iterations < 0;
    int stall = 0;
    const int stall_limit = std::max(2000, 20 * N);
    if (automatic) {
      const double budget = 1.5e9 / (static_cast<double>(N) * N);
      iterations = static_cast<int>(std::max(500.0, std::min(20000.0, budget)));
    }
    std::mt19937_64 rng(static_cast<unsigned long long>(seed));
    const int max_len = std::max(1, std::min(50, (N - 2) / 3));
    for (int it = 0; it < iterations; ++it) {
      // doble puente local: intercambia dos tramos contiguos (conserva el sentido)
      const int l1 = 1 + static_cast<int>(rng() % max_len);
      const int l2 = 1 + static_cast<int>(rng() % max_len);
      const int span = N - l1 - l2;  // posiciones posibles para el inicio del primer tramo
      if (span < 1)
        continue;
      const int p1 = static_cast<int>(rng() % span);
      cand.clear();
      cand.insert(cand.end(), cur.begin(), cur.begin() + p1);
      cand.insert(cand.end(), cur.begin() + p1 + l1, cur.begin() + p1 + l1 + l2);
      cand.insert(cand.end(), cur.begin() + p1, cur.begin() + p1 + l1);
      cand.insert(cand.end(), cur.begin() + p1 + l1 + l2, cur.end());
      // nodos en los extremos de las 3 aristas nuevas
      seeds.clear();
      const int cuts[3] = {p1, p1 + l2, p1 + l1 + l2};
      for (int cpos : cuts) {
        seeds.push_back(cand[(cpos - 1 + N) % N]);
        seeds.push_back(cand[cpos % N]);
      }
      ls.optimizeFrom(cand, seeds);
      const double c = tourCost(M, cand);
      // se acepta un empeoramiento minimo para no quedar atrapado; la mejor solucion se guarda aparte
      if (c <= cur_cost * (1.0 + kAcceptSlack)) {
        cur = cand;
        cur_cost = c;
        if (c < best_cost - eps) {
          best = cand;
          best_cost = c;
          stall = 0;
        }
      }
      sol.iterations++;
      ++stall;
      if (stall % kResetEvery == 0) {  // volver a la mejor solucion si no hay progreso
        cur = best;
        cur_cost = best_cost;
      }
      if (automatic && stall >= stall_limit)
        break;
      if (time_limit > 0 && (sol.iterations & 15) == 0 &&
          std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count() >= time_limit) {
        sol.timed_out = true;
        break;
      }
    }
    ls.optimizeAll(best);  // pulido final con el vecindario completo
  }
  sol.tour = best;
  return sol;
}

// ----------------------------------------------------------------------- salida

bool writeTourCSV(const Network& net, const std::vector<Stop>& stops, const std::vector<int>& order,
                  bool closed, CostKind kind, const std::string& method, double total_cost,
                  double total_length, double total_time, const std::vector<std::string>& wkt,
                  const std::vector<double>& leg_cost, const std::vector<double>& leg_length,
                  const std::vector<double>& leg_time, const std::vector<int>& leg_nodes,
                  const std::string& filename, std::string& err) {
  std::ofstream file(filename);
  if (!file.is_open()) {
    err = "could not create file: " + filename;
    return false;
  }
  file << "# Travelling salesman tour\n";
  file << "# CRS: " << net.crs << "\n";
  file << "# Cost: " << costName(kind) << " (" << costUnit(net, kind) << ")\n";
  file << "# Route: " << (closed ? "closed" : "open") << "\n";
  file << "# Method: " << method << "\n";
  file << "# Stops: " << stops.size() << "\n";
  file << "# Total cost: " << num(total_cost) << "\n";
  file << "# Total length: " << num(total_length) << "\n";
  file << "# Total time: " << num(total_time) << "\n";
  file << "# Geometry column: geometry\n";
  file << "id,from_name,to_name,from_node,to_node,cost,cumulative_cost,length,time,node_count,geometry\n";
  const size_t legs = wkt.size();
  double cum = 0;
  for (size_t i = 0; i < legs; ++i) {
    const Stop& a = stops[order[i]];
    const Stop& b = stops[order[(i + 1) % order.size()]];
    cum += leg_cost[i];
    file << (i + 1) << "," << csvField(a.label) << "," << csvField(b.label) << ","
         << net.nodes[a.node].id << "," << net.nodes[b.node].id << "," << num(leg_cost[i]) << ","
         << num(cum) << "," << num(leg_length[i]) << "," << num(leg_time[i]) << "," << leg_nodes[i]
         << ",\"" << wkt[i] << "\"\n";
  }
  file.flush();
  if (!file.good()) {
    err = "error while writing file: " + filename;
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char* argv[]) {
  Options opt;
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
    } else if (arg == "-stops") {
      const char* v = value("-stops");
      if (!v)
        return 1;
      opt.stops_file = v;
    } else if (arg == "-nodes") {
      const char* v = value("-nodes");
      if (!v)
        return 1;
      opt.nodes_list = v;
    } else if (arg == "-start") {
      const char* v = value("-start");
      if (!v)
        return 1;
      opt.start_query = v;
    } else if (arg == "-end") {
      const char* v = value("-end");
      if (!v)
        return 1;
      opt.end_query = v;
    } else if (arg == "-open") {
      opt.open_route = true;
    } else if (arg == "-output") {
      const char* v = value("-output");
      if (!v)
        return 1;
      opt.output_file = v;
    } else if (arg == "-cost") {
      const char* v = value("-cost");
      if (!v)
        return 1;
      if (!parseCostKind(v, opt.kind)) {
        std::cerr << "Error: -cost must be 'length' or 'time', not '" << v << "'\n";
        return 1;
      }
    } else if (arg == "-method") {
      const char* v = value("-method");
      if (!v)
        return 1;
      opt.method = toLower(trim(v));
      if (opt.method != "auto" && opt.method != "exact" && opt.method != "heuristic") {
        std::cerr << "Error: -method must be auto, exact or heuristic, not '" << v << "'\n";
        return 1;
      }
    } else if (arg == "-iterations") {
      const char* v = value("-iterations");
      if (!v)
        return 1;
      if (!parseInt(v, opt.iterations) || opt.iterations < 0) {
        std::cerr << "Error: invalid value for -iterations: " << v << "\n";
        return 1;
      }
    } else if (arg == "-seed") {
      const char* v = value("-seed");
      if (!v)
        return 1;
      if (!parseInt(v, opt.seed) || opt.seed < 0) {
        std::cerr << "Error: invalid value for -seed: " << v << "\n";
        return 1;
      }
    } else if (arg == "-time_limit") {
      const char* v = value("-time_limit");
      if (!v)
        return 1;
      if (!parseDouble(v, opt.time_limit) || opt.time_limit < 0) {
        std::cerr << "Error: invalid value for -time_limit: " << v << "\n";
        return 1;
      }
    } else if (arg == "-stop_tol") {
      const char* v = value("-stop_tol");
      if (!v)
        return 1;
      if (!parseDouble(v, opt.stop_tol) || opt.stop_tol < 0) {
        std::cerr << "Error: invalid value for -stop_tol: " << v << "\n";
        return 1;
      }
    } else if (arg == "-verbose") {
      opt.verbose = true;
    } else if (!arg.empty() && arg[0] == '-') {
      std::cerr << "Error: Unknown option: " << arg << "\n";
      printUsage();
      return 1;
    } else if (opt.network_file.empty()) {
      opt.network_file = arg;
    } else {
      std::cerr << "Error: Unexpected argument: " << arg << "\n";
      printUsage();
      return 1;
    }
  }

  if (opt.network_file.empty()) {
    std::cerr << "Error: Missing the network file\n";
    printUsage();
    return 1;
  }
  if (opt.stops_file.empty() == opt.nodes_list.empty()) {
    std::cerr << "Error: Give the stops with exactly one of -stops <file> or -nodes <list>\n";
    printUsage();
    return 1;
  }

  const auto t_begin = std::chrono::steady_clock::now();

  Network net;
  std::string err;
  if (!readNetwork(opt.network_file, net, err)) {
    std::cerr << "Error: " << err << "\n";
    return 1;
  }
  if (net.nodes.empty()) {
    std::cerr << "Error: the network has no nodes\n";
    return 1;
  }
  size_t load_errors = 0;
  for (const auto& is : net.load_issues)
    if (is.severity == "error")
      load_errors++;
  if (load_errors)
    std::cerr << "Warning: " << load_errors << " problem(s) while reading " << opt.network_file
              << " (run spatial_network_info -validate for details)\n";

  std::vector<Stop> stops;
  if (!opt.stops_file.empty()) {
    if (!loadStopsFile(net, opt, stops, err)) {
      std::cerr << "Error: " << err << "\n";
      return 1;
    }
  } else if (!loadStopsList(net, opt.nodes_list, stops, err)) {
    std::cerr << "Error: " << err << "\n";
    return 1;
  }

  // Extremos: se agregan a las paradas si no estaban.
  auto locate = [&](const std::string& q, const char* what, bool front, int& idx) {
    // 1) nombre de una parada del archivo, 2) ID o nombre de un nodo de la red
    const std::string lq = toLower(trim(q));
    for (size_t i = 0; i < stops.size(); ++i)
      if (toLower(trim(stops[i].label)) == lq) {
        idx = static_cast<int>(i);
        return true;
      }
    int node = -1;
    std::string e;
    if (!findNode(net, q, node, e)) {
      std::cerr << "Error: " << what << ": " << e << "\n";
      return false;
    }
    for (size_t i = 0; i < stops.size(); ++i)
      if (stops[i].node == node) {
        idx = static_cast<int>(i);
        return true;
      }
    Stop s;
    s.node = node;
    s.label = nodeLabel(net, node);
    std::cerr << "Note: " << what << " " << (net.nodes[node].name.empty() ? s.label : "'" + s.label + "' (node " + std::to_string(net.nodes[node].id) + ")")
              << " is not among the stops; it was added\n";
    if (front) {
      stops.insert(stops.begin(), s);
      idx = 0;
    } else {
      stops.push_back(s);
      idx = static_cast<int>(stops.size()) - 1;
    }
    return true;
  };
  int start_idx = 0, end_idx = -1;
  if (!opt.start_query.empty() && !locate(opt.start_query, "Start", true, start_idx))
    return 1;
  if (!opt.end_query.empty()) {
    if (!locate(opt.end_query, "End", false, end_idx))
      return 1;
  }

  const int k = static_cast<int>(stops.size());
  if (k < 2) {
    std::cerr << "Error: at least 2 different stops are needed (found " << k << ")\n";
    return 1;
  }

  bool closed = !opt.open_route && end_idx < 0;
  if (end_idx >= 0 && end_idx == start_idx) {
    std::cerr << "Note: the end is the same stop as the start; computing a closed tour\n";
    closed = true;
    end_idx = -1;
  }
  const bool free_end = !closed && end_idx < 0;

  if (opt.method == "exact" && k > kMaxExactStops) {
    std::cerr << "Error: -method exact supports at most " << kMaxExactStops << " stops (found " << k
              << "); use -method heuristic or auto\n";
    return 1;
  }

  // ---- matriz de costos entre paradas
  const Graph graph = buildGraph(net, opt.kind);
  std::vector<char> is_stop(net.nodes.size(), 0);
  for (const auto& s : stops)
    is_stop[s.node] = 1;
  Matrix C(k, std::vector<double>(k, kInf));
  {
    PathTree tree;
    for (int i = 0; i < k; ++i) {
      dijkstra(graph, stops[i].node, tree, -1, kInf, &is_stop, k);
      for (int j = 0; j < k; ++j)
        C[i][j] = (i == j) ? 0.0 : tree.dist[stops[j].node];
    }
  }

  // ---- alcanzabilidad
  auto fail_unreachable = [&](const std::vector<std::string>& names, const std::string& msg) {
    std::cerr << "Error: " << msg << ":";
    for (size_t i = 0; i < names.size() && i < 8; ++i)
      std::cerr << " '" << names[i] << "'";
    if (names.size() > 8)
      std::cerr << " ... (" << names.size() << " in total)";
    std::cerr << "\n       The network may be disconnected or have one-way streets in the way "
                 "(check it with spatial_network_info -validate)\n";
  };
  {
    std::vector<std::string> bad;
    for (int j = 0; j < k; ++j)
      if (!std::isfinite(C[start_idx][j]))
        bad.push_back(stops[j].label);
    if (!bad.empty()) {
      fail_unreachable(bad, "stops that cannot be reached from the start '" + stops[start_idx].label + "'");
      return 1;
    }
    bad.clear();
    const int back_to = closed ? start_idx : end_idx;
    if (back_to >= 0) {
      for (int j = 0; j < k; ++j)
        if (!std::isfinite(C[j][back_to]))
          bad.push_back(stops[j].label);
      if (!bad.empty()) {
        fail_unreachable(bad, std::string("stops from which '") + stops[back_to].label +
                                  "' cannot be reached");
        return 1;
      }
    }
  }

  // ---- problema asimetrico en forma de ciclo
  double max_c = 0;
  for (int i = 0; i < k; ++i)
    for (int j = 0; j < k; ++j)
      if (std::isfinite(C[i][j]))
        max_c = std::max(max_c, C[i][j]);
  const double BIG = (max_c + 1.0) * 1e6;
  const double eps = 1e-9 * (max_c + 1.0);
  const int N = closed ? k : k + 1;
  Matrix M(N, std::vector<double>(N, 0.0));
  for (int i = 0; i < k; ++i)
    for (int j = 0; j < k; ++j)
      M[i][j] = (i == j) ? 0.0 : (std::isfinite(C[i][j]) ? C[i][j] : BIG);
  int anchor = start_idx;
  if (!closed) {
    const int D = k;
    for (int i = 0; i < k; ++i) {
      M[i][D] = (free_end || i == end_idx) ? 0.0 : BIG;
      M[D][i] = (i == start_idx) ? 0.0 : BIG;
    }
    anchor = D;
  }

  bool use_exact = (opt.method == "exact") || (opt.method == "auto" && k <= kAutoExactStops);
  Solution sol;
  if (use_exact) {
    sol.tour = solveExact(M, anchor);
    sol.exact = true;
    sol.initial_cost = tourCost(M, nearestNeighbour(M, anchor));
  } else {
    sol = solveHeuristic(M, anchor, opt.iterations, opt.seed, eps, opt.time_limit);
  }
  if (tourCost(M, sol.tour) >= BIG / 2) {
    std::cerr << "Error: no route visits every stop"
              << (closed ? " and returns to the start" : "")
              << " with the given constraints (one-way streets may make it impossible)\n";
    return 1;
  }

  // Orden de visita (indices de parada), empezando en la parada de inicio.
  std::vector<int> order;
  {
    size_t pos = 0;
    while (sol.tour[pos] != anchor)
      pos++;
    for (int i = 0; i < N; ++i) {
      const int v = sol.tour[(pos + i) % N];
      if (v < k)
        order.push_back(v);
    }
  }
  if (order.front() != start_idx) {
    std::cerr << "Error: internal error: the tour does not begin at the start stop\n";
    return 1;
  }
  const size_t legs = closed ? order.size() : order.size() - 1;

  // ---- reconstruccion de los tramos
  std::vector<std::string> wkt(legs);
  std::vector<double> leg_cost(legs, 0), leg_len(legs, 0), leg_time(legs, 0);
  std::vector<int> leg_nodes(legs, 0);
  double total_cost = 0, total_length = 0, total_time = 0;
  {
    PathTree tree;
    Path path;
    for (size_t i = 0; i < legs; ++i) {
      const int a = stops[order[i]].node;
      const int b = stops[order[(i + 1) % order.size()]].node;
      dijkstra(graph, a, tree, b);
      if (!buildPath(tree, a, b, path)) {
        std::cerr << "Error: internal error: no path between consecutive stops\n";
        return 1;
      }
      leg_cost[i] = tree.dist[b];
      for (int ei : path.edges) {
        leg_len[i] += net.edges[ei].length;
        leg_time[i] += net.edges[ei].travel_time;
      }
      leg_nodes[i] = static_cast<int>(path.nodes.size());
      std::string w = "LINESTRING(";
      for (size_t p = 0; p < path.nodes.size(); ++p) {
        const Node& n = net.nodes[path.nodes[p]];
        if (p > 0)
          w += ", ";
        w += num(n.x, 12) + " " + num(n.y, 12);
      }
      w += ")";
      wkt[i] = w;
      total_cost += leg_cost[i];
      total_length += leg_len[i];
      total_time += leg_time[i];
    }
  }

  const std::string method_name =
      sol.exact ? "exact (Held-Karp)" : "heuristic (nearest neighbour + 2-opt/Or-opt + perturbation)";
  if (!writeTourCSV(net, stops, order, closed, opt.kind, method_name, total_cost, total_length,
                    total_time, wkt, leg_cost, leg_len, leg_time, leg_nodes, opt.output_file, err)) {
    std::cerr << "Error: " << err << "\n";
    return 1;
  }

  const double elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - t_begin).count();
  const std::string lunit = net.length_unit == "meters" ? "m" : "units";
  const std::string cunit = costUnit(net, opt.kind);

  std::cout << "========================================\n";
  std::cout << "  TRAVELLING SALESMAN TOUR\n";
  std::cout << "========================================\n\n";
  std::cout << "Network: " << opt.network_file << " (" << net.nodes.size() << " nodes, "
            << net.edges.size() << " edges)\n";
  std::cout << "Cost: " << costName(opt.kind) << " (" << cunit << ")\n";
  std::cout << "Stops: " << k << "\n";
  std::cout << "Route: " << (closed ? "closed (returns to the start)" : (free_end ? "open (free end)" : "open (fixed end)"))
            << "\n";
  std::cout << "Start: " << stops[start_idx].label << "\n";
  if (!closed && end_idx >= 0)
    std::cout << "End: " << stops[end_idx].label << "\n";
  std::cout << "Method: " << method_name << "\n";
  if (!sol.exact)
    std::cout << "Iterations: " << sol.iterations << " (seed " << opt.seed << ")"
              << (sol.timed_out ? " - stopped by -time_limit" : "") << "\n";
  std::cout << "\n";

  if (opt.verbose) {
    std::cout << "Stops:\n";
    for (int i = 0; i < k; ++i) {
      std::cout << "  " << std::setw(3) << (i + 1) << "  " << stops[i].label << "  (node "
                << net.nodes[stops[i].node].id << ")";
      if (stops[i].snap >= 0)
        std::cout << "  snapped " << num(stops[i].snap, 6) << " CRS units";
      std::cout << "\n";
    }
    std::cout << "\n";
    if (k <= 15) {
      std::cout << "Cost matrix (" << cunit << ", row = from, column = to):\n      ";
      for (int j = 0; j < k; ++j)
        std::cout << std::setw(10) << (j + 1);
      std::cout << "\n";
      for (int i = 0; i < k; ++i) {
        std::cout << std::setw(5) << (i + 1) << " ";
        for (int j = 0; j < k; ++j)
          std::cout << std::setw(10) << std::fixed << std::setprecision(3) << C[i][j];
        std::cout << "\n";
      }
      std::cout << std::defaultfloat << "\n";
    }
  }

  if (k <= 40 || opt.verbose) {
    std::cout << "Order of visit:\n";
    std::cout << "  #   " << std::left << std::setw(28) << "Stop" << std::right << std::setw(8) << "Node"
              << std::setw(14) << "Leg cost" << std::setw(14) << "Cumulative" << "\n";
    double cum = 0;
    for (size_t i = 0; i < order.size() + (closed ? 1 : 0); ++i) {
      const Stop& s = stops[order[i % order.size()]];
      const bool back = closed && i == order.size();
      if (i > 0)
        cum += leg_cost[i - 1];
      std::cout << "  " << std::left << std::setw(4) << (i + 1) << std::setw(28)
                << (s.label + (back ? " (return)" : "")).substr(0, 27) << std::right << std::setw(8)
                << net.nodes[s.node].id << std::setw(14) << std::fixed << std::setprecision(3)
                << (i == 0 ? 0.0 : leg_cost[i - 1]) << std::setw(14) << cum << "\n";
    }
    std::cout << std::defaultfloat << "\n";
  } else {
    std::cout << "Order of visit: " << k << " stops (use -verbose to list them; see " << opt.output_file
              << ")\n\n";
  }

  std::cout << "Total cost: " << num(total_cost) << " " << cunit << "\n";
  std::cout << "Total length: " << num(total_length) << " " << lunit << "\n";
  std::cout << "Total time: " << num(total_time) << " min\n";
  if (!sol.exact && sol.initial_cost > 0) {
    const double final_cost = tourCost(M, sol.tour);
    std::cout << "Nearest-neighbour tour: " << num(sol.initial_cost) << " " << cunit
              << " (improved " << std::fixed << std::setprecision(1)
              << (100.0 * (sol.initial_cost - final_cost) / sol.initial_cost) << "%)\n"
              << std::defaultfloat;
  }
  std::cout << "Elapsed: " << std::fixed << std::setprecision(3) << elapsed << " s\n" << std::defaultfloat;
  std::cout << "\nTour written to: " << opt.output_file << "\n";

  std::cout << "\n========================================\n";
  std::cout << "  TOUR COMPLETE\n";
  std::cout << "========================================\n";
  return 0;
}
