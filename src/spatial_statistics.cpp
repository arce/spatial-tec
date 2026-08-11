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

struct Statistics {
  double min_val;
  double max_val;
  double mean_val;
  double std_val;
  double sum_val;
  size_t count;
  size_t nodata_count;
  size_t total_count;
  double median_val;
  double p25_val;
  double p75_val;

  Statistics()
      : min_val(0),
        max_val(0),
        mean_val(0),
        std_val(0),
        sum_val(0),
        count(0),
        nodata_count(0),
        total_count(0),
        median_val(0),
        p25_val(0),
        p75_val(0) {}
};

struct Bin {
  double low;
  double high;
  size_t count;
  Bin() : low(0), high(0), count(0) {}
  Bin(double l, double h) : low(l), high(h), count(0) {}
};

Statistics calculateStatistics(const std::vector<double>& values, double nodata) {
  Statistics stats;

  std::vector<double> valid;
  for (double v : values) {
    if (std::abs(v - nodata) > 1e-9) {
      valid.push_back(v);
    } else {
      stats.nodata_count++;
    }
  }

  stats.total_count = values.size();
  stats.count = valid.size();

  if (valid.empty()) {
    return stats;
  }

  std::vector<double> sorted = valid;
  std::sort(sorted.begin(), sorted.end());

  stats.min_val = sorted.front();
  stats.max_val = sorted.back();

  stats.sum_val = std::accumulate(valid.begin(), valid.end(), 0.0);
  stats.mean_val = stats.sum_val / valid.size();

  double sq_sum = 0.0;
  for (double v : valid) {
    double diff = v - stats.mean_val;
    sq_sum += diff * diff;
  }
  stats.std_val = std::sqrt(sq_sum / valid.size());

  stats.median_val = sorted[sorted.size() / 2];
  stats.p25_val = sorted[sorted.size() / 4];
  stats.p75_val = sorted[sorted.size() * 3 / 4];

  return stats;
}

std::vector<Bin> calculateHistogram(const std::vector<double>& values, int num_bins, double nodata,
                                    double min_val = 0, double max_val = 0) {
  std::vector<Bin> bins;

  std::vector<double> valid;
  for (double v : values) {
    if (std::abs(v - nodata) > 1e-9) {
      valid.push_back(v);
    }
  }

  if (valid.empty()) {
    return bins;
  }

  if (min_val == 0 && max_val == 0) {
    min_val = *std::min_element(valid.begin(), valid.end());
    max_val = *std::max_element(valid.begin(), valid.end());
  }

  double range = max_val - min_val;
  if (range < 1e-12) {
    Bin b(min_val, min_val + 1);
    b.count = valid.size();
    bins.push_back(b);
    return bins;
  }

  double bin_width = range / num_bins;

  for (int i = 0; i < num_bins; ++i) {
    double low = min_val + i * bin_width;
    double high = low + bin_width;
    bins.push_back(Bin(low, high));
  }

  for (double v : valid) {
    int idx = static_cast<int>((v - min_val) / bin_width);
    if (idx >= num_bins)
      idx = num_bins - 1;
    if (idx < 0)
      idx = 0;
    bins[idx].count++;
  }

  return bins;
}

std::vector<double> getRasterValues(const Spatial::RasterDataset& dataset) {
  std::vector<double> values;
  values.reserve(dataset.nrows * dataset.ncols);

  for (const auto& v : dataset.data) {
    values.push_back(v);
  }

  return values;
}

std::vector<double> getRasterValuesInZones(const Spatial::RasterDataset& raster,
                                           const Spatial::VectorDataset& zones,
                                           const std::string& zone_id) {
  std::vector<double> values;

  for (int r = 0; r < raster.nrows; ++r) {
    for (int c = 0; c < raster.ncols; ++c) {
      double x = raster.xllcorner + c * raster.cellsize + raster.cellsize / 2.0;
      double y =
          raster.yllcorner + (raster.nrows - 1 - r) * raster.cellsize + raster.cellsize / 2.0;
      double val = raster.at(r, c);

      if (std::abs(val - raster.nodata_value) < 1e-9)
        continue;

      for (const auto& zone : zones.features) {
        bool inside = false;

        if (zone.type == Spatial::VectorFeature::GeometryType::POLYGON) {
          inside = true;
        }
        if (inside) {
          values.push_back(val);
          break;
        }
      }
    }
  }

  return values;
}

