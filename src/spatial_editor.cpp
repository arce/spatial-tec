// spatial_editor -- FLTK graphical tool to create and edit spatial vector
// objects (points, lines and polygons) and save them in this project's
// CSV+WKT vector format (the same one spatial_viewer, spatial_csv2geo and
// the rest of the vector tools read and write; see core/spatial_csv.hpp /
// spatial_io.hpp).
//
// This tool started as a copy of spatial_viewer.cpp and reuses its shared
// building blocks -- Viewer::Layer (include/viewer/layer.hpp) and
// Viewer::MapWidget (include/viewer/map_widget.hpp), which already render
// vector layers and handle pan/zoom -- instead of reimplementing them.
// map_widget.hpp gained a handful of small public read-only accessors
// (screen<->world transforms, current scale, hasData()) for this file to
// use; they are pure passthroughs and don't change spatial_viewer's own
// behavior. See adr/0007-editor-tool.md for the full rationale, including
// what's deliberately out of scope for this first version (a single
// editable layer, no background reference layers, whole-feature delete
// rather than per-vertex insert/remove).
//
// Editing model: a single Viewer::Layer is "the" editable layer at a time
// (new, or loaded from an existing CSV). Three draw tools add features by
// clicking on the map -- Point commits immediately, Line/Polygon accumulate
// vertices until finished (double-click, Enter, or the "Finish shape"
// button) -- and a Select/Edit tool clicks a feature to select it, drags
// any of its vertices to reshape it, and deletes it (Delete key or button).
// Attribute columns are user-defined per layer (a simple comma-separated
// list when creating a new layer, or whatever a loaded CSV already has)
// and edited through a small fixed-size form on the right.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Return_Button.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>

#include "core/spatial_csv.hpp"
#include "core/spatial_geom.hpp"
#include "core/spatial_io.hpp"
#include "core/spatial_types.hpp"
#include "viewer/layer.hpp"
#include "viewer/map_widget.hpp"

