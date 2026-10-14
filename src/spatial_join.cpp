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

enum JoinType { WITHIN, CONTAINS, INTERSECTS, NEAREST };

bool evaluateSpatialRelation(const Spatial::VectorFeature& source,
                             const Spatial::VectorFeature& target, JoinType type) {
  switch (type) {
    case WITHIN: {
      if (target.type != Spatial::VectorFeature::GeometryType::POLYGON &&
          target.type != Spatial::VectorFeature::GeometryType::MULTIPOLYGON) {
        return false;
      }

      for (size_t i = 0; i < source.coordinates.size(); i += 2) {
        if (!pointInPolygonFeature(source.coordinates[i], source.coordinates[i + 1], target)) {
          return false;
        }
      }
      return true;
    }

    case CONTAINS: {
      return evaluateSpatialRelation(target, source, WITHIN);
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
      } else if (target.type == Spatial::VectorFeature::GeometryType::POLYGON ||
                 target.type == Spatial::VectorFeature::GeometryType::MULTIPOLYGON) {
        for (size_t i = 0; i < source.coordinates.size(); i += 2) {
          if (pointInPolygonFeature(source.coordinates[i], source.coordinates[i + 1], target)) {
            return true;
          }
        }
      }
      return false;
    }

    case NEAREST: {
      return false;
    }
  }

  return false;
}

void writeJoinedCSV(const Spatial::VectorDataset& dataset, const std::string& filename,
                    const std::string& join_type, const std::string& target_file) {
  std::ofstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not create file: " << filename << "\n";
    return;
  }

  if (!dataset.crs.empty())
    file << "# CRS: " << dataset.crs << "\n";
  file << "# Join: " << join_type << "\n";
  file << "# Target: " << target_file << "\n";
  file << "# Geometry column: " << dataset.geometry_column << "\n";
  file << "# Total features: " << dataset.features.size() << "\n";

  for (size_t i = 0; i < dataset.columns.size(); ++i) {
    file << dataset.columns[i];
    if (i < dataset.columns.size() - 1)
      file << ",";
  }
  file << "\n";

  for (const auto& feature : dataset.features) {
    for (size_t i = 0; i < dataset.columns.size(); ++i) {
      const std::string& col = dataset.columns[i];
      if (col == dataset.geometry_column) {
        file << geometryToWKT(feature);
      } else {
        auto it = feature.attributes.find(col);
        if (it != feature.attributes.end()) {
          file << it->second;
        }
      }
      if (i < dataset.columns.size() - 1)
        file << ",";
    }
    file << "\n";
  }
}

void joinWithin(const Spatial::VectorDataset& source, const Spatial::VectorDataset& target,
                Spatial::VectorDataset& output, const std::string& prefix,
                const std::set<std::string>& attrs_filter) {
  output.columns = source.columns;
  output.geometry_column = source.geometry_column;
  output.crs = source.crs;

  std::map<std::string, std::string> target_col_map;
  for (const auto& col : target.columns) {
    if (col == target.geometry_column)
      continue;
    if (!attrs_filter.empty() && attrs_filter.find(col) == attrs_filter.end())
      continue;

    std::string new_name = prefix + col;

    if (std::find(output.columns.begin(), output.columns.end(), new_name) != output.columns.end()) {
      new_name = prefix + col + "_2";
    }
    output.columns.push_back(new_name);
    target_col_map[col] = new_name;
  }

  output.columns.push_back("_join_type");
  output.columns.push_back("_target_id");

  for (const auto& source_feature : source.features) {
    Spatial::VectorFeature result = source_feature;

    for (const auto& target_feature : target.features) {
      if (evaluateSpatialRelation(source_feature, target_feature, WITHIN)) {
        for (const auto& [orig_name, new_name] : target_col_map) {
          auto it = target_feature.attributes.find(orig_name);
          if (it != target_feature.attributes.end()) {
            result.attributes[new_name] = it->second;
          } else {
            result.attributes[new_name] = "";
          }
        }

        result.attributes["_join_type"] = "within";
        auto id_it = target_feature.attributes.find("id");
        if (id_it != target_feature.attributes.end()) {
          result.attributes["_target_id"] = id_it->second;
        }

        break;
      }
    }

    for (const auto& [orig_name, new_name] : target_col_map) {
      if (result.attributes.find(new_name) == result.attributes.end()) {
        result.attributes[new_name] = "";
      }
    }
    if (result.attributes.find("_join_type") == result.attributes.end()) {
      result.attributes["_join_type"] = "none";
      result.attributes["_target_id"] = "";
    }

    output.features.push_back(result);
  }
}

