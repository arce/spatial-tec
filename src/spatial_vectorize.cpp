#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
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
    std::cout << "Polygonizing...\n";
    std::cout << "Merging contiguous cells that share the same value into single polygons.\n\n";

    output.columns.push_back("cell_count");

    int nrows = dataset.nrows;
    int ncols = dataset.ncols;

    auto isActive = [&](int r, int c) -> bool {
      if (r < 0 || r >= nrows || c < 0 || c >= ncols) return false;
      double v = dataset.at(r, c);
      if (std::abs(v - dataset.nodata_value) < 1e-9) return false;
      if (use_filter && std::abs(v - filter_value) > 1e-9) return false;
      return true;
    };

    auto sameValue = [](double a, double b) { return std::abs(a - b) < 1e-9; };

    // --- Step 1: connected-component labeling (4-connectivity, equal value) ---
    std::vector<int> region_id(static_cast<size_t>(nrows) * static_cast<size_t>(ncols), -1);
    std::vector<double> region_value;
    std::vector<long long> region_cell_count;

    const int dr[4] = {-1, 1, 0, 0};
    const int dc[4] = {0, 0, -1, 1};

    for (int r = 0; r < nrows; ++r) {
      for (int c = 0; c < ncols; ++c) {
        size_t idx = static_cast<size_t>(r) * ncols + c;
        if (region_id[idx] != -1 || !isActive(r, c)) continue;

        double val = dataset.at(r, c);
        int rid = static_cast<int>(region_value.size());
        region_value.push_back(val);
        region_cell_count.push_back(0);

        std::vector<std::pair<int, int>> stack;
        stack.push_back({r, c});
        region_id[idx] = rid;

        while (!stack.empty()) {
          auto cell = stack.back();
          stack.pop_back();
          region_cell_count[rid]++;

          for (int k = 0; k < 4; ++k) {
            int nr = cell.first + dr[k];
            int nc = cell.second + dc[k];
            if (!isActive(nr, nc)) continue;
            size_t nidx = static_cast<size_t>(nr) * ncols + nc;
            if (region_id[nidx] != -1) continue;
            if (!sameValue(dataset.at(nr, nc), val)) continue;
            region_id[nidx] = rid;
            stack.push_back({nr, nc});
          }
        }
      }
    }

    int num_regions = static_cast<int>(region_value.size());
    std::cout << "Connected regions found: " << num_regions << "\n";

    // --- Step 2: boundary tracing for each region ---
    // Grid corner nodes: node(r, c) for r in [0, nrows], c in [0, ncols].
    auto nodeX = [&](int c) { return dataset.xllcorner + c * dataset.cellsize; };
    auto nodeY = [&](int r) { return dataset.yllcorner + (nrows - r) * dataset.cellsize; };
    int node_width = ncols + 1;
    auto nodeId = [&](int r, int c) -> long long {
      return static_cast<long long>(r) * node_width + c;
    };

    auto inRegion = [&](int r, int c, int rid) -> bool {
      if (r < 0 || r >= nrows || c < 0 || c >= ncols) return false;
      return region_id[static_cast<size_t>(r) * ncols + c] == rid;
    };

    int processed = 0;

    for (int rid = 0; rid < num_regions; ++rid) {
      std::unordered_map<long long, std::vector<long long>> edges_from;

      for (int r = 0; r < nrows; ++r) {
        for (int c = 0; c < ncols; ++c) {
          if (!inRegion(r, c, rid)) continue;

          if (!inRegion(r - 1, c, rid))  // top of cell is a boundary
            edges_from[nodeId(r, c + 1)].push_back(nodeId(r, c));
          if (!inRegion(r + 1, c, rid))  // bottom of cell is a boundary
            edges_from[nodeId(r + 1, c)].push_back(nodeId(r + 1, c + 1));
          if (!inRegion(r, c - 1, rid))  // left of cell is a boundary
            edges_from[nodeId(r, c)].push_back(nodeId(r + 1, c));
          if (!inRegion(r, c + 1, rid))  // right of cell is a boundary
            edges_from[nodeId(r + 1, c + 1)].push_back(nodeId(r, c + 1));
        }
      }

      // Consume the directed boundary edges into one or more closed rings.
      // (A region can trace to more than one ring at a "pinch point" where
      // it touches itself only diagonally -- each ring is still emitted.)
      std::unordered_map<long long, size_t> cursor;
      std::vector<std::vector<long long>> rings;
      long long max_steps = static_cast<long long>(nrows) * ncols * 4 + 16;

      for (auto& entry : edges_from) {
        long long start = entry.first;
        while (cursor[start] < entry.second.size()) {
          std::vector<long long> ring;
          long long current = start;
          ring.push_back(current);
          bool closed = false;

          for (long long step = 0; step < max_steps; ++step) {
            auto it = edges_from.find(current);
            if (it == edges_from.end()) break;
            size_t& used = cursor[current];
            if (used >= it->second.size()) break;
            long long next = it->second[used++];
            ring.push_back(next);
            current = next;
            if (current == start) {
              closed = true;
              break;
            }
          }

          if (closed && ring.size() >= 5) {
            rings.push_back(std::move(ring));
          }
        }
      }

      if (rings.empty()) continue;

      std::vector<std::vector<double>> ring_coords;
      ring_coords.reserve(rings.size());
      for (auto& ring : rings) {
        std::vector<double> coords;
        coords.reserve(ring.size() * 2);
        for (long long node : ring) {
          int nr = static_cast<int>(node / node_width);
          int nc = static_cast<int>(node % node_width);
          coords.push_back(nodeX(nc));
          coords.push_back(nodeY(nr));
        }
        ring_coords.push_back(std::move(coords));
      }
      // Largest ring first (treated as the outer boundary of the region).
      std::sort(ring_coords.begin(), ring_coords.end(),
                [](const std::vector<double>& a, const std::vector<double>& b) {
                  return polygonArea(a) > polygonArea(b);
                });

      Spatial::VectorFeature feature;
      feature.attributes["id"] = std::to_string(++feature_id);
      feature.attributes["value"] = std::to_string(region_value[rid]);
      feature.attributes["cell_count"] = std::to_string(region_cell_count[rid]);

      if (ring_coords.size() == 1) {
        feature.type = Spatial::VectorFeature::GeometryType::POLYGON;
        feature.coordinates = ring_coords[0];
      } else {
        feature.type = Spatial::VectorFeature::GeometryType::MULTIPOLYGON;
        for (const auto& coords : ring_coords) {
          feature.part_starts.push_back(feature.coordinates.size() / 2);
          feature.coordinates.insert(feature.coordinates.end(), coords.begin(), coords.end());
        }
      }

      output.features.push_back(feature);
      processed++;
    }

    std::cout << "Polygons created: " << processed << "\n";

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

