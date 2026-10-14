#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <vector>

#include "../include/core/ascii_grid.hpp"
#include "../include/core/spatial_csv.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

struct Point2D {
  double x, y;
  Point2D() : x(0), y(0) {}
  Point2D(double x, double y) : x(x), y(y) {}
};

struct ZonalStats {
  double sum;
  double mean;
  double min_val;
  double max_val;
  double range;
  double std_val;
  double variance;
  double cv;
  double median;
  double p25;
  double p75;
  size_t count;
  size_t nodata_count;
  size_t variety;
  double majority;
  double minority;
  double diversity;
  double sum_sq;
  std::vector<double> percentiles;
  std::vector<double> values;

  ZonalStats()
      : sum(0),
        mean(0),
        min_val(0),
        max_val(0),
        range(0),
        std_val(0),
        variance(0),
        cv(0),
        median(0),
        p25(0),
        p75(0),
        count(0),
        nodata_count(0),
        variety(0),
        majority(0),
        minority(0),
        diversity(0),
        sum_sq(0) {}
};

struct Zone {
  std::string id;
  std::vector<Point2D> polygon;
  std::map<std::string, std::string> attributes;
  ZonalStats stats;
};

std::vector<Point2D> parseWKTPolygon(const std::string& wkt) {
  std::vector<Point2D> points;
  std::string str = trim(wkt);

  size_t start = str.find('(');
  if (start == std::string::npos)
    return points;

  size_t end = str.rfind(')');
  if (end == std::string::npos || end <= start)
    return points;

  std::string inner = str.substr(start + 1, end - start - 1);
  inner.erase(std::remove(inner.begin(), inner.end(), '('), inner.end());
  inner.erase(std::remove(inner.begin(), inner.end(), ')'), inner.end());

  auto coord_strings = split(inner, ',');

  for (const auto& cs : coord_strings) {
    auto parts = split(trim(cs), ' ');
    if (parts.size() >= 2) {
      try {
        double x = std::stod(parts[0]);
        double y = std::stod(parts[1]);
        points.push_back(Point2D(x, y));
      } catch (...) {
      }
    }
  }

  return points;
}

std::vector<Zone> readZones(const std::string& filename, const std::string& zone_id_attr) {
  std::vector<Zone> zones;
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file: " << filename << "\n";
    return zones;
  }

  std::string line;
  std::vector<std::string> headers;
  bool is_header = true;
  int col_id = -1;
  int col_geometry = -1;
  std::vector<int> attr_cols;

  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#')
      continue;

    auto values = splitCSV(line);

    if (is_header) {
      headers = values;

      for (size_t i = 0; i < headers.size(); ++i) {
        std::string lower = toLower(headers[i]);
        if (lower == zone_id_attr || lower == "id") {
          col_id = i;
        } else if (lower == "geometry" || lower == "geom" || lower == "wkt") {
          col_geometry = i;
        } else {
          attr_cols.push_back(i);
        }
      }

      is_header = false;
      continue;
    }

    if (col_geometry == -1) {
      std::cerr << "Error: No geometry column found\n";
      return zones;
    }

    Zone zone;

    if (col_id >= 0 && col_id < (int)values.size()) {
      zone.id = values[col_id];
    } else {
      zone.id = std::to_string(zones.size() + 1);
    }

    if (col_geometry >= 0 && col_geometry < (int)values.size()) {
      zone.polygon = parseWKTPolygon(values[col_geometry]);
    }

    if (zone.polygon.size() < 3)
      continue;

    for (int idx : attr_cols) {
      if (idx < (int)values.size()) {
        zone.attributes[headers[idx]] = values[idx];
      }
    }

    zones.push_back(zone);
  }

  return zones;
}

bool pointInPolygon(double x, double y, const std::vector<Point2D>& polygon) {
  bool inside = false;
  int n = polygon.size();

  for (int i = 0, j = n - 1; i < n; j = i++) {
    double xi = polygon[i].x;
    double yi = polygon[i].y;
    double xj = polygon[j].x;
    double yj = polygon[j].y;

    if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) {
      inside = !inside;
    }
  }

  return inside;
}

