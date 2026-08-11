#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <vector>

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
};

enum InterpMethod { NEAREST, BILINEAR, CUBIC, AVERAGE, MIN, MAX };

InterpMethod parseMethod(const std::string& method) {
  std::string m = toLower(trim(method));

  if (m == "nearest")
    return NEAREST;
  if (m == "bilinear")
    return BILINEAR;
  if (m == "cubic")
    return CUBIC;
  if (m == "average" || m == "mean")
    return AVERAGE;
  if (m == "min")
    return MIN;
  if (m == "max")
    return MAX;

  return AVERAGE;
}

std::string methodToString(InterpMethod method) {
  switch (method) {
    case NEAREST:
      return "nearest";
    case BILINEAR:
      return "bilinear";
    case CUBIC:
      return "cubic";
    case AVERAGE:
      return "average";
    case MIN:
      return "min";
    case MAX:
      return "max";
    default:
      return "average";
  }
}

double getValueNearest(const Spatial::RasterDataset& raster, double x, double y, double nodata) {
  int col = static_cast<int>((x - raster.xllcorner) / raster.cellsize + 0.5);
  int row = static_cast<int>(
      (raster.yllcorner + raster.nrows * raster.cellsize - y) / raster.cellsize + 0.5);

  if (col < 0 || col >= raster.ncols || row < 0 || row >= raster.nrows) {
    return nodata;
  }

  double val = raster.at(row, col);
  if (std::abs(val - raster.nodata_value) < 1e-9) {
    return nodata;
  }
  return val;
}

double getValueBilinear(const Spatial::RasterDataset& raster, double x, double y, double nodata) {
  double col_d = (x - raster.xllcorner) / raster.cellsize;
  double row_d = (raster.yllcorner + raster.nrows * raster.cellsize - y) / raster.cellsize;

  int col = static_cast<int>(col_d);
  int row = static_cast<int>(row_d);

  if (col < 0 || col >= raster.ncols - 1 || row < 0 || row >= raster.nrows - 1) {
    return getValueNearest(raster, x, y, nodata);
  }

  double fx = col_d - col;
  double fy = row_d - row;

  double v00 = raster.at(row, col);
  double v10 = raster.at(row, col + 1);
  double v01 = raster.at(row + 1, col);
  double v11 = raster.at(row + 1, col + 1);

  if (std::abs(v00 - raster.nodata_value) < 1e-9 || std::abs(v10 - raster.nodata_value) < 1e-9 ||
      std::abs(v01 - raster.nodata_value) < 1e-9 || std::abs(v11 - raster.nodata_value) < 1e-9) {
    return getValueNearest(raster, x, y, nodata);
  }

  double v0 = v00 * (1 - fx) + v10 * fx;
  double v1 = v01 * (1 - fx) + v11 * fx;
  return v0 * (1 - fy) + v1 * fy;
}

double getValueCubic(const Spatial::RasterDataset& raster, double x, double y, double nodata) {
  return getValueBilinear(raster, x, y, nodata);
}

double getValueAverage(const Spatial::RasterDataset& raster, double x, double y,
                       double new_cellsize, double nodata) {
  double half = new_cellsize / 2.0;
  double x1 = x - half;
  double x2 = x + half;
  double y1 = y - half;
  double y2 = y + half;

  int col1 = static_cast<int>((x1 - raster.xllcorner) / raster.cellsize);
  int col2 = static_cast<int>((x2 - raster.xllcorner) / raster.cellsize);
  int row1 =
      static_cast<int>((raster.yllcorner + raster.nrows * raster.cellsize - y2) / raster.cellsize);
  int row2 =
      static_cast<int>((raster.yllcorner + raster.nrows * raster.cellsize - y1) / raster.cellsize);

  col1 = std::max(0, std::min(col1, raster.ncols - 1));
  col2 = std::max(0, std::min(col2, raster.ncols - 1));
  row1 = std::max(0, std::min(row1, raster.nrows - 1));
  row2 = std::max(0, std::min(row2, raster.nrows - 1));

  double sum = 0;
  int count = 0;

  for (int r = row1; r <= row2; ++r) {
    for (int c = col1; c <= col2; ++c) {
      double val = raster.at(r, c);
      if (std::abs(val - raster.nodata_value) > 1e-9) {
        sum += val;
        count++;
      }
    }
  }

  if (count == 0)
    return nodata;
  return sum / count;
}

