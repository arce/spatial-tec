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

enum QueryType {
  WITHIN,
  CONTAINS,
  INTERSECTS,
  CROSSES,
  OVERLAPS,
  TOUCHES,
  DISJOINT,
  DISTANCE,
  NEAREST
};

bool evaluateSpatialRelation(const Spatial::VectorFeature& feature1,
                             const Spatial::VectorFeature& feature2, QueryType type,
                             double distance_threshold = 0) {
  BBox bbox1 = getFeatureBBox(feature1);
  BBox bbox2 = getFeatureBBox(feature2);

  switch (type) {
    case WITHIN: {
      if (feature2.type != Spatial::VectorFeature::GeometryType::POLYGON &&
          feature2.type != Spatial::VectorFeature::GeometryType::MULTIPOLYGON) {
        return false;
      }

      for (size_t i = 0; i < feature1.coordinates.size(); i += 2) {
        if (!pointInPolygonFeature(feature1.coordinates[i], feature1.coordinates[i + 1], feature2)) {
          return false;
        }
      }
      return true;
    }

    case CONTAINS: {
      return evaluateSpatialRelation(feature2, feature1, WITHIN);
    }

    case INTERSECTS: {
      if (feature2.type == Spatial::VectorFeature::GeometryType::POINT) {
        if (feature1.type == Spatial::VectorFeature::GeometryType::POLYGON ||
            feature1.type == Spatial::VectorFeature::GeometryType::MULTIPOLYGON) {
          return pointInPolygonFeature(feature2.coordinates[0], feature2.coordinates[1],
                                       feature1);
        }

        for (size_t i = 0; i < feature1.coordinates.size(); i += 2) {
          double dist = pointToPointDistance(feature1.coordinates[i], feature1.coordinates[i + 1],
                                             feature2.coordinates[0], feature2.coordinates[1]);
          if (dist < 1e-9)
            return true;
        }
      } else if (feature2.type == Spatial::VectorFeature::GeometryType::LINESTRING) {
        for (size_t i = 0; i < feature1.coordinates.size(); i += 2) {
          double dist = pointToLineDistance(feature1.coordinates[i], feature1.coordinates[i + 1],
                                            feature2.coordinates);
          if (dist < 1e-9)
            return true;
        }
      } else if (feature2.type == Spatial::VectorFeature::GeometryType::POLYGON ||
                 feature2.type == Spatial::VectorFeature::GeometryType::MULTIPOLYGON) {
        for (size_t i = 0; i < feature1.coordinates.size(); i += 2) {
          if (pointInPolygonFeature(feature1.coordinates[i], feature1.coordinates[i + 1],
                                    feature2)) {
            return true;
          }
        }
      }
      return false;
    }

    case DISTANCE: {
      double min_dist = std::numeric_limits<double>::max();

      for (size_t i = 0; i < feature1.coordinates.size(); i += 2) {
        for (size_t j = 0; j < feature2.coordinates.size(); j += 2) {
          double dist = pointToPointDistance(feature1.coordinates[i], feature1.coordinates[i + 1],
                                             feature2.coordinates[j], feature2.coordinates[j + 1]);
          min_dist = std::min(min_dist, dist);
        }
      }

      return min_dist <= distance_threshold;
    }

    case NEAREST: {
      return false;
    }

    case CROSSES:
    case OVERLAPS:
    case TOUCHES:
    case DISJOINT:
    default: {
      return evaluateSpatialRelation(feature1, feature2, INTERSECTS);
    }
  }
}

void writeQueryCSV(const Spatial::VectorDataset& dataset, const std::string& filename,
                   const std::string& query_type, const std::string& target_file = "") {
  std::vector<std::string> comments_before = {"# Query: " + query_type};
  if (!target_file.empty())
    comments_before.push_back("# Target: " + target_file);
  writeVectorCSV(dataset, filename, comments_before,
                 {"# Total features: " + std::to_string(dataset.features.size())});
}

void queryWithin(const Spatial::VectorDataset& source, const Spatial::VectorDataset& target,
                 Spatial::VectorDataset& output, bool invert = false) {
  output.columns = source.columns;
  output.geometry_column = source.geometry_column;
  output.crs = source.crs;

  output.columns.push_back("_query_type");
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

    if ((invert && !found) || (!invert && found)) {
      Spatial::VectorFeature result = source_feature;
      result.attributes["_query_type"] = invert ? "not_within" : "within";
      result.attributes["_target_id"] = target_id;
      output.features.push_back(result);
    }
  }
}