namespace Editor {

enum class DrawMode { SELECT, POINT, LINE, POLYGON };

inline const char* typeName(Spatial::VectorFeature::GeometryType t) {
  using GT = Spatial::VectorFeature::GeometryType;
  switch (t) {
    case GT::POINT: return "Point";
    case GT::LINESTRING: return "Line";
    case GT::POLYGON: return "Polygon";
    case GT::MULTIPOINT: return "MultiPoint";
    case GT::MULTILINESTRING: return "MultiLine";
    case GT::MULTIPOLYGON: return "MultiPolygon";
  }
  return "?";
}

inline std::string trimStr(const std::string& s) {
  size_t start = s.find_first_not_of(" \t\r\n");
  size_t end = s.find_last_not_of(" \t\r\n");
  return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

// Grows a dataset's bounding box to include one feature's coordinates,
// initializing it on the very first coordinate seen (VectorDataset starts
// with has_bbox=false and min/max at 0, which would otherwise corrupt the
// box for data that doesn't happen to straddle the origin).
inline void growBBox(Spatial::VectorDataset& ds, const Spatial::VectorFeature& f) {
  for (size_t i = 0; i + 1 < f.coordinates.size(); i += 2) {
    double px = f.coordinates[i];
    double py = f.coordinates[i + 1];
    if (!ds.has_bbox) {
      ds.min_x = ds.max_x = px;
      ds.min_y = ds.max_y = py;
      ds.has_bbox = true;
    } else {
      ds.min_x = std::min(ds.min_x, px);
      ds.max_x = std::max(ds.max_x, px);
      ds.min_y = std::min(ds.min_y, py);
      ds.max_y = std::max(ds.max_y, py);
    }
  }
}

inline void recomputeBBox(Spatial::VectorDataset& ds) {
  ds.has_bbox = false;
  for (const auto& f : ds.features) growBBox(ds, f);
}

// Distance from (wx, wy) to a polygon's boundary, including the closing
// segment from the last vertex back to the first -- pointToLineDistance
// (core/spatial_geom.hpp) treats its input as an open polyline, so a click
// near the edge that closes the ring wouldn't otherwise register.
inline double polygonBoundaryDistance(double wx, double wy, const std::vector<double>& coords) {
  double d = pointToLineDistance(wx, wy, coords);
  size_t n = coords.size() / 2;
  if (n >= 2) {
    d = std::min(d, pointToSegmentDistance(wx, wy, coords[(n - 1) * 2], coords[(n - 1) * 2 + 1], coords[0],
                                            coords[1]));
  }
  return d;
}

inline bool hitTestEditable(const Spatial::VectorFeature& f, double wx, double wy, double tol) {
  using GT = Spatial::VectorFeature::GeometryType;
  if (f.coordinates.size() < 2) return false;
  if (f.type == GT::POINT) return pointToPointDistance(wx, wy, f.coordinates[0], f.coordinates[1]) <= tol;
  if (f.type == GT::LINESTRING) return pointToLineDistance(wx, wy, f.coordinates) <= tol;
  if (f.type == GT::POLYGON) {
    if (f.coordinates.size() >= 6 && pointInPolygon(wx, wy, f.coordinates)) return true;
    return polygonBoundaryDistance(wx, wy, f.coordinates) <= tol;
  }
  return false;  // multi-part types aren't produced by this tool
}

// MapWidget subclass that adds digitizing on top of the existing pan/zoom/
// rendering it already provides for any Viewer::Layer. It doesn't touch
// the base class's private state; it only reads it back through the small
// set of accessors map_widget.hpp now exposes publicly.
class EditorMapWidget : public Viewer::MapWidget {
 public:
  EditorMapWidget(int X, int Y, int W, int H) : Viewer::MapWidget(X, Y, W, H) {}

  void setEditLayer(Viewer::Layer* layer) {
    edit_layer_ = layer;
    selected_feature_ = -1;
    cancelPending();
  }

  void setMode(DrawMode m) {
    mode_ = m;
    cancelPending();
  }
  DrawMode mode() const { return mode_; }

  void cancelPending() {
    pending_.clear();
    have_mouse_pos_ = false;
    redraw();
  }

  void finishPending() {
    if (!edit_layer_) return;
    if (mode_ == DrawMode::LINE && pending_.size() >= 2) {
      commitFeature(Spatial::VectorFeature::GeometryType::LINESTRING);
    } else if (mode_ == DrawMode::POLYGON && pending_.size() >= 3) {
      commitFeature(Spatial::VectorFeature::GeometryType::POLYGON);
    }
    pending_.clear();
    have_mouse_pos_ = false;
    redraw();
  }

  int selectedFeature() const { return selected_feature_; }

  void selectFeature(int idx) {
    if (!edit_layer_ || idx < 0 || idx >= (int)edit_layer_->vector_data.features.size()) idx = -1;
    if (idx == selected_feature_) return;
    selected_feature_ = idx;
    redraw();
    if (on_change_) on_change_();
  }

  void deleteSelected() {
    if (!edit_layer_ || selected_feature_ < 0) return;
    auto& feats = edit_layer_->vector_data.features;
    if (selected_feature_ >= (int)feats.size()) return;
    feats.erase(feats.begin() + selected_feature_);
    edit_layer_->vector_data.feature_count = feats.size();
    recomputeBBox(edit_layer_->vector_data);
    selected_feature_ = -1;
    redraw();
    if (on_change_) on_change_();
  }

  void setOnChange(std::function<void()> cb) { on_change_ = std::move(cb); }

 protected:
  void draw() override {
    MapWidget::draw();
    drawPendingOverlay();
    drawSelectionHandles();
  }

  int handle(int event) override {
    switch (event) {
      case FL_PUSH: {
        Fl::focus(this);
        if (!Fl::event_inside(x(), y(), w(), h())) break;

        if (Fl::event_button() == FL_MIDDLE_MOUSE) {
          mid_panning_ = true;
          mid_pan_x_ = Fl::event_x();
          mid_pan_y_ = Fl::event_y();
          return 1;
        }

        if (Fl::event_button() == FL_LEFT_MOUSE && edit_layer_) {
          if (mode_ == DrawMode::POINT) {
            addPointFeature(screenToWorldX(Fl::event_x()), screenToWorldY(Fl::event_y()));
            return 1;
          }
          if (mode_ == DrawMode::LINE || mode_ == DrawMode::POLYGON) {
            bool double_click = Fl::event_clicks() > 0;
            if (double_click && !pending_.empty()) {
              finishPending();
              return 1;
            }
            pending_.push_back({screenToWorldX(Fl::event_x()), screenToWorldY(Fl::event_y())});
            redraw();
            return 1;
          }
          if (mode_ == DrawMode::SELECT) {
            if (handleSelectPush()) return 1;
          }
        }
        break;
      }
      case FL_DRAG: {
        if (mid_panning_) {
          pan(Fl::event_x() - mid_pan_x_, Fl::event_y() - mid_pan_y_);
          mid_pan_x_ = Fl::event_x();
          mid_pan_y_ = Fl::event_y();
          return 1;
        }
        if (dragging_vertex_) {
          moveSelectedVertex(screenToWorldX(Fl::event_x()), screenToWorldY(Fl::event_y()));
          return 1;
        }
        break;
      }
      case FL_RELEASE: {
        if (mid_panning_) {
          mid_panning_ = false;
          return 1;
        }
        if (dragging_vertex_) {
          dragging_vertex_ = false;
          recomputeBBox(edit_layer_->vector_data);
          if (on_change_) on_change_();
          return 1;
        }
        break;
      }
      case FL_MOVE: {
        if ((mode_ == DrawMode::LINE || mode_ == DrawMode::POLYGON) && !pending_.empty()) {
          mouse_wx_ = screenToWorldX(Fl::event_x());
          mouse_wy_ = screenToWorldY(Fl::event_y());
          have_mouse_pos_ = true;
          redraw();
        }
        break;
      }
      case FL_KEYDOWN: {
        int key = Fl::event_key();
        if ((key == FL_Enter || key == FL_KP_Enter) && !pending_.empty()) {
          finishPending();
          return 1;
        }
        if (key == FL_Escape && !pending_.empty()) {
          cancelPending();
          return 1;
        }
        if ((key == FL_Delete || key == FL_BackSpace) && mode_ == DrawMode::SELECT && selected_feature_ >= 0) {
          deleteSelected();
          return 1;
        }
        break;
      }
      case FL_FOCUS:
      case FL_UNFOCUS:
        return 1;
      default:
        break;
    }
    return MapWidget::handle(event);
  }

 private:
  void addPointFeature(double wx, double wy) {
    pending_ = {{wx, wy}};
    commitFeature(Spatial::VectorFeature::GeometryType::POINT);
    pending_.clear();
  }

  void commitFeature(Spatial::VectorFeature::GeometryType type) {
    if (!edit_layer_) return;
    bool was_empty = edit_layer_->vector_data.features.empty();

    Spatial::VectorFeature f;
    f.type = type;
    f.coordinates.reserve(pending_.size() * 2);
    for (const auto& p : pending_) {
      f.coordinates.push_back(p.first);
      f.coordinates.push_back(p.second);
    }
    for (const auto& col : edit_layer_->vector_data.columns) {
      if (col != edit_layer_->vector_data.geometry_column) f.attributes[col] = "";
    }

    growBBox(edit_layer_->vector_data, f);
    edit_layer_->vector_data.features.push_back(std::move(f));
    edit_layer_->vector_data.feature_count = edit_layer_->vector_data.features.size();
    int idx = (int)edit_layer_->vector_data.features.size() - 1;
    selected_feature_ = idx;

    if (was_empty) {
      // Bootstraps a sane initial view for a brand new layer: zoomToFeature
      // already pads a single feature's (possibly zero-area) extent by a
      // sensible margin, which plain fitToExtent() does not.
      zoomToFeature(edit_layer_, idx);
    }
    redraw();
    if (on_change_) on_change_();
  }

  bool handleSelectPush() {
    if (!edit_layer_) return false;
    double click_sx = Fl::event_x();
    double click_sy = Fl::event_y();
    const double kVertexPx = 7.0;

    // First, try to grab a vertex of the ALREADY selected feature -- takes
    // priority over re-selecting so a vertex sitting on top of another
    // feature can still be dragged.
    if (selected_feature_ >= 0 && selected_feature_ < (int)edit_layer_->vector_data.features.size()) {
      auto& f = edit_layer_->vector_data.features[selected_feature_];
      for (size_t i = 0; i + 1 < f.coordinates.size(); i += 2) {
        double sx = worldToScreenX(f.coordinates[i]);
        double sy = worldToScreenY(f.coordinates[i + 1]);
        if (std::hypot(sx - click_sx, sy - click_sy) <= kVertexPx) {
          dragging_vertex_ = true;
          dragging_vertex_index_ = (int)(i / 2);
          return true;
        }
      }
    }

    double wx = screenToWorldX(click_sx);
    double wy = screenToWorldY(click_sy);
    double tol = kVertexPx / std::max(currentScale(), 1e-9);

    const auto& feats = edit_layer_->vector_data.features;
    int best = -1;
    double best_extent = std::numeric_limits<double>::max();
    for (int i = 0; i < (int)feats.size(); ++i) {
      if (!hitTestEditable(feats[i], wx, wy, tol)) continue;
      double minx = feats[i].coordinates[0], maxx = minx, miny = feats[i].coordinates[1], maxy = miny;
      for (size_t k = 0; k + 1 < feats[i].coordinates.size(); k += 2) {
        minx = std::min(minx, feats[i].coordinates[k]);
        maxx = std::max(maxx, feats[i].coordinates[k]);
        miny = std::min(miny, feats[i].coordinates[k + 1]);
        maxy = std::max(maxy, feats[i].coordinates[k + 1]);
      }
      double extent = (maxx - minx) + (maxy - miny);
      if (extent < best_extent) {
        best_extent = extent;
        best = i;
      }
    }

    if (best != selected_feature_) {
      selected_feature_ = best;
      redraw();
      if (on_change_) on_change_();
    }
    return best >= 0;
  }

  void moveSelectedVertex(double wx, double wy) {
    if (!edit_layer_ || selected_feature_ < 0) return;
    auto& feats = edit_layer_->vector_data.features;
    if (selected_feature_ >= (int)feats.size()) return;
    auto& coords = feats[selected_feature_].coordinates;
    size_t base = (size_t)dragging_vertex_index_ * 2;
    if (base + 1 >= coords.size()) return;
    coords[base] = wx;
    coords[base + 1] = wy;
    redraw();
  }

  void drawPendingOverlay() {
    if (pending_.empty()) return;
    fl_color(fl_rgb_color(0, 140, 0));
    fl_line_style(FL_SOLID, 2);
    fl_begin_line();
    for (const auto& p : pending_) fl_vertex(worldToScreenX(p.first), worldToScreenY(p.second));
    if (have_mouse_pos_) fl_vertex(worldToScreenX(mouse_wx_), worldToScreenY(mouse_wy_));
    fl_end_line();
    fl_line_style(0);

    fl_color(fl_rgb_color(0, 140, 0));
    for (const auto& p : pending_) {
      double sx = worldToScreenX(p.first), sy = worldToScreenY(p.second);
      fl_pie(sx - 3, sy - 3, 6, 6, 0, 360);
    }
  }

  void drawSelectionHandles() {
    if (!edit_layer_ || selected_feature_ < 0) return;
    const auto& feats = edit_layer_->vector_data.features;
    if (selected_feature_ >= (int)feats.size()) return;
    const auto& f = feats[selected_feature_];

    fl_color(fl_rgb_color(230, 0, 230));
    for (size_t i = 0; i + 1 < f.coordinates.size(); i += 2) {
      double sx = worldToScreenX(f.coordinates[i]);
      double sy = worldToScreenY(f.coordinates[i + 1]);
      fl_rectf((int)std::lround(sx) - 3, (int)std::lround(sy) - 3, 7, 7);
    }
  }

  DrawMode mode_ = DrawMode::SELECT;
  Viewer::Layer* edit_layer_ = nullptr;

  std::vector<std::pair<double, double>> pending_;
  bool have_mouse_pos_ = false;
  double mouse_wx_ = 0.0, mouse_wy_ = 0.0;

  int selected_feature_ = -1;
  bool dragging_vertex_ = false;
  int dragging_vertex_index_ = -1;

  bool mid_panning_ = false;
  double mid_pan_x_ = 0.0, mid_pan_y_ = 0.0;

  std::function<void()> on_change_;
};

// Small modal asking for the attribute column names of a brand new layer
// (the geometry column is implicit and always added automatically).
class NewLayerDialog : public Fl_Double_Window {
 public:
  NewLayerDialog() : Fl_Double_Window(400, 160, "New Editable Layer") {
    Fl_Box* info = new Fl_Box(10, 10, 380, 50,
                               "Attribute column names for the new layer, separated by "
                               "commas (the geometry column is added automatically):");
    info->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);

    columns_input_ = new Fl_Input(10, 66, 380, 26);
    columns_input_->value("name");

    Fl_Return_Button* ok = new Fl_Return_Button(120, 110, 90, 32, "OK");
    ok->callback(cbOk, this);
    Fl_Button* cancel = new Fl_Button(220, 110, 90, 32, "Cancel");
    cancel->callback(cbCancel, this);

    set_modal();
    end();
  }

  bool accepted = false;
  std::string columns_text;

 private:
  static void cbOk(Fl_Widget*, void* d) {
    auto* dlg = static_cast<NewLayerDialog*>(d);
    dlg->columns_text = dlg->columns_input_->value() ? dlg->columns_input_->value() : "";
    dlg->accepted = true;
    dlg->hide();
  }
  static void cbCancel(Fl_Widget*, void* d) { static_cast<NewLayerDialog*>(d)->hide(); }

  Fl_Input* columns_input_ = nullptr;
};

// Main window: map on the left (pan/zoom/digitizing), tool + feature +
// attribute panel on the right.
class MainWindow : public Fl_Double_Window {
 public:
  static constexpr int kMaxAttrRows = 6;

