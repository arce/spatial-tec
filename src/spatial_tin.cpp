#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "../include/core/spatial_delaunay.hpp"
#include "../include/core/spatial_io.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

namespace {

const double PI = 3.14159265358979323846;

void printUsage() {
  std::cerr << "spatial_tin - Triangulated irregular network (TIN) from elevation points\n\n";
  std::cerr << "Usage: spatial_tin <input.csv> <output.csv> -attribute <col> -operation <build|topology|terrain|volume|elevation> [options]\n\n";
  std::cerr << "The input is a point layer (spatial CSV with a POINT geometry, or a CSV with x/y columns) and\n";
  std::cerr << "-attribute is the column with the elevation. The points are triangulated with the Delaunay\n";
  std::cerr << "criterion; the TIN covers the convex hull of the points. Points with the same x, y are merged\n";
  std::cerr << "(mean elevation).\n\n";
  std::cerr << "Operations:\n";
  std::cerr << "  -operation build      Triangles as polygons: id, node ids, node elevations, centroid and its\n";
  std::cerr << "                        elevation (linear interpolation = mean of the three), 2D area\n";
  std::cerr << "  -operation topology   Table of triangles: nodes and neighboring triangles (0 = none, the edge is\n";
  std::cerr << "                        on the border). Plain CSV, no geometry. Use -nodes for the node table\n";
  std::cerr << "  -operation terrain    Triangles with 2D and 3D area, slope (degrees and percent) and aspect\n";
  std::cerr << "                        (direction the face looks at, degrees clockwise from north; -1 if flat)\n";
  std::cerr << "  -operation volume     Cut and fill against a horizontal plane (-level): per triangle and in total\n";
  std::cerr << "  -operation elevation  Elevation at the given points (-point x y, repeatable): the triangle that\n";
  std::cerr << "                        contains each point, the plane z = a*x + b*y + c of that triangle, and z\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -attribute <col>   Column with the elevation (required)\n";
  std::cerr << "  -level <z>         Elevation of the reference plane (volume; required)\n";
  std::cerr << "  -point <x> <y>     Point where the elevation is computed (elevation; required, can be repeated)\n";
  std::cerr << "  -nodes <file>      Also write the nodes: id, elevation, degree (number of edges), on_hull, and\n";
  std::cerr << "                     the adjacent nodes (spatial CSV with POINT geometry)\n";
  std::cerr << "  -verbose           Show detailed information\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_tin points.csv tin.csv -attribute elevation -operation build\n";
  std::cerr << "  spatial_tin points.csv topo.csv -attribute elevation -operation topology -nodes nodes.csv\n";
  std::cerr << "  spatial_tin points.csv faces.csv -attribute elevation -operation terrain\n";
  std::cerr << "  spatial_tin points.csv cutfill.csv -attribute elevation -operation volume -level 1250\n";
}

struct Node {
  double x = 0, y = 0, z = 0;
};

// Reads the points of a spatial CSV (POINT / POINT Z in WKT) or of a CSV with x, y columns.
bool readPoints(const std::string& filename, const std::string& attr, std::vector<Node>& nodes,
                std::string& crs, size_t& rows, size_t& merged, std::string& error) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    error = "Could not open file: " + filename;
    return false;
  }
  std::string line;
  bool header = true;
  int cx = -1, cy = -1, cg = -1, ca = -1;
  std::map<std::pair<double, double>, size_t> seen;
  std::vector<double> zsum;
  std::vector<int> zcount;
  rows = 0;
  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') {
      if (line.find("CRS:") != std::string::npos)
        crs = trim(line.substr(line.find(":") + 1));
      continue;
    }
    std::vector<std::string> v = splitCSV(line);
    if (header) {
      for (size_t i = 0; i < v.size(); ++i) {
        std::string l = toLower(v[i]);
        if (l == toLower(attr)) ca = static_cast<int>(i);
        else if (l == "x" || l == "longitude" || l == "lon") cx = static_cast<int>(i);
        else if (l == "y" || l == "latitude" || l == "lat") cy = static_cast<int>(i);
        else if (l == "geometry" || l == "geom" || l == "wkt") cg = static_cast<int>(i);
      }
      header = false;
      continue;
    }
    double x = 0, y = 0, z = 0;
    bool has_z = false;
    try {
      if (cg >= 0 && cg < static_cast<int>(v.size())) {
        std::string g = trim(v[cg]);
        size_t a = g.find('('), b = g.rfind(')');
        if (a == std::string::npos || b == std::string::npos || b <= a) continue;
        if (g.compare(0, 5, "POINT") != 0 && g.compare(0, 5, "point") != 0) continue;
        std::vector<std::string> parts = split(trim(g.substr(a + 1, b - a - 1)), ' ');
        std::vector<std::string> nz;
        for (auto& p : parts) if (!trim(p).empty()) nz.push_back(trim(p));
        if (nz.size() < 2) continue;
        x = std::stod(nz[0]);
        y = std::stod(nz[1]);
        if (nz.size() >= 3) { z = std::stod(nz[2]); has_z = true; }
      } else if (cx >= 0 && cy >= 0 && cx < static_cast<int>(v.size()) && cy < static_cast<int>(v.size())) {
        x = std::stod(v[cx]);
        y = std::stod(v[cy]);
      } else {
        continue;
      }
      if (ca >= 0 && ca < static_cast<int>(v.size())) {
        z = std::stod(v[ca]);
        has_z = true;
      }
    } catch (...) {
      continue;
    }
    if (!has_z) {
      if (ca < 0) {  // no such column (and the geometry carries no Z)
        error = "Attribute column not found: " + attr;
        return false;
      }
      continue;
    }
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
      continue;
    ++rows;
    auto key = std::make_pair(x, y);
    auto it = seen.find(key);
    if (it == seen.end()) {
      seen[key] = nodes.size();
      Node n;
      n.x = x; n.y = y;
      nodes.push_back(n);
      zsum.push_back(z);
      zcount.push_back(1);
    } else {
      zsum[it->second] += z;
      zcount[it->second] += 1;
      ++merged;
    }
  }
  for (size_t i = 0; i < nodes.size(); ++i)
    nodes[i].z = zsum[i] / zcount[i];
  if (nodes.empty()) {
    error = "No valid points found (is -attribute \"" + attr + "\" the elevation column?)";
    return false;
  }
  return true;
}

