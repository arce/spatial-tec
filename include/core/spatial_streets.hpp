#pragma once
// Shared helpers for the street-based commands: spatial_address,
// spatial_reverse_geocode and spatial_address_validate.
//
// A street file is a spatial CSV with one segment per row:
//   street_name, left_from, left_to, right_from, right_to, geometry
//   (optional: left_parity, right_parity, city, postal_code)
// Left/right are relative to the digitizing direction of the LINESTRING.

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "spatial_string.hpp"

namespace streets {

struct Range {
  bool valid = false;
  int from = 0;
  int to = 0;
  std::string parity = "both";  // even | odd | both

  int lo() const { return std::min(from, to); }
  int hi() const { return std::max(from, to); }

  bool contains(int n) const {
    if (!valid || n < lo() || n > hi())
      return false;
    if (parity == "even")
      return n % 2 == 0;
    if (parity == "odd")
      return n % 2 != 0;
    return true;
  }
};

struct Segment {
  int row = 0;  // 1-based data row in the street file
  std::string name, city, postal;
  std::vector<double> xy;  // x0 y0 x1 y1 ...
  Range left, right;
  bool explicit_left_parity = false, explicit_right_parity = false;

  size_t vertexCount() const { return xy.size() / 2; }

  double length() const {
    double total = 0;
    for (size_t i = 0; i + 3 < xy.size(); i += 2)
      total += std::hypot(xy[i + 2] - xy[i], xy[i + 3] - xy[i + 1]);
    return total;
  }

  // Point and unit tangent at distance d from the start of the line.
  void pointAt(double d, double& px, double& py, double& tx, double& ty) const {
    double acc = 0;
    size_t n = xy.size();
    for (size_t i = 0; i + 3 < n; i += 2) {
      double dx = xy[i + 2] - xy[i], dy = xy[i + 3] - xy[i + 1];
      double len = std::hypot(dx, dy);
      bool last = (i + 4 >= n);
      if (len > 1e-12 && (acc + len >= d || last)) {
        double u = std::max(0.0, std::min(1.0, (d - acc) / len));
        px = xy[i] + u * dx;
        py = xy[i + 1] + u * dy;
        tx = dx / len;
        ty = dy / len;
        return;
      }
      acc += len;
    }
    px = xy[0];
    py = xy[1];
    tx = 1;
    ty = 0;
  }

