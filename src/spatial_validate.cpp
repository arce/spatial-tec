#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../include/core/spatial_csv.hpp"
#include "../include/core/spatial_geom.hpp"
#include "../include/core/spatial_io.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

void printUsage() {
  std::cerr << "spatial_validate - valida (y opcionalmente repara) la geometria de un conjunto de datos vectoriales\n";
  std::cerr << "\n";
  std::cerr << "Usage: spatial_validate <input> <output> [options]\n";
  std::cerr << "\n";
  std::cerr << "Options:\n";
  std::cerr << "  -fix              Intentar reparar geometrias invalidas\n";
  std::cerr << "  -report <file>    Guardar un reporte de errores encontrados\n";
  std::cerr << "\n";
  std::cerr << "Checks:\n";
  std::cerr << "  empty_geometry     (error)   parte sin puntos -- no reparable\n";
  std::cerr << "  too_few_vertices   (error)   linea con <2 puntos o anillo con <3 -- no reparable\n";
  std::cerr << "  duplicate_vertex   (warning) vertices consecutivos identicos -- reparable con -fix\n";
  std::cerr << "  unclosed_ring      (error)   primer/ultimo vertice de un poligono distintos -- reparable con -fix\n";
  std::cerr << "  self_intersection  (error)   segmentos no adyacentes que se cruzan -- no reparable\n";
  std::cerr << "  cw_orientation     (warning) anillo en sentido horario (se espera CCW) -- reparable con -fix\n";
  std::cerr << "\n";
  std::cerr << "El archivo de salida siempre se escribe (identico al de entrada si no se\n";
  std::cerr << "encontraron problemas reparables, o con las reparaciones aplicadas si se\n";
  std::cerr << "paso -fix). Sin -fix, los problemas se reportan pero la geometria no se\n";
  std::cerr << "modifica.\n";
  std::cerr << "\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_validate parcels.csv parcels_valid.csv -fix -report errors.csv\n";
}

struct Issue {
  std::string feature_id;
  std::string geometry_type;
  int part_index;
  std::string issue;
  std::string severity;
  bool fixed;
  std::string message;
};

std::string geometryTypeName(Spatial::VectorFeature::GeometryType t) {
  switch (t) {
    case Spatial::VectorFeature::GeometryType::POINT: return "POINT";
    case Spatial::VectorFeature::GeometryType::LINESTRING: return "LINESTRING";
    case Spatial::VectorFeature::GeometryType::POLYGON: return "POLYGON";
    case Spatial::VectorFeature::GeometryType::MULTIPOINT: return "MULTIPOINT";
    case Spatial::VectorFeature::GeometryType::MULTILINESTRING: return "MULTILINESTRING";
    case Spatial::VectorFeature::GeometryType::MULTIPOLYGON: return "MULTIPOLYGON";
  }
  return "UNKNOWN";
}

enum class GeomCategory { POINT_CAT, LINE_CAT, RING_CAT };

GeomCategory categoryOf(Spatial::VectorFeature::GeometryType t) {
  switch (t) {
    case Spatial::VectorFeature::GeometryType::POINT:
    case Spatial::VectorFeature::GeometryType::MULTIPOINT:
      return GeomCategory::POINT_CAT;
    case Spatial::VectorFeature::GeometryType::POLYGON:
    case Spatial::VectorFeature::GeometryType::MULTIPOLYGON:
      return GeomCategory::RING_CAT;
    default:
      return GeomCategory::LINE_CAT;
  }
}

std::vector<std::vector<double>> extractParts(const Spatial::VectorFeature& f) {
  std::vector<std::vector<double>> parts;
  for (const auto& range : featurePartRanges(f)) {
    std::vector<double> part;
    for (size_t p = range.first; p < range.second; ++p) {
      part.push_back(f.coordinates[p * 2]);
      part.push_back(f.coordinates[p * 2 + 1]);
    }
    parts.push_back(std::move(part));
  }
  return parts;
}

void reassembleFeature(Spatial::VectorFeature& f, const std::vector<std::vector<double>>& parts) {
  f.coordinates.clear();
  f.part_starts.clear();
  if (parts.size() > 1) {
    size_t running = 0;
    for (const auto& part : parts) {
      f.part_starts.push_back(running);
      running += part.size() / 2;
    }
  }
  for (const auto& part : parts) {
    f.coordinates.insert(f.coordinates.end(), part.begin(), part.end());
  }
}

bool dedupeConsecutive(std::vector<double>& pts, bool fix, int& removed_count) {
  size_t n = pts.size() / 2;
  if (n < 2) return false;

  std::vector<double> result;
  result.push_back(pts[0]);
  result.push_back(pts[1]);
  bool any_dup = false;

  for (size_t i = 1; i < n; ++i) {
    double x = pts[i * 2], y = pts[i * 2 + 1];
    double last_x = result[result.size() - 2];
    double last_y = result[result.size() - 1];
    if (std::fabs(x - last_x) < 1e-9 && std::fabs(y - last_y) < 1e-9) {
      any_dup = true;
      ++removed_count;
      continue;
    }
    result.push_back(x);
    result.push_back(y);
  }

  if (any_dup && fix) pts = result;
  return any_dup;
}

