#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <vector>

#include "../include/core/spatial_csv.hpp"
#include "../include/core/spatial_io.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

struct Point2D {
  double x, y;
  Point2D() : x(0), y(0) {}
  Point2D(double x, double y) : x(x), y(y) {}

  Point2D operator+(const Point2D& other) const {
    return Point2D(x + other.x, y + other.y);
  }

  Point2D operator-(const Point2D& other) const {
    return Point2D(x - other.x, y - other.y);
  }

  Point2D operator*(double scalar) const {
    return Point2D(x * scalar, y * scalar);
  }

  double length() const {
    return std::sqrt(x * x + y * y);
  }

  Point2D normalized() const {
    double len = length();
    if (len < 1e-12)
      return Point2D(0, 0);
    return Point2D(x / len, y / len);
  }

  Point2D perpendicular() const {
    return Point2D(-y, x);
  }
};

std::vector<Point2D> generateCircle(double cx, double cy, double radius, int segments = 36) {
  std::vector<Point2D> points;
  for (int i = 0; i <= segments; ++i) {
    double angle = 2.0 * M_PI * i / segments;
    points.push_back(Point2D(cx + radius * std::cos(angle), cy + radius * std::sin(angle)));
  }
  return points;
}

std::vector<Point2D> bufferPoint(const Point2D& point, double distance, int segments = 36) {
  return generateCircle(point.x, point.y, distance, segments);
}

std::vector<Point2D> bufferLine(const std::vector<Point2D>& line, double distance,
                                int segments = 16, bool cap_round = true) {
  if (line.size() < 2)
    return std::vector<Point2D>();

  std::vector<Point2D> result;
  std::vector<Point2D> left_points;
  std::vector<Point2D> right_points;

  for (size_t i = 0; i < line.size() - 1; ++i) {
    Point2D p1 = line[i];
    Point2D p2 = line[i + 1];
    Point2D dir = (p2 - p1).normalized();
    Point2D normal = dir.perpendicular();

    Point2D left1 = p1 + normal * distance;
    Point2D right1 = p1 - normal * distance;
    Point2D left2 = p2 + normal * distance;
    Point2D right2 = p2 - normal * distance;

    left_points.push_back(left1);
    left_points.push_back(left2);
    right_points.push_back(right1);
    right_points.push_back(right2);
  }

  for (const auto& p : left_points) {
    result.push_back(p);
  }

  if (cap_round && !line.empty()) {
    Point2D last = line.back();
    auto end_circle = generateCircle(last.x, last.y, distance, segments);

    for (int i = segments / 2; i <= segments; ++i) {
      result.push_back(end_circle[i]);
    }
  }

  for (auto it = right_points.rbegin(); it != right_points.rend(); ++it) {
    result.push_back(*it);
  }

  if (cap_round && !line.empty()) {
    Point2D first = line.front();
    auto start_circle = generateCircle(first.x, first.y, distance, segments);

    for (int i = 0; i <= segments / 2; ++i) {
      result.push_back(start_circle[i]);
    }
  }

  return result;
}

std::vector<Point2D> bufferPolygon(const std::vector<Point2D>& polygon, double distance) {
  if (polygon.size() < 3)
    return std::vector<Point2D>();

  std::vector<Point2D> result;

  for (size_t i = 0; i < polygon.size(); ++i) {
    size_t prev = (i == 0) ? polygon.size() - 1 : i - 1;
    size_t next = (i + 1) % polygon.size();

    Point2D p = polygon[i];
    Point2D p_prev = polygon[prev];
    Point2D p_next = polygon[next];

    Point2D dir_prev = (p - p_prev).normalized();
    Point2D dir_next = (p_next - p).normalized();
    Point2D normal_prev = dir_prev.perpendicular();
    Point2D normal_next = dir_next.perpendicular();

    Point2D bisector = (normal_prev + normal_next).normalized();
    double scale =
        distance /
        std::max(0.1,
                 std::abs(std::sin(
                     M_PI - std::acos((dir_prev.x * (-dir_next.x) + dir_prev.y * (-dir_next.y)) /
                                      (dir_prev.length() * dir_next.length())))));

    if (std::isfinite(scale) && scale > 0) {
      Point2D new_point = p + bisector * scale * (distance > 0 ? 1 : -1);
      result.push_back(new_point);
    } else {
      result.push_back(p);
    }
  }

  return result;
}