  // Nearest point of the line to (x, y): distance, measure along the line
  // and cross product sign (>0: point lies to the left of the line).
  void nearest(double x, double y, double& dist, double& measure, double& cross, double& sx,
               double& sy) const {
    dist = 1e300;
    measure = 0;
    cross = 0;
    sx = xy[0];
    sy = xy[1];
    double acc = 0;
    for (size_t i = 0; i + 3 < xy.size(); i += 2) {
      double ax = xy[i], ay = xy[i + 1], bx = xy[i + 2], by = xy[i + 3];
      double dx = bx - ax, dy = by - ay;
      double len2 = dx * dx + dy * dy, len = std::sqrt(len2);
      double u = len2 > 1e-24 ? ((x - ax) * dx + (y - ay) * dy) / len2 : 0;
      u = std::max(0.0, std::min(1.0, u));
      double qx = ax + u * dx, qy = ay + u * dy;
      double d = std::hypot(x - qx, y - qy);
      if (d < dist) {
        dist = d;
        measure = acc + u * len;
        cross = dx * (y - ay) - dy * (x - ax);
        sx = qx;
        sy = qy;
      }
      acc += len;
    }
  }
};

inline std::string normalizeName(const std::string& s) {
  std::string out;
  bool space = false;
  for (char c : toLower(trim(s))) {
    if (c == ' ' || c == '\t') {
      space = true;
    } else {
      if (space && !out.empty())
        out += ' ';
      space = false;
      out += c;
    }
  }
  return out;
}

// 1 - (Levenshtein distance / length of the longer string), in [0, 1].
inline double nameSimilarity(const std::string& a, const std::string& b) {
  if (a == b)
    return 1.0;
  if (a.empty() || b.empty())
    return 0.0;
  std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
  for (size_t j = 0; j <= b.size(); ++j)
    prev[j] = (int)j;
  for (size_t i = 1; i <= a.size(); ++i) {
    cur[0] = (int)i;
    for (size_t j = 1; j <= b.size(); ++j) {
      int cost = a[i - 1] == b[j - 1] ? 0 : 1;
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
    }
    std::swap(prev, cur);
  }
  return 1.0 - (double)prev[b.size()] / (double)std::max(a.size(), b.size());
}

inline std::string csvQuote(const std::string& s) {
  if (s.find_first_of(",\"\n") == std::string::npos)
    return s;
  std::string out = "\"";
  for (char c : s) {
    if (c == '"')
      out += '"';
    out += c;
  }
  return out + "\"";
}

inline bool parseInt(const std::string& s, int& out) {
  std::string t = trim(s);
  if (t.empty())
    return false;
  try {
    size_t used = 0;
    double v = std::stod(t, &used);
    if (used != t.size() || v != std::floor(v))
      return false;
    out = (int)v;
    return true;
  } catch (...) {
    return false;
  }
}

// Coordinates of any WKT geometry as a flat x y x y ... list.
inline std::vector<double> parseWKTCoords(const std::string& wkt) {
  std::vector<double> xy;
  size_t a = wkt.find('(');
  size_t b = wkt.rfind(')');
  if (a == std::string::npos || b == std::string::npos || b <= a)
    return xy;
  std::string inner = wkt.substr(a + 1, b - a - 1);
  inner.erase(std::remove(inner.begin(), inner.end(), '('), inner.end());
  inner.erase(std::remove(inner.begin(), inner.end(), ')'), inner.end());
  for (const auto& pt : split(inner, ',')) {
    auto c = split(trim(pt), ' ');
    if (c.size() >= 2) {
      try {
        double x = std::stod(c[0]), y = std::stod(c[1]);
        xy.push_back(x);
        xy.push_back(y);
      } catch (...) {
      }
    }
  }
  return xy;
}

inline std::string inferParity(int from, int to) {
  bool fe = from % 2 == 0, te = to % 2 == 0;
  if (fe && te)
    return "even";
  if (!fe && !te)
    return "odd";
  return "both";
}

// Reads a street file. Returns false (with err set) on a fatal problem.
// Rows with unreadable geometry are kept (with fewer than 2 vertices) so that
// spatial_address_validate can report them; callers that only geocode should
// skip segments whose vertexCount() < 2.
inline bool readStreets(const std::string& filename, std::vector<Segment>& out, std::string& err) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    err = "Could not open file: " + filename;
    return false;
  }

  std::string line;
  bool is_header = true;
  int col_name = -1, col_lf = -1, col_lt = -1, col_rf = -1, col_rt = -1;
  int col_city = -1, col_postal = -1, col_geom = -1, col_lpar = -1, col_rpar = -1;
  int row = 0;

  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#')
      continue;
    auto values = splitCSV(line);

    if (is_header) {
      for (size_t i = 0; i < values.size(); ++i) {
        std::string h = toLower(trim(values[i]));
        if (h == "street_name" || h == "street" || h == "name")
          col_name = (int)i;
        else if (h == "left_from" || h == "l_from")
          col_lf = (int)i;
        else if (h == "left_to" || h == "l_to")
          col_lt = (int)i;
        else if (h == "right_from" || h == "r_from")
          col_rf = (int)i;
        else if (h == "right_to" || h == "r_to")
          col_rt = (int)i;
        else if (h == "left_parity")
          col_lpar = (int)i;
        else if (h == "right_parity")
          col_rpar = (int)i;
        else if (h == "city")
          col_city = (int)i;
        else if (h == "postal_code" || h == "zip" || h == "codigo_postal")
          col_postal = (int)i;
        else if (h == "geometry" || h == "geom" || h == "wkt")
          col_geom = (int)i;
      }
      if (col_name < 0 || col_geom < 0) {
        err = "Missing required columns (street_name, geometry)";
        return false;
      }
      if ((col_lf < 0 || col_lt < 0) && (col_rf < 0 || col_rt < 0)) {
        err = "Missing range columns (left_from/left_to or right_from/right_to)";
        return false;
      }
      is_header = false;
      continue;
    }

    row++;
    auto get = [&](int c) -> std::string {
      return (c >= 0 && c < (int)values.size()) ? trim(values[c]) : "";
    };

    Segment seg;
    seg.row = row;
    seg.name = get(col_name);
    seg.city = get(col_city);
    seg.postal = get(col_postal);
    seg.xy = parseWKTCoords(get(col_geom));

    auto makeRange = [&](Range& r, bool& has_explicit, int cf, int ct, int cp) {
      int f = 0, t = 0;
      bool okf = parseInt(get(cf), f), okt = parseInt(get(ct), t);
      if (!okf)
        f = 0;
      if (!okt)
        t = 0;
      r.from = f;
      r.to = t;
      r.valid = (f != 0 || t != 0);
      r.parity = inferParity(f, t);
      std::string p = toLower(get(cp));
      if (p == "even" || p == "odd" || p == "both") {
        r.parity = p;
        has_explicit = true;
      }
    };
    makeRange(seg.left, seg.explicit_left_parity, col_lf, col_lt, col_lpar);
    makeRange(seg.right, seg.explicit_right_parity, col_rf, col_rt, col_rpar);

    out.push_back(seg);
  }

  if (is_header) {
    err = "Empty street file: " + filename;
    return false;
  }
  return true;
}

}  // namespace streets
