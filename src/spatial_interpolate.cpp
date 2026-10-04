#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <vector>

#include "../include/core/ascii_grid.hpp"
#include "../include/core/spatial_csv.hpp"
#include "../include/core/spatial_io.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

struct Point2D {
  double x, y, z;
  Point2D() : x(0), y(0), z(0) {}
  Point2D(double x, double y, double z = 0) : x(x), y(y), z(z) {}

  double distanceTo(const Point2D& other) const {
    double dx = x - other.x;
    double dy = y - other.y;
    return std::sqrt(dx * dx + dy * dy);
  }

  double distanceTo2D(const Point2D& other) const {
    double dx = x - other.x;
    double dy = y - other.y;
    return std::sqrt(dx * dx + dy * dy);
  }
};

struct BBox {
  double minx, miny, maxx, maxy;
  BBox() : minx(0), miny(0), maxx(0), maxy(0) {}
  BBox(double x1, double y1, double x2, double y2)
      : minx(std::min(x1, x2)),
        miny(std::min(y1, y2)),
        maxx(std::max(x1, x2)),
        maxy(std::max(y1, y2)) {}

  bool contains(double x, double y) const {
    return x >= minx && x <= maxx && y >= miny && y <= maxy;
  }
};

std::vector<Point2D> parseWKTPoints(const std::string& wkt) {
  std::vector<Point2D> points;
  std::string str = trim(wkt);

  size_t start = str.find('(');
  if (start == std::string::npos)
    return points;

  size_t end = str.rfind(')');
  if (end == std::string::npos || end <= start)
    return points;

  std::string inner = str.substr(start + 1, end - start - 1);
  auto coord_strings = split(inner, ',');

  for (const auto& cs : coord_strings) {
    auto parts = split(trim(cs), ' ');
    if (parts.size() >= 2) {
      try {
        double x = std::stod(parts[0]);
        double y = std::stod(parts[1]);
        double z = (parts.size() >= 3) ? std::stod(parts[2]) : 0;
        points.push_back(Point2D(x, y, z));
      } catch (...) {
      }
    }
  }

  return points;
}

std::vector<Point2D> readPoints(const std::string& filename, const std::string& attr_name,
                                std::string& crs) {
  std::vector<Point2D> points;
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file: " << filename << "\n";
    return points;
  }

  std::string line;
  std::vector<std::string> headers;
  bool is_header = true;
  int col_x = -1, col_y = -1, col_z = -1;
  int col_geometry = -1;
  int col_attr = -1;

  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') {
      if (line.find("CRS:") != std::string::npos) {
        crs = trim(line.substr(line.find(":") + 1));
      }
      continue;
    }

    auto values = splitCSV(line);

    if (is_header) {
      headers = values;

      for (size_t i = 0; i < headers.size(); ++i) {
        std::string lower = toLower(headers[i]);
        if (!attr_name.empty() && lower == toLower(attr_name)) {
          col_attr = i;
        } else if (lower == "x" || lower == "longitude" || lower == "lon") {
          col_x = i;
        } else if (lower == "y" || lower == "latitude" || lower == "lat") {
          col_y = i;
        } else if (lower == "z" || lower == "value" || lower == "elevation") {
          col_z = i;
        } else if (lower == "geometry" || lower == "geom" || lower == "wkt") {
          col_geometry = i;
        }
      }

      is_header = false;
      continue;
    }

    Point2D pt;

    if (col_geometry >= 0 && col_geometry < (int)values.size()) {
      auto pts = parseWKTPoints(values[col_geometry]);
      if (!pts.empty()) {
        pt.x = pts[0].x;
        pt.y = pts[0].y;
        pt.z = pts[0].z;
      } else {
        continue;
      }
    } else if (col_x >= 0 && col_y >= 0) {
      try {
        pt.x = std::stod(values[col_x]);
        pt.y = std::stod(values[col_y]);
        if (col_z >= 0 && col_z < (int)values.size()) {
          pt.z = std::stod(values[col_z]);
        }
      } catch (...) {
        continue;
      }
    } else {
      continue;
    }

    int col_val = (col_attr >= 0) ? col_attr : col_z;
    if (col_val >= 0 && col_val < (int)values.size()) {
      try {
        pt.z = std::stod(values[col_val]);
      } catch (...) {
        continue;
      }
    } else if (col_geometry >= 0 && col_val < 0) {
      // keep Z from WKT geometry (POINT Z)
    } else {
      std::cerr << "Error: No attribute column found for interpolation\n";
      return points;
    }

    points.push_back(pt);
  }

  return points;
}

