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

double pointToPolygonDistance(double px, double py, const std::vector<double>& polygon) {
  if (polygon.size() < 4)
    return std::numeric_limits<double>::max();

  bool inside = false;
  int n = polygon.size() / 2;

  for (int i = 0, j = n - 1; i < n; j = i++) {
    double xi = polygon[i * 2];
    double yi = polygon[i * 2 + 1];
    double xj = polygon[j * 2];
    double yj = polygon[j * 2 + 1];

    if (((yi > py) != (yj > py)) && (px < (xj - xi) * (py - yi) / (yj - yi) + xi)) {
      inside = !inside;
    }
  }

  if (inside)
    return 0;

  double min_dist = std::numeric_limits<double>::max();

  for (int i = 0; i < n; ++i) {
    int j = (i + 1) % n;
    double xi = polygon[i * 2];
    double yi = polygon[i * 2 + 1];
    double xj = polygon[j * 2];
    double yj = polygon[j * 2 + 1];

    double dist = pointToSegmentDistance(px, py, xi, yi, xj, yj);
    min_dist = std::min(min_dist, dist);
  }

  return min_dist;
}

double featureDistance(const Spatial::VectorFeature& f1, const Spatial::VectorFeature& f2) {
  if (f1.type == Spatial::VectorFeature::GeometryType::POINT &&
      f2.type == Spatial::VectorFeature::GeometryType::POINT) {
    return pointToPointDistance(f1.coordinates[0], f1.coordinates[1], f2.coordinates[0],
                                f2.coordinates[1]);
  }

  std::vector<Point2D> points1, points2;

  for (size_t i = 0; i < f1.coordinates.size(); i += 2) {
    points1.push_back(Point2D(f1.coordinates[i], f1.coordinates[i + 1]));
  }

  for (size_t i = 0; i < f2.coordinates.size(); i += 2) {
    points2.push_back(Point2D(f2.coordinates[i], f2.coordinates[i + 1]));
  }

  if (points1.empty() || points2.empty()) {
    return std::numeric_limits<double>::max();
  }

  double min_dist = std::numeric_limits<double>::max();
  for (const auto& p1 : points1) {
    for (const auto& p2 : points2) {
      double dist = p1.distanceTo(p2);
      min_dist = std::min(min_dist, dist);
    }
  }

  return min_dist;
}

void distanceMatrix(const Spatial::VectorDataset& dataset, Spatial::VectorDataset& output,
                    double threshold) {
  output.columns = {"id", "geometry"};

  for (size_t i = 0; i < dataset.features.size(); ++i) {
    std::string col_name = "dist_to_" + std::to_string(i);
    output.columns.push_back(col_name);
  }
  output.columns.push_back("min_distance");
  output.columns.push_back("max_distance");
  output.columns.push_back("mean_distance");
  output.columns.push_back("closest_id");

  output.geometry_column = "geometry";
  output.crs = dataset.crs;

  std::cout << "Calculating distance matrix...\n";
  std::cout << "Features: " << dataset.features.size() << "\n";
  std::cout << "Total pairs: " << (dataset.features.size() * (dataset.features.size() - 1) / 2)
            << "\n";

  int processed = 0;
  int total = dataset.features.size() * dataset.features.size();

  for (size_t i = 0; i < dataset.features.size(); ++i) {
    Spatial::VectorFeature result;
    result.type = Spatial::VectorFeature::GeometryType::POINT;

    if (dataset.features[i].coordinates.size() >= 2) {
      result.coordinates = {dataset.features[i].coordinates[0], dataset.features[i].coordinates[1]};
    } else {
      result.coordinates = {0, 0};
    }

    result.attributes = dataset.features[i].attributes;
    result.attributes["id"] = std::to_string(i);
    result.attributes["geometry"] = geometryToWKT(dataset.features[i]);

    double min_dist = std::numeric_limits<double>::max();
    double max_dist = 0;
    double sum_dist = 0;
    int count = 0;
    std::string closest_id = "";

    for (size_t j = 0; j < dataset.features.size(); ++j) {
      if (i == j)
        continue;

      double dist = featureDistance(dataset.features[i], dataset.features[j]);

      if (threshold > 0 && dist > threshold) {
        dist = -1;
      }

      result.attributes["dist_to_" + std::to_string(j)] = (dist >= 0) ? std::to_string(dist) : "";

      if (dist >= 0) {
        min_dist = std::min(min_dist, dist);
        max_dist = std::max(max_dist, dist);
        sum_dist += dist;
        count++;

        if (dist < min_dist || closest_id.empty()) {
          closest_id = std::to_string(j);
        }
      }

      processed++;
      if (processed % 10000 == 0) {
        std::cout << "\r  Progress: " << (processed * 100 / total) << "%" << std::flush;
      }
    }

    if (count > 0) {
      result.attributes["min_distance"] = std::to_string(min_dist);
      result.attributes["max_distance"] = std::to_string(max_dist);
      result.attributes["mean_distance"] = std::to_string(sum_dist / count);
      result.attributes["closest_id"] = closest_id;
    } else {
      result.attributes["min_distance"] = "";
      result.attributes["max_distance"] = "";
      result.attributes["mean_distance"] = "";
      result.attributes["closest_id"] = "";
    }

    output.features.push_back(result);
  }

  std::cout << "\r  Progress: 100%\n";
}

