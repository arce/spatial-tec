#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <vector>

#include "../include/core/spatial_csv.hpp"
#include "../include/core/spatial_geom.hpp"
#include "../include/core/spatial_io.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

struct Point2D {
  double x, y;
  Point2D() : x(0), y(0) {}
  Point2D(double x, double y) : x(x), y(y) {}

  double distanceTo(const Point2D& other) const {
    double dx = x - other.x;
    double dy = y - other.y;
    return std::sqrt(dx * dx + dy * dy);
  }
};

double perpendicularDistance(const Point2D& p, const Point2D& p1, const Point2D& p2) {
  double dx = p2.x - p1.x;
  double dy = p2.y - p1.y;
  double len2 = dx * dx + dy * dy;

  if (len2 < 1e-12) {
    return p.distanceTo(p1);
  }

  double t = ((p.x - p1.x) * dx + (p.y - p1.y) * dy) / len2;
  t = std::max(0.0, std::min(1.0, t));

  double cx = p1.x + t * dx;
  double cy = p1.y + t * dy;

  return p.distanceTo(Point2D(cx, cy));
}

void douglasPeucker(const std::vector<Point2D>& points, int start, int end, double tolerance,
                    std::vector<bool>& keep) {
  if (end <= start + 1)
    return;

  double max_dist = 0;
  int max_idx = start;

  for (int i = start + 1; i < end; ++i) {
    double dist = perpendicularDistance(points[i], points[start], points[end]);
    if (dist > max_dist) {
      max_dist = dist;
      max_idx = i;
    }
  }

  if (max_dist > tolerance) {
    keep[max_idx] = true;
    douglasPeucker(points, start, max_idx, tolerance, keep);
    douglasPeucker(points, max_idx, end, tolerance, keep);
  }
}

std::vector<Point2D> simplifyDouglasPeucker(const std::vector<Point2D>& points, double tolerance) {
  if (points.size() <= 2)
    return points;

  std::vector<bool> keep(points.size(), false);
  keep[0] = true;
  keep[points.size() - 1] = true;

  douglasPeucker(points, 0, points.size() - 1, tolerance, keep);

  std::vector<Point2D> result;
  for (size_t i = 0; i < points.size(); ++i) {
    if (keep[i]) {
      result.push_back(points[i]);
    }
  }

  return result;
}

struct TriangleArea {
  size_t index;
  double area;

  bool operator<(const TriangleArea& other) const {
    return area > other.area;
  }
};

double triangleArea(const Point2D& p1, const Point2D& p2, const Point2D& p3) {
  return std::abs((p2.x - p1.x) * (p3.y - p1.y) - (p3.x - p1.x) * (p2.y - p1.y)) / 2.0;
}

std::vector<Point2D> simplifyVisvalingam(const std::vector<Point2D>& points, double tolerance) {
  if (points.size() <= 2)
    return points;

  return simplifyDouglasPeucker(points, tolerance);
}

std::vector<double> simplifyCoordinates(const std::vector<double>& coords, double tolerance,
                                        const std::string& method = "douglas", double factor = 0) {
  if (coords.size() < 4)
    return coords;

  std::vector<Point2D> points;
  for (size_t i = 0; i < coords.size(); i += 2) {
    points.push_back(Point2D(coords[i], coords[i + 1]));
  }

  std::vector<Point2D> simplified;

  if (method == "douglas" || method == "d") {
    simplified = simplifyDouglasPeucker(points, tolerance);
  } else if (method == "visvalingam" || method == "v") {
    simplified = simplifyVisvalingam(points, tolerance);
  } else {
    simplified = simplifyDouglasPeucker(points, tolerance);
  }

  if (factor > 0 && factor < 1 && simplified.size() > 2) {
    size_t target = static_cast<size_t>(simplified.size() * factor);
    if (target < 2)
      target = 2;

    double low = 0;
    double high = tolerance * 10;
    double best_tol = tolerance;
    std::vector<Point2D> best_result = simplified;

    for (int iter = 0; iter < 20; ++iter) {
      double mid = (low + high) / 2;
      auto test = simplifyDouglasPeucker(points, mid);
      if (test.size() <= target) {
        high = mid;
        if (test.size() >= target) {
          best_result = test;
          best_tol = mid;
        }
      } else {
        low = mid;
      }
    }

    simplified = best_result;
  }

  std::vector<double> result;
  for (const auto& p : simplified) {
    result.push_back(p.x);
    result.push_back(p.y);
  }

  return result;
}

