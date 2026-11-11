#pragma once
// Delaunay triangulation of a set of 2D points (incremental Bowyer-Watson with an
// "infinite vertex": the outside of the convex hull is covered by ghost triangles, so
// no artificial super-triangle is needed and the hull is exact).
//
// Triangles are stored with neighbor links; vertex index -1 is the point at infinity.
// Finite triangles are counter-clockwise. The points must be distinct (the caller removes
// duplicates). Header-only, C++17/STL.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Spatial {

class Delaunay {
 public:
  // Builds the triangulation. Returns false when there are fewer than 3 points or all of
  // them are collinear (no triangle exists).
  bool build(const std::vector<double>& xs, const std::vector<double>& ys) {
    tris_.clear();
    free_.clear();
    const size_t n = xs.size();
    n_ = n;
    if (n < 3 || ys.size() != n)
      return false;
    x_.resize(n);
    y_.resize(n);
    ox_ = xs[0];
    oy_ = ys[0];
    for (size_t i = 0; i < n; ++i) {  // translate: improves the precision of the predicates
      x_[i] = xs[i] - ox_;
      y_[i] = ys[i] - oy_;
    }

    // Seed triangle: the first two points and the first one that is not collinear with them.
    int i0 = 0, i1 = -1, i2 = -1;
    for (size_t i = 1; i < n; ++i)
      if (x_[i] != x_[0] || y_[i] != y_[0]) { i1 = static_cast<int>(i); break; }
    if (i1 < 0)
      return false;
    const double dx = x_[i1] - x_[i0], dy = y_[i1] - y_[i0];
    for (size_t i = 1; i < n; ++i) {
      if (static_cast<int>(i) == i1)
        continue;
      double o = dx * (y_[i] - y_[i0]) - dy * (x_[i] - x_[i0]);
      double scale = std::sqrt(dx * dx + dy * dy) *
                     std::sqrt((x_[i] - x_[i0]) * (x_[i] - x_[i0]) + (y_[i] - y_[i0]) * (y_[i] - y_[i0]));
      if (std::fabs(o) > 1e-12 * scale) { i2 = static_cast<int>(i); break; }
    }
    if (i2 < 0)
      return false;
    if (orient(i0, i1, x_[i2], y_[i2]) < 0)
      std::swap(i1, i2);

    // Initial triangle plus its three ghost triangles.
    addTri(i0, i1, i2);
    addTri(i1, i0, -1);
    addTri(i2, i1, -1);
    addTri(i0, i2, -1);
    linkAll(4);
    mark_.assign(tris_.size(), 0);
    stamp_ = 0;
    byU_.assign(n + 1, -1);
    byW_.assign(n + 1, -1);
    last_ = 0;

    // Remaining points in a boustrophedon order (spatially coherent: short walks).
    std::vector<int> order;
    order.reserve(n);
    for (size_t i = 0; i < n; ++i)
      if (static_cast<int>(i) != i0 && static_cast<int>(i) != i1 && static_cast<int>(i) != i2)
        order.push_back(static_cast<int>(i));
    double minx = x_[0], maxx = x_[0], miny = y_[0], maxy = y_[0];
    for (size_t i = 1; i < n; ++i) {
      minx = std::min(minx, x_[i]); maxx = std::max(maxx, x_[i]);
      miny = std::min(miny, y_[i]); maxy = std::max(maxy, y_[i]);
    }
    const int strips = std::max(1, static_cast<int>(std::sqrt(static_cast<double>(n) / 2.0)));
    const double h = (maxy - miny) / strips;
    std::vector<int> strip(n, 0);
    for (size_t i = 0; i < n; ++i) {
      int s = h > 0 ? static_cast<int>((y_[i] - miny) / h) : 0;
      strip[i] = std::min(strips - 1, std::max(0, s));
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) {
      if (strip[a] != strip[b]) return strip[a] < strip[b];
      return (strip[a] % 2 == 0) ? x_[a] < x_[b] : x_[a] > x_[b];
    });
    for (int p : order)
      insert(p);
    return numTriangles() > 0;
  }

  size_t numPoints() const { return n_; }

  size_t numTriangles() const {
    size_t c = 0;
    for (const Tri& t : tris_)
      if (t.alive && t.v[0] >= 0 && t.v[1] >= 0 && t.v[2] >= 0) ++c;
    return c;
  }

  // Finite triangles (vertex indices into the arrays given to build), counter-clockwise.
  std::vector<std::array<int, 3>> triangles() const {
    std::vector<std::array<int, 3>> out;
    for (const Tri& t : tris_)
      if (t.alive && t.v[0] >= 0 && t.v[1] >= 0 && t.v[2] >= 0)
        out.push_back({t.v[0], t.v[1], t.v[2]});
    return out;
  }

  // Finite triangles with their neighbors. nbrs[k][i] is the index (in the same list) of the
  // triangle across the edge opposite to verts[k][i], or -1 when that edge is on the convex hull.
  void triangulation(std::vector<std::array<int, 3>>& verts, std::vector<std::array<int, 3>>& nbrs) const {
    std::vector<int> idx(tris_.size(), -1);
    verts.clear();
    nbrs.clear();
    for (size_t t = 0; t < tris_.size(); ++t)
      if (tris_[t].alive && !isGhost(static_cast<int>(t))) {
        idx[t] = static_cast<int>(verts.size());
        verts.push_back({tris_[t].v[0], tris_[t].v[1], tris_[t].v[2]});
      }
    for (size_t t = 0; t < tris_.size(); ++t)
      if (idx[t] >= 0)
        nbrs.push_back({idx[tris_[t].n[0]], idx[tris_[t].n[1]], idx[tris_[t].n[2]]});
  }

  // Finds the triangle that contains (x, y) and its barycentric coordinates. Returns false
  // when the point is outside the convex hull. Consecutive queries that are close to each
  // other are fast (the walk starts at the previous result).
  bool locate(double x, double y, std::array<int, 3>& v, double& l0, double& l1, double& l2) const {
    if (tris_.empty())
      return false;
    const double px = x - xOrigin(), py = y - yOrigin();
    int t = last_;
    if (!tris_[t].alive || isGhost(t))
      t = anyFinite();
    if (t < 0)
      return false;
    const size_t limit = 4 * tris_.size() + 100;
    for (size_t steps = 0; steps < limit; ++steps) {
      const Tri& T = tris_[t];
      int moved = -1;
      for (int k = 0; k < 3; ++k) {
        int i = static_cast<int>((steps + k) % 3);
        int a = T.v[(i + 1) % 3], b = T.v[(i + 2) % 3];
        double o = orient(a, b, px, py);
        double tol = 1e-12 * (std::fabs((x_[b] - x_[a]) * (py - y_[a])) + std::fabs((y_[b] - y_[a]) * (px - x_[a])));
        if (o < -tol) { moved = i; break; }
      }
      if (moved < 0) {
        last_ = t;
        double o0 = orient(T.v[1], T.v[2], px, py);
        double o1 = orient(T.v[2], T.v[0], px, py);
        double o2 = orient(T.v[0], T.v[1], px, py);
        double s = o0 + o1 + o2;
        v = {T.v[0], T.v[1], T.v[2]};
        if (s <= 0) { l0 = 1; l1 = l2 = 0; return true; }
        l0 = std::max(0.0, o0 / s); l1 = std::max(0.0, o1 / s); l2 = std::max(0.0, o2 / s);
        double sum = l0 + l1 + l2;
        l0 /= sum; l1 /= sum; l2 /= sum;
        return true;
      }
      int nb = T.n[moved];
      if (isGhost(nb))
        return false;  // beyond a hull edge
      t = nb;
    }
    // Fallback (should not happen on a Delaunay triangulation): exhaustive search.
    for (size_t i = 0; i < tris_.size(); ++i) {
      if (!tris_[i].alive || isGhost(static_cast<int>(i))) continue;
      const Tri& T = tris_[i];
      double o0 = orient(T.v[1], T.v[2], px, py), o1 = orient(T.v[2], T.v[0], px, py),
             o2 = orient(T.v[0], T.v[1], px, py);
      if (o0 >= 0 && o1 >= 0 && o2 >= 0) {
        double s = o0 + o1 + o2;
        v = {T.v[0], T.v[1], T.v[2]};
        l0 = o0 / s; l1 = o1 / s; l2 = o2 / s;
        last_ = static_cast<int>(i);
        return true;
      }
    }
    return false;
  }

 private:
  struct Tri {
    int v[3];   // vertices, -1 = point at infinity
    int n[3];   // n[i]: neighbor across the edge opposite v[i]
    bool alive;
  };

  size_t n_ = 0;
  std::vector<double> x_, y_;
  std::vector<Tri> tris_;
  std::vector<int> free_, mark_, byU_, byW_;
  int stamp_ = 0;
  mutable int last_ = 0;
  double ox_ = 0, oy_ = 0;  // origin of the translated coordinates (the first point)
  double xOrigin() const { return ox_; }
  double yOrigin() const { return oy_; }

  bool isGhost(int t) const {
    return tris_[t].v[0] < 0 || tris_[t].v[1] < 0 || tris_[t].v[2] < 0;
  }

  int anyFinite() const {
    for (size_t i = tris_.size(); i-- > 0;)
      if (tris_[i].alive && !isGhost(static_cast<int>(i)))
        return static_cast<int>(i);
    return -1;
  }

  double orient(int a, int b, double px, double py) const {
    return (x_[b] - x_[a]) * (py - y_[a]) - (y_[b] - y_[a]) * (px - x_[a]);
  }

  int addTri(int a, int b, int c) {
    Tri t;
    t.v[0] = a; t.v[1] = b; t.v[2] = c;
    t.n[0] = t.n[1] = t.n[2] = -1;
    t.alive = true;
    if (!free_.empty()) {
      int id = free_.back();
      free_.pop_back();
      tris_[id] = t;
      if (static_cast<size_t>(id) < mark_.size()) mark_[id] = 0;
      return id;
    }
    tris_.push_back(t);
    if (!mark_.empty()) mark_.push_back(0);
    return static_cast<int>(tris_.size()) - 1;
  }

  // Links neighbors of the first `count` triangles by matching their directed edges.
  void linkAll(size_t count) {
    for (size_t a = 0; a < count; ++a)
      for (int i = 0; i < 3; ++i) {
        int u = tris_[a].v[(i + 1) % 3], w = tris_[a].v[(i + 2) % 3];
        for (size_t b = 0; b < count; ++b) {
          if (b == a) continue;
          for (int j = 0; j < 3; ++j)
            if (tris_[b].v[(j + 1) % 3] == w && tris_[b].v[(j + 2) % 3] == u) {
              tris_[a].n[i] = static_cast<int>(b);
              tris_[b].n[j] = static_cast<int>(a);
            }
        }
      }
  }

  // True when point p must be removed from the triangulation before p is inserted.
  bool conflict(int t, double px, double py) const {
    const Tri& T = tris_[t];
    int g = -1;
    for (int i = 0; i < 3; ++i)
      if (T.v[i] < 0) g = i;
    if (g < 0) {
      double adx = x_[T.v[0]] - px, ady = y_[T.v[0]] - py;
      double bdx = x_[T.v[1]] - px, bdy = y_[T.v[1]] - py;
      double cdx = x_[T.v[2]] - px, cdy = y_[T.v[2]] - py;
      double al = adx * adx + ady * ady, bl = bdx * bdx + bdy * bdy, cl = cdx * cdx + cdy * cdy;
      double det = al * (bdx * cdy - cdx * bdy) + bl * (cdx * ady - adx * cdy) + cl * (adx * bdy - bdx * ady);
      double perm = al * (std::fabs(bdx * cdy) + std::fabs(cdx * bdy)) +
                    bl * (std::fabs(cdx * ady) + std::fabs(adx * cdy)) +
                    cl * (std::fabs(adx * bdy) + std::fabs(bdx * ady));
      return det > 1e-13 * perm;
    }
    // Ghost triangle of the hull edge a -> b: the outside lies on the left of a -> b.
    int a = T.v[(g + 1) % 3], b = T.v[(g + 2) % 3];
    double o = orient(a, b, px, py);
    if (o > 0) return true;
    if (o == 0) {  // on the line of the hull edge: conflict only if strictly between a and b
      double dx = x_[b] - x_[a], dy = y_[b] - y_[a];
      double s = (px - x_[a]) * dx + (py - y_[a]) * dy;
      return s > 0 && s < dx * dx + dy * dy;
    }
    return false;
  }

  int findSeed(double px, double py) {
    int t = last_;
    if (!tris_[t].alive || isGhost(t))
      t = anyFinite();
    const size_t limit = 4 * tris_.size() + 100;
    for (size_t steps = 0; steps < limit; ++steps) {
      const Tri& T = tris_[t];
      if (isGhost(t)) {
        if (conflict(t, px, py)) return t;
        break;
      }
      int moved = -1;
      for (int k = 0; k < 3; ++k) {
        int i = static_cast<int>((steps + k) % 3);
        if (orient(T.v[(i + 1) % 3], T.v[(i + 2) % 3], px, py) < 0) { moved = i; break; }
      }
      if (moved < 0)
        return conflict(t, px, py) ? t : -1;  // -1: the point coincides with a vertex
      t = T.n[moved];
    }
    for (size_t i = 0; i < tris_.size(); ++i)
      if (tris_[i].alive && conflict(static_cast<int>(i), px, py))
        return static_cast<int>(i);
    return -1;
  }

  bool insert(int p) {
    const double px = x_[p], py = y_[p];
    int seed = findSeed(px, py);
    if (seed < 0)
      return false;
    ++stamp_;
    std::vector<int> cav;
    cav.push_back(seed);
    mark_[seed] = stamp_;
    for (size_t k = 0; k < cav.size(); ++k) {
      int t = cav[k];
      for (int i = 0; i < 3; ++i) {
        int nb = tris_[t].n[i];
        if (mark_[nb] == stamp_) continue;
        if (conflict(nb, px, py)) {
          mark_[nb] = stamp_;
          cav.push_back(nb);
        }
      }
    }
    // Boundary of the cavity: edges u -> w (cavity on the left) whose neighbor is outside.
    struct Edge { int u, w, outside, slot; };  // slot: index in outside.n that points into the cavity
    std::vector<Edge> boundary;
    for (int t : cav)
      for (int i = 0; i < 3; ++i) {
        int nb = tris_[t].n[i];
        if (mark_[nb] != stamp_) {
          int slot = 0;
          for (int k = 0; k < 3; ++k)
            if (tris_[nb].n[k] == t) slot = k;
          boundary.push_back({tris_[t].v[(i + 1) % 3], tris_[t].v[(i + 2) % 3], nb, slot});
        }
      }
    // Validity: simple cycle, and every new finite triangle must be counter-clockwise.
    for (const Edge& e : boundary)
      if (e.u >= 0 && e.w >= 0 && !(orient(e.u, e.w, px, py) > 0))
        return false;
    bool simple = true;
    for (const Edge& e : boundary) {
      if (byU_[e.u + 1] == stamp_ || byW_[e.w + 1] == stamp_) simple = false;
      byU_[e.u + 1] = stamp_;
      byW_[e.w + 1] = stamp_;
    }
    for (const Edge& e : boundary) { byU_[e.u + 1] = -1; byW_[e.w + 1] = -1; }
    if (!simple)
      return false;

    for (int t : cav) {
      tris_[t].alive = false;
      free_.push_back(t);
    }
    std::vector<int> created;
    created.reserve(boundary.size());
    for (const Edge& e : boundary) {
      int nt = addTri(e.u, e.w, p);
      created.push_back(nt);
      byU_[e.u + 1] = nt;  // new triangle whose edge starts at u
      byW_[e.w + 1] = nt;  // new triangle whose edge ends at w
      tris_[nt].n[2] = e.outside;
      tris_[e.outside].n[e.slot] = nt;
    }
    for (size_t k = 0; k < boundary.size(); ++k) {
      int nt = created[k];
      tris_[nt].n[0] = byU_[boundary[k].w + 1];  // across (w, p): triangle that starts at w
      tris_[nt].n[1] = byW_[boundary[k].u + 1];  // across (p, u): triangle that ends at u
    }
    for (const Edge& e : boundary) { byU_[e.u + 1] = -1; byW_[e.w + 1] = -1; }
    for (int nt : created)
      if (!isGhost(nt)) { last_ = nt; break; }
    return true;
  }
};

}  // namespace Spatial
