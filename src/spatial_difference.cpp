#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
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

  bool intersects(const BBox& other) const {
    return !(other.maxx < minx || other.minx > maxx || other.maxy < miny || other.miny > maxy);
  }
};

double featureDistance(const Spatial::VectorFeature& f1, const Spatial::VectorFeature& f2) {
  double min_dist = std::numeric_limits<double>::max();

  for (size_t i = 0; i < f1.coordinates.size(); i += 2) {
    for (size_t j = 0; j < f2.coordinates.size(); j += 2) {
      double dist = pointToPointDistance(f1.coordinates[i], f1.coordinates[i + 1],
                                         f2.coordinates[j], f2.coordinates[j + 1]);
      min_dist = std::min(min_dist, dist);
    }
  }

  return min_dist;
}

BBox getFeatureBBox(const Spatial::VectorFeature& feature) {
  BBox bbox;
  bool first = true;

  for (size_t i = 0; i < feature.coordinates.size(); i += 2) {
    double x = feature.coordinates[i];
    double y = feature.coordinates[i + 1];
    if (first) {
      bbox.minx = bbox.maxx = x;
      bbox.miny = bbox.maxy = y;
      first = false;
    } else {
      bbox.minx = std::min(bbox.minx, x);
      bbox.miny = std::min(bbox.miny, y);
      bbox.maxx = std::max(bbox.maxx, x);
      bbox.maxy = std::max(bbox.maxy, y);
    }
  }

  return bbox;
}

enum DiffType { WITHIN, INTERSECTS, POLYGON_DIFF, SYMMETRIC, DISTANCE };

bool evaluateSpatialRelation(const Spatial::VectorFeature& source,
                             const Spatial::VectorFeature& target, DiffType type,
                             double distance_threshold = 0) {
  BBox bbox1 = getFeatureBBox(source);
  BBox bbox2 = getFeatureBBox(target);

  switch (type) {
    case WITHIN: {
      if (target.type != Spatial::VectorFeature::GeometryType::POLYGON) {
        return false;
      }

      for (size_t i = 0; i < source.coordinates.size(); i += 2) {
        if (!pointInPolygon(source.coordinates[i], source.coordinates[i + 1], target.coordinates)) {
          return false;
        }
      }
      return true;
    }

    case INTERSECTS: {
      if (target.type == Spatial::VectorFeature::GeometryType::POINT) {
        for (size_t i = 0; i < source.coordinates.size(); i += 2) {
          double dist = pointToPointDistance(source.coordinates[i], source.coordinates[i + 1],
                                             target.coordinates[0], target.coordinates[1]);
          if (dist < 1e-9)
            return true;
        }
      } else if (target.type == Spatial::VectorFeature::GeometryType::LINESTRING) {
        for (size_t i = 0; i < source.coordinates.size(); i += 2) {
          double dist = pointToLineDistance(source.coordinates[i], source.coordinates[i + 1],
                                            target.coordinates);
          if (dist < 1e-9)
            return true;
        }
      } else if (target.type == Spatial::VectorFeature::GeometryType::POLYGON) {
        for (size_t i = 0; i < source.coordinates.size(); i += 2) {
          if (pointInPolygon(source.coordinates[i], source.coordinates[i + 1],
                             target.coordinates)) {
            return true;
          }
        }
      }
      return false;
    }

    case POLYGON_DIFF: {
      if (source.type != Spatial::VectorFeature::GeometryType::POLYGON ||
          target.type != Spatial::VectorFeature::GeometryType::POLYGON) {
        return false;
      }

      for (size_t i = 0; i < source.coordinates.size(); i += 2) {
        if (pointInPolygon(source.coordinates[i], source.coordinates[i + 1], target.coordinates)) {
          return true;
        }
      }
      return false;
    }

    case DISTANCE: {
      double dist = featureDistance(source, target);
      return dist <= distance_threshold;
    }

    case SYMMETRIC: {
      return false;
    }
  }

  return false;
}

void differenceWithin(const Spatial::VectorDataset& source, const Spatial::VectorDataset& target,
                      Spatial::VectorDataset& output, bool symmetric = false) {
  output.columns = source.columns;
  output.geometry_column = source.geometry_column;
  output.crs = source.crs;

  output.columns.push_back("_diff_type");
  output.columns.push_back("_target_id");

  for (const auto& source_feature : source.features) {
    bool found = false;
    std::string target_id = "";

    for (const auto& target_feature : target.features) {
      if (evaluateSpatialRelation(source_feature, target_feature, WITHIN)) {
        found = true;
        auto it = target_feature.attributes.find("id");
        if (it != target_feature.attributes.end()) {
          target_id = it->second;
        }
        break;
      }
    }

    if (!found) {
      Spatial::VectorFeature result = source_feature;
      result.attributes["_diff_type"] = "not_within";
      result.attributes["_target_id"] = target_id;
      output.features.push_back(result);
    } else if (symmetric) {
    }
  }
}

