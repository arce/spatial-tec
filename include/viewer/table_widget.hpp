#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <FL/Fl.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Table.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>

#include "core/spatial_geom.hpp"
#include "core/spatial_string.hpp"
#include "viewer/layer.hpp"

namespace Viewer {

class TableWidget : public Fl_Table {
public:
  TableWidget(int X, int Y, int W, int H, const char* L = 0)
      : Fl_Table(X, Y, W, H, L) {
    begin();
    input_editor_ = new Fl_Input(0, 0, 0, 0);
    input_editor_->hide();
    input_editor_->when(FL_WHEN_ENTER_KEY_ALWAYS | FL_WHEN_RELEASE);
    input_editor_->callback(inputCallback, this);
    end();

    rows(0);
    cols(0);
    row_height_all(25);
    col_width_all(110);
    col_header(1);
    row_header(1);
    col_header_height(25);
    row_header_width(40);
    col_resize(1);
    col_resize_min(30);
  }

  void setLayers(std::vector<std::shared_ptr<Layer>>* l) {
    if (editing_) doneEditing();
    layers_ = l;
    refresh();
  }

  void setCurrentLayer(int index) {
    if (editing_) doneEditing();
    current_layer_index_ = index;
    refresh();
  }

  // Called whenever the user clicks a row (a cell or the row header),
  // reporting the row index so the map can highlight that feature.
  void setRowSelectCallback(std::function<void(int)> cb) { row_select_cb_ = std::move(cb); }

  void refresh() {
    if (editing_) doneEditing();
    updateTableData();

    // Raster RAT rows are classes, not sequential features, so when a label
    // field is configured, show it in the row header instead of "1,2,3..."
    // -- that needs a wider row header than the default numeric one. Set
    // this before distributeColumnWidths() below, since it accounts for
    // row_header_width() when spreading columns across the available width.
    Layer* raster_layer = currentRasterLayer();
    bool show_row_labels = raster_layer && raster_layer->raster_data.has_rat &&
                            !raster_layer->rat_label_field.empty();
    row_header_width(show_row_labels ? 100 : 40);

    rows((int)cell_data_.size());
    cols((int)columns_.size());
    if (columns_ != last_layout_columns_) {
      distributeColumnWidths();
      last_layout_columns_ = columns_;
    }

    redraw();
  }

  // Highlights and scrolls to the given feature row (used to keep the table
  // in sync when a feature is picked on the map).
  void selectRow(int row) {
    if (row < 0 || row >= (int)cell_data_.size()) return;
    if (editing_) doneEditing();
    selected_row_ = row;
    row_position(std::max(0, row - 3));
    redraw();
  }

  // Hides the currently selected column from the attribute table (it can be
  // restored again with showAllColumns()). Works for both vector attribute
  // columns and raster RAT columns.
  void hideColumn() {
    Layer* layer = currentLayer();
    if (!layer) return;
    if (selected_col_ < 0 || selected_col_ >= (int)columns_.size()) {
      fl_message_title("Notice");
      fl_message("Select a column to hide.");
      return;
    }
    if ((int)columns_.size() <= 1) {
      fl_message_title("Notice");
      fl_message("Cannot hide the only remaining column.");
      return;
    }

    layer->hidden_columns.insert(columns_[selected_col_]);
    selected_col_ = -1;
    refresh();
  }

  // Restores every column previously hidden for the current layer.
  void showAllColumns() {
    Layer* layer = currentLayer();
    if (!layer) return;
    if (layer->hidden_columns.empty()) return;

    layer->hidden_columns.clear();
    refresh();
  }

  void addRow() {
    Layer* layer = currentVectorLayer();
    if (!layer) return;

    Spatial::VectorFeature feat;
    feat.type = Spatial::VectorFeature::GeometryType::POINT;
    feat.coordinates = {0.0, 0.0};
    for (const auto& col : layer->vector_data.columns) {
      if (col != layer->vector_data.geometry_column) feat.attributes[col] = "";
    }

    layer->vector_data.features.push_back(feat);
    layer->vector_data.feature_count = layer->vector_data.features.size();
    selected_row_ = (int)layer->vector_data.features.size() - 1;
    refresh();
  }