std::vector<double> getVectorAttributeValues(const Spatial::VectorDataset& dataset,
                                             const std::string& attribute) {
  std::vector<double> values;

  for (const auto& feature : dataset.features) {
    auto it = feature.attributes.find(attribute);
    if (it != feature.attributes.end()) {
      try {
        double val = std::stod(it->second);
        values.push_back(val);
      } catch (...) {
      }
    }
  }

  return values;
}

void writeStatisticsCSV(const Statistics& stats, const std::string& filename,
                        const std::string& prefix = "") {
  std::ofstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not create file: " << filename << "\n";
    return;
  }

  file << "statistic,value\n";
  file << prefix << "count," << stats.count << "\n";
  file << prefix << "nodata_count," << stats.nodata_count << "\n";
  file << prefix << "total_count," << stats.total_count << "\n";
  file << prefix << "min," << std::fixed << std::setprecision(6) << stats.min_val << "\n";
  file << prefix << "max," << std::fixed << std::setprecision(6) << stats.max_val << "\n";
  file << prefix << "mean," << std::fixed << std::setprecision(6) << stats.mean_val << "\n";
  file << prefix << "std," << std::fixed << std::setprecision(6) << stats.std_val << "\n";
  file << prefix << "sum," << std::fixed << std::setprecision(6) << stats.sum_val << "\n";
  file << prefix << "median," << std::fixed << std::setprecision(6) << stats.median_val << "\n";
  file << prefix << "p25," << std::fixed << std::setprecision(6) << stats.p25_val << "\n";
  file << prefix << "p75," << std::fixed << std::setprecision(6) << stats.p75_val << "\n";
}

void writeHistogramCSV(const std::vector<Bin>& bins, const std::string& filename) {
  std::ofstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not create file: " << filename << "\n";
    return;
  }

  file << "bin_low,bin_high,count\n";
  for (const auto& bin : bins) {
    file << std::fixed << std::setprecision(6) << bin.low << "," << std::fixed
         << std::setprecision(6) << bin.high << "," << bin.count << "\n";
  }
}

void printStatistics(const Statistics& stats, const std::string& prefix = "") {
  std::cout << prefix << "Count: " << stats.count << "\n";
  std::cout << prefix << "NODATA: " << stats.nodata_count << "\n";
  std::cout << prefix << "Total: " << stats.total_count << "\n";
  std::cout << prefix << "Min: " << std::fixed << std::setprecision(6) << stats.min_val << "\n";
  std::cout << prefix << "Max: " << std::fixed << std::setprecision(6) << stats.max_val << "\n";
  std::cout << prefix << "Mean: " << std::fixed << std::setprecision(6) << stats.mean_val << "\n";
  std::cout << prefix << "Std: " << std::fixed << std::setprecision(6) << stats.std_val << "\n";
  std::cout << prefix << "Sum: " << std::fixed << std::setprecision(6) << stats.sum_val << "\n";
  std::cout << prefix << "Median: " << std::fixed << std::setprecision(6) << stats.median_val
            << "\n";
  std::cout << prefix << "P25: " << std::fixed << std::setprecision(6) << stats.p25_val << "\n";
  std::cout << prefix << "P75: " << std::fixed << std::setprecision(6) << stats.p75_val << "\n";
}

void printHistogram(const std::vector<Bin>& bins) {
  std::cout << "\nHistogram:\n";
  std::cout << "  Range -> Count\n";

  size_t max_count = 0;
  for (const auto& bin : bins) {
    max_count = std::max(max_count, bin.count);
  }

  int max_bar_width = 50;
  for (const auto& bin : bins) {
    int bar_width = max_count > 0 ? (bin.count * max_bar_width / max_count) : 0;
    std::cout << "  [" << std::fixed << std::setprecision(2) << bin.low << " - " << std::fixed
              << std::setprecision(2) << bin.high << "]: " << std::setw(6) << bin.count << " "
              << std::string(bar_width, '#') << "\n";
  }
}

