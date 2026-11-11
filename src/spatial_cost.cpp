#include <algorithm>
#include <cctype>
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

const double SQRT2 = 1.41421356237309504880;
const double EARTH_RADIUS = 6371000.0;  // meters
const double REFRACTION = 0.13;         // standard atmospheric refraction coefficient
const double LOS_TOL = 1e-9;            // grazing lines of sight count as visible

void printUsage() {
  std::cerr << "spatial_cost - Cost-distance, viewshed and least-cost corridor analysis\n\n";
  std::cerr << "Usage: spatial_cost <input1> [input2] <output> -operation <cost|viewshed|corridor> [options]\n\n";
  std::cerr << "Operations:\n";
  std::cerr << "  -operation cost        1 input (friction raster) -> accumulated cost surface from the sources\n";
  std::cerr << "  -operation viewshed    1 input (DEM) -> visibility raster from one or more observers\n";
  std::cerr << "  -operation corridor    2 inputs (two accumulated cost surfaces) -> least-cost corridor\n\n";
  std::cerr << "Options (apply only to the matching operation):\n";
  std::cerr << "  -source <x> <y>       Origin point in map coordinates (cost; required, can be repeated)\n";
  std::cerr << "  -max_cost <value>     Maximum accumulated cost; farther cells are NODATA (cost; default: no limit)\n";
  std::cerr << "  -observer <x> <y>     Observer point in map coordinates (viewshed; required, can be repeated)\n";
  std::cerr << "  -obs_height <value>   Observer height above the terrain (viewshed; default: 1.7)\n";
  std::cerr << "  -target_height <v>    Height above the terrain of the point that is looked at (viewshed; default: 0)\n";
  std::cerr << "  -radius <value>       Maximum analysis radius in map units (viewshed; default: no limit)\n";
  std::cerr << "  -curvature            Correct for the curvature of the Earth and for refraction (viewshed).\n";
  std::cerr << "                        Assumes that the map units and the elevations are meters\n";
  std::cerr << "  -threshold <value>    Largest total cost kept in the corridor, absolute (corridor;\n";
  std::cerr << "                        default: the minimum total cost plus 10%)\n\n";
  std::cerr << "Method:\n";
  std::cerr << "  cost      Least accumulated cost (Dijkstra) over the 8 neighbors. Moving between two cells costs the\n";
  std::cerr << "            average of their friction values times the distance between their centers (cellsize, or\n";
  std::cerr << "            cellsize*sqrt(2) on the diagonals). Friction must be >= 0; NODATA cells are barriers.\n";
  std::cerr << "            Sources have cost 0. Cells that cannot be reached are NODATA.\n";
  std::cerr << "  corridor  Adds the two accumulated cost surfaces (from point A and from point B). The sum in a cell is\n";
  std::cerr << "            the cost of the cheapest route A -> B that goes through that cell. Cells whose sum is above\n";
  std::cerr << "            the threshold are NODATA. Both inputs must have the same grid.\n";
  std::cerr << "  viewshed  Line of sight from the observer (at observer cell center, elevation + -obs_height) to the\n";
  std::cerr << "            center of every cell (elevation + -target_height). The terrain is sampled with bilinear\n";
  std::cerr << "            interpolation every cellsize along the line. With one observer the result is 1 (visible)\n";
  std::cerr << "            or 0 (hidden); with several observers it is the number of observers that see the cell.\n";
  std::cerr << "            Cells farther than -radius from every observer are NODATA.\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_cost friction.asc cost.asc -operation cost -source 483200 1092500\n";
  std::cerr << "  spatial_cost cost_a.asc cost_b.asc corridor.asc -operation corridor -threshold 1500\n";
  std::cerr << "  spatial_cost dem.asc view.asc -operation viewshed -observer 483200 1092500 -obs_height 1.7 -radius 5000\n";
}

bool isNoData(const Spatial::RasterDataset& r, double v) {
  return std::isnan(v) || std::fabs(v - r.nodata_value) < 1e-9;
}

bool worldToCell(const Spatial::RasterDataset& r, double x, double y, int& row, int& col) {
  double fc = std::floor((x - r.xllcorner) / r.cellsize);
  double fr = std::floor((y - r.yllcorner) / r.cellsize);
  if (!(fc >= 0 && fc < r.ncols && fr >= 0 && fr < r.nrows))
    return false;
  col = static_cast<int>(fc);
  row = r.nrows - 1 - static_cast<int>(fr);
  return true;
}

bool validRaster(const Spatial::RasterDataset& r) {
  return r.ncols > 0 && r.nrows > 0 && r.cellsize > 0 &&
         r.data.size() == static_cast<size_t>(r.ncols) * static_cast<size_t>(r.nrows);
}

