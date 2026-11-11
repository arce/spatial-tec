#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

#include "../include/core/ascii_grid.hpp"
#include "../include/core/spatial_io.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

namespace {

const double PI = 3.14159265358979323846;
const double RAD2DEG = 180.0 / PI;
const double DEG2RAD = PI / 180.0;

void printUsage() {
  std::cerr << "spatial_terrain - Terrain analysis from a DEM: slope, aspect, hillshade, curvature\n\n";
  std::cerr << "Usage: spatial_terrain <input.asc> <output.asc> -operation <slope|aspect|hillshade|curvature> [options]\n\n";
  std::cerr << "Operations:\n";
  std::cerr << "  -operation slope        Slope raster (degrees or percent)\n";
  std::cerr << "  -operation aspect       Aspect raster: direction the slope faces, in degrees\n";
  std::cerr << "                          clockwise from north (0 = N, 90 = E, 180 = S, 270 = W)\n";
  std::cerr << "  -operation hillshade    Shaded relief raster (0-255)\n";
  std::cerr << "  -operation curvature    Terrain curvature raster\n\n";
  std::cerr << "Options (apply only to the matching operation):\n";
  std::cerr << "  -units <degrees|percent>            Slope units (slope, default: degrees)\n";
  std::cerr << "  -z_factor <value>                   Multiplier applied to elevations (slope, aspect,\n";
  std::cerr << "                                      hillshade, curvature; default: 1.0). Use it for\n";
  std::cerr << "                                      vertical exaggeration or to convert z units to the\n";
  std::cerr << "                                      horizontal units of the grid\n";
  std::cerr << "  -flat_value <value>                 Value for flat cells (aspect; default: -1)\n";
  std::cerr << "  -azimuth <value>                    Light source azimuth in degrees (hillshade; default: 315)\n";
  std::cerr << "  -altitude <value>                   Light source altitude in degrees (hillshade; default: 45)\n";
  std::cerr << "  -type <profile|planform|general>    Curvature type (curvature; default: general)\n\n";
  std::cerr << "Method:\n";
  std::cerr << "  Slope, aspect and hillshade use Horn's method on the 3x3 window around each cell.\n";
  std::cerr << "  Curvature uses the Zevenbergen-Thorne polynomial and is reported multiplied by 100\n";
  std::cerr << "  (positive = convex, negative = concave).\n";
  std::cerr << "  The cell size must be in the same units as the elevations (e.g. meters); for a DEM\n";
  std::cerr << "  in geographic degrees, reproject it first or set -z_factor.\n";
  std::cerr << "  Cells outside the grid or with NODATA neighbors reuse the value of the center cell;\n";
  std::cerr << "  cells whose own value is NODATA stay NODATA.\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_terrain dem.asc slope.asc -operation slope\n";
  std::cerr << "  spatial_terrain dem.asc slope.asc -operation slope -units percent\n";
  std::cerr << "  spatial_terrain dem.asc aspect.asc -operation aspect\n";
  std::cerr << "  spatial_terrain dem.asc hillshade.asc -operation hillshade -azimuth 315 -altitude 45\n";
  std::cerr << "  spatial_terrain dem.asc curvature.asc -operation curvature -type profile\n";
}

bool isNoData(const Spatial::RasterDataset& r, double v) {
  return std::isnan(v) || std::fabs(v - r.nodata_value) < 1e-9;
}

// Loads the 3x3 window around (row, col), scaled by z_factor, in reading order:
//   w[0] w[1] w[2]      a b c   (north row)
//   w[3] w[4] w[5]  =   d e f
//   w[6] w[7] w[8]      g h i   (south row)
// Returns false when the center cell is NODATA. Neighbors that fall outside the
// grid or are NODATA take the value of the center cell.
bool loadWindow(const Spatial::RasterDataset& r, int row, int col, double z_factor, double w[9]) {
  double center = r.at(row, col);
  if (isNoData(r, center))
    return false;
  int k = 0;
  for (int dr = -1; dr <= 1; ++dr) {
    for (int dc = -1; dc <= 1; ++dc, ++k) {
      int rr = row + dr, cc = col + dc;
      double v = center;
      if (rr >= 0 && rr < r.nrows && cc >= 0 && cc < r.ncols) {
        double n = r.at(rr, cc);
        if (!isNoData(r, n))
          v = n;
      }
      w[k] = v * z_factor;
    }
  }
  return true;
}

// Horn's finite differences: dz/dx grows eastward, dz/dy grows southward.
void hornGradient(const double w[9], double cellsize, double& dzdx, double& dzdy) {
  dzdx = ((w[2] + 2.0 * w[5] + w[8]) - (w[0] + 2.0 * w[3] + w[6])) / (8.0 * cellsize);
  dzdy = ((w[6] + 2.0 * w[7] + w[8]) - (w[0] + 2.0 * w[1] + w[2])) / (8.0 * cellsize);
}

double slopeValue(double dzdx, double dzdy, bool percent) {
  double rise = std::sqrt(dzdx * dzdx + dzdy * dzdy);
  return percent ? rise * 100.0 : std::atan(rise) * RAD2DEG;
}

// Aspect in degrees clockwise from north; flat_value when there is no gradient.
double aspectValue(double dzdx, double dzdy, double flat_value) {
  if (std::fabs(dzdx) < 1e-12 && std::fabs(dzdy) < 1e-12)
    return flat_value;
  double deg = 90.0 - std::atan2(dzdy, -dzdx) * RAD2DEG;
  deg = std::fmod(deg, 360.0);
  if (deg < 0.0)
    deg += 360.0;
  return deg;
}

double hillshadeValue(double dzdx, double dzdy, double azimuth, double altitude) {
  double zenith = (90.0 - altitude) * DEG2RAD;
  double az_math = std::fmod(360.0 - azimuth + 90.0, 360.0) * DEG2RAD;
  double slope = std::atan(std::sqrt(dzdx * dzdx + dzdy * dzdy));
  double aspect = 0.0;
  if (std::fabs(dzdx) > 1e-12) {
    aspect = std::atan2(dzdy, -dzdx);
    if (aspect < 0.0)
      aspect += 2.0 * PI;
  } else if (dzdy > 0.0) {
    aspect = PI / 2.0;
  } else if (dzdy < 0.0) {
    aspect = 3.0 * PI / 2.0;
  }
  double shade = 255.0 * (std::cos(zenith) * std::cos(slope) +
                          std::sin(zenith) * std::sin(slope) * std::cos(az_math - aspect));
  return shade < 0.0 ? 0.0 : shade;
}

enum CurvatureType { CURV_GENERAL, CURV_PROFILE, CURV_PLANFORM };

// Zevenbergen-Thorne coefficients; the result is multiplied by 100.
double curvatureValue(const double w[9], double cellsize, CurvatureType type) {
  double L2 = cellsize * cellsize;
  double D = ((w[3] + w[5]) / 2.0 - w[4]) / L2;
  double E = ((w[1] + w[7]) / 2.0 - w[4]) / L2;
  double F = (-w[0] + w[2] + w[6] - w[8]) / (4.0 * L2);
  double G = (-w[3] + w[5]) / (2.0 * cellsize);
  double H = (w[1] - w[7]) / (2.0 * cellsize);
  if (type == CURV_GENERAL)
    return -2.0 * (D + E) * 100.0;
  double g2h2 = G * G + H * H;
  if (g2h2 < 1e-24)
    return 0.0;
  if (type == CURV_PROFILE)
    return -2.0 * (D * G * G + E * H * H + F * G * H) / g2h2 * 100.0;
  return 2.0 * (D * H * H + E * G * G - F * G * H) / g2h2 * 100.0;
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printUsage();
    return 1;
  }

