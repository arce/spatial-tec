#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../include/core/spatial_streets.hpp"

using streets::csvQuote;
using streets::Segment;

struct PointInput {
  std::string id;
  double x = 0, y = 0;
  bool ok = false;
  std::vector<std::string> extras;
};

static bool readPoints(const std::string& filename, std::vector<PointInput>& out,
                       std::vector<std::string>& extra_names, std::string& err) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    err = "Could not open file: " + filename;
    return false;
  }

  std::string line;
  bool is_header = true;
  int col_id = -1, col_geom = -1, col_x = -1, col_y = -1;
  std::vector<int> extra_cols;

  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#')
      continue;
    auto values = splitCSV(line);

    if (is_header) {
      for (size_t i = 0; i < values.size(); ++i) {
        std::string h = toLower(trim(values[i]));
        if (h == "id")
          col_id = (int)i;
        else if (h == "geometry" || h == "geom" || h == "wkt")
          col_geom = (int)i;
        else if (h == "x" || h == "longitude" || h == "lon")
          col_x = (int)i;
        else if (h == "y" || h == "latitude" || h == "lat")
          col_y = (int)i;
        else {
          extra_cols.push_back((int)i);
          extra_names.push_back(trim(values[i]));
        }
      }
      if (col_geom < 0 && (col_x < 0 || col_y < 0)) {
        err = "The points file needs a geometry column (POINT WKT) or x,y columns";
        return false;
      }
      is_header = false;
      continue;
    }

    auto get = [&](int c) -> std::string {
      return (c >= 0 && c < (int)values.size()) ? trim(values[c]) : "";
    };

    PointInput p;
    p.id = get(col_id);
    if (col_geom >= 0) {
      auto xy = streets::parseWKTCoords(get(col_geom));
      if (xy.size() >= 2) {
        p.x = xy[0];
        p.y = xy[1];
        p.ok = true;
      }
    }
    if (!p.ok && col_x >= 0 && col_y >= 0) {
      try {
        p.x = std::stod(get(col_x));
        p.y = std::stod(get(col_y));
        p.ok = true;
      } catch (...) {
      }
    }
    for (int c : extra_cols)
      p.extras.push_back(get(c));
    out.push_back(p);
  }
  return true;
}

// House number on a side of a segment closest to the position t in [0, 1],
// respecting the parity of the side (e.g. only even numbers on an even side).
static int interpolateNumber(const streets::Range& r, double t) {
  if (r.from == r.to)
    return r.from;
  int step = (r.parity == "both") ? 1 : 2;
  int dir = r.to > r.from ? 1 : -1;
  double raw = r.from + t * (r.to - r.from);
  int count = (int)std::floor(std::abs(r.to - r.from) / (double)step);
  int k = (int)std::lround((raw - r.from) / (dir * step));
  k = std::max(0, std::min(count, k));
  return r.from + dir * step * k;
}

static void printUsage() {
  std::cerr << "spatial_reverse_geocode - Find the approximate street address of one or more points\n\n";
  std::cerr << "Usage: spatial_reverse_geocode <streets.csv> <points.csv> <output.csv> [options]\n\n";
  std::cerr << "Street file must contain:\n";
  std::cerr << "  street_name, left_from, left_to, right_from, right_to, geometry\n";
  std::cerr << "  Optional: left_parity, right_parity (even|odd|both), city, postal_code\n\n";
  std::cerr << "Points file must contain:\n";
  std::cerr << "  a geometry column (POINT WKT) or x,y columns; optionally id\n";
  std::cerr << "  (any other column is copied to the output)\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -max_dist <value>   Maximum distance from the point to a street (default: 50.0)\n";
  std::cerr << "  -verbose            Print the result of every point\n\n";
  std::cerr << "Each point is snapped to the nearest street segment and the house number is\n";
  std::cerr << "interpolated from the numbering range of the side of the street where it lies.\n";
  std::cerr << "Output columns added to the point columns:\n";
  std::cerr << "  status (found | not_found | invalid), street_name, house_number, side, city,\n";
  std::cerr << "  postal_code, distance, measure, street_segment, snapped_x, snapped_y, x, y, geometry\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_reverse_geocode streets.csv points.csv result.csv\n";
  std::cerr << "  spatial_reverse_geocode streets.csv points.csv result.csv -max_dist 20\n";
}

