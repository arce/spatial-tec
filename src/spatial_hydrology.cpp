#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "../include/core/ascii_grid.hpp"
#include "../include/core/spatial_io.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

namespace {

// The eight neighbors, in the order of their D8 codes: E, SE, S, SW, W, NW, N, NE.
const int DR[8] = {0, 1, 1, 1, 0, -1, -1, -1};
const int DC[8] = {1, 1, 0, -1, -1, -1, 0, 1};
const int CODE[8] = {1, 2, 4, 8, 16, 32, 64, 128};
const double SQRT2 = 1.41421356237309504880;
const double EQ_TOL = 1e-9;

void printUsage() {
  std::cerr << "spatial_hydrology - DEM hydrology: fill sinks, flow direction, flow accumulation, watershed\n\n";
  std::cerr << "Usage: spatial_hydrology <input.asc> <output.asc> -operation <fill|flow_direction|flow_accumulation|watershed> [options]\n\n";
  std::cerr << "Operations:\n";
  std::cerr << "  -operation fill                Fill sinks (depressions) in a DEM\n";
  std::cerr << "  -operation flow_direction       D8 flow direction from an already-filled DEM\n";
  std::cerr << "  -operation flow_accumulation    Flow accumulation from a flow direction raster\n";
  std::cerr << "                                  (input.asc is the flow direction raster for this operation)\n";
  std::cerr << "  -operation watershed            Watershed (drainage basin) of one or more pour points\n";
  std::cerr << "                                  (input.asc is the flow direction raster for this operation)\n\n";
  std::cerr << "Options (apply only to the matching operation):\n";
  std::cerr << "  -max_depth <value>   Maximum depth to fill (fill; default: no limit). Depressions deeper\n";
  std::cerr << "                       than this value are left unfilled\n";
  std::cerr << "  -method <d8>         Flow direction algorithm (flow_direction; default: d8)\n";
  std::cerr << "  -weights <file>      Optional weights raster with the same grid as the flow direction raster\n";
  std::cerr << "                       (flow_accumulation; default: each cell weighs 1)\n";
  std::cerr << "  -pour_point <x> <y>  Outlet of a watershed, in map coordinates (watershed; required, can be\n";
  std::cerr << "                       repeated: the basins are numbered 1, 2, 3... in the order given)\n";
  std::cerr << "  -snap_raster <file>  Flow accumulation raster used to move each pour point to the cell with the\n";
  std::cerr << "                       highest accumulation within -snap_distance (watershed)\n";
  std::cerr << "  -snap_distance <d>   Snap search distance in map units (watershed; default: 0 = no snapping)\n";
  std::cerr << "  -verbose             Show detailed information\n\n";
  std::cerr << "Method:\n";
  std::cerr << "  fill              Priority-flood: raises every depression up to its spill elevation.\n";
  std::cerr << "                    Water leaves through the edge of the grid or through NODATA cells.\n";
  std::cerr << "  flow_direction    D8: each cell drains to the neighbor with the steepest descent (drop divided\n";
  std::cerr << "                    by the distance, which is sqrt(2) times longer on the diagonals). Codes:\n";
  std::cerr << "                    E=1 SE=2 S=4 SW=8 W=16 NW=32 N=64 NE=128; 0 = no flow (outlet or sink).\n";
  std::cerr << "                    Flat areas left by -operation fill drain toward their spill point.\n";
  std::cerr << "  flow_accumulation Number of cells (or sum of weights) that drain through each cell,\n";
  std::cerr << "                    INCLUDING the cell itself: the minimum value is 1 (a ridge cell).\n";
  std::cerr << "  watershed         Every cell that drains to a pour point gets the number of that point; the\n";
  std::cerr << "                    rest is NODATA. A cell belongs to the nearest pour point downstream of it.\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_hydrology dem.asc dem_filled.asc -operation fill\n";
  std::cerr << "  spatial_hydrology dem_filled.asc flowdir.asc -operation flow_direction\n";
  std::cerr << "  spatial_hydrology flowdir.asc flowacc.asc -operation flow_accumulation\n";
  std::cerr << "  spatial_hydrology flowdir.asc basin.asc -operation watershed -pour_point 483200 1092500\n";
  std::cerr << "  spatial_hydrology flowdir.asc basin.asc -operation watershed -pour_point 483200 1092500 \\\n";
  std::cerr << "                    -snap_raster flowacc.asc -snap_distance 90\n";
}

bool isNoData(const Spatial::RasterDataset& r, double v) {
  return std::isnan(v) || std::fabs(v - r.nodata_value) < 1e-9;
}

struct Grid {
  int rows, cols;
  std::vector<char> valid;
  Grid(const Spatial::RasterDataset& r)
      : rows(r.nrows), cols(r.ncols), valid(r.data.size(), 0) {
    for (size_t i = 0; i < r.data.size(); ++i)
      valid[i] = isNoData(r, r.data[i]) ? 0 : 1;
  }
  bool inside(int r, int c) const { return r >= 0 && r < rows && c >= 0 && c < cols; }
  size_t idx(int r, int c) const { return static_cast<size_t>(r) * cols + c; }
  bool isValid(int r, int c) const { return inside(r, c) && valid[idx(r, c)]; }
  // A cell is on the edge when water can leave the DEM through one of its neighbors.
  bool onEdge(int r, int c) const {
    for (int k = 0; k < 8; ++k)
      if (!isValid(r + DR[k], c + DC[k]))
        return true;
    return false;
  }
};

// ---------------------------------------------------------------- fill

struct FillStats {
  size_t filled_cells = 0;
  double max_depth = 0.0;
  double total_depth = 0.0;
  size_t unfilled_depressions = 0;
};

FillStats fillSinks(const Spatial::RasterDataset& in, const Grid& g, double max_depth,
                    std::vector<double>& filled) {
  const size_t n = in.data.size();
  filled = in.data;
  std::vector<char> closed(n, 0);
  typedef std::pair<double, size_t> Item;
  std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;

  for (int r = 0; r < g.rows; ++r)
    for (int c = 0; c < g.cols; ++c)
      if (g.isValid(r, c) && g.onEdge(r, c)) {
        size_t i = g.idx(r, c);
        closed[i] = 1;
        pq.push(Item(filled[i], i));
      }

  while (!pq.empty()) {
    Item top = pq.top();
    pq.pop();
    int r = static_cast<int>(top.second / g.cols);
    int c = static_cast<int>(top.second % g.cols);
    for (int k = 0; k < 8; ++k) {
      int rr = r + DR[k], cc = c + DC[k];
      if (!g.isValid(rr, cc))
        continue;
      size_t j = g.idx(rr, cc);
      if (closed[j])
        continue;
      closed[j] = 1;
      if (filled[j] < top.first)
        filled[j] = top.first;
      pq.push(Item(filled[j], j));
    }
  }

  FillStats st;
  if (max_depth >= 0.0) {
    // A depression is a group of connected cells that were raised.
    std::vector<char> seen(n, 0);
    for (size_t s = 0; s < n; ++s) {
      if (seen[s] || !g.valid[s] || filled[s] - in.data[s] <= EQ_TOL)
        continue;
      std::vector<size_t> comp;
      std::vector<size_t> stack(1, s);
      seen[s] = 1;
      double deepest = 0.0;
      while (!stack.empty()) {
        size_t i = stack.back();
        stack.pop_back();
        comp.push_back(i);
        deepest = std::max(deepest, filled[i] - in.data[i]);
        int r = static_cast<int>(i / g.cols), c = static_cast<int>(i % g.cols);
        for (int k = 0; k < 8; ++k) {
          int rr = r + DR[k], cc = c + DC[k];
          if (!g.isValid(rr, cc))
            continue;
          size_t j = g.idx(rr, cc);
          if (!seen[j] && filled[j] - in.data[j] > EQ_TOL) {
            seen[j] = 1;
            stack.push_back(j);
          }
        }
      }
      if (deepest > max_depth + EQ_TOL) {
        for (size_t i : comp)
          filled[i] = in.data[i];
        ++st.unfilled_depressions;
      }
    }
  }

  for (size_t i = 0; i < n; ++i) {
    if (!g.valid[i])
      continue;
    double d = filled[i] - in.data[i];
    if (d > EQ_TOL) {
      ++st.filled_cells;
      st.total_depth += d;
      st.max_depth = std::max(st.max_depth, d);
    }
  }
  return st;
}

// ---------------------------------------------------------------- flow direction

struct DirStats {
  size_t with_direction = 0;
  size_t flats_resolved = 0;
  size_t outlets = 0;
  size_t sinks = 0;
};

// dir: -1 = NODATA, 0 = no flow, otherwise the D8 code.
DirStats flowDirection(const Spatial::RasterDataset& dem, const Grid& g, std::vector<int>& dir) {
  const size_t n = dem.data.size();
  dir.assign(n, -1);
  DirStats st;

  for (int r = 0; r < g.rows; ++r) {
    for (int c = 0; c < g.cols; ++c) {
      if (!g.isValid(r, c))
        continue;
      size_t i = g.idx(r, c);
      double z = dem.data[i];
      int best = 0;
      double best_drop = 0.0;
      for (int k = 0; k < 8; ++k) {
        int rr = r + DR[k], cc = c + DC[k];
        if (!g.isValid(rr, cc))
          continue;
        double dist = (DR[k] != 0 && DC[k] != 0) ? SQRT2 : 1.0;
        double drop = (z - dem.data[g.idx(rr, cc)]) / dist;
        if (drop > best_drop) {
          best_drop = drop;
          best = CODE[k];
        }
      }
      dir[i] = best;
    }
  }

  // Flat areas (as left by "fill") have no lower neighbor: route them to the nearest
  // cell that already drains, or to an outlet on the edge, moving outward breadth-first.
  std::vector<char> done(n, 0);
  std::queue<size_t> q;
  for (int r = 0; r < g.rows; ++r) {
    for (int c = 0; c < g.cols; ++c) {
      if (!g.isValid(r, c))
        continue;
      size_t i = g.idx(r, c);
      if (dir[i] > 0 || g.onEdge(r, c)) {
        done[i] = 1;
        q.push(i);
      }
    }
  }
  while (!q.empty()) {
    size_t p = q.front();
    q.pop();
    int r = static_cast<int>(p / g.cols), c = static_cast<int>(p % g.cols);
    for (int k = 0; k < 8; ++k) {
      int rr = r + DR[k], cc = c + DC[k];
      if (!g.isValid(rr, cc))
        continue;
      size_t j = g.idx(rr, cc);
      if (done[j] || dir[j] != 0 || std::fabs(dem.data[j] - dem.data[p]) > EQ_TOL)
        continue;
      dir[j] = CODE[(k + 4) % 8];  // from j back to p
      done[j] = 1;
      ++st.flats_resolved;
      q.push(j);
    }
  }

  for (int r = 0; r < g.rows; ++r) {
    for (int c = 0; c < g.cols; ++c) {
      if (!g.isValid(r, c))
        continue;
      int d = dir[g.idx(r, c)];
      if (d > 0)
        ++st.with_direction;
      else if (g.onEdge(r, c))
        ++st.outlets;
      else
        ++st.sinks;
    }
  }
  return st;
}

// ---------------------------------------------------------------- flow accumulation

int codeToIndex(double v) {
  long code = std::lround(v);
  if (std::fabs(v - static_cast<double>(code)) > 1e-9)
    return -2;
  for (int k = 0; k < 8; ++k)
    if (CODE[k] == code)
      return k;
  return code == 0 ? -1 : -2;
}

// Cell of the grid that contains the map point (x, y); false when it falls outside.
bool worldToCell(const Spatial::RasterDataset& r, double x, double y, int& row, int& col) {
  double fc = std::floor((x - r.xllcorner) / r.cellsize);
  double fr = std::floor((y - r.yllcorner) / r.cellsize);
  if (!(fc >= 0 && fc < r.ncols && fr >= 0 && fr < r.nrows))
    return false;
  col = static_cast<int>(fc);
  row = r.nrows - 1 - static_cast<int>(fr);
  return true;
}

// Downstream cell of every cell, or -1 when the water leaves the grid / has no direction.
// Returns false (with a message) when the raster holds a value that is not a D8 code.
bool buildDownstream(const Spatial::RasterDataset& dir, const Grid& g, std::vector<long>& down,
                     std::string& error) {
  down.assign(dir.data.size(), -1);
  for (int r = 0; r < g.rows; ++r) {
    for (int c = 0; c < g.cols; ++c) {
      if (!g.isValid(r, c))
        continue;
      size_t i = g.idx(r, c);
      int k = codeToIndex(dir.data[i]);
      if (k == -2) {
        error = "Invalid flow direction value " + std::to_string(dir.data[i]) + " at row " +
                std::to_string(r) + ", column " + std::to_string(c) +
                " (expected 0, 1, 2, 4, 8, 16, 32, 64 or 128)";
        return false;
      }
      if (k < 0)
        continue;
      int rr = r + DR[k], cc = c + DC[k];
      if (g.isValid(rr, cc))
        down[i] = static_cast<long>(g.idx(rr, cc));
    }
  }
  return true;
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printUsage();
    return 1;
  }

