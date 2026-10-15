#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../include/core/spatial_streets.hpp"

using streets::csvQuote;
using streets::Segment;

struct AddressInput {
  std::string id;
  std::string house_text;
  int house_number = 0;
  bool number_ok = false;
  std::string street_name, city, postal_code;
  std::vector<std::string> extras;  // values of the extra columns, in header order
};

struct GeocodeResult {
  std::string status = "not_found";  // matched | partial | not_found | invalid
  double score = 0;                  // similarity of the street name (1 = exact)
  bool has_point = false;
  double x = 0, y = 0;
  std::string side;
  int segment_row = 0;
  double measure = 0;
  double offset = 0;
};

static bool readAddresses(const std::string& filename, std::vector<AddressInput>& out,
                          std::vector<std::string>& extra_names, std::string& err) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    err = "Could not open file: " + filename;
    return false;
  }

  std::string line;
  bool is_header = true;
  int col_id = -1, col_num = -1, col_street = -1, col_city = -1, col_postal = -1;
  std::vector<int> extra_cols;

  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#')
      continue;
    auto values = splitCSV(line);

    if (is_header) {
      for (size_t i = 0; i < values.size(); ++i) {
        std::string h = toLower(trim(values[i]));
        if (h == "id")
          col_id = (int)i;
        else if (h == "house_number" || h == "number" || h == "num")
          col_num = (int)i;
        else if (h == "street_name" || h == "street" || h == "name")
          col_street = (int)i;
        else if (h == "city")
          col_city = (int)i;
        else if (h == "postal_code" || h == "zip" || h == "codigo_postal")
          col_postal = (int)i;
        else if (h == "geometry" || h == "longitude" || h == "latitude" || h == "status")
          continue;  // output columns, never copied from the input
        else {
          extra_cols.push_back((int)i);
          extra_names.push_back(trim(values[i]));
        }
      }
      if (col_num < 0 || col_street < 0) {
        err = "Missing required columns (house_number, street_name)";
        return false;
      }
      is_header = false;
      continue;
    }

    auto get = [&](int c) -> std::string {
      return (c >= 0 && c < (int)values.size()) ? trim(values[c]) : "";
    };

    AddressInput a;
    a.id = get(col_id);
    a.house_text = get(col_num);
    a.number_ok = streets::parseInt(a.house_text, a.house_number) && a.house_number > 0;
    a.street_name = get(col_street);
    a.city = get(col_city);
    a.postal_code = get(col_postal);
    for (int c : extra_cols)
      a.extras.push_back(get(c));
    out.push_back(a);
  }
  return true;
}

static GeocodeResult geocode(const AddressInput& addr, const std::vector<Segment>& segments,
                             double offset_distance, bool fuzzy, double tolerance) {
  GeocodeResult res;
  if (!addr.number_ok || addr.street_name.empty()) {
    res.status = "invalid";
    return res;
  }

  std::string wanted = streets::normalizeName(addr.street_name);
  std::string wanted_city = streets::normalizeName(addr.city);
  std::string wanted_postal = streets::normalizeName(addr.postal_code);

  const Segment* best = nullptr;
  bool best_left = false;
  double best_sim = -1;
  double street_sim = 0;  // best similarity among segments of an accepted street name
  bool street_found = false;

  for (const auto& seg : segments) {
    if (seg.vertexCount() < 2)
      continue;
    // City / postal code only filter when both sides provide a value.
    if (!wanted_city.empty() && !seg.city.empty() && streets::normalizeName(seg.city) != wanted_city)
      continue;
    if (!wanted_postal.empty() && !seg.postal.empty() &&
        streets::normalizeName(seg.postal) != wanted_postal)
      continue;

    double sim = streets::nameSimilarity(wanted, streets::normalizeName(seg.name));
    bool accepted = fuzzy ? (sim >= tolerance) : (sim >= 1.0);
    if (!accepted)
      continue;

    street_found = true;
    street_sim = std::max(street_sim, sim);

    bool in_left = seg.left.contains(addr.house_number);
    bool in_right = seg.right.contains(addr.house_number);
    if ((in_left || in_right) && sim > best_sim) {
      best = &seg;
      best_left = in_left;
      best_sim = sim;
    }
  }

  if (!best) {
    res.status = street_found ? "partial" : "not_found";
    res.score = street_found ? street_sim : 0;
    return res;
  }

  const streets::Range& r = best_left ? best->left : best->right;
  double t = (r.to != r.from) ? (double)(addr.house_number - r.from) / (r.to - r.from) : 0.0;
  t = std::max(0.0, std::min(1.0, t));

  double len = best->length();
  double px, py, tx, ty;
  best->pointAt(t * len, px, py, tx, ty);

  // Left normal of the digitizing direction is (-ty, tx).
  double signed_offset = best_left ? offset_distance : -offset_distance;
  res.status = "matched";
  res.score = best_sim;
  res.has_point = true;
  res.x = px + (-ty) * signed_offset;
  res.y = py + tx * signed_offset;
  res.side = best_left ? "left" : "right";
  res.segment_row = best->row;
  res.measure = t * len;
  res.offset = signed_offset;
  return res;
}

