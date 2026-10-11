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

  // Un lado acepta un numero si esta dentro de su rango y, cuando los dos
  // extremos del rango tienen la misma paridad (p. ej. 100-198 o 101-199),
  // si el numero tiene esa misma paridad. Asi 150 cae en el rango par y 151
  // en el impar aunque ambos queden numericamente dentro de ambos rangos.
  static bool sideAccepts(int number, int from, int to) {
    int lo = std::min(from, to);
    int hi = std::max(from, to);
    if (number < lo || number > hi) return false;
    if ((from % 2) == (to % 2)) return (number % 2) == (from % 2);
    return true;
  }

  bool getRangeForNumber(int number, int& from, int& to, bool& is_left) const {
    if (hasLeftRange() && sideAccepts(number, left_from, left_to)) {
      from = left_from;
      to = left_to;
      is_left = true;
      return true;
    }
    if (hasRightRange() && sideAccepts(number, right_from, right_to)) {
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

void printUsage() {
  std::cerr << "spatial_address - Geocode addresses by street interpolation\n\n";
  std::cerr << "Usage: spatial_address <streets.csv> <addresses.csv> <output.csv> [options]\n\n";
  std::cerr << "Street file must contain:\n";
  std::cerr << "  street_name, left_from, left_to, right_from, right_to, geometry\n";
  std::cerr << "  Optional: city, postal_code\n\n";
  std::cerr << "Addresses file must contain:\n";
  std::cerr << "  id, house_number, street_name\n";
  std::cerr << "  Optional: city, postal_code\n";
  std::cerr << "  (city/postal_code only filter when BOTH files declare them)\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -offset <value>      Lateral offset from centerline (default: 5.0)\n";
  std::cerr << "  -offset_field <col>  Column of the addresses file with a per-address offset\n";
  std::cerr << "  -exact               Exact street-name match, case-insensitive (default)\n";
  std::cerr << "  -fuzzy               Approximate street-name match (abbreviations, typos)\n";
  std::cerr << "  -tolerance <0-1>     Minimum similarity for -fuzzy (default: 0.8)\n";
  std::cerr << "  -city <name>         Only use street segments of this city\n";
  std::cerr << "  -postal <code>       Only use street segments of this postal code\n";
  std::cerr << "  -bbox \"x0 y0 x1 y1\" Only use street segments touching this box\n";
  std::cerr << "  -fallback centroid   For street found but number out of range, use street centroid\n";
  std::cerr << "  -reverse_direction   Reverse the digitizing direction of all streets\n";
  std::cerr << "  -include_segment     Add street_segment and measure columns\n";
  std::cerr << "  -include_side        Add side and offset columns\n";
  std::cerr << "  -stats               Print status statistics\n";
  std::cerr << "  -report <file>       Write a text report of the run\n";
  std::cerr << "  -h, -help            Display this help message\n\n";
  std::cerr << "Status values: matched, ambiguous, partial, not_found\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_address streets.csv addresses.csv result.csv\n";
  std::cerr << "  spatial_address streets.csv addresses.csv result.csv -fuzzy -tolerance 0.7 -stats\n";
}

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

  // Corregido: eliminada condición redundante
  if (geom_type == "LINESTRING") {
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
          std::cerr << "  Warning: Failed to parse coordinate values: " << e.what() << "\n";
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
          std::cerr << "  Warning: Failed to parse coordinate values: " << e.what() << "\n";
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
          std::cerr << "  Warning: Failed to parse coordinate values: " << e.what() << "\n";
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
        } else if (lower == "city") { // Corregido: eliminada condición redundante
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
        } else if (lower == "city") { // Corregido: eliminada condición redundante
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

// ---------------------------------------------------------------------------
// Opciones y resultados
// ---------------------------------------------------------------------------

struct Options {
  double offset = 5.0;
  std::string offset_field;
  bool fuzzy = false;
  double tolerance = 0.8;
  std::string city_filter;
  std::string postal_filter;
  bool has_bbox = false;
  double bbox[4] = {0, 0, 0, 0};  // xmin ymin xmax ymax
  bool stats = false;
  std::string report_file;
  bool include_segment = false;
  bool include_side = false;
  bool fallback_centroid = false;
  bool reverse_direction = false;
};

struct GeoResult {
  std::string status = "not_found";  // matched, partial, ambiguous, not_found
  double score = 0.0;
  double x = 0.0;
  double y = 0.0;
  int segment_index = -1;
  std::string side;
  double measure = 0.0;
  double offset = 0.0;
  bool located() const {
    return status == "matched" || status == "ambiguous" || status == "partial";
  }
};

static bool segmentInBBox(const StreetSegment& seg, const double* b) {
  for (size_t i = 0; i + 1 < seg.coordinates.size(); i += 2) {
    double x = seg.coordinates[i], y = seg.coordinates[i + 1];
    if (x >= b[0] && x <= b[2] && y >= b[1] && y <= b[3]) return true;
  }
  return false;
}

// La ciudad y el codigo postal solo descartan un segmento cuando AMBOS lados
// (direccion y calle) tienen valor y son distintos. Si la capa de calles no
// declara esos campos, no se usan como criterio.
static bool areaCompatible(const AddressInput& a, const StreetSegment& s) {
  if (!a.city.empty() && !s.city.empty() && toLower(a.city) != toLower(s.city)) return false;
  if (!a.postal_code.empty() && !s.postal_code.empty() &&
      toLower(a.postal_code) != toLower(s.postal_code))
    return false;
  return true;
}

static GeoResult geocodeAddress(const AddressInput& address,
                                const std::vector<StreetSegment>& segments, const Options& opt) {
  GeoResult res;

  const std::string key = addr::normalizeKey(address.street_name);
  const std::string plain = toLower(address.street_name);

  // 1. Candidatos por nombre de calle (exacto o aproximado).
  struct Cand {
    size_t idx;
    double name_score;
  };
  std::vector<Cand> cands;
  for (size_t i = 0; i < segments.size(); ++i) {
    const auto& seg = segments[i];
    if (!areaCompatible(address, seg)) continue;
    double sc = 0.0;
    if (opt.fuzzy) {
      sc = addr::similarity(key, addr::normalizeKey(seg.street_name));
      if (sc < opt.tolerance) continue;
    } else {
      if (plain != toLower(seg.street_name)) continue;
      sc = 1.0;
    }
    cands.push_back({i, sc});
  }

  if (cands.empty()) return res;  // calle no encontrada

  // 2. Entre los candidatos, los que contienen el numero de casa.
  double best = -1.0;
  std::vector<size_t> hits;
  for (const auto& c : cands) {
    int from = 0, to = 0;
    bool is_left = false;
    if (!segments[c.idx].getRangeForNumber(address.house_number, from, to, is_left)) continue;
    if (c.name_score > best + 1e-9) {
      best = c.name_score;
      hits.clear();
      hits.push_back(c.idx);
    } else if (std::fabs(c.name_score - best) <= 1e-9) {
      hits.push_back(c.idx);
    }
  }

  if (hits.empty()) {
    // Calle encontrada pero el numero no esta en ningun rango.
    double bs = 0.0;
    for (const auto& c : cands) bs = std::max(bs, c.name_score);
    res.status = "partial";
    res.score = bs * 0.5;
    if (opt.fallback_centroid) {
      double sx = 0, sy = 0;
      int n = 0;
      for (const auto& c : cands) {
        if (std::fabs(c.name_score - bs) > 1e-9) continue;
        const auto& seg = segments[c.idx];
        auto mid = seg.interpolatePosition(0.5);
        sx += mid.first;
        sy += mid.second;
        ++n;
      }
      if (n > 0) {
        res.x = sx / n;
        res.y = sy / n;
        res.segment_index = (int)cands.front().idx;
        res.side = "";
        return res;
      }
    }
    return res;  // partial sin coordenadas (0,0)
  }

  const StreetSegment& seg = segments[hits.front()];
  int from = 0, to = 0;
  bool is_left = false;
  seg.getRangeForNumber(address.house_number, from, to, is_left);

  double t = 0.0;
  if (to != from) t = (double)(address.house_number - from) / (to - from);
  t = std::max(0.0, std::min(1.0, t));

  auto pos = seg.interpolatePosition(t);
  double px = pos.first, py = pos.second;

  // Direccion de la linea en el punto interpolado.
  double dx = 1, dy = 0;
  double total_len = seg.getTotalLength();
  if (total_len > 1e-12) {
    double target = t * total_len, acc = 0;
    for (size_t i = 0; i + 3 < seg.coordinates.size(); i += 2) {
      double sdx = seg.coordinates[i + 2] - seg.coordinates[i];
      double sdy = seg.coordinates[i + 3] - seg.coordinates[i + 1];
      double sl = std::sqrt(sdx * sdx + sdy * sdy);
      if (acc + sl >= target || i + 4 >= seg.coordinates.size()) {
        if (sl > 1e-12) {
          dx = sdx / sl;
          dy = sdy / sl;
        }
        break;
      }
      acc += sl;
    }
  }

  // Desplazamiento lateral: positivo hacia la izquierda de la linea.
  double off = opt.offset;
  if (!opt.offset_field.empty()) {
    auto it = address.extra_attrs.find(opt.offset_field);
    if (it != address.extra_attrs.end()) {
      try {
        off = std::stod(it->second);
      } catch (...) {
      }
    }
  }
  // El rango izquierdo queda a la izquierda del sentido de digitalizacion y el
  // derecho a la derecha (la normal izquierda de (dx,dy) es (-dy,dx)).
  double signed_off = is_left ? off : -off;

  px += (-dy) * signed_off;
  py += (dx) * signed_off;

  res.x = px;
  res.y = py;
  res.segment_index = (int)hits.front();
  res.side = is_left ? "left" : "right";
  res.measure = t;
  res.offset = off;
  res.score = best;
  res.status = (hits.size() > 1) ? "ambiguous" : "matched";
  return res;
}

static void writeGeocodedResults(const std::vector<AddressInput>& addresses,
                                 const std::vector<GeoResult>& results,
                                 const std::vector<StreetSegment>& segments, const Options& opt,
                                 const std::string& filename) {
  std::ofstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not create output file: " << filename << "\n";
    return;
  }

  static const std::set<std::string> base = {"id", "house_number", "street_name", "city",
                                             "postal_code"};

  file << "# Geocoded addresses\n";
  file << "# Geometry column: geometry\n";
  file << "id,house_number,street_name,city,postal_code";

  std::vector<std::string> extra_keys;
  if (!addresses.empty()) {
    for (const auto& kv : addresses[0].extra_attrs) {
      if (!base.count(kv.first)) {
        extra_keys.push_back(kv.first);
        file << "," << addr::csvEscape(kv.first);
      }
    }
  }
  file << ",status,score";
  if (opt.include_segment) file << ",street_segment,measure";
  if (opt.include_side) file << ",side,offset";
  file << ",longitude,latitude,geometry\n";

  file << std::fixed << std::setprecision(6);
  for (size_t i = 0; i < addresses.size(); ++i) {
    const auto& a = addresses[i];
    const auto& r = results[i];

    file << a.id << "," << a.house_number << ","
         << "\"" << a.street_name << "\","
         << "\"" << a.city << "\","
         << "\"" << a.postal_code << "\"";
    for (const auto& k : extra_keys) {
      auto it = a.extra_attrs.find(k);
      file << ",\"" << (it != a.extra_attrs.end() ? it->second : "") << "\"";
    }

    file << "," << r.status << "," << std::setprecision(3) << r.score << std::setprecision(6);
    if (opt.include_segment) {
      std::string sid;
      if (r.segment_index >= 0) {
        const auto& seg = segments[r.segment_index];
        auto it = seg.extra_attrs.find("id");
        sid = (it != seg.extra_attrs.end() && !it->second.empty())
                  ? it->second
                  : std::to_string(r.segment_index + 1);
      }
      file << "," << sid << "," << (r.side.empty() ? std::string() : std::to_string(r.measure));
    }
    if (opt.include_side) {
      file << "," << r.side << ","
           << (r.side.empty() ? std::string() : std::to_string(r.offset));
    }
    file << "," << r.x << "," << r.y << ",POINT(" << r.x << " " << r.y << ")\n";
  }
}

static void writeReport(const std::string& filename, const Options& opt,
                        const std::string& streets_file, const std::string& addresses_file,
                        const std::vector<AddressInput>& addresses,
                        const std::vector<GeoResult>& results,
                        const std::map<std::string, int>& counts) {
  std::ofstream rpt(filename);
  if (!rpt.is_open()) {
    std::cerr << "Error: Could not create report file: " << filename << "\n";
    return;
  }
  rpt << "GEOCODING REPORT\n================\n";
  rpt << "Streets file:   " << streets_file << "\n";
  rpt << "Addresses file: " << addresses_file << "\n";
  rpt << "Offset:         " << opt.offset
      << (opt.offset_field.empty() ? "" : " (campo: " + opt.offset_field + ")") << "\n";
  rpt << "Matching:       " << (opt.fuzzy ? "fuzzy" : "exact");
  if (opt.fuzzy) rpt << " (tolerance " << opt.tolerance << ")";
  rpt << "\n";
  rpt << "Fallback:       " << (opt.fallback_centroid ? "centroid" : "none") << "\n";
  rpt << "Reverse dir:    " << (opt.reverse_direction ? "yes" : "no") << "\n\n";
  rpt << "Total addresses: " << addresses.size() << "\n";
  for (const auto& kv : counts) rpt << "  " << kv.first << ": " << kv.second << "\n";
  rpt << "\nAddresses not fully resolved:\n";
  bool any = false;
  for (size_t i = 0; i < addresses.size(); ++i) {
    if (results[i].status == "matched") continue;
    any = true;
    rpt << "  id=" << addresses[i].id << " " << addresses[i].house_number << " "
        << addresses[i].street_name << " -> " << results[i].status << "\n";
  }
  if (!any) rpt << "  (none)\n";
}

int main(int argc, char* argv[]) {
  if (argc == 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "-help")) {
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
  Options opt;

  for (int i = 4; i < argc; ++i) {
    std::string arg = argv[i];
    auto need = [&](const char* what) -> const char* {
      if (i + 1 >= argc) {
        std::cerr << "Error: option " << arg << " requires " << what << "\n\n";
        printUsage();
        std::exit(1);
      }
      return argv[++i];
    };
    try {
      if (arg == "-offset") {
        opt.offset = std::stod(need("a value"));
      } else if (arg == "-offset_field") {
        opt.offset_field = need("a column name");
      } else if (arg == "-fuzzy") {
        opt.fuzzy = true;
      } else if (arg == "-exact") {
        opt.fuzzy = false;
      } else if (arg == "-tolerance") {
        opt.tolerance = std::stod(need("a value between 0 and 1"));
        if (opt.tolerance < 0.0 || opt.tolerance > 1.0) {
          std::cerr << "Error: -tolerance must be between 0 and 1\n";
          return 1;
        }
      } else if (arg == "-city") {
        opt.city_filter = need("a city name");
      } else if (arg == "-postal") {
        opt.postal_filter = need("a postal code");
      } else if (arg == "-bbox") {
        std::istringstream iss(need("\"xmin ymin xmax ymax\""));
        if (!(iss >> opt.bbox[0] >> opt.bbox[1] >> opt.bbox[2] >> opt.bbox[3])) {
          std::cerr << "Error: -bbox expects \"xmin ymin xmax ymax\"\n";
          return 1;
        }
        opt.has_bbox = true;
      } else if (arg == "-stats") {
        opt.stats = true;
      } else if (arg == "-report") {
        opt.report_file = need("a file name");
      } else if (arg == "-include_segment") {
        opt.include_segment = true;
      } else if (arg == "-include_side") {
        opt.include_side = true;
      } else if (arg == "-fallback") {
        std::string v = need("a mode (centroid)");
        if (v != "centroid") {
          std::cerr << "Error: unknown fallback mode: " << v << " (supported: centroid)\n";
          return 1;
        }
        opt.fallback_centroid = true;
      } else if (arg == "-reverse_direction") {
        opt.reverse_direction = true;
      } else if (arg == "-h" || arg == "-help") {
        printUsage();
        return 0;
      } else {
        std::cerr << "Error: Unknown option: " << arg << "\n\n";
        printUsage();
        return 1;
      }
    } catch (const std::exception&) {
      std::cerr << "Error: invalid value for " << arg << "\n";
      return 1;
    }
  }

  std::cout << "========================================\n";
  std::cout << "  ADDRESS GEOCODING\n";
  std::cout << "========================================\n\n";
  std::cout << "Streets: " << streets_file << "\n";
  std::cout << "Addresses: " << addresses_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Offset: " << opt.offset << "\n";
  std::cout << "Matching: " << (opt.fuzzy ? "fuzzy" : "exact");
  if (opt.fuzzy) std::cout << " (tolerance " << opt.tolerance << ")";
  std::cout << "\n\n";

  std::vector<StreetSegment> all_segments = readStreetSegmentsDirect(streets_file);
  std::cout << "\nStreet segments loaded: " << all_segments.size() << "\n";

  if (all_segments.empty()) {
    std::cerr << "\nError: No valid street segments found\n";
    std::cerr << "Please check that your geometry column contains valid WKT LINESTRING data\n";
    return 1;
  }

  // Filtros de area de busqueda.
  std::vector<StreetSegment> segments;
  for (auto& seg : all_segments) {
    if (!opt.city_filter.empty() && toLower(seg.city) != toLower(opt.city_filter)) continue;
    if (!opt.postal_filter.empty() && toLower(seg.postal_code) != toLower(opt.postal_filter))
      continue;
    if (opt.has_bbox && !segmentInBBox(seg, opt.bbox)) continue;
    if (opt.reverse_direction) {
      std::vector<double> rev;
      for (size_t k = seg.coordinates.size(); k >= 2; k -= 2) {
        rev.push_back(seg.coordinates[k - 2]);
        rev.push_back(seg.coordinates[k - 1]);
      }
      seg.coordinates = rev;
    }
    segments.push_back(seg);
  }
  if (segments.size() != all_segments.size()) {
    std::cout << "Segments after filters: " << segments.size() << "\n";
  }
  if (segments.empty()) {
    std::cerr << "\nError: No street segments left after applying -city/-postal/-bbox filters\n";
    return 1;
  }

  std::vector<AddressInput> addresses = readAddressesDirect(addresses_file);
  std::cout << "Addresses loaded: " << addresses.size() << "\n";

  if (addresses.empty()) {
    std::cerr << "\nError: No valid addresses found\n";
    return 1;
  }

  std::vector<GeoResult> results;
  std::map<std::string, int> counts = {
      {"matched", 0}, {"ambiguous", 0}, {"partial", 0}, {"not_found", 0}};
  double score_sum = 0;

  std::cout << "\nGeocoding addresses...\n";
  for (size_t i = 0; i < addresses.size(); ++i) {
    GeoResult r = geocodeAddress(addresses[i], segments, opt);
    counts[r.status]++;
    score_sum += r.score;
    results.push_back(r);
    if ((i + 1) % 10 == 0) {
      std::cout << "\r  Progress: " << (i + 1) << "/" << addresses.size() << std::flush;
    }
  }
  std::cout << "\r  Progress: " << addresses.size() << "/" << addresses.size() << "\n";

  writeGeocodedResults(addresses, results, segments, opt, output_file);
  if (!opt.report_file.empty()) {
    writeReport(opt.report_file, opt, streets_file, addresses_file, addresses, results, counts);
  }

  int located = counts["matched"] + counts["ambiguous"];
  std::cout << "\n========================================\n";
  std::cout << "  GEOCODING COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Addresses processed: " << addresses.size() << "\n";
  std::cout << "Matched: " << located << "\n";
  std::cout << "Not found: " << (addresses.size() - located) << "\n";
  std::cout << "Output written to: " << output_file << "\n";

  if (opt.stats) {
    std::cout << "\nStatistics:\n";
    for (const auto& kv : counts) {
      double pct = 100.0 * kv.second / (double)addresses.size();
      std::cout << "  " << std::left << std::setw(10) << kv.first << std::right << std::setw(6)
                << kv.second << "  (" << std::fixed << std::setprecision(1) << pct << "%)\n";
    }
    std::cout << "  Mean score: " << std::fixed << std::setprecision(3)
              << score_sum / (double)addresses.size() << "\n";
  }
  if (!opt.report_file.empty()) {
    std::cout << "Report written to: " << opt.report_file << "\n";
  }

  return 0;
}