void differenceIntersects(const Spatial::VectorDataset& source,
                          const Spatial::VectorDataset& target, Spatial::VectorDataset& output,
                          bool symmetric = false) {
  output.columns = source.columns;
  output.geometry_column = source.geometry_column;
  output.crs = source.crs;

  output.columns.push_back("_diff_type");
  output.columns.push_back("_target_id");

  for (const auto& source_feature : source.features) {
    bool found = false;
    std::string target_id = "";

    for (const auto& target_feature : target.features) {
      if (evaluateSpatialRelation(source_feature, target_feature, INTERSECTS)) {
        found = true;
        auto it = target_feature.attributes.find("id");
        if (it != target_feature.attributes.end()) {
          target_id = it->second;
        }
        break;
      }
    }

    if (!found) {
      Spatial::VectorFeature result = source_feature;
      result.attributes["_diff_type"] = "not_intersects";
      result.attributes["_target_id"] = target_id;
      output.features.push_back(result);
    }
  }
}

void differencePolygon(const Spatial::VectorDataset& source, const Spatial::VectorDataset& target,
                       Spatial::VectorDataset& output) {
  output.columns = source.columns;
  output.geometry_column = source.geometry_column;
  output.crs = source.crs;

  output.columns.push_back("_diff_type");
  output.columns.push_back("_target_id");

  for (const auto& source_feature : source.features) {
    bool overlaps = false;
    std::string target_id = "";

    if (source_feature.type == Spatial::VectorFeature::GeometryType::POLYGON) {
      for (const auto& target_feature : target.features) {
        if (target_feature.type == Spatial::VectorFeature::GeometryType::POLYGON) {
          for (size_t i = 0; i < source_feature.coordinates.size(); i += 2) {
            if (pointInPolygon(source_feature.coordinates[i], source_feature.coordinates[i + 1],
                               target_feature.coordinates)) {
              overlaps = true;
              auto it = target_feature.attributes.find("id");
              if (it != target_feature.attributes.end()) {
                target_id = it->second;
              }
              break;
            }
          }
        }
        if (overlaps)
          break;
      }
    }

    if (!overlaps) {
      Spatial::VectorFeature result = source_feature;
      result.attributes["_diff_type"] = "polygon_difference";
      result.attributes["_target_id"] = target_id;
      output.features.push_back(result);
    }
  }
}

void differenceSymmetric(const Spatial::VectorDataset& source, const Spatial::VectorDataset& target,
                         Spatial::VectorDataset& output) {
  output.columns = source.columns;
  output.geometry_column = source.geometry_column;
  output.crs = source.crs;

  output.columns.push_back("_diff_type");
  output.columns.push_back("_source_id");
  output.columns.push_back("_target_id");

  for (const auto& source_feature : source.features) {
    bool found = false;
    std::string target_id = "";

    for (const auto& target_feature : target.features) {
      if (evaluateSpatialRelation(source_feature, target_feature, INTERSECTS)) {
        found = true;
        auto it = target_feature.attributes.find("id");
        if (it != target_feature.attributes.end()) {
          target_id = it->second;
        }
        break;
      }
    }

    if (!found) {
      Spatial::VectorFeature result = source_feature;
      result.attributes["_diff_type"] = "symmetric_left";
      auto id_it = source_feature.attributes.find("id");
      result.attributes["_source_id"] =
          (id_it != source_feature.attributes.end()) ? id_it->second : "";
      result.attributes["_target_id"] = target_id;
      output.features.push_back(result);
    }
  }

  for (const auto& target_feature : target.features) {
    bool found = false;
    std::string source_id = "";

    for (const auto& source_feature : source.features) {
      if (evaluateSpatialRelation(target_feature, source_feature, INTERSECTS)) {
        found = true;
        auto it = source_feature.attributes.find("id");
        if (it != source_feature.attributes.end()) {
          source_id = it->second;
        }
        break;
      }
    }

    if (!found) {
      Spatial::VectorFeature result;
      result.type = target_feature.type;
      result.coordinates = target_feature.coordinates;

      for (const auto& col : output.columns) {
        if (col == "_diff_type") {
          result.attributes["_diff_type"] = "symmetric_right";
        } else if (col == "_source_id") {
          result.attributes["_source_id"] = source_id;
        } else if (col == "_target_id") {
          auto id_it = target_feature.attributes.find("id");
          result.attributes["_target_id"] =
              (id_it != target_feature.attributes.end()) ? id_it->second : "";
        } else if (col == output.geometry_column) {
        } else {
          auto it = target_feature.attributes.find(col);
          if (it != target_feature.attributes.end()) {
            result.attributes[col] = it->second;
          } else {
            result.attributes[col] = "";
          }
        }
      }

      output.features.push_back(result);
    }
  }
}

