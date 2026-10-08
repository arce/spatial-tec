#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
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

  bool intersects(const BBox& other) const {
    return !(other.maxx < minx || other.minx > maxx || other.maxy < miny || other.miny > maxy);
  }

  BBox intersection(const BBox& other) const {
    return BBox(std::max(minx, other.minx), std::max(miny, other.miny), std::min(maxx, other.maxx),
                std::min(maxy, other.maxy));
  }
};

std::vector<std::vector<double>> readPolygonsFromCSV(const std::string& filename) {
  std::vector<std::vector<double>> polygons;

  Spatial::SpatialCSVReader reader;
  Spatial::VectorDataset dataset;
  if (!reader.read(filename, dataset)) {
    return polygons;
  }

  for (const auto& feature : dataset.features) {
    if (feature.type != Spatial::VectorFeature::GeometryType::POLYGON &&
        feature.type != Spatial::VectorFeature::GeometryType::MULTIPOLYGON) {
      continue;
    }
    for (const auto& range : featurePartRanges(feature)) {
      std::vector<double> ring;
      ring.reserve((range.second - range.first) * 2);
      for (size_t p = range.first; p < range.second; ++p) {
        ring.push_back(feature.coordinates[p * 2]);
        ring.push_back(feature.coordinates[p * 2 + 1]);
      }
      if (!ring.empty()) {
        polygons.push_back(std::move(ring));
      }
    }
  }

  return polygons;
}

void clipRasterByBBox(const Spatial::RasterDataset& input, Spatial::RasterDataset& output,
                      const BBox& bbox) {
  BBox raster_extent(input.xllcorner, input.yllcorner,
                     input.xllcorner + input.ncols * input.cellsize,
                     input.yllcorner + input.nrows * input.cellsize);

  if (!bbox.intersects(raster_extent)) {
    std::cerr << "Error: BBox does not intersect raster\n";
    output = input;
    return;
  }

  BBox clipped = bbox.intersection(raster_extent);

  int start_col = static_cast<int>((clipped.minx - input.xllcorner) / input.cellsize);
  int start_row = static_cast<int>((input.yllcorner + input.nrows * input.cellsize - clipped.maxy) /
                                   input.cellsize);
  int end_col = static_cast<int>((clipped.maxx - input.xllcorner) / input.cellsize);
  int end_row = static_cast<int>((input.yllcorner + input.nrows * input.cellsize - clipped.miny) /
                                 input.cellsize);

  start_col = std::max(0, start_col);
  start_row = std::max(0, start_row);
  end_col = std::min(input.ncols - 1, end_col);
  end_row = std::min(input.nrows - 1, end_row);

  int new_ncols = end_col - start_col + 1;
  int new_nrows = end_row - start_row + 1;

  output.ncols = new_ncols;
  output.nrows = new_nrows;
  output.xllcorner = input.xllcorner + start_col * input.cellsize;
  output.yllcorner = input.yllcorner + (input.nrows - end_row - 1) * input.cellsize;
  output.cellsize = input.cellsize;
  output.nodata_value = input.nodata_value;
  output.crs = input.crs;
  output.data.resize(new_nrows * new_ncols);

  for (int r = 0; r < new_nrows; ++r) {
    for (int c = 0; c < new_ncols; ++c) {
      int src_r = start_row + r;
      int src_c = start_col + c;
      output.at(r, c) = input.at(src_r, src_c);
    }
  }
}

void clipRasterByPolygon(const Spatial::RasterDataset& input, Spatial::RasterDataset& output,
                         const std::vector<std::vector<double>>& polygons, bool invert = false) {
  BBox poly_bbox;
  bool first = true;
  for (const auto& polygon : polygons) {
    for (size_t i = 0; i < polygon.size(); i += 2) {
      double x = polygon[i];
      double y = polygon[i + 1];
      if (first) {
        poly_bbox.minx = poly_bbox.maxx = x;
        poly_bbox.miny = poly_bbox.maxy = y;
        first = false;
      } else {
        poly_bbox.minx = std::min(poly_bbox.minx, x);
        poly_bbox.miny = std::min(poly_bbox.miny, y);
        poly_bbox.maxx = std::max(poly_bbox.maxx, x);
        poly_bbox.maxy = std::max(poly_bbox.maxy, y);
      }
    }
  }

  Spatial::RasterDataset temp;
  clipRasterByBBox(input, temp, poly_bbox);

  output = temp;
  int masked = 0;

  for (int r = 0; r < output.nrows; ++r) {
    for (int c = 0; c < output.ncols; ++c) {
      double x = output.xllcorner + c * output.cellsize + output.cellsize / 2.0;
      double y =
          output.yllcorner + (output.nrows - 1 - r) * output.cellsize + output.cellsize / 2.0;

      bool inside = false;
      for (const auto& polygon : polygons) {
        if (pointInPolygon(x, y, polygon)) {
          inside = true;
          break;
        }
      }

      if (invert ? inside : !inside) {
        output.at(r, c) = output.nodata_value;
        masked++;
      }
    }
  }

  std::cout << "Masked cells: " << masked << "\n";
}