void queryContains(const Spatial::VectorDataset& source, const Spatial::VectorDataset& target,
                   Spatial::VectorDataset& output, bool invert = false) {
  output.columns = source.columns;
  output.geometry_column = source.geometry_column;
  output.crs = source.crs;

  output.columns.push_back("_query_type");
  output.columns.push_back("_target_id");

  for (const auto& source_feature : source.features) {
    bool found = false;
    std::string target_id = "";

    for (const auto& target_feature : target.features) {
      if (evaluateSpatialRelation(source_feature, target_feature, CONTAINS)) {
        found = true;
        auto it = target_feature.attributes.find("id");
        if (it != target_feature.attributes.end()) {
          target_id = it->second;
        }
        break;
      }
    }

    if ((invert && !found) || (!invert && found)) {
      Spatial::VectorFeature result = source_feature;
      result.attributes["_query_type"] = invert ? "not_contains" : "contains";
      result.attributes["_target_id"] = target_id;
      output.features.push_back(result);
    }
  }
}

void queryIntersects(const Spatial::VectorDataset& source, const Spatial::VectorDataset& target,
                     Spatial::VectorDataset& output, bool invert = false) {
  output.columns = source.columns;
  output.geometry_column = source.geometry_column;
  output.crs = source.crs;

  output.columns.push_back("_query_type");
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

    if ((invert && !found) || (!invert && found)) {
      Spatial::VectorFeature result = source_feature;
      result.attributes["_query_type"] = invert ? "not_intersects" : "intersects";
      result.attributes["_target_id"] = target_id;
      output.features.push_back(result);
    }
  }
}

void queryDistance(const Spatial::VectorDataset& source, const Spatial::VectorDataset& target,
                   Spatial::VectorDataset& output, double distance, bool invert = false) {
  output.columns = source.columns;
  output.geometry_column = source.geometry_column;
  output.crs = source.crs;

  output.columns.push_back("_query_type");
  output.columns.push_back("_target_id");
  output.columns.push_back("_query_distance");

  for (const auto& source_feature : source.features) {
    bool found = false;
    std::string target_id = "";
    double min_dist = std::numeric_limits<double>::max();

    for (const auto& target_feature : target.features) {
      for (size_t i = 0; i < source_feature.coordinates.size(); i += 2) {
        for (size_t j = 0; j < target_feature.coordinates.size(); j += 2) {
          double dist = pointToPointDistance(
              source_feature.coordinates[i], source_feature.coordinates[i + 1],
              target_feature.coordinates[j], target_feature.coordinates[j + 1]);
          if (dist < min_dist) {
            min_dist = dist;
          }
        }
      }

      if (min_dist <= distance) {
        found = true;
        auto it = target_feature.attributes.find("id");
        if (it != target_feature.attributes.end()) {
          target_id = it->second;
        }
        break;
      }
    }

    if ((invert && !found) || (!invert && found)) {
      Spatial::VectorFeature result = source_feature;
      result.attributes["_query_type"] = invert ? "not_within_distance" : "within_distance";
      result.attributes["_target_id"] = target_id;
      result.attributes["_query_distance"] = std::to_string(min_dist);
      output.features.push_back(result);
    }
  }
}

void queryNearest(const Spatial::VectorDataset& source, const Spatial::VectorDataset& target,
                  Spatial::VectorDataset& output) {
  output.columns = source.columns;
  output.geometry_column = source.geometry_column;
  output.crs = source.crs;

  output.columns.push_back("_query_type");
  output.columns.push_back("_nearest_id");
  output.columns.push_back("_nearest_distance");
  output.columns.push_back("_nearest_attrs");

  for (const auto& source_feature : source.features) {
    double min_dist = std::numeric_limits<double>::max();
    size_t nearest_idx = 0;
    std::string nearest_id = "";

    for (size_t j = 0; j < target.features.size(); ++j) {
      const auto& target_feature = target.features[j];

      for (size_t i = 0; i < source_feature.coordinates.size(); i += 2) {
        for (size_t k = 0; k < target_feature.coordinates.size(); k += 2) {
          double dist = pointToPointDistance(
              source_feature.coordinates[i], source_feature.coordinates[i + 1],
              target_feature.coordinates[k], target_feature.coordinates[k + 1]);
          if (dist < min_dist) {
            min_dist = dist;
            nearest_idx = j;
            auto it = target_feature.attributes.find("id");
            if (it != target_feature.attributes.end()) {
              nearest_id = it->second;
            }
          }
        }
      }
    }

    Spatial::VectorFeature result = source_feature;
    result.attributes["_query_type"] = "nearest";
    result.attributes["_nearest_id"] = nearest_id;
    result.attributes["_nearest_distance"] = std::to_string(min_dist);

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
      result.attributes["_nearest_attrs"] = attrs_str;
    }

    output.features.push_back(result);
  }
}