std::vector<Point2D> coordinatesToPoints(const std::vector<double>& coords) {
  std::vector<Point2D> points;
  for (size_t i = 0; i < coords.size(); i += 2) {
    points.push_back(Point2D(coords[i], coords[i + 1]));
  }
  return points;
}

std::vector<double> pointsToCoordinates(const std::vector<Point2D>& points) {
  std::vector<double> coords;
  for (const auto& p : points) {
    coords.push_back(p.x);
    coords.push_back(p.y);
  }
  return coords;
}

Spatial::VectorFeature createBufferFeature(const Spatial::VectorFeature& original, double distance,
                                           int segments, bool cap_round) {
  Spatial::VectorFeature buffer_feature;
  buffer_feature.type = Spatial::VectorFeature::GeometryType::POLYGON;
  buffer_feature.attributes = original.attributes;

  std::vector<Point2D> points = coordinatesToPoints(original.coordinates);
  std::vector<Point2D> buffer_points;

  switch (original.type) {
    case Spatial::VectorFeature::GeometryType::POINT: {
      buffer_points = bufferPoint(points[0], distance, segments);
      break;
    }
    case Spatial::VectorFeature::GeometryType::LINESTRING: {
      buffer_points = bufferLine(points, distance, segments, cap_round);
      break;
    }
    case Spatial::VectorFeature::GeometryType::POLYGON: {
      if (points.size() > 1 && std::abs(points.front().x - points.back().x) < 1e-12 &&
          std::abs(points.front().y - points.back().y) < 1e-12) {
        points.pop_back();
      }
      buffer_points = bufferPolygon(points, distance);
      break;
    }
  }

  buffer_feature.coordinates = pointsToCoordinates(buffer_points);
  return buffer_feature;
}

void dissolveBuffers(Spatial::VectorDataset& dataset) {
  std::cout << "Note: Dissolve is a simplified implementation.\n";
  std::cout << "      For proper dissolve, use external tools like GDAL.\n";

  if (dataset.features.empty())
    return;

  Spatial::VectorFeature dissolved = dataset.features[0];
  dissolved.type = Spatial::VectorFeature::GeometryType::POLYGON;

  dataset.features.clear();
  dataset.features.push_back(dissolved);
}

void printUsage() {
  std::cerr << "spatial_buffer_vector - Create buffers around vector geometries\n\n";
  std::cerr << "Usage: spatial_buffer_vector <input> <output> -distance <value> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -distance <value>   Buffer distance (positive = outward, negative = inward)\n";
  std::cerr << "  -units <units>      Units: units (default) or meters/kilometers\n";
  std::cerr << "  -segments <n>       Number of segments for circles (default: 36)\n";
  std::cerr << "  -cap <style>        Cap style: round (default) or flat\n";
  std::cerr << "  -dissolve           Dissolve overlapping buffers\n";
  std::cerr << "\nExamples:\n";
  std::cerr << "  spatial_buffer_vector cities.csv buffers.csv -distance 10\n";
  std::cerr << "  spatial_buffer_vector roads.csv corridors.csv -distance 100 -units meters\n";
  std::cerr << "  spatial_buffer_vector zones.csv inner.csv -distance -5\n";
  std::cerr << "  spatial_buffer_vector cities.csv dissolved.csv -distance 10 -dissolve\n";
}

