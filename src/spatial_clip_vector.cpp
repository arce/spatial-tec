#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "../include/core/spatial_csv.hpp"
#include "../include/core/spatial_geom.hpp"
#include "../include/core/spatial_io.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

struct BBox {
  double minx, miny, maxx, maxy;
  BBox() : minx(0), miny(0), maxx(0), maxy(0) {}
  BBox(double x1, double y1, double x2, double y2)
      : minx(std::min(x1, x2)),
        miny(std::min(y1, y2)),
        maxx(std::max(x1, x2)),
        maxy(std::max(y1, y2)) {}

  bool contains(double x, double y) const {
    return x >= minx && x <= maxx && y >= miny && y <= maxy;
  }

  bool contains(const std::vector<double>& coords) const {
    for (size_t i = 0; i < coords.size(); i += 2) {
      if (!contains(coords[i], coords[i + 1]))
        return false;
    }
    return true;
  }
};

bool polygonContainsFeature(const std::vector<double>& polygon,
                            const Spatial::VectorFeature& feature) {
  for (size_t i = 0; i < feature.coordinates.size(); i += 2) {
    if (pointInPolygon(feature.coordinates[i], feature.coordinates[i + 1], polygon)) {
      return true;
    }
  }
  return false;
}

bool anyPolygonContainsFeature(const std::vector<std::vector<double>>& polygons,
                               const Spatial::VectorFeature& feature) {
  for (const auto& polygon : polygons) {
    if (polygonContainsFeature(polygon, feature)) {
      return true;
    }
  }
  return false;
}

std::vector<double> bboxToPolygon(const BBox& bbox) {
  return {bbox.minx, bbox.miny, bbox.maxx, bbox.miny,
          bbox.maxx, bbox.maxy, bbox.minx, bbox.maxy};
}

// Returns the parameter t in [0,1] along segment (ax,ay)-(bx,by) where it
// crosses segment (cx,cy)-(dx,dy), if the two segments actually cross.
bool segmentIntersection(double ax, double ay, double bx, double by, double cx, double cy,
                          double dx, double dy, double& t) {
  double r_x = bx - ax, r_y = by - ay;
  double s_x = dx - cx, s_y = dy - cy;
  double denom = r_x * s_y - r_y * s_x;
  if (std::fabs(denom) < 1e-12) return false;  // parallel/collinear -- ignored
  double t_num = (cx - ax) * s_y - (cy - ay) * s_x;
  double u_num = (cx - ax) * r_y - (cy - ay) * r_x;
  double tt = t_num / denom;
  double uu = u_num / denom;
  if (tt < -1e-9 || tt > 1.0 + 1e-9 || uu < -1e-9 || uu > 1.0 + 1e-9) return false;
  t = std::min(1.0, std::max(0.0, tt));
  return true;
}

bool insideAnyPolygon(double x, double y, const std::vector<std::vector<double>>& polygons) {
  for (const auto& polygon : polygons) {
    if (pointInPolygon(x, y, polygon)) return true;
  }
  return false;
}