bool sameGrid(const Spatial::RasterDataset& a, const Spatial::RasterDataset& b) {
  return a.ncols == b.ncols && a.nrows == b.nrows && std::fabs(a.cellsize - b.cellsize) < 1e-9 * a.cellsize &&
         std::fabs(a.xllcorner - b.xllcorner) < 1e-6 * a.cellsize &&
         std::fabs(a.yllcorner - b.yllcorner) < 1e-6 * a.cellsize;
}

Spatial::RasterDataset emptyLike(const Spatial::RasterDataset& in) {
  Spatial::RasterDataset out;
  out.ncols = in.ncols;
  out.nrows = in.nrows;
  out.xllcorner = in.xllcorner;
  out.yllcorner = in.yllcorner;
  out.cellsize = in.cellsize;
  out.nodata_value = in.nodata_value;
  out.crs = in.crs;
  out.data.assign(in.data.size(), in.nodata_value);
  return out;
}

// Resolves the points given in map coordinates into cells; reports the first one that is not usable.
bool pointsToCells(const Spatial::RasterDataset& r, const std::vector<char>& valid,
                   const std::vector<std::pair<double, double>>& pts, const char* what,
                   std::vector<size_t>& cells) {
  for (size_t p = 0; p < pts.size(); ++p) {
    int row = 0, col = 0;
    if (!worldToCell(r, pts[p].first, pts[p].second, row, col)) {
      std::cerr << "Error: " << what << " " << p + 1 << " (" << pts[p].first << ", " << pts[p].second
                << ") is outside the raster\n";
      return false;
    }
    size_t i = static_cast<size_t>(row) * r.ncols + col;
    if (!valid[i]) {
      std::cerr << "Error: " << what << " " << p + 1 << " (" << pts[p].first << ", " << pts[p].second
                << ") is on a NODATA cell\n";
      return false;
    }
    cells.push_back(i);
  }
  return true;
}

// ---------------------------------------------------------------- cost

struct CostStats {
  size_t reached = 0;
  double max_cost = 0.0;
};

CostStats costDistance(const Spatial::RasterDataset& fr, const std::vector<char>& valid,
                       const std::vector<size_t>& sources, bool has_max, double max_cost,
                       std::vector<double>& cost) {
  static const int DR[8] = {0, 1, 1, 1, 0, -1, -1, -1};
  static const int DC[8] = {1, 1, 0, -1, -1, -1, 0, 1};
  const int rows = fr.nrows, cols = fr.ncols;
  const double INF = std::numeric_limits<double>::infinity();
  cost.assign(fr.data.size(), INF);
  typedef std::pair<double, size_t> Item;
  std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
  for (size_t s : sources) {
    cost[s] = 0.0;
    pq.push(Item(0.0, s));
  }
  while (!pq.empty()) {
    Item it = pq.top();
    pq.pop();
    size_t i = it.second;
    if (it.first > cost[i])
      continue;
    int r = static_cast<int>(i / cols), c = static_cast<int>(i % cols);
    for (int k = 0; k < 8; ++k) {
      int rr = r + DR[k], cc = c + DC[k];
      if (rr < 0 || rr >= rows || cc < 0 || cc >= cols)
        continue;
      size_t j = static_cast<size_t>(rr) * cols + cc;
      if (!valid[j])
        continue;
      double dist = (DR[k] != 0 && DC[k] != 0) ? SQRT2 : 1.0;
      double nc = it.first + 0.5 * (fr.data[i] + fr.data[j]) * dist * fr.cellsize;
      if (has_max && nc > max_cost)
        continue;
      if (nc < cost[j]) {
        cost[j] = nc;
        pq.push(Item(nc, j));
      }
    }
  }
  CostStats st;
  for (size_t i = 0; i < cost.size(); ++i) {
    if (std::isinf(cost[i]))
      continue;
    ++st.reached;
    st.max_cost = std::max(st.max_cost, cost[i]);
  }
  return st;
}

// ---------------------------------------------------------------- viewshed

