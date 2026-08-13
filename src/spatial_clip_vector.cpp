#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

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

  bool contains(const std::vector<double>& coords) const {
    for (size_t i = 0; i < coords.size(); i += 2) {
      if (!contains(coords[i], coords[i + 1]))
        return false;
    }
    return true;
  }
};

bool polygonContainsFeature(const std::vector<double>& polygon,
                            const Spatial::VectorFeature& feature) {
  for (size_t i = 0; i < feature.coordinates.size(); i += 2) {
    if (pointInPolygon(feature.coordinates[i], feature.coordinates[i + 1], polygon)) {
      return true;
    }
  }
  return false;
}

std::vector<double> readPolygonFromCSV(const std::string& filename) {
  std::ifstream file(filename);
  if (!file.is_open())
    return {};

  std::string line;
  std::vector<double> polygon;
  bool is_header = true;

  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#')
      continue;

    if (is_header) {
      is_header = false;
      continue;
    }

    auto values = split(line, ',');
    for (const auto& val : values) {
      std::string v = trim(val);
      if (v.find("POLYGON") == 0) {
        size_t start = v.find('(');
        size_t end = v.rfind(')');
        if (start != std::string::npos && end != std::string::npos) {
          std::string inner = v.substr(start + 1, end - start - 1);
          inner.erase(std::remove(inner.begin(), inner.end(), '('), inner.end());
          inner.erase(std::remove(inner.begin(), inner.end(), ')'), inner.end());

          auto points = split(inner, ',');
          for (const auto& p : points) {
            auto coords = split(trim(p), ' ');
            if (coords.size() >= 2) {
              polygon.push_back(std::stod(coords[0]));
              polygon.push_back(std::stod(coords[1]));
            }
          }
        }
        break;
      }
    }
  }

  return polygon;
}

void printUsage() {
  std::cerr << "spatial_clip_vector - Clip vector data by spatial extent\n\n";
  std::cerr << "Usage: spatial_clip_vector <input> <output> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -bbox <minx> <miny> <maxx> <maxy>   Clip by bounding box\n";
  std::cerr << "  -polygon <file>                     Clip by polygon from CSV\n";
  std::cerr << "  -clip_to <file>                     Clip to extent of another file\n";
  std::cerr << "  -invert                            Invert clipping\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_clip_vector cities.csv area.csv -bbox -84.5 9.5 -84.0 10.0\n";
  std::cerr << "  spatial_clip_vector cities.csv polygon.csv -polygon boundary.csv\n";
  std::cerr << "  spatial_clip_vector cities.csv zone.csv -clip_to elevation.asc\n";
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

  Spatial::SpatialCSVReader reader;
  Spatial::VectorDataset dataset;

  if (!reader.read(input_file, dataset)) {
    std::cerr << "Error: Could not read input file\n";
    return 1;
  }

  std::cout << "Input features: " << dataset.features.size() << "\n";

  std::vector<double> polygon;

  if (use_polygon) {
    polygon = readPolygonFromCSV(polygon_file);
    if (polygon.empty()) {
      std::cerr << "Error: Could not read polygon from " << polygon_file << "\n";
      return 1;
    }
    std::cout << "Polygon vertices: " << polygon.size() / 2 << "\n";
  } else if (use_clip_to) {
    std::cerr << "Error: -clip_to not yet implemented\n";
    return 1;
  }

  Spatial::VectorDataset output;
  output.columns = dataset.columns;
  output.geometry_column = dataset.geometry_column;
  output.crs = dataset.crs;

  for (const auto& feature : dataset.features) {
    bool inside;

    if (use_polygon) {
      inside = polygonContainsFeature(polygon, feature);
    } else if (use_bbox) {
      inside = bbox.contains(feature.coordinates);
    } else {
      std::cerr << "Error: No clipping method specified\n";
      return 1;
    }

    if (invert ? !inside : inside) {
      output.features.push_back(feature);
    }
  }

  writeVectorCSV(output, output_file, {}, {"# Clipped from original dataset"});

  std::cout << "Clipped features: " << output.features.size() << "\n";
  std::cout << "Clipping complete.\n";

  return 0;
}

