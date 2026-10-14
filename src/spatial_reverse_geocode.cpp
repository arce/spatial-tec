#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
<<<<<<< HEAD
=======
#include <limits>
#include <map>
>>>>>>> 9612af9c91ae858debce8b24c2c80f22bd6c523e
#include <sstream>
#include <string>
#include <vector>

<<<<<<< HEAD
#include "../include/core/spatial_streets.hpp"

using streets::csvQuote;
using streets::Segment;

struct PointInput {
  std::string id;
  double x = 0, y = 0;
  bool ok = false;
  std::vector<std::string> extras;
};

static bool readPoints(const std::string& filename, std::vector<PointInput>& out,
                       std::vector<std::string>& extra_names, std::string& err) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    err = "Could not open file: " + filename;
    return false;
  }

  std::string line;
  bool is_header = true;
  int col_id = -1, col_geom = -1, col_x = -1, col_y = -1;
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
        else if (h == "geometry" || h == "geom" || h == "wkt")
          col_geom = (int)i;
        else if (h == "x" || h == "longitude" || h == "lon")
          col_x = (int)i;
        else if (h == "y" || h == "latitude" || h == "lat")
          col_y = (int)i;
        else {
          extra_cols.push_back((int)i);
          extra_names.push_back(trim(values[i]));
        }
      }
      if (col_geom < 0 && (col_x < 0 || col_y < 0)) {
        err = "The points file needs a geometry column (POINT WKT) or x,y columns";
        return false;
      }
      is_header = false;
      continue;
    }

    auto get = [&](int c) -> std::string {
      return (c >= 0 && c < (int)values.size()) ? trim(values[c]) : "";
    };

    PointInput p;
    p.id = get(col_id);
    if (col_geom >= 0) {
      auto xy = streets::parseWKTCoords(get(col_geom));
      if (xy.size() >= 2) {
        p.x = xy[0];
        p.y = xy[1];
        p.ok = true;
      }
    }
    if (!p.ok && col_x >= 0 && col_y >= 0) {
      try {
        p.x = std::stod(get(col_x));
        p.y = std::stod(get(col_y));
        p.ok = true;
      } catch (...) {
      }
    }
    for (int c : extra_cols)
      p.extras.push_back(get(c));
    out.push_back(p);
=======
#include "../include/core/spatial_address_utils.hpp"
#include "../include/core/spatial_string.hpp"

// ---------------------------------------------------------------------------
// Datos
// ---------------------------------------------------------------------------

struct StreetSegment {
  std::string id;
  std::string street_name;
  std::string city;
  std::string postal_code;
  std::vector<double> coords;  // x0 y0 x1 y1 ...
  int left_from = 0, left_to = 0, right_from = 0, right_to = 0;
  bool hasLeft() const { return left_from != 0 || left_to != 0; }
  bool hasRight() const { return right_from != 0 || right_to != 0; }
};

struct QueryPoint {
  std::string id;
  double x = 0, y = 0;
};

struct KnownAddress {
  std::string id;
  int house_number = 0;
  std::string street_name;
  double x = 0, y = 0;
};

struct ReverseResult {
  std::string status = "too_far";  // matched, approximate, too_far
  int segment_index = -1;
  std::string side;
  double distance = 0;
  double measure = 0;
  int house_number = 0;
  // Direccion conocida mas cercana (opcional, -addresses).
  std::string near_id;
  std::string near_label;
  double near_dist = -1;
};

static std::vector<double> parseWKT(const std::string& wkt) {
  std::vector<double> coords;
  std::string str = trim(wkt);
  size_t start = str.find('(');
  size_t end = str.rfind(')');
  if (start == std::string::npos || end == std::string::npos || end <= start) return coords;
  std::string inner = str.substr(start + 1, end - start - 1);
  inner.erase(std::remove(inner.begin(), inner.end(), '('), inner.end());
  inner.erase(std::remove(inner.begin(), inner.end(), ')'), inner.end());
  for (const auto& point : split(inner, ',')) {
    auto parts = split(trim(point), ' ');
    if (parts.size() >= 2) {
      try {
        coords.push_back(std::stod(parts[0]));
        coords.push_back(std::stod(parts[1]));
      } catch (...) {
      }
    }
  }
  return coords;
}

static int toInt(const std::string& s) {
  try {
    return std::stoi(s);
  } catch (...) {
    return 0;
  }
}

