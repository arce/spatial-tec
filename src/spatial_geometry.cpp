#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <vector>

#include "../include/core/spatial_csv.hpp"
#include "../include/core/spatial_geom.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

struct BoundingBox {
  double xmin, ymin, xmax, ymax;
  BoundingBox() : xmin(0), ymin(0), xmax(0), ymax(0) {}
  BoundingBox(double xmin, double ymin, double xmax, double ymax)
      : xmin(xmin), ymin(ymin), xmax(xmax), ymax(ymax) {}

  std::vector<double> toPolygonCoords() const {
    return {xmin, ymin, xmax, ymin, xmax, ymax, xmin, ymax, xmin, ymin};
  }
};

double calculateArea(const std::vector<double>& coords) {
  if (coords.size() < 6) {
    return 0.0;
  }

  double area = 0;
  int n = coords.size() / 2;

  for (int i = 0; i < n; ++i) {
    int j = (i + 1) % n;
    double xi = coords[i * 2];
    double yi = coords[i * 2 + 1];
    double xj = coords[j * 2];
    double yj = coords[j * 2 + 1];
    area += xi * yj - xj * yi;
  }

  return std::abs(area) / 2.0;
}

double calculatePerimeter(const std::vector<double>& coords) {
  if (coords.size() < 4) {
    return 0.0;
  }

  double perimeter = 0;
  int n = coords.size() / 2;

  for (int i = 0; i < n; ++i) {
    int j = (i + 1) % n;
    double dx = coords[j * 2] - coords[i * 2];
    double dy = coords[j * 2 + 1] - coords[i * 2 + 1];
    perimeter += std::sqrt(dx * dx + dy * dy);
  }

  return perimeter;
}

double calculateLength(const std::vector<double>& coords) {
  if (coords.size() < 4) {
    return 0.0;
  }

  double length = 0;
  int n = coords.size() / 2;

  for (int i = 0; i < n - 1; ++i) {
    double dx = coords[(i + 1) * 2] - coords[i * 2];
    double dy = coords[(i + 1) * 2 + 1] - coords[i * 2 + 1];
    length += std::sqrt(dx * dx + dy * dy);
  }

  return length;
}

size_t calculateVertexCount(const std::vector<double>& coords) {
  return coords.size() / 2;
}

BoundingBox calculateBoundingBox(const std::vector<double>& coords) {
  if (coords.size() < 2) {
    return BoundingBox(0, 0, 0, 0);
  }

  double xmin = coords[0], xmax = coords[0];
  double ymin = coords[1], ymax = coords[1];

  for (size_t i = 2; i < coords.size(); i += 2) {
    double x = coords[i];
    double y = coords[i + 1];
    if (x < xmin)
      xmin = x;
    if (x > xmax)
      xmax = x;
    if (y < ymin)
      ymin = y;
    if (y > ymax)
      ymax = y;
  }

  return BoundingBox(xmin, ymin, xmax, ymax);
}

struct GeometryStats {
  size_t total_features;
  size_t processed;
  size_t skipped;

  double total_area;
  double total_perimeter;
  double total_length;
  size_t total_vertices;

  GeometryStats()
      : total_features(0),
        processed(0),
        skipped(0),
        total_area(0),
        total_perimeter(0),
        total_length(0),
        total_vertices(0) {}
};