std::string num(double v, int prec = 6) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.*f", prec, v);
  return buf;
}
std::string numg(double v) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.10g", v);
  return buf;
}

struct Tri {
  int n[3];     // node indices (0-based), counter-clockwise, the smallest node first
  int nb[3];    // neighbor triangle across the edge opposite n[i]; -1 on the border
};

struct Plane {
  double area2d = 0, gx = 0, gy = 0;
};

Plane planeOf(const std::vector<Node>& N, const Tri& t) {
  const Node &a = N[t.n[0]], &b = N[t.n[1]], &c = N[t.n[2]];
  double a2 = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);  // twice the signed area
  Plane p;
  p.area2d = 0.5 * std::fabs(a2);
  if (a2 != 0) {
    p.gx = ((b.z - a.z) * (c.y - a.y) - (c.z - a.z) * (b.y - a.y)) / a2;
    p.gy = ((b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z)) / a2;
  }
  return p;
}

// Integral of max(h, 0) over a triangle whose height over the reference plane is linear and
// takes the values h0, h1, h2 at its vertices; `area` is the triangle area.
double positivePart(double area, double h0, double h1, double h2) {
  double h[3] = {h0, h1, h2};
  std::sort(h, h + 3);
  double a = h[0], b = h[1], c = h[2];
  if (a >= 0) return area * (a + b + c) / 3.0;
  if (c <= 0) return 0.0;
  if (b <= 0) {  // only the highest vertex is above the plane: a small triangle around it
    double f = (c / (c - a)) * (c / (c - b));
    return f * area * c / 3.0;
  }
  // two vertices above: total signed volume minus the (negative) part around the lowest one
  double f = (-a / (b - a)) * (-a / (c - a));
  return area * (a + b + c) / 3.0 + f * area * (-a) / 3.0;
}

