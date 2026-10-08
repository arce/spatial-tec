#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <vector>

#include "../include/core/spatial_address_utils.hpp"
#include "../include/core/spatial_string.hpp"

struct StreetSegment {
  std::string id;
  std::string street_name;
  std::vector<double> coordinates;
  int left_from;
  int left_to;
  int right_from;
  int right_to;
  std::string city;
  std::string postal_code;

  bool hasLeftRange() const {
    return left_from != 0 || left_to != 0;
  }

  bool hasRightRange() const {
    return right_from != 0 || right_to != 0;
  }
};

struct ValidationIssue {
  std::string street_name;
  std::string issue_type;
  std::string side;
  std::string description;
};

std::vector<double> parseWKTGeometry(const std::string& wkt) {
  std::vector<double> coords;
  std::string str = trim(wkt);
  std::string geom_type;
  size_t type_end = str.find('(');
  if (type_end != std::string::npos) {
    geom_type = trim(str.substr(0, type_end));
  }

  size_t start = str.find('(');
  size_t end = str.rfind(')');
  if (start == std::string::npos || end == std::string::npos || end <= start) {
    return coords;
  }

  std::string inner = str.substr(start + 1, end - start - 1);
  if (geom_type == "POLYGON") {
    inner.erase(std::remove(inner.begin(), inner.end(), '('), inner.end());
    inner.erase(std::remove(inner.begin(), inner.end(), ')'), inner.end());
  }

  auto points = split(inner, ',');
  for (const auto& point : points) {
    std::string p = trim(point);
    if (p.empty()) continue;
    auto parts = split(p, ' ');
    if (parts.size() >= 2) {
      try {
        coords.push_back(std::stod(parts[0]));
        coords.push_back(std::stod(parts[1]));
      } catch (...) {}
    }
  }
  return coords;
}

std::vector<StreetSegment> readStreetSegmentsDirect(const std::string& filename) {
  std::vector<StreetSegment> segments;
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file: " << filename << "\n";
    return segments;
  }

  std::string line;
  std::vector<std::string> headers;
  bool is_header = true;
  int line_num = 0;

  int col_id = -1;
  int col_street_name = -1;
  int col_left_from = -1;
  int col_left_to = -1;
  int col_right_from = -1;
  int col_right_to = -1;
  int col_city = -1;
  int col_postal_code = -1;
  int col_geometry = -1;

  while (std::getline(file, line)) {
    line_num++;
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;

    auto values = splitCSV(line);

    if (is_header) {
      headers = values;
      for (size_t i = 0; i < headers.size(); ++i) {
        std::string lower = toLower(headers[i]);
        if (lower == "id") col_id = i;
        else if (lower == "street_name" || lower == "street" || lower == "name") col_street_name = i;
        else if (lower == "left_from" || lower == "l_from") col_left_from = i;
        else if (lower == "left_to" || lower == "l_to") col_left_to = i;
        else if (lower == "right_from" || lower == "r_from") col_right_from = i;
        else if (lower == "right_to" || lower == "r_to") col_right_to = i;
        else if (lower == "city") col_city = i;
        else if (lower == "postal_code" || lower == "zip") col_postal_code = i;
        else if (lower == "geometry" || lower == "geom" || lower == "wkt") col_geometry = i;
      }
      is_header = false;
      continue;
    }

    if (col_street_name == -1) continue;

    StreetSegment seg;
    auto get_value = [&](int col) -> std::string {
      return (col >= 0 && col < (int)values.size()) ? values[col] : "";
    };
    auto to_int = [](const std::string& s) -> int {
      try { return std::stoi(s); } catch (...) { return 0; }
    };

    seg.id = get_value(col_id);
    seg.street_name = get_value(col_street_name);
    seg.city = get_value(col_city);
    seg.postal_code = get_value(col_postal_code);
    seg.left_from = to_int(get_value(col_left_from));
    seg.left_to = to_int(get_value(col_left_to));
    seg.right_from = to_int(get_value(col_right_from));
    seg.right_to = to_int(get_value(col_right_to));

    if (col_geometry >= 0) {
      seg.coordinates = parseWKTGeometry(get_value(col_geometry));
    }

    segments.push_back(seg);
  }
  return segments;
}

