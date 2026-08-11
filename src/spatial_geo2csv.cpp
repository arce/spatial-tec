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
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"
#include "../include/json.hpp"
using json = nlohmann::json;

std::string coordinatesToWKT(const json& coordinates, const std::string& type) {
  std::string wkt;

  if (type == "Point") {
    if (coordinates.is_array() && coordinates.size() >= 2) {
      wkt = "POINT(" + std::to_string(coordinates[0].get<double>()) + " " +
            std::to_string(coordinates[1].get<double>()) + ")";
    }
  } else if (type == "LineString") {
    wkt = "LINESTRING(";
    for (size_t i = 0; i < coordinates.size(); ++i) {
      if (i > 0)
        wkt += ", ";
      wkt += std::to_string(coordinates[i][0].get<double>()) + " " +
             std::to_string(coordinates[i][1].get<double>());
    }
    wkt += ")";
  } else if (type == "Polygon") {
    wkt = "POLYGON(";
    for (size_t i = 0; i < coordinates.size(); ++i) {
      if (i > 0)
        wkt += ", ";
      wkt += "(";
      for (size_t j = 0; j < coordinates[i].size(); ++j) {
        if (j > 0)
          wkt += ", ";
        wkt += std::to_string(coordinates[i][j][0].get<double>()) + " " +
               std::to_string(coordinates[i][j][1].get<double>());
      }
      wkt += ")";
    }
    wkt += ")";
  } else if (type == "MultiPoint") {
    wkt = "MULTIPOINT(";
    for (size_t i = 0; i < coordinates.size(); ++i) {
      if (i > 0)
        wkt += ", ";
      wkt += "(" + std::to_string(coordinates[i][0].get<double>()) + " " +
             std::to_string(coordinates[i][1].get<double>()) + ")";
    }
    wkt += ")";
  } else if (type == "MultiLineString") {
    wkt = "MULTILINESTRING(";
    for (size_t i = 0; i < coordinates.size(); ++i) {
      if (i > 0)
        wkt += ", ";
      wkt += "(";
      for (size_t j = 0; j < coordinates[i].size(); ++j) {
        if (j > 0)
          wkt += ", ";
        wkt += std::to_string(coordinates[i][j][0].get<double>()) + " " +
               std::to_string(coordinates[i][j][1].get<double>());
      }
      wkt += ")";
    }
    wkt += ")";
  } else if (type == "MultiPolygon") {
    wkt = "MULTIPOLYGON(";
    for (size_t i = 0; i < coordinates.size(); ++i) {
      if (i > 0)
        wkt += ", ";
      wkt += "(";
      for (size_t j = 0; j < coordinates[i].size(); ++j) {
        if (j > 0)
          wkt += ", ";
        wkt += "(";
        for (size_t k = 0; k < coordinates[i][j].size(); ++k) {
          if (k > 0)
            wkt += ", ";
          wkt += std::to_string(coordinates[i][j][k][0].get<double>()) + " " +
                 std::to_string(coordinates[i][j][k][1].get<double>());
        }
        wkt += ")";
      }
      wkt += ")";
    }
    wkt += ")";
  } else if (type == "GeometryCollection") {
    if (!coordinates.empty()) {
      const auto& geom = coordinates[0];
      std::string geom_type = geom["type"].get<std::string>();
      const auto& geom_coords = geom["coordinates"];
      wkt = coordinatesToWKT(geom_coords, geom_type);
    }
  }

  return wkt;
}

// Reserved/structural column names can collide with a real GeoJSON property
// of the same name (e.g. a feature that already has an "id" or "geometry"
// property). When that happens the CSV would end up with two columns
// sharing one name, and SpatialCSVReader collapses same-named columns into
// a single attribute on read -- silently discarding whichever one it saw
// first. To avoid that, the structural column is renamed (never the user's
// real property) until it no longer collides.
std::string uniqueReservedName(const std::string& desired, const std::set<std::string>& taken) {
  if (taken.find(desired) == taken.end()) {
    return desired;
  }
  std::string candidate = "_" + desired;
  while (taken.find(candidate) != taken.end()) {
    candidate = "_" + candidate;
  }
  std::cerr << "Warning: property \"" << desired << "\" collides with a reserved column; "
            << "writing it as \"" << candidate << "\" instead so the property value is not lost.\n";
  return candidate;
}

