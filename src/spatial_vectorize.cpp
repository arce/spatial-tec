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

#include "../include/core/ascii_grid.hpp"
#include "../include/core/spatial_csv.hpp"
#include "../include/core/spatial_geom.hpp"
#include "../include/core/spatial_io.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

struct Point2D {
  double x, y;
  double value;
  Point2D() : x(0), y(0), value(0) {}
  Point2D(double x, double y, double v = 0) : x(x), y(y), value(v) {}
};

void printUsage() {
  std::cerr << "spatial_vectorize - Convert raster to vector data\n\n";
  std::cerr << "Usage: spatial_vectorize <input> <output> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -contour -interval <value>   Extract contours at intervals\n";
  std::cerr << "  -contour -values <list>      Extract contours at specific values\n";
  std::cerr << "  -polygonize                  Convert to polygons\n";
  std::cerr << "  -points                      Convert to points with values\n";
  std::cerr << "  -filter <value>              Only include cells with value\n";
  std::cerr << "\nExamples:\n";
  std::cerr << "  spatial_vectorize elevation.asc curves.csv -contour -interval 10\n";
  std::cerr << "  spatial_vectorize classification.asc polygons.csv -polygonize\n";
  std::cerr << "  spatial_vectorize elevation.asc points.csv -points\n";
}