void distanceToPoint(const Spatial::VectorDataset& dataset, Spatial::VectorDataset& output,
                     double px, double py) {
  output.columns = dataset.columns;
  output.geometry_column = dataset.geometry_column;
  output.crs = dataset.crs;

  output.columns.push_back("distance_to_point");
  output.columns.push_back("distance_x");
  output.columns.push_back("distance_y");

  std::cout << "Calculating distances to point (" << px << ", " << py << ")\n";

  for (const auto& feature : dataset.features) {
    Spatial::VectorFeature result = feature;

    double min_dist = std::numeric_limits<double>::max();

    for (size_t i = 0; i < feature.coordinates.size(); i += 2) {
      double dist =
          pointToPointDistance(px, py, feature.coordinates[i], feature.coordinates[i + 1]);
      min_dist = std::min(min_dist, dist);
    }

    result.attributes["distance_to_point"] = std::to_string(min_dist);
    result.attributes["distance_x"] = std::to_string(px);
    result.attributes["distance_y"] = std::to_string(py);

    output.features.push_back(result);
  }
}

void distanceToNearest(const Spatial::VectorDataset& source, const Spatial::VectorDataset& target,
                       Spatial::VectorDataset& output) {
  output.columns = source.columns;
  output.geometry_column = source.geometry_column;
  output.crs = source.crs;

  output.columns.push_back("nearest_distance");
  output.columns.push_back("nearest_id");
  output.columns.push_back("nearest_attrs");

  std::cout << "Finding nearest features...\n";
  std::cout << "Source: " << source.features.size() << " features\n";
  std::cout << "Target: " << target.features.size() << " features\n";

  int processed = 0;
  int total = source.features.size() * target.features.size();

  for (const auto& source_feature : source.features) {
    Spatial::VectorFeature result = source_feature;

    double min_dist = std::numeric_limits<double>::max();
    size_t nearest_idx = 0;

    for (size_t j = 0; j < target.features.size(); ++j) {
      double dist = featureDistance(source_feature, target.features[j]);

      if (dist < min_dist) {
        min_dist = dist;
        nearest_idx = j;
      }

      processed++;
      if (processed % 10000 == 0) {
        std::cout << "\r  Progress: " << (processed * 100 / total) << "%" << std::flush;
      }
    }

    result.attributes["nearest_distance"] = std::to_string(min_dist);
    result.attributes["nearest_id"] = std::to_string(nearest_idx);

    if (nearest_idx < target.features.size()) {
      const auto& nearest = target.features[nearest_idx];
      std::string attrs_str;
      for (const auto& [key, value] : nearest.attributes) {
        if (key != "geometry") {
          if (!attrs_str.empty())
            attrs_str += "|";
          attrs_str += key + "=" + value;
        }
      }
      result.attributes["nearest_attrs"] = attrs_str;
    }

    output.features.push_back(result);
  }

  std::cout << "\r  Progress: 100%\n";
}

