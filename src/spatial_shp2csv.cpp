#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <vector>

#include "../include/core/spatial_geom.hpp"
#include "../include/core/spatial_shp.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

std::string geomTypeToString(Spatial::VectorFeature::GeometryType type) {
  switch (type) {
    case Spatial::VectorFeature::GeometryType::POINT:
      return "POINT";
    case Spatial::VectorFeature::GeometryType::LINESTRING:
      return "LINESTRING";
    case Spatial::VectorFeature::GeometryType::POLYGON:
      return "POLYGON";
    case Spatial::VectorFeature::GeometryType::MULTIPOINT:
      return "MULTIPOINT";
    case Spatial::VectorFeature::GeometryType::MULTILINESTRING:
      return "MULTILINESTRING";
    case Spatial::VectorFeature::GeometryType::MULTIPOLYGON:
      return "MULTIPOLYGON";
  }
  return "POINT";
}

std::string escapeCSV(const std::string& value) {
  if (value.find(',') != std::string::npos || value.find('"') != std::string::npos ||
      value.find('\n') != std::string::npos) {
    std::string escaped = value;
    size_t pos = 0;
    while ((pos = escaped.find('"', pos)) != std::string::npos) {
      escaped.replace(pos, 1, "\"\"");
      pos += 2;
    }
    return "\"" + escaped + "\"";
  }
  return value;
}

void printUsage() {
  std::cerr << "spatial_shp2csv - Convert a Shapefile (.shp/.shx/.dbf) to spatial CSV\n\n";
  std::cerr << "Usage: spatial_shp2csv <input.shp> <output.csv> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -no_geometry        Exclude geometry column\n";
  std::cerr << "  -include_type       Include geometry type column\n";
  std::cerr << "  -props <list>       Only include specified attribute fields (comma-separated)\n";
  std::cerr << "  -verbose            Show detailed information\n\n";
  std::cerr << "Notes:\n";
  std::cerr << "  <input.shp> may also be given as the .dbf path or with no extension -- the\n";
  std::cerr << "  base name is used to look up the .shp/.dbf pair. A companion .dbf with the\n";
  std::cerr << "  same base name is expected next to the .shp for attribute data.\n";
  std::cerr << "  Polygon rings are read as-is: a real hole in the source Shapefile becomes an\n";
  std::cerr << "  extra part of a MULTIPOLYGON rather than an inner ring, the same\n";
  std::cerr << "  simplification the rest of this project already applies to WKT geometry.\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_shp2csv parcels.shp parcels.csv\n";
  std::cerr << "  spatial_shp2csv parcels.shp parcels.csv -props name,zoning\n";
  std::cerr << "  spatial_shp2csv parcels.shp parcels.csv -include_type\n";
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printUsage();
    return 1;
  }

  std::string input_file = argv[1];
  std::string output_file = argv[2];
  bool include_geometry = true;
  bool include_type = false;
  bool verbose = false;
  std::set<std::string> include_props;

  for (int i = 3; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-no_geometry") {
      include_geometry = false;
    } else if (arg == "-include_type") {
      include_type = true;
    } else if (arg == "-verbose") {
      verbose = true;
    } else if (arg == "-props" && i + 1 < argc) {
      for (const auto& t : split(argv[++i], ',')) {
        std::string prop = trim(t);
        if (!prop.empty()) include_props.insert(prop);
      }
    } else {
      std::cerr << "Error: Unknown option: " << arg << "\n";
      return 1;
    }
  }

  std::cout << "========================================\n";
  std::cout << "  SHAPEFILE TO CSV CONVERTER\n";
  std::cout << "========================================\n\n";
  std::cout << "Input: " << input_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  if (!include_props.empty()) {
    std::cout << "Properties: ";
    for (const auto& p : include_props) std::cout << p << " ";
    std::cout << "\n";
  }
  std::cout << "\n";

  Spatial::ShapefileReader reader;
  Spatial::VectorDataset dataset;
  std::string error;
  if (!reader.read(input_file, dataset, &error)) {
    std::cerr << "Error: " << error << "\n";
    return 1;
  }

  std::string id_col = dataset.columns.front();
  std::vector<std::string> attr_cols;
  for (const auto& col : dataset.columns) {
    if (col == id_col || col == dataset.geometry_column) continue;
    if (include_props.empty() || include_props.count(col)) attr_cols.push_back(col);
  }

  std::ofstream out(output_file);
  if (!out.is_open()) {
    std::cerr << "Error: Could not create output file: " << output_file << "\n";
    return 1;
  }

  out << "# Geometry column: " << (include_geometry ? dataset.geometry_column : "") << "\n";
  out << "# Converted from Shapefile\n";
  out << "# Features: " << dataset.features.size() << "\n";

  out << id_col;
  for (const auto& c : attr_cols) out << "," << c;
  if (include_type) out << ",geometry_type";
  if (include_geometry) out << "," << dataset.geometry_column;
  out << "\n";

  for (const auto& feature : dataset.features) {
    auto id_it = feature.attributes.find(id_col);
    out << escapeCSV(id_it != feature.attributes.end() ? id_it->second : "");

    for (const auto& c : attr_cols) {
      auto it = feature.attributes.find(c);
      out << "," << escapeCSV(it != feature.attributes.end() ? it->second : "");
    }

    if (include_type) out << "," << geomTypeToString(feature.type);
    if (include_geometry) out << "," << escapeCSV(geometryToWKT(feature));
    out << "\n";
  }
  out.close();

  std::cout << "========================================\n";
  std::cout << "  CONVERSION COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Features converted: " << dataset.features.size() << "\n";
  std::cout << "Output written to: " << output_file << "\n";

  if (verbose && !dataset.type_counts.empty()) {
    std::cout << "\nGeometry types:\n";
    for (const auto& [type_name, count] : dataset.type_counts) {
      std::cout << "  " << type_name << ": " << count << "\n";
    }
  }

  return 0;
}
