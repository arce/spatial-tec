#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "../include/core/spatial_tiff.hpp"

static void printUsage() {
  std::cerr << "spatial_tiff2asc - Convert a GeoTIFF to an Arc/Info ASCII Grid (.asc)\n\n";
  std::cerr << "Usage: spatial_tiff2asc <input.tif> <output.asc> [options]\n";
  std::cerr << "       spatial_tiff2asc <input.tif> -info\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -bbox <xmin> <ymin> <xmax> <ymax>  Only convert the window covering this extent\n";
  std::cerr << "                      (map coordinates of the GeoTIFF; cells are included if they\n";
  std::cerr << "                      overlap the extent). Strongly recommended for global rasters.\n";
  std::cerr << "  -band <n>           Band to convert, 1-based (default: 1)\n";
  std::cerr << "  -nodata <value>     NODATA value written to the .asc (default: the file's own\n";
  std::cerr << "                      GDAL_NODATA, or -9999 if it has none). Source NODATA cells and\n";
  std::cerr << "                      NaN are rewritten to this value.\n";
  std::cerr << "  -info               Print the TIFF/GeoTIFF metadata and exit\n";
  std::cerr << "  -verbose            Show detailed information\n\n";
  std::cerr << "Supported: classic TIFF (not BigTIFF), strips or tiles, no compression / LZW /\n";
  std::cerr << "PackBits, optional horizontal predictor, 8/16/32-bit integer and 32/64-bit float\n";
  std::cerr << "samples. Cells must be square (the .asc format has a single cellsize).\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_tiff2asc tmin.tif tmin.asc -bbox -86.0 8.0 -82.5 11.3\n";
  std::cerr << "  spatial_tiff2asc landsat.tif red.asc -band 3\n";
  std::cerr << "  spatial_tiff2asc tmin.tif -info\n";
}

static const char* formatName(const Spatial::TiffInfo& t) {
  if (t.sample_format == 3) return t.bits_per_sample == 32 ? "Float32" : "Float64";
  if (t.sample_format == 2) return t.bits_per_sample == 8 ? "Int8" : t.bits_per_sample == 16 ? "Int16" : "Int32";
  return t.bits_per_sample == 8 ? "UInt8" : t.bits_per_sample == 16 ? "UInt16" : "UInt32";
}

static const char* compressionName(int c) {
  return c == 1 ? "none" : c == 5 ? "LZW" : c == 32773 ? "PackBits" : "other";
}