static void printUsage() {
  std::cerr << "spatial_address - Geocode addresses by street interpolation\n\n";
  std::cerr << "Usage: spatial_address <streets.csv> <addresses.csv> <output.csv> [options]\n\n";
  std::cerr << "Street file must contain:\n";
  std::cerr << "  street_name, left_from, left_to, right_from, right_to, geometry\n";
  std::cerr << "  Optional: left_parity, right_parity (even|odd|both), city, postal_code\n\n";
  std::cerr << "Addresses file must contain:\n";
  std::cerr << "  id, house_number, street_name\n";
  std::cerr << "  Optional: city, postal_code (any other column is copied to the output)\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -offset <value>      Lateral offset from the centerline (default: 5.0)\n";
  std::cerr << "  -fuzzy               Accept street names that are similar but not identical\n";
  std::cerr << "  -tolerance <value>   Minimum name similarity in [0, 1] for -fuzzy (default: 0.8)\n";
  std::cerr << "  -verbose             Print the result of every address\n\n";
  std::cerr << "Output columns added to the address columns:\n";
  std::cerr << "  status (matched | partial | not_found | invalid), score, side, street_segment,\n";
  std::cerr << "  measure, offset, longitude, latitude, geometry\n";
  std::cerr << "Addresses that are not matched have an empty geometry.\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_address streets.csv addresses.csv result.csv\n";
  std::cerr << "  spatial_address streets.csv addresses.csv result.csv -offset 8 -fuzzy -tolerance 0.7\n";
}

