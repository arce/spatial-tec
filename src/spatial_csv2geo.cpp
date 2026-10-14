#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <vector>

#include "../include/core/spatial_csv.hpp"
#include "../include/core/spatial_geom.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

std::string escapeJSON(const std::string& s) {
  std::string result;
  for (char c : s) {
    if (c == '"')
      result += "\\\"";
    else if (c == '\\')
      result += "\\\\";
    else if (c == '\n')
      result += "\\n";
    else if (c == '\r')
      result += "\\r";
    else if (c == '\t')
      result += "\\t";
    else if (c == '\b')
      result += "\\b";
    else if (c == '\f')
      result += "\\f";
    else if (c < 32) {
      std::stringstream ss;
      ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c;
      result += ss.str();
    } else {
      result += c;
    }
  }
  return result;
}

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

std::string coordinatesToGeoJSON(const Spatial::VectorFeature& feature, const std::string& geom_type) {
  const std::vector<double>& coords = feature.coordinates;
  std::string result;

  if (geom_type == "POINT") {
    if (coords.size() >= 2) {
      result = "[" + std::to_string(coords[0]) + ", " + std::to_string(coords[1]) + "]";
    }
  } else if (geom_type == "LINESTRING") {
    result = "[";
    for (size_t i = 0; i < coords.size(); i += 2) {
      if (i > 0)
        result += ", ";
      result += "[" + std::to_string(coords[i]) + ", " + std::to_string(coords[i + 1]) + "]";
    }
    result += "]";
  } else if (geom_type == "POLYGON") {
    result = "[[";
    for (size_t i = 0; i < coords.size(); i += 2) {
      if (i > 0)
        result += ", ";
      result += "[" + std::to_string(coords[i]) + ", " + std::to_string(coords[i + 1]) + "]";
    }

    if (coords.size() >= 4) {
      double first_x = coords[0];
      double first_y = coords[1];
      double last_x = coords[coords.size() - 2];
      double last_y = coords[coords.size() - 1];
      if (std::abs(first_x - last_x) > 1e-12 || std::abs(first_y - last_y) > 1e-12) {
        result += ", [" + std::to_string(first_x) + ", " + std::to_string(first_y) + "]";
      }
    }
    result += "]]";
  } else if (geom_type == "MULTIPOINT") {

    result = "[";
    bool first = true;
    for (const auto& range : featurePartRanges(feature)) {
      for (size_t p = range.first; p < range.second; ++p) {
        if (!first) result += ", ";
        first = false;
        result += "[" + std::to_string(coords[p * 2]) + ", " + std::to_string(coords[p * 2 + 1]) + "]";
      }
    }
    result += "]";
  } else if (geom_type == "MULTILINESTRING") {

    result = "[";
    bool first_part = true;
    for (const auto& range : featurePartRanges(feature)) {
      if (!first_part) result += ", ";
      first_part = false;
      result += "[";
      bool first_pt = true;
      for (size_t p = range.first; p < range.second; ++p) {
        if (!first_pt) result += ", ";
        first_pt = false;
        result += "[" + std::to_string(coords[p * 2]) + ", " + std::to_string(coords[p * 2 + 1]) + "]";
      }
      result += "]";
    }
    result += "]";
  } else if (geom_type == "MULTIPOLYGON") {

    result = "[";
    bool first_part = true;
    for (const auto& range : featurePartRanges(feature)) {
      if (!first_part) result += ", ";
      first_part = false;
      result += "[[";
      bool first_pt = true;
      double first_x = 0, first_y = 0;
      for (size_t p = range.first; p < range.second; ++p) {
        if (first_pt) {
          first_x = coords[p * 2];
          first_y = coords[p * 2 + 1];
        } else {
          result += ", ";
        }
        first_pt = false;
        result += "[" + std::to_string(coords[p * 2]) + ", " + std::to_string(coords[p * 2 + 1]) + "]";
      }
      if (range.second > range.first) {
        double last_x = coords[(range.second - 1) * 2];
        double last_y = coords[(range.second - 1) * 2 + 1];
        if (std::abs(first_x - last_x) > 1e-12 || std::abs(first_y - last_y) > 1e-12) {
          result += ", [" + std::to_string(first_x) + ", " + std::to_string(first_y) + "]";
        }
      }
      result += "]]";
    }
    result += "]";
  }

  return result;
}

std::string geometryToGeoJSON(const Spatial::VectorFeature& feature, const std::string& geom_type) {
  std::string result;

  std::string geo_type;
  if (geom_type == "POINT")
    geo_type = "Point";
  else if (geom_type == "LINESTRING")
    geo_type = "LineString";
  else if (geom_type == "POLYGON")
    geo_type = "Polygon";
  else if (geom_type == "MULTIPOINT")
    geo_type = "MultiPoint";
  else if (geom_type == "MULTILINESTRING")
    geo_type = "MultiLineString";
  else if (geom_type == "MULTIPOLYGON")
    geo_type = "MultiPolygon";
  else
    geo_type = "Point";

  std::string coords_str = coordinatesToGeoJSON(feature, geom_type);

  result = "{\n";
  result += "      \"type\": \"" + geo_type + "\",\n";
  result += "      \"coordinates\": " + coords_str + "\n";
  result += "    }";

  return result;
}