double interpolateNearest(double x, double y, const std::vector<Point2D>& points) {
  double min_dist = std::numeric_limits<double>::max();
  double value = 0;

  for (const auto& p : points) {
    double dist = p.distanceTo2D(Point2D(x, y));
    if (dist < min_dist) {
      min_dist = dist;
      value = p.z;
    }
  }

  return value;
}

double interpolateIDW(double x, double y, const std::vector<Point2D>& points, double power = 2.0,
                      double radius = 0) {
  double numerator = 0;
  double denominator = 0;

  for (const auto& p : points) {
    double dist = p.distanceTo2D(Point2D(x, y));

    if (radius > 0 && dist > radius)
      continue;

    if (dist < 1e-12) {
      return p.z;
    }

    double weight = 1.0 / std::pow(dist, power);
    numerator += weight * p.z;
    denominator += weight;
  }

  if (denominator < 1e-12)
    return std::numeric_limits<double>::quiet_NaN();
  return numerator / denominator;
}

double interpolateIDWNeighbors(double x, double y, const std::vector<Point2D>& points,
                               int n_neighbors = 12, double power = 2.0, double radius = 0) {
  std::vector<std::pair<double, double>> distances;

  for (const auto& p : points) {
    double dist = p.distanceTo2D(Point2D(x, y));
    distances.push_back({dist, p.z});
  }

  std::sort(distances.begin(), distances.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

  int n = std::min(n_neighbors, (int)distances.size());

  double numerator = 0;
  double denominator = 0;

  for (int i = 0; i < n; ++i) {
    double dist = distances[i].first;
    double val = distances[i].second;

    if (radius > 0 && dist > radius)
      break;

    if (dist < 1e-12) {
      return val;
    }

    double weight = 1.0 / std::pow(dist, power);
    numerator += weight * val;
    denominator += weight;
  }

  if (denominator < 1e-12)
    return std::numeric_limits<double>::quiet_NaN();
  return numerator / denominator;
}

double interpolateLinear(double x, double y, const std::vector<Point2D>& points) {
  std::vector<std::pair<double, Point2D>> dist_points;
  for (const auto& p : points) {
    double dist = p.distanceTo2D(Point2D(x, y));
    dist_points.push_back({dist, p});
  }

  std::sort(dist_points.begin(), dist_points.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

  if (dist_points.size() < 3) {
    return dist_points[0].second.z;
  }

  Point2D p1 = dist_points[0].second;
  Point2D p2 = dist_points[1].second;
  Point2D p3 = dist_points[2].second;

  double d1 = p1.distanceTo2D(Point2D(x, y));
  double d2 = p2.distanceTo2D(Point2D(x, y));
  double d3 = p3.distanceTo2D(Point2D(x, y));
  double sum_d = d1 + d2 + d3;

  if (sum_d < 1e-12)
    return p1.z;

  return (d1 * p1.z + d2 * p2.z + d3 * p3.z) / sum_d;
}

double interpolateKriging(double x, double y, const std::vector<Point2D>& points) {
  return interpolateIDWNeighbors(x, y, points, 8, 1.5);
}

double interpolateNaturalNeighbor(double x, double y, const std::vector<Point2D>& points) {
  return interpolateIDWNeighbors(x, y, points, 10, 1.0);
}

enum RBFFunction { RBF_GAUSSIAN, RBF_MULTIQUADRIC, RBF_INVERSE_MULTIQUADRIC, RBF_THIN_PLATE, RBF_LINEAR };

bool parseRBFFunction(const std::string& name, RBFFunction& out) {
  std::string f = toLower(trim(name));
  if (f == "gaussian" || f == "gauss") out = RBF_GAUSSIAN;
  else if (f == "multiquadric" || f == "mq") out = RBF_MULTIQUADRIC;
  else if (f == "inverse_multiquadric" || f == "imq") out = RBF_INVERSE_MULTIQUADRIC;
  else if (f == "thin_plate" || f == "thin_plate_spline" || f == "tps") out = RBF_THIN_PLATE;
  else if (f == "linear") out = RBF_LINEAR;
  else return false;
  return true;
}

std::string rbfFunctionToString(RBFFunction f) {
  switch (f) {
    case RBF_GAUSSIAN: return "gaussian";
    case RBF_MULTIQUADRIC: return "multiquadric";
    case RBF_INVERSE_MULTIQUADRIC: return "inverse_multiquadric";
    case RBF_THIN_PLATE: return "thin_plate";
    case RBF_LINEAR: return "linear";
  }
  return "gaussian";
}

bool rbfUsesSigma(RBFFunction f) {
  return f == RBF_GAUSSIAN || f == RBF_MULTIQUADRIC || f == RBF_INVERSE_MULTIQUADRIC;
}

inline double rbfPhi(RBFFunction f, double r, double sigma) {
  switch (f) {
    case RBF_GAUSSIAN: return std::exp(-(r * r) / (2.0 * sigma * sigma));
    case RBF_MULTIQUADRIC: return std::sqrt(r * r + sigma * sigma);
    case RBF_INVERSE_MULTIQUADRIC: return 1.0 / std::sqrt(r * r + sigma * sigma);
    case RBF_THIN_PLATE: return (r > 1e-12) ? r * r * std::log(r) : 0.0;
    case RBF_LINEAR: return r;
  }
  return 0.0;
}

double estimateSigma(const std::vector<Point2D>& points) {
  if (points.size() < 2) return 1.0;
  double sum = 0;
  for (size_t i = 0; i < points.size(); ++i) {
    double best = std::numeric_limits<double>::max();
    for (size_t j = 0; j < points.size(); ++j) {
      if (i == j) continue;
      double d = points[i].distanceTo2D(points[j]);
      if (d > 1e-12) best = std::min(best, d);
    }
    if (best < std::numeric_limits<double>::max()) sum += best;
  }
  double s = sum / points.size();
  return s > 0 ? s : 1.0;
}

bool solveLinearSystem(std::vector<double>& A, std::vector<double>& b, int n) {
  for (int k = 0; k < n; ++k) {
    int piv = k;
    double maxv = std::abs(A[(size_t)k * n + k]);
    for (int i = k + 1; i < n; ++i) {
      double v = std::abs(A[(size_t)i * n + k]);
      if (v > maxv) { maxv = v; piv = i; }
    }
    if (maxv < 1e-12) return false;
    if (piv != k) {
      for (int j = 0; j < n; ++j) std::swap(A[(size_t)k * n + j], A[(size_t)piv * n + j]);
      std::swap(b[k], b[piv]);
    }
    for (int i = k + 1; i < n; ++i) {
      double f = A[(size_t)i * n + k] / A[(size_t)k * n + k];
      if (f == 0) continue;
      for (int j = k; j < n; ++j) A[(size_t)i * n + j] -= f * A[(size_t)k * n + j];
      b[i] -= f * b[k];
    }
  }
  for (int i = n - 1; i >= 0; --i) {
    double sum = b[i];
    for (int j = i + 1; j < n; ++j) sum -= A[(size_t)i * n + j] * b[j];
    b[i] = sum / A[(size_t)i * n + i];
  }
  return true;
}

struct RBFModel {
  RBFFunction func = RBF_GAUSSIAN;
  double sigma = 0;
  bool sigma_set = false;
  bool auto_sigma = false;
  RBFFunction rbf_func = RBF_GAUSSIAN;
  double cx = 0, cy = 0;
  std::vector<Point2D> pts;
  std::vector<double> w;
  double a0 = 0, a1 = 0, a2 = 0;
  bool ok = false;

  bool fit(const std::vector<Point2D>& points, RBFFunction f, double sig) {
    func = f;
    sigma = sig;
    ok = false;
    int n = (int)points.size();
    if (n == 0) return false;
    cx = cy = 0;
    for (const auto& p : points) { cx += p.x; cy += p.y; }
    cx /= n; cy /= n;
    pts.clear();
    for (const auto& p : points) pts.push_back(Point2D(p.x - cx, p.y - cy, p.z));

    for (int np : {3, 1}) {
      if (n < np) continue;
      int m = n + np;
      std::vector<double> A((size_t)m * m, 0.0), b(m, 0.0);
      for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j)
          A[(size_t)i * m + j] = rbfPhi(func, pts[i].distanceTo2D(pts[j]), sigma);
        double poly[3] = {1.0, pts[i].x, pts[i].y};
        for (int k = 0; k < np; ++k) {
          A[(size_t)i * m + n + k] = poly[k];
          A[(size_t)(n + k) * m + i] = poly[k];
        }
        b[i] = pts[i].z;
      }
      if (solveLinearSystem(A, b, m)) {
        w.assign(b.begin(), b.begin() + n);
        a0 = b[n];
        a1 = (np == 3) ? b[n + 1] : 0.0;
        a2 = (np == 3) ? b[n + 2] : 0.0;
        ok = true;
        return true;
      }
    }
    return false;
  }

  double eval(double x, double y) const {
    if (!ok) return std::numeric_limits<double>::quiet_NaN();
    Point2D q(x - cx, y - cy);
    double v = a0 + a1 * q.x + a2 * q.y;
    for (size_t i = 0; i < pts.size(); ++i) v += w[i] * rbfPhi(func, pts[i].distanceTo2D(q), sigma);
    return v;
  }
};