ZonalStats calculateStats(const std::vector<double>& values, double nodata,
                          bool calc_percentiles = true) {
  ZonalStats stats;

  std::vector<double> valid;
  for (double v : values) {
    if (std::abs(v - nodata) > 1e-9) {
      valid.push_back(v);
    } else {
      stats.nodata_count++;
    }
  }

  stats.count = valid.size();
  stats.values = valid;

  if (valid.empty()) {
    return stats;
  }

  std::vector<double> sorted = valid;
  std::sort(sorted.begin(), sorted.end());

  stats.min_val = sorted.front();
  stats.max_val = sorted.back();
  stats.range = stats.max_val - stats.min_val;

  stats.sum = std::accumulate(valid.begin(), valid.end(), 0.0);
  stats.mean = stats.sum / valid.size();

  stats.sum_sq = 0;
  for (double v : valid) {
    stats.sum_sq += v * v;
  }

  stats.variance = 0;
  for (double v : valid) {
    double diff = v - stats.mean;
    stats.variance += diff * diff;
  }
  stats.variance /= valid.size();
  stats.std_val = std::sqrt(stats.variance);

  if (std::abs(stats.mean) > 1e-12) {
    stats.cv = (stats.std_val / stats.mean) * 100.0;
  } else {
    stats.cv = 0;
  }

  if (calc_percentiles) {
    stats.median = sorted[sorted.size() / 2];
    stats.p25 = sorted[sorted.size() / 4];
    stats.p75 = sorted[sorted.size() * 3 / 4];
  }

  std::map<double, size_t> freq;
  for (double v : valid) {
    freq[v]++;
  }

  stats.variety = freq.size();
  stats.diversity = (double)stats.variety / valid.size();

  size_t max_freq = 0;
  size_t min_freq = valid.size();
  for (const auto& [value, count] : freq) {
    if (count > max_freq) {
      max_freq = count;
      stats.majority = value;
    }
    if (count < min_freq) {
      min_freq = count;
      stats.minority = value;
    }
  }

  return stats;
}

void writeZonalStatsCSV(const std::vector<Zone>& zones,
                        const std::vector<std::string>& stats_to_write, const std::string& filename,
                        bool keep_attrs = false) {
  std::ofstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not create file: " << filename << "\n";
    return;
  }

  file << "# Zonal statistics\n";
  file << "# Zones: " << zones.size() << "\n";
  file << "# Statistics: ";
  for (const auto& s : stats_to_write) file << s << " ";
  file << "\n";

  file << "zone_id";

  if (keep_attrs) {
    if (!zones.empty()) {
      for (const auto& [key, value] : zones[0].attributes) {
        file << "," << key;
      }
    }
  }

  for (const auto& stat : stats_to_write) {
    if (stat == "sum")
      file << ",sum";
    else if (stat == "mean")
      file << ",mean";
    else if (stat == "min")
      file << ",min";
    else if (stat == "max")
      file << ",max";
    else if (stat == "range")
      file << ",range";
    else if (stat == "std")
      file << ",std";
    else if (stat == "variance")
      file << ",variance";
    else if (stat == "cv")
      file << ",cv";
    else if (stat == "count")
      file << ",count";
    else if (stat == "nodata_count")
      file << ",nodata_count";
    else if (stat == "median")
      file << ",median";
    else if (stat == "p25")
      file << ",p25";
    else if (stat == "p75")
      file << ",p75";
    else if (stat == "variety")
      file << ",variety";
    else if (stat == "diversity")
      file << ",diversity";
    else if (stat == "majority")
      file << ",majority";
    else if (stat == "minority")
      file << ",minority";
    else if (stat == "sum_sq")
      file << ",sum_sq";
  }
  file << "\n";

  for (const auto& zone : zones) {
    file << zone.id;

    if (keep_attrs) {
      for (const auto& [key, value] : zone.attributes) {
        file << "," << value;
      }
    }

    const auto& stats = zone.stats;
    for (const auto& stat : stats_to_write) {
      if (stat == "sum")
        file << "," << std::fixed << std::setprecision(6) << stats.sum;
      else if (stat == "mean")
        file << "," << std::fixed << std::setprecision(6) << stats.mean;
      else if (stat == "min")
        file << "," << std::fixed << std::setprecision(6) << stats.min_val;
      else if (stat == "max")
        file << "," << std::fixed << std::setprecision(6) << stats.max_val;
      else if (stat == "range")
        file << "," << std::fixed << std::setprecision(6) << stats.range;
      else if (stat == "std")
        file << "," << std::fixed << std::setprecision(6) << stats.std_val;
      else if (stat == "variance")
        file << "," << std::fixed << std::setprecision(6) << stats.variance;
      else if (stat == "cv")
        file << "," << std::fixed << std::setprecision(6) << stats.cv;
      else if (stat == "count")
        file << "," << stats.count;
      else if (stat == "nodata_count")
        file << "," << stats.nodata_count;
      else if (stat == "median")
        file << "," << std::fixed << std::setprecision(6) << stats.median;
      else if (stat == "p25")
        file << "," << std::fixed << std::setprecision(6) << stats.p25;
      else if (stat == "p75")
        file << "," << std::fixed << std::setprecision(6) << stats.p75;
      else if (stat == "variety")
        file << "," << stats.variety;
      else if (stat == "diversity")
        file << "," << std::fixed << std::setprecision(6) << stats.diversity;
      else if (stat == "majority")
        file << "," << std::fixed << std::setprecision(6) << stats.majority;
      else if (stat == "minority")
        file << "," << std::fixed << std::setprecision(6) << stats.minority;
      else if (stat == "sum_sq")
        file << "," << std::fixed << std::setprecision(6) << stats.sum_sq;
    }
    file << "\n";
  }
}

