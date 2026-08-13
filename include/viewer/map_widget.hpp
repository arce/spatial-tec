#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/fl_draw.H>

#include "core/spatial_geom.hpp"
#include "viewer/layer.hpp"
#include "viewer/multipart_geometry.hpp"

namespace Viewer {

inline void valueToColor(double t, uchar& r, uchar& g, uchar& b) {
  t = std::clamp(t, 0.0, 1.0);
  if (t < 0.5) {
    double tt = t * 2.0;
    r = 0;
    g = (uchar)(255 * tt);
    b = (uchar)(255 * (1.0 - tt));
  } else {
    double tt = (t - 0.5) * 2.0;
    r = (uchar)(255 * tt);
    g = (uchar)(255 * (1.0 - tt));
    b = 0;
  }
}

// Parses a "RRGGBB" or "#RRGGBB" hex color (the convention used for RAT
// color fields). Returns false (leaving r/g/b untouched) if the string
// isn't a valid 6-digit hex color.
inline bool parseHexColor(const std::string& s, uchar& r, uchar& g, uchar& b) {
  std::string hex = s;
  if (!hex.empty() && hex[0] == '#') hex = hex.substr(1);
  if (hex.size() != 6) return false;
  for (char ch : hex) {
    if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
  }
  unsigned int rgb = 0;
  try {
    rgb = (unsigned int)std::stoul(hex, nullptr, 16);
  } catch (...) {
    return false;
  }
  r = (uchar)((rgb >> 16) & 0xFF);
  g = (uchar)((rgb >> 8) & 0xFF);
  b = (uchar)(rgb & 0xFF);
  return true;
}

class MapWidget : public Fl_Box {
public:
  MapWidget(int X, int Y, int W, int H, const char* L = 0)
      : Fl_Box(X, Y, W, H, L) {
    box(FL_DOWN_BOX);
    color(FL_WHITE);
  }

  // Called when the user clicks (without dragging) on a feature. Receives
  // the layer index and feature index within that layer's vector_data.
  void setFeatureClickCallback(std::function<void(int, int)> cb) {
    feature_click_cb_ = std::move(cb);
  }

  // Highlights a single feature (e.g. because its row was picked in the
  // attribute table, or it was clicked on the map). Identified by the raw
  // Layer pointer rather than an index so the highlight survives layer
  // reordering/insertion; it's automatically dropped if that layer is no
  // longer in layers_ (see drawHighlightedFeature()).
  void setHighlightedFeature(Layer* layer, int feature_index) {
    highlight_layer_ = layer;
    highlight_feature_ = feature_index;
    redraw();
  }

  void clearHighlight() {
    highlight_layer_ = nullptr;
    highlight_feature_ = -1;
    redraw();
  }

  void setLayers(std::vector<std::shared_ptr<Layer>>* l) {
    layers_ = l;
    calculateExtent();
    redraw();
  }

  void calculateExtent() {
    if (!layers_ || layers_->empty()) {
      has_data_ = false;
      return;
    }

    bool first = true;
    double g_minx = 0, g_miny = 0, g_maxx = 0, g_maxy = 0;

    for (const auto& layer : *layers_) {
      if (!layer->visible) continue;
      double minx, miny, maxx, maxy;
      if (!layer->getBBox(minx, miny, maxx, maxy)) continue;
      if (first) {
        g_minx = minx; g_miny = miny;
        g_maxx = maxx; g_maxy = maxy;
        first = false;
      } else {
        g_minx = std::min(g_minx, minx);
        g_miny = std::min(g_miny, miny);
        g_maxx = std::max(g_maxx, maxx);
        g_maxy = std::max(g_maxy, maxy);
      }
    }

    if (first) {
      has_data_ = false;
      return;
    }

    has_data_ = true;
    double world_w = g_maxx - g_minx;
    double world_h = g_maxy - g_miny;
    if (world_w <= 1e-9) world_w = 1.0;
    if (world_h <= 1e-9) world_h = 1.0;

    // Remembered so zoomToFeature() can pick a sensible minimum zoom extent
    // (as a fraction of the whole dataset) regardless of coordinate units.
    full_extent_w_ = world_w;
    full_extent_h_ = world_h;

    double avail_w = std::max(10.0, (double)w() - 2 * margin_);
    double avail_h = std::max(10.0, (double)h() - 2 * margin_);

    scale_x_ = std::min(avail_w / world_w, avail_h / world_h);
    scale_y_ = scale_x_;

    offset_x_ = g_minx - ((avail_w / scale_x_) - world_w) / 2.0;
    offset_y_ = g_miny - ((avail_h / scale_y_) - world_h) / 2.0;
  }

