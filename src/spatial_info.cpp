#include <filesystem>
#include <iomanip>
#include <iostream>

#include "../include/core/ascii_grid.hpp"
#include "../include/core/spatial_csv.hpp"
#include "../include/core/spatial_types.hpp"

void printVectorInfo(const std::string& filename, const Spatial::VectorDataset& dataset) {
  std::cout << "\n========================================\n";
  std::cout << "  VECTOR INFORMATION\n";
  std::cout << "========================================\n\n";

  std::cout << "File: " << filename << "\n";
  std::cout << "Format: CSV (vector)\n";
  if (!dataset.crs.empty())
    std::cout << "CRS: " << dataset.crs << "\n";
  std::cout << "Features: " << dataset.feature_count << "\n";
  std::cout << "Columns: " << dataset.columns.size() << "\n";
  std::cout << "  ";
  for (const auto& col : dataset.columns) {
    std::cout << "[" << col << "] ";
  }
  std::cout << "\n";
  std::cout << "Geometry column: " << dataset.geometry_column << "\n";

  std::cout << "\nGeometry types:\n";
  for (const auto& [type, count] : dataset.type_counts) {
    std::cout << "  " << std::setw(12) << type << ": " << count << "\n";
  }

  if (dataset.has_bbox) {
    std::cout << "\nExtent:\n";
    std::cout << "  Min X: " << std::fixed << std::setprecision(6) << dataset.min_x << "\n";
    std::cout << "  Min Y: " << std::fixed << std::setprecision(6) << dataset.min_y << "\n";
    std::cout << "  Max X: " << std::fixed << std::setprecision(6) << dataset.max_x << "\n";
    std::cout << "  Max Y: " << std::fixed << std::setprecision(6) << dataset.max_y << "\n";
  }
}

void printRasterInfo(const std::string& filename, const Spatial::RasterDataset& dataset) {
  std::cout << "\n========================================\n";
  std::cout << "  RASTER INFORMATION\n";
  std::cout << "========================================\n\n";

  std::cout << "File: " << filename << "\n";
  std::cout << "Format: ASCII Grid (raster)\n";
  if (!dataset.crs.empty())
    std::cout << "CRS: " << dataset.crs << "\n";

  std::cout << "\nDimensions:\n";
  std::cout << "  Columns: " << dataset.ncols << "\n";
  std::cout << "  Rows: " << dataset.nrows << "\n";
  std::cout << "  Total cells: " << (dataset.ncols * dataset.nrows) << "\n";

  std::cout << "\nGeoreferencing:\n";
  std::cout << "  XLLCorner: " << std::fixed << std::setprecision(6) << dataset.xllcorner << "\n";
  std::cout << "  YLLCorner: " << std::fixed << std::setprecision(6) << dataset.yllcorner << "\n";
  std::cout << "  Cell size: " << std::fixed << std::setprecision(6) << dataset.cellsize << "\n";
  std::cout << "  NODATA: " << dataset.nodata_value << "\n";

  if (dataset.has_stats) {
    std::cout << "\nStatistics:\n";
    std::cout << "  Min: " << std::fixed << std::setprecision(6) << dataset.min_val << "\n";
    std::cout << "  Max: " << std::fixed << std::setprecision(6) << dataset.max_val << "\n";
    std::cout << "  Mean: " << std::fixed << std::setprecision(6) << dataset.mean_val << "\n";
    std::cout << "  Sum: " << std::fixed << std::setprecision(2) << dataset.sum_val << "\n";
    std::cout << "  Valid cells: " << dataset.valid_cells << "\n";
    std::cout << "  NODATA cells: " << dataset.nodata_cells << "\n";
  }
}

void printUsage() {
  std::cerr << "spatial_info - Get information about spatial files\n\n";
  std::cerr << "Usage: spatial_info <file>\n\n";
  std::cerr << "Supported formats:\n";
  std::cerr << "  Vector: .csv (with WKT geometry)\n";
  std::cerr << "  Raster: .asc, .grd (Arc/Info ASCII Grid)\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_info cities.csv\n";
  std::cerr << "  spatial_info elevation.asc\n";
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    printUsage();
    return 1;
  }

  std::string filename = argv[1];
  std::string ext = std::filesystem::path(filename).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  if (ext == ".csv" || ext == ".tsv") {
    Spatial::SpatialCSVReader reader;
    Spatial::VectorDataset dataset;
    if (reader.read(filename, dataset)) {
      printVectorInfo(filename, dataset);
    } else {
      std::cerr << "Error: Could not read file\n";
      return 1;
    }
  } else if (ext == ".asc" || ext == ".grd") {
    Spatial::ASCIIGridReader reader;
    Spatial::RasterDataset dataset;
    if (reader.read(filename, dataset)) {
      printRasterInfo(filename, dataset);
    } else {
      std::cerr << "Error: Could not read file\n";
      return 1;
    }
  } else {
    std::cerr << "Error: Unsupported format: " << ext << "\n";
    std::cerr << "Supported: .csv (vector), .asc/.grd (raster)\n";
    return 1;
  }

  return 0;
}