void writeHistogramCSV(const std::vector<Zone>& zones, const std::string& filename, int num_bins) {
  std::ofstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not create file: " << filename << "\n";
    return;
  }

  file << "# Zonal histograms\n";
  file << "# Bins: " << num_bins << "\n";

  for (const auto& zone : zones) {
    if (zone.stats.values.empty())
      continue;

    auto values = zone.stats.values;
    double min_val = *std::min_element(values.begin(), values.end());
    double max_val = *std::max_element(values.begin(), values.end());
    double range = max_val - min_val;

    if (range < 1e-12) {
      file << zone.id << ",all," << min_val << "," << values.size() << "\n";
      continue;
    }

    double bin_width = range / num_bins;
    std::vector<size_t> bins(num_bins, 0);

    for (double v : values) {
      int idx = static_cast<int>((v - min_val) / bin_width);
      if (idx >= num_bins)
        idx = num_bins - 1;
      if (idx < 0)
        idx = 0;
      bins[idx]++;
    }

    for (int i = 0; i < num_bins; ++i) {
      double low = min_val + i * bin_width;
      double high = low + bin_width;
      file << zone.id << "," << std::fixed << std::setprecision(6) << low << "," << std::fixed
           << std::setprecision(6) << high << "," << bins[i] << "\n";
    }
  }
}

void printUsage() {
  std::cerr << "spatial_zonal - Calculate zonal statistics\n\n";
  std::cerr << "Usage: spatial_zonal <raster.asc> <zones.csv> <output.csv> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -stat <stats>      Statistics to calculate (comma-separated):\n";
  std::cerr << "                     sum,mean,min,max,range,std,variance,cv\n";
  std::cerr << "                     count,nodata_count,median,p25,p75\n";
  std::cerr << "                     variety,diversity,majority,minority,sum_sq\n";
  std::cerr << "                     all (all statistics)\n";
  std::cerr << "  -zone_id <attr>    Attribute for zone identification (default: id)\n";
  std::cerr << "  -keep_attrs        Keep zone attributes in output\n";
  std::cerr << "  -histogram         Generate histogram for each zone\n";
  std::cerr << "  -bins <n>          Number of bins for histogram (default: 10)\n";
  std::cerr << "  -verbose           Show detailed information\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_zonal elevation.asc zones.csv stats.csv -stat mean,min,max,sum\n";
  std::cerr << "  spatial_zonal classification.asc zones.csv stats.csv -stat variety,diversity\n";
  std::cerr << "  spatial_zonal elevation.asc zones.csv stats.csv -stat all -keep_attrs\n";
}

