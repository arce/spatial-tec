#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "../include/core/ascii_grid.hpp"
#include "../include/core/spatial_csv.hpp"
#include "../include/core/spatial_geom.hpp"
#include "../include/core/spatial_io.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

struct Point2D {
  double x, y;
  double value;
  Point2D() : x(0), y(0), value(0) {}
  Point2D(double x, double y, double v = 0) : x(x), y(y), value(v) {}
};

void printUsage() {
  std::cerr << "spatial_vectorize - Convert raster to vector data\n\n";
  std::cerr << "Usage: spatial_vectorize <input> <output> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -contour -interval <value>   Extract contours at intervals\n";
  std::cerr << "  -contour -values <list>      Extract contours at specific values\n";
  std::cerr << "  -polygonize                  Convert to polygons\n";
  std::cerr << "  -points                      Convert to points with values\n";
  std::cerr << "  -filter <value>              Only include cells with value\n";
  std::cerr << "  -tolerance <dist>            Polygonize: simplification tolerance in map\n";
  std::cerr << "                               units (default: 1.5 x cell size)\n";
  std::cerr << "  -no_simplify                 Polygonize: keep exact cell edges (stair steps)\n";
  std::cerr << "\nExamples:\n";
  std::cerr << "  spatial_vectorize elevation.asc curves.csv -contour -interval 10\n";
  std::cerr << "  spatial_vectorize classification.asc polygons.csv -polygonize\n";
  std::cerr << "  spatial_vectorize elevation.asc points.csv -points\n";
}