int main(int argc, char* argv[]) {
  if (argc < 4) {
    printUsage();
    return 1;
  }

  std::string input_file;
  std::string output_file;
  std::string mode;
  double contour_interval = 0;
  std::vector<double> contour_values;
  double filter_value = 0;
  bool use_filter = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-contour") {
      mode = "contour";
    } else if (arg == "-interval" && i + 1 < argc) {
      contour_interval = std::stod(argv[++i]);
    } else if (arg == "-values" && i + 1 < argc) {
      std::string values_str = argv[++i];
      auto tokens = split(values_str, ',');
      for (const auto& t : tokens) {
        contour_values.push_back(std::stod(trim(t)));
      }
    } else if (arg == "-polygonize") {
      mode = "polygonize";
    } else if (arg == "-points") {
      mode = "points";
    } else if (arg == "-filter" && i + 1 < argc) {
      filter_value = std::stod(argv[++i]);
      use_filter = true;
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

  if (mode.empty()) {
    std::cerr << "Error: Must specify -contour, -polygonize, or -points\n";
    return 1;
  }

  if (mode == "contour" && contour_interval <= 0 && contour_values.empty()) {
    std::cerr << "Error: -contour requires -interval or -values\n";
    return 1;
  }

  std::cout << "Input: " << input_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Mode: " << mode << "\n";
  if (mode == "contour") {
    if (!contour_values.empty()) {
      std::cout << "Values: ";
      for (double v : contour_values) std::cout << v << " ";
      std::cout << "\n";
    } else {
      std::cout << "Interval: " << contour_interval << "\n";
    }
  }
  if (use_filter) {
    std::cout << "Filter: value=" << filter_value << "\n";
  }
  std::cout << "\n";

  Spatial::ASCIIGridReader reader;
  Spatial::RasterDataset dataset;

  if (!reader.read(input_file, dataset)) {
    std::cerr << "Error: Could not read input file\n";
    return 1;
  }

  std::cout << "Raster dimensions: " << dataset.ncols << "x" << dataset.nrows << "\n";
  std::cout << "Total cells: " << (dataset.ncols * dataset.nrows) << "\n\n";

  Spatial::VectorDataset output;
  output.columns = {"id", "value", "geometry"};
  output.geometry_column = "geometry";
  output.crs = dataset.crs;

  int feature_id = 0;

  if (mode == "points") {
    std::cout << "Converting to points...\n";
    int processed = 0;

    for (int r = 0; r < dataset.nrows; ++r) {
      for (int c = 0; c < dataset.ncols; ++c) {
        double val = dataset.at(r, c);
        if (std::abs(val - dataset.nodata_value) < 1e-9)
          continue;
        if (use_filter && std::abs(val - filter_value) > 1e-9)
          continue;

        double x = dataset.xllcorner + c * dataset.cellsize + dataset.cellsize / 2.0;
        double y =
            dataset.yllcorner + (dataset.nrows - 1 - r) * dataset.cellsize + dataset.cellsize / 2.0;

        Spatial::VectorFeature feature;
        feature.type = Spatial::VectorFeature::GeometryType::POINT;
        feature.coordinates = {x, y};
        feature.attributes["id"] = std::to_string(++feature_id);
        feature.attributes["value"] = std::to_string(val);
        output.features.push_back(feature);
        processed++;
      }
    }

    std::cout << "Points extracted: " << processed << "\n";

  } else if (mode == "polygonize") {
    std::cout << "Polygonizing (simplified)...\n";
    std::cout << "Note: Full polygonization requires complex algorithms.\n";
    std::cout << "      This is a simplified version for demonstration.\n\n";

    int processed = 0;

    for (int r = 0; r < dataset.nrows; ++r) {
      for (int c = 0; c < dataset.ncols; ++c) {
        double val = dataset.at(r, c);
        if (std::abs(val - dataset.nodata_value) < 1e-9)
          continue;
        if (use_filter && std::abs(val - filter_value) > 1e-9)
          continue;

        double x = dataset.xllcorner + c * dataset.cellsize;
        double y = dataset.yllcorner + (dataset.nrows - 1 - r) * dataset.cellsize;
        double cs = dataset.cellsize;

        Spatial::VectorFeature feature;
        feature.type = Spatial::VectorFeature::GeometryType::POLYGON;
        feature.coordinates = {x, y, x + cs, y, x + cs, y + cs, x, y + cs, x, y};
        feature.attributes["id"] = std::to_string(++feature_id);
        feature.attributes["value"] = std::to_string(val);
        output.features.push_back(feature);
        processed++;
      }
    }

    std::cout << "Polygons created: " << processed << "\n";
    std::cout << "Note: This creates individual cell polygons, not merged areas.\n";

  } else if (mode == "contour") {
    std::cout << "Extracting contours (simplified)...\n";
    std::cout << "Note: Full contour extraction requires marching squares algorithm.\n";
    std::cout << "      This is a simplified version for demonstration.\n\n";

    std::vector<double> values;
    if (!contour_values.empty()) {
      values = contour_values;
    } else {
      double min_val = dataset.min_val;
      double max_val = dataset.max_val;
      double start = std::floor(min_val / contour_interval) * contour_interval;
      for (double v = start; v <= max_val; v += contour_interval) {
        values.push_back(v);
      }
    }

    std::cout << "Contour levels: ";
    for (double v : values) std::cout << v << " ";
    std::cout << "\n";

    int processed = 0;

    for (double level : values) {
      for (int r = 0; r < dataset.nrows - 1; ++r) {
        for (int c = 0; c < dataset.ncols - 1; ++c) {
          double vals[4] = {dataset.at(r, c), dataset.at(r, c + 1), dataset.at(r + 1, c),
                            dataset.at(r + 1, c + 1)};

          bool above = false;
          bool below = false;
          for (int i = 0; i < 4; ++i) {
            if (vals[i] >= level)
              above = true;
            if (vals[i] < level)
              below = true;
          }

          if (above && below) {
            double cx = dataset.xllcorner + (c + 0.5) * dataset.cellsize;
            double cy = dataset.yllcorner + (dataset.nrows - 1 - r - 0.5) * dataset.cellsize;

            Spatial::VectorFeature feature;
            feature.type = Spatial::VectorFeature::GeometryType::POINT;
            feature.coordinates = {cx, cy};
            feature.attributes["id"] = std::to_string(++feature_id);
            feature.attributes["value"] = std::to_string(level);
            feature.attributes["type"] = "contour_point";
            output.features.push_back(feature);
            processed++;
          }
        }
      }
    }

    std::cout << "Contour points extracted: " << processed << "\n";
    std::cout << "Note: Full contours would connect these points into lines.\n";
  }

  output.feature_count = output.features.size();

  writeVectorCSV(
      output, output_file, {},
      {"# Vectorized from raster", "# Total features: " + std::to_string(output.features.size())});

  std::cout << "\n========================================\n";
  std::cout << "  VECTORIZATION COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Total features: " << output.features.size() << "\n";
  std::cout << "Output written to: " << output_file << "\n";

  return 0;
}