  void deleteRow() {
    Layer* layer = currentVectorLayer();
    if (!layer) return;
    if (selected_row_ < 0 || selected_row_ >= (int)layer->vector_data.features.size()) {
      fl_message_title("Notice");
      fl_message("Select a row to delete.");
      return;
    }

    layer->vector_data.features.erase(layer->vector_data.features.begin() + selected_row_);
    layer->vector_data.feature_count = layer->vector_data.features.size();
    if (selected_row_ >= (int)layer->vector_data.features.size()) {
      selected_row_ = (int)layer->vector_data.features.size() - 1;
    }
    refresh();
  }

  void addColumn() {
    Layer* layer = currentVectorLayer();
    if (!layer) return;

    fl_message_title("New Column");
    const char* name = fl_input("New column name:", "");
    if (!name || std::string(name).empty()) return;

    std::string col_name = trim(name);
    for (const auto& c : layer->vector_data.columns) {
      if (c == col_name) {
        fl_message_title("Error");
        fl_message("Column '%s' already exists.", col_name.c_str());
        return;
      }
    }

    layer->vector_data.columns.push_back(col_name);
    for (auto& feat : layer->vector_data.features) feat.attributes[col_name] = "";
    refresh();
    // Select the new column within the (possibly filtered) visible list.
    auto it = std::find(columns_.begin(), columns_.end(), col_name);
    selected_col_ = (it != columns_.end()) ? (int)std::distance(columns_.begin(), it) : -1;
    redraw();
  }

  void deleteColumn() {
    Layer* layer = currentVectorLayer();
    if (!layer) return;
    if (selected_col_ < 0 || selected_col_ >= (int)columns_.size()) {
      fl_message_title("Notice");
      fl_message("Select a column to delete.");
      return;
    }

    std::string col_name = columns_[selected_col_];
    if (col_name == layer->vector_data.geometry_column) {
      fl_message_title("Error");
      fl_message("Cannot delete the geometry column.");
      return;
    }

    auto& cols = layer->vector_data.columns;
    cols.erase(std::remove(cols.begin(), cols.end(), col_name), cols.end());
    layer->hidden_columns.erase(col_name);
    for (auto& feat : layer->vector_data.features) feat.attributes.erase(col_name);
    refresh();
    if (selected_col_ >= (int)columns_.size()) selected_col_ = (int)columns_.size() - 1;
    redraw();
  }

  void draw_cell(TableContext context, int R, int C, int X, int Y, int W, int H) override {
    if (context == CONTEXT_STARTPAGE) {
      // Other widgets (the attributes panel, the layer list) set their own
      // font sizes when they draw; since this table never used to set its
      // own, it would inherit whatever they left behind. Reset it once per
      // redraw pass so the table's text size stays consistent regardless of
      // what was drawn most recently elsewhere in the window.
      fl_font(FL_HELVETICA, 12);
    } else if (context == CONTEXT_CELL) {
      drawDataCell(R, C, X, Y, W, H);
    } else if (context == CONTEXT_COL_HEADER) {
      drawColHeader(C, X, Y, W, H);
    } else if (context == CONTEXT_ROW_HEADER) {
      drawRowHeader(R, X, Y, W, H);
    }
  }

  // Column widths are absolute pixel values by default, so without this
  // Fl_Table just grows a horizontal scrollbar when the surrounding panel
  // is resized instead of the columns filling the new space. Rescale them
  // proportionally (keeping relative widths, including any the user
  // dragged by hand) whenever the widget itself is resized.
  void resize(int X, int Y, int W, int H) override {
    Fl_Table::resize(X, Y, W, H);
    rescaleColumnWidths();
  }

  int handle(int event) override {
    if (event == FL_PUSH) {
      int R, C;
      ResizeFlag resize_flag;
      TableContext context = cursor2rowcol(R, C, resize_flag);

      if (context == CONTEXT_CELL || context == CONTEXT_ROW_HEADER || context == CONTEXT_COL_HEADER) {
        selected_row_ = R;
        selected_col_ = C;
        redraw();
      }

      // Only CELL/ROW_HEADER clicks actually pick a row (COL_HEADER always
      // reports R==0, which isn't a real row selection).
      if ((context == CONTEXT_CELL || context == CONTEXT_ROW_HEADER) && row_select_cb_ &&
          R >= 0 && R < (int)cell_data_.size()) {
        row_select_cb_(R);
      }

      if (context == CONTEXT_CELL) {
        if (editing_ && (R != edit_row_ || C != edit_col_)) doneEditing();
        if (Fl::event_clicks() > 0) {
          startEditing(R, C);
          return 1;
        }
      } else if (editing_) {
        doneEditing();
      }
    } else if (event == FL_KEYBOARD) {
      if (Fl::event_key() == FL_Escape && editing_) {
        editing_ = false;
        input_editor_->hide();
        redraw();
        return 1;
      }
    }
    return Fl_Table::handle(event);
  }

private:
  Layer* currentLayer() {
    if (!layers_ || current_layer_index_ < 0 || current_layer_index_ >= (int)layers_->size()) {
      return nullptr;
    }
    return (*layers_)[current_layer_index_].get();
  }