  // Frames the given feature in the viewport (used when its row is picked
  // in the attribute table). Small/point features get a minimum view size
  // based on a fraction of the full dataset extent, so zooming doesn't go
  // in infinitely; larger features get proportional padding around them.
  void zoomToFeature(Layer* layer, int feature_index) {
    if (!layer || layer->type != LayerType::VECTOR) return;
    const auto& features = layer->vector_data.features;
    if (feature_index < 0 || feature_index >= (int)features.size()) return;

    const auto& feature = features[feature_index];
    if (feature.coordinates.size() < 2) return;

    double minx = feature.coordinates[0], maxx = feature.coordinates[0];
    double miny = feature.coordinates[1], maxy = feature.coordinates[1];
    for (size_t i = 0; i < feature.coordinates.size(); i += 2) {
      minx = std::min(minx, feature.coordinates[i]);
      maxx = std::max(maxx, feature.coordinates[i]);
      miny = std::min(miny, feature.coordinates[i + 1]);
      maxy = std::max(maxy, feature.coordinates[i + 1]);
    }

    double world_w = maxx - minx;
    double world_h = maxy - miny;

    double min_extent = std::max(full_extent_w_, full_extent_h_) * 0.03;
    if (min_extent <= 1e-9) min_extent = 1.0;

    double pad_w = std::max(world_w * 0.3, min_extent);
    double pad_h = std::max(world_h * 0.3, min_extent);
    minx -= pad_w;
    maxx += pad_w;
    miny -= pad_h;
    maxy += pad_h;

    world_w = maxx - minx;
    world_h = maxy - miny;
    if (world_w <= 1e-9) world_w = 1.0;
    if (world_h <= 1e-9) world_h = 1.0;

    has_data_ = true;
    double avail_w = std::max(10.0, (double)w() - 2 * margin_);
    double avail_h = std::max(10.0, (double)h() - 2 * margin_);

    scale_x_ = std::min(avail_w / world_w, avail_h / world_h);
    scale_y_ = scale_x_;

    offset_x_ = minx - ((avail_w / scale_x_) - world_w) / 2.0;
    offset_y_ = miny - ((avail_h / scale_y_) - world_h) / 2.0;
    redraw();
  }

  void zoomIn() { zoomAt(x() + w() / 2.0, y() + h() / 2.0, 1.2); }
  void zoomOut() { zoomAt(x() + w() / 2.0, y() + h() / 2.0, 1.0 / 1.2); }

  void zoomAt(double screen_x, double screen_y, double factor) {
    if (!has_data_) return;
    double wx = toWorldX(screen_x);
    double wy = toWorldY(screen_y);
    scale_x_ *= factor;
    scale_y_ *= factor;
    offset_x_ = wx - (screen_x - x() - margin_) / scale_x_;
    offset_y_ = wy - (y() + h() - margin_ - screen_y) / scale_y_;
    redraw();
  }

  void fitToExtent() {
    calculateExtent();
    redraw();
  }

  void pan(double dx, double dy) {
    offset_x_ -= dx / scale_x_;
    offset_y_ += dy / scale_y_;
    redraw();
  }

  void draw() override {
    Fl_Box::draw();

    if (!has_data_ || !layers_) {
      fl_color(FL_GRAY);
      fl_font(FL_HELVETICA, 14);
      fl_draw("No layers loaded", x() + w() / 2 - 80, y() + h() / 2, 160, 20, FL_ALIGN_CENTER);
      return;
    }

    fl_push_clip(x(), y(), w(), h());

    for (const auto& layer : *layers_) {
      if (layer->visible && layer->type == LayerType::RASTER) drawRasterLayer(*layer);
    }
    for (const auto& layer : *layers_) {
      if (layer->visible && layer->type == LayerType::VECTOR) drawVectorLayer(*layer);
    }

    // Vector labels are drawn in their own top-most pass (after every
    // layer, raster or vector) so they're never hidden underneath a layer
    // drawn later. Raster cell labels are instead drawn inline, right
    // after that raster's own cells -- see drawRasterLayer().
    for (const auto& layer : *layers_) {
      if (layer->visible && layer->type == LayerType::VECTOR && layer->show_labels &&
          !layer->label_field.empty()) {
        drawVectorLabels(*layer);
      }
    }

    drawHighlightedFeature();

    fl_pop_clip();

    drawViewportCoordinates();
  }