std::string featureToGeoJSON(const Spatial::VectorFeature& feature, const std::string& geom_type,
                             const std::vector<std::string>& props_filter, bool pretty) {
  std::string result;
  std::string indent = pretty ? "  " : "";
  std::string nl = pretty ? "\n" : "";

  result = indent + "{\n";
  result += indent + "  \"type\": \"Feature\",\n";

  result += indent + "  \"geometry\": " + geometryToGeoJSON(feature, geom_type) + ",\n";

  result += indent + "  \"properties\": {\n";

  bool first = true;
  for (const auto& [key, value] : feature.attributes) {
    if (!props_filter.empty()) {
      bool found = false;
      for (const auto& p : props_filter) {
        if (toLower(p) == toLower(key)) {
          found = true;
          break;
        }
      }
      if (!found)
        continue;
    }

    if (!first) {
      result += ",\n";
    }
    first = false;

    std::string escaped_value = escapeJSON(value);

    bool is_num = false;
    try {
      std::stod(value);
      is_num = true;
    } catch (...) {
    }

    if (is_num) {
      result += indent + "    \"" + key + "\": " + value;
    } else if (value == "true" || value == "false") {
      result += indent + "    \"" + key + "\": " + value;
    } else if (value == "null") {
      result += indent + "    \"" + key + "\": null";
    } else {
      result += indent + "    \"" + key + "\": \"" + escaped_value + "\"";
    }
  }

  result += nl + indent + "  }\n";
  result += indent + "}";

  return result;
}

void printUsage() {
  std::cerr << "spatial_csv2geo - Convert spatial CSV to GeoJSON\n\n";
  std::cerr << "Usage: spatial_csv2geo <input.csv> <output.geojson> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -geometry_column <col>   Name of geometry column (default: geometry)\n";
  std::cerr << "  -crs <value>             Coordinate system (default: EPSG:4326)\n";
  std::cerr << "  -props <list>            Only include specific properties (comma-separated)\n";
  std::cerr << "  -pretty                  Pretty print (indented)\n";
  std::cerr << "  -compact                 Compact output (no spaces)\n";
  std::cerr << "  -verbose                 Show detailed information\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_csv2geo cities.csv cities.geojson\n";
  std::cerr << "  spatial_csv2geo cities.csv cities.geojson -pretty\n";
  std::cerr << "  spatial_csv2geo cities.csv cities.geojson -props id,name,population\n";
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printUsage();
    return 1;
  }

  std::string input_file = argv[1];
  std::string output_file = argv[2];
  std::string geometry_column = "geometry";
  std::string crs = "EPSG:4326";
  std::vector<std::string> props_filter;
  bool pretty = false;
  bool compact = false;
  bool verbose = false;

  for (int i = 3; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-geometry_column" && i + 1 < argc) {
      geometry_column = argv[++i];
    } else if (arg == "-crs" && i + 1 < argc) {
      crs = argv[++i];
    } else if (arg == "-props" && i + 1 < argc) {
      std::string props_str = argv[++i];
      auto tokens = split(props_str, ',');
      for (const auto& t : tokens) {
        std::string prop = trim(t);
        if (!prop.empty()) {
          props_filter.push_back(prop);
        }
      }
    } else if (arg == "-pretty") {
      pretty = true;
    } else if (arg == "-compact") {
      compact = true;
    } else if (arg == "-verbose") {
      verbose = true;
    }
  }

  Spatial::SpatialCSVReader reader;
  Spatial::VectorDataset dataset;

  if (!reader.read(input_file, dataset)) {
    std::cerr << "Error: Could not read input file\n";
    return 1;
  }

  std::cout << "========================================\n";
  std::cout << "  CSV TO GEOJSON CONVERTER\n";
  std::cout << "========================================\n\n";
  std::cout << "Input: " << input_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Features: " << dataset.features.size() << "\n";
  std::cout << "Geometry column: " << geometry_column << "\n";
  std::cout << "CRS: " << crs << "\n";
  if (!props_filter.empty()) {
    std::cout << "Properties: ";
    for (const auto& p : props_filter) std::cout << p << " ";
    std::cout << "\n";
  }
  std::cout << "\n";

  std::ofstream file(output_file);
  if (!file.is_open()) {
    std::cerr << "Error: Could not create output file\n";
    return 1;
  }

  std::string indent = pretty ? "  " : "";
  std::string nl = pretty ? "\n" : "";
  std::string space = pretty ? " " : "";
  std::string comma = pretty ? "," : ",";

  file << "{" << nl;
  file << indent << "\"type\": \"FeatureCollection\"," << nl;
  file << indent << "\"crs\": {" << nl;
  file << indent << "  \"type\": \"name\"," << nl;
  file << indent << "  \"properties\": {\"name\": \"" << crs << "\"}" << nl;
  file << indent << "}," << nl;
  file << indent << "\"features\": [" << nl;

  int feature_count = 0;
  for (const auto& feature : dataset.features) {
    std::string geom_type = geomTypeToString(feature.type);

    if (feature.coordinates.empty())
      continue;

    if (feature_count > 0) {
      file << "," << nl;
    }

    file << featureToGeoJSON(feature, geom_type, props_filter, pretty);

    feature_count++;
  }

  file << nl;
  file << indent << "]" << nl;
  file << "}" << nl;

  file.close();

  std::cout << "========================================\n";
  std::cout << "  CONVERSION COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Features converted: " << feature_count << "\n";
  std::cout << "Output written to: " << output_file << "\n";

  if (verbose) {
    std::cout << "\nSample feature:\n";
    if (!dataset.features.empty()) {
      std::string geom_type = geomTypeToString(dataset.features[0].type);
      std::cout << featureToGeoJSON(dataset.features[0], geom_type, props_filter, true) << "\n";
    }
  }

  return 0;
}