void printUsage() {
  std::cerr << "spatial_clip_raster - Clip raster data by spatial extent\n\n";
  std::cerr << "Usage: spatial_clip_raster <input> <output> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -bbox <minx> <miny> <maxx> <maxy>   Clip by bounding box\n";
  std::cerr << "  -polygon <file>                     Clip by polygon(s) from CSV (mask) -- every\n";
  std::cerr << "                                       POLYGON/MULTIPOLYGON row is tested\n";
  std::cerr << "                                       independently, a cell inside any one of\n";
  std::cerr << "                                       them is kept\n";
  std::cerr << "  -clip_to <file>                     Clip to extent of another raster\n";
  std::cerr << "  -invert                            Invert mask\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_clip_raster elevation.asc area.asc -bbox -84.5 9.5 -84.0 10.0\n";
  std::cerr << "  spatial_clip_raster elevation.asc mask.asc -polygon boundary.csv\n";
  std::cerr << "  spatial_clip_raster elevation.asc matched.asc -clip_to other.asc\n";
}

int main(int argc, char* argv[]) {
  if (argc < 4) {
    printUsage();
    return 1;
  }

  std::string input_file = argv[1];
  std::string output_file = argv[2];

  BBox bbox;
  std::string polygon_file;
  std::string reference_file;
  bool use_bbox = false;
  bool use_polygon = false;
  bool use_clip_to = false;
  bool invert = false;

  for (int i = 3; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-bbox" && i + 4 < argc) {
      bbox = BBox(std::stod(argv[i + 1]), std::stod(argv[i + 2]), std::stod(argv[i + 3]),
                  std::stod(argv[i + 4]));
      use_bbox = true;
      i += 4;
    } else if (arg == "-polygon" && i + 1 < argc) {
      polygon_file = argv[i + 1];
      use_polygon = true;
      i += 1;
    } else if (arg == "-clip_to" && i + 1 < argc) {
      reference_file = argv[i + 1];
      use_clip_to = true;
      i += 1;
    } else if (arg == "-invert") {
      invert = true;
    } else {
      std::cerr << "Error: Unknown option: " << arg << "\n";
      printUsage();
      return 1;
    }
  }

  Spatial::ASCIIGridReader reader;
  Spatial::RasterDataset dataset;

  if (!reader.read(input_file, dataset)) {
    std::cerr << "Error: Could not read input file\n";
    return 1;
  }

  std::cout << "Input cells: " << (dataset.nrows * dataset.ncols) << "\n";
  std::cout << "Dimensions: " << dataset.ncols << "x" << dataset.nrows << "\n";

  Spatial::RasterDataset output;

  if (use_bbox) {
    std::cout << "Clipping by BBox: " << bbox.minx << " " << bbox.miny << " " << bbox.maxx << " "
              << bbox.maxy << "\n";
    clipRasterByBBox(dataset, output, bbox);
  } else if (use_polygon) {
    auto polygons = readPolygonsFromCSV(polygon_file);
    if (polygons.empty()) {
      std::cerr << "Error: Could not read any POLYGON/MULTIPOLYGON feature from " << polygon_file
                << "\n";
      return 1;
    }
    size_t total_vertices = 0;
    for (const auto& p : polygons) total_vertices += p.size() / 2;
    std::cout << "Clipping by " << polygons.size() << " polygon(s), " << total_vertices
              << " vertices total\n";
    clipRasterByPolygon(dataset, output, polygons, invert);
  } else if (use_clip_to) {
    std::cerr << "Error: -clip_to not yet implemented\n";
    return 1;
  } else {
    std::cerr << "Error: No clipping method specified\n";
    return 1;
  }

  writeRasterASCII(output, output_file, 2);

  std::cout << "Output cells: " << (output.nrows * output.ncols) << "\n";
  std::cout << "Output dimensions: " << output.ncols << "x" << output.nrows << "\n";
  std::cout << "Clipping complete.\n";

  return 0;
}