  MainWindow(int W, int H, const char* L = nullptr) : Fl_Double_Window(W, H, L) {
    const int mb_h = 26;
    menu_bar_ = new Fl_Menu_Bar(0, 0, W, mb_h);
    menu_bar_->add("&File/&New Layer...", 0, cbNewLayer, this);
    menu_bar_->add("&File/&Open Layer...", 0, cbOpenLayer, this);
    menu_bar_->add("&File/&Save", 0, cbSaveLayer, this);
    menu_bar_->add("&File/Save &As...", 0, cbSaveLayerAs, this, FL_MENU_DIVIDER);
    // The literal "/" in the label has to be escaped ("\/") so FLTK's menu
    // path parser doesn't read it as a submenu separator.
    menu_bar_->add("&Draw/&Select \\/ Edit", "s", cbModeSelect, this, FL_MENU_RADIO | FL_MENU_VALUE);
    menu_bar_->add("&Draw/&Point", "p", cbModePoint, this, FL_MENU_RADIO);
    menu_bar_->add("&Draw/&Line", "l", cbModeLine, this, FL_MENU_RADIO);
    menu_bar_->add("&Draw/Poly&gon", "g", cbModePolygon, this, FL_MENU_RADIO | FL_MENU_DIVIDER);
    menu_bar_->add("&Draw/&Finish shape", 0, cbFinishShape, this);
    menu_bar_->add("&Draw/&Cancel shape", 0, cbCancelShape, this);

    canvas_ = new EditorMapWidget(10, 10 + mb_h, W - 300, H - 20 - mb_h);
    canvas_->setOnChange([this] { onEditorChange(); });

    const int px = W - 280;
    const int pw = 270;
    int y = 10 + mb_h;

    mode_label_ = new Fl_Box(px, y, pw, 20, "Mode: Select / Edit");
    mode_label_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    mode_label_->labelfont(FL_HELVETICA_BOLD);
    y += 20 + 4;

    help_box_ = new Fl_Box(px, y, pw, 96,
                            "Point: click to place.\n"
                            "Line / Polygon: click to add vertices; "
                            "double-click, Enter or \"Finish shape\" to finish; Esc cancels.\n"
                            "Select / Edit: click a feature to select it, drag a "
                            "square handle to move a vertex, Delete removes it.\n"
                            "Wheel: zoom. Middle-drag: pan.");
    help_box_->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    help_box_->labelsize(11);
    help_box_->labelfont(FL_HELVETICA_ITALIC);
    y += 96 + 10;

    Fl_Box* features_label = new Fl_Box(px, y, pw, 16, "Features");
    features_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += 16 + 4;

    feature_list_ = new Fl_Hold_Browser(px, y, pw, 140);
    {
      static int widths[] = {24, 70, 60, 0};
      feature_list_->column_widths(widths);
    }
    feature_list_->callback(cbFeatureSelect, this);
    y += 140 + 6;

    btn_delete_feature_ = new Fl_Button(px, y, pw, 26, "Delete selected feature");
    btn_delete_feature_->callback(cbDeleteFeature, this);
    y += 26 + 14;

    Fl_Box* columns_label = new Fl_Box(px, y, pw, 16, "Attribute columns");
    columns_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += 16 + 4;

    column_list_ = new Fl_Hold_Browser(px, y, pw, 70);
    y += 70 + 6;

    input_new_column_ = new Fl_Input(px, y, pw - 90, 24);
    btn_add_column_ = new Fl_Button(px + pw - 86, y, 86, 24, "Add");
    btn_add_column_->callback(cbAddColumn, this);
    y += 24 + 6;

    btn_remove_column_ = new Fl_Button(px, y, pw, 24, "Remove selected column");
    btn_remove_column_->callback(cbRemoveColumn, this);
    y += 24 + 14;

    Fl_Box* attrs_label = new Fl_Box(px, y, pw, 16, "Selected feature attributes");
    attrs_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += 16 + 6;

    for (int i = 0; i < kMaxAttrRows; ++i) {
      attr_label_[i] = new Fl_Box(px, y, 80, 22, "");
      attr_label_[i]->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
      attr_input_[i] = new Fl_Input(px + 84, y, pw - 84, 22);
      attr_label_[i]->hide();
      attr_input_[i]->hide();
      y += 22 + 4;
    }

    btn_update_attrs_ = new Fl_Button(px, y, pw, 26, "Update attributes");
    btn_update_attrs_->callback(cbUpdateAttrs, this);
    btn_update_attrs_->deactivate();
    y += 26 + 14;

    status_label_ = new Fl_Box(px, y, pw, std::max(50, H - y - 10), "Create or open a layer to begin.");
    status_label_->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    status_label_->box(FL_BORDER_BOX);

    size_range(820, 560);
    resizable(canvas_);
    end();
    show();
    Fl::focus(canvas_);
  }