void distanceSelf(const Spatial::VectorDataset& dataset, Spatial::VectorDataset& output) {
  output.columns = dataset.columns;
  output.geometry_column = dataset.geometry_column;
  output.crs = dataset.crs;

  output.columns.push_back("min_distance");
  output.columns.push_back("closest_id");
  output.columns.push_back("closest_distance");

  std::cout << "Finding nearest features within same dataset...\n";
  std::cout << "Features: " << dataset.features.size() << "\n";

  int processed = 0;
  int total = dataset.features.size() * (dataset.features.size() - 1) / 2;

  for (size_t i = 0; i < dataset.features.size(); ++i) {
    Spatial::VectorFeature result = dataset.features[i];

    double min_dist = std::numeric_limits<double>::max();
    size_t closest_idx = 0;

    for (size_t j = 0; j < dataset.features.size(); ++j) {
      if (i == j)
        continue;

      double dist = featureDistance(dataset.features[i], dataset.features[j]);

      if (dist < min_dist) {
        min_dist = dist;
        closest_idx = j;
      }

      processed++;
      if (processed % 10000 == 0) {
        std::cout << "\r  Progress: " << (processed * 100 / total) << "%" << std::flush;
      }
    }

    result.attributes["min_distance"] = std::to_string(min_dist);
    result.attributes["closest_id"] = std::to_string(closest_idx);
    result.attributes["closest_distance"] = std::to_string(min_dist);

    output.features.push_back(result);
  }

  std::cout << "\r  Progress: 100%\n";
}

void printUsage() {
  std::cerr << "spatial_distance - Calculate distances between features\n\n";
  std::cerr << "Usage: spatial_distance <input> <output> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -matrix              Create distance matrix between all features\n";
  std::cerr << "  -point <x> <y>       Distance to a reference point\n";
  std::cerr << "  -nearest <file>      Distance to nearest feature in another dataset\n";
  std::cerr << "  -self                Distance to nearest feature in same dataset\n";
  std::cerr << "  -threshold <value>   Only include distances <= threshold (matrix mode)\n";
  std::cerr << "\nExamples:\n";
  std::cerr << "  spatial_distance cities.csv matrix.csv -matrix\n";
  std::cerr << "  spatial_distance cities.csv distances.csv -point -84.09 9.93\n";
  std::cerr << "  spatial_distance cities.csv distances.csv -nearest hospitals.csv\n";
  std::cerr << "  spatial_distance cities.csv distances.csv -self\n";
}

int main(int argc, char* argv[]) {
  if (argc < 4) {
    printUsage();
    return 1;
  }

  std::string input_file;
  std::string output_file;
  std::string target_file;
  std::string mode;
  double px = 0, py = 0;
  bool has_point = false;
  double threshold = 0;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-matrix") {
      mode = "matrix";
    } else if (arg == "-point" && i + 2 < argc) {
      mode = "point";
      px = std::stod(argv[++i]);
      py = std::stod(argv[++i]);
      has_point = true;
    } else if (arg == "-nearest" && i + 1 < argc) {
      mode = "nearest";
      target_file = argv[++i];
    } else if (arg == "-self") {
      mode = "self";
    } else if (arg == "-threshold" && i + 1 < argc) {
      threshold = std::stod(argv[++i]);
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

  if (mode.empty()) {
    std::cerr << "Error: Mode required (-matrix, -point, -nearest, -self)\n";
    return 1;
  }

  std::cout << "========================================\n";
  std::cout << "  SPATIAL DISTANCE\n";
  std::cout << "========================================\n\n";
  std::cout << "Input: " << input_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Mode: " << mode << "\n";
  if (mode == "point") {
    std::cout << "Reference point: (" << px << ", " << py << ")\n";
  }
  if (mode == "nearest") {
    std::cout << "Target: " << target_file << "\n";
  }
  if (threshold > 0) {
    std::cout << "Threshold: " << threshold << "\n";
  }
  std::cout << "\n";

  Spatial::SpatialCSVReader reader;
  Spatial::VectorDataset source;

  if (!reader.read(input_file, source)) {
    std::cerr << "Error: Could not read input file\n";
    return 1;
  }

  std::cout << "Source features: " << source.features.size() << "\n";

  Spatial::VectorDataset output;

  if (mode == "matrix") {
    distanceMatrix(source, output, threshold);
  } else if (mode == "point") {
    if (!has_point) {
      std::cerr << "Error: -point requires coordinates\n";
      return 1;
    }
    distanceToPoint(source, output, px, py);
  } else if (mode == "nearest") {
    Spatial::VectorDataset target;
    if (!reader.read(target_file, target)) {
      std::cerr << "Error: Could not read target file\n";
      return 1;
    }
    std::cout << "Target features: " << target.features.size() << "\n";
    distanceToNearest(source, target, output);
  } else if (mode == "self") {
    distanceSelf(source, output);
  }

  writeVectorCSV(output, output_file, {"# Mode: " + mode},
                 {"# Total features: " + std::to_string(output.features.size())});

  std::cout << "\n========================================\n";
  std::cout << "  DISTANCE COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Output features: " << output.features.size() << "\n";
  std::cout << "Output written to: " << output_file << "\n";

  return 0;
}