// Bilinear elevation at fractional (row, col) in cell-center coordinates.
// Returns false when one of the cells that contribute is NODATA.
bool sampleElevation(const Spatial::RasterDataset& dem, const std::vector<char>& valid, double r, double c,
                     double& z) {
  int r0 = static_cast<int>(std::floor(r)), c0 = static_cast<int>(std::floor(c));
  double fr = r - r0, fc = c - c0;
  if (fr < 1e-12) fr = 0.0;
  if (fc < 1e-12) fc = 0.0;
  if (fr > 1.0 - 1e-12) { fr = 0.0; ++r0; }
  if (fc > 1.0 - 1e-12) { fc = 0.0; ++c0; }
  int r1 = fr > 0.0 ? r0 + 1 : r0, c1 = fc > 0.0 ? c0 + 1 : c0;
  if (r0 < 0 || c0 < 0 || r1 >= dem.nrows || c1 >= dem.ncols)
    return false;
  const int cols = dem.ncols;
  size_t i00 = static_cast<size_t>(r0) * cols + c0, i01 = static_cast<size_t>(r0) * cols + c1;
  size_t i10 = static_cast<size_t>(r1) * cols + c0, i11 = static_cast<size_t>(r1) * cols + c1;
  if (!valid[i00] || !valid[i01] || !valid[i10] || !valid[i11])
    return false;
  z = dem.data[i00] * (1 - fr) * (1 - fc) + dem.data[i01] * (1 - fr) * fc +
      dem.data[i10] * fr * (1 - fc) + dem.data[i11] * fr * fc;
  return true;
}

struct ViewParams {
  double obs_height = 1.7;
  double target_height = 0.0;
  bool curvature = false;
};