int main(int argc, char* argv[]) {
  if (argc < 4) {
    printUsage();
    return 1;
  }

  std::string raster_file = argv[1];
  std::string zones_file = argv[2];
  std::string output_file = argv[3];

  std::string zone_id_attr = "id";
  std::vector<std::string> stats_to_calc = {"sum", "mean", "min", "max", "count"};
  bool keep_attrs = false;
  bool calc_histogram = false;
  int num_bins = 10;
  bool verbose = false;

  for (int i = 4; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-stat" && i + 1 < argc) {
      std::string stats_str = argv[++i];
      stats_to_calc.clear();

      if (stats_str == "all") {
        stats_to_calc = {"sum",      "mean",    "min",       "max",          "range",    "std",
                         "variance", "cv",      "count",     "nodata_count", "median",   "p25",
                         "p75",      "variety", "diversity", "majority",     "minority", "sum_sq"};
      } else {
        auto tokens = split(stats_str, ',');
        for (const auto& t : tokens) {
          std::string s = toLower(trim(t));
          if (s == "sum" || s == "mean" || s == "min" || s == "max" || s == "range" || s == "std" ||
              s == "variance" || s == "cv" || s == "count" || s == "nodata_count" ||
              s == "median" || s == "p25" || s == "p75" || s == "variety" || s == "diversity" ||
              s == "majority" || s == "minority" || s == "sum_sq") {
            stats_to_calc.push_back(s);
          }
        }
      }
    } else if (arg == "-zone_id" && i + 1 < argc) {
      zone_id_attr = argv[++i];
    } else if (arg == "-keep_attrs") {
      keep_attrs = true;
    } else if (arg == "-histogram") {
      calc_histogram = true;
    } else if (arg == "-bins" && i + 1 < argc) {
      num_bins = std::stoi(argv[++i]);
    } else if (arg == "-verbose") {
      verbose = true;
    }
  }

  if (stats_to_calc.empty()) {
    stats_to_calc = {"sum", "mean", "min", "max", "count"};
  }

  std::cout << "========================================\n";
  std::cout << "  ZONAL STATISTICS\n";
  std::cout << "========================================\n\n";
  std::cout << "Raster: " << raster_file << "\n";
  std::cout << "Zones: " << zones_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Statistics: ";
  for (const auto& s : stats_to_calc) std::cout << s << " ";
  std::cout << "\n\n";

  Spatial::ASCIIGridReader reader;
  Spatial::RasterDataset raster;

  if (!reader.read(raster_file, raster)) {
    std::cerr << "Error: Could not read raster file\n";
    return 1;
  }

  std::cout << "Raster: " << raster.ncols << "x" << raster.nrows << " ("
            << (raster.ncols * raster.nrows) << " cells)\n";

  auto zones = readZones(zones_file, zone_id_attr);

  if (zones.empty()) {
    std::cerr << "Error: No zones found\n";
    return 1;
  }

  std::cout << "Zones: " << zones.size() << "\n\n";

  int total_processed = 0;

  for (auto& zone : zones) {
    std::vector<double> values;

    for (int r = 0; r < raster.nrows; ++r) {
      for (int c = 0; c < raster.ncols; ++c) {
        double x = raster.xllcorner + c * raster.cellsize + raster.cellsize / 2.0;
        double y =
            raster.yllcorner + (raster.nrows - 1 - r) * raster.cellsize + raster.cellsize / 2.0;

        if (pointInPolygon(x, y, zone.polygon)) {
          values.push_back(raster.at(r, c));
        }
      }
    }

    zone.stats = calculateStats(values, raster.nodata_value);
    total_processed += values.size();

    if (verbose) {
      std::cout << "Zone " << zone.id << ": " << zone.stats.count << " cells\n";
    }
  }

  std::cout << "Total cells processed: " << total_processed << "\n\n";

  writeZonalStatsCSV(zones, stats_to_calc, output_file, keep_attrs);

  if (calc_histogram) {
    std::string hist_file = output_file.substr(0, output_file.find_last_of('.')) + "_hist.csv";
    writeHistogramCSV(zones, hist_file, num_bins);
    std::cout << "Histogram written to: " << hist_file << "\n";
  }

  std::cout << "\n========================================\n";
  std::cout << "  ZONAL STATISTICS COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Output written to: " << output_file << "\n";

  return 0;
}