GeometryStats calculateGeometries(const Spatial::VectorDataset& input,
                                  Spatial::VectorDataset& output, bool calc_area,
                                  bool calc_perimeter, bool calc_length, bool calc_vertices,
                                  bool calc_bbox) {
  GeometryStats stats;
  stats.total_features = input.features.size();

  output.columns = input.columns;
  output.geometry_column = input.geometry_column;
  output.crs = input.crs;

  if (calc_area) {
    output.columns.push_back("area");
  }
  if (calc_perimeter) {
    output.columns.push_back("perimeter");
  }
  if (calc_length) {
    output.columns.push_back("length");
  }
  if (calc_vertices) {
    output.columns.push_back("vertices");
  }
  if (calc_bbox) {
    output.columns.push_back("bbox");
  }

  if (std::find(output.columns.begin(), output.columns.end(), "source_type") ==
      output.columns.end()) {
    output.columns.push_back("source_type");
  }

  for (const auto& feature : input.features) {
    std::string type_name;
    bool is_polygon = false;
    bool is_line = false;
    bool is_point = false;

    switch (feature.type) {
      case Spatial::VectorFeature::GeometryType::POINT:
        type_name = "point";
        is_point = true;
        break;
      case Spatial::VectorFeature::GeometryType::LINESTRING:
        type_name = "line";
        is_line = true;
        break;
      case Spatial::VectorFeature::GeometryType::POLYGON:
        type_name = "polygon";
        is_polygon = true;
        break;
      default:
        type_name = "unknown";
        stats.skipped++;
        continue;
    }

    stats.processed++;

    Spatial::VectorFeature result = feature;

    result.attributes["source_type"] = type_name;

    if (calc_area && is_polygon) {
      double area = calculateArea(feature.coordinates);
      result.attributes["area"] = std::to_string(area);
      stats.total_area += area;
    } else if (calc_area) {
      result.attributes["area"] = "0";
    }

    if (calc_perimeter && is_polygon) {
      double perimeter = calculatePerimeter(feature.coordinates);
      result.attributes["perimeter"] = std::to_string(perimeter);
      stats.total_perimeter += perimeter;
    } else if (calc_perimeter) {
      result.attributes["perimeter"] = "0";
    }

    if (calc_length && is_line) {
      double length = calculateLength(feature.coordinates);
      result.attributes["length"] = std::to_string(length);
      stats.total_length += length;
    } else if (calc_length) {
      result.attributes["length"] = "0";
    }

    if (calc_vertices) {
      size_t vertices = calculateVertexCount(feature.coordinates);
      result.attributes["vertices"] = std::to_string(vertices);
      stats.total_vertices += vertices;
    }

    if (calc_bbox) {
      BoundingBox bbox = calculateBoundingBox(feature.coordinates);
      std::string wkt = "POLYGON ((" + std::to_string(bbox.xmin) + " " + std::to_string(bbox.ymin) +
                        ", " + std::to_string(bbox.xmax) + " " + std::to_string(bbox.ymin) + ", " +
                        std::to_string(bbox.xmax) + " " + std::to_string(bbox.ymax) + ", " +
                        std::to_string(bbox.xmin) + " " + std::to_string(bbox.ymax) + ", " +
                        std::to_string(bbox.xmin) + " " + std::to_string(bbox.ymin) + "))";
      result.attributes["bbox"] = "\"" + wkt + "\"";
    }

    output.features.push_back(result);
  }

  return stats;
}

void writeVectorCSV(const Spatial::VectorDataset& dataset, const std::string& filename,
                    const std::vector<std::string>& comments = {},
                    const std::vector<std::string>& footers = {}) {
  std::ofstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open output file: " << filename << "\n";
    return;
  }

  for (const auto& comment : comments) {
    file << "# " << comment << "\n";
  }

  for (size_t i = 0; i < dataset.columns.size(); ++i) {
    if (i > 0)
      file << ",";
    file << dataset.columns[i];
  }
  file << "\n";

  for (const auto& feature : dataset.features) {
    for (size_t i = 0; i < dataset.columns.size(); ++i) {
      if (i > 0)
        file << ",";

      const std::string& col = dataset.columns[i];

      if (col == dataset.geometry_column) {
        std::string wkt;
        switch (feature.type) {
          case Spatial::VectorFeature::GeometryType::POINT:
            wkt = "POINT (" + std::to_string(feature.coordinates[0]) + " " +
                  std::to_string(feature.coordinates[1]) + ")";
            break;
          case Spatial::VectorFeature::GeometryType::LINESTRING:
            wkt = "LINESTRING (";
            for (size_t j = 0; j < feature.coordinates.size(); j += 2) {
              if (j > 0)
                wkt += ", ";
              wkt += std::to_string(feature.coordinates[j]) + " " +
                     std::to_string(feature.coordinates[j + 1]);
            }
            wkt += ")";
            break;
          case Spatial::VectorFeature::GeometryType::POLYGON:
            wkt = "POLYGON ((";
            for (size_t j = 0; j < feature.coordinates.size(); j += 2) {
              if (j > 0)
                wkt += ", ";
              wkt += std::to_string(feature.coordinates[j]) + " " +
                     std::to_string(feature.coordinates[j + 1]);
            }
            wkt += "))";
            break;
          default:
            wkt = "";
        }
        file << "\"" << wkt << "\"";
      } else {
        auto it = feature.attributes.find(col);
        if (it != feature.attributes.end()) {
          std::string value = it->second;
          if (value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.length() - 2);
          }
          file << value;
        }
      }
    }
    file << "\n";
  }

  for (const auto& footer : footers) {
    file << "# " << footer << "\n";
  }

  file.close();
}