void writeSimplifiedCSV(const Spatial::VectorDataset& dataset, const std::string& filename,
                        const std::string& method, double tolerance) {
  std::ostringstream tol_ss;
  tol_ss << std::fixed << std::setprecision(6) << tolerance;
  writeVectorCSV(dataset, filename,
                 {"# Simplified using: " + method, "# Tolerance: " + tol_ss.str()},
                 {"# Total features: " + std::to_string(dataset.features.size())});
}

struct SimplifyStats {
  size_t original_vertices;
  size_t simplified_vertices;
  size_t features_processed;
  size_t features_skipped;
  double reduction_percent;
};

SimplifyStats simplifyDataset(const Spatial::VectorDataset& input, Spatial::VectorDataset& output,
                              double tolerance, const std::string& method, double factor,
                              const std::set<std::string>& types) {
  SimplifyStats stats;
  stats.original_vertices = 0;
  stats.simplified_vertices = 0;
  stats.features_processed = 0;
  stats.features_skipped = 0;

  output.columns = input.columns;
  output.geometry_column = input.geometry_column;
  output.crs = input.crs;

  for (const auto& feature : input.features) {
    std::string type_name;
    switch (feature.type) {
      case Spatial::VectorFeature::GeometryType::POINT:
        type_name = "point";
        break;
      case Spatial::VectorFeature::GeometryType::LINESTRING:
        type_name = "line";
        break;
      case Spatial::VectorFeature::GeometryType::POLYGON:
        type_name = "polygon";
        break;
    }

    if (!types.empty() && types.find(type_name) == types.end()) {
      output.features.push_back(feature);
      stats.features_skipped++;
      continue;
    }

    if (feature.type == Spatial::VectorFeature::GeometryType::POINT) {
      output.features.push_back(feature);
      stats.features_skipped++;
      continue;
    }

    stats.original_vertices += feature.coordinates.size() / 2;

    std::vector<double> simplified_coords;
    if (tolerance > 0) {
      simplified_coords = simplifyCoordinates(feature.coordinates, tolerance, method, factor);
    } else if (factor > 0 && factor < 1) {
      double auto_tolerance = 0.001;
      simplified_coords = simplifyCoordinates(feature.coordinates, auto_tolerance, method, factor);
    } else {
      simplified_coords = feature.coordinates;
    }

    Spatial::VectorFeature simplified = feature;
    simplified.coordinates = simplified_coords;

    stats.simplified_vertices += simplified_coords.size() / 2;
    stats.features_processed++;

    output.features.push_back(simplified);
  }

  stats.reduction_percent =
      (stats.original_vertices > 0)
          ? (1.0 - (double)stats.simplified_vertices / stats.original_vertices) * 100.0
          : 0;

  output.feature_count = output.features.size();

  return stats;
}

void printUsage() {
  std::cerr << "spatial_simplify - Simplify vector geometries\n\n";
  std::cerr << "Usage: spatial_simplify <input> <output> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -tolerance <value>   Simplification tolerance (Douglas-Peucker)\n";
  std::cerr << "  -factor <value>      Reduction factor (0-1, e.g., 0.5 = 50% reduction)\n";
  std::cerr << "  -method <method>     Simplification method: douglas, visvalingam\n";
  std::cerr << "                       (default: douglas)\n";
  std::cerr << "  -types <list>        Types to simplify: point,line,polygon\n";
  std::cerr << "                       (default: line,polygon)\n";
  std::cerr << "  -verbose             Show detailed statistics\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_simplify curves.csv simplified_curves.csv -tolerance 0.001\n";
  std::cerr << "  spatial_simplify polygons.csv simplified_polygons.csv -factor 0.5\n";
  std::cerr << "  spatial_simplify data.csv simplified_data.csv -tolerance 0.01 -verbose\n";
}

