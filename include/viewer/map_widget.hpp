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
#include <FL/Fl_Image.H>
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

inline bool sameStyleRules(const std::vector<Spatial::StyleRule>& a,
                           const std::vector<Spatial::StyleRule>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].min_val != b[i].min_val || a[i].max_val != b[i].max_val || a[i].color != b[i].color) return false;
  }
  return true;
}

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

  void setFeatureClickCallback(std::function<void(int, int)> cb) {
    feature_click_cb_ = std::move(cb);
  }

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

    full_extent_w_ = world_w;
    full_extent_h_ = world_h;

    double avail_w = std::max(10.0, (double)w() - 2 * margin_);
    double avail_h = std::max(10.0, (double)h() - 2 * margin_);

    scale_x_ = std::min(avail_w / world_w, avail_h / world_h);
    scale_y_ = scale_x_;

    offset_x_ = g_minx - ((avail_w / scale_x_) - world_w) / 2.0;
    offset_y_ = g_miny - ((avail_h / scale_y_) - world_h) / 2.0;
  }

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

  // Read-only screen<->world coordinate transforms and view state, exposed
  // so a subclass can place new vertices exactly under the cursor and
  // hit-test existing ones (see spatial_editor.cpp, which digitizes
  // point/line/polygon features by clicking on the map). Pure passthroughs
  // to the private helpers below -- no behavior change for spatial_viewer.
  double screenToWorldX(double sx) const { return toWorldX(sx); }
  double screenToWorldY(double sy) const { return toWorldY(sy); }
  double worldToScreenX(double wx) const { return toScreenX(wx); }
  double worldToScreenY(double wy) const { return toScreenY(wy); }
  double currentScale() const { return scale_x_; }
  bool hasData() const { return has_data_; }

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

  void selectFeatureAt(int screen_x, int screen_y) {
    if (!layers_ || !has_data_ || !feature_click_cb_) return;

    double wx = toWorldX(screen_x);
    double wy = toWorldY(screen_y);
    double tol = 5.0 / std::max(scale_x_, 1e-9);

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
    if (pointInPolygon(wx, wy, coords)) return true;
    return pointToLineDistance(wx, wy, coords) <= tol;
  }

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

    Fl_Color highlight_color = fl_rgb_color(255, 140, 0);

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
        for (const auto& range : featurePartRanges(feature)) {
          drawFeaturePart(layer, feature.type, Viewer::extractRange(feature, range), fill_color);
        }
      }

      fl_line_style(0);
    }
  }

  void drawFeaturePart(const Layer& layer, Spatial::VectorFeature::GeometryType base_type,
                       const std::vector<double>& coords, Fl_Color fill_color) {
    using Geom = Spatial::VectorFeature::GeometryType;
    if (coords.size() < 2) return;

    if (base_type == Geom::POINT || base_type == Geom::MULTIPOINT) {
      double cx = toScreenX(coords[0]);
      double cy = toScreenY(coords[1]);
      const double pr = 4.0;

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
    if (ds.data.empty() || ds.ncols <= 0 || ds.nrows <= 0 || ds.cellsize <= 0.0) return;

    double range = ds.max_val - ds.min_val;
    if (range < 1e-12) range = 1.0;

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

    // --- Destination rectangle ---------------------------------------
    // The part of the widget the raster actually covers, clamped to the
    // widget itself. IMPORTANT: Fl_Image::draw(X,Y,W,H) does NOT scale the
    // image to fit W,H -- per FLTK's own docs it clips to that box and
    // draws the image at its *own* width/height. A bitmap built at raster-
    // cell resolution and handed a differently-sized screen rectangle (the
    // previous version of this function) therefore came out the wrong
    // size/position at any zoom level other than exactly 1 screen pixel
    // per cell -- worse the further you zoomed, which is the bug just
    // reported. The fix: render directly at *screen* resolution, one
    // sample per destination pixel, looked up in the raster via the
    // inverse view transform (nearest-neighbor) -- so the bitmap's native
    // size always equals the box it's drawn into, and draw() never needs
    // to scale anything.
    double ds_left = ds.xllcorner;
    double ds_right = ds.xllcorner + ds.ncols * ds.cellsize;
    double ds_bottom = ds.yllcorner;
    double ds_top = ds.yllcorner + ds.nrows * ds.cellsize;

    double sx_a = toScreenX(ds_left), sx_b = toScreenX(ds_right);
    double sy_a = toScreenY(ds_top), sy_b = toScreenY(ds_bottom);

    int screen_x1 = std::max(x(), (int)std::floor(std::min(sx_a, sx_b)));
    int screen_x2 = std::min(x() + w(), (int)std::ceil(std::max(sx_a, sx_b)));
    int screen_y1 = std::max(y(), (int)std::floor(std::min(sy_a, sy_b)));
    int screen_y2 = std::min(y() + h(), (int)std::ceil(std::max(sy_a, sy_b)));

    int dest_w = screen_x2 - screen_x1;
    int dest_h = screen_y2 - screen_y1;
    if (dest_w <= 0 || dest_h <= 0) return;

    // --- Bitmap cache -------------------------------------------------
    // Used to mean one fl_rectf() call per visible cell -- for a widget
    // full of raster at native resolution that is on the order of
    // widget_width * widget_height individual draw calls on *every*
    // redraw, and something as unrelated as dragging a vector vertex in
    // spatial_editor calls redraw() on every mouse-move. Colors are now
    // written into an RGBA buffer sized to the destination screen
    // rectangle (cheap memory writes, no FLTK call overhead) and blitted
    // once as a single Fl_RGB_Image at its native size; the buffer is
    // cached on the layer itself and only rebuilt when the view (pan/zoom/
    // resize) or the raster's style actually changed since the last
    // redraw.
    auto& cache = layer.raster_cache;
    bool cache_ok = cache.valid && cache.pixel_w == dest_w && cache.pixel_h == dest_h &&
                    cache.scale_x == scale_x_ && cache.offset_x == offset_x_ &&
                    cache.offset_y == offset_y_ && cache.widget_x == x() && cache.widget_y == y() &&
                    cache.widget_w == w() && cache.widget_h == h() &&
                    cache.use_rat_color == use_rat_color &&
                    cache.rat_color_field == layer.rat_color_field &&
                    sameStyleRules(cache.style_rules, layer.style_rules);

    if (!cache_ok) {
      cache.rgba.assign((size_t)dest_w * dest_h * 4, 0);
      for (int py = 0; py < dest_h; ++py) {
        double wy = toWorldY(screen_y1 + py);
        // Inverse of: y_bottom(row) = yllcorner + (nrows-1-row)*cellsize.
        // NOTE: must floor the cell-units distance *before* subtracting
        // from (nrows-1), not the other way around (floor(a-b) != a -
        // floor(b) in general) -- getting this order wrong is exactly what
        // caused the misalignment.
        int row = (ds.nrows - 1) - (int)std::floor((wy - ds.yllcorner) / ds.cellsize);
        if (row < 0 || row >= ds.nrows) continue;

        for (int px = 0; px < dest_w; ++px) {
          double wx = toWorldX(screen_x1 + px);
          int col = (int)std::floor((wx - ds.xllcorner) / ds.cellsize);
          if (col < 0 || col >= ds.ncols) continue;

          double val = ds.at(row, col);
          if (std::abs(val - ds.nodata_value) < 1e-9) continue;  // alpha stays 0: transparent

          uchar r_col, g_col, b_col;
          bool colored = false;
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
          size_t idx = ((size_t)py * dest_w + px) * 4;
          cache.rgba[idx + 0] = r_col;
          cache.rgba[idx + 1] = g_col;
          cache.rgba[idx + 2] = b_col;
          cache.rgba[idx + 3] = 255;
        }
      }

      cache.pixel_w = dest_w;
      cache.pixel_h = dest_h;
      cache.scale_x = scale_x_;
      cache.offset_x = offset_x_;
      cache.offset_y = offset_y_;
      cache.widget_x = x();
      cache.widget_y = y();
      cache.widget_w = w();
      cache.widget_h = h();
      cache.use_rat_color = use_rat_color;
      cache.rat_color_field = layer.rat_color_field;
      cache.style_rules = layer.style_rules;
      cache.valid = true;
    }

    Fl_RGB_Image img(cache.rgba.data(), dest_w, dest_h, 4);
    img.draw(screen_x1, screen_y1, dest_w, dest_h);

    // Per-cell value labels are only shown once cells are large enough on
    // screen to matter, using the same viewport-cell math as before this
    // change -- kept local to this block since the color fill above no
    // longer iterates raster cells at all.
    if (layer.show_labels && ds.cellsize * scale_x_ >= 12.0) {
      double view_left = std::min(toWorldX(x()), toWorldX(x() + w()));
      double view_right = std::max(toWorldX(x()), toWorldX(x() + w()));
      double view_bottom = std::min(toWorldY(y()), toWorldY(y() + h()));
      double view_top = std::max(toWorldY(y()), toWorldY(y() + h()));

      int col_min = std::clamp((int)std::floor((view_left - ds.xllcorner) / ds.cellsize) - 1, 0,
                                ds.ncols - 1);
      int col_max = std::clamp((int)std::ceil((view_right - ds.xllcorner) / ds.cellsize) + 1, 0,
                                ds.ncols - 1);
      int row_min = std::clamp(
          (ds.nrows - 1) - (int)std::ceil((view_top - ds.yllcorner) / ds.cellsize) - 1, 0, ds.nrows - 1);
      int row_max = std::clamp(
          (ds.nrows - 1) - (int)std::floor((view_bottom - ds.yllcorner) / ds.cellsize) + 1, 0,
          ds.nrows - 1);

      for (int r = row_min; r <= row_max; ++r) {
        for (int c = col_min; c <= col_max; ++c) {
          double val = ds.at(r, c);
          if (std::abs(val - ds.nodata_value) < 1e-9) continue;

          double x_left = ds.xllcorner + c * ds.cellsize;
          double y_bottom = ds.yllcorner + (ds.nrows - 1 - r) * ds.cellsize;
          double sx1 = toScreenX(x_left);
          double sy1 = toScreenY(y_bottom + ds.cellsize);
          double sw = std::max(1.0, toScreenX(x_left + ds.cellsize) - sx1);
          double sh = std::max(1.0, toScreenY(y_bottom) - sy1);
          if (sw < 16 || sh < 12) continue;

          std::string text = rasterCellLabel(ds, layer, val, rat_index);
          drawLabelText((int)sx1, (int)sy1, (int)sw, (int)sh, text, FL_ALIGN_CENTER);
        }
      }
    }
  }

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

  static std::vector<double> largestPart(const Spatial::VectorFeature& feature) {
    using Geom = Spatial::VectorFeature::GeometryType;
    auto ranges = featurePartRanges(feature);
    size_t best = 0;
    double best_measure = -1.0;
    for (size_t i = 0; i < ranges.size(); ++i) {
      double measure = 0.0;
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

}