void joinContains(const Spatial::VectorDataset& source, const Spatial::VectorDataset& target,
                  Spatial::VectorDataset& output, const std::string& prefix,
                  const std::set<std::string>& attrs_filter) {
  output.columns = source.columns;
  output.geometry_column = source.geometry_column;
  output.crs = source.crs;

  std::map<std::string, std::string> target_col_map;
  for (const auto& col : target.columns) {
    if (col == target.geometry_column)
      continue;
    if (!attrs_filter.empty() && attrs_filter.find(col) == attrs_filter.end())
      continue;

    std::string new_name = prefix + col;
    if (std::find(output.columns.begin(), output.columns.end(), new_name) != output.columns.end()) {
      new_name = prefix + col + "_2";
    }
    output.columns.push_back(new_name);
    target_col_map[col] = new_name;
  }

  output.columns.push_back("_join_type");
  output.columns.push_back("_target_id");

  for (const auto& source_feature : source.features) {
    Spatial::VectorFeature result = source_feature;

    for (const auto& target_feature : target.features) {
      if (evaluateSpatialRelation(source_feature, target_feature, CONTAINS)) {
        for (const auto& [orig_name, new_name] : target_col_map) {
          auto it = target_feature.attributes.find(orig_name);
          if (it != target_feature.attributes.end()) {
            result.attributes[new_name] = it->second;
          } else {
            result.attributes[new_name] = "";
          }
        }
        result.attributes["_join_type"] = "contains";
        auto id_it = target_feature.attributes.find("id");
        if (id_it != target_feature.attributes.end()) {
          result.attributes["_target_id"] = id_it->second;
        }
        break;
      }
    }

    for (const auto& [orig_name, new_name] : target_col_map) {
      if (result.attributes.find(new_name) == result.attributes.end()) {
        result.attributes[new_name] = "";
      }
    }
    if (result.attributes.find("_join_type") == result.attributes.end()) {
      result.attributes["_join_type"] = "none";
      result.attributes["_target_id"] = "";
    }

    output.features.push_back(result);
  }
}

void joinIntersects(const Spatial::VectorDataset& source, const Spatial::VectorDataset& target,
                    Spatial::VectorDataset& output, const std::string& prefix,
                    const std::set<std::string>& attrs_filter) {
  output.columns = source.columns;
  output.geometry_column = source.geometry_column;
  output.crs = source.crs;

  std::map<std::string, std::string> target_col_map;
  for (const auto& col : target.columns) {
    if (col == target.geometry_column)
      continue;
    if (!attrs_filter.empty() && attrs_filter.find(col) == attrs_filter.end())
      continue;

    std::string new_name = prefix + col;
    if (std::find(output.columns.begin(), output.columns.end(), new_name) != output.columns.end()) {
      new_name = prefix + col + "_2";
    }
    output.columns.push_back(new_name);
    target_col_map[col] = new_name;
  }

  output.columns.push_back("_join_type");
  output.columns.push_back("_target_id");

  for (const auto& source_feature : source.features) {
    Spatial::VectorFeature result = source_feature;

    for (const auto& target_feature : target.features) {
      if (evaluateSpatialRelation(source_feature, target_feature, INTERSECTS)) {
        for (const auto& [orig_name, new_name] : target_col_map) {
          auto it = target_feature.attributes.find(orig_name);
          if (it != target_feature.attributes.end()) {
            result.attributes[new_name] = it->second;
          } else {
            result.attributes[new_name] = "";
          }
        }
        result.attributes["_join_type"] = "intersects";
        auto id_it = target_feature.attributes.find("id");
        if (id_it != target_feature.attributes.end()) {
          result.attributes["_target_id"] = id_it->second;
        }
        break;
      }
    }

    for (const auto& [orig_name, new_name] : target_col_map) {
      if (result.attributes.find(new_name) == result.attributes.end()) {
        result.attributes[new_name] = "";
      }
    }
    if (result.attributes.find("_join_type") == result.attributes.end()) {
      result.attributes["_join_type"] = "none";
      result.attributes["_target_id"] = "";
    }

    output.features.push_back(result);
  }
}

void joinNearest(const Spatial::VectorDataset& source, const Spatial::VectorDataset& target,
                 Spatial::VectorDataset& output, const std::string& prefix,
                 const std::set<std::string>& attrs_filter) {
  output.columns = source.columns;
  output.geometry_column = source.geometry_column;
  output.crs = source.crs;

  std::map<std::string, std::string> target_col_map;
  for (const auto& col : target.columns) {
    if (col == target.geometry_column)
      continue;
    if (!attrs_filter.empty() && attrs_filter.find(col) == attrs_filter.end())
      continue;

    std::string new_name = prefix + col;
    if (std::find(output.columns.begin(), output.columns.end(), new_name) != output.columns.end()) {
      new_name = prefix + col + "_2";
    }
    output.columns.push_back(new_name);
    target_col_map[col] = new_name;
  }

  output.columns.push_back(prefix + "distance");
  output.columns.push_back("_join_type");
  output.columns.push_back("_target_id");

  for (const auto& source_feature : source.features) {
    Spatial::VectorFeature result = source_feature;

    double min_dist = std::numeric_limits<double>::max();
    size_t nearest_idx = 0;

    for (size_t j = 0; j < target.features.size(); ++j) {
      const auto& target_feature = target.features[j];
      double dist = featureDistance(source_feature, target_feature);

      if (dist < min_dist) {
        min_dist = dist;
        nearest_idx = j;
      }
    }

    if (nearest_idx < target.features.size()) {
      const auto& nearest = target.features[nearest_idx];

      for (const auto& [orig_name, new_name] : target_col_map) {
        auto it = nearest.attributes.find(orig_name);
        if (it != nearest.attributes.end()) {
          result.attributes[new_name] = it->second;
        } else {
          result.attributes[new_name] = "";
        }
      }

      result.attributes["_join_type"] = "nearest";
      result.attributes[prefix + "distance"] = std::to_string(min_dist);
      auto id_it = nearest.attributes.find("id");
      if (id_it != nearest.attributes.end()) {
        result.attributes["_target_id"] = id_it->second;
      }
    } else {
      for (const auto& [orig_name, new_name] : target_col_map) {
        result.attributes[new_name] = "";
      }
      result.attributes["_join_type"] = "none";
      result.attributes[prefix + "distance"] = "";
      result.attributes["_target_id"] = "";
    }

    output.features.push_back(result);
  }
}