 private:
  // ---- static callbacks ----
  static void cbNewLayer(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->newLayer(); }
  static void cbOpenLayer(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->openLayer(); }
  static void cbSaveLayer(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->saveLayer(); }
  static void cbSaveLayerAs(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->saveLayerAs(); }

  static void cbModeSelect(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->setMode(DrawMode::SELECT); }
  static void cbModePoint(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->setMode(DrawMode::POINT); }
  static void cbModeLine(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->setMode(DrawMode::LINE); }
  static void cbModePolygon(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->setMode(DrawMode::POLYGON); }
  static void cbFinishShape(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->canvas_->finishPending(); }
  static void cbCancelShape(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->canvas_->cancelPending(); }

  static void cbFeatureSelect(Fl_Widget*, void* d) {
    auto* win = static_cast<MainWindow*>(d);
    int row = win->feature_list_->value();
    win->canvas_->selectFeature(row - 1);
    win->canvas_->redraw();
  }
  static void cbDeleteFeature(Fl_Widget*, void* d) {
    auto* win = static_cast<MainWindow*>(d);
    if (win->canvas_->selectedFeature() < 0) {
      fl_alert("Select a feature from the list, or on the map, first.");
      return;
    }
    win->canvas_->deleteSelected();
  }
  static void cbAddColumn(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->addColumn(); }
  static void cbRemoveColumn(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->removeColumn(); }
  static void cbUpdateAttrs(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->updateSelectedAttributes(); }