  Layer* currentVectorLayer() {
    Layer* layer = currentLayer();
    return (layer && layer->type == LayerType::VECTOR) ? layer : nullptr;
  }

  Layer* currentRasterLayer() {
    Layer* layer = currentLayer();
    return (layer && layer->type == LayerType::RASTER) ? layer : nullptr;
  }

  void updateTableData() {
    columns_.clear();
    cell_data_.clear();

    if (Layer* layer = currentVectorLayer()) {
      // Geometry is hidden by default the first time a layer's columns are
      // seen (it's rarely useful to look at raw WKT in the grid); the user
      // can bring it back via Table/Show all columns.
      if (!layer->geometry_hidden_initialized) {
        if (!layer->vector_data.geometry_column.empty()) {
          layer->hidden_columns.insert(layer->vector_data.geometry_column);
        }
        layer->geometry_hidden_initialized = true;
      }

      for (const auto& col : layer->vector_data.columns) {
        if (layer->hidden_columns.count(col)) continue;
        columns_.push_back(col);
      }

      for (const auto& feature : layer->vector_data.features) {
        std::vector<std::string> row;
        for (const auto& col : columns_) {
          if (col == layer->vector_data.geometry_column) {
            row.push_back(geometryToWKT(feature));
            continue;
          }
          auto it = feature.attributes.find(col);
          row.push_back(it != feature.attributes.end() ? it->second : "");
        }
        cell_data_.push_back(row);
      }
      return;
    }

    if (Layer* layer = currentRasterLayer()) {
      if (!layer->raster_data.has_rat) return;

      for (const auto& col : layer->raster_data.rat_columns) {
        if (layer->hidden_columns.count(col)) continue;
        columns_.push_back(col);
      }

      for (const auto& row : layer->raster_data.rat_rows) {
        std::vector<std::string> r;
        for (const auto& col : columns_) {
          auto it = row.find(col);
          r.push_back(it != row.end() ? it->second : "");
        }
        cell_data_.push_back(r);
      }
    }
  }

  // Spreads column widths so the table fills 100% of the available
  // horizontal space instead of leaving a blank gap after the last column.
  void distributeColumnWidths() {
    int n = (int)columns_.size();
    if (n <= 0) return;

    int avail = w() - row_header_width();
    if (avail <= 0) return;

    const int min_w = 90;
    int width = avail / n;
    if (width < min_w) width = min_w;

    for (int c = 0; c < n; ++c) col_width(c, width);
  }

  // Rescales all existing column widths so they still add up to the
  // available width, preserving their relative proportions (rather than
  // resetting them to an even split, which would undo manual resizing).
  void rescaleColumnWidths() {
    int n = cols();
    if (n <= 0) return;

    int avail = w() - row_header_width();
    if (avail <= 0) return;

    int old_total = 0;
    for (int c = 0; c < n; ++c) old_total += col_width(c);
    if (old_total <= 0) return;

    double scale = (double)avail / (double)old_total;
    if (std::abs(scale - 1.0) < 1e-6) return;

    const int min_w = 30;
    int running = 0;
    for (int c = 0; c < n; ++c) {
      int neww;
      if (c == n - 1) {
        // Last column absorbs any rounding remainder so the total exactly
        // matches the available width (no leftover gap or overhang).
        neww = std::max(min_w, avail - running);
      } else {
        neww = std::max(min_w, (int)std::lround(col_width(c) * scale));
      }
      col_width(c, neww);
      running += neww;
    }
  }

  void startEditing(int R, int C) {
    if (editing_) doneEditing();
    if (R < 0 || R >= (int)cell_data_.size() || C < 0 || C >= (int)columns_.size()) return;

    Layer* layer = currentVectorLayer();
    if (!layer || columns_[C] == layer->vector_data.geometry_column) return;

    edit_row_ = R;
    edit_col_ = C;
    editing_ = true;

    int X, Y, W, H;
    find_cell(CONTEXT_CELL, R, C, X, Y, W, H);
    input_editor_->resize(X, Y, W, H);
    input_editor_->value(cell_data_[R][C].c_str());
    input_editor_->show();
    input_editor_->take_focus();
    redraw();
  }

