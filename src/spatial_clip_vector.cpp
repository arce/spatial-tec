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

bool anyPolygonContainsFeature(const std::vector<std::vector<double>>& polygons,
                               const Spatial::VectorFeature& feature) {
  for (const auto& polygon : polygons) {
    if (polygonContainsFeature(polygon, feature)) {
      return true;
    }
  }
  return false;
}

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

void printUsage() {
  std::cerr << "spatial_clip_vector - Clip vector data by spatial extent\n\n";
  std::cerr << "Usage: spatial_clip_vector <input> <output> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -bbox <minx> <miny> <maxx> <maxy>   Clip by bounding box\n";
  std::cerr << "  -polygon <file>                     Clip by polygon(s) from CSV -- every\n";
  std::cerr << "                                       POLYGON/MULTIPOLYGON row is tested\n";
  std::cerr << "                                       independently, a feature matching any\n";
  std::cerr << "                                       one of them is kept\n";
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

  std::vector<std::vector<double>> polygons;

  if (use_polygon) {
    polygons = readPolygonsFromCSV(polygon_file);
    if (polygons.empty()) {
      std::cerr << "Error: Could not read any POLYGON/MULTIPOLYGON feature from " << polygon_file
                << "\n";
      return 1;
    }
    size_t total_vertices = 0;
    for (const auto& p : polygons) total_vertices += p.size() / 2;
    std::cout << "Polygons: " << polygons.size() << " (" << total_vertices << " vertices total)\n";
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
      inside = anyPolygonContainsFeature(polygons, feature);
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

