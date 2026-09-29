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

void printUsage() {
  std::cerr << "spatial_address_validate - Valida rangos de direcciones de calles (solapes, huecos, rangos invertidos)\n\n";
  std::cerr << "Usage: spatial_address_validate <streets.csv> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -check_overlaps   Detectar rangos de numeracion solapados\n";
  std::cerr << "  -check_gaps       Detectar huecos en la numeracion\n";
  std::cerr << "  -report <file>    Guardar un reporte de problemas encontrados en formato CSV\n";
  std::cerr << "  -h, -help         Mostrar esta ayuda\n\n";
  std::cerr << "Example:\n";
  std::cerr << "  spatial_address_validate streets.csv -check_overlaps -check_gaps -report issues.csv\n";
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    printUsage();
    return 1;
  }

  std::string streets_file = argv[1];
  bool check_overlaps = false;
  bool check_gaps = false;
  std::string report_file = "";

  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-check_overlaps") {
      check_overlaps = true;
    } else if (arg == "-check_gaps") {
      check_gaps = true;
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
  std::cout << "  ADDRESS RANGE VALIDATION\n";
  std::cout << "========================================\n\n";
  std::cout << "Streets file: " << streets_file << "\n";
  std::cout << "Check overlaps: " << (check_overlaps ? "yes" : "no") << "\n";
  std::cout << "Check gaps: " << (check_gaps ? "yes" : "no") << "\n";
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

  std::cout << "\n========================================\n";
  std::cout << "  VALIDATION FINISHED\n";
  std::cout << "========================================\n";

  return issues.empty() ? 0 : 2;}