// ---------------------------------------------------------------------------
// -fix: reescribe el archivo de calles corrigiendo rangos invertidos
// ---------------------------------------------------------------------------
static int fixInvertedRanges(const std::string& in_file, const std::string& out_file) {
  std::ifstream in(in_file);
  if (!in.is_open()) {
    std::cerr << "Error: Could not open file: " << in_file << "\n";
    return -1;
  }
  std::ofstream out(out_file);
  if (!out.is_open()) {
    std::cerr << "Error: Could not create file: " << out_file << "\n";
    return -1;
  }

  std::string line;
  std::vector<std::string> headers;
  bool is_header = true;
  int col_lf = -1, col_lt = -1, col_rf = -1, col_rt = -1;
  int fixed = 0;

  while (std::getline(in, line)) {
    std::string t = trim(line);
    if (t.empty() || t[0] == '#') {
      out << line << "\n";
      continue;
    }
    auto values = splitCSV(t);
    if (is_header) {
      headers = values;
      for (size_t i = 0; i < headers.size(); ++i) {
        std::string l = toLower(headers[i]);
        if (l == "left_from" || l == "l_from") col_lf = (int)i;
        else if (l == "left_to" || l == "l_to") col_lt = (int)i;
        else if (l == "right_from" || l == "r_from") col_rf = (int)i;
        else if (l == "right_to" || l == "r_to") col_rt = (int)i;
      }
      is_header = false;
    } else {
      auto swapIfInverted = [&](int cf, int ct) {
        if (cf < 0 || ct < 0 || cf >= (int)values.size() || ct >= (int)values.size()) return;
        try {
          int a = std::stoi(values[cf]), b = std::stoi(values[ct]);
          if ((a != 0 || b != 0) && a > b) {
            std::swap(values[cf], values[ct]);
            ++fixed;
          }
        } catch (...) {
        }
      };
      swapIfInverted(col_lf, col_lt);
      swapIfInverted(col_rf, col_rt);
    }
    for (size_t i = 0; i < values.size(); ++i) {
      if (i) out << ",";
      out << addr::csvEscape(values[i]);
    }
    out << "\n";
  }
  return fixed;
}

// ---------------------------------------------------------------------------
// -check_accuracy: compara un resultado geocodificado con una referencia
// ---------------------------------------------------------------------------
static bool readPoints(const std::string& filename, std::map<std::string, std::pair<double, double>>& pts) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file: " << filename << "\n";
    return false;
  }
  std::string line;
  std::vector<std::string> headers;
  bool is_header = true;
  int col_id = -1, col_x = -1, col_y = -1, col_geom = -1, col_status = -1;
  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    auto v = splitCSV(line);
    if (is_header) {
      for (size_t i = 0; i < v.size(); ++i) {
        std::string l = toLower(v[i]);
        if (l == "id") col_id = (int)i;
        else if (l == "longitude" || l == "lon" || l == "x") col_x = (int)i;
        else if (l == "latitude" || l == "lat" || l == "y") col_y = (int)i;
        else if (l == "geometry" || l == "geom" || l == "wkt") col_geom = (int)i;
        else if (l == "status") col_status = (int)i;
      }
      is_header = false;
      if (col_id < 0 || ((col_x < 0 || col_y < 0) && col_geom < 0)) {
        std::cerr << "Error: " << filename
                  << " needs columns id and (longitude,latitude) or geometry\n";
        return false;
      }
      continue;
    }
    if (col_id >= (int)v.size()) continue;
    if (col_status >= 0 && col_status < (int)v.size() &&
        (v[col_status] == "not_found" || v[col_status] == "partial"))
      continue;
    double x = 0, y = 0;
    try {
      if (col_x >= 0 && col_y >= 0 && col_x < (int)v.size() && col_y < (int)v.size()) {
        x = std::stod(v[col_x]);
        y = std::stod(v[col_y]);
      } else if (col_geom >= 0 && col_geom < (int)v.size()) {
        auto c = parseWKTGeometry(v[col_geom]);
        if (c.size() < 2) continue;
        x = c[0];
        y = c[1];
      } else {
        continue;
      }
    } catch (...) {
      continue;
    }
    pts[v[col_id]] = {x, y};
  }
  return true;
}