void printUsage() {
  std::cerr << "spatial_geometry - Calculate geometric properties of vector geometries\n\n";
  std::cerr << "Usage: spatial_geometry <input> <output> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -area              Calculate area (polygons only)\n";
  std::cerr << "  -perimeter         Calculate perimeter (polygons only)\n";
  std::cerr << "  -length            Calculate length (lines only)\n";
  std::cerr << "  -vertices          Count vertices\n";
  std::cerr << "  -bbox              Calculate bounding box as POLYGON geometry\n";
  std::cerr << "  -all               Calculate all properties\n";
  std::cerr << "  -verbose           Show detailed statistics\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_geometry polygons.csv output.csv -area -perimeter -vertices\n";
  std::cerr << "  spatial_geometry polygons.csv output.csv -bbox\n";
  std::cerr << "  spatial_geometry mixed.csv output.csv -all\n";
  std::cerr << "  spatial_geometry lines.csv output.csv -length -vertices\n\n";
  std::cerr << "Note: All attributes from the input features are copied to the output.\n";
  std::cerr << "A 'source_type' column indicates the original geometry type.\n";
  std::cerr << "Area, perimeter and length are in the same units as the coordinates.\n";
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printUsage();
    return 1;
  }

  std::string input_file;
  std::string output_file;
  bool calc_area = false;
  bool calc_perimeter = false;
  bool calc_length = false;
  bool calc_vertices = false;
  bool calc_bbox = false;
  bool verbose = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-area") {
      calc_area = true;
    } else if (arg == "-perimeter") {
      calc_perimeter = true;
    } else if (arg == "-length") {
      calc_length = true;
    } else if (arg == "-vertices") {
      calc_vertices = true;
    } else if (arg == "-bbox") {
      calc_bbox = true;
    } else if (arg == "-all") {
      calc_area = true;
      calc_perimeter = true;
      calc_length = true;
      calc_vertices = true;
      calc_bbox = true;
    } else if (arg == "-verbose") {
      verbose = true;
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

  if (!calc_area && !calc_perimeter && !calc_length && !calc_vertices && !calc_bbox) {
    std::cerr << "Error: At least one calculation option must be specified\n";
    std::cerr << "Use -area, -perimeter, -length, -vertices, -bbox, or -all\n";
    return 1;
  }

  std::cout << "========================================\n";
  std::cout << "  SPATIAL GEOMETRY\n";
  std::cout << "========================================\n\n";
  std::cout << "Input: " << input_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Calculations:\n";
  if (calc_area)
    std::cout << "  - Area\n";
  if (calc_perimeter)
    std::cout << "  - Perimeter\n";
  if (calc_length)
    std::cout << "  - Length\n";
  if (calc_vertices)
    std::cout << "  - Vertex count\n";
  if (calc_bbox)
    std::cout << "  - Bounding box (as POLYGON)\n";
  std::cout << "\n";

  Spatial::SpatialCSVReader reader;
  Spatial::VectorDataset dataset;

  if (!reader.read(input_file, dataset)) {
    std::cerr << "Error: Could not read input file\n";
    return 1;
  }

  std::cout << "Input features: " << dataset.features.size() << "\n";

  size_t points = 0, lines = 0, polygons = 0;
  for (const auto& f : dataset.features) {
    switch (f.type) {
      case Spatial::VectorFeature::GeometryType::POINT:
        points++;
        break;
      case Spatial::VectorFeature::GeometryType::LINESTRING:
        lines++;
        break;
      case Spatial::VectorFeature::GeometryType::POLYGON:
        polygons++;
        break;
      default:
        break;
    }
  }
  std::cout << "  Points: " << points << "\n";
  std::cout << "  Lines: " << lines << "\n";
  std::cout << "  Polygons: " << polygons << "\n\n";

  Spatial::VectorDataset output;
  GeometryStats stats = calculateGeometries(dataset, output, calc_area, calc_perimeter, calc_length,
                                            calc_vertices, calc_bbox);

  std::vector<std::string> comments;
  std::string calc_str;
  if (calc_area)
    calc_str += "area,";
  if (calc_perimeter)
    calc_str += "perimeter,";
  if (calc_length)
    calc_str += "length,";
  if (calc_vertices)
    calc_str += "vertices,";
  if (calc_bbox)
    calc_str += "bbox";
  if (!calc_str.empty() && calc_str.back() == ',')
    calc_str.pop_back();
  comments.push_back("Calculated: " + calc_str);

  writeVectorCSV(output, output_file, comments,
                 {"Total features: " + std::to_string(output.features.size())});

  std::cout << "========================================\n";
  std::cout << "  GEOMETRY CALCULATION COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Total features: " << stats.total_features << "\n";
  std::cout << "Processed: " << stats.processed << "\n";
  std::cout << "Skipped: " << stats.skipped << "\n";

  if (calc_area) {
    std::cout << "Total area: " << std::fixed << std::setprecision(6) << stats.total_area << "\n";
  }
  if (calc_perimeter) {
    std::cout << "Total perimeter: " << std::fixed << std::setprecision(6) << stats.total_perimeter
              << "\n";
  }
  if (calc_length) {
    std::cout << "Total length: " << std::fixed << std::setprecision(6) << stats.total_length
              << "\n";
  }
  if (calc_vertices) {
    std::cout << "Total vertices: " << stats.total_vertices << "\n";
  }

  std::cout << "\nOutput features: " << output.features.size() << "\n";
  std::cout << "Output written to: " << output_file << "\n";

  if (verbose) {
    std::cout << "\nColumns in output:\n";
    for (const auto& col : output.columns) {
      std::cout << "  - " << col << "\n";
    }
  }

  return 0;
}