static std::string cell(const std::vector<std::string>& v, int c) {
  return (c >= 0 && c < (int)v.size()) ? v[c] : "";
}

static std::vector<StreetSegment> readStreets(const std::string& filename) {
  std::vector<StreetSegment> out;
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file: " << filename << "\n";
    return out;
  }
  std::string line;
  bool header = true;
  int c_id = -1, c_name = -1, c_lf = -1, c_lt = -1, c_rf = -1, c_rt = -1, c_city = -1,
      c_postal = -1, c_geom = -1;
  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    auto v = splitCSV(line);
    if (header) {
      for (size_t i = 0; i < v.size(); ++i) {
        std::string l = toLower(v[i]);
        int k = (int)i;
        if (l == "id") c_id = k;
        else if (l == "street_name" || l == "street" || l == "name") c_name = k;
        else if (l == "left_from" || l == "l_from") c_lf = k;
        else if (l == "left_to" || l == "l_to") c_lt = k;
        else if (l == "right_from" || l == "r_from") c_rf = k;
        else if (l == "right_to" || l == "r_to") c_rt = k;
        else if (l == "city") c_city = k;
        else if (l == "postal_code" || l == "zip" || l == "codigo_postal") c_postal = k;
        else if (l == "geometry" || l == "geom" || l == "wkt") c_geom = k;
      }
      header = false;
      if (c_name < 0 || c_geom < 0) {
        std::cerr << "Error: streets file needs street_name and geometry columns\n";
        return out;
      }
      continue;
    }
    StreetSegment s;
    s.id = cell(v, c_id);
    s.street_name = cell(v, c_name);
    s.city = cell(v, c_city);
    s.postal_code = cell(v, c_postal);
    s.left_from = toInt(cell(v, c_lf));
    s.left_to = toInt(cell(v, c_lt));
    s.right_from = toInt(cell(v, c_rf));
    s.right_to = toInt(cell(v, c_rt));
    s.coords = parseWKT(cell(v, c_geom));
    if (s.street_name.empty() || s.coords.size() < 4) continue;
    if (!s.hasLeft() && !s.hasRight()) continue;
    out.push_back(s);
  }
  return out;
}

// Lee puntos (id + x/y, longitude/latitude o geometry POINT). Si
// `need_address` es true tambien lee house_number/street_name y solo conserva
// las filas ya ubicadas (status matched/ambiguous o sin columna status).
template <typename T, typename F>
static bool readPointFile(const std::string& filename, bool need_address, std::vector<T>& out,
                          F make) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file: " << filename << "\n";
    return false;
  }
  std::string line;
  bool header = true;
  int c_id = -1, c_x = -1, c_y = -1, c_geom = -1, c_num = -1, c_street = -1, c_status = -1;
  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    auto v = splitCSV(line);
    if (header) {
      for (size_t i = 0; i < v.size(); ++i) {
        std::string l = toLower(v[i]);
        int k = (int)i;
        if (l == "id") c_id = k;
        else if (l == "longitude" || l == "lon" || l == "x") c_x = k;
        else if (l == "latitude" || l == "lat" || l == "y") c_y = k;
        else if (l == "geometry" || l == "geom" || l == "wkt") c_geom = k;
        else if (l == "house_number" || l == "number" || l == "num") c_num = k;
        else if (l == "street_name" || l == "street") c_street = k;
        else if (l == "status") c_status = k;
      }
      header = false;
      if ((c_x < 0 || c_y < 0) && c_geom < 0) {
        std::cerr << "Error: " << filename
                  << " needs longitude,latitude (or x,y) columns, or a geometry column\n";
        return false;
      }
      if (need_address && (c_num < 0 || c_street < 0)) {
        std::cerr << "Error: " << filename << " needs house_number and street_name columns\n";
        return false;
      }
      continue;
    }
    if (need_address && c_status >= 0) {
      std::string st = cell(v, c_status);
      if (st != "matched" && st != "ambiguous") continue;
    }
    double x = 0, y = 0;
    try {
      if (c_x >= 0 && c_y >= 0 && !cell(v, c_x).empty() && !cell(v, c_y).empty()) {
        x = std::stod(cell(v, c_x));
        y = std::stod(cell(v, c_y));
      } else {
        auto c = parseWKT(cell(v, c_geom));
        if (c.size() < 2) continue;
        x = c[0];
        y = c[1];
      }
    } catch (...) {
      continue;
    }
    out.push_back(make(cell(v, c_id), x, y, cell(v, c_num), cell(v, c_street)));