double getValueMin(const Spatial::RasterDataset& raster, double x, double y, double new_cellsize,
                   double nodata) {
  double half = new_cellsize / 2.0;
  double x1 = x - half;
  double x2 = x + half;
  double y1 = y - half;
  double y2 = y + half;

  int col1 = static_cast<int>((x1 - raster.xllcorner) / raster.cellsize);
  int col2 = static_cast<int>((x2 - raster.xllcorner) / raster.cellsize);
  int row1 =
      static_cast<int>((raster.yllcorner + raster.nrows * raster.cellsize - y2) / raster.cellsize);
  int row2 =
      static_cast<int>((raster.yllcorner + raster.nrows * raster.cellsize - y1) / raster.cellsize);

  col1 = std::max(0, std::min(col1, raster.ncols - 1));
  col2 = std::max(0, std::min(col2, raster.ncols - 1));
  row1 = std::max(0, std::min(row1, raster.nrows - 1));
  row2 = std::max(0, std::min(row2, raster.nrows - 1));

  double min_val = std::numeric_limits<double>::max();
  bool found = false;

  for (int r = row1; r <= row2; ++r) {
    for (int c = col1; c <= col2; ++c) {
      double val = raster.at(r, c);
      if (std::abs(val - raster.nodata_value) > 1e-9) {
        min_val = std::min(min_val, val);
        found = true;
      }
    }
  }

  return found ? min_val : nodata;
}

double getValueMax(const Spatial::RasterDataset& raster, double x, double y, double new_cellsize,
                   double nodata) {
  double half = new_cellsize / 2.0;
  double x1 = x - half;
  double x2 = x + half;
  double y1 = y - half;
  double y2 = y + half;

  int col1 = static_cast<int>((x1 - raster.xllcorner) / raster.cellsize);
  int col2 = static_cast<int>((x2 - raster.xllcorner) / raster.cellsize);
  int row1 =
      static_cast<int>((raster.yllcorner + raster.nrows * raster.cellsize - y2) / raster.cellsize);
  int row2 =
      static_cast<int>((raster.yllcorner + raster.nrows * raster.cellsize - y1) / raster.cellsize);

  col1 = std::max(0, std::min(col1, raster.ncols - 1));
  col2 = std::max(0, std::min(col2, raster.ncols - 1));
  row1 = std::max(0, std::min(row1, raster.nrows - 1));
  row2 = std::max(0, std::min(row2, raster.nrows - 1));

  double max_val = std::numeric_limits<double>::lowest();
  bool found = false;

  for (int r = row1; r <= row2; ++r) {
    for (int c = col1; c <= col2; ++c) {
      double val = raster.at(r, c);
      if (std::abs(val - raster.nodata_value) > 1e-9) {
        max_val = std::max(max_val, val);
        found = true;
      }
    }
  }

  return found ? max_val : nodata;
}

void resampleRaster(const Spatial::RasterDataset& input, Spatial::RasterDataset& output,
                    double new_cellsize, const BBox& bbox, InterpMethod method, double nodata) {
  int ncols = static_cast<int>(std::ceil((bbox.maxx - bbox.minx) / new_cellsize));
  int nrows = static_cast<int>(std::ceil((bbox.maxy - bbox.miny) / new_cellsize));

  output.ncols = ncols;
  output.nrows = nrows;
  output.xllcorner = bbox.minx;
  output.yllcorner = bbox.miny;
  output.cellsize = new_cellsize;
  output.nodata_value = nodata;
  output.crs = input.crs;
  output.data.resize(nrows * ncols, nodata);

  std::cout << "Output dimensions: " << ncols << "x" << nrows << "\n";
  std::cout << "Total cells: " << (ncols * nrows) << "\n\n";

  int processed = 0;
  int nodata_count = 0;

  for (int r = 0; r < nrows; ++r) {
    for (int c = 0; c < ncols; ++c) {
      double x = output.xllcorner + c * output.cellsize + output.cellsize / 2.0;
      double y = output.yllcorner + (nrows - 1 - r) * output.cellsize + output.cellsize / 2.0;

      double val;

      if (method == NEAREST) {
        val = getValueNearest(input, x, y, nodata);
      } else if (method == BILINEAR) {
        val = getValueBilinear(input, x, y, nodata);
      } else if (method == CUBIC) {
        val = getValueCubic(input, x, y, nodata);
      } else if (method == AVERAGE) {
        val = getValueAverage(input, x, y, new_cellsize, nodata);
      } else if (method == MIN) {
        val = getValueMin(input, x, y, new_cellsize, nodata);
      } else if (method == MAX) {
        val = getValueMax(input, x, y, new_cellsize, nodata);
      } else {
        val = getValueAverage(input, x, y, new_cellsize, nodata);
      }

      output.at(r, c) = val;
      processed++;

      if (std::abs(val - nodata) < 1e-9) {
        nodata_count++;
      }
    }

    if ((r + 1) % 100 == 0) {
      std::cout << "\r  Progress: " << ((r + 1) * 100 / nrows) << "%" << std::flush;
    }
  }

  std::cout << "\r  Progress: 100%\n";
  std::cout << "Cells processed: " << processed << "\n";
  std::cout << "NODATA cells: " << nodata_count << "\n";
}

bool readRasterHeader(const std::string& filename, double& xll, double& yll, double& cellsize,
                      int& ncols, int& nrows) {
  std::ifstream file(filename);
  if (!file.is_open())
    return false;

  std::string line;
  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty())
      continue;

    auto tokens = split(line, ' ');
    if (tokens.size() < 2)
      continue;

    std::string key = toLower(tokens[0]);

    if (key == "ncols")
      ncols = std::stoi(tokens[1]);
    else if (key == "nrows")
      nrows = std::stoi(tokens[1]);
    else if (key == "xllcorner" || key == "xllcenter")
      xll = std::stod(tokens[1]);
    else if (key == "yllcorner" || key == "yllcenter")
      yll = std::stod(tokens[1]);
    else if (key == "cellsize")
      cellsize = std::stod(tokens[1]);
  }

  return true;
}