void printUsage() {
  std::cerr << "spatial_join - Perform spatial join between vector datasets\n\n";
  std::cerr << "Usage: spatial_join <source> <target> <output> <operation> [options]\n\n";
  std::cerr << "Operations:\n";
  std::cerr << "  -within     Join attributes where source is within target\n";
  std::cerr << "  -contains   Join attributes where source contains target\n";
  std::cerr << "  -intersects Join attributes where source intersects target\n";
  std::cerr << "  -nearest    Join attributes of nearest target feature\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -prefix <str>    Prefix for target columns (default: 'target_')\n";
  std::cerr << "  -attrs <list>    Only join specific attributes (comma-separated)\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_join cities.csv zones.csv result.csv -within\n";
  std::cerr << "  spatial_join cities.csv hospitals.csv result.csv -nearest -prefix hosp_\n";
  std::cerr << "  spatial_join points.csv roads.csv result.csv -intersects\n";
  std::cerr << "  spatial_join zones.csv cities.csv result.csv -contains -attrs name,population\n";
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
  std::string prefix = "target_";
  std::set<std::string> attrs_filter;
  bool filter_attrs = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-within") {
      operation = "within";
    } else if (arg == "-contains") {
      operation = "contains";
    } else if (arg == "-intersects") {
      operation = "intersects";
    } else if (arg == "-nearest") {
      operation = "nearest";
    } else if (arg == "-prefix" && i + 1 < argc) {
      prefix = argv[++i];
    } else if (arg == "-attrs" && i + 1 < argc) {
      std::string attrs_str = argv[++i];
      auto tokens = split(attrs_str, ',');
      for (const auto& t : tokens) {
        attrs_filter.insert(trim(t));
      }
      filter_attrs = true;
    } else if (source_file.empty()) {
      source_file = arg;
    } else if (target_file.empty()) {
      target_file = arg;
    } else if (output_file.empty()) {
      output_file = arg;
    }
  }

  if (source_file.empty() || target_file.empty() || output_file.empty()) {
    std::cerr << "Error: Source, target, and output files required\n";
    return 1;
  }

  if (operation.empty()) {
    std::cerr << "Error: Operation required (-within, -contains, -intersects, -nearest)\n";
    return 1;
  }

  std::cout << "========================================\n";
  std::cout << "  SPATIAL JOIN\n";
  std::cout << "========================================\n\n";
  std::cout << "Source: " << source_file << "\n";
  std::cout << "Target: " << target_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Operation: " << operation << "\n";
  std::cout << "Prefix: " << prefix << "\n";
  if (filter_attrs) {
    std::cout << "Attributes: ";
    for (const auto& a : attrs_filter) std::cout << a << " ";
    std::cout << "\n";
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

  std::cout << "Target features: " << target.features.size() << "\n";
  std::cout << "Target columns: " << target.columns.size() << "\n\n";

  Spatial::VectorDataset output;

  if (operation == "within") {
    joinWithin(source, target, output, prefix, attrs_filter);
  } else if (operation == "contains") {
    joinContains(source, target, output, prefix, attrs_filter);
  } else if (operation == "intersects") {
    joinIntersects(source, target, output, prefix, attrs_filter);
  } else if (operation == "nearest") {
    joinNearest(source, target, output, prefix, attrs_filter);
  } else {
    std::cerr << "Error: Unknown operation: " << operation << "\n";
    return 1;
  }

  writeJoinedCSV(output, output_file, operation, target_file);

  std::cout << "========================================\n";
  std::cout << "  JOIN COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Source features: " << source.features.size() << "\n";
  std::cout << "Output features: " << output.features.size() << "\n";
  std::cout << "Output columns: " << output.columns.size() << "\n";
  std::cout << "Output written to: " << output_file << "\n";

  return 0;
}