>>>>>>> 9612af9c91ae858debce8b24c2c80f22bd6c523e
  }
  return true;
}

<<<<<<< HEAD
// House number on a side of a segment closest to the position t in [0, 1],
// respecting the parity of the side (e.g. only even numbers on an even side).
static int interpolateNumber(const streets::Range& r, double t) {
  if (r.from == r.to)
    return r.from;
  int step = (r.parity == "both") ? 1 : 2;
  int dir = r.to > r.from ? 1 : -1;
  double raw = r.from + t * (r.to - r.from);
  int count = (int)std::floor(std::abs(r.to - r.from) / (double)step);
  int k = (int)std::lround((raw - r.from) / (dir * step));
  k = std::max(0, std::min(count, k));
  return r.from + dir * step * k;
}

static void printUsage() {
  std::cerr << "spatial_reverse_geocode - Find the approximate street address of one or more points\n\n";
  std::cerr << "Usage: spatial_reverse_geocode <streets.csv> <points.csv> <output.csv> [options]\n\n";
  std::cerr << "Street file must contain:\n";
  std::cerr << "  street_name, left_from, left_to, right_from, right_to, geometry\n";
  std::cerr << "  Optional: left_parity, right_parity (even|odd|both), city, postal_code\n\n";
  std::cerr << "Points file must contain:\n";
  std::cerr << "  a geometry column (POINT WKT) or x,y columns; optionally id\n";
  std::cerr << "  (any other column is copied to the output)\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -max_dist <value>   Maximum distance from the point to a street (default: 50.0)\n";
  std::cerr << "  -verbose            Print the result of every point\n\n";
  std::cerr << "Each point is snapped to the nearest street segment and the house number is\n";
  std::cerr << "interpolated from the numbering range of the side of the street where it lies.\n";
  std::cerr << "Output columns added to the point columns:\n";
  std::cerr << "  status (found | not_found | invalid), street_name, house_number, side, city,\n";
  std::cerr << "  postal_code, distance, measure, street_segment, snapped_x, snapped_y, x, y, geometry\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_reverse_geocode streets.csv points.csv result.csv\n";
  std::cerr << "  spatial_reverse_geocode streets.csv points.csv result.csv -max_dist 20\n";
}