void writeCSVHeader(std::ofstream& file, const std::set<std::string>& property_names,
                    bool include_geometry, bool include_type, const std::string& id_col,
                    const std::string& type_col, const std::string& geom_col) {
  file << id_col;
  for (const auto& prop : property_names) {
    file << "," << prop;
  }
  if (include_type) {
    file << "," << type_col;
  }
  if (include_geometry) {
    file << "," << geom_col;
  }
  file << "\n";
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

void writeCSVRow(std::ofstream& file, const std::string& id, const json& properties,
                 const std::set<std::string>& property_names, const std::string& geometry_wkt,
                 const std::string& geometry_type, bool include_geometry, bool include_type) {
  file << escapeCSV(id);

  for (const auto& prop_name : property_names) {
    file << ",";
    if (properties.contains(prop_name)) {
      const auto& value = properties[prop_name];
      if (value.is_string()) {
        file << escapeCSV(value.get<std::string>());
      } else if (value.is_number_integer()) {
        file << value.get<int>();
      } else if (value.is_number_float()) {
        // max_digits10 (17 for double) is the number of significant digits
        // needed to round-trip a double exactly; the previous fixed 6
        // decimals silently truncated attribute values like population
        // density or measurement fields, not just geometry coordinates.
        std::ostringstream oss;
        oss << std::setprecision(std::numeric_limits<double>::max_digits10) << value.get<double>();
        file << oss.str();
      } else if (value.is_boolean()) {
        file << (value.get<bool>() ? "true" : "false");
      } else if (value.is_null()) {
        file << "";
      } else {
        file << escapeCSV(value.dump());
      }
    } else {
      file << "";
    }
  }

  if (include_type) {
    file << "," << geometry_type;
  }

  if (include_geometry) {
    file << "," << escapeCSV(geometry_wkt);
  }

  file << "\n";
}

void convertGeoJSONToCSV(const std::string& input_file, const std::string& output_file,
                         bool include_geometry = true, bool include_type = false,
                         const std::set<std::string>& include_props = {}) {
  std::ifstream file(input_file);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open input file: " << input_file << "\n";
    return;
  }

  json geojson;
  try {
    file >> geojson;
  } catch (const json::parse_error& e) {
    std::cerr << "Error: Invalid JSON: " << e.what() << "\n";
    return;
  }

  if (!geojson.contains("type")) {
    std::cerr << "Error: Invalid GeoJSON - missing 'type'\n";
    return;
  }

  std::string type = geojson["type"].get<std::string>();

  json features;
  json properties;

  if (type == "FeatureCollection") {
    if (!geojson.contains("features")) {
      std::cerr << "Error: Invalid FeatureCollection - missing 'features'\n";
      return;
    }
    features = geojson["features"];
  } else if (type == "Feature") {
    features = json::array({geojson});
  } else {
    json feature;
    feature["type"] = "Feature";
    feature["geometry"] = geojson;
    feature["properties"] = json::object();
    features = json::array({feature});
  }

  std::string crs;
  if (geojson.contains("crs")) {
    const auto& crs_obj = geojson["crs"];
    if (crs_obj.contains("properties") && crs_obj["properties"].contains("name")) {
      crs = crs_obj["properties"]["name"].get<std::string>();
    }
  }

  std::set<std::string> property_names;
  for (const auto& feature : features) {
    if (feature.contains("properties")) {
      for (const auto& [key, value] : feature["properties"].items()) {
        if (include_props.empty() || include_props.find(key) != include_props.end()) {
          property_names.insert(key);
        }
      }
    }
  }

  // The reserved/structural columns ("id" for the feature identifier, plus
  // optionally "geometry_type" and "geometry") must not collide with a real
  // property name -- see uniqueReservedName() for why.
  std::string id_col = uniqueReservedName("id", property_names);
  std::string type_col = include_type ? uniqueReservedName("geometry_type", property_names) : "";
  std::string geom_col = include_geometry ? uniqueReservedName("geometry", property_names) : "";

  std::ofstream out_file(output_file);
  if (!out_file.is_open()) {
    std::cerr << "Error: Could not create output file: " << output_file << "\n";
    return;
  }

  if (!crs.empty()) {
    out_file << "# CRS: " << crs << "\n";
  }
  out_file << "# Geometry column: " << (geom_col.empty() ? "geometry" : geom_col) << "\n";
  out_file << "# Converted from GeoJSON\n";
  out_file << "# Features: " << features.size() << "\n";

  writeCSVHeader(out_file, property_names, include_geometry, include_type, id_col, type_col,
                 geom_col);

  int sequential_id = 0;
  for (const auto& feature : features) {
    sequential_id++;

    std::string geometry_wkt;
    std::string geometry_type;

    if (feature.contains("geometry") && !feature["geometry"].is_null()) {
      const auto& geometry = feature["geometry"];
      if (geometry.contains("type") && geometry.contains("coordinates")) {
        geometry_type = geometry["type"].get<std::string>();
        geometry_wkt = coordinatesToWKT(geometry["coordinates"], geometry_type);
      }
    }

    json props;
    if (feature.contains("properties") && !feature["properties"].is_null()) {
      props = feature["properties"];
    }

    // GeoJSON Features may carry a top-level "id" member (RFC 7946 §3.2),
    // separate from "properties". Previously this was ignored entirely and
    // replaced by a throwaway sequential counter. Preserve it when present;
    // fall back to the sequential counter only when the feature has none.
    std::string row_id;
    if (feature.contains("id") && !feature["id"].is_null()) {
      const auto& fid = feature["id"];
      row_id = fid.is_string() ? fid.get<std::string>() : fid.dump();
    } else {
      row_id = std::to_string(sequential_id);
    }

    writeCSVRow(out_file, row_id, props, property_names, geometry_wkt, geometry_type,
                include_geometry, include_type);
  }

  out_file.close();
  std::cout << "Converted " << features.size() << " features to CSV\n";
  std::cout << "Output written to: " << output_file << "\n";
}

