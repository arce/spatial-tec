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

struct AddressInput {
  std::string id;
  int house_number;
  std::string street_name;
  std::string city;
  std::string postal_code;
  std::map<std::string, std::string> extra_attrs;
};

struct StreetSegment {
  std::string street_name;
  std::vector<double> coordinates;
  int left_from;
  int left_to;
  int right_from;
  int right_to;
  std::string city;
  std::string postal_code;
  std::map<std::string, std::string> extra_attrs;

  bool hasLeftRange() const {
    return left_from != 0 || left_to != 0;
  }

  bool hasRightRange() const {
    return right_from != 0 || right_to != 0;
  }

  bool getRangeForNumber(int number, int& from, int& to, bool& is_left) const {
    if (hasLeftRange() && number >= left_from && number <= left_to) {
      from = left_from;
      to = left_to;
      is_left = true;
      return true;
    }
    if (hasRightRange() && number >= right_from && number <= right_to) {
      from = right_from;
      to = right_to;
      is_left = false;
      return true;
    }
    return false;
  }

  double getTotalLength() const {
    double total = 0;
    for (size_t i = 0; i < coordinates.size() - 2; i += 2) {
      double dx = coordinates[i + 2] - coordinates[i];
      double dy = coordinates[i + 3] - coordinates[i + 1];
      total += std::sqrt(dx * dx + dy * dy);
    }
    return total;
  }

  std::pair<double, double> interpolatePosition(double t) const {
    if (coordinates.size() < 4 || t < 0 || t > 1) {
      return {0, 0};
    }

    double total_len = getTotalLength();
    if (total_len < 1e-12) {
      return {coordinates[0], coordinates[1]};
    }

    double target_dist = t * total_len;
    double accumulated = 0;

    for (size_t i = 0; i < coordinates.size() - 2; i += 2) {
      double dx = coordinates[i + 2] - coordinates[i];
      double dy = coordinates[i + 3] - coordinates[i + 1];
      double seg_len = std::sqrt(dx * dx + dy * dy);

      if (accumulated + seg_len >= target_dist || i == coordinates.size() - 3) {
        double local_t = (target_dist - accumulated) / seg_len;
        local_t = std::max(0.0, std::min(1.0, local_t));

        double px = coordinates[i] + local_t * dx;
        double py = coordinates[i + 1] + local_t * dy;
        return {px, py};
      }

      accumulated += seg_len;
    }

    return {coordinates[coordinates.size() - 2], coordinates[coordinates.size() - 1]};
  }
};