  void doneEditing() {
    if (!editing_) return;
    editing_ = false;
    input_editor_->hide();

    Layer* layer = currentVectorLayer();
    if (layer && edit_row_ >= 0 && edit_row_ < (int)cell_data_.size() &&
        edit_col_ >= 0 && edit_col_ < (int)columns_.size() &&
        edit_row_ < (int)layer->vector_data.features.size()) {
      std::string col_name = columns_[edit_col_];
      std::string new_val = input_editor_->value() ? input_editor_->value() : "";
      layer->vector_data.features[edit_row_].attributes[col_name] = new_val;
      cell_data_[edit_row_][edit_col_] = new_val;
    }
    redraw();
  }

  void drawDataCell(int R, int C, int X, int Y, int W, int H) {
    if (R >= (int)cell_data_.size() || C >= (int)columns_.size()) return;

    fl_color(R == selected_row_ ? fl_rgb_color(230, 240, 255) : FL_WHITE);
    fl_rectf(X, Y, W, H);
    fl_color(FL_LIGHT2);
    fl_rect(X, Y, W, H);

    if (R == selected_row_ && C == selected_col_) {
      fl_color(FL_BLUE);
      fl_rect(X + 1, Y + 1, W - 2, H - 2);
    }

    fl_color(FL_BLACK);
    fl_font(FL_HELVETICA, 12);
    fl_push_clip(X + 2, Y, W - 4, H);
    fl_draw(cell_data_[R][C].c_str(), X + 4, Y, W - 8, H, FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    fl_pop_clip();
  }

  void drawColHeader(int C, int X, int Y, int W, int H) {
    if (C >= (int)columns_.size()) return;

    fl_color(C == selected_col_ ? fl_rgb_color(150, 180, 210) : FL_GRAY);
    fl_rectf(X, Y, W, H);
    fl_color(FL_BLACK);
    fl_rect(X, Y, W, H);
    fl_font(FL_HELVETICA_BOLD, 12);
    fl_push_clip(X + 2, Y, W - 4, H);
    fl_draw(columns_[C].c_str(), X + 4, Y, W - 8, H, FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    fl_pop_clip();
  }

  void drawRowHeader(int R, int X, int Y, int W, int H) {
    fl_color(FL_GRAY);
    fl_rectf(X, Y, W, H);
    fl_color(FL_BLACK);
    fl_rect(X, Y, W, H);
    fl_font(FL_HELVETICA, 12);

    std::string label = rowHeaderLabel(R);
    fl_push_clip(X + 2, Y, W - 4, H);
    fl_draw(label.c_str(), X + 4, Y, W - 8, H, FL_ALIGN_CENTER);
    fl_pop_clip();
  }

  // Row headers are normally just the 1-based row number. For a raster RAT
  // table with a label field configured, show that class's label instead
  // (e.g. "Forest" rather than "2") so the row header is actually useful
  // for identifying which class each row is.
  std::string rowHeaderLabel(int R) {
    if (Layer* layer = currentRasterLayer()) {
      if (layer->raster_data.has_rat && !layer->rat_label_field.empty() && R >= 0 &&
          R < (int)layer->raster_data.rat_rows.size()) {
        auto it = layer->raster_data.rat_rows[R].find(layer->rat_label_field);
        if (it != layer->raster_data.rat_rows[R].end() && !it->second.empty()) {
          return it->second;
        }
      }
    }
    char label[16];
    snprintf(label, sizeof(label), "%d", R + 1);
    return label;
  }

  static void inputCallback(Fl_Widget*, void* d) {
    static_cast<TableWidget*>(d)->doneEditing();
  }

  std::vector<std::shared_ptr<Layer>>* layers_ = nullptr;
  int current_layer_index_ = -1;
  std::vector<std::string> columns_;
  std::vector<std::vector<std::string>> cell_data_;
  std::vector<std::string> last_layout_columns_;

  Fl_Input* input_editor_ = nullptr;
  int edit_row_ = -1, edit_col_ = -1;
  bool editing_ = false;
  int selected_row_ = -1, selected_col_ = -1;

  std::function<void(int)> row_select_cb_;
};

}  // namespace Viewer