void printUsage() {
  std::cerr << "spatial_query - Perform spatial queries between vector datasets\n\n";
  std::cerr << "Usage: spatial_query <input> <output> <operation> <target> [options]\n\n";
  std::cerr << "Operations:\n";
  std::cerr << "  -within <target>      Features completely inside target\n";
  std::cerr << "  -contains <target>    Features that contain target\n";
  std::cerr << "  -intersects <target>  Features that intersect target\n";
  std::cerr << "  -distance <target> <value>  Features within distance\n";
  std::cerr << "  -nearest <target>     Find nearest feature in target\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -invert               Invert the query result\n";
  std::cerr << "  -units <units>        Distance units (default: same as CRS)\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_query cities.csv result.csv -within protected_zones.csv\n";
  std::cerr << "  spatial_query roads.csv result.csv -intersects rivers.csv\n";
  std::cerr << "  spatial_query cities.csv result.csv -distance roads.csv 10\n";
  std::cerr << "  spatial_query cities.csv result.csv -nearest hospitals.csv\n";
  std::cerr << "  spatial_query cities.csv result.csv -within zones.csv -invert\n";
}

int main(int argc, char* argv[]) {
  if (argc < 5) {
    printUsage();
    return 1;
  }

  std::string input_file;
  std::string output_file;
  std::string operation;
  std::string target_file;
  double distance = 0;
  bool invert = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-within" && i + 1 < argc) {
      operation = "within";
      target_file = argv[++i];
    } else if (arg == "-contains" && i + 1 < argc) {
      operation = "contains";
      target_file = argv[++i];
    } else if (arg == "-intersects" && i + 1 < argc) {
      operation = "intersects";
      target_file = argv[++i];
    } else if (arg == "-distance" && i + 2 < argc) {
      operation = "distance";
      target_file = argv[++i];
      distance = std::stod(argv[++i]);
    } else if (arg == "-nearest" && i + 1 < argc) {
      operation = "nearest";
      target_file = argv[++i];
    } else if (arg == "-invert") {
      invert = true;
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

  if (operation.empty() || target_file.empty()) {
    std::cerr << "Error: Operation and target file required\n";
    return 1;
  }

  if (operation == "distance" && distance <= 0) {
    std::cerr << "Error: Distance must be positive for -distance\n";
    return 1;
  }

  std::cout << "========================================\n";
  std::cout << "  SPATIAL QUERY\n";
  std::cout << "========================================\n\n";
  std::cout << "Source: " << input_file << "\n";
  std::cout << "Target: " << target_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Operation: " << operation << "\n";
  if (operation == "distance") {
    std::cout << "Distance: " << distance << "\n";
  }
  if (invert) {
    std::cout << "Invert: yes\n";
  }
  std::cout << "\n";

  Spatial::SpatialCSVReader reader;
  Spatial::VectorDataset source;

  if (!reader.read(input_file, source)) {
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
    queryWithin(source, target, output, invert);
  } else if (operation == "contains") {
    queryContains(source, target, output, invert);
  } else if (operation == "intersects") {
    queryIntersects(source, target, output, invert);
  } else if (operation == "distance") {
    queryDistance(source, target, output, distance, invert);
  } else if (operation == "nearest") {
    if (invert) {
      std::cerr << "Warning: -invert ignored for -nearest operation\n";
    }
    queryNearest(source, target, output);
  } else {
    std::cerr << "Error: Unknown operation: " << operation << "\n";
    return 1;
  }

  writeQueryCSV(output, output_file, operation, target_file);

  std::cout << "========================================\n";
  std::cout << "  QUERY COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Input features: " << source.features.size() << "\n";
  std::cout << "Output features: " << output.features.size() << "\n";
  if (invert && operation != "nearest") {
    std::cout << "Inverted: yes\n";
  }
  std::cout << "Output written to: " << output_file << "\n";

  return 0;
}