std::vector<double> parseWKTGeometry(const std::string& wkt) {
  std::vector<double> coords;
  std::string str = trim(wkt);

  std::cout << "Parsing WKT: " << str.substr(0, std::min(str.size(), size_t(100))) << "...\n";

  std::string geom_type;
  size_t type_end = str.find('(');
  if (type_end != std::string::npos) {
    geom_type = trim(str.substr(0, type_end));
  }

  size_t start = str.find('(');
  if (start == std::string::npos) {
    std::cout << "  No opening parenthesis found\n";
    return coords;
  }

  size_t end = str.rfind(')');
  if (end == std::string::npos || end <= start) {
    std::cout << "  No closing parenthesis found\n";
    return coords;
  }

  std::string inner = str.substr(start + 1, end - start - 1);

  if (geom_type == "LINESTRING" || geom_type == "LINESTRING") {
    auto points = split(inner, ',');
    for (const auto& point : points) {
      std::string p = trim(point);
      if (p.empty())
        continue;

      auto parts = split(p, ' ');
      if (parts.size() >= 2) {
        try {
          double x = std::stod(parts[0]);
          double y = std::stod(parts[1]);
          coords.push_back(x);
          coords.push_back(y);
        } catch (const std::exception& e) {
        }
      }
    }
  } else if (geom_type == "POLYGON") {
    inner.erase(std::remove(inner.begin(), inner.end(), '('), inner.end());
    inner.erase(std::remove(inner.begin(), inner.end(), ')'), inner.end());

    auto points = split(inner, ',');
    for (const auto& point : points) {
      std::string p = trim(point);
      if (p.empty())
        continue;

      auto parts = split(p, ' ');
      if (parts.size() >= 2) {
        try {
          double x = std::stod(parts[0]);
          double y = std::stod(parts[1]);
          coords.push_back(x);
          coords.push_back(y);
        } catch (const std::exception& e) {
        }
      }
    }
  } else {
    std::string cleaned = inner;
    cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), '('), cleaned.end());
    cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), ')'), cleaned.end());

    auto points = split(cleaned, ',');
    for (const auto& point : points) {
      std::string p = trim(point);
      if (p.empty())
        continue;

      auto parts = split(p, ' ');
      if (parts.size() >= 2) {
        try {
          double x = std::stod(parts[0]);
          double y = std::stod(parts[1]);
          coords.push_back(x);
          coords.push_back(y);
        } catch (const std::exception& e) {
        }
      }
    }
  }

  std::cout << "  Extracted " << coords.size() / 2 << " points\n";
  if (coords.size() >= 4) {
    std::cout << "  First point: " << coords[0] << ", " << coords[1] << "\n";
    std::cout << "  Last point: " << coords[coords.size() - 2] << ", " << coords[coords.size() - 1]
              << "\n";
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
  int geometry_col = -1;

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

    if (line.empty() || line[0] == '#')
      continue;

    auto values = splitCSV(line);

    if (is_header) {
      headers = values;

      for (size_t i = 0; i < headers.size(); ++i) {
        std::string lower = toLower(headers[i]);
        if (lower == "street_name" || lower == "street" || lower == "name") {
          col_street_name = i;
        } else if (lower == "left_from" || lower == "l_from") {
          col_left_from = i;
        } else if (lower == "left_to" || lower == "l_to") {
          col_left_to = i;
        } else if (lower == "right_from" || lower == "r_from") {
          col_right_from = i;
        } else if (lower == "right_to" || lower == "r_to") {
          col_right_to = i;
        } else if (lower == "city" || lower == "city") {
          col_city = i;
        } else if (lower == "postal_code" || lower == "zip" || lower == "codigo_postal") {
          col_postal_code = i;
        } else if (lower == "geometry" || lower == "geom" || lower == "wkt") {
          col_geometry = i;
        }
      }

      std::cout << "Headers found:\n";
      for (size_t i = 0; i < headers.size(); ++i) {
        std::cout << "  " << i << ": " << headers[i] << "\n";
      }
      std::cout << "\n";

      is_header = false;
      continue;
    }

    if (col_street_name == -1 || col_geometry == -1) {
      std::cerr << "Error: Missing required columns (street_name, geometry)\n";
      break;
    }

    if ((col_left_from == -1 || col_left_to == -1) &&
        (col_right_from == -1 || col_right_to == -1)) {
      std::cerr << "Error: Missing range columns (left_from/left_to or right_from/right_to)\n";
      break;
    }

    StreetSegment seg;

    if (col_geometry >= 0 && col_geometry < (int)values.size()) {
      std::string geom_str = values[col_geometry];
      std::cout << "\nProcessing row " << line_num << ", geometry: " << geom_str.substr(0, 50)
                << "...\n";
      seg.coordinates = parseWKTGeometry(geom_str);
    }

    if (seg.coordinates.size() < 4) {
      std::cout << "  Skipping: insufficient coordinates (" << seg.coordinates.size() << ")\n";
      continue;
    }

    auto get_value = [&](int col) -> std::string {
      if (col >= 0 && col < (int)values.size()) {
        return values[col];
      }
      return "";
    };

    auto to_int = [](const std::string& s) -> int {
      try {
        return std::stoi(s);
      } catch (...) {
        return 0;
      }
    };

    seg.street_name = get_value(col_street_name);
    seg.city = get_value(col_city);
    seg.postal_code = get_value(col_postal_code);
    seg.left_from = to_int(get_value(col_left_from));
    seg.left_to = to_int(get_value(col_left_to));
    seg.right_from = to_int(get_value(col_right_from));
    seg.right_to = to_int(get_value(col_right_to));

    for (size_t i = 0; i < values.size() && i < headers.size(); ++i) {
      seg.extra_attrs[headers[i]] = values[i];
    }

    if (!seg.street_name.empty() && (seg.hasLeftRange() || seg.hasRightRange())) {
      segments.push_back(seg);
      std::cout << "  Added segment: " << seg.street_name << " [" << seg.left_from << "-"
                << seg.left_to << "]"
                << " (" << seg.coordinates.size() / 2 << " vertices)\n";
    }
  }

  return segments;
}

