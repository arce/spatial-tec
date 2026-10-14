#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

#include "../include/core/ascii_grid.hpp"
#include "../include/core/spatial_csv.hpp"
#include "../include/core/spatial_geom.hpp"
#include "../include/core/spatial_io.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

struct BBox {
  double minx, miny, maxx, maxy;
  BBox() : minx(0), miny(0), maxx(0), maxy(0) {}
  BBox(double x1, double y1, double x2, double y2)
      : minx(std::min(x1, x2)),
        miny(std::min(y1, y2)),
        maxx(std::max(x1, x2)),
        maxy(std::max(y1, y2)) {}

  bool contains(double x, double y) const {
    return x >= minx && x <= maxx && y >= miny && y <= maxy;
  }
};

BBox getVectorExtent(const Spatial::VectorDataset& dataset) {
  BBox extent;
  bool first = true;

  for (const auto& feature : dataset.features) {
    for (size_t i = 0; i < feature.coordinates.size(); i += 2) {
      double x = feature.coordinates[i];
      double y = feature.coordinates[i + 1];
      if (first) {
        extent.minx = extent.maxx = x;
        extent.miny = extent.maxy = y;
        first = false;
      } else {
        extent.minx = std::min(extent.minx, x);
        extent.miny = std::min(extent.miny, y);
        extent.maxx = std::max(extent.maxx, x);
        extent.maxy = std::max(extent.maxy, y);
      }
    }
  }

  return extent;
}

void printUsage() {
  std::cerr << "spatial_rasterize - Convert vector data to raster\n\n";
  std::cerr << "Usage: spatial_rasterize <input> <output> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -cellsize <value>    Cell size of output raster\n";
  std::cerr << "  -attribute <col>     Attribute column to use for values\n";
  std::cerr << "  -value <value>       Constant value for all cells\n";
  std::cerr << "  -bbox <minx> <miny> <maxx> <maxy>  Output extent\n";
  std::cerr << "  -nodata <value>      NODATA value (default: -9999)\n";
  std::cerr << "\nExamples:\n";
  std::cerr << "  spatial_rasterize cities.csv density.asc -attribute population -cellsize 0.01\n";
  std::cerr << "  spatial_rasterize cities.csv mask.asc -value 1 -cellsize 0.01\n";
  std::cerr << "  spatial_rasterize zones.csv zones.asc -attribute id -cellsize 0.005\n";
}

