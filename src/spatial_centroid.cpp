#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <vector>

#include "../include/core/spatial_csv.hpp"
#include "../include/core/spatial_geom.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

struct Point2D {
  double x, y;
  Point2D() : x(0), y(0) {}
  Point2D(double x, double y) : x(x), y(y) {}
};

Point2D polygonCentroid(const std::vector<double>& coords) {
  if (coords.size() < 6) {
    double sum_x = 0, sum_y = 0;
    int count = coords.size() / 2;
    for (size_t i = 0; i < coords.size(); i += 2) {
      sum_x += coords[i];
      sum_y += coords[i + 1];
    }
    return Point2D(sum_x / count, sum_y / count);
  }

  double cx = 0, cy = 0;
  double area = 0;
  int n = coords.size() / 2;

  std::vector<double> pts = coords;
  if (pts[0] != pts[pts.size() - 2] || pts[1] != pts[pts.size() - 1]) {
    pts.push_back(pts[0]);
    pts.push_back(pts[1]);
  }

  for (int i = 0; i < n; ++i) {
    int j = (i + 1) % n;
    double xi = pts[i * 2];
    double yi = pts[i * 2 + 1];
    double xj = pts[j * 2];
    double yj = pts[j * 2 + 1];

    double cross = xi * yj - xj * yi;
    area += cross;
    cx += (xi + xj) * cross;
    cy += (yi + yj) * cross;
  }

  area /= 2.0;

  if (std::abs(area) < 1e-12) {
    double sum_x = 0, sum_y = 0;
    int count = coords.size() / 2;
    for (size_t i = 0; i < coords.size(); i += 2) {
      sum_x += coords[i];
      sum_y += coords[i + 1];
    }
    return Point2D(sum_x / count, sum_y / count);
  }

  cx /= (6.0 * area);
  cy /= (6.0 * area);

  return Point2D(cx, cy);
}

Point2D lineMidpoint(const std::vector<double>& coords) {
  if (coords.size() < 4) {
    if (coords.size() >= 2) {
      return Point2D(coords[0], coords[1]);
    }
    return Point2D(0, 0);
  }

  std::vector<double> lengths;
  double total_length = 0;

  for (size_t i = 0; i < coords.size() - 2; i += 2) {
    double dx = coords[i + 2] - coords[i];
    double dy = coords[i + 3] - coords[i + 1];
    double len = std::sqrt(dx * dx + dy * dy);
    lengths.push_back(len);
    total_length += len;
  }

  if (total_length < 1e-12) {
    return Point2D(coords[0], coords[1]);
  }

  double target = total_length / 2.0;
  double accumulated = 0;

  for (size_t i = 0; i < lengths.size(); ++i) {
    if (accumulated + lengths[i] >= target) {
      double t = (target - accumulated) / lengths[i];
      double x1 = coords[i * 2];
      double y1 = coords[i * 2 + 1];
      double x2 = coords[i * 2 + 2];
      double y2 = coords[i * 2 + 3];
      return Point2D(x1 + t * (x2 - x1), y1 + t * (y2 - y1));
    }
    accumulated += lengths[i];
  }

  return Point2D(coords[coords.size() - 2], coords[coords.size() - 1]);
}

Point2D pointCentroid(const std::vector<double>& coords) {
  if (coords.size() >= 2) {
    return Point2D(coords[0], coords[1]);
  }
  return Point2D(0, 0);
}

Point2D calculateCentroid(const Spatial::VectorFeature& feature, const std::string& method) {
  switch (feature.type) {
    case Spatial::VectorFeature::GeometryType::POINT:
      return pointCentroid(feature.coordinates);
    case Spatial::VectorFeature::GeometryType::LINESTRING:
      return lineMidpoint(feature.coordinates);
    case Spatial::VectorFeature::GeometryType::POLYGON:
      return polygonCentroid(feature.coordinates);
    default:
      return Point2D(0, 0);
  }
}

struct CentroidStats {
  size_t total_features;
  size_t processed;
  size_t skipped;
  size_t points_created;
  double min_x, min_y, max_x, max_y;

  size_t points_from_points;
  size_t points_from_lines;
  size_t points_from_polygons;

  CentroidStats()
      : total_features(0),
        processed(0),
        skipped(0),
        points_created(0),
        min_x(0),
        min_y(0),
        max_x(0),
        max_y(0),
        points_from_points(0),
        points_from_lines(0),
        points_from_polygons(0) {}
};