const int RBF_MAX_POINTS = 5000;

enum InterpMethod {
  NEAREST,
  IDW,
  IDW_POWER,
  IDW_NEIGHBORS,
  LINEAR,
  KRIGING,
  NATURAL_NEIGHBOR,
  RBF
};

bool parseMethod(const std::string& method, InterpMethod& out) {
  std::string m = toLower(trim(method));
  out = IDW;

  if (m == "nearest" || m == "nn")
    { out = NEAREST; return true; }
  if (m == "idw")
    { out = IDW; return true; }
  if (m == "idw_power" || m == "idwp")
    { out = IDW_POWER; return true; }
  if (m == "idw_neighbors" || m == "idwn")
    { out = IDW_NEIGHBORS; return true; }
  if (m == "linear" || m == "lin")
    { out = LINEAR; return true; }
  if (m == "kriging" || m == "krig")
    { out = KRIGING; return true; }
  // CORRECCIÓN: Se elimina "nn" de aquí para evitar el conflicto con Nearest Neighbor
  if (m == "natural" || m == "natural_neighbor" || m == "nat")
    { out = NATURAL_NEIGHBOR; return true; }
  if (m == "rbf")
    { out = RBF; return true; }

  return false;
}

std::string methodToString(InterpMethod method) {
  switch (method) {
    case NEAREST:
      return "Nearest Neighbor";
    case IDW:
      return "IDW (Inverse Distance Weighted)";
    case IDW_POWER:
      return "IDW with Power";
    case IDW_NEIGHBORS:
      return "IDW with Neighbor Search";
    case LINEAR:
      return "Linear Interpolation";
    case KRIGING:
      return "Kriging (simplified)";
    case NATURAL_NEIGHBOR:
      return "Natural Neighbor";
    case RBF:
      return "RBF (Radial Basis Function)";
    default:
      return "IDW";
  }
}