int main(int argc, char* argv[]) {
  if (argc < 5) {
    printUsage();
    return 1;
  }

  std::string input_file;
  std::string output_file;
  double distance = 0;
  std::string units = "units";
  int segments = 36;
  bool cap_round = true;
  bool dissolve = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-distance" && i + 1 < argc) {
      distance = std::stod(argv[++i]);
    } else if (arg == "-units" && i + 1 < argc) {
      units = argv[++i];
    } else if (arg == "-segments" && i + 1 < argc) {
      segments = std::stoi(argv[++i]);
    } else if (arg == "-cap" && i + 1 < argc) {
      std::string cap = argv[++i];
      cap_round = (cap == "round");
    } else if (arg == "-dissolve") {
      dissolve = true;
    } else if (input_file.empty()) {
      input_file = arg;
    } else {
      output_file = arg;
    }
  }

  if (input_file.empty() || output_file.empty()) {
    std::cerr << "Error: Input and output files required\n";
    printUsage();
    return 1;
  }

  if (std::abs(distance) < 1e-12) {
    std::cerr << "Error: Distance must be non-zero\n";
    return 1;
  }

  std::cout << "Input: " << input_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Distance: " << distance << " " << units << "\n";
  std::cout << "Segments: " << segments << "\n";
  std::cout << "Cap style: " << (cap_round ? "round" : "flat") << "\n";
  std::cout << "Dissolve: " << (dissolve ? "yes" : "no") << "\n\n";

  Spatial::SpatialCSVReader reader;
  Spatial::VectorDataset dataset;

  if (!reader.read(input_file, dataset)) {
    std::cerr << "Error: Could not read input file\n";
    return 1;
  }

  std::cout << "Input features: " << dataset.features.size() << "\n";

  Spatial::VectorDataset output;
  output.columns = dataset.columns;
  output.geometry_column = dataset.geometry_column;
  output.crs = dataset.crs;

  int total_features = 0;
  int skipped = 0;

  for (const auto& feature : dataset.features) {
    if (feature.coordinates.size() < 2) {
      skipped++;
      continue;
    }

    Spatial::VectorFeature buffer_feature =
        createBufferFeature(feature, distance, segments, cap_round);

    buffer_feature.attributes["buffer_id"] = std::to_string(total_features + 1);
    buffer_feature.attributes["buffer_distance"] = std::to_string(distance);
    buffer_feature.attributes["original_type"] =
        (feature.type == Spatial::VectorFeature::GeometryType::POINT)        ? "POINT"
        : (feature.type == Spatial::VectorFeature::GeometryType::LINESTRING) ? "LINESTRING"
                                                                             : "POLYGON";

    output.features.push_back(buffer_feature);
    total_features++;

    if (total_features % 100 == 0) {
      std::cout << "\r  Processing: " << total_features << " features" << std::flush;
    }
  }

  std::cout << "\r  Processing: " << total_features << " features\n";
  if (skipped > 0) {
    std::cout << "  Skipped: " << skipped << " features (invalid geometry)\n";
  }

  if (dissolve) {
    std::cout << "\nDissolving buffers...\n";
    dissolveBuffers(output);
  }

  output.feature_count = output.features.size();

  std::unordered_map<std::string, size_t> type_counts;
  for (const auto& f : output.features) {
    std::string type_name;
    switch (f.type) {
      case Spatial::VectorFeature::GeometryType::POINT:
        type_name = "POINT";
        break;
      case Spatial::VectorFeature::GeometryType::LINESTRING:
        type_name = "LINESTRING";
        break;
      case Spatial::VectorFeature::GeometryType::POLYGON:
        type_name = "POLYGON";
        break;
    }
    type_counts[type_name]++;
  }
  output.type_counts = type_counts;

  writeVectorCSV(output, output_file, {},
                 {"# Buffered from original dataset",
                  "# Total features: " + std::to_string(output.features.size())},
                 true);

  std::cout << "\n========================================\n";
  std::cout << "  BUFFER COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Input features: " << dataset.features.size() << "\n";
  std::cout << "Buffer features: " << output.features.size() << "\n";
  if (dissolve) {
    std::cout << "Dissolved: yes\n";
  }
  std::cout << "Geometry types:\n";
  for (const auto& [type, count] : output.type_counts) {
    std::cout << "  " << type << ": " << count << "\n";
  }
  std::cout << "\nOutput written to: " << output_file << "\n";

  return 0;
}