CentroidStats calculateCentroids(const Spatial::VectorDataset& input,
                                 Spatial::VectorDataset& output, const std::string& method,
                                 const std::set<std::string>& types) {
  CentroidStats stats;
  stats.total_features = input.features.size();
  stats.min_x = std::numeric_limits<double>::max();
  stats.min_y = std::numeric_limits<double>::max();
  stats.max_x = std::numeric_limits<double>::lowest();
  stats.max_y = std::numeric_limits<double>::lowest();

  output.columns = input.columns;
  output.geometry_column = input.geometry_column;
  output.crs = input.crs;

  if (std::find(output.columns.begin(), output.columns.end(), "source_type") ==
      output.columns.end()) {
    output.columns.push_back("source_type");
  }

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
      default:
        type_name = "unknown";
    }

    if (!types.empty() && types.find(type_name) == types.end()) {
      stats.skipped++;
      continue;
    }

    Point2D centroid = calculateCentroid(feature, method);

    stats.min_x = std::min(stats.min_x, centroid.x);
    stats.min_y = std::min(stats.min_y, centroid.y);
    stats.max_x = std::max(stats.max_x, centroid.x);
    stats.max_y = std::max(stats.max_y, centroid.y);

    stats.processed++;
    stats.points_created++;

    if (type_name == "point")
      stats.points_from_points++;
    else if (type_name == "line")
      stats.points_from_lines++;
    else if (type_name == "polygon")
      stats.points_from_polygons++;

    Spatial::VectorFeature result;
    result.type = Spatial::VectorFeature::GeometryType::POINT;
    result.coordinates = {centroid.x, centroid.y};

    result.attributes = feature.attributes;

    result.attributes["source_type"] = type_name;

    output.features.push_back(result);
  }

  return stats;
}

void writeVectorCSV(const Spatial::VectorDataset& dataset, const std::string& filename,
                    const std::vector<std::string>& comments = {},
                    const std::vector<std::string>& footers = {}) {
  std::ofstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open output file: " << filename << "\n";
    return;
  }

  for (const auto& comment : comments) {
    file << "# " << comment << "\n";
  }

  for (size_t i = 0; i < dataset.columns.size(); ++i) {
    if (i > 0)
      file << ",";
    file << dataset.columns[i];
  }
  file << "\n";

  for (const auto& feature : dataset.features) {
    for (size_t i = 0; i < dataset.columns.size(); ++i) {
      if (i > 0)
        file << ",";

      const std::string& col = dataset.columns[i];

      if (col == dataset.geometry_column) {
        std::string wkt;
        switch (feature.type) {
          case Spatial::VectorFeature::GeometryType::POINT:
            wkt = "POINT (" + std::to_string(feature.coordinates[0]) + " " +
                  std::to_string(feature.coordinates[1]) + ")";
            break;
          case Spatial::VectorFeature::GeometryType::LINESTRING:
            wkt = "LINESTRING (";
            for (size_t j = 0; j < feature.coordinates.size(); j += 2) {
              if (j > 0)
                wkt += ", ";
              wkt += std::to_string(feature.coordinates[j]) + " " +
                     std::to_string(feature.coordinates[j + 1]);
            }
            wkt += ")";
            break;
          case Spatial::VectorFeature::GeometryType::POLYGON:
            wkt = "POLYGON ((";
            for (size_t j = 0; j < feature.coordinates.size(); j += 2) {
              if (j > 0)
                wkt += ", ";
              wkt += std::to_string(feature.coordinates[j]) + " " +
                     std::to_string(feature.coordinates[j + 1]);
            }
            wkt += "))";
            break;
          default:
            wkt = "";
        }
        file << "\"" << wkt << "\"";
      } else {
        auto it = feature.attributes.find(col);
        if (it != feature.attributes.end()) {
          file << it->second;
        } else {
          file << "";
        }
      }
    }
    file << "\n";
  }

  for (const auto& footer : footers) {
    file << "# " << footer << "\n";
  }

  file.close();
}