  std::string input_file = argv[1];
  std::string output_file = argv[2];
  std::string operation;
  std::string units = "degrees";
  double z_factor = 1.0;
  double flat_value = -1.0;
  double azimuth = 315.0;
  double altitude = 45.0;
  std::string curv_type = "general";

  try {
    for (int i = 3; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "-operation" && i + 1 < argc) {
        operation = argv[++i];
      } else if (arg == "-units" && i + 1 < argc) {
        units = argv[++i];
      } else if (arg == "-z_factor" && i + 1 < argc) {
        z_factor = std::stod(argv[++i]);
      } else if (arg == "-flat_value" && i + 1 < argc) {
        flat_value = std::stod(argv[++i]);
      } else if (arg == "-azimuth" && i + 1 < argc) {
        azimuth = std::stod(argv[++i]);
      } else if (arg == "-altitude" && i + 1 < argc) {
        altitude = std::stod(argv[++i]);
      } else if (arg == "-type" && i + 1 < argc) {
        curv_type = argv[++i];
      } else if (arg == "-help" || arg == "-h") {
        printUsage();
        return 0;
      } else {
        std::cerr << "Error: Unknown option or missing value: " << arg << "\n";
        printUsage();
        return 1;
      }
    }
  } catch (const std::exception&) {
    std::cerr << "Error: Invalid numeric value in options\n";
    return 1;
  }

  if (operation != "slope" && operation != "aspect" && operation != "hillshade" &&
      operation != "curvature") {
    std::cerr << "Error: -operation is required and must be one of: slope, aspect, hillshade, curvature\n";
    printUsage();
    return 1;
  }

  units = toLower(trim(units));
  if (units != "degrees" && units != "percent") {
    std::cerr << "Error: -units must be degrees or percent\n";
    return 1;
  }
  curv_type = toLower(trim(curv_type));
  CurvatureType ctype = CURV_GENERAL;
  if (curv_type == "general") {
    ctype = CURV_GENERAL;
  } else if (curv_type == "profile") {
    ctype = CURV_PROFILE;
  } else if (curv_type == "planform" || curv_type == "plan") {
    ctype = CURV_PLANFORM;
    curv_type = "planform";
  } else {
    std::cerr << "Error: -type must be profile, planform or general\n";
    return 1;
  }
  if (!(z_factor > 0.0) || !std::isfinite(z_factor)) {
    std::cerr << "Error: -z_factor must be a positive number\n";
    return 1;
  }
  if (altitude < 0.0 || altitude > 90.0) {
    std::cerr << "Error: -altitude must be between 0 and 90 degrees\n";
    return 1;
  }
  if (!std::isfinite(azimuth)) {
    std::cerr << "Error: -azimuth must be a finite number\n";
    return 1;
  }
  azimuth = std::fmod(azimuth, 360.0);
  if (azimuth < 0.0)
    azimuth += 360.0;

  Spatial::RasterDataset input;
  Spatial::ASCIIGridReader reader;
  if (!reader.read(input_file, input)) {
    std::cerr << "Error: Could not read input file: " << input_file << "\n";
    return 1;
  }
  if (input.ncols <= 0 || input.nrows <= 0 ||
      input.data.size() != static_cast<size_t>(input.ncols) * static_cast<size_t>(input.nrows)) {
    std::cerr << "Error: Input raster is empty or its data does not match ncols x nrows\n";
    return 1;
  }
  if (!(input.cellsize > 0.0)) {
    std::cerr << "Error: Input raster has an invalid cellsize\n";
    return 1;
  }
  {
    std::ofstream probe(output_file);
    if (!probe.is_open()) {
      std::cerr << "Error: Could not create output file: " << output_file << "\n";
      return 1;
    }
  }

  std::cout << "========================================\n";
  std::cout << "  TERRAIN ANALYSIS: " << operation << "\n";
  std::cout << "========================================\n\n";
  std::cout << "Input: " << input_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Dimensions: " << input.ncols << "x" << input.nrows << "\n";
  std::cout << "Cell size: " << input.cellsize << "\n";
  if (z_factor != 1.0)
    std::cout << "Z factor: " << z_factor << "\n";
  if (operation == "slope") {
    std::cout << "Units: " << units << "\n";
  } else if (operation == "aspect") {
    std::cout << "Flat value: " << flat_value << "\n";
  } else if (operation == "hillshade") {
    std::cout << "Azimuth: " << azimuth << "\n";
    std::cout << "Altitude: " << altitude << "\n";
  } else {
    std::cout << "Curvature type: " << curv_type << "\n";
  }
  if (input.cellsize < 0.01) {
    std::cerr << "\nWarning: the cell size (" << input.cellsize
              << ") looks like geographic degrees. Slope, aspect, hillshade and curvature assume\n"
              << "that horizontal and vertical units are the same; reproject the DEM to a projected\n"
              << "system (spatial_reproject) or set -z_factor accordingly.\n";
  }

  Spatial::RasterDataset output;
  output.ncols = input.ncols;
  output.nrows = input.nrows;
  output.xllcorner = input.xllcorner;
  output.yllcorner = input.yllcorner;
  output.cellsize = input.cellsize;
  output.nodata_value = input.nodata_value;
  output.crs = input.crs;
  output.data.assign(input.data.size(), input.nodata_value);

  size_t valid = 0, nodata = 0;
  double vmin = std::numeric_limits<double>::max();
  double vmax = std::numeric_limits<double>::lowest();
  double vsum = 0.0;
  bool percent = (units == "percent");

  for (int r = 0; r < input.nrows; ++r) {
    for (int c = 0; c < input.ncols; ++c) {
      double w[9];
      if (!loadWindow(input, r, c, z_factor, w)) {
        ++nodata;
        continue;
      }
      double dzdx = 0.0, dzdy = 0.0;
      double value = 0.0;
      if (operation == "curvature") {
        value = curvatureValue(w, input.cellsize, ctype);
      } else {
        hornGradient(w, input.cellsize, dzdx, dzdy);
        if (operation == "slope")
          value = slopeValue(dzdx, dzdy, percent);
        else if (operation == "aspect")
          value = aspectValue(dzdx, dzdy, flat_value);
        else
          value = hillshadeValue(dzdx, dzdy, azimuth, altitude);
      }
      output.at(r, c) = value;
      ++valid;
      vmin = std::min(vmin, value);
      vmax = std::max(vmax, value);
      vsum += value;
    }
  }

  if (valid == 0) {
    std::cerr << "Error: The input raster has no valid cells\n";
    return 1;
  }

  int precision = (operation == "hillshade") ? 2 : (operation == "curvature" ? 6 : 4);
  writeRasterASCII(output, output_file, precision);

  std::cout << "\nCells processed: " << valid << "\n";
  std::cout << "NODATA cells: " << nodata << "\n";
  std::cout << "Result range: " << vmin << " to " << vmax << " (mean " << vsum / valid << ")\n";
  std::cout << "\nDone. Wrote " << output_file << "\n";
  return 0;
}
