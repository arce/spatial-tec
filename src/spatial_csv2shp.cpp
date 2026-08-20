#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "../include/core/spatial_csv.hpp"
#include "../include/core/spatial_shp.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

void printUsage() {
  std::cerr << "spatial_csv2shp - Convert a spatial CSV to Shapefile (.shp + .shx + .dbf)\n\n";
  std::cerr << "Usage: spatial_csv2shp <input.csv> <output.shp> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -geometry_column <col>   Name of the geometry column (default: auto-detected)\n";
  std::cerr << "  -exclude <list>          Exclude these attribute columns from the .dbf, comma-separated\n";
  std::cerr << "  -verbose                 Show detailed information\n\n";
  std::cerr << "Notes:\n";
  std::cerr << "  A Shapefile can only hold one geometry family per file (point, line or\n";
  std::cerr << "  polygon). The family is taken from the first feature with geometry; any\n";
  std::cerr << "  feature of a different family is skipped and reported at the end.\n";
  std::cerr << "  DBF field names are truncated to 10 characters (dBase III limit) and\n";
  std::cerr << "  de-duplicated with a numeric suffix if the truncation collides.\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_csv2shp cities.csv cities.shp\n";
  std::cerr << "  spatial_csv2shp parcels.csv parcels.shp -exclude internal_id\n";
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printUsage();
    return 1;
  }

  std::string input_file = argv[1];
  std::string output_file = argv[2];
  std::string geometry_column;
  std::set<std::string> exclude_cols;
  bool verbose = false;

  for (int i = 3; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-geometry_column" && i + 1 < argc) {
      geometry_column = argv[++i];
    } else if (arg == "-exclude" && i + 1 < argc) {
      for (const auto& t : split(argv[++i], ',')) {
        std::string col = trim(t);
        if (!col.empty()) exclude_cols.insert(col);
      }
    } else if (arg == "-verbose") {
      verbose = true;
    } else {
      std::cerr << "Error: Unknown option: " << arg << "\n";
      return 1;
    }
  }

  std::cout << "========================================\n";
  std::cout << "  CSV TO SHAPEFILE CONVERTER\n";
  std::cout << "========================================\n\n";
  std::cout << "Input: " << input_file << "\n";
  std::cout << "Output: " << output_file << "\n\n";

  Spatial::SpatialCSVReader reader;
  Spatial::VectorDataset dataset;
  if (!reader.read(input_file, dataset)) {
    std::cerr << "Error: Could not read input file: " << input_file << "\n";
    return 1;
  }

  if (!geometry_column.empty() && geometry_column != dataset.geometry_column) {
    bool found = false;
    for (const auto& c : dataset.columns) {
      if (c == geometry_column) found = true;
    }
    if (found) {
      dataset.geometry_column = geometry_column;
    } else {
      std::cerr << "Warning: column \"" << geometry_column << "\" not found, using auto-detected \""
                << dataset.geometry_column << "\" instead.\n";
    }
  }

  if (!exclude_cols.empty()) {
    for (auto& feature : dataset.features) {
      for (const auto& col : exclude_cols) feature.attributes.erase(col);
    }
    std::vector<std::string> filtered_cols;
    for (const auto& c : dataset.columns) {
      if (c == dataset.geometry_column || !exclude_cols.count(c)) filtered_cols.push_back(c);
    }
    dataset.columns = filtered_cols;
  }

  std::cout << "Features read: " << dataset.features.size() << "\n";
  std::cout << "Geometry column: " << dataset.geometry_column << "\n\n";

  std::string error;
  Spatial::ShapefileWriteResult result = Spatial::writeShapefile(dataset, output_file, &error);
  if (!result.ok) {
    std::cerr << "Error: " << error << "\n";
    return 1;
  }

  std::string base = output_file;
  if (base.size() >= 4 && base.substr(base.size() - 4) == ".shp") {
    base = base.substr(0, base.size() - 4);
  }

  std::cout << "========================================\n";
  std::cout << "  CONVERSION COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Geometry family written: " << result.target_family << "\n";
  std::cout << "Features written: " << result.written << "\n";
  if (result.skipped_wrong_family > 0) {
    std::cerr << "Warning: " << result.skipped_wrong_family
              << " feature(s) skipped -- their geometry family did not match \"" << result.target_family
              << "\" (a Shapefile can only hold one geometry family per file).\n";
  }
  std::cout << "Output written to: " << base << ".shp, " << base << ".shx, " << base << ".dbf\n";

  if (verbose) {
    std::cout << "\nDBF field names are limited to 10 characters; check the output .dbf if any\n";
    std::cout << "column name looked truncated or renamed.\n";
  }

  return 0;
}
