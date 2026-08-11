#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

#include "../include/core/ascii_grid.hpp"
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

  void expand(const BBox& other) {
    minx = std::min(minx, other.minx);
    miny = std::min(miny, other.miny);
    maxx = std::max(maxx, other.maxx);
    maxy = std::max(maxy, other.maxy);
  }
};

enum MergeMethod { METHOD_FIRST, METHOD_LAST, METHOD_MIN, METHOD_MAX, METHOD_AVERAGE, METHOD_SUM };

MergeMethod parseMethod(const std::string& method) {
  std::string m = trim(method);
  std::transform(m.begin(), m.end(), m.begin(), ::tolower);

  if (m == "first")
    return METHOD_FIRST;
  if (m == "last")
    return METHOD_LAST;
  if (m == "min")
    return METHOD_MIN;
  if (m == "max")
    return METHOD_MAX;
  if (m == "average" || m == "mean")
    return METHOD_AVERAGE;
  if (m == "sum")
    return METHOD_SUM;

  return METHOD_FIRST;
}

double mergeValues(const std::vector<double>& values, MergeMethod method) {
  if (values.empty())
    return 0;
  if (values.size() == 1)
    return values[0];

  switch (method) {
    case METHOD_FIRST:
      return values[0];
    case METHOD_LAST:
      return values[values.size() - 1];
    case METHOD_MIN: {
      double result = values[0];
      for (double v : values) result = std::min(result, v);
      return result;
    }
    case METHOD_MAX: {
      double result = values[0];
      for (double v : values) result = std::max(result, v);
      return result;
    }
    case METHOD_AVERAGE: {
      double sum = 0;
      for (double v : values) sum += v;
      return sum / values.size();
    }
    case METHOD_SUM: {
      double sum = 0;
      for (double v : values) sum += v;
      return sum;
    }
  }
  return values[0];
}

BBox getRasterExtent(const Spatial::RasterDataset& dataset) {
  return BBox(dataset.xllcorner, dataset.yllcorner,
              dataset.xllcorner + dataset.ncols * dataset.cellsize,
              dataset.yllcorner + dataset.nrows * dataset.cellsize);
}

void printUsage() {
  std::cerr << "spatial_merge_raster - Create raster mosaic from multiple files\n\n";
  std::cerr << "Usage: spatial_merge_raster <input1> [input2 ...] <output> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -method <method>  Merge method: first, last, min, max, average, sum\n";
  std::cerr << "                    (default: first)\n";
  std::cerr << "  -nodata <value>   NODATA value for output (default: -9999)\n";
  std::cerr << "\nExamples:\n";
  std::cerr << "  spatial_merge_raster tile1.asc tile2.asc tile3.asc mosaic.asc\n";
  std::cerr << "  spatial_merge_raster t1.asc t2.asc mosaic.asc -method average\n";
  std::cerr << "  spatial_merge_raster t1.asc t2.asc t3.asc mosaic.asc -method max\n";
}