std::vector<AddressInput> readAddressesDirect(const std::string& filename) {
  std::vector<AddressInput> addresses;
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file: " << filename << "\n";
    return addresses;
  }

  std::string line;
  std::vector<std::string> headers;
  bool is_header = true;

  int col_id = -1;
  int col_house_number = -1;
  int col_street_name = -1;
  int col_city = -1;
  int col_postal_code = -1;

  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#')
      continue;

    auto values = splitCSV(line);

    if (is_header) {
      headers = values;

      for (size_t i = 0; i < headers.size(); ++i) {
        std::string lower = toLower(headers[i]);
        if (lower == "id") {
          col_id = i;
        } else if (lower == "house_number" || lower == "number" || lower == "num") {
          col_house_number = i;
        } else if (lower == "street_name" || lower == "street" || lower == "name") {
          col_street_name = i;
        } else if (lower == "city" || lower == "city") {
          col_city = i;
        } else if (lower == "postal_code" || lower == "zip" || lower == "codigo_postal") {
          col_postal_code = i;
        }
      }

      is_header = false;
      continue;
    }

    if (col_house_number == -1 || col_street_name == -1) {
      std::cerr << "Error: Missing required columns (house_number, street_name)\n";
      break;
    }

    AddressInput addr;

    auto get_value = [&](int col) -> std::string {
      if (col >= 0 && col < (int)values.size()) {
        return values[col];
      }
      return "";
    };

    addr.id = get_value(col_id);
    addr.street_name = get_value(col_street_name);
    addr.city = get_value(col_city);
    addr.postal_code = get_value(col_postal_code);

    try {
      addr.house_number = std::stoi(get_value(col_house_number));
    } catch (...) {
      addr.house_number = 0;
    }

    for (size_t i = 0; i < values.size() && i < headers.size(); ++i) {
      addr.extra_attrs[headers[i]] = values[i];
    }

    if (!addr.street_name.empty() && addr.house_number > 0) {
      addresses.push_back(addr);
    }
  }

  return addresses;
}

std::pair<double, double> geocodeAddress(const AddressInput& address,
                                         const std::vector<StreetSegment>& segments,
                                         double offset_distance, bool& matched) {
  matched = false;

  const StreetSegment* matched_segment = nullptr;

  for (const auto& seg : segments) {
    if (toLower(seg.street_name) != toLower(address.street_name)) {
      continue;
    }

    if (!address.city.empty() && toLower(seg.city) != toLower(address.city)) {
      continue;
    }

    if (!address.postal_code.empty() && toLower(seg.postal_code) != toLower(address.postal_code)) {
      continue;
    }

    int from, to;
    bool is_left;
    if (seg.getRangeForNumber(address.house_number, from, to, is_left)) {
      matched_segment = &seg;
      break;
    }
  }

  if (!matched_segment) {
    return {0, 0};
  }

  int from, to;
  bool is_left;
  if (!matched_segment->getRangeForNumber(address.house_number, from, to, is_left)) {
    return {0, 0};
  }

  double t = 0.0;
  if (to != from) {
    t = (double)(address.house_number - from) / (to - from);
  }
  t = std::max(0.0, std::min(1.0, t));

  auto pos = matched_segment->interpolatePosition(t);
  double px = pos.first;
  double py = pos.second;

  double total_len = matched_segment->getTotalLength();
  if (total_len < 1e-12) {
    matched = true;
    return {px, py};
  }

  double target_dist = t * total_len;
  double accumulated = 0;
  double dx = 0, dy = 0;
  bool found = false;

  for (size_t i = 0; i < matched_segment->coordinates.size() - 2; i += 2) {
    double seg_dx = matched_segment->coordinates[i + 2] - matched_segment->coordinates[i];
    double seg_dy = matched_segment->coordinates[i + 3] - matched_segment->coordinates[i + 1];
    double seg_len = std::sqrt(seg_dx * seg_dx + seg_dy * seg_dy);

    if (accumulated + seg_len >= target_dist || i == matched_segment->coordinates.size() - 3) {
      if (seg_len > 1e-12) {
        dx = seg_dx / seg_len;
        dy = seg_dy / seg_len;
      } else {
        dx = 1;
        dy = 0;
      }
      found = true;
      break;
    }
    accumulated += seg_len;
  }

  if (!found) {
    matched = true;
    return {px, py};
  }

  double nx = -dy;
  double ny = dx;

  bool is_even = (address.house_number % 2 == 0);
  if (!is_left) {
    is_even = !is_even;
  }

  double offset = offset_distance;
  if (!is_even) {
    offset = -offset_distance;
  }

  px += nx * offset;
  py += ny * offset;

  matched = true;
  return {px, py};
}