int main(int argc, char* argv[]) {
  if (argc < 4) {
    printUsage();
    return 1;
  }

  std::string input_file;
  std::string output_file;
  std::string mode;
  double contour_interval = 0;
  std::vector<double> contour_values;
  double filter_value = 0;
  bool use_filter = false;
  double tolerance = -1;  // < 0: default (1.5 cells)
  bool no_simplify = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-contour") {
      mode = "contour";
    } else if (arg == "-interval" && i + 1 < argc) {
      contour_interval = std::stod(argv[++i]);
    } else if (arg == "-values" && i + 1 < argc) {
      std::string values_str = argv[++i];
      auto tokens = split(values_str, ',');
      for (const auto& t : tokens) {
        contour_values.push_back(std::stod(trim(t)));
      }
    } else if (arg == "-polygonize") {
      mode = "polygonize";
    } else if (arg == "-points") {
      mode = "points";
    } else if (arg == "-tolerance" && i + 1 < argc) {
      tolerance = std::stod(argv[++i]);
      if (tolerance < 0) {
        std::cerr << "Error: -tolerance must be >= 0\n";
        return 1;
      }
    } else if (arg == "-no_simplify") {
      no_simplify = true;
    } else if (arg == "-filter" && i + 1 < argc) {
      filter_value = std::stod(argv[++i]);
      use_filter = true;
    } else if (arg.size() > 1 && arg[0] == '-') {
      std::cerr << "Error: Unknown option or missing value: " << arg << "\n\n";
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
    std::cerr << "Error: Input and output files required\n";
    return 1;
  }

  if (mode.empty()) {
    std::cerr << "Error: Must specify -contour, -polygonize, or -points\n";
    return 1;
  }

  if (mode == "contour" && contour_interval <= 0 && contour_values.empty()) {
    std::cerr << "Error: -contour requires -interval or -values\n";
    return 1;
  }

  std::cout << "Input: " << input_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Mode: " << mode << "\n";
  if (mode == "contour") {
    if (!contour_values.empty()) {
      std::cout << "Values: ";
      for (double v : contour_values) std::cout << v << " ";
      std::cout << "\n";
    } else {
      std::cout << "Interval: " << contour_interval << "\n";
    }
  }
  if (use_filter) {
    std::cout << "Filter: value=" << filter_value << "\n";
  }
  std::cout << "\n";

  Spatial::ASCIIGridReader reader;
  Spatial::RasterDataset dataset;

  if (!reader.read(input_file, dataset)) {
    std::cerr << "Error: Could not read input file\n";
    return 1;
  }

  std::cout << "Raster dimensions: " << dataset.ncols << "x" << dataset.nrows << "\n";
  std::cout << "Total cells: " << (dataset.ncols * dataset.nrows) << "\n\n";

  Spatial::VectorDataset output;
  output.columns = {"id", "value", "geometry"};
  output.geometry_column = "geometry";
  output.crs = dataset.crs;

  int feature_id = 0;

  if (mode == "points") {
    std::cout << "Converting to points...\n";
    int processed = 0;

    for (int r = 0; r < dataset.nrows; ++r) {
      for (int c = 0; c < dataset.ncols; ++c) {
        double val = dataset.at(r, c);
        if (std::abs(val - dataset.nodata_value) < 1e-9)
          continue;
        if (use_filter && std::abs(val - filter_value) > 1e-9)
          continue;

        double x = dataset.xllcorner + c * dataset.cellsize + dataset.cellsize / 2.0;
        double y =
            dataset.yllcorner + (dataset.nrows - 1 - r) * dataset.cellsize + dataset.cellsize / 2.0;

        Spatial::VectorFeature feature;
        feature.type = Spatial::VectorFeature::GeometryType::POINT;
        feature.coordinates = {x, y};
        feature.attributes["id"] = std::to_string(++feature_id);
        feature.attributes["value"] = std::to_string(val);
        output.features.push_back(feature);
        processed++;
      }
    }

    std::cout << "Points extracted: " << processed << "\n";

  } else if (mode == "polygonize") {
    std::cout << "Polygonizing...\n";
    std::cout << "Merging contiguous cells that share the same value into single polygons.\n\n";

    output.columns.push_back("cell_count");

    int nrows = dataset.nrows;
    int ncols = dataset.ncols;

    auto isActive = [&](int r, int c) -> bool {
      if (r < 0 || r >= nrows || c < 0 || c >= ncols) return false;
      double v = dataset.at(r, c);
      if (std::abs(v - dataset.nodata_value) < 1e-9) return false;
      if (use_filter && std::abs(v - filter_value) > 1e-9) return false;
      return true;
    };

    auto sameValue = [](double a, double b) { return std::abs(a - b) < 1e-9; };

    // --- Step 1: connected-component labeling (4-connectivity, equal value) ---
    std::vector<int> region_id(static_cast<size_t>(nrows) * static_cast<size_t>(ncols), -1);
    std::vector<double> region_value;
    std::vector<long long> region_cell_count;

    const int dr[4] = {-1, 1, 0, 0};
    const int dc[4] = {0, 0, -1, 1};

    for (int r = 0; r < nrows; ++r) {
      for (int c = 0; c < ncols; ++c) {
        size_t idx = static_cast<size_t>(r) * ncols + c;
        if (region_id[idx] != -1 || !isActive(r, c)) continue;

        double val = dataset.at(r, c);
        int rid = static_cast<int>(region_value.size());
        region_value.push_back(val);
        region_cell_count.push_back(0);

        std::vector<std::pair<int, int>> stack;
        stack.push_back({r, c});
        region_id[idx] = rid;

        while (!stack.empty()) {
          auto cell = stack.back();
          stack.pop_back();
          region_cell_count[rid]++;

          for (int k = 0; k < 4; ++k) {
            int nr = cell.first + dr[k];
            int nc = cell.second + dc[k];
            if (!isActive(nr, nc)) continue;
            size_t nidx = static_cast<size_t>(nr) * ncols + nc;
            if (region_id[nidx] != -1) continue;
            if (!sameValue(dataset.at(nr, nc), val)) continue;
            region_id[nidx] = rid;
            stack.push_back({nr, nc});
          }
        }
      }
    }

    int num_regions = static_cast<int>(region_value.size());
    std::cout << "Connected regions found: " << num_regions << "\n";

    // --- Step 2: boundary tracing for each region ---
    // Grid corner nodes: node(r, c) for r in [0, nrows], c in [0, ncols].
    auto nodeX = [&](int c) { return dataset.xllcorner + c * dataset.cellsize; };
    auto nodeY = [&](int r) { return dataset.yllcorner + (nrows - r) * dataset.cellsize; };
    int node_width = ncols + 1;
    auto nodeId = [&](int r, int c) -> long long {
      return static_cast<long long>(r) * node_width + c;
    };

    auto inRegion = [&](int r, int c, int rid) -> bool {
      if (r < 0 || r >= nrows || c < 0 || c >= ncols) return false;
      return region_id[static_cast<size_t>(r) * ncols + c] == rid;
    };

    // --- Step 1b: topology-preserving simplification of region boundaries ---
    // Every boundary between two regions (or a region and the outside) is
    // split into arcs at junction nodes (where three or more regions meet).
    // Each arc is simplified once with Douglas-Peucker and the surviving
    // nodes are marked in `kept`; both neighboring polygons then use the same
    // vertices, so they still share their boundary exactly (no gaps/overlaps).
    // Simplified arcs that would cross or touch another arc are restored to
    // their exact shape, so thin regions never collapse or overlap.
    const long long n_nodes = static_cast<long long>(nrows + 1) * node_width;
    std::vector<char> kept(static_cast<size_t>(n_nodes), 0);

    double tol = no_simplify ? 0.0 : (tolerance >= 0 ? tolerance : 1.5 * dataset.cellsize);
    {
      using Node = std::pair<int, int>;  // (row, col) of a grid corner
      auto R = [&](int r, int c) -> int {
        if (r < 0 || r >= nrows || c < 0 || c >= ncols) return -1;
        return region_id[static_cast<size_t>(r) * ncols + c];
      };
      // Horizontal edge (r, c): node(r,c)-node(r,c+1), r in [0,nrows], c in [0,ncols)
      // Vertical edge   (r, c): node(r,c)-node(r+1,c), r in [0,nrows), c in [0,ncols]
      auto hB = [&](int r, int c) { return R(r - 1, c) != R(r, c); };
      auto vB = [&](int r, int c) { return R(r, c - 1) != R(r, c); };
      std::vector<char> hUsed(static_cast<size_t>(nrows + 1) * ncols, 0);
      std::vector<char> vUsed(static_cast<size_t>(nrows) * (ncols + 1), 0);

      struct Edge { bool horiz; int r, c; };
      auto incident = [&](int r, int c) {
        std::vector<Edge> out;
        if (c > 0 && hB(r, c - 1)) out.push_back({true, r, c - 1});
        if (c < ncols && hB(r, c)) out.push_back({true, r, c});
        if (r > 0 && vB(r - 1, c)) out.push_back({false, r - 1, c});
        if (r < nrows && vB(r, c)) out.push_back({false, r, c});
        return out;
      };
      auto used = [&](const Edge& e) -> char& {
        return e.horiz ? hUsed[static_cast<size_t>(e.r) * ncols + e.c]
                       : vUsed[static_cast<size_t>(e.r) * (ncols + 1) + e.c];
      };
      auto otherEnd = [&](const Edge& e, int r, int c) -> Node {
        if (e.horiz) return (e.c == c) ? Node(r, c + 1) : Node(r, c - 1);
        return (e.r == r) ? Node(r + 1, c) : Node(r - 1, c);
      };
      // The four raster corners are fixed so the map extent is never cut
      auto isCorner = [&](int r, int c) { return (r == 0 || r == nrows) && (c == 0 || c == ncols); };
      auto isJunction = [&](int r, int c) { return isCorner(r, c) || incident(r, c).size() != 2; };

      auto walk = [&](int r, int c, Edge e, Node stop) {
        std::vector<Node> path = {{r, c}};
        while (true) {
          used(e) = 1;
          Node nx = otherEnd(e, r, c);
          r = nx.first; c = nx.second;
          path.push_back(nx);
          if (nx == stop || isJunction(r, c)) break;
          bool found = false;
          for (const auto& ne : incident(r, c)) {
            if (!used(ne)) { e = ne; found = true; break; }
          }
          if (!found) break;
        }
        return path;
      };

      // 1. Collect arcs (junction-to-junction; closed loops are split in two)
      std::vector<std::vector<Node>> arcs;
      for (int r = 0; r <= nrows; ++r)
        for (int c = 0; c <= ncols; ++c) {
          auto inc = incident(r, c);
          if (inc.empty() || (inc.size() == 2 && !isCorner(r, c))) continue;
          for (const auto& e : inc)
            if (!used(e)) arcs.push_back(walk(r, c, e, {-1, -1}));
        }
      for (int r = 0; r <= nrows; ++r)
        for (int c = 0; c <= ncols; ++c)
          for (const auto& e : incident(r, c)) {
            if (used(e)) continue;
            auto path = walk(r, c, e, {r, c});
            size_t far = 0;
            double best = -1;
            for (size_t k = 0; k < path.size(); ++k) {
              double d = std::hypot(path[k].first - r, path[k].second - c);
              if (d > best) { best = d; far = k; }
            }
            arcs.emplace_back(path.begin(), path.begin() + far + 1);
            arcs.emplace_back(path.begin() + far, path.end());
          }

      // 2. Douglas-Peucker per arc (distances in cells, tolerance in map units)
      double tol_cells = tol / dataset.cellsize;
      auto simplifyArc = [&](const std::vector<Node>& path, double t) {
        std::vector<size_t> out;
        if (path.size() < 2) {
          for (size_t k = 0; k < path.size(); ++k) out.push_back(k);
          return out;
        }
        std::vector<char> keep(path.size(), 0);
        keep[0] = 1;
        keep[path.size() - 1] = 1;
        std::vector<std::pair<size_t, size_t>> stack = {{0, path.size() - 1}};
        while (!stack.empty()) {
          auto [a, b] = stack.back();
          stack.pop_back();
          if (b <= a + 1) continue;
          double ax = path[a].second, ay = path[a].first;
          double dx = path[b].second - ax, dy = path[b].first - ay;
          double len = std::sqrt(dx * dx + dy * dy);
          double best = -1;
          size_t bi = a;
          for (size_t k = a + 1; k < b; ++k) {
            double px = path[k].second - ax, py = path[k].first - ay;
            double d = (len > 0) ? std::abs(px * dy - py * dx) / len : std::sqrt(px * px + py * py);
            if (d > best) { best = d; bi = k; }
          }
          if (best > t + 1e-12) {
            keep[bi] = 1;
            stack.push_back({a, bi});
            stack.push_back({bi, b});
          }
        }
        for (size_t k = 0; k < path.size(); ++k)
          if (keep[k]) out.push_back(k);
        return out;
      };

      // Per-arc tolerance; halved for arcs in conflict until they become exact
      std::vector<double> arc_tol(arcs.size(), tol > 0 ? tol_cells : 0.0);
      std::vector<char> exact(arcs.size(), tol <= 0 ? 1 : 0);
      std::vector<std::vector<size_t>> sidx(arcs.size());  // kept indices into arcs[a]
      std::vector<std::vector<Node>> simp(arcs.size());
      auto resimplify = [&](size_t a) {
        sidx[a] = simplifyArc(arcs[a], arc_tol[a]);
        simp[a].clear();
        for (size_t k : sidx[a]) simp[a].push_back(arcs[a][k]);
      };
      for (size_t a = 0; a < arcs.size(); ++a) resimplify(a);

      // 3. Restore arcs whose simplified segments cross/touch other arcs.
      //    Node coordinates are integers, so the tests are exact.
      auto orient = [](const Node& p, const Node& q, const Node& r) {
        long long v = static_cast<long long>(q.second - p.second) * (r.first - p.first) -
                      static_cast<long long>(q.first - p.first) * (r.second - p.second);
        return (v > 0) - (v < 0);
      };
      auto onSeg = [](const Node& p, const Node& q, const Node& r) {  // r on segment pq (collinear)
        return std::min(p.first, q.first) <= r.first && r.first <= std::max(p.first, q.first) &&
               std::min(p.second, q.second) <= r.second && r.second <= std::max(p.second, q.second);
      };
      // Do segments ab and cd share any point other than a common endpoint?
      auto conflict = [&](const Node& a, const Node& b, const Node& c, const Node& d) {
        int shared = (a == c) + (a == d) + (b == c) + (b == d);
        if (shared >= 2) return true;  // identical segments
        int o1 = orient(a, b, c), o2 = orient(a, b, d), o3 = orient(c, d, a), o4 = orient(c, d, b);
        if (shared == 1) {
          // Only a problem if they overlap collinearly beyond the shared point
          if (o1 != 0 || o2 != 0) return false;
          Node s = (a == c || a == d) ? a : b;
          Node p = (s == a) ? b : a, q = (s == c) ? d : c;
          long long dot = static_cast<long long>(p.first - s.first) * (q.first - s.first) +
                          static_cast<long long>(p.second - s.second) * (q.second - s.second);
          return dot > 0;
        }
        if (o1 != o2 && o3 != o4) return true;
        if (o1 == 0 && onSeg(a, b, c)) return true;
        if (o2 == 0 && onSeg(a, b, d)) return true;
        if (o3 == 0 && onSeg(c, d, a)) return true;
        if (o4 == 0 && onSeg(c, d, b)) return true;
        return false;
      };

      if (tol > 0) {
        const int bucket = 16;
        int bw = ncols / bucket + 1, bh = nrows / bucket + 1;
        for (int iter = 0; iter < 200; ++iter) {
          std::vector<std::vector<std::pair<int, int>>> grid(static_cast<size_t>(bw) * bh);
          for (size_t a = 0; a < simp.size(); ++a)
            for (size_t k = 0; k + 1 < simp[a].size(); ++k) {
              const Node &p = simp[a][k], &q = simp[a][k + 1];
              int r0 = std::min(p.first, q.first) / bucket, r1 = std::max(p.first, q.first) / bucket;
              int c0 = std::min(p.second, q.second) / bucket, c1 = std::max(p.second, q.second) / bucket;
              for (int br = r0; br <= r1; ++br)
                for (int bc = c0; bc <= c1; ++bc)
                  grid[static_cast<size_t>(br) * bw + bc].push_back({static_cast<int>(a), static_cast<int>(k)});
            }
          std::vector<char> bad(simp.size(), 0);
          for (const auto& cell : grid)
            for (size_t i1 = 0; i1 < cell.size(); ++i1)
              for (size_t i2 = i1 + 1; i2 < cell.size(); ++i2) {
                int a1 = cell[i1].first, k1 = cell[i1].second;
                int a2 = cell[i2].first, k2 = cell[i2].second;
                if (exact[a1] && exact[a2]) continue;
                if (a1 == a2 && std::abs(k1 - k2) == 1) continue;  // consecutive
                if (conflict(simp[a1][k1], simp[a1][k1 + 1], simp[a2][k2], simp[a2][k2 + 1])) {
                  if (!exact[a1]) bad[a1] = 1;
                  if (!exact[a2]) bad[a2] = 1;
                }
              }
          // Swept area: the region between an original sub-path and its
          // replacing chord must not contain any other kept vertex (otherwise
          // an island or a neighbor would end up on the wrong side).
          std::vector<std::vector<std::pair<int, int>>> pgrid(static_cast<size_t>(bw) * bh);
          for (size_t a = 0; a < simp.size(); ++a)
            for (size_t k = 0; k < simp[a].size(); ++k)
              pgrid[static_cast<size_t>(simp[a][k].first / bucket) * bw + simp[a][k].second / bucket]
                  .push_back({static_cast<int>(a), static_cast<int>(k)});
          for (size_t a = 0; a < simp.size(); ++a) {
            if (exact[a] || bad[a]) continue;
            for (size_t k = 0; k + 1 < sidx[a].size() && !bad[a]; ++k) {
              size_t i0 = sidx[a][k], i1 = sidx[a][k + 1];
              if (i1 <= i0 + 1) continue;
              int rmin = nrows, rmax = 0, cmin = ncols, cmax = 0;
              for (size_t q = i0; q <= i1; ++q) {
                rmin = std::min(rmin, arcs[a][q].first); rmax = std::max(rmax, arcs[a][q].first);
                cmin = std::min(cmin, arcs[a][q].second); cmax = std::max(cmax, arcs[a][q].second);
              }
              for (int br = rmin / bucket; br <= rmax / bucket && !bad[a]; ++br)
                for (int bc = cmin / bucket; bc <= cmax / bucket && !bad[a]; ++bc)
                  for (const auto& pt : pgrid[static_cast<size_t>(br) * bw + bc]) {
                    const Node& P = simp[pt.first][pt.second];
                    if (P == arcs[a][i0] || P == arcs[a][i1]) continue;
                    if (P.first < rmin || P.first > rmax || P.second < cmin || P.second > cmax) continue;
                    // ray casting over the closed polygon arcs[a][i0..i1] + chord
                    bool inside = false;
                    double py = P.first + 0.5e-3, px = P.second + 0.5e-3;  // avoid vertex hits
                    for (size_t q = i0; q <= i1; ++q) {
                      const Node& A = arcs[a][q];
                      const Node& B = (q == i1) ? arcs[a][i0] : arcs[a][q + 1];
                      if ((A.first > py) != (B.first > py)) {
                        double xint = A.second + (py - A.first) * (B.second - A.second) /
                                                     static_cast<double>(B.first - A.first);
                        if (px < xint) inside = !inside;
                      }
                    }
                    if (inside) {
                      bad[a] = 1;
                      break;
                    }
                  }
            }
          }

          int restored = 0;
          for (size_t a = 0; a < simp.size(); ++a)
            if (bad[a]) {
              arc_tol[a] /= 2;
              if (arc_tol[a] < 0.5) {  // below half a cell: keep the exact shape
                arc_tol[a] = 0;
                exact[a] = 1;
              }
              resimplify(a);
              restored++;
            }
          if (restored == 0) break;
        }
      }

      for (const auto& sarc : simp)
        for (const auto& n : sarc) kept[nodeId(n.first, n.second)] = 1;
    }
    if (tol > 0)
      std::cout << "Boundary simplification tolerance: " << tol << "\n";
    else
      std::cout << "Boundary simplification: off (exact cell edges)\n";

    int processed = 0;

    for (int rid = 0; rid < num_regions; ++rid) {
      std::unordered_map<long long, std::vector<long long>> edges_from;

      for (int r = 0; r < nrows; ++r) {
        for (int c = 0; c < ncols; ++c) {
          if (!inRegion(r, c, rid)) continue;

          if (!inRegion(r - 1, c, rid))  // top of cell is a boundary
            edges_from[nodeId(r, c + 1)].push_back(nodeId(r, c));
          if (!inRegion(r + 1, c, rid))  // bottom of cell is a boundary
            edges_from[nodeId(r + 1, c)].push_back(nodeId(r + 1, c + 1));
          if (!inRegion(r, c - 1, rid))  // left of cell is a boundary
            edges_from[nodeId(r, c)].push_back(nodeId(r + 1, c));
          if (!inRegion(r, c + 1, rid))  // right of cell is a boundary
            edges_from[nodeId(r + 1, c + 1)].push_back(nodeId(r, c + 1));
        }
      }

      // Consume the directed boundary edges into one or more closed rings.
      // (A region can trace to more than one ring at a "pinch point" where
      // it touches itself only diagonally -- each ring is still emitted.)
      std::unordered_map<long long, size_t> cursor;
      std::vector<std::vector<long long>> rings;
      long long max_steps = static_cast<long long>(nrows) * ncols * 4 + 16;

      for (auto& entry : edges_from) {
        long long start = entry.first;
        while (cursor[start] < entry.second.size()) {
          std::vector<long long> ring;
          long long current = start;
          ring.push_back(current);
          bool closed = false;

          for (long long step = 0; step < max_steps; ++step) {
            auto it = edges_from.find(current);
            if (it == edges_from.end()) break;
            size_t& used = cursor[current];
            if (used >= it->second.size()) break;
            long long next = it->second[used++];
            ring.push_back(next);
            current = next;
            if (current == start) {
              closed = true;
              break;
            }
          }

          if (closed && ring.size() >= 5) {
            rings.push_back(std::move(ring));
          }
        }
      }

      if (rings.empty()) continue;

      std::vector<std::vector<double>> ring_coords;
      ring_coords.reserve(rings.size());
      for (auto& ring : rings) {
        // Keep only the nodes that survived simplification (ring is closed:
        // last node == first node)
        std::vector<long long> simp;
        for (size_t k = 0; k + 1 < ring.size(); ++k)
          if (kept[static_cast<size_t>(ring[k])]) simp.push_back(ring[k]);
        if (simp.size() >= 3) {
          simp.push_back(simp.front());
          ring = simp;
        }
        std::vector<double> coords;
        coords.reserve(ring.size() * 2);
        for (long long node : ring) {
          int nr = static_cast<int>(node / node_width);
          int nc = static_cast<int>(node % node_width);
          coords.push_back(nodeX(nc));
          coords.push_back(nodeY(nr));
        }
        ring_coords.push_back(std::move(coords));
      }
      // Largest ring first (treated as the outer boundary of the region).
      std::sort(ring_coords.begin(), ring_coords.end(),
                [](const std::vector<double>& a, const std::vector<double>& b) {
                  return polygonArea(a) > polygonArea(b);
                });

      Spatial::VectorFeature feature;
      feature.attributes["id"] = std::to_string(++feature_id);
      feature.attributes["value"] = std::to_string(region_value[rid]);
      feature.attributes["cell_count"] = std::to_string(region_cell_count[rid]);

      if (ring_coords.size() == 1) {
        feature.type = Spatial::VectorFeature::GeometryType::POLYGON;
        feature.coordinates = ring_coords[0];
      } else {
        feature.type = Spatial::VectorFeature::GeometryType::MULTIPOLYGON;
        for (const auto& coords : ring_coords) {
          feature.part_starts.push_back(feature.coordinates.size() / 2);
          feature.coordinates.insert(feature.coordinates.end(), coords.begin(), coords.end());
        }
      }

      output.features.push_back(feature);
      processed++;
    }

    std::cout << "Polygons created: " << processed << "\n";

  } else if (mode == "contour") {
    // Marching squares over the grid of cell centers. Each contour segment
    // joins two crossing points located on cell-center edges; segments that
    // share an edge are chained into continuous LINESTRINGs.
    std::vector<double> values;
    if (!contour_values.empty()) {
      values = contour_values;
    } else {
      double start = std::ceil(dataset.min_val / contour_interval) * contour_interval;
      for (int k = 0;; ++k) {
        double v = start + k * contour_interval;
        if (v > dataset.max_val + 1e-9) break;
        values.push_back(v);
      }
    }

    std::cout << "Contour levels: ";
    for (double v : values) std::cout << v << " ";
    std::cout << "\n";

    const int nrows = dataset.nrows;
    const int ncols = dataset.ncols;
    const long long vbase = static_cast<long long>(nrows) * ncols;
    auto hEdge = [&](int r, int c) { return static_cast<long long>(r) * ncols + c; };
    auto vEdge = [&](int r, int c) { return vbase + static_cast<long long>(r) * ncols + c; };
    auto cx = [&](double c) { return dataset.xllcorner + (c + 0.5) * dataset.cellsize; };
    auto cy = [&](double r) { return dataset.yllcorner + (nrows - 1 - r + 0.5) * dataset.cellsize; };
    auto isNodata = [&](double v) {
      return std::isnan(v) || std::abs(v - dataset.nodata_value) < 1e-9;
    };

    int lines_created = 0;

    for (double level : values) {
      // Avoid corner values exactly on the level (degenerate crossings)
      double eps = std::max(1e-9, std::abs(level) * 1e-12);
      auto adj = [&](double v) { return (v == level) ? level + eps : v; };

      std::unordered_map<long long, std::pair<double, double>> edge_pt;
      std::vector<std::pair<long long, long long>> segs;

      // Crossing point on the edge between nodes (r1,c1) and (r2,c2)
      auto crossing = [&](long long id, int r1, int c1, double v1, int r2, int c2, double v2) {
        if (edge_pt.count(id)) return;
        double t = (level - v1) / (v2 - v1);
        edge_pt[id] = {cx(c1 + t * (c2 - c1)), cy(r1 + t * (r2 - r1))};
      };

      for (int r = 0; r < nrows - 1; ++r) {
        for (int c = 0; c < ncols - 1; ++c) {
          double tl = dataset.at(r, c), tr = dataset.at(r, c + 1);
          double br = dataset.at(r + 1, c + 1), bl = dataset.at(r + 1, c);
          if (isNodata(tl) || isNodata(tr) || isNodata(br) || isNodata(bl)) continue;
          tl = adj(tl); tr = adj(tr); br = adj(br); bl = adj(bl);

          int idx = (tl > level ? 8 : 0) | (tr > level ? 4 : 0) | (br > level ? 2 : 0) |
                    (bl > level ? 1 : 0);
          if (idx == 0 || idx == 15) continue;

          long long top = hEdge(r, c), bottom = hEdge(r + 1, c);
          long long left = vEdge(r, c), right = vEdge(r, c + 1);
          bool center_in = (tl + tr + br + bl) / 4.0 > level;

          std::vector<std::pair<long long, long long>> cs;
          switch (idx) {
            case 1: case 14: cs = {{left, bottom}}; break;
            case 2: case 13: cs = {{bottom, right}}; break;
            case 3: case 12: cs = {{left, right}}; break;
            case 4: case 11: cs = {{top, right}}; break;
            case 6: case 9:  cs = {{top, bottom}}; break;
            case 7: case 8:  cs = {{left, top}}; break;
            case 5:  // TR and BL above (saddle)
              if (center_in) cs = {{left, top}, {bottom, right}};
              else cs = {{left, bottom}, {top, right}};
              break;
            case 10:  // TL and BR above (saddle)
              if (center_in) cs = {{top, right}, {left, bottom}};
              else cs = {{left, top}, {bottom, right}};
              break;
          }

          for (auto& sg : cs) {
            for (long long e : {sg.first, sg.second}) {
              if (e == top) crossing(e, r, c, tl, r, c + 1, tr);
              else if (e == bottom) crossing(e, r + 1, c, bl, r + 1, c + 1, br);
              else if (e == left) crossing(e, r, c, tl, r + 1, c, bl);
              else crossing(e, r, c + 1, tr, r + 1, c + 1, br);
            }
            segs.push_back(sg);
          }
        }
      }

      // Chain segments that share an edge crossing into polylines
      std::unordered_map<long long, std::vector<size_t>> by_edge;
      for (size_t k = 0; k < segs.size(); ++k) {
        by_edge[segs[k].first].push_back(k);
        by_edge[segs[k].second].push_back(k);
      }
      std::vector<bool> used(segs.size(), false);

      auto walk = [&](size_t start_seg, long long start_edge) {
        std::vector<long long> chain = {start_edge};
        size_t cur = start_seg;
        long long e = start_edge;
        while (true) {
          used[cur] = true;
          long long next = (segs[cur].first == e) ? segs[cur].second : segs[cur].first;
          chain.push_back(next);
          e = next;
          size_t nxt = SIZE_MAX;
          for (size_t k : by_edge[e])
            if (!used[k]) { nxt = k; break; }
          if (nxt == SIZE_MAX) break;
          cur = nxt;
        }
        return chain;
      };

      auto emit = [&](const std::vector<long long>& chain) {
        if (chain.size() < 2) return;
        Spatial::VectorFeature feature;
        feature.type = Spatial::VectorFeature::GeometryType::LINESTRING;
        for (long long e : chain) {
          feature.coordinates.push_back(edge_pt[e].first);
          feature.coordinates.push_back(edge_pt[e].second);
        }
        std::ostringstream lv;
        lv << level;
        feature.attributes["id"] = std::to_string(++feature_id);
        feature.attributes["value"] = lv.str();
        output.features.push_back(feature);
        lines_created++;
      };

      // Open lines first (they start at an edge used by a single segment)...
      for (auto& kv : by_edge) {
        if (kv.second.size() == 1 && !used[kv.second[0]]) emit(walk(kv.second[0], kv.first));
      }
      // ...then closed rings
      for (size_t k = 0; k < segs.size(); ++k) {
        if (!used[k]) emit(walk(k, segs[k].first));
      }
    }

    std::cout << "Contour lines created: " << lines_created << "\n";
  }

  output.feature_count = output.features.size();

  writeVectorCSV(
      output, output_file, {},
      {"# Vectorized from raster", "# Total features: " + std::to_string(output.features.size())});

  std::cout << "\n========================================\n";
  std::cout << "  VECTORIZATION COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Total features: " << output.features.size() << "\n";
  std::cout << "Output written to: " << output_file << "\n";

  return 0;
}