  // ---- actions ----
  void setMode(DrawMode m) {
    canvas_->setMode(m);
    canvas_->redraw();
    switch (m) {
      case DrawMode::SELECT: mode_label_->label("Mode: Select / Edit"); break;
      case DrawMode::POINT: mode_label_->label("Mode: Point"); break;
      case DrawMode::LINE: mode_label_->label("Mode: Line"); break;
      case DrawMode::POLYGON: mode_label_->label("Mode: Polygon"); break;
    }
  }

  bool confirmDiscard() {
    if (!dirty_) return true;
    return fl_choice("Discard unsaved changes to the current layer?", "Cancel", "Discard", nullptr) == 1;
  }

  std::vector<std::string> attributeColumns() const {
    std::vector<std::string> cols;
    if (!edit_layer_) return cols;
    for (const auto& c : edit_layer_->vector_data.columns) {
      if (c != edit_layer_->vector_data.geometry_column) cols.push_back(c);
    }
    return cols;
  }

  void bindLayer(std::shared_ptr<Viewer::Layer> layer) {
    layers_.clear();
    layers_.push_back(layer);
    edit_layer_ = layer;
    canvas_->setEditLayer(edit_layer_.get());
    canvas_->setLayers(&layers_);
    dirty_ = false;
    refreshColumnList();
    refreshFeatureList();
    refreshAttributeInputs();
  }