void differenceDistance(const Spatial::VectorDataset& source, const Spatial::VectorDataset& target,
                        Spatial::VectorDataset& output, double distance) {
  output.columns = source.columns;
  output.geometry_column = source.geometry_column;
  output.crs = source.crs;

  output.columns.push_back("_diff_type");
  output.columns.push_back("_target_id");
  output.columns.push_back("_min_distance");

  for (const auto& source_feature : source.features) {
    bool found = false;
    std::string target_id = "";
    double min_dist = std::numeric_limits<double>::max();

    for (const auto& target_feature : target.features) {
      double dist = featureDistance(source_feature, target_feature);
      if (dist < min_dist) {
        min_dist = dist;
      }

      if (dist <= distance) {
        found = true;
        auto it = target_feature.attributes.find("id");
        if (it != target_feature.attributes.end()) {
          target_id = it->second;
        }
        break;
      }
    }

    if (!found) {
      Spatial::VectorFeature result = source_feature;
      result.attributes["_diff_type"] = "beyond_distance";
      result.attributes["_target_id"] = target_id;
      result.attributes["_min_distance"] = std::to_string(min_dist);
      output.features.push_back(result);
    }
  }
}

void printUsage() {
  std::cerr << "spatial_difference - Find features in one dataset not in another\n\n";
  std::cerr << "Usage: spatial_difference <source> <target> <output> [options]\n\n";
  std::cerr << "Operations:\n";
  std::cerr << "  -within       Features in source NOT within target\n";
  std::cerr << "  -intersects   Features in source NOT intersecting target\n";
  std::cerr << "  -polygon      Polygon areas in source NOT overlapping target\n";
  std::cerr << "  -symmetric    Symmetric difference (in either set but not both)\n";
  std::cerr << "  -distance <d> Features farther than distance from target\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_difference cities.csv zones.csv outside.csv -within\n";
  std::cerr << "  spatial_difference zone_a.csv zone_b.csv unique.csv -polygon\n";
  std::cerr << "  spatial_difference points.csv polygons.csv far.csv -distance 10\n";
  std::cerr << "  spatial_difference a.csv b.csv sym.csv -symmetric\n";
}

int main(int argc, char* argv[]) {
  if (argc < 5) {
    printUsage();
    return 1;
  }

  std::string source_file;
  std::string target_file;
  std::string output_file;
  std::string operation;
  double distance = 0;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-within") {
      operation = "within";
    } else if (arg == "-intersects") {
      operation = "intersects";
    } else if (arg == "-polygon") {
      operation = "polygon";
    } else if (arg == "-symmetric") {
      operation = "symmetric";
    } else if (arg == "-distance" && i + 1 < argc) {
      operation = "distance";
      distance = std::stod(argv[++i]);
    } else if (source_file.empty()) {
      source_file = arg;
    } else if (target_file.empty()) {
      target_file = arg;
    } else {
      output_file = arg;
    }
  }

  if (source_file.empty() || target_file.empty() || output_file.empty()) {
    std::cerr << "Error: Source, target, and output files required\n";
    return 1;
  }

  if (operation.empty()) {
    std::cerr << "Error: Operation required\n";
    return 1;
  }

  std::cout << "========================================\n";
  std::cout << "  SPATIAL DIFFERENCE\n";
  std::cout << "========================================\n\n";
  std::cout << "Source: " << source_file << "\n";
  std::cout << "Target: " << target_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Operation: " << operation << "\n";
  if (operation == "distance") {
    std::cout << "Distance: " << distance << "\n";
  }
  std::cout << "\n";

  Spatial::SpatialCSVReader reader;
  Spatial::VectorDataset source;

  if (!reader.read(source_file, source)) {
    std::cerr << "Error: Could not read source file\n";
    return 1;
  }

  std::cout << "Source features: " << source.features.size() << "\n";

  Spatial::VectorDataset target;

  if (!reader.read(target_file, target)) {
    std::cerr << "Error: Could not read target file\n";
    return 1;
  }

  std::cout << "Target features: " << target.features.size() << "\n\n";

  Spatial::VectorDataset output;

  if (operation == "within") {
    differenceWithin(source, target, output);
  } else if (operation == "intersects") {
    differenceIntersects(source, target, output);
  } else if (operation == "polygon") {
    differencePolygon(source, target, output);
  } else if (operation == "symmetric") {
    differenceSymmetric(source, target, output);
  } else if (operation == "distance") {
    differenceDistance(source, target, output, distance);
  } else {
    std::cerr << "Error: Unknown operation: " << operation << "\n";
    return 1;
  }

  writeVectorCSV(output, output_file, {"# Difference type: " + operation},
                 {"# Total features: " + std::to_string(output.features.size())});

  std::cout << "========================================\n";
  std::cout << "  DIFFERENCE COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Source features: " << source.features.size() << "\n";
  std::cout << "Target features: " << target.features.size() << "\n";
  std::cout << "Output features: " << output.features.size() << "\n";
  std::cout << "Output written to: " << output_file << "\n";

  return 0;
}