int main(int argc, char* argv[]) {
  if (argc >= 2 && (std::string(argv[1]) == "-help" || std::string(argv[1]) == "-h")) {
    printUsage();
    return 0;
  }
  if (argc < 4) {
    printUsage();
    return 1;
  }

  std::string streets_file = argv[1];
  std::string points_file = argv[2];
  std::string output_file = argv[3];
  double max_dist = 50.0;
  bool verbose = false;

  for (int i = 4; i < argc; ++i) {
    std::string arg = argv[i];
    try {
      if (arg == "-max_dist" && i + 1 < argc) {
        max_dist = std::stod(argv[++i]);
        if (max_dist < 0) {
          std::cerr << "Error: -max_dist must not be negative\n";
          return 1;
        }
      } else if (arg == "-verbose") {
        verbose = true;
      } else if (arg == "-help" || arg == "-h") {
        printUsage();
        return 0;
      } else {
        std::cerr << "Error: Unknown option or missing value: " << arg << "\n";
        printUsage();
        return 1;
      }
    } catch (const std::exception&) {
      std::cerr << "Error: Invalid numeric value for " << arg << "\n";
      return 1;
    }
  }

  std::cout << "========================================\n";
  std::cout << "  REVERSE GEOCODING\n";
  std::cout << "========================================\n\n";
  std::cout << "Streets: " << streets_file << "\n";
  std::cout << "Points: " << points_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Max distance: " << max_dist << "\n\n";

  std::string err;
  std::vector<Segment> segments;
  if (!streets::readStreets(streets_file, segments, err)) {
    std::cerr << "Error: " << err << "\n";
    return 1;
  }
  segments.erase(std::remove_if(segments.begin(), segments.end(),
                                [](const Segment& s) {
                                  return s.vertexCount() < 2 || s.name.empty() ||
                                         (!s.left.valid && !s.right.valid);
                                }),
                 segments.end());
  std::cout << "Street segments loaded: " << segments.size() << "\n";
  if (segments.empty()) {
    std::cerr << "Error: No valid street segments found (check name, ranges and WKT LINESTRING)\n";
    return 1;
  }

  std::vector<PointInput> points;
  std::vector<std::string> extra_names;
  if (!readPoints(points_file, points, extra_names, err)) {
    std::cerr << "Error: " << err << "\n";
    return 1;
  }
  std::cout << "Points loaded: " << points.size() << "\n";
  if (points.empty()) {
    std::cerr << "Error: No points found\n";
    return 1;
  }

  std::ofstream out(output_file);
  if (!out.is_open()) {
    std::cerr << "Error: Could not create output file: " << output_file << "\n";
    return 1;
  }
  out << "# Reverse geocoded points\n# Geometry column: geometry\n";
  out << "id";
  for (const auto& n : extra_names)
    out << "," << csvQuote(n);
  out << ",status,street_name,house_number,side,city,postal_code,distance,measure,street_segment,"
         "snapped_x,snapped_y,x,y,geometry\n";

  int found = 0, not_found = 0, invalid = 0;
  for (const auto& p : points) {
    out << csvQuote(p.id);
    for (const auto& v : p.extras)
      out << "," << csvQuote(v);

    if (!p.ok) {
      invalid++;
      out << ",invalid,,,,,,,,,,,,,POINT EMPTY\n";
      continue;
    }

    const Segment* best = nullptr;
    double best_d = 1e300, best_m = 0, best_cross = 0, best_sx = 0, best_sy = 0;
    for (const auto& seg : segments) {
      double d, m, c, sx, sy;
      seg.nearest(p.x, p.y, d, m, c, sx, sy);
      if (d < best_d) {
        best = &seg;
        best_d = d;
        best_m = m;
        best_cross = c;
        best_sx = sx;
        best_sy = sy;
      }
    }

    if (!best || best_d > max_dist) {
      not_found++;
      out << ",not_found,,,,,,,,,,," << std::fixed << std::setprecision(6) << p.x << "," << p.y
          << ",POINT(" << p.x << " " << p.y << ")\n";
      if (verbose)
        std::cout << "  " << p.id << ": not_found (nearest street at " << std::fixed
                  << std::setprecision(3) << best_d << ")\n";
      continue;
    }

    // Left side when the point is to the left of the digitizing direction.
    bool left = best_cross >= 0;
    if (left && !best->left.valid)
      left = false;
    else if (!left && !best->right.valid)
      left = true;
    const streets::Range& r = left ? best->left : best->right;

    double len = best->length();
    double t = len > 1e-12 ? best_m / len : 0.0;
    int number = interpolateNumber(r, t);

    found++;
    out << ",found," << csvQuote(best->name) << "," << number << "," << (left ? "left" : "right") << ","
        << csvQuote(best->city) << "," << csvQuote(best->postal) << "," << std::fixed
        << std::setprecision(6) << best_d << "," << best_m << "," << best->row << "," << best_sx << ","
        << best_sy << "," << p.x << "," << p.y << ",POINT(" << p.x << " " << p.y << ")\n";
    if (verbose)
      std::cout << "  " << p.id << ": " << number << " " << best->name << " (" << (left ? "left" : "right")
                << ", " << std::fixed << std::setprecision(3) << best_d << " from the street)\n";
  }

  std::cout << "\n========================================\n";
  std::cout << "  REVERSE GEOCODING COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Points processed: " << points.size() << "\n";
  std::cout << "Found: " << found << "\n";
  std::cout << "Not found (farther than max distance): " << not_found << "\n";
  if (invalid > 0)
    std::cout << "Invalid (no readable coordinates): " << invalid << "\n";
  std::cout << "Output written to: " << output_file << "\n";
  return 0;
}