// Clips one polyline (flat x,y pairs) against the union of `polygons`, keeping
// the portions that fall inside them (or outside, when `invert` is set).
// A line that crosses the boundary more than once comes back out as several
// disjoint pieces rather than one -- real clipping, not just selection.
std::vector<std::vector<double>> clipPolylineToPolygons(
    const std::vector<double>& line, const std::vector<std::vector<double>>& polygons,
    bool invert) {
  std::vector<std::vector<double>> parts;
  size_t n = line.size() / 2;
  if (n < 2) return parts;

  std::vector<double> current;

  auto appendPoint = [&](double x, double y) {
    if (current.size() >= 2 && std::fabs(current[current.size() - 2] - x) < 1e-9 &&
        std::fabs(current[current.size() - 1] - y) < 1e-9) {
      return;  // skip a duplicate consecutive point
    }
    current.push_back(x);
    current.push_back(y);
  };

  auto flush = [&]() {
    if (current.size() >= 4) parts.push_back(current);
    current.clear();
  };

  for (size_t i = 0; i + 1 < n; ++i) {
    double ax = line[i * 2], ay = line[i * 2 + 1];
    double bx = line[(i + 1) * 2], by = line[(i + 1) * 2 + 1];

    std::vector<double> ts;
    for (const auto& polygon : polygons) {
      size_t m = polygon.size() / 2;
      if (m < 2) continue;
      for (size_t j = 0; j < m; ++j) {
        size_t k = (j + 1) % m;
        double t;
        if (segmentIntersection(ax, ay, bx, by, polygon[j * 2], polygon[j * 2 + 1],
                                 polygon[k * 2], polygon[k * 2 + 1], t)) {
          ts.push_back(t);
        }
      }
    }
    std::sort(ts.begin(), ts.end());

    std::vector<double> cuts;
    cuts.push_back(0.0);
    for (double t : ts) {
      if (t - cuts.back() > 1e-9) cuts.push_back(t);
    }
    if (cuts.back() < 1.0 - 1e-9) cuts.push_back(1.0);

    for (size_t s = 0; s + 1 < cuts.size(); ++s) {
      double t0 = cuts[s], t1 = cuts[s + 1];
      if (t1 - t0 < 1e-9) continue;

      double mx = ax + (t0 + t1) * 0.5 * (bx - ax);
      double my = ay + (t0 + t1) * 0.5 * (by - ay);
      bool keep = insideAnyPolygon(mx, my, polygons);
      if (invert) keep = !keep;

      double x0 = ax + t0 * (bx - ax), y0 = ay + t0 * (by - ay);
      double x1 = ax + t1 * (bx - ax), y1 = ay + t1 * (by - ay);

      if (keep) {
        if (current.empty()) appendPoint(x0, y0);
        appendPoint(x1, y1);
      } else {
        flush();
      }
    }
  }
  flush();
  return parts;
}