// Writes a double compactly: integer-valued numbers without a decimal point,
// everything else with enough significant digits for the source sample type.
static void appendNumber(std::string& s, double v, int sig) {
  char b[40];
  if (std::fabs(v) < 1e15 && v == std::floor(v)) std::snprintf(b, sizeof b, "%.0f", v);
  else std::snprintf(b, sizeof b, "%.*g", sig, v);
  s += b;
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printUsage();
    return 1;
  }

  const std::string input_file = argv[1];
  std::string output_file;
  bool info_only = false, verbose = false, use_bbox = false, user_nodata = false;
  int band = 1;
  double bx0 = 0, by0 = 0, bx1 = 0, by1 = 0, out_nodata = -9999.0;

  int first_opt = 2;
  if (std::string(argv[2]) != "-info") {
    output_file = argv[2];
    first_opt = 3;
  }
  for (int i = first_opt; i < argc; ++i) {
    std::string a = argv[i];
    try {
      if (a == "-info") info_only = true;
      else if (a == "-verbose") verbose = true;
      else if (a == "-band" && i + 1 < argc) band = std::stoi(argv[++i]);
      else if (a == "-nodata" && i + 1 < argc) { out_nodata = std::stod(argv[++i]); user_nodata = true; }
      else if (a == "-bbox" && i + 4 < argc) {
        bx0 = std::stod(argv[++i]); by0 = std::stod(argv[++i]);
        bx1 = std::stod(argv[++i]); by1 = std::stod(argv[++i]);
        use_bbox = true;
      } else {
        std::cerr << "Error: unknown or incomplete option: " << a << "\n\n";
        printUsage();
        return 1;
      }
    } catch (...) {
      std::cerr << "Error: invalid numeric value for option " << a << "\n";
      return 1;
    }
  }
  if (!info_only && output_file.empty()) {
    printUsage();
    return 1;
  }

  Spatial::TiffReader reader;
  std::string err;
  if (!reader.open(input_file, err)) {
    std::cerr << "Error: " << err << "\n";
    return 1;
  }
  const Spatial::TiffInfo& t = reader.info();

  // Georeferencing: GeoTIFF tags give the map position of tiepoint pixel (i,j).
  // For PixelIsPoint rasters the tiepoint is the pixel *center*, so shift half a cell.
  bool georef = t.has_scale && t.has_tiepoint;
  double cell = t.scale_x;
  double x0 = 0.0, y0 = static_cast<double>(t.height);  // map coords of image top-left corner
  if (georef) {
    x0 = t.tie_x - t.tie_i * t.scale_x;
    y0 = t.tie_y + t.tie_j * t.scale_y;
    if (t.pixel_is_point) { x0 -= 0.5 * t.scale_x; y0 += 0.5 * t.scale_y; }
  }

  if (info_only || verbose) {
    std::cout << "File:        " << input_file << "\n";
    std::cout << "Size:        " << t.width << " x " << t.height << " cells, " << t.samples_per_pixel << " band(s)\n";
    std::cout << "Type:        " << formatName(t) << ", compression " << compressionName(t.compression)
              << (t.predictor == 2 ? " + predictor" : "") << ", " << (t.tiled ? "tiled" : "strips") << "\n";
    if (georef) {
      std::cout << "Cell size:   " << t.scale_x << " x " << t.scale_y << "\n";
      std::cout << "Extent:      X " << x0 << " .. " << x0 + t.width * t.scale_x
                << ", Y " << y0 - t.height * t.scale_y << " .. " << y0 << "\n";
    } else {
      std::cout << "Georeference: none (no ModelPixelScale/ModelTiepoint tags)\n";
    }
    std::cout << "EPSG:        " << (t.epsg ? std::to_string(t.epsg) : "unknown/user-defined") << "\n";
    std::cout << "NODATA:      ";
    if (t.has_nodata) std::cout << t.nodata << "\n"; else std::cout << "none declared\n";
    if (info_only) return 0;
    std::cout << "\n";
  }

  if (band < 1 || band > t.samples_per_pixel) {
    std::cerr << "Error: -band " << band << " is out of range (the file has " << t.samples_per_pixel << " band(s))\n";
    return 1;
  }
  if (georef && std::fabs(t.scale_x - t.scale_y) > 1e-9 * std::fabs(t.scale_x)) {
    std::cerr << "Error: non-square cells (" << t.scale_x << " x " << t.scale_y
              << "). The .asc format has a single cellsize; resample the TIFF to square cells first.\n";
    return 1;
  }
  if (!georef) {
    std::cerr << "Warning: no georeferencing in the TIFF; writing pixel coordinates (xllcorner 0, yllcorner 0, cellsize 1).\n";
    cell = 1.0;
    if (use_bbox) {
      std::cerr << "Error: -bbox requires a georeferenced TIFF\n";
      return 1;
    }
  }
  if (cell <= 0) {
    std::cerr << "Error: invalid cell size in GeoTIFF tags\n";
    return 1;
  }

  // Window in pixel space.
  int c0 = 0, c1 = t.width - 1, r0 = 0, r1 = t.height - 1;
  if (use_bbox) {
    if (bx0 > bx1) std::swap(bx0, bx1);
    if (by0 > by1) std::swap(by0, by1);
    double fc0 = std::floor((bx0 - x0) / cell), fc1 = std::ceil((bx1 - x0) / cell) - 1;
    double fr0 = std::floor((y0 - by1) / cell), fr1 = std::ceil((y0 - by0) / cell) - 1;
    if (fc1 < 0 || fr1 < 0 || fc0 > t.width - 1 || fr0 > t.height - 1 || fc1 < fc0 || fr1 < fr0) {
      std::cerr << "Error: the -bbox does not overlap the raster extent (X " << x0 << " .. " << x0 + t.width * cell
                << ", Y " << y0 - t.height * cell << " .. " << y0 << ")\n";
      return 1;
    }
    c0 = std::max(0, static_cast<int>(fc0));
    c1 = std::min(t.width - 1, static_cast<int>(fc1));
    r0 = std::max(0, static_cast<int>(fr0));
    r1 = std::min(t.height - 1, static_cast<int>(fr1));
  }
  const int ncols = c1 - c0 + 1, nrows = r1 - r0 + 1;
  const double xll = georef ? x0 + c0 * cell : 0.0;
  const double yll = georef ? y0 - (r1 + 1) * cell : 0.0;

  if (!user_nodata) out_nodata = t.has_nodata ? t.nodata : -9999.0;
  const bool src_has_nodata = t.has_nodata;
  // Float32 NODATA tags (e.g. "-3.4028235e+38") only match the stored sample after rounding to float.
  const double src_nodata = (t.sample_format == 3 && t.bits_per_sample == 32)
                                ? static_cast<double>(static_cast<float>(t.nodata)) : t.nodata;
  const int sig = (t.sample_format == 3) ? (t.bits_per_sample == 32 ? 7 : 15) : 10;

  std::FILE* out = std::fopen(output_file.c_str(), "wb");
  if (!out) {
    std::cerr << "Error: Could not create output file: " << output_file << "\n";
    return 1;
  }
  std::string head;
  head += "ncols " + std::to_string(ncols) + "\nnrows " + std::to_string(nrows) + "\n";
  head += "xllcorner "; appendNumber(head, xll, 12); head += "\n";
  head += "yllcorner "; appendNumber(head, yll, 12); head += "\n";
  head += "cellsize "; appendNumber(head, cell, 15); head += "\n";
  head += "NODATA_value "; appendNumber(head, out_nodata, sig); head += "\n";
  std::fwrite(head.data(), 1, head.size(), out);

  std::vector<double> row;
  std::string line;
  double vmin = std::numeric_limits<double>::max(), vmax = std::numeric_limits<double>::lowest(), vsum = 0;
  size_t valid = 0, nodata_cells = 0;
  for (int r = r0; r <= r1; ++r) {
    if (!reader.readRow(r, c0, c1, band - 1, row, err)) {
      std::cerr << "Error: " << err << " (row " << r << ")\n";
      std::fclose(out);
      return 1;
    }
    line.clear();
    for (int c = 0; c < ncols; ++c) {
      double v = row[c];
      bool nd = std::isnan(v) || (src_has_nodata && v == src_nodata);
      if (nd) {
        v = out_nodata;
        ++nodata_cells;
      } else {
        ++valid;
        vmin = std::min(vmin, v); vmax = std::max(vmax, v); vsum += v;
      }
      if (c) line += ' ';
      appendNumber(line, v, sig);
    }
    line += '\n';
    std::fwrite(line.data(), 1, line.size(), out);
  }
  if (std::fclose(out) != 0) {
    std::cerr << "Error: failed writing " << output_file << "\n";
    return 1;
  }

  std::cout << "========================================\n";
  std::cout << "  CONVERSION COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Grid: " << ncols << " x " << nrows << " cells (band " << band << ")\n";
  if (use_bbox) std::cout << "Window: columns " << c0 << ".." << c1 << ", rows " << r0 << ".." << r1 << " of the source\n";
  std::cout << "Valid cells: " << valid << ", NODATA cells: " << nodata_cells << "\n";
  if (valid) std::cout << "Range: " << vmin << " .. " << vmax << ", mean " << vsum / valid << "\n";
  if (t.epsg && t.epsg != 4326) std::cout << "Note: source CRS is EPSG:" << t.epsg << " (the .asc does not store a CRS)\n";
  std::cout << "Output written to: " << output_file << "\n";
  return 0;
}