void interpolateGrid(const std::vector<Point2D>& points, Spatial::RasterDataset& output,
                     InterpMethod method, double power = 2.0, int n_neighbors = 12,
                     double radius = 0, const RBFModel* rbf = nullptr) {
  int nrows = output.nrows;
  int ncols = output.ncols;
  double cellsize = output.cellsize;
  double xll = output.xllcorner;
  double yll = output.yllcorner;
  double nodata = output.nodata_value;

  std::cout << "Interpolating grid: " << ncols << "x" << nrows << " cells\n";
  std::cout << "Method: " << methodToString(method) << "\n";

  int processed = 0;
  int total = nrows * ncols;

  for (int r = 0; r < nrows; ++r) {
    for (int c = 0; c < ncols; ++c) {
      double x = xll + c * cellsize + cellsize / 2.0;
      double y = yll + (nrows - 1 - r) * cellsize + cellsize / 2.0;

      double value;

      switch (method) {
        case NEAREST:
          value = interpolateNearest(x, y, points);
          break;
        case IDW:
          value = interpolateIDW(x, y, points, power, radius);
          break;
        case IDW_POWER:
          value = interpolateIDW(x, y, points, power, radius);
          break;
        case IDW_NEIGHBORS:
          value = interpolateIDWNeighbors(x, y, points, n_neighbors, power, radius);
          break;
        case LINEAR:
          value = interpolateLinear(x, y, points);
          break;
        case KRIGING:
          value = interpolateKriging(x, y, points);
          break;
        case NATURAL_NEIGHBOR:
          value = interpolateNaturalNeighbor(x, y, points);
          break;
        case RBF:
          value = rbf ? rbf->eval(x, y) : std::numeric_limits<double>::quiet_NaN();
          break;
        default:
          value = interpolateIDW(x, y, points, power, radius);
          break;
      }

      output.at(r, c) = std::isnan(value) ? nodata : value;
      processed++;
    }

    if ((r + 1) % 100 == 0) {
      std::cout << "\r  Progress: " << (processed * 100 / total) << "%" << std::flush;
    }
  }

  std::cout << "\r  Progress: 100%\n";
}

