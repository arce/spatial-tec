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
};

bool polygonsIntersect(const std::vector<double>& p1, const std::vector<double>& p2) {
  for (size_t i = 0; i < p1.size(); i += 2) {
    if (pointInPolygon(p1[i], p1[i + 1], p2)) {
      return true;
    }
  }

  for (size_t i = 0; i < p2.size(); i += 2) {
    if (pointInPolygon(p2[i], p2[i + 1], p1)) {
      return true;
    }
  }
  return false;
}

bool polygonsAdjacent(const std::vector<double>& p1, const std::vector<double>& p2) {
  double threshold = 0.001;

  for (size_t i = 0; i < p1.size(); i += 2) {
    for (size_t j = 0; j < p2.size(); j += 2) {
      double dx = p1[i] - p2[j];
      double dy = p1[i + 1] - p2[j + 1];
      if (dx * dx + dy * dy < threshold * threshold) {
        return true;
      }
    }
  }
  return false;
}

void writeUnionCSV(const Spatial::VectorDataset& dataset, const std::string& filename,
                   const std::string& operation) {
  writeVectorCSV(dataset, filename, {"# Operation: " + operation},
                 {"# Total features: " + std::to_string(dataset.features.size())}, true);
}

Spatial::VectorDataset unionPolygons(const Spatial::VectorDataset& input,
                                     const std::string& group_by, bool all_in_one,
                                     const std::string& method) {
  Spatial::VectorDataset output;
  output.columns = input.columns;
  output.geometry_column = input.geometry_column;
  output.crs = input.crs;

  output.columns.push_back("_union_group");
  output.columns.push_back("_union_count");
  output.columns.push_back("_original_count");

  if (all_in_one) {
    Spatial::VectorFeature combined;
    combined.type = Spatial::VectorFeature::GeometryType::POLYGON;
    combined.attributes["_union_group"] = "all";
    combined.attributes["_union_count"] = std::to_string(input.features.size());
    combined.attributes["_original_count"] = std::to_string(input.features.size());

    if (!input.features.empty()) {
      combined.coordinates = input.features[0].coordinates;

      for (const auto& [key, value] : input.features[0].attributes) {
        combined.attributes[key] = value;
      }
    }

    output.features.push_back(combined);

  } else if (!group_by.empty()) {
    std::map<std::string, std::vector<const Spatial::VectorFeature*>> groups;

    for (const auto& feature : input.features) {
      auto it = feature.attributes.find(group_by);
      std::string group_key = (it != feature.attributes.end()) ? it->second : "default";
      groups[group_key].push_back(&feature);
    }

    for (const auto& [group, features] : groups) {
      if (features.empty())
        continue;

      Spatial::VectorFeature combined;
      combined.type = Spatial::VectorFeature::GeometryType::POLYGON;
      combined.attributes["_union_group"] = group;
      combined.attributes["_union_count"] = std::to_string(features.size());
      combined.attributes["_original_count"] = std::to_string(features.size());

      combined.coordinates = features[0]->coordinates;

      for (const auto& [key, value] : features[0]->attributes) {
        if (key != group_by && key != "_union_group" && key != "_union_count" &&
            key != "_original_count") {
          combined.attributes[key] = value;
        }
      }

      output.features.push_back(combined);
    }

  } else {
    std::vector<bool> processed(input.features.size(), false);
    std::vector<std::vector<size_t>> groups;

    for (size_t i = 0; i < input.features.size(); ++i) {
      if (processed[i])
        continue;
      if (input.features[i].type != Spatial::VectorFeature::GeometryType::POLYGON) {
        output.features.push_back(input.features[i]);
        processed[i] = true;
        continue;
      }

      std::vector<size_t> group = {i};
      processed[i] = true;

      bool changed = true;
      while (changed) {
        changed = false;
        for (size_t j = i + 1; j < input.features.size(); ++j) {
          if (processed[j])
            continue;
          if (input.features[j].type != Spatial::VectorFeature::GeometryType::POLYGON)
            continue;

          for (size_t k : group) {
            bool connected = false;
            if (method == "intersect") {
              connected =
                  polygonsIntersect(input.features[k].coordinates, input.features[j].coordinates);
            } else {
              connected =
                  polygonsIntersect(input.features[k].coordinates, input.features[j].coordinates) ||
                  polygonsAdjacent(input.features[k].coordinates, input.features[j].coordinates);
            }

            if (connected) {
              group.push_back(j);
              processed[j] = true;
              changed = true;
              break;
            }
          }
        }
      }

      groups.push_back(group);
    }

    for (const auto& group : groups) {
      if (group.empty())
        continue;

      Spatial::VectorFeature combined;
      combined.type = Spatial::VectorFeature::GeometryType::POLYGON;
      combined.attributes["_union_group"] = std::to_string(groups.size());
      combined.attributes["_union_count"] = std::to_string(group.size());
      combined.attributes["_original_count"] = std::to_string(group.size());

      combined.coordinates = input.features[group[0]].coordinates;

      for (const auto& [key, value] : input.features[group[0]].attributes) {
        if (key != "_union_group" && key != "_union_count" && key != "_original_count") {
          combined.attributes[key] = value;
        }
      }

      output.features.push_back(combined);
    }
  }

  return output;
}