int main(int argc, char* argv[]) {
  if (argc >= 2 && (std::string(argv[1]) == "-help" || std::string(argv[1]) == "-h")) {
    printUsage();
    return 0;
  }
  if (argc < 4) {
    printUsage();
    return 1;
  }

  std::string streets_file = argv[1];
  std::string addresses_file = argv[2];
  std::string output_file = argv[3];
  double offset = 5.0;
  bool fuzzy = false, verbose = false;
  double tolerance = 0.8;

  for (int i = 4; i < argc; ++i) {
    std::string arg = argv[i];
    try {
      if (arg == "-offset" && i + 1 < argc) {
        offset = std::stod(argv[++i]);
      } else if (arg == "-tolerance" && i + 1 < argc) {
        tolerance = std::stod(argv[++i]);
        if (tolerance < 0 || tolerance > 1) {
          std::cerr << "Error: -tolerance must be between 0 and 1\n";
          return 1;
        }
      } else if (arg == "-fuzzy") {
        fuzzy = true;
      } else if (arg == "-verbose") {
        verbose = true;
      } else if (arg == "-help" || arg == "-h") {
        printUsage();
        return 0;
      } else {
        std::cerr << "Error: Unknown option or missing value: " << arg << "\n";
        printUsage();
        return 1;
      }
    } catch (const std::exception&) {
      std::cerr << "Error: Invalid numeric value for " << arg << "\n";
      return 1;
    }
  }

  std::cout << "========================================\n";
  std::cout << "  ADDRESS GEOCODING\n";
  std::cout << "========================================\n\n";
  std::cout << "Streets: " << streets_file << "\n";
  std::cout << "Addresses: " << addresses_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Offset: " << offset << "\n";
  if (fuzzy)
    std::cout << "Fuzzy matching: tolerance " << tolerance << "\n";
  std::cout << "\n";

  std::string err;
  std::vector<Segment> segments;
  if (!streets::readStreets(streets_file, segments, err)) {
    std::cerr << "Error: " << err << "\n";
    return 1;
  }
  size_t usable = 0;
  for (const auto& s : segments)
    if (s.vertexCount() >= 2 && (s.left.valid || s.right.valid) && !s.name.empty())
      usable++;
  std::cout << "Street segments loaded: " << usable << "\n";
  if (usable == 0) {
    std::cerr << "Error: No valid street segments found (check name, ranges and WKT LINESTRING)\n";
    return 1;
  }

  std::vector<AddressInput> addresses;
  std::vector<std::string> extra_names;
  if (!readAddresses(addresses_file, addresses, extra_names, err)) {
    std::cerr << "Error: " << err << "\n";
    return 1;
  }
  std::cout << "Addresses loaded: " << addresses.size() << "\n";
  if (addresses.empty()) {
    std::cerr << "Error: No addresses found\n";
    return 1;
  }

  std::ofstream out(output_file);
  if (!out.is_open()) {
    std::cerr << "Error: Could not create output file: " << output_file << "\n";
    return 1;
  }
  out << "# Geocoded addresses\n# Geometry column: geometry\n";
  out << "id,house_number,street_name,city,postal_code";
  for (const auto& n : extra_names)
    out << "," << csvQuote(n);
  out << ",status,score,side,street_segment,measure,offset,longitude,latitude,geometry\n";

  std::map<std::string, int> counts;
  for (const auto& addr : addresses) {
    GeocodeResult r = geocode(addr, segments, offset, fuzzy, tolerance);
    counts[r.status]++;

    out << csvQuote(addr.id) << "," << csvQuote(addr.house_text) << "," << csvQuote(addr.street_name)
        << "," << csvQuote(addr.city) << "," << csvQuote(addr.postal_code);
    for (const auto& v : addr.extras)
      out << "," << csvQuote(v);
    out << "," << r.status << "," << std::fixed << std::setprecision(2) << r.score;
    if (r.has_point) {
      out << "," << r.side << "," << r.segment_row << "," << std::setprecision(6) << r.measure << ","
          << r.offset << "," << r.x << "," << r.y << ",POINT(" << r.x << " " << r.y << ")\n";
    } else {
      out << ",,,,,,,POINT EMPTY\n";
    }

    if (verbose) {
      std::cout << "  " << addr.id << ": " << addr.house_text << " " << addr.street_name << " -> "
                << r.status;
      if (r.has_point)
        std::cout << " (" << r.side << ", " << std::fixed << std::setprecision(3) << r.x << " " << r.y
                  << ")";
      std::cout << "\n";
    }
  }

  std::cout << "\n========================================\n";
  std::cout << "  GEOCODING COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Addresses processed: " << addresses.size() << "\n";
  std::cout << "Matched: " << counts["matched"] << "\n";
  std::cout << "Partial (street found, number out of range): " << counts["partial"] << "\n";
  std::cout << "Not found: " << counts["not_found"] << "\n";
  if (counts["invalid"] > 0)
    std::cout << "Invalid (missing street or non-numeric house number): " << counts["invalid"] << "\n";
  std::cout << "Output written to: " << output_file << "\n";
  return 0;
}
