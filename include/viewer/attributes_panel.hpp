#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include <FL/Fl.H>
#include <FL/Fl_Table.H>
#include <FL/fl_draw.H>

#include "core/spatial_geom.hpp"
#include "viewer/layer.hpp"

namespace Viewer {

// Read-only "Attribute / Value" grid for whatever feature is currently
// selected (via a map click or a table row click). Only shows columns that
// are currently visible in the attribute table (i.e. not in
// Layer::hidden_columns), per the Table/Hide column and Table/Show all
// columns menu options.
class AttributesPanel : public Fl_Table {
public:
  AttributesPanel(int X, int Y, int W, int H, const char* L = 0)
      : Fl_Table(X, Y, W, H, L) {
    rows(0);
    cols(2);
    col_width(0, 140);
    col_width(1, 200);
    row_height_all(24);
    col_header(1);
    col_header_height(24);
    row_header(0);
    col_resize(1);
    col_resize_min(50);
  }

  // Shows the given feature's visible attributes. `layer` may be null to
  // clear the panel.
  void showFeature(Layer* layer, int feature_index) {
    layer_ = layer;
    feature_index_ = feature_index;
    refresh();
  }

  void clear() {
    layer_ = nullptr;
    feature_index_ = -1;
    refresh();
  }

  // Rebuilds the row list from the current layer/feature. Safe to call
  // whenever column visibility changes elsewhere (e.g. Table/Hide column)
  // to keep this panel in sync with whatever is currently displayed.
  void refresh() {
    entries_.clear();

    if (layer_ && layer_->type == LayerType::VECTOR && feature_index_ >= 0 &&
        feature_index_ < (int)layer_->vector_data.features.size()) {
      const auto& feature = layer_->vector_data.features[feature_index_];
      for (const auto& col : layer_->vector_data.columns) {
        if (layer_->hidden_columns.count(col)) continue;

        std::string value;
        if (col == layer_->vector_data.geometry_column) {
          value = geometryToWKT(feature);
        } else {
          auto it = feature.attributes.find(col);
          value = (it != feature.attributes.end()) ? it->second : "";
        }
        entries_.emplace_back(col, value);
      }
    } else if (layer_ && layer_->type == LayerType::RASTER && layer_->raster_data.has_rat &&
               feature_index_ >= 0 && feature_index_ < (int)layer_->raster_data.rat_rows.size()) {
      // feature_index_ is a RAT row index here (one row per class), not a
      // per-cell feature -- see MapWidget::rasterRatRowAt() / TableWidget's
      // raster branch of updateTableData(), which use the same indexing.
      const auto& row = layer_->raster_data.rat_rows[feature_index_];
      for (const auto& col : layer_->raster_data.rat_columns) {
        if (layer_->hidden_columns.count(col)) continue;
        auto it = row.find(col);
        entries_.emplace_back(col, it != row.end() ? it->second : "");
      }
    }

    rows((int)entries_.size());
    if (!entries_.empty()) distributeColumnWidths();
    redraw();
  }

  void draw_cell(TableContext context, int R, int C, int X, int Y, int W, int H) override {
    if (context == CONTEXT_CELL) {
      drawDataCell(R, C, X, Y, W, H);
    } else if (context == CONTEXT_COL_HEADER) {
      drawColHeader(C, X, Y, W, H);
    }
  }

  // Without this, the two columns keep their absolute pixel widths when the
  // panel is resized (via the Fl_Tile divider or the window itself), so
  // resizing just grows a scrollbar instead of the columns filling the new
  // space. Rescale proportionally, preserving the Attribute/Value split
  // ratio (including any the user dragged by hand).
  void resize(int X, int Y, int W, int H) override {
    Fl_Table::resize(X, Y, W, H);
    rescaleColumnWidths();
  }

private:
  void distributeColumnWidths() {
    int avail = w();
    if (avail <= 0) return;
    int c0 = std::max(80, avail * 2 / 5);
    int c1 = std::max(80, avail - c0);
    col_width(0, c0);
    col_width(1, c1);
  }

  void rescaleColumnWidths() {
    int n = cols();
    if (n <= 0) return;

    int avail = w();
    if (avail <= 0) return;

    int old_total = 0;
    for (int c = 0; c < n; ++c) old_total += col_width(c);
    if (old_total <= 0) return;

    double scale = (double)avail / (double)old_total;
    if (std::abs(scale - 1.0) < 1e-6) return;

    const int min_w = 40;
    int running = 0;
    for (int c = 0; c < n; ++c) {
      int neww;
      if (c == n - 1) {
        neww = std::max(min_w, avail - running);
      } else {
        neww = std::max(min_w, (int)std::lround(col_width(c) * scale));
      }
      col_width(c, neww);
      running += neww;
    }
  }

  void drawDataCell(int R, int C, int X, int Y, int W, int H) {
    if (R >= (int)entries_.size() || C > 1) return;

    fl_color(R % 2 == 0 ? FL_WHITE : fl_rgb_color(246, 246, 246));
    fl_rectf(X, Y, W, H);
    fl_color(FL_LIGHT2);
    fl_rect(X, Y, W, H);

    const std::string& text = (C == 0) ? entries_[R].first : entries_[R].second;
    fl_color(C == 0 ? FL_BLACK : fl_rgb_color(40, 40, 40));
    if (C == 0) fl_font(FL_HELVETICA_BOLD, 12);
    else fl_font(FL_HELVETICA, 12);

    fl_push_clip(X + 2, Y, W - 4, H);
    fl_draw(text.c_str(), X + 6, Y, W - 10, H, FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    fl_pop_clip();
  }

  void drawColHeader(int C, int X, int Y, int W, int H) {
    fl_color(FL_GRAY);
    fl_rectf(X, Y, W, H);
    fl_color(FL_BLACK);
    fl_rect(X, Y, W, H);
    fl_font(FL_HELVETICA_BOLD, 12);
    const char* label = (C == 0) ? "Attribute" : "Value";
    fl_draw(label, X + 6, Y, W - 10, H, FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  }

  Layer* layer_ = nullptr;
  int feature_index_ = -1;
  std::vector<std::pair<std::string, std::string>> entries_;
};

}  // namespace Viewer