void crossValidate(const std::vector<Point2D>& points, InterpMethod method, double power = 2.0,
                   int n_neighbors = 12, double radius = 0, RBFFunction rbf_func = RBF_GAUSSIAN,
                   double sigma = 1.0) {
  std::cout << "\nCross-validation (leave-one-out):\n";

  double mae = 0;
  double rmse = 0;
  int n = 0;

  for (size_t i = 0; i < points.size(); ++i) {
    std::vector<Point2D> train;
    for (size_t j = 0; j < points.size(); ++j) {
      if (i != j)
        train.push_back(points[j]);
    }

    double predicted;
    switch (method) {
      case NEAREST:
        predicted = interpolateNearest(points[i].x, points[i].y, train);
        break;
      case IDW:
      case IDW_POWER:
        predicted = interpolateIDW(points[i].x, points[i].y, train, power, radius);
        break;
      case IDW_NEIGHBORS:
        predicted =
            interpolateIDWNeighbors(points[i].x, points[i].y, train, n_neighbors, power, radius);
        break;
      case RBF: {
        RBFModel m;
        m.fit(train, rbf_func, sigma);
        predicted = m.eval(points[i].x, points[i].y);
        break;
      }
      default:
        predicted = interpolateIDW(points[i].x, points[i].y, train, power);
        break;
    }

    if (std::isnan(predicted))
      continue;
    n++;
    double error = predicted - points[i].z;
    mae += std::abs(error);
    rmse += error * error;
  }

  if (n == 0) {
    std::cout << "  No points could be predicted (search radius too small)\n";
    return;
  }
  std::cout << "  Points evaluated: " << n << " of " << points.size() << "\n";
  mae /= n;
  rmse = std::sqrt(rmse / n);

  std::cout << "  Mean Absolute Error (MAE): " << std::fixed << std::setprecision(6) << mae << "\n";
  std::cout << "  Root Mean Square Error (RMSE): " << std::fixed << std::setprecision(6) << rmse
            << "\n";
}