int main(int argc, char* argv[]) {
  if (argc >= 2 && (std::string(argv[1]) == "-help" || std::string(argv[1]) == "-h")) {
=======
// ---------------------------------------------------------------------------
// Geometria
// ---------------------------------------------------------------------------

struct Nearest {
  double dist = std::numeric_limits<double>::max();
  double t = 0;      // fraccion 0..1 de la longitud de la linea
  double cross = 0;  // >0: el punto esta a la izquierda del sentido de digitalizacion
};

static Nearest nearestOnLine(const std::vector<double>& c, double px, double py) {
  Nearest best;
  double total = 0;
  for (size_t i = 0; i + 3 < c.size(); i += 2)
    total += std::hypot(c[i + 2] - c[i], c[i + 3] - c[i + 1]);
  double acc = 0;
  for (size_t i = 0; i + 3 < c.size(); i += 2) {
    double ax = c[i], ay = c[i + 1], bx = c[i + 2], by = c[i + 3];
    double dx = bx - ax, dy = by - ay;
    double len2 = dx * dx + dy * dy;
    double len = std::sqrt(len2);
    double u = len2 > 1e-18 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0;
    u = std::max(0.0, std::min(1.0, u));
    double qx = ax + u * dx, qy = ay + u * dy;
    double d = std::hypot(px - qx, py - qy);
    if (d < best.dist - 1e-12) {
      best.dist = d;
      best.t = total > 1e-12 ? (acc + u * len) / total : 0.0;
      best.cross = dx * (py - ay) - dy * (px - ax);
    }
    acc += len;
  }
  return best;
}

// Numero de casa estimado en un rango, respetando la paridad del lado.
static int interpolateNumber(int from, int to, double t) {
  double n = from + t * (to - from);
  int lo = std::min(from, to), hi = std::max(from, to);
  int r;
  if ((from % 2) == (to % 2)) {
    int p = ((from % 2) + 2) % 2;
    r = (int)std::lround((n - p) / 2.0) * 2 + p;
    if (r < lo) r = lo;
    if (r > hi) r = hi;
  } else {
    r = (int)std::lround(n);
    r = std::max(lo, std::min(hi, r));
  }
  return r;
}

// ---------------------------------------------------------------------------
// Programa
// ---------------------------------------------------------------------------

void printUsage() {
  std::cerr << "spatial_reverse_geocode - Geocodificacion inversa: estima la direccion de uno o mas puntos\n\n";
  std::cerr << "Usage: spatial_reverse_geocode <streets.csv> <points.csv> <output.csv> [options]\n\n";
  std::cerr << "streets.csv   Capa de calles (la misma que usa spatial_address)\n";
  std::cerr << "points.csv    Puntos a consultar: id y longitude,latitude (o x,y, o geometry POINT)\n\n";
  std::cerr << "Para cada punto se busca el segmento de calle mas cercano, se proyecta el punto sobre\n";
  std::cerr << "el y se interpola el numero de casa en el rango del lado en que cae el punto\n";
  std::cerr << "(inverso de spatial_address).\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -max_dist <value>      Distancia maxima punto-calle (default: 50.0)\n";
  std::cerr << "  -addresses <file>      Resultado de spatial_address: agrega la direccion ya\n";
  std::cerr << "                         geocodificada mas cercana (dentro de -max_dist)\n";
  std::cerr << "  -h, -help              Mostrar esta ayuda\n\n";
  std::cerr << "Status: matched (el lado del punto tiene rango), approximate (solo existe el rango del\n";
  std::cerr << "lado opuesto), too_far (ninguna calle dentro de -max_dist).\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_reverse_geocode streets.csv points.csv result.csv\n";
  std::cerr << "  spatial_reverse_geocode streets.csv points.csv result.csv -max_dist 20 -addresses geocoded.csv\n";
}

int main(int argc, char* argv[]) {
  if (argc == 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "-help")) {
>>>>>>> 9612af9c91ae858debce8b24c2c80f22bd6c523e
    printUsage();
    return 0;
  }
  if (argc < 4) {
    printUsage();
    return 1;
  }

  std::string streets_file = argv[1];
  std::string points_file = argv[2];
  std::string output_file = argv[3];
<<<<<<< HEAD
  double max_dist = 50.0;
  bool verbose = false;

  for (int i = 4; i < argc; ++i) {
    std::string arg = argv[i];
    try {
      if (arg == "-max_dist" && i + 1 < argc) {
        max_dist = std::stod(argv[++i]);
        if (max_dist < 0) {
          std::cerr << "Error: -max_dist must not be negative\n";
          return 1;
        }
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
=======
  std::string addresses_file;
  double max_dist = 50.0;

  for (int i = 4; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-max_dist" && i + 1 < argc) {
      try {
        max_dist = std::stod(argv[++i]);
      } catch (...) {
        std::cerr << "Error: invalid value for -max_dist\n";
        return 1;
      }
      if (max_dist < 0) {
        std::cerr << "Error: -max_dist must be >= 0\n";
        return 1;
      }
    } else if (arg == "-addresses" && i + 1 < argc) {
      addresses_file = argv[++i];
    } else if (arg == "-help" || arg == "-h") {
      printUsage();
      return 0;
    } else {
      std::cerr << "Error: Unknown option or missing value: " << arg << "\n\n";
      printUsage();
>>>>>>> 9612af9c91ae858debce8b24c2c80f22bd6c523e
      return 1;
    }
  }

  std::cout << "========================================\n";
  std::cout << "  REVERSE GEOCODING\n";
  std::cout << "========================================\n\n";
  std::cout << "Streets: " << streets_file << "\n";
  std::cout << "Points: " << points_file << "\n";
  std::cout << "Output: " << output_file << "\n";
<<<<<<< HEAD
  std::cout << "Max distance: " << max_dist << "\n\n";

  std::string err;
  std::vector<Segment> segments;
  if (!streets::readStreets(streets_file, segments, err)) {
    std::cerr << "Error: " << err << "\n";
    return 1;
  }
  segments.erase(std::remove_if(segments.begin(), segments.end(),
                                [](const Segment& s) {
                                  return s.vertexCount() < 2 || s.name.empty() ||
                                         (!s.left.valid && !s.right.valid);
                                }),
                 segments.end());
  std::cout << "Street segments loaded: " << segments.size() << "\n";
  if (segments.empty()) {
    std::cerr << "Error: No valid street segments found (check name, ranges and WKT LINESTRING)\n";
    return 1;
  }

  std::vector<PointInput> points;
  std::vector<std::string> extra_names;
  if (!readPoints(points_file, points, extra_names, err)) {
    std::cerr << "Error: " << err << "\n";
    return 1;
  }
  std::cout << "Points loaded: " << points.size() << "\n";
  if (points.empty()) {
    std::cerr << "Error: No points found\n";
    return 1;
=======
  std::cout << "Max distance: " << max_dist << "\n";
  if (!addresses_file.empty()) std::cout << "Known addresses: " << addresses_file << "\n";
  std::cout << "\n";

  auto streets = readStreets(streets_file);
  if (streets.empty()) {
    std::cerr << "Error: No valid street segments found (need street_name, geometry and a range)\n";
    return 1;
  }
  std::cout << "Street segments loaded: " << streets.size() << "\n";

  std::vector<QueryPoint> points;
  if (!readPointFile(points_file, false, points,
                     [](const std::string& id, double x, double y, const std::string&,
                        const std::string&) { return QueryPoint{id, x, y}; }))
    return 1;
  if (points.empty()) {
    std::cerr << "Error: No valid points found in " << points_file << "\n";
    return 1;
  }
  std::cout << "Points loaded: " << points.size() << "\n";

  std::vector<KnownAddress> known;
  if (!addresses_file.empty()) {
    if (!readPointFile(addresses_file, true, known,
                       [](const std::string& id, double x, double y, const std::string& num,
                          const std::string& street) {
                         KnownAddress a;
                         a.id = id;
                         a.x = x;
                         a.y = y;
                         a.house_number = toInt(num);
                         a.street_name = street;
                         return a;
                       }))
      return 1;
    std::cout << "Known addresses loaded: " << known.size() << "\n";
  }

  std::vector<ReverseResult> results;
  int n_matched = 0, n_approx = 0, n_far = 0;

  for (const auto& p : points) {
    ReverseResult r;
    // Segmento mas cercano.
    double best = std::numeric_limits<double>::max();
    Nearest bn;
    int bi = -1;
    for (size_t i = 0; i < streets.size(); ++i) {
      Nearest n = nearestOnLine(streets[i].coords, p.x, p.y);
      if (n.dist < best - 1e-12) {
        best = n.dist;
        bn = n;
        bi = (int)i;
      }
    }

    if (bi >= 0 && best <= max_dist) {
      const auto& s = streets[bi];
      bool geom_left = bn.cross >= 0;  // sobre la linea se asume el lado izquierdo
      bool use_left = geom_left;
      r.status = "matched";
      if (use_left && !s.hasLeft()) {
        use_left = false;
        r.status = "approximate";
      } else if (!use_left && !s.hasRight()) {
        use_left = true;
        r.status = "approximate";
      }
      int from = use_left ? s.left_from : s.right_from;
      int to = use_left ? s.left_to : s.right_to;
      r.segment_index = bi;
      r.side = geom_left ? "left" : "right";
      r.distance = best;
      r.measure = bn.t;
      r.house_number = interpolateNumber(from, to, bn.t);
      (r.status == "matched" ? n_matched : n_approx)++;
    } else {
      r.distance = (bi >= 0) ? best : 0;
      ++n_far;
    }

    if (!known.empty()) {
      double nd = std::numeric_limits<double>::max();
      int ni = -1;
      for (size_t k = 0; k < known.size(); ++k) {
        double d = std::hypot(known[k].x - p.x, known[k].y - p.y);
        if (d < nd) {
          nd = d;
          ni = (int)k;
        }
      }
      if (ni >= 0 && nd <= max_dist) {
        r.near_id = known[ni].id;
        r.near_label = std::to_string(known[ni].house_number) + " " + known[ni].street_name;
        r.near_dist = nd;
      }
    }
    results.push_back(r);
>>>>>>> 9612af9c91ae858debce8b24c2c80f22bd6c523e
  }

  std::ofstream out(output_file);
  if (!out.is_open()) {
    std::cerr << "Error: Could not create output file: " << output_file << "\n";
    return 1;
  }
  out << "# Reverse geocoded points\n# Geometry column: geometry\n";
<<<<<<< HEAD
  out << "id";
  for (const auto& n : extra_names)
    out << "," << csvQuote(n);
  out << ",status,street_name,house_number,side,city,postal_code,distance,measure,street_segment,"
         "snapped_x,snapped_y,x,y,geometry\n";

  int found = 0, not_found = 0, invalid = 0;
  for (const auto& p : points) {
    out << csvQuote(p.id);
    for (const auto& v : p.extras)
      out << "," << csvQuote(v);

    if (!p.ok) {
      invalid++;
      out << ",invalid,,,,,,,,,,,,,POINT EMPTY\n";
      continue;
    }

    const Segment* best = nullptr;
    double best_d = 1e300, best_m = 0, best_cross = 0, best_sx = 0, best_sy = 0;
    for (const auto& seg : segments) {
      double d, m, c, sx, sy;
      seg.nearest(p.x, p.y, d, m, c, sx, sy);
      if (d < best_d) {
        best = &seg;
        best_d = d;
        best_m = m;
        best_cross = c;
        best_sx = sx;
        best_sy = sy;
      }
    }

    if (!best || best_d > max_dist) {
      not_found++;
      out << ",not_found,,,,,,,,,,," << std::fixed << std::setprecision(6) << p.x << "," << p.y
          << ",POINT(" << p.x << " " << p.y << ")\n";
      if (verbose)
        std::cout << "  " << p.id << ": not_found (nearest street at " << std::fixed
                  << std::setprecision(3) << best_d << ")\n";
      continue;
    }

    // Left side when the point is to the left of the digitizing direction.
    bool left = best_cross >= 0;
    if (left && !best->left.valid)
      left = false;
    else if (!left && !best->right.valid)
      left = true;
    const streets::Range& r = left ? best->left : best->right;

    double len = best->length();
    double t = len > 1e-12 ? best_m / len : 0.0;
    int number = interpolateNumber(r, t);

    found++;
    out << ",found," << csvQuote(best->name) << "," << number << "," << (left ? "left" : "right") << ","
        << csvQuote(best->city) << "," << csvQuote(best->postal) << "," << std::fixed
        << std::setprecision(6) << best_d << "," << best_m << "," << best->row << "," << best_sx << ","
        << best_sy << "," << p.x << "," << p.y << ",POINT(" << p.x << " " << p.y << ")\n";
    if (verbose)
      std::cout << "  " << p.id << ": " << number << " " << best->name << " (" << (left ? "left" : "right")
                << ", " << std::fixed << std::setprecision(3) << best_d << " from the street)\n";
=======
  out << "id,longitude,latitude,status,house_number,street_name,city,postal_code,side,distance,"
         "street_segment,measure";
  if (!known.empty()) out << ",nearest_address_id,nearest_address,nearest_address_distance";
  out << ",geometry\n";
  out << std::fixed << std::setprecision(6);
  for (size_t i = 0; i < points.size(); ++i) {
    const auto& p = points[i];
    const auto& r = results[i];
    out << addr::csvEscape(p.id) << "," << p.x << "," << p.y << "," << r.status << ",";
    if (r.segment_index >= 0) {
      const auto& s = streets[r.segment_index];
      std::string sid = s.id.empty() ? std::to_string(r.segment_index + 1) : s.id;
      out << r.house_number << "," << addr::csvEscape(s.street_name) << ","
          << addr::csvEscape(s.city) << "," << addr::csvEscape(s.postal_code) << "," << r.side
          << "," << r.distance << "," << addr::csvEscape(sid) << "," << r.measure;
    } else {
      out << ",,,,," << (r.distance > 0 ? std::to_string(r.distance) : std::string()) << ",,";
    }
    if (!known.empty()) {
      out << "," << addr::csvEscape(r.near_id) << "," << addr::csvEscape(r.near_label) << ","
          << (r.near_dist >= 0 ? std::to_string(r.near_dist) : std::string());
    }
    out << ",POINT(" << p.x << " " << p.y << ")\n";
>>>>>>> 9612af9c91ae858debce8b24c2c80f22bd6c523e
  }

  std::cout << "\n========================================\n";
  std::cout << "  REVERSE GEOCODING COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Points processed: " << points.size() << "\n";
<<<<<<< HEAD
  std::cout << "Found: " << found << "\n";
  std::cout << "Not found (farther than max distance): " << not_found << "\n";
  if (invalid > 0)
    std::cout << "Invalid (no readable coordinates): " << invalid << "\n";
=======
  std::cout << "Matched: " << n_matched << "\n";
  std::cout << "Approximate: " << n_approx << "\n";
  std::cout << "Too far: " << n_far << "\n";
>>>>>>> 9612af9c91ae858debce8b24c2c80f22bd6c523e
  std::cout << "Output written to: " << output_file << "\n";
  return 0;
}