// Builds a clipped copy of a LINESTRING/MULTILINESTRING feature. Returns
// false when nothing of the line survives the clip (feature.attributes are
// preserved on the copy so downstream columns still line up).
bool clipLineFeature(const Spatial::VectorFeature& feature,
                      const std::vector<std::vector<double>>& polygons, bool invert,
                      Spatial::VectorFeature& out) {
  std::vector<std::vector<double>> all_parts;
  for (const auto& range : featurePartRanges(feature)) {
    std::vector<double> part(feature.coordinates.begin() + range.first * 2,
                              feature.coordinates.begin() + range.second * 2);
    for (auto& clipped : clipPolylineToPolygons(part, polygons, invert)) {
      all_parts.push_back(std::move(clipped));
    }
  }

  if (all_parts.empty()) return false;

  out = feature;
  out.coordinates.clear();
  out.part_starts.clear();

  if (all_parts.size() == 1) {
    out.type = Spatial::VectorFeature::GeometryType::LINESTRING;
    out.coordinates = std::move(all_parts.front());
  } else {
    out.type = Spatial::VectorFeature::GeometryType::MULTILINESTRING;
    size_t running_pairs = 0;
    for (auto& part : all_parts) {
      out.part_starts.push_back(running_pairs);
      running_pairs += part.size() / 2;
      out.coordinates.insert(out.coordinates.end(), part.begin(), part.end());
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Polygon-against-polygon clipping (Sutherland-Hodgman, triangulated so the
// clip polygon doesn't need to be convex -- real provinces/states usually
// aren't).
// ---------------------------------------------------------------------------

struct ClipPoint {
  double x, y;
};

inline double crossProduct(const ClipPoint& o, const ClipPoint& a, const ClipPoint& b) {
  return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

std::vector<ClipPoint> ringToPoints(const std::vector<double>& ring) {
  std::vector<ClipPoint> pts;
  pts.reserve(ring.size() / 2);
  for (size_t i = 0; i < ring.size(); i += 2) pts.push_back({ring[i], ring[i + 1]});
  // Shapefiles (and some WKT) store polygon rings closed, i.e. the last point
  // repeats the first -- drop that duplicate, everything downstream assumes
  // an open ring (n distinct vertices, wrapping i -> (i+1)%n).
  while (pts.size() > 1 &&
         std::fabs(pts.front().x - pts.back().x) < 1e-9 &&
         std::fabs(pts.front().y - pts.back().y) < 1e-9) {
    pts.pop_back();
  }
  return pts;
}

std::vector<double> pointsToRing(const std::vector<ClipPoint>& pts) {
  std::vector<double> ring;
  ring.reserve(pts.size() * 2);
  for (const auto& p : pts) {
    ring.push_back(p.x);
    ring.push_back(p.y);
  }
  return ring;
}

// Ear-clipping triangulation of a simple polygon (no holes -- consistent with
// this project treating holes as separate MULTIPOLYGON parts elsewhere).
// Returns an empty list for degenerate input instead of looping forever.
std::vector<std::vector<ClipPoint>> triangulatePolygon(std::vector<ClipPoint> poly) {
  std::vector<std::vector<ClipPoint>> triangles;
  if (poly.size() < 3) return triangles;

  double area2 = 0.0;
  for (size_t i = 0; i < poly.size(); ++i) {
    size_t j = (i + 1) % poly.size();
    area2 += poly[i].x * poly[j].y - poly[j].x * poly[i].y;
  }
  if (area2 < 0) std::reverse(poly.begin(), poly.end());  // normalize to CCW

  std::vector<size_t> idx(poly.size());
  for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;

  size_t guard = 0;
  size_t max_guard = idx.size() * idx.size() + 16;

  while (idx.size() > 3 && guard++ < max_guard) {
    size_t n = idx.size();
    bool ear_found = false;

    for (size_t i = 0; i < n; ++i) {
      size_t ip = (i + n - 1) % n;
      size_t in = (i + 1) % n;
      const ClipPoint& a = poly[idx[ip]];
      const ClipPoint& b = poly[idx[i]];
      const ClipPoint& c = poly[idx[in]];

      if (crossProduct(a, b, c) <= 1e-12) continue;  // reflex or degenerate vertex

      bool any_inside = false;
      for (size_t k = 0; k < n && !any_inside; ++k) {
        if (k == ip || k == i || k == in) continue;
        const ClipPoint& p = poly[idx[k]];
        double d1 = crossProduct(a, b, p);
        double d2 = crossProduct(b, c, p);
        double d3 = crossProduct(c, a, p);
        bool has_neg = d1 < 0 || d2 < 0 || d3 < 0;
        bool has_pos = d1 > 0 || d2 > 0 || d3 > 0;
        if (!(has_neg && has_pos)) any_inside = true;
      }
      if (any_inside) continue;

      triangles.push_back(std::vector<ClipPoint>{a, b, c});
      idx.erase(idx.begin() + i);
      ear_found = true;
      break;
    }

    if (!ear_found) break;  // self-intersecting/degenerate ring -- stop gracefully
  }

  if (idx.size() == 3) {
    triangles.push_back(std::vector<ClipPoint>{poly[idx[0]], poly[idx[1]], poly[idx[2]]});
  }
  return triangles;
}

// True when a CCW polygon has no reflex vertices -- lets a whole convex clip
// area (a plain bbox rectangle, or a convex province) be used as a single
// clipper instead of being split into triangles for no reason, so a subject
// polygon clipped against it comes back as one clean ring instead of a
// jumble of adjoining slivers.
bool isConvexCCW(const std::vector<ClipPoint>& poly) {
  size_t n = poly.size();
  if (n < 3) return false;
  for (size_t i = 0; i < n; ++i) {
    const ClipPoint& a = poly[(i + n - 1) % n];
    const ClipPoint& b = poly[i];
    const ClipPoint& c = poly[(i + 1) % n];
    if (crossProduct(a, b, c) < -1e-9) return false;
  }
  return true;
}

// Decomposes one clip polygon ring into one or more convex pieces: the ring
// itself (reoriented CCW) when it is already convex, or its ear-clipping
// triangles otherwise.
std::vector<std::vector<ClipPoint>> decomposeToConvexParts(std::vector<ClipPoint> poly) {
  if (poly.size() < 3) return {};

  double area2 = 0.0;
  for (size_t i = 0; i < poly.size(); ++i) {
    size_t j = (i + 1) % poly.size();
    area2 += poly[i].x * poly[j].y - poly[j].x * poly[i].y;
  }
  if (area2 < 0) std::reverse(poly.begin(), poly.end());

  if (isConvexCCW(poly)) return {poly};
  return triangulatePolygon(poly);
}

ClipPoint intersectLines(const ClipPoint& p1, const ClipPoint& p2, const ClipPoint& a,
                          const ClipPoint& b) {
  double a1 = p2.y - p1.y, b1 = p1.x - p2.x, c1 = a1 * p1.x + b1 * p1.y;
  double a2 = b.y - a.y, b2 = a.x - b.x, c2 = a2 * a.x + b2 * a.y;
  double det = a1 * b2 - a2 * b1;
  if (std::fabs(det) < 1e-12) return p1;  // parallel -- shouldn't happen when called from clipping
  return {(b2 * c1 - b1 * c2) / det, (a1 * c2 - a2 * c1) / det};
}

// One Sutherland-Hodgman step: keeps the part of `subject` on the left of the
// directed edge a->b (edges of a CCW convex clipper -- see triangulatePolygon).
std::vector<ClipPoint> clipAgainstEdge(const std::vector<ClipPoint>& subject, const ClipPoint& a,
                                        const ClipPoint& b) {
  std::vector<ClipPoint> out;
  size_t n = subject.size();
  if (n == 0) return out;

  for (size_t i = 0; i < n; ++i) {
    const ClipPoint& curr = subject[i];
    const ClipPoint& prev = subject[(i + n - 1) % n];
    bool curr_inside = crossProduct(a, b, curr) >= -1e-9;
    bool prev_inside = crossProduct(a, b, prev) >= -1e-9;

    if (curr_inside) {
      if (!prev_inside) out.push_back(intersectLines(prev, curr, a, b));
      out.push_back(curr);
    } else if (prev_inside) {
      out.push_back(intersectLines(prev, curr, a, b));
    }
  }
  return out;
}

std::vector<ClipPoint> clipPolygonByConvex(std::vector<ClipPoint> subject,
                                            const std::vector<ClipPoint>& clip_poly) {
  size_t m = clip_poly.size();
  for (size_t i = 0; i < m && !subject.empty(); ++i) {
    subject = clipAgainstEdge(subject, clip_poly[i], clip_poly[(i + 1) % m]);
  }
  return subject;
}

// Clips one polygon ring against the triangulated clip area, returning zero or
// more resulting rings (a concave clip area, or a subject that straddles
// several of its triangles, naturally produces more than one).
std::vector<std::vector<double>> clipPolygonRing(
    const std::vector<double>& ring, const std::vector<std::vector<ClipPoint>>& clip_convex_parts) {
  std::vector<std::vector<double>> parts;
  std::vector<ClipPoint> subject = ringToPoints(ring);
  if (subject.size() < 3) return parts;

  for (const auto& convex_part : clip_convex_parts) {
    auto clipped = clipPolygonByConvex(subject, convex_part);
    if (clipped.size() < 3) continue;
    auto out_ring = pointsToRing(clipped);
    if (polygonArea(out_ring) > 1e-9) parts.push_back(std::move(out_ring));
  }
  return parts;
}

// Builds a clipped copy of a POLYGON/MULTIPOLYGON feature. Returns false when
// nothing survives. `partial_overlap` is set when -invert was requested on a
// feature that only partly overlaps the clip area: this tool has no polygon
// subtraction (no true holes in the data model, only extra MULTIPOLYGON
// parts), so that case is approximated by keeping the polygon whole rather
// than silently producing a wrong shape.
bool clipPolygonFeature(const Spatial::VectorFeature& feature,
                         const std::vector<std::vector<ClipPoint>>& clip_convex_parts, bool invert,
                         Spatial::VectorFeature& out, bool& partial_overlap) {
  partial_overlap = false;

  std::vector<std::vector<double>> all_parts;
  double original_area = 0.0;
  for (const auto& range : featurePartRanges(feature)) {
    std::vector<double> part(feature.coordinates.begin() + range.first * 2,
                              feature.coordinates.begin() + range.second * 2);
    original_area += polygonArea(part);
    for (auto& clipped : clipPolygonRing(part, clip_convex_parts)) {
      all_parts.push_back(std::move(clipped));
    }
  }

  auto assemble = [&](std::vector<std::vector<double>>& parts) {
    out = feature;
    out.coordinates.clear();
    out.part_starts.clear();
    if (parts.size() == 1) {
      out.type = Spatial::VectorFeature::GeometryType::POLYGON;
      out.coordinates = std::move(parts.front());
    } else {
      out.type = Spatial::VectorFeature::GeometryType::MULTIPOLYGON;
      size_t running = 0;
      for (auto& part : parts) {
        out.part_starts.push_back(running);
        running += part.size() / 2;
        out.coordinates.insert(out.coordinates.end(), part.begin(), part.end());
      }
    }
  };

  if (!invert) {
    if (all_parts.empty()) return false;
    assemble(all_parts);
    return true;
  }

  double clipped_area = 0.0;
  for (const auto& part : all_parts) clipped_area += polygonArea(part);

  if (clipped_area <= 1e-9) {
    // Entirely outside the clip area -- keep the whole feature.
    out = feature;
    return true;
  }
  if (original_area > 1e-9 && clipped_area >= original_area * (1.0 - 1e-6)) {
    // Entirely inside the clip area -- nothing remains outside.
    return false;
  }
  // Partial overlap: fall back to keeping the polygon unchanged (see comment
  // above) instead of attempting an unsupported boolean subtraction.
  partial_overlap = true;
  out = feature;
  return true;
}

std::vector<std::vector<double>> readPolygonsFromCSV(const std::string& filename) {
  std::vector<std::vector<double>> polygons;

  Spatial::SpatialCSVReader reader;
  Spatial::VectorDataset dataset;
  if (!reader.read(filename, dataset)) {
    return polygons;
  }

  for (const auto& feature : dataset.features) {
    if (feature.type != Spatial::VectorFeature::GeometryType::POLYGON &&
        feature.type != Spatial::VectorFeature::GeometryType::MULTIPOLYGON) {
      continue;
    }
    for (const auto& range : featurePartRanges(feature)) {
      std::vector<double> ring;
      ring.reserve((range.second - range.first) * 2);
      for (size_t p = range.first; p < range.second; ++p) {
        ring.push_back(feature.coordinates[p * 2]);
        ring.push_back(feature.coordinates[p * 2 + 1]);
      }
      if (!ring.empty()) {
        polygons.push_back(std::move(ring));
      }
    }
  }

  return polygons;
}

void printUsage() {
  std::cerr << "spatial_clip_vector - Clip vector data by spatial extent\n\n";
  std::cerr << "Usage: spatial_clip_vector <input> <output> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -bbox <minx> <miny> <maxx> <maxy>   Clip by bounding box\n";
  std::cerr << "  -polygon <file>                     Clip by polygon(s) from CSV -- every\n";
  std::cerr << "                                       POLYGON/MULTIPOLYGON row is tested\n";
  std::cerr << "                                       independently, a feature matching any\n";
  std::cerr << "                                       one of them is kept\n";
  std::cerr << "  -clip_to <file>                     Clip to extent of another file\n";
  std::cerr << "  -invert                            Invert clipping\n\n";
  std::cerr << "Notes:\n";
  std::cerr << "  LINESTRING/MULTILINESTRING and POLYGON/MULTIPOLYGON features are actually\n";
  std::cerr << "  cut at the clip boundary, not just selected: only the portion inside the\n";
  std::cerr << "  area (or outside, with -invert) is kept. A shape that crosses the boundary\n";
  std::cerr << "  more than once comes out as a MULTILINESTRING/MULTIPOLYGON with one part\n";
  std::cerr << "  per surviving piece. Clipping against a convex area (a bbox, or a convex\n";
  std::cerr << "  polygon) keeps a single surviving piece as one clean ring; a concave clip\n";
  std::cerr << "  polygon is triangulated internally, so a piece that spans more than one of\n";
  std::cerr << "  those triangles can come back as several small adjoining polygons instead\n";
  std::cerr << "  of one -- the covered area is still exactly right, just split up. POINT/\n";
  std::cerr << "  MULTIPOINT features can only be selected, not cut, so they are kept as-is\n";
  std::cerr << "  when they match (like spatial_filter_vector).\n";
  std::cerr << "  As elsewhere in this project, a polygon with a real hole is expected as an\n";
  std::cerr << "  extra MULTIPOLYGON part rather than an inner ring, so the clip area is the\n";
  std::cerr << "  union of all given polygon parts, not a hole-aware area. This also means\n";
  std::cerr << "  -invert on a polygon that only partly overlaps the clip area cannot cut a\n";
  std::cerr << "  hole into it (no boolean subtraction) -- it is kept whole and reported.\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_clip_vector cities.csv area.csv -bbox -84.5 9.5 -84.0 10.0\n";
  std::cerr << "  spatial_clip_vector rivers.csv rivers_clipped.csv -polygon province.csv\n";
  std::cerr << "  spatial_clip_vector cities.csv zone.csv -clip_to elevation.asc\n";
}

int main(int argc, char* argv[]) {
  if (argc < 4) {
    printUsage();
    return 1;
  }

  std::string input_file = argv[1];
  std::string output_file = argv[2];

  BBox bbox;
  std::string polygon_file;
  std::string reference_file;
  bool use_bbox = false;
  bool use_polygon = false;
  bool use_clip_to = false;
  bool invert = false;

  for (int i = 3; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-bbox" && i + 4 < argc) {
      bbox = BBox(std::stod(argv[i + 1]), std::stod(argv[i + 2]), std::stod(argv[i + 3]),
                  std::stod(argv[i + 4]));
      use_bbox = true;
      i += 4;
    } else if (arg == "-polygon" && i + 1 < argc) {
      polygon_file = argv[i + 1];
      use_polygon = true;
      i += 1;
    } else if (arg == "-clip_to" && i + 1 < argc) {
      reference_file = argv[i + 1];
      use_clip_to = true;
      i += 1;
    } else if (arg == "-invert") {
      invert = true;
    } else {
      std::cerr << "Error: Unknown option: " << arg << "\n";
      printUsage();
      return 1;
    }
  }

  Spatial::SpatialCSVReader reader;
  Spatial::VectorDataset dataset;

  if (!reader.read(input_file, dataset)) {
    std::cerr << "Error: Could not read input file\n";
    return 1;
  }

  std::cout << "Input features: " << dataset.features.size() << "\n";

  std::vector<std::vector<double>> polygons;

  if (use_polygon) {
    polygons = readPolygonsFromCSV(polygon_file);
    if (polygons.empty()) {
      std::cerr << "Error: Could not read any POLYGON/MULTIPOLYGON feature from " << polygon_file
                << "\n";
      return 1;
    }
    size_t total_vertices = 0;
    for (const auto& p : polygons) total_vertices += p.size() / 2;
    std::cout << "Polygons: " << polygons.size() << " (" << total_vertices << " vertices total)\n";
  } else if (use_clip_to) {
    std::cerr << "Error: -clip_to not yet implemented\n";
    return 1;
  }

  // Lines and polygons are both clipped for real against this same polygon
  // set -- for -bbox that set is just the bounding box turned into a
  // rectangle ring.
  std::vector<std::vector<double>> line_clip_polygons = polygons;
  if (use_bbox) {
    line_clip_polygons.push_back(bboxToPolygon(bbox));
  }

  std::vector<std::vector<ClipPoint>> clip_convex_parts;
  for (const auto& polygon : line_clip_polygons) {
    for (auto& part : decomposeToConvexParts(ringToPoints(polygon))) {
      clip_convex_parts.push_back(std::move(part));
    }
  }

  Spatial::VectorDataset output;
  output.columns = dataset.columns;
  output.geometry_column = dataset.geometry_column;
  output.crs = dataset.crs;

  size_t lines_cut = 0;
  size_t lines_split = 0;
  size_t polygons_cut = 0;
  size_t polygons_split = 0;
  size_t polygons_not_subtracted = 0;

  for (const auto& feature : dataset.features) {
    bool is_line = feature.type == Spatial::VectorFeature::GeometryType::LINESTRING ||
                   feature.type == Spatial::VectorFeature::GeometryType::MULTILINESTRING;
    bool is_polygon = feature.type == Spatial::VectorFeature::GeometryType::POLYGON ||
                      feature.type == Spatial::VectorFeature::GeometryType::MULTIPOLYGON;

    if (is_line && (use_polygon || use_bbox)) {
      Spatial::VectorFeature clipped;
      if (clipLineFeature(feature, line_clip_polygons, invert, clipped)) {
        if (clipped.type == Spatial::VectorFeature::GeometryType::MULTILINESTRING) {
          ++lines_split;
        }
        ++lines_cut;
        output.features.push_back(std::move(clipped));
      }
      continue;
    }

    if (is_polygon && (use_polygon || use_bbox)) {
      Spatial::VectorFeature clipped;
      bool partial_overlap = false;
      if (clipPolygonFeature(feature, clip_convex_parts, invert, clipped, partial_overlap)) {
        if (clipped.type == Spatial::VectorFeature::GeometryType::MULTIPOLYGON) {
          ++polygons_split;
        }
        if (partial_overlap) ++polygons_not_subtracted;
        ++polygons_cut;
        output.features.push_back(std::move(clipped));
      }
      continue;
    }

    bool inside;

    if (use_polygon) {
      inside = anyPolygonContainsFeature(polygons, feature);
    } else if (use_bbox) {
      inside = bbox.contains(feature.coordinates);
    } else {
      std::cerr << "Error: No clipping method specified\n";
      return 1;
    }

    if (invert ? !inside : inside) {
      output.features.push_back(feature);
    }
  }

  writeVectorCSV(output, output_file, {}, {"# Clipped from original dataset"});

  std::cout << "Clipped features: " << output.features.size() << "\n";
  if (lines_cut > 0) {
    std::cout << "Lines actually cut at the boundary: " << lines_cut;
    if (lines_split > 0) std::cout << " (" << lines_split << " split into multiple pieces)";
    std::cout << "\n";
  }
  if (polygons_cut > 0) {
    std::cout << "Polygons actually cut at the boundary: " << polygons_cut;
    if (polygons_split > 0) std::cout << " (" << polygons_split << " split into multiple pieces)";
    std::cout << "\n";
  }
  if (polygons_not_subtracted > 0) {
    std::cout << "Warning: " << polygons_not_subtracted
               << " polygon(s) only partly overlapped the clip area under -invert and were\n"
               << "kept whole -- this tool cannot cut a hole into a polygon (no boolean\n"
               << "subtraction / no true holes in the data model).\n";
  }
  std::cout << "Clipping complete.\n";

  return 0;
}

