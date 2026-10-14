#include <algorithm>
#include <iostream>
#include <limits>
#include <string>

#include "../include/core/spatial_csv.hpp"
#include "../include/core/spatial_io.hpp"
#include "../include/core/spatial_projection.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

void printUsage() {
  std::cerr << "spatial_reproject - Convert vector coordinates between coordinate systems\n\n";
  std::cerr << "Usage: spatial_reproject <input.csv> <output.csv> -from <system> -to <system> "
               "[options]\n\n";
  std::cerr << "Supported systems (case-insensitive):\n";
  std::cerr << "  wgs84         Geographic WGS84 (EPSG:4326) -- coordinates as lon,lat degrees\n";
  std::cerr << "  crtm05        CR05 / CRTM05 (EPSG:5367) -- Costa Rica's official system\n";
  std::cerr << "  utm16n        WGS 84 / UTM zone 16N (EPSG:32616)\n";
  std::cerr << "  utm17n        WGS 84 / UTM zone 17N (EPSG:32617)\n";
  std::cerr << "  webmercator   WGS 84 / Pseudo-Mercator (EPSG:3857) -- OSM/Google Maps tiles\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -from <system>   Coordinate system of the input file (required)\n";
  std::cerr << "  -to <system>     Coordinate system of the output file (required)\n";
  std::cerr << "  -verbose         Show detailed statistics\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_reproject cities.csv cities_crtm05.csv -from wgs84 -to crtm05\n";
  std::cerr << "  spatial_reproject cities_crtm05.csv cities.csv -from crtm05 -to wgs84\n";
  std::cerr << "  spatial_reproject roads.csv roads_utm.csv -from wgs84 -to utm17n\n";
  std::cerr << "  spatial_reproject cities.csv cities_web.csv -from wgs84 -to webmercator\n\n";
  std::cerr << "Note: every coordinate pair of every feature is converted (POINT, LINESTRING,\n";
  std::cerr << "POLYGON and their MULTI* variants all work the same way, part by part). All\n";
  std::cerr << "projected systems (crtm05/utm16n/utm17n/webmercator) go through geographic\n";
  std::cerr << "WGS84 as an intermediate step, so converting between two projected systems\n";
  std::cerr << "(e.g. -from utm16n -to crtm05) works too.\n";
}

bool parseSystemArg(const std::string& raw, const char* flag_name, Spatial::ProjSystem& out) {
  std::string lowered = toLower(trim(raw));
  if (!Spatial::parseProjSystem(lowered, out)) {
    std::cerr << "Error: Unknown " << flag_name << " system: " << raw << "\n";
    std::cerr << "Supported: wgs84, crtm05, utm16n, utm17n, webmercator\n";
    return false;
  }
  return true;
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printUsage();
    return 1;
  }

  std::string input_file;
  std::string output_file;
  std::string from_arg, to_arg;
  bool verbose = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-from" && i + 1 < argc) {
      from_arg = argv[++i];
    } else if (arg == "-to" && i + 1 < argc) {
      to_arg = argv[++i];
    } else if (arg == "-verbose") {
      verbose = true;
    } else if (arg == "-help" || arg == "-h") {
      printUsage();
      return 0;
    } else if (input_file.empty()) {
      input_file = arg;
    } else if (output_file.empty()) {
      output_file = arg;
    } else {
      std::cerr << "Error: Unknown option: " << arg << "\n";
      printUsage();
      return 1;
    }
  }

  if (input_file.empty() || output_file.empty()) {
    std::cerr << "Error: Input and output files required\n";
    printUsage();
    return 1;
  }
  if (from_arg.empty() || to_arg.empty()) {
    std::cerr << "Error: -from and -to are both required\n";
    printUsage();
    return 1;
  }

  Spatial::ProjSystem from_sys, to_sys;
  if (!parseSystemArg(from_arg, "-from", from_sys)) return 1;
  if (!parseSystemArg(to_arg, "-to", to_sys)) return 1;

  std::cout << "========================================\n";
  std::cout << "  SPATIAL REPROJECT\n";
  std::cout << "========================================\n\n";
  std::cout << "Input: " << input_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "From: " << Spatial::projSystemLabel(from_sys) << "\n";
  std::cout << "To: " << Spatial::projSystemLabel(to_sys) << "\n\n";

  Spatial::SpatialCSVReader reader;
  Spatial::VectorDataset dataset;
  if (!reader.read(input_file, dataset)) {
    std::cerr << "Error: Could not read input file\n";
    return 1;
  }

  std::cout << "Input features: " << dataset.features.size() << "\n";

  if (from_sys == to_sys) {
    std::cout << "Note: -from and -to are the same system, coordinates are copied unchanged.\n";
  }

  Spatial::VectorDataset output = dataset;
  output.crs = Spatial::projSystemLabel(to_sys);

  double min_x = std::numeric_limits<double>::max();
  double min_y = std::numeric_limits<double>::max();
  double max_x = std::numeric_limits<double>::lowest();
  double max_y = std::numeric_limits<double>::lowest();
  size_t points_converted = 0;

  for (auto& feature : output.features) {
    for (size_t i = 0; i + 1 < feature.coordinates.size(); i += 2) {
      double out_x, out_y;
      Spatial::reprojectPoint(feature.coordinates[i], feature.coordinates[i + 1], from_sys, to_sys,
                              out_x, out_y);
      feature.coordinates[i] = out_x;
      feature.coordinates[i + 1] = out_y;

      min_x = std::min(min_x, out_x);
      min_y = std::min(min_y, out_y);
      max_x = std::max(max_x, out_x);
      max_y = std::max(max_y, out_y);
      ++points_converted;
    }
  }

  if (points_converted > 0) {
    output.min_x = min_x;
    output.min_y = min_y;
    output.max_x = max_x;
    output.max_y = max_y;
    output.has_bbox = true;
  }

  writeVectorCSV(output, output_file,
                 {"# Reprojected: " + Spatial::projSystemLabel(from_sys) + " -> " +
                  Spatial::projSystemLabel(to_sys)});

  std::cout << "\n========================================\n";
  std::cout << "  REPROJECT COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Features processed: " << output.features.size() << "\n";
  std::cout << "Coordinate pairs converted: " << points_converted << "\n";

  if (points_converted > 0) {
    std::cout << "\nOutput extent:\n";
    std::cout << "  Min X: " << min_x << "\n";
    std::cout << "  Min Y: " << min_y << "\n";
    std::cout << "  Max X: " << max_x << "\n";
    std::cout << "  Max Y: " << max_y << "\n";
  }

  std::cout << "\nOutput written to: " << output_file << "\n";

  if (verbose) {
    std::cout << "\nColumns in output:\n";
    for (const auto& col : output.columns) {
      std::cout << "  - " << col << "\n";
    }
  }

  return 0;
}