  void newLayer() {
    if (!confirmDiscard()) return;
    NewLayerDialog dlg;
    dlg.show();
    while (dlg.shown()) Fl::wait();
    if (!dlg.accepted) return;

    std::vector<std::string> cols;
    std::istringstream ss(dlg.columns_text);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
      tok = trimStr(tok);
      if (!tok.empty()) cols.push_back(tok);
    }
    if (cols.empty()) cols.push_back("name");

    auto layer = std::make_shared<Viewer::Layer>();
    layer->type = Viewer::LayerType::VECTOR;
    layer->name = "untitled";
    layer->color = FL_BLUE;
    layer->fill_color = FL_BLUE;
    layer->vector_data.columns = cols;
    layer->vector_data.geometry_column = "geometry";
    layer->vector_data.columns.push_back("geometry");

    current_filename_.clear();
    bindLayer(layer);
    updateStatus("New layer ready. Pick a Draw tool and click on the map.");
  }

  void openLayer() {
    if (!confirmDiscard()) return;
    Fl_File_Chooser chooser(current_directory_.c_str(), "CSV files (*.csv)", Fl_File_Chooser::SINGLE,
                             "Open editable layer");
    chooser.show();
    while (chooser.shown()) Fl::wait();
    if (chooser.count() <= 0 || !chooser.value()) return;
    std::string filename = chooser.value();
    current_directory_ = std::filesystem::path(filename).parent_path().string();

    auto layer = std::make_shared<Viewer::Layer>();
    layer->type = Viewer::LayerType::VECTOR;
    layer->filename = filename;
    layer->name = std::filesystem::path(filename).stem().string();
    layer->color = FL_BLUE;
    layer->fill_color = FL_BLUE;

    Spatial::SpatialCSVReader reader;
    if (!reader.read(filename, layer->vector_data)) {
      fl_alert("Could not read vector file: %s", filename.c_str());
      return;
    }
    if (layer->vector_data.geometry_column.empty()) layer->vector_data.geometry_column = "geometry";
    if (std::find(layer->vector_data.columns.begin(), layer->vector_data.columns.end(),
                  layer->vector_data.geometry_column) == layer->vector_data.columns.end()) {
      layer->vector_data.columns.push_back(layer->vector_data.geometry_column);
    }

    current_filename_ = filename;
    bindLayer(layer);
    updateStatus("Loaded: " + filename + "  (" + std::to_string(layer->vector_data.features.size()) +
                 " features).");
  }

