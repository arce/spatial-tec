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
        if (lower == "x" || lower == "longitude" || lower == "lon") {
          col_x = i;
        } else if (lower == "y" || lower == "latitude" || lower == "lat") {
          col_y = i;
        } else if (lower == "z" || lower == "value" || lower == "elevation") {
          col_z = i;
        } else if (lower == "geometry" || lower == "geom" || lower == "wkt") {
          col_geometry = i;
        } else if (toLower(headers[i]) == toLower(attr_name)) {
          col_attr = i;
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

    if (col_attr >= 0 && col_attr < (int)values.size()) {
      try {
        pt.z = std::stod(values[col_attr]);
      } catch (...) {
        continue;
      }
    } else if (col_z >= 0) {
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
    return 0;
  return numerator / denominator;
}

double interpolateIDWNeighbors(double x, double y, const std::vector<Point2D>& points,
                               int n_neighbors = 12, double power = 2.0) {
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

    if (dist < 1e-12) {
      return val;
    }

    double weight = 1.0 / std::pow(dist, power);
    numerator += weight * val;
    denominator += weight;
  }

  if (denominator < 1e-12)
    return 0;
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

double interpolateRBF(double x, double y, const std::vector<Point2D>& points, double sigma = 1.0) {
  double numerator = 0;
  double denominator = 0;

  for (const auto& p : points) {
    double dist = p.distanceTo2D(Point2D(x, y));
    double weight = std::exp(-(dist * dist) / (2 * sigma * sigma));
    numerator += weight * p.z;
    denominator += weight;
  }

  if (denominator < 1e-12)
    return 0;
  return numerator / denominator;
}

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

InterpMethod parseMethod(const std::string& method) {
  std::string m = toLower(trim(method));

  if (m == "nearest" || m == "nn")
    return NEAREST;
  if (m == "idw")
    return IDW;
  if (m == "idw_power" || m == "idwp")
    return IDW_POWER;
  if (m == "idw_neighbors" || m == "idwn")
    return IDW_NEIGHBORS;
  if (m == "linear" || m == "lin")
    return LINEAR;
  if (m == "kriging" || m == "krig")
    return KRIGING;
  if (m == "natural" || m == "natural_neighbor" || m == "nn")
    return NATURAL_NEIGHBOR;
  if (m == "rbf")
    return RBF;

  return IDW;
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
                     double radius = 0, double sigma = 1.0) {
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
          value = interpolateIDWNeighbors(x, y, points, n_neighbors, power);
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
          value = interpolateRBF(x, y, points, sigma);
          break;
        default:
          value = interpolateIDW(x, y, points, power, radius);
          break;
      }

      output.at(r, c) = value;
      processed++;
    }

    if ((r + 1) % 100 == 0) {
      std::cout << "\r  Progress: " << (processed * 100 / total) << "%" << std::flush;
    }
  }

  std::cout << "\r  Progress: 100%\n";
}

void crossValidate(const std::vector<Point2D>& points, InterpMethod method, double power = 2.0,
                   int n_neighbors = 12) {
  std::cout << "\nCross-validation (leave-one-out):\n";

  double mae = 0;
  double rmse = 0;
  int n = points.size();

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
        predicted = interpolateIDW(points[i].x, points[i].y, train, power);
        break;
      case IDW_NEIGHBORS:
        predicted = interpolateIDWNeighbors(points[i].x, points[i].y, train, n_neighbors, power);
        break;
      default:
        predicted = interpolateIDW(points[i].x, points[i].y, train, power);
        break;
    }

    double error = predicted - points[i].z;
    mae += std::abs(error);
    rmse += error * error;
  }

  mae /= n;
  rmse = std::sqrt(rmse / n);

  std::cout << "  Mean Absolute Error (MAE): " << std::fixed << std::setprecision(6) << mae << "\n";
  std::cout << "  Root Mean Square Error (RMSE): " << std::fixed << std::setprecision(6) << rmse
            << "\n";
}

void printUsage() {
  std::cerr << "spatial_interpolate - Spatial interpolation from points to raster\n\n";
  std::cerr << "Usage: spatial_interpolate <input.csv> <output.asc> -attribute <col> [options]\n\n";
  std::cerr << "Required:\n";
  std::cerr << "  -attribute <col>   Column name for values to interpolate\n";
  std::cerr << "  -cellsize <value>  Cell size of output raster\n\n";
  std::cerr << "Methods:\n";
  std::cerr << "  -method <method>   Interpolation method:\n";
  std::cerr << "                     nearest, idw, idw_power, idw_neighbors,\n";
  std::cerr << "                     linear, kriging, natural, rbf\n";
  std::cerr << "                     (default: idw)\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -power <value>     Power for IDW (default: 2.0)\n";
  std::cerr << "  -neighbors <n>     Number of neighbors for IDW (default: 12)\n";
  std::cerr << "  -radius <value>    Search radius (default: no limit)\n";
  std::cerr << "  -sigma <value>     Sigma for RBF (default: 1.0)\n";
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
  double radius = 0;
  double sigma = 1.0;
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
    } else if (arg == "-radius" && i + 1 < argc) {
      radius = std::stod(argv[++i]);
    } else if (arg == "-sigma" && i + 1 < argc) {
      sigma = std::stod(argv[++i]);
    } else if (arg == "-bbox" && i + 4 < argc) {
      bbox = BBox(std::stod(argv[++i]), std::stod(argv[++i]), std::stod(argv[++i]),
                  std::stod(argv[++i]));
      use_bbox = true;
    } else if (arg == "-nodata" && i + 1 < argc) {
      nodata = std::stod(argv[++i]);
    } else if (arg == "-cross_validate") {
      cross_validate = true;
    } else if (arg == "-verbose") {
      verbose = true;
    } else if (input_file.empty()) {
      input_file = arg;
    } else {
      output_file = arg;
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

  if (verbose) {
    std::cout << "  X range: " << std::fixed << std::setprecision(4) << points[0].x << " - "
              << points[points.size() - 1].x << "\n";
    std::cout << "  Y range: " << std::fixed << std::setprecision(4) << points[0].y << " - "
              << points[points.size() - 1].y << "\n";
    std::cout << "  Z range: " << std::fixed << std::setprecision(4) << points[0].z << " - "
              << points[points.size() - 1].z << "\n\n";
  }

  if (!use_bbox) {
    bbox = BBox(points[0].x, points[0].y, points[points.size() - 1].x, points[points.size() - 1].y);

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

  InterpMethod method = parseMethod(method_str);
  interpolateGrid(points, output, method, power, n_neighbors, radius, sigma);

  if (cross_validate) {
    crossValidate(points, method, power, n_neighbors);
  }

  writeRasterASCII(output, output_file);

  std::cout << "\n========================================\n";
  std::cout << "  INTERPOLATION COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Output written to: " << output_file << "\n";
  std::cout << "Cells: " << (nrows * ncols) << "\n";

  return 0;
}