  int handle(int event) override {
    switch (event) {
      case FL_PUSH:
        if (!Fl::event_inside(x(), y(), w(), h())) return 0;
        updateCursorPos();
        if (Fl::event_clicks() > 0) {
          // Double-click: show the feature's attributes instead of panning.
          selectFeatureAt(Fl::event_x(), Fl::event_y());
          return 1;
        }
        panning_ = true;
        pan_start_x_ = Fl::event_x();
        pan_start_y_ = Fl::event_y();
        return 1;

      case FL_DRAG:
        if (!panning_) return 0;
        updateCursorPos();
        pan(Fl::event_x() - pan_start_x_, Fl::event_y() - pan_start_y_);
        pan_start_x_ = Fl::event_x();
        pan_start_y_ = Fl::event_y();
        return 1;

      case FL_RELEASE:
        panning_ = false;
        return 1;

      case FL_MOVE:
        // Live cursor-coordinate readout (bottom-right corner), like the
        // MapWindow reference: it updates as the mouse moves, unlike the
        // idle viewport-extent readout which only changes on pan/zoom.
        if (!Fl::event_inside(x(), y(), w(), h())) {
          if (mouse_in_view_) {
            mouse_in_view_ = false;
            redraw();
          }
          return 0;
        }
        updateCursorPos();
        return 1;

      case FL_LEAVE:
        if (mouse_in_view_) {
          mouse_in_view_ = false;
          redraw();
        }
        return 1;

      case FL_MOUSEWHEEL: {
        if (!Fl::event_inside(x(), y(), w(), h())) return 0;
        double factor = (Fl::event_dy() > 0) ? 1.0 / 1.2 : 1.2;
        zoomAt(Fl::event_x(), Fl::event_y(), factor);
        return 1;
      }

      default:
        return Fl_Box::handle(event);
    }
  }

private:
  void updateCursorPos() {
    mouse_in_view_ = true;
    mouse_screen_x_ = Fl::event_x();
    mouse_screen_y_ = Fl::event_y();
    redraw();
  }

  double toScreenX(double wx) const { return x() + margin_ + (wx - offset_x_) * scale_x_; }
  double toScreenY(double wy) const { return y() + h() - margin_ - (wy - offset_y_) * scale_y_; }
  double toWorldX(double sx) const { return offset_x_ + (sx - x() - margin_) / scale_x_; }
  double toWorldY(double sy) const { return offset_y_ + (y() + h() - margin_ - sy) / scale_y_; }

  // Finds the topmost (last-drawn) visible feature under the click and
  // reports it via feature_click_cb_. Uses each feature's bounding box as a
  // cheap first filter, then an exact geometry test (point-in-polygon /
  // distance-to-line/point) so selection is accurate even for irregular
  // shapes. For a raster layer with a RAT, "feature" means the RAT row for
  // whatever class the clicked cell belongs to (see rasterRatRowAt()) --
  // feature_click_cb_ is reused with that as the second index.
  void selectFeatureAt(int screen_x, int screen_y) {
    if (!layers_ || !has_data_ || !feature_click_cb_) return;

    double wx = toWorldX(screen_x);
    double wy = toWorldY(screen_y);
    double tol = 5.0 / std::max(scale_x_, 1e-9);  // ~5 screen px, in world units

    // Layers are drawn from index 0 to size()-1, so the last one drawn (and
    // therefore the one visually on top) is at the end of the vector.
    for (int li = (int)layers_->size() - 1; li >= 0; --li) {
      const auto& layer = (*layers_)[li];
      if (!layer->visible) continue;

      if (layer->type == LayerType::RASTER) {
        if (!layer->raster_data.has_rat) continue;
        int rat_row = rasterRatRowAt(layer->raster_data, wx, wy);
        if (rat_row >= 0) {
          feature_click_cb_(li, rat_row);
          return;
        }
        continue;
      }

      const auto& features = layer->vector_data.features;
      int best_fi = -1;
      double best_extent = std::numeric_limits<double>::max();

      for (int fi = 0; fi < (int)features.size(); ++fi) {
        const auto& feature = features[fi];
        if (!hitTestFeature(feature, wx, wy, tol)) continue;

        double extent = featureBBoxExtent(feature);
        if (extent < best_extent) {
          best_extent = extent;
          best_fi = fi;
        }
      }

      if (best_fi >= 0) {
        feature_click_cb_(li, best_fi);
        return;
      }
    }
  }