void writeGeocodedResults(const std::vector<AddressInput>& addresses,
                          const std::vector<std::pair<double, double>>& coords,
                          const std::vector<bool>& matched, const std::string& filename) {
  std::ofstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not create output file: " << filename << "\n";
    return;
  }

  file << "# Geocoded addresses\n";
  file << "# Geometry column: geometry\n";

  file << "id,house_number,street_name,city,postal_code";

  if (!addresses.empty()) {
    for (const auto& [key, value] : addresses[0].extra_attrs) {
      if (key != "id" && key != "house_number" && key != "street_name" && key != "city" &&
          key != "postal_code") {
        file << "," << key;
      }
    }
  }

  file << ",status,longitude,latitude,geometry\n";

  for (size_t i = 0; i < addresses.size(); ++i) {
    const auto& addr = addresses[i];
    double lon = coords[i].first;
    double lat = coords[i].second;
    std::string status = matched[i] ? "matched" : "not_found";

    file << addr.id << "," << addr.house_number << ","
         << "\"" << addr.street_name << "\","
         << "\"" << addr.city << "\","
         << "\"" << addr.postal_code << "\"";

    for (const auto& [key, value] : addr.extra_attrs) {
      if (key != "id" && key != "house_number" && key != "street_name" && key != "city" &&
          key != "postal_code") {
        file << "," << "\"" << value << "\"";
      }
    }

    file << "," << status << "," << std::fixed << std::setprecision(6) << lon << "," << std::fixed
         << std::setprecision(6) << lat << ","
         << "POINT(" << std::fixed << std::setprecision(6) << lon << " " << std::fixed
         << std::setprecision(6) << lat << ")\n";
  }
}

void printUsage() {
  std::cerr << "spatial_address - Geocode addresses by street interpolation\n\n";
  std::cerr << "Usage: spatial_address <streets.csv> <addresses.csv> <output.csv> [options]\n\n";
  std::cerr << "Street file must contain:\n";
  std::cerr << "  street_name, left_from, left_to, right_from, right_to, geometry\n";
  std::cerr << "  Optional: city, postal_code\n\n";
  std::cerr << "Addresses file must contain:\n";
  std::cerr << "  id, house_number, street_name\n";
  std::cerr << "  Optional: city, postal_code\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -offset <value>   Lateral offset from centerline (default: 5.0)\n";
  std::cerr << "\nExamples:\n";
  std::cerr << "  spatial_address streets.csv addresses.csv result.csv\n";
}

int main(int argc, char* argv[]) {
  if (argc < 4) {
    printUsage();
    return 1;
  }

  std::string streets_file = argv[1];
  std::string addresses_file = argv[2];
  std::string output_file = argv[3];
  double offset = 5.0;

  for (int i = 4; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-offset" && i + 1 < argc) {
      offset = std::stod(argv[++i]);
    }
  }

  std::cout << "========================================\n";
  std::cout << "  ADDRESS GEOCODING\n";
  std::cout << "========================================\n\n";
  std::cout << "Streets: " << streets_file << "\n";
  std::cout << "Addresses: " << addresses_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Offset: " << offset << "\n\n";

  std::vector<StreetSegment> segments = readStreetSegmentsDirect(streets_file);
  std::cout << "\nStreet segments loaded: " << segments.size() << "\n";

  if (segments.empty()) {
    std::cerr << "\nError: No valid street segments found\n";
    std::cerr << "Please check that your geometry column contains valid WKT LINESTRING data\n";
    return 1;
  }

  std::vector<AddressInput> addresses = readAddressesDirect(addresses_file);
  std::cout << "Addresses loaded: " << addresses.size() << "\n";

  if (addresses.empty()) {
    std::cerr << "\nError: No valid addresses found\n";
    return 1;
  }

  std::vector<std::pair<double, double>> coords;
  std::vector<bool> matched;
  int matched_count = 0;

  std::cout << "\nGeocoding addresses...\n";
  for (size_t i = 0; i < addresses.size(); ++i) {
    const auto& addr = addresses[i];
    bool is_matched;
    auto pos = geocodeAddress(addr, segments, offset, is_matched);
    coords.push_back(pos);
    matched.push_back(is_matched);
    if (is_matched)
      matched_count++;

    if ((i + 1) % 10 == 0) {
      std::cout << "\r  Progress: " << (i + 1) << "/" << addresses.size() << std::flush;
    }
  }
  std::cout << "\r  Progress: " << addresses.size() << "/" << addresses.size() << "\n";

  writeGeocodedResults(addresses, coords, matched, output_file);

  std::cout << "\n========================================\n";
  std::cout << "  GEOCODING COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Addresses processed: " << addresses.size() << "\n";
  std::cout << "Matched: " << matched_count << "\n";
  std::cout << "Not found: " << (addresses.size() - matched_count) << "\n";
  std::cout << "Output written to: " << output_file << "\n";

  return 0;
}