void printUsage() {
  std::cerr << "geojson2csv - Convert GeoJSON to spatial CSV\n\n";
  std::cerr << "Usage: geojson2csv <input.geojson> <output.csv> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -no_geometry        Exclude geometry column\n";
  std::cerr << "  -include_type       Include geometry type column\n";
  std::cerr << "  -props <list>       Only include specified properties (comma-separated)\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  geojson2csv data.geojson data.csv\n";
  std::cerr << "  geojson2csv data.geojson data.csv -props id,name,population\n";
  std::cerr << "  geojson2csv data.geojson data.csv -include_type\n";
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
  std::set<std::string> include_props;

  for (int i = 3; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-no_geometry") {
      include_geometry = false;
    } else if (arg == "-include_type") {
      include_type = true;
    } else if (arg == "-props" && i + 1 < argc) {
      std::string props_str = argv[++i];
      auto tokens = split(props_str, ',');
      for (const auto& t : tokens) {
        std::string prop = trim(t);
        if (!prop.empty()) {
          include_props.insert(prop);
        }
      }
    } else {
      std::cerr << "Error: Unknown option: " << arg << "\n";
      return 1;
    }
  }

  std::cout << "========================================\n";
  std::cout << "  GEOJSON TO CSV CONVERTER\n";
  std::cout << "========================================\n\n";
  std::cout << "Input: " << input_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  if (!include_props.empty()) {
    std::cout << "Properties: ";
    for (const auto& p : include_props) {
      std::cout << p << " ";
    }
    std::cout << "\n";
  }
  std::cout << "\n";

  if (!std::filesystem::exists(input_file)) {
    std::cerr << "Error: Input file not found: " << input_file << "\n";
    return 1;
  }

  convertGeoJSONToCSV(input_file, output_file, include_geometry, include_type, include_props);

  std::cout << "\n========================================\n";
  std::cout << "  CONVERSION COMPLETE\n";
  std::cout << "========================================\n";

  return 0;
}