  // Maps a world-space click to the raster cell under it, then looks up
  // that cell's value in the RAT. Returns -1 if the click misses the grid,
  // lands on a nodata cell, or the value has no matching RAT row.
  static int rasterRatRowAt(const Spatial::RasterDataset& ds, double wx, double wy) {
    if (ds.ncols <= 0 || ds.nrows <= 0 || ds.cellsize <= 0) return -1;

    int c = (int)std::floor((wx - ds.xllcorner) / ds.cellsize);
    int r_from_bottom = (int)std::floor((wy - ds.yllcorner) / ds.cellsize);
    if (c < 0 || c >= ds.ncols || r_from_bottom < 0 || r_from_bottom >= ds.nrows) return -1;

    int r = ds.nrows - 1 - r_from_bottom;
    double val = ds.at(r, c);
    if (std::abs(val - ds.nodata_value) < 1e-9) return -1;

    return ds.findRatRow(val);
  }

  // Point/line/polygon hit test for one flat [x0,y0,x1,y1,...] part, picked
  // by `base_type` (the MULTI* feature's own type, since each part of a
  // MultiPoint/MultiLineString/MultiPolygon is itself a point/line/polygon).
  static bool hitTestPart(Spatial::VectorFeature::GeometryType base_type,
                          const std::vector<double>& coords, double wx, double wy, double tol) {
    using Geom = Spatial::VectorFeature::GeometryType;
    if (coords.size() < 2) return false;

    if (base_type == Geom::POINT || base_type == Geom::MULTIPOINT) {
      return pointToPointDistance(wx, wy, coords[0], coords[1]) <= tol;
    }
    if (base_type == Geom::LINESTRING || base_type == Geom::MULTILINESTRING) {
      return pointToLineDistance(wx, wy, coords) <= tol;
    }
    // POLYGON/MULTIPOLYGON part: inside the ring, or close enough to its boundary.
    if (pointInPolygon(wx, wy, coords)) return true;
    return pointToLineDistance(wx, wy, coords) <= tol;
  }

  // feature.part_starts (see spatial_types.hpp) carries the real part
  // boundaries for a MULTIPOINT/MULTILINESTRING/MULTIPOLYGON made of more
  // than one part -- empty for everything else, in which case this tests
  // feature.coordinates as a single shape, exactly as before.
  static bool hitTestFeature(const Spatial::VectorFeature& feature, double wx, double wy, double tol) {
    if (feature.coordinates.size() < 2) return false;

    double minx = feature.coordinates[0], maxx = feature.coordinates[0];
    double miny = feature.coordinates[1], maxy = feature.coordinates[1];
    for (size_t i = 0; i < feature.coordinates.size(); i += 2) {
      minx = std::min(minx, feature.coordinates[i]);
      maxx = std::max(maxx, feature.coordinates[i]);
      miny = std::min(miny, feature.coordinates[i + 1]);
      maxy = std::max(maxy, feature.coordinates[i + 1]);
    }
    if (wx < minx - tol || wx > maxx + tol || wy < miny - tol || wy > maxy + tol) return false;

    if (feature.part_starts.empty()) {
      return hitTestPart(feature.type, feature.coordinates, wx, wy, tol);
    }
    for (const auto& range : featurePartRanges(feature)) {
      if (hitTestPart(feature.type, Viewer::extractRange(feature, range), wx, wy, tol)) return true;
    }
    return false;
  }

  static double featureBBoxExtent(const Spatial::VectorFeature& feature) {
    if (feature.coordinates.size() < 2) return std::numeric_limits<double>::max();
    double minx = feature.coordinates[0], maxx = feature.coordinates[0];
    double miny = feature.coordinates[1], maxy = feature.coordinates[1];
    for (size_t i = 0; i < feature.coordinates.size(); i += 2) {
      minx = std::min(minx, feature.coordinates[i]);
      maxx = std::max(maxx, feature.coordinates[i]);
      miny = std::min(miny, feature.coordinates[i + 1]);
      maxy = std::max(maxy, feature.coordinates[i + 1]);
    }
    return (maxx - minx) + (maxy - miny);
  }