static int checkAccuracy(const std::string& result_file, const std::string& ref_file,
                         const std::string& report_file) {
  std::map<std::string, std::pair<double, double>> res, ref;
  if (!readPoints(result_file, res) || !readPoints(ref_file, ref)) return 1;

  std::vector<double> d;
  int missing = 0;
  for (const auto& kv : ref) {
    auto it = res.find(kv.first);
    if (it == res.end()) {
      ++missing;
      continue;
    }
    double dx = it->second.first - kv.second.first;
    double dy = it->second.second - kv.second.second;
    d.push_back(std::sqrt(dx * dx + dy * dy));
  }
  if (d.empty()) {
    std::cerr << "Error: no ids in common between result and reference (or none geocoded)\n";
    return 1;
  }
  std::sort(d.begin(), d.end());
  double sum = 0, sq = 0;
  for (double v : d) {
    sum += v;
    sq += v * v;
  }
  double mean = sum / d.size();
  double rmse = std::sqrt(sq / d.size());
  double median = (d.size() % 2) ? d[d.size() / 2] : 0.5 * (d[d.size() / 2 - 1] + d[d.size() / 2]);

  std::ostringstream o;
  o << std::fixed << std::setprecision(3);
  o << "ACCURACY\n";
  o << "  Compared points:     " << d.size() << "\n";
  o << "  Reference not found: " << missing << "\n";
  o << "  Mean error:          " << mean << "\n";
  o << "  Median error:        " << median << "\n";
  o << "  RMSE:                " << rmse << "\n";
  o << "  Max error:           " << d.back() << "\n";
  std::cout << o.str();
  if (!report_file.empty()) {
    std::ofstream rpt(report_file);
    if (rpt.is_open()) {
      rpt << o.str();
      std::cout << "\nReport successfully written to: " << report_file << "\n";
    }
  }
  return 0;
}