bool isRingClosed(const std::vector<double>& pts) {
  size_t n = pts.size() / 2;
  if (n < 2) return false;
  return std::fabs(pts[0] - pts[(n - 1) * 2]) < 1e-9 && std::fabs(pts[1] - pts[(n - 1) * 2 + 1]) < 1e-9;
}

void closeRing(std::vector<double>& pts) {
  if (pts.size() < 2) return;
  pts.push_back(pts[0]);
  pts.push_back(pts[1]);
}

double signedArea(const std::vector<double>& pts) {
  size_t n = pts.size() / 2;
  double area2 = 0.0;
  for (size_t i = 0; i < n; ++i) {
    size_t j = (i + 1) % n;
    area2 += pts[i * 2] * pts[j * 2 + 1] - pts[j * 2] * pts[i * 2 + 1];
  }
  return area2 / 2.0;
}

void reversePoints(std::vector<double>& pts) {
  size_t n = pts.size() / 2;
  for (size_t i = 0; i < n / 2; ++i) {
    std::swap(pts[i * 2], pts[(n - 1 - i) * 2]);
    std::swap(pts[i * 2 + 1], pts[(n - 1 - i) * 2 + 1]);
  }
}

int orientation(double px, double py, double qx, double qy, double rx, double ry) {
  double val = (qx - px) * (ry - py) - (qy - py) * (rx - px);
  if (std::fabs(val) < 1e-9) return 0;
  return (val > 0) ? 1 : 2;
}

bool segmentsProperlyIntersect(double x1, double y1, double x2, double y2, double x3, double y3,
                                double x4, double y4) {
  int o1 = orientation(x1, y1, x2, y2, x3, y3);
  int o2 = orientation(x1, y1, x2, y2, x4, y4);
  int o3 = orientation(x3, y3, x4, y4, x1, y1);
  int o4 = orientation(x3, y3, x4, y4, x2, y2);
  return o1 != o2 && o3 != o4 && o1 != 0 && o2 != 0 && o3 != 0 && o4 != 0;
}

bool hasSelfIntersection(const std::vector<double>& pts, bool is_ring) {
  size_t n = pts.size() / 2;
  if (n < 4) return false;
  size_t num_segments = n - 1;
  if (num_segments < 3) return false;

  for (size_t i = 0; i < num_segments; ++i) {
    for (size_t j = i + 2; j < num_segments; ++j) {
      if (is_ring && i == 0 && j == num_segments - 1) continue;
      double x1 = pts[i * 2], y1 = pts[i * 2 + 1];
      double x2 = pts[(i + 1) * 2], y2 = pts[(i + 1) * 2 + 1];
      double x3 = pts[j * 2], y3 = pts[j * 2 + 1];
      double x4 = pts[(j + 1) * 2], y4 = pts[(j + 1) * 2 + 1];
      if (segmentsProperlyIntersect(x1, y1, x2, y2, x3, y3, x4, y4)) return true;
    }
  }
  return false;
}