int main(int argc, char* argv[]) {
  if (argc < 4) {
    printUsage();
    return 1;
  }

  std::string input_file;
  std::string output_file;
  double tolerance = 0;
  double factor = 0;
  std::string method = "douglas";
  std::set<std::string> types = {"line", "polygon"};
  bool verbose = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-tolerance" && i + 1 < argc) {
      tolerance = std::stod(argv[++i]);
    } else if (arg == "-factor" && i + 1 < argc) {
      factor = std::stod(argv[++i]);
      if (factor < 0 || factor > 1) {
        std::cerr << "Error: Factor must be between 0 and 1\n";
        return 1;
      }
    } else if (arg == "-method" && i + 1 < argc) {
      method = toLower(argv[++i]);
      if (method != "douglas" && method != "d" && method != "visvalingam" && method != "v") {
        std::cerr << "Error: Unknown method: " << method << "\n";
        return 1;
      }
    } else if (arg == "-types" && i + 1 < argc) {
      types.clear();
      std::string types_str = argv[++i];
      auto tokens = split(types_str, ',');
      for (const auto& t : tokens) {
        std::string type = toLower(trim(t));
        if (type == "point" || type == "line" || type == "polygon") {
          types.insert(type);
        }
      }
      if (types.empty()) {
        std::cerr << "Error: No valid types specified\n";
        return 1;
      }
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

  if (tolerance <= 0 && factor <= 0) {
    std::cerr << "Error: Either -tolerance or -factor must be specified\n";
    return 1;
  }

  std::cout << "========================================\n";
  std::cout << "  SPATIAL SIMPLIFY\n";
  std::cout << "========================================\n\n";
  std::cout << "Input: " << input_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Method: " << method << "\n";
  if (tolerance > 0) {
    std::cout << "Tolerance: " << tolerance << "\n";
  }
  if (factor > 0) {
    std::cout << "Factor: " << factor << "\n";
  }
  std::cout << "Types: ";
  for (const auto& t : types) std::cout << t << " ";
  std::cout << "\n";
  std::cout << "\n";

  Spatial::SpatialCSVReader reader;
  Spatial::VectorDataset dataset;

  if (!reader.read(input_file, dataset)) {
    std::cerr << "Error: Could not read input file\n";
    return 1;
  }

  std::cout << "Input features: " << dataset.features.size() << "\n";

  size_t total_vertices = 0;
  for (const auto& f : dataset.features) {
    total_vertices += f.coordinates.size() / 2;
  }
  std::cout << "Input vertices: " << total_vertices << "\n\n";

  Spatial::VectorDataset output;
  SimplifyStats stats = simplifyDataset(dataset, output, tolerance, method, factor, types);

  writeSimplifiedCSV(output, output_file, method, tolerance > 0 ? tolerance : factor);

  std::cout << "========================================\n";
  std::cout << "  SIMPLIFY COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Features processed: " << stats.features_processed << "\n";
  std::cout << "Features skipped: " << stats.features_skipped << "\n";
  std::cout << "Original vertices: " << stats.original_vertices << "\n";
  std::cout << "Simplified vertices: " << stats.simplified_vertices << "\n";
  std::cout << "Reduction: " << std::fixed << std::setprecision(1) << stats.reduction_percent
            << "%\n";
  std::cout << "Output features: " << output.features.size() << "\n";
  std::cout << "Output written to: " << output_file << "\n";

  if (verbose) {
    std::cout << "\nDetailed statistics by type:\n";

    std::map<std::string, size_t> orig_by_type, simp_by_type;
    for (size_t i = 0; i < dataset.features.size(); ++i) {
      const auto& f = dataset.features[i];
      std::string type;
      switch (f.type) {
        case Spatial::VectorFeature::GeometryType::POINT:
          type = "point";
          break;
        case Spatial::VectorFeature::GeometryType::LINESTRING:
          type = "line";
          break;
        case Spatial::VectorFeature::GeometryType::POLYGON:
          type = "polygon";
          break;
      }
      orig_by_type[type] += f.coordinates.size() / 2;
    }
    for (const auto& f : output.features) {
      std::string type;
      switch (f.type) {
        case Spatial::VectorFeature::GeometryType::POINT:
          type = "point";
          break;
        case Spatial::VectorFeature::GeometryType::LINESTRING:
          type = "line";
          break;
        case Spatial::VectorFeature::GeometryType::POLYGON:
          type = "polygon";
          break;
      }
      simp_by_type[type] += f.coordinates.size() / 2;
    }

    for (const auto& [type, count] : orig_by_type) {
      size_t simp_count = simp_by_type[type];
      double reduction = count > 0 ? (1.0 - (double)simp_count / count) * 100.0 : 0;
      std::cout << "  " << type << ": " << count << " -> " << simp_count << " (" << std::fixed
                << std::setprecision(1) << reduction << "%)\n";
    }
  }

  return 0;
}