void printUsage() {
  std::cerr << "spatial_address_validate - Valida rangos de direcciones de calles (solapes, huecos, rangos invertidos)\n\n";
  std::cerr << "Usage: spatial_address_validate <streets.csv> [options]\n";
  std::cerr << "       spatial_address_validate <result.csv> -check_accuracy -reference <truth.csv>\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -check_overlaps   Detectar rangos de numeracion solapados\n";
  std::cerr << "  -check_gaps       Detectar huecos en la numeracion\n";
  std::cerr << "  -check_ranges     Detectar rangos inconsistentes (paridad mezclada, valores\n";
  std::cerr << "                    negativos, segmentos sin rango ni geometria valida)\n";
  std::cerr << "  -fix <out.csv>    Escribir una copia del archivo con los rangos invertidos corregidos\n";
  std::cerr << "  -report <file>    Guardar un reporte de problemas encontrados en formato CSV\n";
  std::cerr << "  -check_accuracy   Comparar un resultado de spatial_address con una referencia\n";
  std::cerr << "  -reference <file> CSV con id y longitude,latitude (o geometry POINT) de referencia\n";
  std::cerr << "  -h, -help         Mostrar esta ayuda\n\n";
  std::cerr << "Exit code: 0 sin problemas, 2 si se encontraron problemas, 1 en caso de error.\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_address_validate streets.csv -check_overlaps -check_gaps -report issues.csv\n";
  std::cerr << "  spatial_address_validate streets.csv -check_ranges -fix streets_fixed.csv\n";
  std::cerr << "  spatial_address_validate result.csv -check_accuracy -reference ground_truth.csv\n";
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    printUsage();
    return 1;
  }

  std::string streets_file = argv[1];
  bool check_overlaps = false;
  bool check_gaps = false;
  bool check_ranges = false;
  bool check_accuracy = false;
  std::string fix_file = "";
  std::string reference_file = "";
  std::string report_file = "";

  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-check_overlaps") {
      check_overlaps = true;
    } else if (arg == "-check_gaps") {
      check_gaps = true;
    } else if (arg == "-check_ranges") {
      check_ranges = true;
    } else if (arg == "-check_accuracy") {
      check_accuracy = true;
    } else if (arg == "-reference" && i + 1 < argc) {
      reference_file = argv[++i];
    } else if (arg == "-fix" && i + 1 < argc) {
      fix_file = argv[++i];
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

  if (check_accuracy) {
    if (reference_file.empty()) {
      std::cerr << "Error: -check_accuracy requires -reference <file>\n";
      return 1;
    }
    return checkAccuracy(streets_file, reference_file, report_file);
  }

  std::cout << "========================================\n";
  std::cout << "  ADDRESS RANGE VALIDATION\n";
  std::cout << "========================================\n\n";
  std::cout << "Streets file: " << streets_file << "\n";
  std::cout << "Check overlaps: " << (check_overlaps ? "yes" : "no") << "\n";
  std::cout << "Check gaps: " << (check_gaps ? "yes" : "no") << "\n";
  std::cout << "Check ranges: " << (check_ranges ? "yes" : "no") << "\n";
  std::cout << "Report output: " << (report_file.empty() ? "none (console only)" : report_file) << "\n\n";

  auto segments = readStreetSegmentsDirect(streets_file);
  std::cout << "Total street segments loaded: " << segments.size() << "\n\n";

  if (segments.empty()) {
    std::cerr << "Error: No valid street segments found or file could not be read.\n";
    return 1;
  }

  std::vector<ValidationIssue> issues;

  for (const auto& seg : segments) {
    if (seg.hasLeftRange() && seg.left_from > seg.left_to) {
      issues.push_back({seg.street_name, "INVERTED_RANGE", "left", 
        "Segment ID " + seg.id + " has inverted left range: " + std::to_string(seg.left_from) + " to " + std::to_string(seg.left_to)});
    }
    if (seg.hasRightRange() && seg.right_from > seg.right_to) {
      issues.push_back({seg.street_name, "INVERTED_RANGE", "right", 
        "Segment ID " + seg.id + " has inverted right range: " + std::to_string(seg.right_from) + " to " + std::to_string(seg.right_to)});
    }
  }

  if (check_ranges) {
    for (const auto& seg : segments) {
      std::string who = "Segment ID " + seg.id;
      if (!seg.hasLeftRange() && !seg.hasRightRange()) {
        issues.push_back({seg.street_name, "NO_RANGE", "both", who + " has no numbering range"});
      }
      if (seg.coordinates.size() < 4) {
        issues.push_back({seg.street_name, "BAD_GEOMETRY", "both",
                          who + " has fewer than 2 valid vertices"});
      }
      struct Side { const char* name; int from; int to; };
      Side sides[2] = {{"left", seg.left_from, seg.left_to}, {"right", seg.right_from, seg.right_to}};
      for (const auto& sd : sides) {
        if (sd.from < 0 || sd.to < 0) {
          issues.push_back({seg.street_name, "NEGATIVE_VALUE", sd.name,
                            who + " has a negative " + sd.name + " range"});
        }
        if ((sd.from != 0 || sd.to != 0) && ((sd.from % 2) != (sd.to % 2))) {
          issues.push_back({seg.street_name, "PARITY_MISMATCH", sd.name,
                            who + " " + sd.name + " range " + std::to_string(sd.from) + "-" +
                                std::to_string(sd.to) + " mixes odd and even ends"});
        }
      }
      if (seg.hasLeftRange() && seg.hasRightRange() && (seg.left_from % 2) == (seg.right_from % 2)) {
        issues.push_back({seg.street_name, "SAME_PARITY_SIDES", "both",
                          who + " has the same parity on both sides (expected odd on one side and even on the other)"});
      }
    }
  }

  std::map<std::string, std::vector<StreetSegment>> street_groups;
  for (const auto& seg : segments) {
    street_groups[toLower(seg.street_name)].push_back(seg);
  }

  for (auto& [name, group] : street_groups) {
    if (check_overlaps || check_gaps) {
      struct RangeItem { int min_val; int max_val; std::string id; };
      std::vector<RangeItem> left_ranges, right_ranges;

      for (const auto& seg : group) {
        if (seg.hasLeftRange() && seg.left_from <= seg.left_to) {
          left_ranges.push_back({seg.left_from, seg.left_to, seg.id});
        }
        if (seg.hasRightRange() && seg.right_from <= seg.right_to) {
          right_ranges.push_back({seg.right_from, seg.right_to, seg.id});
        }
      }

      auto checkSideOverlapsAndGaps = [&](std::vector<RangeItem>& ranges, const std::string& side) {
        if (ranges.empty()) return;
        std::sort(ranges.begin(), ranges.end(), [](const RangeItem& a, const RangeItem& b) {
          return a.min_val < b.min_val;
        });

        for (size_t i = 0; i < ranges.size(); ++i) {
          if (check_overlaps && i + 1 < ranges.size()) {
            if (ranges[i].max_val > ranges[i + 1].min_val) {
              issues.push_back({group[0].street_name, "OVERLAP", side,
                "Overlap on " + side + " side between segments " + ranges[i].id + " (" + 
                std::to_string(ranges[i].min_val) + "-" + std::to_string(ranges[i].max_val) + ") and " +
                ranges[i + 1].id + " (" + std::to_string(ranges[i + 1].min_val) + "-" + 
                std::to_string(ranges[i + 1].max_val) + ")"});
            }
          }
          if (check_gaps && i + 1 < ranges.size()) {
            if (ranges[i].max_val + 2 < ranges[i + 1].min_val) {
              issues.push_back({group[0].street_name, "GAP", side,
                "Gap on " + side + " side between " + std::to_string(ranges[i].max_val) + " (seg " + ranges[i].id +
                ") and " + std::to_string(ranges[i + 1].min_val) + " (seg " + ranges[i + 1].id + ")"});
            }
          }
        }
      };

      if (check_overlaps || check_gaps) {
        checkSideOverlapsAndGaps(left_ranges, "left");
        checkSideOverlapsAndGaps(right_ranges, "right");
      }
    }
  }

  std::cout << "Validation complete. Total issues found: " << issues.size() << "\n\n";
  for (const auto& issue : issues) {
    std::cout << "[" << issue.issue_type << "] " << issue.street_name << " (" << issue.side << "): " << issue.description << "\n";
  }

  if (!report_file.empty()) {
    std::ofstream rpt(report_file);
    if (rpt.is_open()) {
      rpt << "street_name,issue_type,side,description\n";
      for (const auto& issue : issues) {
        rpt << "\"" << issue.street_name << "\","
            << issue.issue_type << ","
            << issue.side << ","
            << "\"" << issue.description << "\"\n";
      }
      std::cout << "\nReport successfully written to: " << report_file << "\n";
    } else {
      std::cerr << "\nError: Could not create report file: " << report_file << "\n";
    }
  }

  if (!fix_file.empty()) {
    int n = fixInvertedRanges(streets_file, fix_file);
    if (n < 0) return 1;
    std::cout << "\nFixed " << n << " inverted range(s). Corrected file written to: " << fix_file << "\n";
  }

  std::cout << "\n========================================\n";
  std::cout << "  VALIDATION FINISHED\n";
  std::cout << "========================================\n";

  return issues.empty() ? 0 : 2;
}