Spatial::VectorFeature polygonOf(const std::vector<Node>& N, const Tri& t) {
  Spatial::VectorFeature f;
  f.type = Spatial::VectorFeature::GeometryType::POLYGON;
  for (int k = 0; k < 4; ++k) {
    const Node& p = N[t.n[k % 3]];
    f.coordinates.push_back(p.x);
    f.coordinates.push_back(p.y);
  }
  return f;
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc >= 2 && (std::string(argv[1]) == "-help" || std::string(argv[1]) == "-h")) {
    printUsage();
    return 0;
  }
  if (argc < 5) {
    printUsage();
    return 1;
  }

  std::string input_file = argv[1], output_file = argv[2];
  std::string attr, operation, nodes_file;
  double level = 0;
  bool has_level = false, verbose = false;
  std::vector<std::pair<double, double>> query;
  try {
    for (int i = 3; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "-attribute" && i + 1 < argc) attr = argv[++i];
      else if (arg == "-operation" && i + 1 < argc) operation = argv[++i];
      else if (arg == "-level" && i + 1 < argc) { level = std::stod(argv[++i]); has_level = true; }
      else if (arg == "-point" && i + 2 < argc) {
        double qx = std::stod(argv[++i]);
        double qy = std::stod(argv[++i]);
        query.emplace_back(qx, qy);
      }
      else if (arg == "-nodes" && i + 1 < argc) nodes_file = argv[++i];
      else if (arg == "-verbose") verbose = true;
      else if (arg == "-help" || arg == "-h") { printUsage(); return 0; }
      else {
        std::cerr << "Error: Unknown option or missing value: " << arg << "\n";
        printUsage();
        return 1;
      }
    }
  } catch (const std::exception&) {
    std::cerr << "Error: Invalid numeric value in options\n";
    return 1;
  }
  if (operation != "build" && operation != "topology" && operation != "terrain" && operation != "volume" &&
      operation != "elevation") {
    std::cerr << "Error: -operation is required and must be one of: build, topology, terrain, volume, elevation\n";
    printUsage();
    return 1;
  }
  if (attr.empty()) {
    std::cerr << "Error: -attribute <col> (the elevation column) is required\n";
    return 1;
  }
  if (operation == "elevation" && query.empty()) {
    std::cerr << "Error: -point <x> <y> is required for -operation elevation\n";
    return 1;
  }
  if (operation == "volume" && (!has_level || !std::isfinite(level))) {
    std::cerr << "Error: -level <z> is required for -operation volume\n";
    return 1;
  }

  std::vector<Node> nodes;
  std::string crs, error;
  size_t rows = 0, merged = 0;
  if (!readPoints(input_file, attr, nodes, crs, rows, merged, error)) {
    std::cerr << "Error: " << error << "\n";
    return 1;
  }
  {
    std::ofstream probe(output_file);
    if (!probe.is_open()) {
      std::cerr << "Error: Could not create output file: " << output_file << "\n";
      return 1;
    }
  }

  std::vector<double> xs(nodes.size()), ys(nodes.size());
  for (size_t i = 0; i < nodes.size(); ++i) { xs[i] = nodes[i].x; ys[i] = nodes[i].y; }
  Spatial::Delaunay dt;
  if (!dt.build(xs, ys)) {
    std::cerr << "Error: A TIN needs at least 3 distinct points that are not all on one line\n";
    return 1;
  }

  // Triangles in a stable order: each one starts at its smallest node, sorted by nodes.
  std::vector<std::array<int, 3>> verts, nbrs;
  dt.triangulation(verts, nbrs);
  const size_t T = verts.size();
  std::vector<Tri> tris(T);
  for (size_t k = 0; k < T; ++k) {
    int s = 0;
    for (int i = 1; i < 3; ++i)
      if (verts[k][i] < verts[k][s]) s = i;
    for (int i = 0; i < 3; ++i) {
      tris[k].n[i] = verts[k][(s + i) % 3];
      tris[k].nb[i] = nbrs[k][(s + i) % 3];
    }
  }
  std::vector<size_t> order(T);
  for (size_t k = 0; k < T; ++k) order[k] = k;
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    for (int i = 0; i < 3; ++i)
      if (tris[a].n[i] != tris[b].n[i]) return tris[a].n[i] < tris[b].n[i];
    return false;
  });
  std::vector<int> newIndex(T);
  for (size_t k = 0; k < T; ++k) newIndex[order[k]] = static_cast<int>(k);
  std::vector<Tri> sorted(T);
  for (size_t k = 0; k < T; ++k) {
    sorted[k] = tris[order[k]];
    for (int i = 0; i < 3; ++i)
      if (sorted[k].nb[i] >= 0) sorted[k].nb[i] = newIndex[sorted[k].nb[i]];
  }
  tris.swap(sorted);

  // Nodes: degree, adjacent nodes, border flag.
  std::vector<std::set<int>> adjacent(nodes.size());
  std::vector<char> on_hull(nodes.size(), 0);
  size_t border_edges = 0;
  for (const Tri& t : tris)
    for (int i = 0; i < 3; ++i) {
      int a = t.n[(i + 1) % 3], b = t.n[(i + 2) % 3];
      adjacent[a].insert(b);
      adjacent[b].insert(a);
      if (t.nb[i] < 0) { on_hull[a] = on_hull[b] = 1; ++border_edges; }
    }
  const size_t edges = (3 * T + border_edges) / 2;
  size_t hull_nodes = 0;
  for (char h : on_hull) hull_nodes += h ? 1 : 0;
  double area2d_total = 0;
  for (const Tri& t : tris) area2d_total += planeOf(nodes, t).area2d;

  std::cout << "========================================\n";
  std::cout << "  TIN: " << operation << "\n";
  std::cout << "========================================\n\n";
  std::cout << "Points read: " << rows << "\n";
  std::cout << "Nodes: " << nodes.size();
  if (merged > 0) std::cout << " (" << merged << " points with the same x, y merged, mean elevation)";
  std::cout << "\nTriangles: " << T << "\n";
  std::cout << "Edges: " << edges << " (" << border_edges << " on the border)\n";
  std::cout << "Nodes on the border (convex hull): " << hull_nodes << "\n";
  std::cout << "Area of the TIN (2D): " << area2d_total << "\n";
  if (verbose)
    std::cout << "Euler check (nodes - edges + triangles, must be 1): "
              << static_cast<long>(nodes.size()) - static_cast<long>(edges) + static_cast<long>(T) << "\n";

  // ---------------------------------------------------------------- node table
  if (!nodes_file.empty()) {
    Spatial::VectorDataset nd;
    nd.columns = {"id", "z", "degree", "on_hull", "adjacent_nodes", "geometry"};
    nd.geometry_column = "geometry";
    nd.crs = crs;
    for (size_t i = 0; i < nodes.size(); ++i) {
      Spatial::VectorFeature f;
      f.type = Spatial::VectorFeature::GeometryType::POINT;
      f.coordinates = {nodes[i].x, nodes[i].y};
      f.attributes["id"] = std::to_string(i + 1);
      f.attributes["z"] = numg(nodes[i].z);
      f.attributes["degree"] = std::to_string(adjacent[i].size());
      f.attributes["on_hull"] = on_hull[i] ? "1" : "0";
      std::string adj;
      for (int j : adjacent[i]) adj += (adj.empty() ? "" : ";") + std::to_string(j + 1);
      f.attributes["adjacent_nodes"] = adj;
      nd.features.push_back(f);
    }
    nd.feature_count = nd.features.size();
    writeVectorCSV(nd, nodes_file, {}, {"# Nodes of the TIN", "# Total nodes: " + std::to_string(nodes.size())});
    std::cout << "Nodes written to: " << nodes_file << "\n";
  }

  // ---------------------------------------------------------------- topology: plain table
  if (operation == "topology") {
    std::ofstream out(output_file);
    out << "triangle,node1,node2,node3,neighbor1,neighbor2,neighbor3\n";
    for (size_t k = 0; k < T; ++k) {
      out << k + 1;
      for (int i = 0; i < 3; ++i) out << "," << tris[k].n[i] + 1;
      for (int i = 0; i < 3; ++i) out << "," << tris[k].nb[i] + 1;  // 0 = none
      out << "\n";
    }
    std::cout << "\nneighborN is the triangle across the edge opposite nodeN (0 = the edge is on the border)\n";
    std::cout << "\nDone. Wrote " << output_file << "\n";
    return 0;
  }

  // ---------------------------------------------------------------- elevation at points
  if (operation == "elevation") {
    Spatial::VectorDataset pd;
    pd.columns = {"id", "x", "y", "triangle", "a", "b", "c", "z", "geometry"};
    pd.geometry_column = "geometry";
    pd.crs = crs;
    size_t outside = 0;
    std::cout << "\n";
    for (size_t q = 0; q < query.size(); ++q) {
      const double qx = query[q].first, qy = query[q].second;
      int found = -1;
      for (size_t k = 0; k < T && found < 0; ++k) {
        const Node &a = nodes[tris[k].n[0]], &b = nodes[tris[k].n[1]], &c = nodes[tris[k].n[2]];
        double o0 = (b.x - a.x) * (qy - a.y) - (b.y - a.y) * (qx - a.x);
        double o1 = (c.x - b.x) * (qy - b.y) - (c.y - b.y) * (qx - b.x);
        double o2 = (a.x - c.x) * (qy - c.y) - (a.y - c.y) * (qx - c.x);
        double tol = 1e-12 * (std::fabs(o0) + std::fabs(o1) + std::fabs(o2));
        if (o0 >= -tol && o1 >= -tol && o2 >= -tol) found = static_cast<int>(k);
      }
      Spatial::VectorFeature f;
      f.type = Spatial::VectorFeature::GeometryType::POINT;
      f.coordinates = {qx, qy};
      f.attributes["id"] = std::to_string(q + 1);
      f.attributes["x"] = numg(qx);
      f.attributes["y"] = numg(qy);
      if (found < 0) {
        ++outside;
        f.attributes["triangle"] = "0";
        std::cout << "Point " << q + 1 << " (" << numg(qx) << ", " << numg(qy)
                  << "): outside the TIN (convex hull of the points)\n";
      } else {
        const Tri& t = tris[found];
        Plane p = planeOf(nodes, t);
        const Node& a = nodes[t.n[0]];
        double c0 = a.z - p.gx * a.x - p.gy * a.y;
        double z = p.gx * qx + p.gy * qy + c0;
        f.attributes["triangle"] = std::to_string(found + 1);
        f.attributes["a"] = numg(p.gx);
        f.attributes["b"] = numg(p.gy);
        f.attributes["c"] = numg(c0);
        f.attributes["z"] = numg(z);
        std::cout << "Point " << q + 1 << " (" << numg(qx) << ", " << numg(qy) << "): triangle " << found + 1
                  << " (nodes " << t.n[0] + 1 << ", " << t.n[1] + 1 << ", " << t.n[2] + 1 << "), z = "
                  << numg(p.gx) << " x + " << numg(p.gy) << " y + " << numg(c0) << " = " << numg(z) << "\n";
      }
      pd.features.push_back(f);
    }
    pd.feature_count = pd.features.size();
    writeVectorCSV(pd, output_file, {},
                   {"# Elevation on the TIN: z = a*x + b*y + c in the triangle that contains the point",
                    "# Points outside the TIN have triangle 0 and no z"});
    if (outside > 0)
      std::cerr << "\nWarning: " << outside << " point(s) outside the TIN have no elevation\n";
    std::cout << "\nDone. Wrote " << output_file << "\n";
    return 0;
  }

  // ---------------------------------------------------------------- spatial outputs
  Spatial::VectorDataset out;
  out.geometry_column = "geometry";
  out.crs = crs;
  std::vector<std::string> notes;
  double total_area3d = 0, total_cut = 0, total_fill = 0, area_above = 0, area_below = 0;
  double slope_weighted = 0;

  if (operation == "build")
    out.columns = {"id", "node1", "node2", "node3", "z1", "z2", "z3", "centroid_x", "centroid_y",
                   "centroid_z", "area_2d", "geometry"};
  else if (operation == "terrain")
    out.columns = {"id", "node1", "node2", "node3", "mean_z", "area_2d", "area_3d", "slope_deg",
                   "slope_pct", "aspect", "geometry"};
  else
    out.columns = {"id", "node1", "node2", "node3", "mean_z", "area_2d", "cut", "fill", "geometry"};

  for (size_t k = 0; k < T; ++k) {
    const Tri& t = tris[k];
    Plane p = planeOf(nodes, t);
    const Node &a = nodes[t.n[0]], &b = nodes[t.n[1]], &c = nodes[t.n[2]];
    double mean_z = (a.z + b.z + c.z) / 3.0;
    Spatial::VectorFeature f = polygonOf(nodes, t);
    f.attributes["id"] = std::to_string(k + 1);
    for (int i = 0; i < 3; ++i) f.attributes["node" + std::to_string(i + 1)] = std::to_string(t.n[i] + 1);
    f.attributes["mean_z"] = numg(mean_z);
    f.attributes["area_2d"] = num(p.area2d);
    if (operation == "build") {
      f.attributes["z1"] = numg(a.z);
      f.attributes["z2"] = numg(b.z);
      f.attributes["z3"] = numg(c.z);
      f.attributes["centroid_x"] = num((a.x + b.x + c.x) / 3.0);
      f.attributes["centroid_y"] = num((a.y + b.y + c.y) / 3.0);
      f.attributes["centroid_z"] = numg(mean_z);
    } else if (operation == "terrain") {
      double g = std::hypot(p.gx, p.gy);
      double aspect = g < 1e-12 ? -1.0 : std::fmod(std::atan2(-p.gx, -p.gy) * 180.0 / PI + 360.0, 360.0);
      double area3d = p.area2d * std::sqrt(1.0 + g * g);
      total_area3d += area3d;
      slope_weighted += std::atan(g) * 180.0 / PI * p.area2d;
      f.attributes["area_3d"] = num(area3d);
      f.attributes["slope_deg"] = num(std::atan(g) * 180.0 / PI, 4);
      f.attributes["slope_pct"] = num(100.0 * g, 4);
      f.attributes["aspect"] = num(aspect, 2);
    } else {
      double pos = positivePart(p.area2d, a.z - level, b.z - level, c.z - level);
      double sgn = p.area2d * ((a.z - level) + (b.z - level) + (c.z - level)) / 3.0;  // signed volume
      double neg = pos - sgn;
      total_cut += pos;
      total_fill += neg;
      f.attributes["cut"] = num(pos);
      f.attributes["fill"] = num(neg);
      // plan area where the terrain is above / below the plane
      double h[3] = {a.z - level, b.z - level, c.z - level};
      int up = (h[0] > 0) + (h[1] > 0) + (h[2] > 0);
      double fr_above;
      if (up == 3) fr_above = 1.0;
      else if (up == 0) fr_above = 0.0;
      else {
        std::sort(h, h + 3);
        if (up == 1) fr_above = (h[2] / (h[2] - h[0])) * (h[2] / (h[2] - h[1]));
        else fr_above = 1.0 - (-h[0] / (h[1] - h[0])) * (-h[0] / (h[2] - h[0]));
      }
      area_above += fr_above * p.area2d;
      area_below += (1.0 - fr_above) * p.area2d;
    }
    out.features.push_back(f);
  }
  out.feature_count = out.features.size();

  notes.push_back("# TIN: Delaunay triangulation of " + std::to_string(nodes.size()) + " points");
  notes.push_back("# Total triangles: " + std::to_string(T));
  if (operation == "terrain") {
    std::cout << "Area of the surface (3D): " << total_area3d << "\n";
    std::cout << "Mean slope (weighted by area): " << (area2d_total > 0 ? slope_weighted / area2d_total : 0)
              << " degrees\n";
    notes.push_back("# Aspect: degrees clockwise from north, direction of steepest descent; -1 = flat");
  } else if (operation == "volume") {
    std::cout << "Reference plane: z = " << level << "\n";
    std::cout << "Cut (terrain above the plane, volume to remove): " << total_cut << "\n";
    std::cout << "Fill (terrain below the plane, volume to add):   " << total_fill << "\n";
    std::cout << "Net (cut - fill): " << total_cut - total_fill << "\n";
    std::cout << "Plan area above the plane: " << area_above << ", below: " << area_below << "\n";
    notes.push_back("# Reference plane z = " + numg(level));
    notes.push_back("# Total cut: " + num(total_cut) + "  Total fill: " + num(total_fill));
  }
  writeVectorCSV(out, output_file, {}, notes);

  std::cout << "\nDone. Wrote " << output_file << "\n";
  return 0;
}