void printUsage() {
  std::cerr << "spatial_interpolate - Spatial interpolation from points to raster\n\n";
  std::cerr << "Usage: spatial_interpolate <input.csv> <output.asc> -attribute <col> -cellsize <value> [options]\n\n";
  std::cerr << "Required arguments:\n";
  std::cerr << "  <input.csv>        Input points file (CSV format)\n";
  std::cerr << "  <output.asc>       Output raster file (ASCII Grid format)\n";
  std::cerr << "  -attribute <col>   Column name for values to interpolate\n";
  std::cerr << "  -cellsize <value>  Cell size of output raster (must be > 0)\n\n";
  std::cerr << "Methods:\n";
  std::cerr << "  -method <method>   Interpolation method:\n";
  std::cerr << "                     nearest (nn), idw, idw_power (idwp), idw_neighbors (idwn),\n";
  std::cerr << "                     linear (lin), kriging (krig), natural (nat), rbf\n";
  std::cerr << "                     (default: idw)\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -power <value>     Power for IDW (default: 2.0)\n";
  std::cerr << "  -neighbors <n>     Number of neighbors for IDW (default: 12).\n";
  std::cerr << "                     Note: Combining idw/idw_power with -neighbors automatically\n";
  std::cerr << "                     switches execution to neighbor-restricted IDW.\n";
  std::cerr << "  -radius <value>    Search radius for idw/idw_neighbors (alias: -search_radius)\n";
  std::cerr << "                     Cells with no points in radius get NODATA (default: no limit)\n";
  std::cerr << "  -function <name>   RBF function: gaussian (default), multiquadric,\n";
  std::cerr << "                     inverse_multiquadric, thin_plate, linear\n";
  std::cerr << "  -sigma <value>     Shape parameter for gaussian/multiquadric/\n";
  std::cerr << "                     inverse_multiquadric (default: automatic)\n";
  std::cerr << "  -auto_sigma        Estimate sigma as the mean nearest-neighbor distance\n";
  std::cerr << "  -bbox <minx> <miny> <maxx> <maxy>  Output extent\n";
  std::cerr << "  -nodata <value>    NODATA value (default: -9999)\n";
  std::cerr << "  -cross_validate    Perform leave-one-out cross-validation\n";
  std::cerr << "  -verbose           Show detailed information\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_interpolate points.csv surface.asc -attribute value -method idw "
               "-cellsize 0.01\n";
  std::cerr << "  spatial_interpolate points.csv surface.asc -attribute value -method nearest "
               "-cellsize 0.01\n";
  std::cerr << "  spatial_interpolate points.csv surface.asc -attribute value -method idw_power "
               "-power 3 -cellsize 0.01\n";
  std::cerr << "  spatial_interpolate points.csv surface.asc -attribute value -method "
               "idw_neighbors -neighbors 8 -cellsize 0.01\n";
}