void validatePart(std::vector<double>& part, GeomCategory cat, int part_index,
                   const std::string& feature_id, const std::string& type_name, bool fix,
                   std::vector<Issue>& issues) {
  if (cat == GeomCategory::POINT_CAT) {
    if (part.size() < 2) {
      issues.push_back({feature_id, type_name, part_index, "empty_geometry", "error", false,
                         "Part has no coordinates"});
    }
    return;
  }

  int removed = 0;
  bool had_dups = dedupeConsecutive(part, fix, removed);
  if (had_dups) {
    issues.push_back({feature_id, type_name, part_index, "duplicate_vertex", "warning", fix,
                       std::to_string(removed) + " consecutive duplicate vertex(es) found"});
  }

  bool is_ring = (cat == GeomCategory::RING_CAT);
  if (is_ring) {
    bool closed = isRingClosed(part);
    if (!closed) {
      if (fix) closeRing(part);
      issues.push_back({feature_id, type_name, part_index, "unclosed_ring", "error", fix,
                         "First and last vertex of the ring do not match"});
    }
  }

  size_t n = part.size() / 2;
  size_t distinct = is_ring && isRingClosed(part) && n > 0 ? n - 1 : n;
  size_t min_required = is_ring ? 3 : 2;
  if (distinct < min_required) {
    issues.push_back({feature_id, type_name, part_index, "too_few_vertices", "error", false,
                       "Only " + std::to_string(distinct) + " distinct vertex(es), needs at least " +
                           std::to_string(min_required)});
    return;
  }

  if (hasSelfIntersection(part, is_ring)) {
    issues.push_back({feature_id, type_name, part_index, "self_intersection", "error", false,
                       "Two non-adjacent segments cross"});
  }

  if (is_ring) {
    double area = signedArea(part);
    if (area < 0) {
      if (fix) reversePoints(part);
      issues.push_back({feature_id, type_name, part_index, "cw_orientation", "warning", fix,
                         "Ring is clockwise, expected counter-clockwise (CCW)"});
    }
  }
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printUsage();
    return 1;
  }

  std::string input_file = argv[1];
  std::string output_file = argv[2];
  bool fix = false;
  std::string report_file = "";
  for (int i = 3; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-fix") {
      fix = true;
    } else if (arg == "-report" && i + 1 < argc) {
      report_file = argv[++i];
    } else if (arg == "-help" || arg == "-h") {
      printUsage();
      return 0;
    } else {
      std::cerr << "Error: Unknown option: " << arg << "\n";
      printUsage();
      return 1;
    }
  }

  std::cout << "========================================\n";
  std::cout << "  GEOMETRY VALIDATION (VALIDACION DE GEOMETRIA)\n";
  std::cout << "========================================\n\n";
  std::cout << "input: " << input_file << "\n";
  std::cout << "output: " << output_file << "\n";
  std::cout << "-fix: " << (fix ? "yes" : "no") << "\n";
  std::cout << "-report: " << (report_file.empty() ? "(not requested)" : report_file) << "\n\n";

  Spatial::VectorDataset dataset;
  Spatial::SpatialCSVReader reader;
  if (!reader.read(input_file, dataset)) {
    std::cerr << "Error: Could not read input file: " << input_file << "\n";
    return 1;
  }
  std::cout << "Loaded " << dataset.features.size() << " features\n\n";

  std::string id_column;
  for (const auto& c : dataset.columns) {
    if (toLower(c) == "id") {
      id_column = c;
      break;
    }
  }

  std::vector<Issue> all_issues;
  size_t features_with_issues = 0;
  size_t features_fixed = 0;

  for (size_t fi = 0; fi < dataset.features.size(); ++fi) {
    auto& feature = dataset.features[fi];
    std::string feature_id;
    if (!id_column.empty()) {
      auto it = feature.attributes.find(id_column);
      feature_id = (it != feature.attributes.end()) ? it->second : ("row_" + std::to_string(fi + 1));
    } else {
      feature_id = "row_" + std::to_string(fi + 1);
    }
    std::string type_name = geometryTypeName(feature.type);
    GeomCategory cat = categoryOf(feature.type);

    auto parts = extractParts(feature);
    std::vector<Issue> feature_issues;
    for (size_t pi = 0; pi < parts.size(); ++pi) {
      validatePart(parts[pi], cat, static_cast<int>(pi), feature_id, type_name, fix, feature_issues);
    }

    if (!feature_issues.empty()) {
      ++features_with_issues;
      bool any_fixed = false;
      for (const auto& issue : feature_issues) {
        if (issue.fixed) any_fixed = true;
      }
      if (any_fixed) ++features_fixed;
      all_issues.insert(all_issues.end(), feature_issues.begin(), feature_issues.end());
    }

    reassembleFeature(feature, parts);
  }

  if (all_issues.empty()) {
    std::cout << "No se encontraron problemas de geometria.\n";
  } else {
    std::cout << "Problemas encontrados:\n";
    for (const auto& issue : all_issues) {
      std::cout << "  [" << issue.severity << "] " << issue.feature_id << " (" << issue.geometry_type
                << ", parte " << issue.part_index << "): " << issue.issue << " -- " << issue.message;
      if (fix) std::cout << (issue.fixed ? " [reparado]" : " [no reparable]");
      std::cout << "\n";
    }
  }

  size_t error_count = 0, warning_count = 0, fixed_count = 0;
  for (const auto& issue : all_issues) {
    if (issue.severity == "error") ++error_count;
    else ++warning_count;
    if (issue.fixed) ++fixed_count;
  }

  std::cout << "\nResumen:\n";
  std::cout << "  Features analizadas: " << dataset.features.size() << "\n";
  std::cout << "  Features con problemas: " << features_with_issues << "\n";
  std::cout << "  Problemas totales: " << all_issues.size() << " (" << error_count << " error(es), "
            << warning_count << " advertencia(s))\n";
  if (fix) {
    std::cout << "  Problemas reparados: " << fixed_count << "\n";
    std::cout << "  Features con al menos una reparacion: " << features_fixed << "\n";
  }

  if (!report_file.empty()) {
    std::ofstream report(report_file);
    if (!report.is_open()) {
      std::cerr << "Error: Could not create report file: " << report_file << "\n";
      return 1;
    }
    report << "feature_id,geometry_type,part,issue,severity,fixed,message\n";
    for (const auto& issue : all_issues) {
      report << issue.feature_id << "," << issue.geometry_type << "," << issue.part_index << ","
             << issue.issue << "," << issue.severity << "," << (issue.fixed ? "yes" : "no") << ","
             << "\"" << issue.message << "\"\n";
    }
    std::cout << "\nReporte escrito en: " << report_file << "\n";
  }

  writeVectorCSV(dataset, output_file);
  std::cout << "\nOutput written to: " << output_file << "\n";
  std::cout << "Validation complete.\n";

  return 0;
}