  std::string input_file = argv[1];
  std::string output_file = argv[2];
  std::string operation;
  double max_depth = -1.0;
  std::string method = "d8";
  std::string weights_file;
  bool verbose = false;
  std::vector<std::pair<double, double>> pour_points;
  std::string snap_file;
  double snap_distance = 0.0;

  try {
    for (int i = 3; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "-operation" && i + 1 < argc) {
        operation = argv[++i];
      } else if (arg == "-max_depth" && i + 1 < argc) {
        max_depth = std::stod(argv[++i]);
      } else if (arg == "-method" && i + 1 < argc) {
        method = argv[++i];
      } else if (arg == "-weights" && i + 1 < argc) {
        weights_file = argv[++i];
      } else if (arg == "-pour_point" && i + 2 < argc) {
        double px = std::stod(argv[++i]);
        double py = std::stod(argv[++i]);
        pour_points.emplace_back(px, py);
      } else if (arg == "-snap_raster" && i + 1 < argc) {
        snap_file = argv[++i];
      } else if (arg == "-snap_distance" && i + 1 < argc) {
        snap_distance = std::stod(argv[++i]);
      } else if (arg == "-verbose") {
        verbose = true;
      } else if (arg == "-help" || arg == "-h") {
        printUsage();
        return 0;
      } else {
        std::cerr << "Error: Unknown option or missing value: " << arg << "\n";
        printUsage();
        return 1;
      }
    }
  } catch (const std::exception&) {
    std::cerr << "Error: Invalid numeric value in options\n";
    return 1;
  }

  if (operation != "fill" && operation != "flow_direction" && operation != "flow_accumulation" &&
      operation != "watershed") {
    std::cerr << "Error: -operation is required and must be one of: fill, flow_direction, flow_accumulation, watershed\n";
    printUsage();
    return 1;
  }
  method = toLower(trim(method));
  if (method != "d8") {
    std::cerr << "Error: -method must be d8\n";
    return 1;
  }
  if (!std::isfinite(max_depth)) {
    std::cerr << "Error: -max_depth must be a finite number\n";
    return 1;
  }

  if (operation == "watershed") {
    if (pour_points.empty()) {
      std::cerr << "Error: -pour_point <x> <y> is required for -operation watershed\n";
      return 1;
    }
    if (!(snap_distance >= 0.0) || !std::isfinite(snap_distance)) {
      std::cerr << "Error: -snap_distance must be a number >= 0\n";
      return 1;
    }
    if (snap_distance > 0.0 && snap_file.empty()) {
      std::cerr << "Error: -snap_distance needs -snap_raster (a flow accumulation raster)\n";
      return 1;
    }
  }

  Spatial::RasterDataset input;
  Spatial::ASCIIGridReader reader;
  if (!reader.read(input_file, input)) {
    std::cerr << "Error: Could not read input file: " << input_file << "\n";
    return 1;
  }
  if (input.ncols <= 0 || input.nrows <= 0 ||
      input.data.size() != static_cast<size_t>(input.ncols) * static_cast<size_t>(input.nrows)) {
    std::cerr << "Error: Input raster is empty or its data does not match ncols x nrows\n";
    return 1;
  }
  {
    std::ofstream probe(output_file);
    if (!probe.is_open()) {
      std::cerr << "Error: Could not create output file: " << output_file << "\n";
      return 1;
    }
  }

  Grid grid(input);
  size_t valid_cells = 0;
  for (char v : grid.valid)
    valid_cells += v ? 1 : 0;
  if (valid_cells == 0) {
    std::cerr << "Error: The input raster has no valid cells\n";
    return 1;
  }

  std::cout << "========================================\n";
  std::cout << "  HYDROLOGY: " << operation << "\n";
  std::cout << "========================================\n\n";
  std::cout << "Input: " << input_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Dimensions: " << input.ncols << "x" << input.nrows << "\n";
  std::cout << "Valid cells: " << valid_cells << "\n";

  Spatial::RasterDataset output;
  output.ncols = input.ncols;
  output.nrows = input.nrows;
  output.xllcorner = input.xllcorner;
  output.yllcorner = input.yllcorner;
  output.cellsize = input.cellsize;
  output.nodata_value = input.nodata_value;
  output.crs = input.crs;
  output.data.assign(input.data.size(), input.nodata_value);

  int precision = 0;

  if (operation == "fill") {
    std::vector<double> filled;
    FillStats st = fillSinks(input, grid, max_depth, filled);
    output.data = filled;
    precision = 6;
    std::cout << "Cells raised: " << st.filled_cells << "\n";
    std::cout << "Maximum fill depth: " << st.max_depth << "\n";
    if (max_depth >= 0.0) {
      std::cout << "-max_depth: " << max_depth << "\n";
      std::cout << "Depressions left unfilled: " << st.unfilled_depressions << "\n";
    }
    if (verbose)
      std::cout << "Total fill depth (sum over cells): " << st.total_depth << "\n";
  } else if (operation == "flow_direction") {
    std::vector<int> dir;
    DirStats st = flowDirection(input, grid, dir);
    for (size_t i = 0; i < dir.size(); ++i)
      if (dir[i] >= 0)
        output.data[i] = dir[i];
    std::cout << "Method: " << method << "\n";
    std::cout << "Cells with a flow direction: " << st.with_direction << "\n";
    std::cout << "Outlets on the edge (code 0): " << st.outlets << "\n";
    if (verbose)
      std::cout << "Flat cells routed to their spill point: " << st.flats_resolved << "\n";
    if (st.sinks > 0) {
      std::cerr << "\nWarning: " << st.sinks
                << " interior cells have no lower neighbor (sinks; code 0). Run -operation fill\n"
                << "on the DEM first so that every cell drains to the edge.\n";
    }
  } else if (operation == "watershed") {
    const size_t n = input.data.size();
    std::vector<long> down;
    std::string error;
    if (!buildDownstream(input, grid, down, error)) {
      std::cerr << "Error: " << error << "\n";
      return 1;
    }

    Spatial::RasterDataset snap;
    if (!snap_file.empty()) {
      if (!reader.read(snap_file, snap)) {
        std::cerr << "Error: Could not read snap raster: " << snap_file << "\n";
        return 1;
      }
      if (snap.ncols != input.ncols || snap.nrows != input.nrows || snap.data.size() != n) {
        std::cerr << "Error: The snap raster must have the same dimensions as the flow direction raster\n";
        return 1;
      }
    }

    std::vector<int> label(n, 0);
    std::queue<size_t> q;
    for (size_t p = 0; p < pour_points.size(); ++p) {
      int row = 0, col = 0;
      if (!worldToCell(input, pour_points[p].first, pour_points[p].second, row, col)) {
        std::cerr << "Error: Pour point " << p + 1 << " (" << pour_points[p].first << ", "
                  << pour_points[p].second << ") is outside the raster\n";
        return 1;
      }
      int row0 = row, col0 = col;
      if (snap_distance > 0.0) {
        double best = -std::numeric_limits<double>::infinity(), best_dist = 0.0;
        int reach = static_cast<int>(std::ceil(snap_distance / input.cellsize));
        for (int dr = -reach; dr <= reach; ++dr) {
          for (int dc = -reach; dc <= reach; ++dc) {
            int rr = row0 + dr, cc = col0 + dc;
            double dist = std::sqrt(static_cast<double>(dr * dr + dc * dc)) * input.cellsize;
            if (dist > snap_distance + 1e-9 || !grid.isValid(rr, cc))
              continue;
            double v = snap.data[grid.idx(rr, cc)];
            if (isNoData(snap, v))
              continue;
            if (v > best || (v == best && dist < best_dist)) {
              best = v;
              best_dist = dist;
              row = rr;
              col = cc;
            }
          }
        }
      }
      if (!grid.isValid(row, col)) {
        std::cerr << "Error: Pour point " << p + 1 << " falls on a NODATA cell of the flow direction raster\n";
        return 1;
      }
      size_t seed = grid.idx(row, col);
      if (label[seed] != 0) {
        std::cerr << "Error: Pour points " << label[seed] << " and " << p + 1
                  << " fall on the same cell (row " << row << ", column " << col << ")\n";
        return 1;
      }
      label[seed] = static_cast<int>(p + 1);
      q.push(seed);
      std::cout << "Pour point " << p + 1 << ": row " << row << ", column " << col;
      if (row != row0 || col != col0)
        std::cout << " (snapped from row " << row0 << ", column " << col0 << ")";
      std::cout << "\n";
    }

    // Walk upstream: a neighbor belongs to the basin when it drains into the cell.
    while (!q.empty()) {
      size_t cur = q.front();
      q.pop();
      int r = static_cast<int>(cur / grid.cols), c = static_cast<int>(cur % grid.cols);
      for (int k = 0; k < 8; ++k) {
        int rr = r + DR[k], cc = c + DC[k];
        if (!grid.isValid(rr, cc))
          continue;
        size_t j = grid.idx(rr, cc);
        if (label[j] == 0 && down[j] == static_cast<long>(cur)) {
          label[j] = label[cur];
          q.push(j);
        }
      }
    }

    std::vector<size_t> count(pour_points.size() + 1, 0);
    for (size_t i = 0; i < n; ++i) {
      if (label[i] > 0) {
        output.data[i] = label[i];
        ++count[label[i]];
      }
    }
    double cell_area = input.cellsize * input.cellsize;
    for (size_t p = 1; p < count.size(); ++p)
      std::cout << "Watershed " << p << ": " << count[p] << " cells (area " << count[p] * cell_area << ")\n";
  } else {
    const size_t n = input.data.size();
    std::vector<double> weight(n, 1.0);
    if (!weights_file.empty()) {
      Spatial::RasterDataset wr;
      if (!reader.read(weights_file, wr)) {
        std::cerr << "Error: Could not read weights file: " << weights_file << "\n";
        return 1;
      }
      if (wr.ncols != input.ncols || wr.nrows != input.nrows || wr.data.size() != n) {
        std::cerr << "Error: The weights raster must have the same dimensions as the flow direction raster\n";
        return 1;
      }
      size_t nodata_weights = 0;
      for (size_t i = 0; i < n; ++i) {
        if (isNoData(wr, wr.data[i])) {
          weight[i] = 0.0;
          ++nodata_weights;
        } else {
          weight[i] = wr.data[i];
        }
      }
      precision = 6;
      std::cout << "Weights: " << weights_file << "\n";
      if (nodata_weights > 0 && verbose)
        std::cout << "NODATA weights counted as 0: " << nodata_weights << "\n";
    }

    std::vector<long> down;
    std::string error;
    if (!buildDownstream(input, grid, down, error)) {
      std::cerr << "Error: " << error << "\n";
      return 1;
    }
    std::vector<int> indeg(n, 0);
    for (size_t i = 0; i < n; ++i)
      if (down[i] >= 0)
        ++indeg[static_cast<size_t>(down[i])];

    std::vector<double> acc(n, 0.0);
    std::queue<size_t> q;
    for (size_t i = 0; i < n; ++i) {
      if (!grid.valid[i])
        continue;
      acc[i] = weight[i];
      if (indeg[i] == 0)
        q.push(i);
    }
    size_t processed = 0;
    while (!q.empty()) {
      size_t i = q.front();
      q.pop();
      ++processed;
      if (down[i] >= 0) {
        size_t j = static_cast<size_t>(down[i]);
        acc[j] += acc[i];
        if (--indeg[j] == 0)
          q.push(j);
      }
    }
    if (processed != valid_cells) {
      std::cerr << "Error: The flow directions contain a cycle (" << (valid_cells - processed)
                << " cells never drain out); this is not a valid flow direction raster\n";
      return 1;
    }

    double vmax = 0.0;
    size_t outlets = 0;
    for (size_t i = 0; i < n; ++i) {
      if (!grid.valid[i])
        continue;
      output.data[i] = acc[i];
      vmax = std::max(vmax, acc[i]);
      if (down[i] < 0)
        ++outlets;
    }
    std::cout << "Outlets (cells where the water leaves): " << outlets << "\n";
    std::cout << "Maximum accumulation: " << vmax << "\n";
  }

  writeRasterASCII(output, output_file, precision);
  std::cout << "\nDone. Wrote " << output_file << "\n";
  return 0;
}