int main(int argc, char* argv[]) {
  if (argc < 4) {
    printUsage();
    return 1;
  }

  std::string input_file;
  std::string output_file;
  double cellsize = 0;
  std::string attribute;
  double const_value = 0;
  bool use_const = false;
  BBox bbox;
  bool use_bbox = false;
  double nodata = -9999.0;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-cellsize" && i + 1 < argc) {
      cellsize = std::stod(argv[++i]);
    } else if (arg == "-attribute" && i + 1 < argc) {
      attribute = argv[++i];
    } else if (arg == "-value" && i + 1 < argc) {
      const_value = std::stod(argv[++i]);
      use_const = true;
    } else if (arg == "-bbox" && i + 4 < argc) {
      bbox = BBox(std::stod(argv[++i]), std::stod(argv[++i]), std::stod(argv[++i]),
                  std::stod(argv[++i]));
      use_bbox = true;
    } else if (arg == "-nodata" && i + 1 < argc) {
      nodata = std::stod(argv[++i]);
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

  if (cellsize <= 0) {
    std::cerr << "Error: Cell size must be positive\n";
    return 1;
  }

  if (!use_const && attribute.empty()) {
    std::cerr << "Error: Either -attribute or -value must be specified\n";
    return 1;
  }

  std::cout << "Input: " << input_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Cell size: " << cellsize << "\n";
  if (use_const) {
    std::cout << "Value: " << const_value << " (constant)\n";
  } else {
    std::cout << "Attribute: " << attribute << "\n";
  }
  if (use_bbox) {
    std::cout << "Extent: " << bbox.minx << " " << bbox.miny << " " << bbox.maxx << " " << bbox.maxy
              << "\n";
  }
  std::cout << "\n";

  Spatial::SpatialCSVReader reader;
  Spatial::VectorDataset dataset;

  if (!reader.read(input_file, dataset)) {
    std::cerr << "Error: Could not read input file\n";
    return 1;
  }

  std::cout << "Input features: " << dataset.features.size() << "\n";

  if (!use_bbox) {
    bbox = getVectorExtent(dataset);

    double margin = cellsize * 2;
    bbox.minx -= margin;
    bbox.miny -= margin;
    bbox.maxx += margin;
    bbox.maxy += margin;
    std::cout << "Auto-extent: " << bbox.minx << " " << bbox.miny << " " << bbox.maxx << " "
              << bbox.maxy << "\n";
  }

  int ncols = static_cast<int>(std::ceil((bbox.maxx - bbox.minx) / cellsize));
  int nrows = static_cast<int>(std::ceil((bbox.maxy - bbox.miny) / cellsize));

  std::cout << "Output dimensions: " << ncols << "x" << nrows << "\n";
  std::cout << "Total cells: " << (ncols * nrows) << "\n\n";

  Spatial::RasterDataset output;
  output.ncols = ncols;
  output.nrows = nrows;
  output.xllcorner = bbox.minx;
  output.yllcorner = bbox.miny;
  output.cellsize = cellsize;
  output.nodata_value = nodata;
  output.data.resize(nrows * ncols, nodata);

  std::cout << "Rasterizing...\n";
  int processed = 0;

  for (int r = 0; r < nrows; ++r) {
    for (int c = 0; c < ncols; ++c) {
      double x = output.xllcorner + c * output.cellsize + output.cellsize / 2.0;
      double y = output.yllcorner + (nrows - 1 - r) * output.cellsize + output.cellsize / 2.0;

      for (const auto& feature : dataset.features) {
        bool covers = false;
        double value = 0;

        switch (feature.type) {
          case Spatial::VectorFeature::GeometryType::POINT: {
            double dx = x - feature.coordinates[0];
            double dy = y - feature.coordinates[1];
            if (dx * dx + dy * dy < cellsize * cellsize / 4) {
              covers = true;
              if (use_const) {
                value = const_value;
              } else {
                auto it = feature.attributes.find(attribute);
                if (it != feature.attributes.end()) {
                  value = std::stod(it->second);
                }
              }
            }
            break;
          }
          case Spatial::VectorFeature::GeometryType::LINESTRING: {
            for (size_t i = 0; i < feature.coordinates.size() - 2; i += 2) {
              double x1 = feature.coordinates[i];
              double y1 = feature.coordinates[i + 1];
              double x2 = feature.coordinates[i + 2];
              double y2 = feature.coordinates[i + 3];

              double dx = x2 - x1;
              double dy = y2 - y1;
              double len2 = dx * dx + dy * dy;
              if (len2 < 1e-12)
                continue;

              double t = ((x - x1) * dx + (y - y1) * dy) / len2;
              t = std::max(0.0, std::min(1.0, t));

              double px = x1 + t * dx;
              double py = y1 + t * dy;
              double dist2 = (x - px) * (x - px) + (y - py) * (y - py);

              if (dist2 < cellsize * cellsize / 4) {
                covers = true;
                if (use_const) {
                  value = const_value;
                } else {
                  auto it = feature.attributes.find(attribute);
                  if (it != feature.attributes.end()) {
                    value = std::stod(it->second);
                  }
                }
                break;
              }
            }
            break;
          }
          case Spatial::VectorFeature::GeometryType::POLYGON: {
            if (pointInPolygon(x, y, feature.coordinates)) {
              covers = true;
              if (use_const) {
                value = const_value;
              } else {
                auto it = feature.attributes.find(attribute);
                if (it != feature.attributes.end()) {
                  value = std::stod(it->second);
                }
              }
            }
            break;
          }
        }

        if (covers) {
          output.at(r, c) = value;
          processed++;
          break;
        }
      }
    }

    if ((r + 1) % 100 == 0) {
      std::cout << "\r  Progress: " << ((r + 1) * 100 / nrows) << "%" << std::flush;
    }
  }

  std::cout << "\r  Progress: 100%\n";
  std::cout << "Cells with data: " << processed << "\n";
  std::cout << "NODATA cells: " << (ncols * nrows - processed) << "\n";

  writeRasterASCII(output, output_file);

  std::cout << "\n========================================\n";
  std::cout << "  RASTERIZATION COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Output written to: " << output_file << "\n";

  return 0;
}

