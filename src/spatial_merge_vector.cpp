#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <unordered_map>

#include "../include/core/spatial_csv.hpp"
#include "../include/core/spatial_geom.hpp"
#include "../include/core/spatial_io.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

bool hasCommonColumns(const std::vector<std::string>& cols1,
                      const std::vector<std::string>& cols2) {
  for (const auto& col : cols1) {
    if (std::find(cols2.begin(), cols2.end(), col) != cols2.end()) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> mergeColumns(const std::vector<std::vector<std::string>>& all_columns) {
  std::set<std::string> unique_cols;
  for (const auto& cols : all_columns) {
    for (const auto& col : cols) {
      unique_cols.insert(col);
    }
  }
  return std::vector<std::string>(unique_cols.begin(), unique_cols.end());
}

void printUsage() {
  std::cerr << "spatial_merge_vector - Merge multiple vector files\n\n";
  std::cerr << "Usage: spatial_merge_vector <input1> [input2 ...] <output>\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -auto_attrs    Automatically merge columns from all inputs\n";
  std::cerr << "  -drop_dups     Drop duplicate features (by geometry)\n";
  std::cerr << "\nExamples:\n";
  std::cerr << "  spatial_merge_vector cities1.csv cities2.csv merged.csv\n";
  std::cerr << "  spatial_merge_vector c1.csv c2.csv c3.csv merged.csv -auto_attrs\n";
}

int main(int argc, char* argv[]) {
  if (argc < 4) {
    printUsage();
    return 1;
  }

  std::vector<std::string> input_files;
  std::string output_file;
  bool auto_attrs = false;
  bool drop_dups = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-auto_attrs") {
      auto_attrs = true;
    } else if (arg == "-drop_dups") {
      drop_dups = true;
    } else {
      if (i == argc - 1) {
        output_file = arg;
      } else {
        input_files.push_back(arg);
      }
    }
  }

  if (input_files.empty()) {
    std::cerr << "Error: At least one input file required\n";
    return 1;
  }

  if (output_file.empty()) {
    std::cerr << "Error: Output file required\n";
    return 1;
  }

  std::cout << "Input files: " << input_files.size() << "\n";
  for (const auto& f : input_files) {
    std::cout << "  " << f << "\n";
  }
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Auto-attrs: " << (auto_attrs ? "yes" : "no") << "\n";
  std::cout << "Drop duplicates: " << (drop_dups ? "yes" : "no") << "\n\n";

  Spatial::SpatialCSVReader reader;
  std::vector<Spatial::VectorDataset> datasets;
  std::vector<std::vector<std::string>> all_columns;
  std::string crs;
  std::string geometry_column = "geometry";

  for (const auto& filename : input_files) {
    Spatial::VectorDataset dataset;
    if (!reader.read(filename, dataset)) {
      std::cerr << "Error: Could not read input file: " << filename << "\n";
      return 1;
    }
    datasets.push_back(dataset);
    all_columns.push_back(dataset.columns);
    if (!dataset.crs.empty()) {
      if (crs.empty()) {
        crs = dataset.crs;
      } else if (crs != dataset.crs) {
        std::cerr << "Warning: Different CRS detected: " << crs << " vs " << dataset.crs << "\n";
        std::cerr << "  Using first CRS: " << crs << "\n";
      }
    }
    std::cout << "  Loaded: " << filename << " (" << dataset.features.size() << " features)\n";
  }

  std::vector<std::string> merged_columns;
  if (auto_attrs) {
    merged_columns = mergeColumns(all_columns);
    std::cout << "\nMerged columns: " << merged_columns.size() << "\n";
    for (const auto& col : merged_columns) {
      std::cout << "  " << col << "\n";
    }
  } else {
    merged_columns = datasets[0].columns;
    std::cout << "\nUsing columns from first file: " << merged_columns.size() << "\n";

    for (size_t i = 1; i < datasets.size(); ++i) {
      if (datasets[i].columns != merged_columns) {
        std::cerr << "Warning: File " << input_files[i] << " has different columns\n";
        std::cerr << "  Expected: ";
        for (const auto& c : merged_columns) std::cerr << c << " ";
        std::cerr << "\n  Got: ";
        for (const auto& c : datasets[i].columns) std::cerr << c << " ";
        std::cerr << "\n";
        std::cerr << "  Use -auto_attrs to merge all columns\n";
      }
    }
  }

  Spatial::VectorDataset merged;
  merged.columns = merged_columns;
  merged.geometry_column = geometry_column;
  merged.crs = crs;
  merged.min_x = std::numeric_limits<double>::max();
  merged.min_y = std::numeric_limits<double>::max();
  merged.max_x = std::numeric_limits<double>::lowest();
  merged.max_y = std::numeric_limits<double>::lowest();

  int total_features = 0;
  int duplicate_count = 0;
  std::set<std::string> feature_hashes;

  for (const auto& dataset : datasets) {
    for (const auto& feature : dataset.features) {
      if (drop_dups) {
        std::string wkt = geometryToWKT(feature);
        if (feature_hashes.find(wkt) != feature_hashes.end()) {
          duplicate_count++;
          continue;
        }
        feature_hashes.insert(wkt);
      }

      Spatial::VectorFeature merged_feature;
      merged_feature.type = feature.type;
      merged_feature.coordinates = feature.coordinates;

      for (const auto& col : merged_columns) {
        auto it = feature.attributes.find(col);
        if (it != feature.attributes.end()) {
          merged_feature.attributes[col] = it->second;
        } else {
          merged_feature.attributes[col] = "";
        }
      }

      for (size_t i = 0; i < feature.coordinates.size(); i += 2) {
        double x = feature.coordinates[i];
        double y = feature.coordinates[i + 1];
        merged.min_x = std::min(merged.min_x, x);
        merged.min_y = std::min(merged.min_y, y);
        merged.max_x = std::max(merged.max_x, x);
        merged.max_y = std::max(merged.max_y, y);
        merged.has_bbox = true;
      }

      merged.features.push_back(merged_feature);
      total_features++;
    }
  }

  merged.feature_count = merged.features.size();

  std::unordered_map<std::string, size_t> type_counts;
  for (const auto& f : merged.features) {
    std::string type_name;
    switch (f.type) {
      case Spatial::VectorFeature::GeometryType::POINT:
        type_name = "POINT";
        break;
      case Spatial::VectorFeature::GeometryType::LINESTRING:
        type_name = "LINESTRING";
        break;
      case Spatial::VectorFeature::GeometryType::POLYGON:
        type_name = "POLYGON";
        break;
    }
    type_counts[type_name]++;
  }
  merged.type_counts = type_counts;

  writeVectorCSV(merged, output_file, {},
                 {"# Merged from multiple files",
                  "# Total features: " + std::to_string(merged.features.size())});

  std::cout << "\n========================================\n";
  std::cout << "  MERGE COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Total features: " << total_features << "\n";
  if (drop_dups) {
    std::cout << "Duplicates removed: " << duplicate_count << "\n";
  }
  std::cout << "Output features: " << merged.features.size() << "\n";
  std::cout << "Output columns: " << merged.columns.size() << "\n";
  std::cout << "Geometry types:\n";
  for (const auto& [type, count] : merged.type_counts) {
    std::cout << "  " << type << ": " << count << "\n";
  }
  if (merged.has_bbox) {
    std::cout << "Extent:\n";
    std::cout << "  Min X: " << std::fixed << std::setprecision(6) << merged.min_x << "\n";
    std::cout << "  Min Y: " << std::fixed << std::setprecision(6) << merged.min_y << "\n";
    std::cout << "  Max X: " << std::fixed << std::setprecision(6) << merged.max_x << "\n";
    std::cout << "  Max Y: " << std::fixed << std::setprecision(6) << merged.max_y << "\n";
  }
  std::cout << "\nOutput written to: " << output_file << "\n";

  return 0;
}