int main(int argc, char* argv[]) {
  if (argc < 5) {
    printUsage();
    return 1;
  }

  std::string input_file;
  std::string output_file;
  std::string attr_name;
  std::string method_str = "idw";
  double cellsize = 0;
  double power = 2.0;
  int n_neighbors = 12;
  bool neighbors_set = false;
  double radius = 0;
  double sigma = 0;
  bool sigma_set = false;
  bool auto_sigma = false;
  RBFFunction rbf_func = RBF_GAUSSIAN;
  BBox bbox;
  bool use_bbox = false;
  double nodata = -9999.0;
  bool cross_validate = false;
  bool verbose = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-attribute" && i + 1 < argc) {
      attr_name = argv[++i];
    } else if (arg == "-cellsize" && i + 1 < argc) {
      cellsize = std::stod(argv[++i]);
    } else if (arg == "-method" && i + 1 < argc) {
      method_str = argv[++i];
    } else if (arg == "-power" && i + 1 < argc) {
      power = std::stod(argv[++i]);
    } else if (arg == "-neighbors" && i + 1 < argc) {
      n_neighbors = std::stoi(argv[++i]);
      neighbors_set = true;
    } else if ((arg == "-radius" || arg == "-search_radius") && i + 1 < argc) {
      radius = std::stod(argv[++i]);
    } else if (arg == "-sigma" && i + 1 < argc) {
      sigma = std::stod(argv[++i]);
      sigma_set = true;
    } else if (arg == "-function" && i + 1 < argc) {
      std::string fname = argv[++i];
      if (!parseRBFFunction(fname, rbf_func)) {
        std::cerr << "Error: Unknown RBF function: " << fname << "\n"
                  << "Valid: gaussian, multiquadric, inverse_multiquadric, thin_plate, linear\n";
        return 1;
      }
    } else if (arg == "-auto_sigma") {
      auto_sigma = true;
    } else if (arg == "-bbox" && i + 4 < argc) {
      double bx1 = std::stod(argv[i + 1]);
      double by1 = std::stod(argv[i + 2]);
      double bx2 = std::stod(argv[i + 3]);
      double by2 = std::stod(argv[i + 4]);
      i += 4;
      bbox = BBox(bx1, by1, bx2, by2);
      use_bbox = true;
    } else if (arg == "-nodata" && i + 1 < argc) {
      nodata = std::stod(argv[++i]);
    } else if (arg == "-cross_validate") {
      cross_validate = true;
    } else if (arg == "-verbose") {
      verbose = true;
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

  if (attr_name.empty()) {
    std::cerr << "Error: -attribute required\n";
    return 1;
  }

  if (cellsize <= 0) {
    std::cerr << "Error: -cellsize must be positive\n";
    return 1;
  }

  std::string crs;
  auto points = readPoints(input_file, attr_name, crs);

  if (points.empty()) {
    std::cerr << "Error: No valid points found in input file\n";
    return 1;
  }

  std::cout << "Points loaded: " << points.size() << "\n";

  double pminx = points[0].x, pmaxx = points[0].x;
  double pminy = points[0].y, pmaxy = points[0].y;
  double pminz = points[0].z, pmaxz = points[0].z;
  for (const auto& p : points) {
    pminx = std::min(pminx, p.x); pmaxx = std::max(pmaxx, p.x);
    pminy = std::min(pminy, p.y); pmaxy = std::max(pmaxy, p.y);
    pminz = std::min(pminz, p.z); pmaxz = std::max(pmaxz, p.z);
  }

  if (verbose) {
    std::cout << "  X range: " << std::fixed << std::setprecision(4) << pminx << " - " << pmaxx
              << "\n";
    std::cout << "  Y range: " << std::fixed << std::setprecision(4) << pminy << " - " << pmaxy
              << "\n";
    std::cout << "  Z range: " << std::fixed << std::setprecision(4) << pminz << " - " << pmaxz
              << "\n\n";
  }

  if (!use_bbox) {
    bbox = BBox(pminx, pminy, pmaxx, pmaxy);

    double margin = cellsize * 10;
    bbox.minx -= margin;
    bbox.miny -= margin;
    bbox.maxx += margin;
    bbox.maxy += margin;
  }

  int ncols = static_cast<int>(std::ceil((bbox.maxx - bbox.minx) / cellsize));
  int nrows = static_cast<int>(std::ceil((bbox.maxy - bbox.miny) / cellsize));

  std::cout << "Output grid: " << ncols << "x" << nrows << " cells\n";
  std::cout << "Cell size: " << cellsize << "\n";
  std::cout << "Extent: " << bbox.minx << " " << bbox.miny << " " << bbox.maxx << " " << bbox.maxy
            << "\n\n";

  Spatial::RasterDataset output;
  output.ncols = ncols;
  output.nrows = nrows;
  output.xllcorner = bbox.minx;
  output.yllcorner = bbox.miny;
  output.cellsize = cellsize;
  output.nodata_value = nodata;
  output.crs = crs;
  output.data.resize(nrows * ncols, nodata);

  InterpMethod method;
  if (!parseMethod(method_str, method)) {
    std::cerr << "Error: Unknown method: " << method_str << "\n"
              << "Valid: nearest, idw, idw_power, idw_neighbors, linear, kriging, natural, rbf\n";
    return 1;
  }

  if (neighbors_set && (method == IDW || method == IDW_POWER)) {
    if (n_neighbors < 1) {
      std::cerr << "Error: -neighbors must be >= 1\n";
      return 1;
    }
    method = (n_neighbors == 1) ? NEAREST : IDW_NEIGHBORS;
  }

  RBFModel rbf_model;
  if (method == RBF) {
    if ((int)points.size() > RBF_MAX_POINTS) {
      std::cerr << "Error: RBF supports at most " << RBF_MAX_POINTS << " points ("
                << points.size() << " given). Use idw or a subsample.\n";
      return 1;
    }
    if (sigma_set && sigma <= 0) {
      std::cerr << "Error: -sigma must be positive\n";
      return 1;
    }
    if (auto_sigma || !sigma_set) {
      sigma = estimateSigma(points);
    }
    std::cout << "RBF function: " << rbfFunctionToString(rbf_func);
    if (rbfUsesSigma(rbf_func))
      std::cout << " (sigma = " << sigma << (sigma_set && !auto_sigma ? "" : ", automatic") << ")";
    std::cout << "\n";
    if (!rbf_model.fit(points, rbf_func, sigma)) {
      std::cerr << "Error: RBF system is singular (duplicate points or sigma too large/small)\n";
      return 1;
    }
  }

  interpolateGrid(points, output, method, power, n_neighbors, radius, &rbf_model);

  if (cross_validate) {
    crossValidate(points, method, power, n_neighbors, radius, rbf_func, sigma);
  }

  writeRasterASCII(output, output_file);

  std::cout << "\n========================================\n";
  std::cout << "  INTERPOLATION COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Output written to: " << output_file << "\n";
  std::cout << "Cells: " << (nrows * ncols) << "\n";

  return 0;
}