void printUsage() {
  std::cerr << "spatial_resample - Change resolution of raster data\n\n";
  std::cerr << "Usage: spatial_resample <input> <output> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -resolution <value>    New cell size\n";
  std::cerr
      << "  -scale <factor>        Scale factor (0.5 = double resolution, 2 = half resolution)\n";
  std::cerr << "  -method <method>       Interpolation method:\n";
  std::cerr << "                           nearest, bilinear, cubic, average, min, max\n";
  std::cerr << "                         (default: average)\n";
  std::cerr << "  -align_to <file>       Align to another raster (same resolution and extent)\n";
  std::cerr << "  -bbox <minx> <miny> <maxx> <maxy>  Output extent\n";
  std::cerr << "  -nodata <value>        NODATA value (default: -9999)\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_resample elev.asc elev_100m.asc -resolution 100 -method average\n";
  std::cerr << "  spatial_resample elev.asc elev_10m.asc -resolution 10 -method bilinear\n";
  std::cerr << "  spatial_resample elev.asc elev_align.asc -align_to other.asc\n";
  std::cerr << "  spatial_resample elev.asc elev_half.asc -scale 2 -method average\n";
}

int main(int argc, char* argv[]) {
  if (argc < 4) {
    printUsage();
    return 1;
  }

  std::string input_file;
  std::string output_file;
  std::string align_file;
  double resolution = 0;
  double scale = 0;
  InterpMethod method = AVERAGE;
  BBox bbox;
  bool use_bbox = false;
  double nodata = -9999.0;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-resolution" && i + 1 < argc) {
      resolution = std::stod(argv[++i]);
    } else if (arg == "-scale" && i + 1 < argc) {
      scale = std::stod(argv[++i]);
    } else if (arg == "-method" && i + 1 < argc) {
      method = parseMethod(argv[++i]);
    } else if (arg == "-align_to" && i + 1 < argc) {
      align_file = argv[++i];
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

  if (!align_file.empty()) {
    double align_xll, align_yll, align_cellsize;
    int align_ncols, align_nrows;

    if (!readRasterHeader(align_file, align_xll, align_yll, align_cellsize, align_ncols,
                          align_nrows)) {
      std::cerr << "Error: Could not read alignment file: " << align_file << "\n";
      return 1;
    }

    resolution = align_cellsize;
    bbox = BBox(align_xll, align_yll, align_xll + align_ncols * align_cellsize,
                align_yll + align_nrows * align_cellsize);
    use_bbox = true;

    std::cout << "Aligning to: " << align_file << "\n";
    std::cout << "  Resolution: " << resolution << "\n";
    std::cout << "  Extent: " << bbox.minx << " " << bbox.miny << " " << bbox.maxx << " "
              << bbox.maxy << "\n\n";
  }

  if (resolution <= 0 && scale <= 0) {
    std::cerr << "Error: Either -resolution or -scale must be specified\n";
    return 1;
  }

  Spatial::ASCIIGridReader reader;
  Spatial::RasterDataset input;

  if (!reader.read(input_file, input)) {
    std::cerr << "Error: Could not read input file\n";
    return 1;
  }

  std::cout << "========================================\n";
  std::cout << "  RASTER RESAMPLE\n";
  std::cout << "========================================\n\n";
  std::cout << "Input: " << input_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Input dimensions: " << input.ncols << "x" << input.nrows << "\n";
  std::cout << "Input cell size: " << input.cellsize << "\n";
  std::cout << "Input extent: " << input.xllcorner << " " << input.yllcorner << " "
            << (input.xllcorner + input.ncols * input.cellsize) << " "
            << (input.yllcorner + input.nrows * input.cellsize) << "\n\n";

  if (scale > 0) {
    resolution = input.cellsize * scale;
    std::cout << "Scale factor: " << scale << "\n";
  }

  std::cout << "Method: " << methodToString(method) << "\n";
  std::cout << "New cell size: " << resolution << "\n";

  if (!use_bbox) {
    bbox = BBox(input.xllcorner, input.yllcorner, input.xllcorner + input.ncols * input.cellsize,
                input.yllcorner + input.nrows * input.cellsize);
  }

  std::cout << "Output extent: " << bbox.minx << " " << bbox.miny << " " << bbox.maxx << " "
            << bbox.maxy << "\n\n";

  Spatial::RasterDataset output;
  resampleRaster(input, output, resolution, bbox, method, nodata);

  writeRasterASCII(output, output_file);

  std::cout << "\n========================================\n";
  std::cout << "  RESAMPLE COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Output dimensions: " << output.ncols << "x" << output.nrows << "\n";
  std::cout << "Output cell size: " << output.cellsize << "\n";
  std::cout << "Output written to: " << output_file << "\n";

  return 0;
}