int main(int argc, char* argv[]) {
  if (argc < 4) {
    printUsage();
    return 1;
  }

  std::vector<std::string> input_files;
  std::string output_file;
  MergeMethod method = METHOD_FIRST;
  double nodata_value = -9999.0;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-method" && i + 1 < argc) {
      method = parseMethod(argv[i + 1]);
      i++;
    } else if (arg == "-nodata" && i + 1 < argc) {
      nodata_value = std::stod(argv[i + 1]);
      i++;
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
  std::cout << "Method: ";
  switch (method) {
    case METHOD_FIRST:
      std::cout << "first";
      break;
    case METHOD_LAST:
      std::cout << "last";
      break;
    case METHOD_MIN:
      std::cout << "min";
      break;
    case METHOD_MAX:
      std::cout << "max";
      break;
    case METHOD_AVERAGE:
      std::cout << "average";
      break;
    case METHOD_SUM:
      std::cout << "sum";
      break;
  }
  std::cout << "\n";
  std::cout << "NODATA: " << nodata_value << "\n\n";

  Spatial::ASCIIGridReader reader;
  std::vector<Spatial::RasterDataset> rasters;
  BBox total_extent;
  bool first_extent = true;
  double cellsize = 0;
  bool same_cellsize = true;

  for (const auto& filename : input_files) {
    Spatial::RasterDataset dataset;
    if (!reader.read(filename, dataset)) {
      std::cerr << "Error: Could not read input file: " << filename << "\n";
      return 1;
    }
    rasters.push_back(dataset);

    BBox extent = getRasterExtent(dataset);
    if (first_extent) {
      total_extent = extent;
      cellsize = dataset.cellsize;
      first_extent = false;
    } else {
      total_extent.expand(extent);
      if (std::abs(dataset.cellsize - cellsize) > 1e-9) {
        same_cellsize = false;
        std::cerr << "Warning: Different cell sizes detected in " << filename << "\n";
        std::cerr << "  Expected: " << cellsize << ", Got: " << dataset.cellsize << "\n";
      }
    }

    std::cout << "  Loaded: " << filename << " (" << dataset.ncols << "x" << dataset.nrows << ")\n";
    std::cout << "    Extent: " << std::fixed << std::setprecision(6) << extent.minx << " "
              << extent.miny << " " << extent.maxx << " " << extent.maxy << "\n";
  }

  if (!same_cellsize) {
    std::cerr << "\nError: All rasters must have the same cell size\n";
    return 1;
  }

  int ncols = static_cast<int>(std::ceil((total_extent.maxx - total_extent.minx) / cellsize));
  int nrows = static_cast<int>(std::ceil((total_extent.maxy - total_extent.miny) / cellsize));

  std::cout << "\nMosaic extent:\n";
  std::cout << "  Min X: " << std::fixed << std::setprecision(6) << total_extent.minx << "\n";
  std::cout << "  Min Y: " << std::fixed << std::setprecision(6) << total_extent.miny << "\n";
  std::cout << "  Max X: " << std::fixed << std::setprecision(6) << total_extent.maxx << "\n";
  std::cout << "  Max Y: " << std::fixed << std::setprecision(6) << total_extent.maxy << "\n";
  std::cout << "  Columns: " << ncols << "\n";
  std::cout << "  Rows: " << nrows << "\n";
  std::cout << "  Total cells: " << (ncols * nrows) << "\n\n";

  Spatial::RasterDataset output;
  output.ncols = ncols;
  output.nrows = nrows;
  output.xllcorner = total_extent.minx;
  output.yllcorner = total_extent.miny;
  output.cellsize = cellsize;
  output.nodata_value = nodata_value;
  output.crs = rasters[0].crs;
  output.data.resize(nrows * ncols, nodata_value);

  std::cout << "Processing cells...\n";
  int processed = 0;
  int no_data_count = 0;

  for (int r = 0; r < nrows; ++r) {
    for (int c = 0; c < ncols; ++c) {
      double x = output.xllcorner + c * output.cellsize + output.cellsize / 2.0;
      double y = output.yllcorner + (nrows - 1 - r) * output.cellsize + output.cellsize / 2.0;

      std::vector<double> values;

      for (const auto& raster : rasters) {
        BBox raster_extent = getRasterExtent(raster);
        if (x >= raster_extent.minx && x <= raster_extent.maxx && y >= raster_extent.miny &&
            y <= raster_extent.maxy) {
          int col = static_cast<int>((x - raster.xllcorner) / raster.cellsize);
          int row = static_cast<int>((raster.yllcorner + raster.nrows * raster.cellsize - y) /
                                     raster.cellsize);

          col = std::max(0, std::min(col, raster.ncols - 1));
          row = std::max(0, std::min(row, raster.nrows - 1));

          double val = raster.at(row, col);
          if (std::abs(val - raster.nodata_value) > 1e-9) {
            values.push_back(val);
          }
        }
      }

      if (values.empty()) {
        output.at(r, c) = nodata_value;
        no_data_count++;
      } else {
        output.at(r, c) = mergeValues(values, method);
        processed++;
      }
    }

    if ((r + 1) % 100 == 0) {
      std::cout << "\r  Progress: " << ((r + 1) * 100 / nrows) << "%" << std::flush;
    }
  }

  std::cout << "\r  Progress: 100%\n";
  std::cout << "\nProcessed cells: " << processed << "\n";
  std::cout << "NODATA cells: " << no_data_count << "\n";

  writeRasterASCII(output, output_file);

  double min_val = std::numeric_limits<double>::max();
  double max_val = std::numeric_limits<double>::lowest();
  double sum_val = 0;
  int valid_cells = 0;

  for (const auto& val : output.data) {
    if (std::abs(val - nodata_value) > 1e-9) {
      min_val = std::min(min_val, val);
      max_val = std::max(max_val, val);
      sum_val += val;
      valid_cells++;
    }
  }

  std::cout << "\n========================================\n";
  std::cout << "  MERGE COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Output dimensions: " << output.ncols << "x" << output.nrows << "\n";
  std::cout << "Valid cells: " << valid_cells << "\n";
  if (valid_cells > 0) {
    std::cout << "Min: " << std::fixed << std::setprecision(6) << min_val << "\n";
    std::cout << "Max: " << std::fixed << std::setprecision(6) << max_val << "\n";
    std::cout << "Mean: " << std::fixed << std::setprecision(6) << (sum_val / valid_cells) << "\n";
  }
  std::cout << "Output written to: " << output_file << "\n";

  return 0;
}