  void saveLayerToPath(const std::string& filename) {
    writeVectorCSV(edit_layer_->vector_data, filename);
    current_filename_ = filename;
    edit_layer_->filename = filename;
    dirty_ = false;
    updateStatus("Saved: " + filename);
  }

  void saveLayer() {
    if (!edit_layer_) {
      fl_alert("Create or open a layer first.");
      return;
    }
    if (current_filename_.empty()) {
      saveLayerAs();
      return;
    }
    saveLayerToPath(current_filename_);
  }

  void saveLayerAs() {
    if (!edit_layer_) {
      fl_alert("Create or open a layer first.");
      return;
    }
    Fl_File_Chooser chooser(current_directory_.c_str(), "CSV files (*.csv)", Fl_File_Chooser::CREATE,
                             "Save layer as");
    chooser.show();
    while (chooser.shown()) Fl::wait();
    if (chooser.count() <= 0 || !chooser.value()) return;
    std::string filename = chooser.value();
    if (filename.size() < 4 || filename.substr(filename.size() - 4) != ".csv") filename += ".csv";
    current_directory_ = std::filesystem::path(filename).parent_path().string();
    saveLayerToPath(filename);
  }

  void addColumn() {
    if (!edit_layer_) {
      fl_alert("Create or open a layer first.");
      return;
    }
    std::string col = input_new_column_->value() ? trimStr(input_new_column_->value()) : "";
    if (col.empty()) return;
    auto& ds = edit_layer_->vector_data;
    if (col == ds.geometry_column || std::find(ds.columns.begin(), ds.columns.end(), col) != ds.columns.end()) {
      fl_alert("That column name is already in use.");
      return;
    }
    auto geom_it = std::find(ds.columns.begin(), ds.columns.end(), ds.geometry_column);
    ds.columns.insert(geom_it, col);
    for (auto& f : ds.features) f.attributes[col] = "";
    input_new_column_->value("");
    refreshColumnList();
    refreshAttributeInputs();
    markDirty();
  }

  void removeColumn() {
    if (!edit_layer_) return;
    int row = column_list_->value();
    if (row <= 0) {
      fl_alert("Select a column from the list first.");
      return;
    }
    std::string col = column_list_->text(row);
    auto& ds = edit_layer_->vector_data;
    ds.columns.erase(std::remove(ds.columns.begin(), ds.columns.end(), col), ds.columns.end());
    for (auto& f : ds.features) f.attributes.erase(col);
    refreshColumnList();
    refreshAttributeInputs();
    markDirty();
  }