void printUsage() {
  std::cerr << "spatial_union - Union/dissolve polygons\n\n";
  std::cerr << "Usage: spatial_union <input> <output> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -all              Union all polygons into one\n";
  std::cerr << "  -group_by <attr>  Group and union by attribute\n";
  std::cerr << "  -method <method>  Union method: intersect, adjacent, both\n";
  std::cerr << "                    (default: both)\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_union polygons.csv merged_polygons.csv\n";
  std::cerr << "  spatial_union polygons.csv single_polygon.csv -all\n";
  std::cerr << "  spatial_union parcels.csv grouped_parcels.csv -group_by zone\n";
}

int main(int argc, char* argv[]) {
  if (argc < 4) {
    printUsage();
    return 1;
  }

  std::string input_file;
  std::string output_file;
  std::string group_by;
  bool all_in_one = false;
  std::string method = "both";

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-all") {
      all_in_one = true;
    } else if (arg == "-group_by" && i + 1 < argc) {
      group_by = argv[++i];
    } else if (arg == "-method" && i + 1 < argc) {
      method = toLower(argv[++i]);
      if (method != "intersect" && method != "adjacent" && method != "both") {
        std::cerr << "Error: Unknown method: " << method << "\n";
        std::cerr << "Methods: intersect, adjacent, both\n";
        return 1;
      }
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
  std::cout << "  SPATIAL UNION\n";
  std::cout << "========================================\n\n";
  std::cout << "Input: " << input_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  if (all_in_one) {
    std::cout << "Mode: Union all into one\n";
  } else if (!group_by.empty()) {
    std::cout << "Group by: " << group_by << "\n";
  } else {
    std::cout << "Mode: Union intersecting/adjacent polygons\n";
  }
  std::cout << "Method: " << method << "\n";
  std::cout << "\n";

  Spatial::SpatialCSVReader reader;
  Spatial::VectorDataset dataset;

  if (!reader.read(input_file, dataset)) {
    std::cerr << "Error: Could not read input file\n";
    return 1;
  }

  size_t total_polygons = 0;
  size_t other_features = 0;
  for (const auto& f : dataset.features) {
    if (f.type == Spatial::VectorFeature::GeometryType::POLYGON) {
      total_polygons++;
    } else {
      other_features++;
    }
  }

  std::cout << "Input features: " << dataset.features.size() << "\n";
  std::cout << "  Polygons: " << total_polygons << "\n";
  std::cout << "  Other types: " << other_features << "\n\n";

  Spatial::VectorDataset output = unionPolygons(dataset, group_by, all_in_one, method);

  size_t output_polygons = 0;
  for (const auto& f : output.features) {
    if (f.type == Spatial::VectorFeature::GeometryType::POLYGON) {
      output_polygons++;
    }
  }

  writeUnionCSV(output, output_file,
                all_in_one ? "union_all" : (!group_by.empty() ? "union_group" : "union_dissolve"));

  std::cout << "========================================\n";
  std::cout << "  UNION COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Input polygons: " << total_polygons << "\n";
  std::cout << "Output polygons: " << output_polygons << "\n";
  if (total_polygons > 0) {
    double reduction = (1.0 - (double)output_polygons / total_polygons) * 100.0;
    std::cout << "Reduction: " << std::fixed << std::setprecision(1) << reduction << "%\n";
  }
  if (all_in_one) {
    std::cout << "All polygons merged into one\n";
  } else if (!group_by.empty()) {
    std::cout << "Grouped by: " << group_by << "\n";
  }
  std::cout << "Output features: " << output.features.size() << "\n";
  std::cout << "Output written to: " << output_file << "\n";

  return 0;
}