void printUsage() {
  std::cerr << "spatial_statistics - Calculate spatial statistics\n\n";
  std::cerr << "Usage: spatial_statistics <input> [options]\n\n";
  std::cerr << "Options for raster:\n";
  std::cerr << "  -stats                Calculate basic statistics\n";
  std::cerr << "  -histogram            Generate histogram\n";
  std::cerr << "  -bins <n>             Number of histogram bins (default: 10)\n";
  std::cerr << "  -zones <file>         Calculate zonal statistics (vector zones)\n";
  std::cerr << "  -zone_id <attr>       Attribute for zone identification\n\n";
  std::cerr << "Options for vector:\n";
  std::cerr << "  -attribute <col>      Attribute column to analyze\n";
  std::cerr << "  -stat <stat1,stat2>   Statistics to calculate\n\n";
  std::cerr << "General options:\n";
  std::cerr << "  -output <file>        Output CSV file\n";
  std::cerr << "  -verbose              Verbose output\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_statistics elev.asc -stats\n";
  std::cerr << "  spatial_statistics elev.asc -stats -histogram -bins 20\n";
  std::cerr << "  spatial_statistics elev.asc -zones zones.csv -stat mean,min,max\n";
  std::cerr << "  spatial_statistics cities.csv -attribute population -stat min,max,mean\n";
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    printUsage();
    return 1;
  }

  std::string input_file;
  std::string output_file;
  std::string zones_file;
  std::string zone_id_attr = "id";
  std::string attribute;
  std::string stats_to_calc;
  bool calc_stats = false;
  bool calc_histogram = false;
  int num_bins = 10;
  bool verbose = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-stats") {
      calc_stats = true;
    } else if (arg == "-histogram") {
      calc_histogram = true;
    } else if (arg == "-bins" && i + 1 < argc) {
      num_bins = std::stoi(argv[++i]);
    } else if (arg == "-zones" && i + 1 < argc) {
      zones_file = argv[++i];
    } else if (arg == "-zone_id" && i + 1 < argc) {
      zone_id_attr = argv[++i];
    } else if (arg == "-attribute" && i + 1 < argc) {
      attribute = argv[++i];
    } else if (arg == "-stat" && i + 1 < argc) {
      stats_to_calc = argv[++i];
      calc_stats = true;
    } else if (arg == "-output" && i + 1 < argc) {
      output_file = argv[++i];
    } else if (arg == "-verbose") {
      verbose = true;
    } else if (input_file.empty()) {
      input_file = arg;
    }
  }

  if (input_file.empty()) {
    std::cerr << "Error: Input file required\n";
    return 1;
  }

  std::string ext = std::filesystem::path(input_file).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  bool is_raster = (ext == ".asc" || ext == ".grd");
  bool is_vector = (ext == ".csv" || ext == ".tsv");

  if (!is_raster && !is_vector) {
    std::cerr << "Error: Unsupported file format\n";
    return 1;
  }

  std::cout << "========================================\n";
  std::cout << "  SPATIAL STATISTICS\n";
  std::cout << "========================================\n\n";
  std::cout << "Input: " << input_file << "\n";

  if (is_raster) {
    Spatial::ASCIIGridReader reader;
    Spatial::RasterDataset dataset;

    if (!reader.read(input_file, dataset)) {
      std::cerr << "Error: Could not read raster file\n";
      return 1;
    }

    std::cout << "Type: Raster\n";
    std::cout << "Dimensions: " << dataset.ncols << "x" << dataset.nrows << "\n";
    std::cout << "Cells: " << (dataset.ncols * dataset.nrows) << "\n\n";

    if (!zones_file.empty()) {
      std::cout << "Zonal statistics (zones: " << zones_file << ")\n";
      std::cout << "========================================\n\n";

      Spatial::SpatialCSVReader csv_reader;
      Spatial::VectorDataset zones;

      if (!csv_reader.read(zones_file, zones)) {
        std::cerr << "Error: Could not read zones file\n";
        return 1;
      }

      std::cout << "Zones: " << zones.features.size() << "\n\n";

      if (output_file.empty()) {
        output_file = "zonal_stats.csv";
      }

      std::ofstream out_file(output_file);
      if (!out_file.is_open()) {
        std::cerr << "Error: Could not create output file\n";
        return 1;
      }

      out_file << "zone_id,count,min,max,mean,std,sum\n";

      int zone_idx = 0;
      for (const auto& zone : zones.features) {
        std::vector<double> values;
        std::string zone_id = std::to_string(zone_idx++);

        auto id_it = zone.attributes.find(zone_id_attr);
        if (id_it != zone.attributes.end()) {
          zone_id = id_it->second;
        }

        values = getRasterValues(dataset);

        Statistics stats = calculateStatistics(values, dataset.nodata_value);

        std::cout << "Zone " << zone_id << ":\n";
        printStatistics(stats, "  ");
        std::cout << "\n";

        out_file << zone_id << "," << stats.count << "," << std::fixed << std::setprecision(6)
                 << stats.min_val << "," << std::fixed << std::setprecision(6) << stats.max_val
                 << "," << std::fixed << std::setprecision(6) << stats.mean_val << "," << std::fixed
                 << std::setprecision(6) << stats.std_val << "," << std::fixed
                 << std::setprecision(6) << stats.sum_val << "\n";
      }

      out_file.close();
      std::cout << "Zonal statistics written to: " << output_file << "\n";

    } else {
      std::vector<double> values = getRasterValues(dataset);

      if (calc_stats) {
        Statistics stats = calculateStatistics(values, dataset.nodata_value);
        std::cout << "Basic Statistics:\n";
        std::cout << "========================================\n";
        printStatistics(stats);
        std::cout << "\n";

        if (!output_file.empty()) {
          writeStatisticsCSV(stats, output_file);
          std::cout << "Statistics written to: " << output_file << "\n";
        }
      }

      if (calc_histogram) {
        std::cout << "Histogram (bins: " << num_bins << ")\n";
        std::cout << "========================================\n";
        auto bins = calculateHistogram(values, num_bins, dataset.nodata_value);
        printHistogram(bins);
        std::cout << "\n";

        if (!output_file.empty() && !calc_stats) {
          writeHistogramCSV(bins, output_file);
          std::cout << "Histogram written to: " << output_file << "\n";
        }
      }
    }

  } else if (is_vector) {
    Spatial::SpatialCSVReader csv_reader;
    Spatial::VectorDataset dataset;

    if (!csv_reader.read(input_file, dataset)) {
      std::cerr << "Error: Could not read vector file\n";
      return 1;
    }

    std::cout << "Type: Vector\n";
    std::cout << "Features: " << dataset.features.size() << "\n";
    std::cout << "Columns: ";
    for (const auto& col : dataset.columns) {
      std::cout << col << " ";
    }
    std::cout << "\n\n";

    if (attribute.empty()) {
      std::cerr << "Error: -attribute required for vector statistics\n";
      return 1;
    }

    bool found = false;
    for (const auto& col : dataset.columns) {
      if (col == attribute) {
        found = true;
        break;
      }
    }

    if (!found) {
      std::cerr << "Error: Attribute '" << attribute << "' not found in dataset\n";
      return 1;
    }

    std::vector<double> values = getVectorAttributeValues(dataset, attribute);

    std::cout << "Attribute: " << attribute << "\n";
    std::cout << "Valid values: " << values.size() << " / " << dataset.features.size() << "\n\n";

    if (calc_stats) {
      Statistics stats = calculateStatistics(values, 0);
      std::cout << "Basic Statistics:\n";
      std::cout << "========================================\n";
      printStatistics(stats);
      std::cout << "\n";

      if (!output_file.empty()) {
        writeStatisticsCSV(stats, output_file, attribute + "_");
        std::cout << "Statistics written to: " << output_file << "\n";
      }
    }

    if (calc_histogram) {
      std::cout << "Histogram (bins: " << num_bins << ")\n";
      std::cout << "========================================\n";
      auto bins = calculateHistogram(values, num_bins, 0);
      printHistogram(bins);
      std::cout << "\n";

      if (!output_file.empty() && !calc_stats) {
        writeHistogramCSV(bins, output_file);
        std::cout << "Histogram written to: " << output_file << "\n";
      }
    }
  }

  std::cout << "\n========================================\n";
  std::cout << "  STATISTICS COMPLETE\n";
  std::cout << "========================================\n";

  return 0;
}