void printUsage() {
  std::cerr << "spatial_centroid - Calculate centroids of vector geometries\n\n";
  std::cerr << "Usage: spatial_centroid <input> <output> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -method <method>   Centroid method: centroid, mass, midpoint\n";
  std::cerr << "                     (default: centroid for polygons, midpoint for lines)\n";
  std::cerr << "  -types <list>      Types to process: point,line,polygon\n";
  std::cerr << "                     (default: line,polygon)\n";
  std::cerr << "  -verbose           Show detailed statistics\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_centroid polygons.csv centroids.csv\n";
  std::cerr << "  spatial_centroid polygons.csv centroids.csv -method mass\n";
  std::cerr << "  spatial_centroid lines.csv midpoints.csv\n";
  std::cerr << "  spatial_centroid polygons.csv centroids.csv -types polygon\n";
  std::cerr << "  spatial_centroid mixed.csv centroids.csv -types point,line,polygon\n\n";
  std::cerr << "Note: The output ALWAYS has POINT geometry in the geometry column.\n";
  std::cerr << "All attributes from the input features are copied to the output points.\n";
  std::cerr << "A 'source_type' column indicates the original geometry type.\n";
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printUsage();
    return 1;
  }

  std::string input_file;
  std::string output_file;
  std::string method = "centroid";
  std::set<std::string> types = {"line", "polygon"};
  bool verbose = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-method" && i + 1 < argc) {
      method = toLower(argv[++i]);
      if (method != "centroid" && method != "mass" && method != "midpoint" && method != "c" &&
          method != "m" && method != "mp") {
        std::cerr << "Error: Unknown method: " << method << "\n";
        return 1;
      }
      if (method == "c")
        method = "centroid";
      if (method == "m")
        method = "mass";
      if (method == "mp")
        method = "midpoint";
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

  std::cout << "========================================\n";
  std::cout << "  SPATIAL CENTROID\n";
  std::cout << "========================================\n\n";
  std::cout << "Input: " << input_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Method: " << method << "\n";
  std::cout << "Types: ";
  for (const auto& t : types) std::cout << t << " ";
  std::cout << "\n\n";

  Spatial::SpatialCSVReader reader;
  Spatial::VectorDataset dataset;

  if (!reader.read(input_file, dataset)) {
    std::cerr << "Error: Could not read input file\n";
    return 1;
  }

  std::cout << "Input features: " << dataset.features.size() << "\n";

  size_t points = 0, lines = 0, polygons = 0;
  for (const auto& f : dataset.features) {
    switch (f.type) {
      case Spatial::VectorFeature::GeometryType::POINT:
        points++;
        break;
      case Spatial::VectorFeature::GeometryType::LINESTRING:
        lines++;
        break;
      case Spatial::VectorFeature::GeometryType::POLYGON:
        polygons++;
        break;
      default:
        break;
    }
  }
  std::cout << "  Points: " << points << "\n";
  std::cout << "  Lines: " << lines << "\n";
  std::cout << "  Polygons: " << polygons << "\n\n";

  Spatial::VectorDataset output;
  CentroidStats stats = calculateCentroids(dataset, output, method, types);

  writeVectorCSV(output, output_file, {"Centroid method: " + method},
                 {"Total features: " + std::to_string(output.features.size())});

  std::cout << "========================================\n";
  std::cout << "  CENTROID COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Total features: " << stats.total_features << "\n";
  std::cout << "Processed: " << stats.processed << "\n";
  std::cout << "Skipped: " << stats.skipped << "\n";
  std::cout << "Centroids created: " << stats.points_created << "\n";

  if (stats.points_created > 0) {
    std::cout << "\nBy source type:\n";
    std::cout << "  From points: " << stats.points_from_points << "\n";
    std::cout << "  From lines: " << stats.points_from_lines << "\n";
    std::cout << "  From polygons: " << stats.points_from_polygons << "\n";

    std::cout << "\nExtent of centroids:\n";
    std::cout << "  Min X: " << std::fixed << std::setprecision(6) << stats.min_x << "\n";
    std::cout << "  Min Y: " << std::fixed << std::setprecision(6) << stats.min_y << "\n";
    std::cout << "  Max X: " << std::fixed << std::setprecision(6) << stats.max_x << "\n";
    std::cout << "  Max Y: " << std::fixed << std::setprecision(6) << stats.max_y << "\n";
  }

  std::cout << "\nOutput features: " << output.features.size() << "\n";
  std::cout << "Output written to: " << output_file << "\n";

  if (verbose) {
    std::cout << "\nColumns in output:\n";
    for (const auto& col : output.columns) {
      std::cout << "  - " << col << "\n";
    }
  }

  return 0;
}