  void refreshColumnList() {
    column_list_->clear();
    for (const auto& c : attributeColumns()) column_list_->add(c.c_str());
  }

  void refreshFeatureList() {
    feature_list_->clear();
    if (!edit_layer_) return;
    const auto& feats = edit_layer_->vector_data.features;
    for (size_t i = 0; i < feats.size(); ++i) {
      int nverts = (int)(feats[i].coordinates.size() / 2);
      char line[128];
      std::snprintf(line, sizeof(line), "%d\t%s\t%d pt", (int)(i + 1), typeName(feats[i].type), nverts);
      feature_list_->add(line);
    }
    int sel = canvas_->selectedFeature();
    if (sel >= 0 && sel < (int)feats.size()) {
      feature_list_->select(sel + 1);
    }
  }

  void refreshAttributeInputs() {
    auto cols = attributeColumns();
    int sel = canvas_ ? canvas_->selectedFeature() : -1;
    const Spatial::VectorFeature* feature = nullptr;
    if (edit_layer_ && sel >= 0 && sel < (int)edit_layer_->vector_data.features.size()) {
      feature = &edit_layer_->vector_data.features[sel];
    }

    for (int i = 0; i < kMaxAttrRows; ++i) {
      if (i < (int)cols.size()) {
        std::string label = cols[i] + ":";
        attr_label_[i]->copy_label(label.c_str());
        attr_label_[i]->show();
        attr_input_[i]->show();
        std::string value;
        if (feature) {
          auto it = feature->attributes.find(cols[i]);
          if (it != feature->attributes.end()) value = it->second;
        }
        attr_input_[i]->value(value.c_str());
        if (feature)
          attr_input_[i]->activate();
        else
          attr_input_[i]->deactivate();
      } else {
        attr_label_[i]->hide();
        attr_input_[i]->hide();
      }
    }
    btn_update_attrs_->activate();
    if (!feature) btn_update_attrs_->deactivate();
  }

  void updateSelectedAttributes() {
    int sel = canvas_->selectedFeature();
    if (!edit_layer_ || sel < 0 || sel >= (int)edit_layer_->vector_data.features.size()) {
      fl_alert("Select a feature first.");
      return;
    }
    auto cols = attributeColumns();
    auto& feature = edit_layer_->vector_data.features[sel];
    for (int i = 0; i < (int)cols.size() && i < kMaxAttrRows; ++i) {
      const char* v = attr_input_[i]->value();
      feature.attributes[cols[i]] = v ? v : "";
    }
    markDirty();
    updateStatus("Attributes updated for feature " + std::to_string(sel + 1) + ".");
  }

  void onEditorChange() {
    markDirty();
    refreshFeatureList();
    refreshAttributeInputs();
  }

  void markDirty() { dirty_ = true; }

  void updateStatus(const std::string& text) { status_label_->copy_label(text.c_str()); }

  Fl_Menu_Bar* menu_bar_ = nullptr;
  EditorMapWidget* canvas_ = nullptr;
  Fl_Box* mode_label_ = nullptr;
  Fl_Box* help_box_ = nullptr;
  Fl_Hold_Browser* feature_list_ = nullptr;
  Fl_Button* btn_delete_feature_ = nullptr;
  Fl_Hold_Browser* column_list_ = nullptr;
  Fl_Input* input_new_column_ = nullptr;
  Fl_Button* btn_add_column_ = nullptr;
  Fl_Button* btn_remove_column_ = nullptr;
  Fl_Box* attr_label_[kMaxAttrRows] = {nullptr};
  Fl_Input* attr_input_[kMaxAttrRows] = {nullptr};
  Fl_Button* btn_update_attrs_ = nullptr;
  Fl_Box* status_label_ = nullptr;

  std::vector<std::shared_ptr<Viewer::Layer>> layers_;
  std::shared_ptr<Viewer::Layer> edit_layer_;
  std::string current_filename_;
  std::string current_directory_ = ".";
  bool dirty_ = false;
};

}  // namespace Editor

int main(int argc, char** argv) {
  Fl::scheme("gtk+");
  Editor::MainWindow win(1200, 820, "Spatial Editor - Spatial TEC");

  if (argc > 1) {
    // Placeholder for symmetry with the other tools' argv handling; opening
    // a file still goes through File > Open Layer... today.
    (void)argv;
  }

  return Fl::run();
}