  // Shows live world coordinates under the cursor while the mouse is over
  // the map (matching the MapWindow reference), or falls back to the
  // viewport's visible bounding box when the mouse isn't over the widget.
  // Anchored to the bottom-right corner.
  void drawViewportCoordinates() {
    char buf[160];
    if (mouse_in_view_) {
      double wx = toWorldX(mouse_screen_x_);
      double wy = toWorldY(mouse_screen_y_);
      std::snprintf(buf, sizeof(buf), "X: %.6f, Y: %.6f", wx, wy);
    } else {
      double minx = toWorldX(x());
      double maxx = toWorldX(x() + w());
      double miny = toWorldY(y() + h());
      double maxy = toWorldY(y());
      std::snprintf(buf, sizeof(buf), "X: %.6f, %.6f    Y: %.6f, %.6f", minx, maxx, miny, maxy);
    }

    fl_font(FL_HELVETICA, 11);
    int tw = 0, th = 0;
    fl_measure(buf, tw, th, 0);

    const int pad_x = 6, pad_y = 4, margin = 6;
    int box_w = tw + pad_x * 2;
    int box_h = th + pad_y * 2;
    int box_x = x() + w() - box_w - margin;
    int box_y = y() + h() - box_h - margin;

    fl_push_clip(x(), y(), w(), h());
    fl_color(fl_rgb_color(255, 255, 255));
    fl_rectf(box_x, box_y, box_w, box_h);
    fl_color(fl_rgb_color(130, 130, 130));
    fl_rect(box_x, box_y, box_w, box_h);
    fl_color(FL_BLACK);
    fl_draw(buf, box_x + pad_x, box_y, box_w - pad_x * 2, box_h, FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    fl_pop_clip();
  }

  // Outline-only draw of one flat [x0,y0,x1,y1,...] part, picked by
  // `base_type` same as hitTestPart(). Shared by drawHighlightedFeature()
  // below and, per-part, by every MULTI* feature so each island/segment
  // gets its own outline instead of one shape with the parts wired
  // together.
  void drawPartOutline(Spatial::VectorFeature::GeometryType base_type,
                       const std::vector<double>& coords) {
    using Geom = Spatial::VectorFeature::GeometryType;
    if (coords.size() < 2) return;

    if (base_type == Geom::POINT || base_type == Geom::MULTIPOINT) {
      fl_circle(toScreenX(coords[0]), toScreenY(coords[1]), 8);
    } else if (base_type == Geom::LINESTRING || base_type == Geom::MULTILINESTRING) {
      fl_begin_line();
      for (size_t i = 0; i < coords.size(); i += 2) {
        fl_vertex(toScreenX(coords[i]), toScreenY(coords[i + 1]));
      }
      fl_end_line();
    } else if ((base_type == Geom::POLYGON || base_type == Geom::MULTIPOLYGON) && coords.size() >= 4) {
      fl_begin_line();
      for (size_t i = 0; i < coords.size(); i += 2) {
        fl_vertex(toScreenX(coords[i]), toScreenY(coords[i + 1]));
      }
      fl_vertex(toScreenX(coords[0]), toScreenY(coords[1]));
      fl_end_line();
    }
  }

  // Draws an outline around the currently highlighted feature (selected
  // either by clicking it on the map or by clicking its row in the
  // attribute table). Silently does nothing if the layer it belongs to is
  // no longer in layers_ (e.g. it was deleted).
  void drawHighlightedFeature() {
    if (!highlight_layer_ || !layers_) return;

    bool layer_present = false;
    for (const auto& l : *layers_) {
      if (l.get() == highlight_layer_) {
        layer_present = true;
        break;
      }
    }
    if (!layer_present || !highlight_layer_->visible || highlight_layer_->type != LayerType::VECTOR) return;

    const auto& features = highlight_layer_->vector_data.features;
    if (highlight_feature_ < 0 || highlight_feature_ >= (int)features.size()) return;

    const auto& feature = features[highlight_feature_];
    if (feature.coordinates.size() < 2) return;

    Fl_Color highlight_color = fl_rgb_color(255, 140, 0);  // orange

    fl_color(highlight_color);
    fl_line_style(FL_SOLID, 3);

    if (feature.part_starts.empty()) {
      drawPartOutline(feature.type, feature.coordinates);
    } else {
      for (const auto& range : featurePartRanges(feature)) {
        drawPartOutline(feature.type, Viewer::extractRange(feature, range));
      }
    }

    fl_line_style(0);
  }

  // Resolves the fill color to use for a vector feature: if the layer has
  // style_rules (loaded from a .sty sidecar), scans the feature's
  // attributes for one that falls in a rule's [min,max] range and returns
  // that rule's color; otherwise falls back to the layer's plain
  // fill_color. Mirrors spatial_svg's applyRules so a .sty renders the
  // same way in both tools -- see ADR-0004.
  Fl_Color resolveFillColor(const Layer& layer, const Spatial::VectorFeature& feature) {
    if (!layer.style_rules.empty()) {
      for (const auto& rule : layer.style_rules) {
        for (const auto& [attr_name, attr_value] : feature.attributes) {
          (void)attr_name;
          try {
            double value = std::stod(attr_value);
            if (value >= rule.min_val && value <= rule.max_val) {
              uchar r, g, b;
              if (parseHexColor(rule.color, r, g, b)) return fl_rgb_color(r, g, b);
            }
          } catch (...) {
          }
        }
      }
    }
    return layer.fill_color;
  }

  void drawVectorLayer(const Layer& layer) {
    for (size_t fi = 0; fi < layer.vector_data.features.size(); ++fi) {
      const auto& feature = layer.vector_data.features[fi];
      if (feature.coordinates.size() < 2) continue;

      fl_line_style(FL_SOLID, layer.line_width);
      Fl_Color fill_color = resolveFillColor(layer, feature);

      if (feature.part_starts.empty()) {
        drawFeaturePart(layer, feature.type, feature.coordinates, fill_color);
      } else {
        // Each part gets its own fl_begin_polygon()/fl_end_polygon() call,
        // so e.g. a MultiPolygon made of several separate islands fills
        // and outlines each island on its own -- no stray edge connecting
        // one island to the next, no self-intersecting combined shape.
        for (const auto& range : featurePartRanges(feature)) {
          drawFeaturePart(layer, feature.type, Viewer::extractRange(feature, range), fill_color);
        }
      }

      fl_line_style(0);
    }
  }

  // Draws one flat [x0,y0,x1,y1,...] part with the same fill+outline
  // styling drawVectorLayer() always used for a plain POINT/LINESTRING/
  // POLYGON feature. `base_type` is the owning feature's type, so a
  // MULTIPOINT/MULTILINESTRING/MULTIPOLYGON part is drawn exactly like its
  // singular counterpart -- see the Geom::MULTI* cases below.
  void drawFeaturePart(const Layer& layer, Spatial::VectorFeature::GeometryType base_type,
                       const std::vector<double>& coords, Fl_Color fill_color) {
    using Geom = Spatial::VectorFeature::GeometryType;
    if (coords.size() < 2) return;

    if (base_type == Geom::POINT || base_type == Geom::MULTIPOINT) {
      double cx = toScreenX(coords[0]);
      double cy = toScreenY(coords[1]);
      const double pr = 4.0;  // marker radius, in screen pixels

      if (layer.fill) {
        fl_color(fill_color);
        fl_pie((int)std::lround(cx - pr), (int)std::lround(cy - pr), (int)std::lround(pr * 2),
               (int)std::lround(pr * 2), 0, 360);
      }

      fl_color(layer.color);
      fl_circle(cx, cy, pr);
    } else if (base_type == Geom::LINESTRING || base_type == Geom::MULTILINESTRING) {
      fl_color(layer.color);
      fl_begin_line();
      for (size_t i = 0; i < coords.size(); i += 2) {
        fl_vertex(toScreenX(coords[i]), toScreenY(coords[i + 1]));
      }
      fl_end_line();
    } else if (base_type == Geom::POLYGON || base_type == Geom::MULTIPOLYGON) {
      if (coords.size() < 4) return;

      if (layer.fill) {
        fl_color(fill_color);
        fl_begin_polygon();
        for (size_t i = 0; i < coords.size(); i += 2) {
          fl_vertex(toScreenX(coords[i]), toScreenY(coords[i + 1]));
        }
        fl_end_polygon();
      }

      fl_color(layer.color);
      fl_begin_line();
      for (size_t i = 0; i < coords.size(); i += 2) {
        fl_vertex(toScreenX(coords[i]), toScreenY(coords[i + 1]));
      }
      fl_vertex(toScreenX(coords[0]), toScreenY(coords[1]));
      fl_end_line();
    }
  }

  void drawRasterLayer(const Layer& layer) {
    const auto& ds = layer.raster_data;
    if (ds.data.empty() || ds.ncols <= 0 || ds.nrows <= 0) return;

    double range = ds.max_val - ds.min_val;
    if (range < 1e-12) range = 1.0;

    // When a RAT color field is set, look up each cell's class color
    // instead of the default min/max gradient; the same value->RAT row
    // lookup also drives the optional cell label text below. Build the
    // value->row index cache once per draw (not per cell) for speed.
    bool use_rat_color = ds.has_rat && !layer.rat_color_field.empty();
    bool need_rat_index = use_rat_color || (layer.show_labels && ds.has_rat &&
                                             !layer.rat_label_field.empty());
    std::unordered_map<double, int> rat_index;
    if (need_rat_index) {
      for (size_t i = 0; i < ds.rat_rows.size(); ++i) {
        auto it = ds.rat_rows[i].find("value");
        if (it == ds.rat_rows[i].end()) continue;
        try {
          rat_index[std::stod(it->second)] = (int)i;
        } catch (...) {
        }
      }
    }

    for (int r = 0; r < ds.nrows; ++r) {
      for (int c = 0; c < ds.ncols; ++c) {
        double val = ds.at(r, c);
        if (std::abs(val - ds.nodata_value) < 1e-9) continue;

        double x_left = ds.xllcorner + c * ds.cellsize;
        double y_bottom = ds.yllcorner + (ds.nrows - 1 - r) * ds.cellsize;
        double x_right = x_left + ds.cellsize;
        double y_top = y_bottom + ds.cellsize;

        double sx1 = toScreenX(x_left);
        double sy1 = toScreenY(y_top);
        double sw = std::max(1.0, toScreenX(x_right) - sx1);
        double sh = std::max(1.0, toScreenY(y_bottom) - sy1);

        uchar r_col, g_col, b_col;
        bool colored = false;
        // A .sty sidecar (layer.style_rules) takes priority over both the
        // RAT color field and the default gradient below -- its classes
        // were precomputed by spatial_colormap specifically for this
        // layer, so they're the most specific style available. No
        // interpolation: each cell's value is matched against a class
        // range and that class's color used as-is (see ADR-0004).
        if (!layer.style_rules.empty()) {
          for (const auto& rule : layer.style_rules) {
            if (val >= rule.min_val && val <= rule.max_val) {
              colored = parseHexColor(rule.color, r_col, g_col, b_col);
              break;
            }
          }
        }
        if (!colored && use_rat_color) {
          auto idx_it = rat_index.find(val);
          if (idx_it != rat_index.end()) {
            auto col_it = ds.rat_rows[idx_it->second].find(layer.rat_color_field);
            if (col_it != ds.rat_rows[idx_it->second].end()) {
              colored = parseHexColor(col_it->second, r_col, g_col, b_col);
            }
          }
        }
        if (!colored) {
          valueToColor((val - ds.min_val) / range, r_col, g_col, b_col);
        }
        fl_color(r_col, g_col, b_col);
        fl_rectf((int)sx1, (int)sy1, (int)sw + 1, (int)sh + 1);

        // Skip the label if the cell is too small on screen to hold
        // legible text (common when zoomed out on a large grid).
        if (layer.show_labels && sw >= 16 && sh >= 12) {
          std::string text = rasterCellLabel(ds, layer, val, rat_index);
          drawLabelText((int)sx1, (int)sy1, (int)sw, (int)sh, text, FL_ALIGN_CENTER);
        }
      }
    }
  }

  // Text to show for a raster cell's label: the configured RAT label field
  // if there's a matching class, otherwise the raw cell value (formatted
  // without a trailing ".00" for whole numbers).
  static std::string rasterCellLabel(const Spatial::RasterDataset& ds, const Layer& layer,
                                      double val, const std::unordered_map<double, int>& rat_index) {
    if (ds.has_rat && !layer.rat_label_field.empty()) {
      auto idx_it = rat_index.find(val);
      if (idx_it != rat_index.end()) {
        auto col_it = ds.rat_rows[idx_it->second].find(layer.rat_label_field);
        if (col_it != ds.rat_rows[idx_it->second].end() && !col_it->second.empty()) {
          return col_it->second;
        }
      }
    }
    char buf[32];
    if (std::fabs(val - std::round(val)) < 1e-6) {
      std::snprintf(buf, sizeof(buf), "%.0f", val);
    } else {
      std::snprintf(buf, sizeof(buf), "%.2f", val);
    }
    return buf;
  }

  // Picks the biggest part of a MultiPolygon/MultiLineString/MultiPoint
  // feature to anchor its single label on -- e.g. the main island of a
  // region made of several, rather than an arbitrary or averaged point
  // that might land outside every part. "Biggest" is area for polygons,
  // length for lines, and simply the first point for MultiPoint (there's
  // no natural "biggest" for a bare point).
  static std::vector<double> largestPart(const Spatial::VectorFeature& feature) {
    using Geom = Spatial::VectorFeature::GeometryType;
    auto ranges = featurePartRanges(feature);
    size_t best = 0;
    double best_measure = -1.0;
    for (size_t i = 0; i < ranges.size(); ++i) {
      double measure = 0.0;  // MULTIPOINT: first part wins (no natural "biggest" point).
      if (feature.type == Geom::MULTIPOLYGON) {
        measure = polygonArea(Viewer::extractRange(feature, ranges[i]));
      } else if (feature.type == Geom::MULTILINESTRING) {
        measure = lineLength(Viewer::extractRange(feature, ranges[i]));
      }
      if (measure > best_measure) {
        best_measure = measure;
        best = i;
      }
    }
    return Viewer::extractRange(feature, ranges[best]);
  }

  // Draws a vector layer's labels: polygons at their centroid, points just
  // to the right of the marker, lines at their length-wise midpoint. For a
  // MULTI* feature with more than one part, the label anchors on the
  // largest part (see largestPart()) instead of averaging across every
  // part, which could land the label outside all of them (e.g. between two
  // separate islands).
  void drawVectorLabels(const Layer& layer) {
    using Geom = Spatial::VectorFeature::GeometryType;

    for (size_t fi = 0; fi < layer.vector_data.features.size(); ++fi) {
      const auto& feature = layer.vector_data.features[fi];
      if (feature.coordinates.size() < 2) continue;

      auto it = feature.attributes.find(layer.label_field);
      if (it == feature.attributes.end() || it->second.empty()) continue;

      std::vector<double> largest_part_storage;
      const std::vector<double>* anchor_coords = &feature.coordinates;
      if (!feature.part_starts.empty()) {
        largest_part_storage = largestPart(feature);
        anchor_coords = &largest_part_storage;
      }

      double lx = 0, ly = 0;
      bool is_point = false;
      if (feature.type == Geom::POINT || feature.type == Geom::MULTIPOINT) {
        lx = (*anchor_coords)[0];
        ly = (*anchor_coords)[1];
        is_point = true;
      } else if ((feature.type == Geom::POLYGON || feature.type == Geom::MULTIPOLYGON) &&
                 anchor_coords->size() >= 6) {
        polygonCentroid(*anchor_coords, lx, ly);
      } else if (feature.type == Geom::LINESTRING || feature.type == Geom::MULTILINESTRING) {
        lineMidpoint(*anchor_coords, lx, ly);
      } else {
        continue;
      }

      int sx = (int)toScreenX(lx);
      int sy = (int)toScreenY(ly);

      if (is_point) {
        drawLabelText(sx + 7, sy - 7, 160, 14, it->second, FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
      } else {
        drawLabelText(sx - 80, sy - 7, 160, 14, it->second, FL_ALIGN_CENTER);
      }
    }
  }

  // Shared label-drawing primitive: a thin white halo behind black text so
  // labels stay legible over any fill color or raster cell underneath.
  // Resets the font itself (rather than assuming the caller did) since
  // fl_font() is global FLTK drawing state shared with every other widget.
  static void drawLabelText(int x, int y, int w, int h, const std::string& text, Fl_Align align) {
    if (text.empty()) return;
    fl_font(FL_HELVETICA, 11);

    fl_color(FL_WHITE);
    for (int ox = -1; ox <= 1; ++ox) {
      for (int oy = -1; oy <= 1; ++oy) {
        if (ox == 0 && oy == 0) continue;
        fl_draw(text.c_str(), x + ox, y + oy, w, h, align);
      }
    }
    fl_color(FL_BLACK);
    fl_draw(text.c_str(), x, y, w, h, align);
  }

  std::vector<std::shared_ptr<Layer>>* layers_ = nullptr;
  double scale_x_ = 1.0, scale_y_ = 1.0;
  double offset_x_ = 0.0, offset_y_ = 0.0;
  bool has_data_ = false;
  double margin_ = 20.0;
  bool panning_ = false;
  double pan_start_x_ = 0, pan_start_y_ = 0;
  std::function<void(int, int)> feature_click_cb_;

  Layer* highlight_layer_ = nullptr;
  int highlight_feature_ = -1;

  double full_extent_w_ = 1.0, full_extent_h_ = 1.0;

  bool mouse_in_view_ = false;
  double mouse_screen_x_ = 0, mouse_screen_y_ = 0;
};

}  // namespace Viewer