// Marks (adds 1 to) every cell seen from the observer cell. Cells farther than radius_cells get no mark and
// are flagged in `analyzed`.
size_t viewFrom(const Spatial::RasterDataset& dem, const std::vector<char>& valid, size_t obs,
                const ViewParams& p, bool has_radius, double radius, std::vector<int>& seen,
                std::vector<char>& analyzed) {
  const int rows = dem.nrows, cols = dem.ncols;
  const int orow = static_cast<int>(obs / cols), ocol = static_cast<int>(obs % cols);
  const double cs = dem.cellsize;
  const double zo = dem.data[obs] + p.obs_height;
  const double curv = (1.0 - REFRACTION) / (2.0 * EARTH_RADIUS);  // drop = curv * d^2
  size_t visible = 0;

  int reach_r = rows, reach_c = cols;
  if (has_radius) {
    reach_r = reach_c = static_cast<int>(std::ceil(radius / cs));
  }
  int rmin = std::max(0, orow - reach_r), rmax = std::min(rows - 1, orow + reach_r);
  int cmin = std::max(0, ocol - reach_c), cmax = std::min(cols - 1, ocol + reach_c);

  for (int r = rmin; r <= rmax; ++r) {
    for (int c = cmin; c <= cmax; ++c) {
      size_t t = static_cast<size_t>(r) * cols + c;
      if (!valid[t])
        continue;
      double dr = r - orow, dc = c - ocol;
      double dcells = std::sqrt(dr * dr + dc * dc);
      double d = dcells * cs;
      if (has_radius && d > radius)
        continue;
      analyzed[t] = 1;
      if (t == obs) {
        seen[t] += 1;
        ++visible;
        continue;
      }
      double zt = dem.data[t] + p.target_height;
      if (p.curvature)
        zt -= curv * d * d;
      bool blocked = false;
      for (double k = 1.0; k < dcells - 1e-9; k += 1.0) {
        double f = k / dcells;
        double z;
        if (!sampleElevation(dem, valid, orow + dr * f, ocol + dc * f, z))
          continue;  // NODATA does not block
        double s = k * cs;
        if (p.curvature)
          z -= curv * s * s;
        double line = zo + (zt - zo) * f;
        if (z > line + LOS_TOL) {
          blocked = true;
          break;
        }
      }
      if (!blocked) {
        seen[t] += 1;
        ++visible;
      }
    }
  }
  return visible;
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

  std::vector<std::string> positionals;
  int first_option_idx = argc;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (!arg.empty() && arg[0] == '-' && !(arg.size() > 1 && (isdigit(arg[1]) || arg[1] == '.'))) {
      first_option_idx = i;
      break;
    }
    positionals.push_back(arg);
  }

  if (positionals.size() < 2) {
    std::cerr << "Error: expected at least <input> <output>\n";
    printUsage();
    return 1;
  }

  std::string output_file = positionals.back();
  std::vector<std::string> inputs(positionals.begin(), positionals.end() - 1);

  std::string operation;
  std::vector<std::pair<double, double>> sources, observers;
  bool has_max = false, has_radius = false, has_threshold = false;
  double max_cost = 0.0, radius = 0.0, threshold = 0.0;
  ViewParams vp;

  try {
    for (int i = first_option_idx; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "-operation" && i + 1 < argc) {
        operation = argv[++i];
      } else if (arg == "-source" && i + 2 < argc) {
        double x = std::stod(argv[++i]);
        double y = std::stod(argv[++i]);
        sources.emplace_back(x, y);
      } else if (arg == "-max_cost" && i + 1 < argc) {
        max_cost = std::stod(argv[++i]);
        has_max = true;
      } else if (arg == "-observer" && i + 2 < argc) {
        double x = std::stod(argv[++i]);
        double y = std::stod(argv[++i]);
        observers.emplace_back(x, y);
      } else if (arg == "-obs_height" && i + 1 < argc) {
        vp.obs_height = std::stod(argv[++i]);
      } else if (arg == "-target_height" && i + 1 < argc) {
        vp.target_height = std::stod(argv[++i]);
      } else if (arg == "-radius" && i + 1 < argc) {
        radius = std::stod(argv[++i]);
        has_radius = true;
      } else if (arg == "-curvature") {
        vp.curvature = true;
      } else if (arg == "-threshold" && i + 1 < argc) {
        threshold = std::stod(argv[++i]);
        has_threshold = true;
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

  if (operation != "cost" && operation != "viewshed" && operation != "corridor") {
    std::cerr << "Error: -operation is required and must be one of: cost, viewshed, corridor\n";
    printUsage();
    return 1;
  }
  if (operation == "corridor" && inputs.size() != 2) {
    std::cerr << "Error: -operation corridor requires exactly two input rasters\n";
    printUsage();
    return 1;
  }
  if ((operation == "cost" || operation == "viewshed") && inputs.size() != 1) {
    std::cerr << "Error: -operation " << operation << " requires exactly one input raster\n";
    printUsage();
    return 1;
  }
  if (operation == "cost") {
    if (sources.empty()) {
      std::cerr << "Error: -source <x> <y> is required for -operation cost\n";
      printUsage();
      return 1;
    }
    if (has_max && !(max_cost >= 0.0 && std::isfinite(max_cost))) {
      std::cerr << "Error: -max_cost must be a number >= 0\n";
      return 1;
    }
  }
  if (operation == "viewshed") {
    if (observers.empty()) {
      std::cerr << "Error: -observer <x> <y> is required for -operation viewshed\n";
      printUsage();
      return 1;
    }
    if (!(vp.obs_height >= 0.0 && std::isfinite(vp.obs_height))) {
      std::cerr << "Error: -obs_height must be a number >= 0\n";
      return 1;
    }
    if (!(vp.target_height >= 0.0 && std::isfinite(vp.target_height))) {
      std::cerr << "Error: -target_height must be a number >= 0\n";
      return 1;
    }
    if (has_radius && !(radius > 0.0 && std::isfinite(radius))) {
      std::cerr << "Error: -radius must be a number > 0\n";
      return 1;
    }
  }
  if (operation == "corridor" && has_threshold && !(threshold >= 0.0 && std::isfinite(threshold))) {
    std::cerr << "Error: -threshold must be a number >= 0\n";
    return 1;
  }

  Spatial::ASCIIGridReader reader;
  std::vector<Spatial::RasterDataset> in(inputs.size());
  for (size_t i = 0; i < inputs.size(); ++i) {
    if (!reader.read(inputs[i], in[i])) {
      std::cerr << "Error: Could not read input file: " << inputs[i] << "\n";
      return 1;
    }
    if (!validRaster(in[i])) {
      std::cerr << "Error: Input raster " << inputs[i] << " is empty or its data does not match ncols x nrows\n";
      return 1;
    }
  }
  {
    std::ofstream probe(output_file);
    if (!probe.is_open()) {
      std::cerr << "Error: Could not create output file: " << output_file << "\n";
      return 1;
    }
  }

  std::vector<std::vector<char>> valid(in.size());
  for (size_t k = 0; k < in.size(); ++k) {
    valid[k].assign(in[k].data.size(), 0);
    for (size_t i = 0; i < in[k].data.size(); ++i)
      valid[k][i] = isNoData(in[k], in[k].data[i]) ? 0 : 1;
  }

  std::cout << "========================================\n";
  std::cout << "  COST / VIEWSHED / CORRIDOR: " << operation << "\n";
  std::cout << "========================================\n\n";
  for (size_t i = 0; i < inputs.size(); ++i)
    std::cout << "Input " << i + 1 << ": " << inputs[i] << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Dimensions: " << in[0].ncols << "x" << in[0].nrows << ", cellsize " << in[0].cellsize << "\n";

  Spatial::RasterDataset output = emptyLike(in[0]);
  int precision = 0;

  if (operation == "cost") {
    const Spatial::RasterDataset& fr = in[0];
    for (size_t i = 0; i < fr.data.size(); ++i) {
      if (valid[0][i] && fr.data[i] < 0.0) {
        std::cerr << "Error: Friction values must be >= 0 (negative value " << fr.data[i] << " in row "
                  << i / fr.ncols + 1 << ", column " << i % fr.ncols + 1 << ")\n";
        return 1;
      }
    }
    std::vector<size_t> src;
    if (!pointsToCells(fr, valid[0], sources, "Source", src))
      return 1;
    std::vector<double> cost;
    CostStats st = costDistance(fr, valid[0], src, has_max, max_cost, cost);
    for (size_t i = 0; i < cost.size(); ++i)
      if (!std::isinf(cost[i]))
        output.data[i] = cost[i];
    precision = 4;
    size_t valid_cells = 0;
    for (char v : valid[0])
      valid_cells += v ? 1 : 0;
    std::cout << "Sources: " << src.size() << "\n";
    if (has_max)
      std::cout << "-max_cost: " << max_cost << "\n";
    std::cout << "Cells reached: " << st.reached << " of " << valid_cells << " valid cells\n";
    std::cout << "Maximum accumulated cost: " << st.max_cost << "\n";
    if (st.reached < valid_cells && !has_max)
      std::cout << "Note: " << valid_cells - st.reached
                << " cells cannot be reached from the sources (surrounded by barriers); they are NODATA\n";
  } else if (operation == "corridor") {
    if (!sameGrid(in[0], in[1])) {
      std::cerr << "Error: The two cost rasters must have the same grid (dimensions, origin and cellsize)\n";
      return 1;
    }
    const size_t n = in[0].data.size();
    double minimum = std::numeric_limits<double>::infinity();
    std::vector<double> sum(n, 0.0);
    std::vector<char> ok(n, 0);
    for (size_t i = 0; i < n; ++i) {
      if (!valid[0][i] || !valid[1][i])
        continue;
      sum[i] = in[0].data[i] + in[1].data[i];
      ok[i] = 1;
      minimum = std::min(minimum, sum[i]);
    }
    if (std::isinf(minimum)) {
      std::cerr << "Error: There are no cells with a valid value in both cost rasters\n";
      return 1;
    }
    double limit = has_threshold ? threshold : minimum * 1.1;
    double tol = 1e-9 * std::max(1.0, std::fabs(limit));
    size_t kept = 0, total = 0;
    for (size_t i = 0; i < n; ++i) {
      if (!ok[i])
        continue;
      ++total;
      if (sum[i] <= limit + tol) {
        output.data[i] = sum[i];
        ++kept;
      }
    }
    precision = 4;
    std::cout << "Minimum total cost (least-cost route A -> B): " << minimum << "\n";
    std::cout << "Threshold: " << limit << (has_threshold ? " (-threshold)" : " (minimum + 10%)") << "\n";
    std::cout << "Cells in the corridor: " << kept << " of " << total << "\n";
    if (kept == 0)
      std::cerr << "Warning: the threshold is below the minimum total cost; the corridor is empty\n";
  } else {  // viewshed
    const Spatial::RasterDataset& dem = in[0];
    std::vector<size_t> obs;
    if (!pointsToCells(dem, valid[0], observers, "Observer", obs))
      return 1;
    std::vector<int> seen(dem.data.size(), 0);
    std::vector<char> analyzed(dem.data.size(), 0);
    std::cout << "Observers: " << obs.size() << "  height " << vp.obs_height << ", target height "
              << vp.target_height << "\n";
    if (has_radius)
      std::cout << "-radius: " << radius << "\n";
    if (vp.curvature)
      std::cout << "Curvature and refraction correction: on (R = 6371000 m, k = " << REFRACTION << ")\n";
    for (size_t o = 0; o < obs.size(); ++o) {
      size_t vis = viewFrom(dem, valid[0], obs[o], vp, has_radius, radius, seen, analyzed);
      std::cout << "Observer " << o + 1 << " sees " << vis << " cells\n";
    }
    size_t cells_analyzed = 0, cells_seen = 0;
    for (size_t i = 0; i < seen.size(); ++i) {
      if (!analyzed[i])
        continue;
      ++cells_analyzed;
      output.data[i] = seen[i];
      if (seen[i] > 0)
        ++cells_seen;
    }
    double area = cells_seen * dem.cellsize * dem.cellsize;
    std::cout << "Cells analyzed: " << cells_analyzed << "\n";
    std::cout << "Cells visible from at least one observer: " << cells_seen << " ("
              << (cells_analyzed ? 100.0 * cells_seen / cells_analyzed : 0.0) << "%), area " << area
              << " map units squared\n";
  }

  writeRasterASCII(output, output_file, precision);
  std::cout << "\nDone. Wrote " << output_file << "\n";
  return 0;
}
