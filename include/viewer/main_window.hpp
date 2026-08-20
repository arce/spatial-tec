#pragma once

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Color_Chooser.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Tile.H>
#include <FL/fl_message.H>

#include "core/ascii_grid.hpp"
#include "core/spatial_csv.hpp"
#include "core/spatial_io.hpp"
#include "core/spatial_style.hpp"
#include "viewer/attributes_panel.hpp"
#include "viewer/dock_panel.hpp"
#include "viewer/layer.hpp"
#include "viewer/layer_list_widget.hpp"
#include "viewer/map_widget.hpp"
#include "viewer/table_widget.hpp"

namespace Viewer {

class MainWindow : public Fl_Double_Window {
public:
  MainWindow(int W, int H, const char* L = 0) : Fl_Double_Window(W, H, L) {
    current_directory_ = std::filesystem::current_path().string();
    setupMenuBar();
    setupLayout();
    end();
    show();
  }

  void loadFileFromArgs(const std::string& filename) { loadFile(filename); }

  void resize(int X, int Y, int W, int H) override {
    Fl_Double_Window::resize(X, Y, W, H);
    if (menu_bar_) menu_bar_->size(W, layout_menu_h_);
  }

private:
  void setupMenuBar() {
    menu_bar_ = new Fl_Menu_Bar(0, 0, w(), 25);
    menu_bar_->add("File/Quit", FL_COMMAND + 'q', cbMenuQuit, this);
    menu_bar_->add("Layers/Load layer...", FL_COMMAND + 'o', cbMenuOpen, this);
    menu_bar_->add("Layers/Save layer", FL_COMMAND + 's', cbMenuSave, this);
    menu_bar_->add("Layers/Save layer as...", FL_COMMAND + 'S', cbMenuSaveAs, this,FL_MENU_DIVIDER);
    menu_bar_->add("Layers/Edit layer...", 0, cbLayerEdit, this);
    menu_bar_->add("Layers/Delete layer", 0, cbLayerRemove, this,FL_MENU_DIVIDER);
    menu_bar_->add("Layers/Move up", FL_COMMAND + 'U', cbLayerMoveUp, this);
    menu_bar_->add("Layers/Move down", FL_COMMAND + 'D', cbLayerMoveDown, this);
    menu_bar_->add("View/Zoom in", FL_COMMAND + '+', cbZoomIn, this);
    menu_bar_->add("View/Zoom out", FL_COMMAND + '-', cbZoomOut, this);
    menu_bar_->add("View/Zoom extent", FL_COMMAND + '0', cbFitExtent, this);
    menu_bar_->add("Table/Add row", 0, cbTableAddRow, this);
    menu_bar_->add("Table/Delete row", 0, cbTableDeleteRow, this, FL_MENU_DIVIDER);
    menu_bar_->add("Table/Add column", 0, cbTableAddColumn, this);
    menu_bar_->add("Table/Delete column", 0, cbTableDeleteColumn, this, FL_MENU_DIVIDER);
    menu_bar_->add("Table/Hide column", 0, cbTableHideColumn, this);
    menu_bar_->add("Table/Show all columns", 0, cbTableShowAllColumns, this);

    mi_panel_layers_ = menu_bar_->add("Panels/Layers", 0, cbTogglePanelLayers, this,
                                       FL_MENU_TOGGLE | FL_MENU_VALUE);
    mi_panel_attributes_ = menu_bar_->add("Panels/Attributes", 0, cbTogglePanelAttributes, this,
                                           FL_MENU_TOGGLE);
    mi_panel_map_ = menu_bar_->add("Panels/Map", 0, cbTogglePanelMap, this,
                                    FL_MENU_TOGGLE | FL_MENU_VALUE);
    mi_panel_table_ = menu_bar_->add("Panels/Table", 0, cbTogglePanelTable, this,
                                      FL_MENU_TOGGLE | FL_MENU_VALUE);

    menu_bar_->add("Help/About...", 0, cbMenuAbout, this);
  }

  void setupLayout() {
    layout_menu_h_ = 25;

    int body_y = layout_menu_h_;
    int body_h = h() - layout_menu_h_;
    int left_w = (int)(w() * 0.25);
    int right_x = left_w;
    int right_w = w() - left_w;

    left_split_h_ = (int)(body_h * 0.55);
    int layer_h = left_split_h_;
    int attrs_h = body_h - layer_h;

    right_split_h_ = (int)(body_h * 0.68);
    int map_h = right_split_h_;
    int table_h = body_h - map_h;

    Fl_Tile* outer_tile = new Fl_Tile(0, body_y, w(), body_h);

    left_tile_ = new Fl_Tile(0, body_y, left_w, body_h);
    setupLayerPanel(0, body_y, left_w, layer_h);
    setupAttributesPanel(0, body_y + layer_h, left_w, attrs_h);
    left_tile_->end();

    right_tile_ = new Fl_Tile(right_x, body_y, right_w, body_h);
    setupMapPanel(right_x, body_y, right_w, map_h);
    setupTablePanel(right_x, body_y + map_h, right_w, table_h);
    right_tile_->end();

    outer_tile->end();

    resizable(outer_tile);

    left_tile_->init_sizes();
    right_tile_->init_sizes();

    updateLeftColumn(true, false);
  }

  void setupLayerPanel(int X, int Y, int W, int H) {
    layer_panel_ = new DockPanel(X, Y, W, H, "Layers");
    layer_panel_->color(FL_BACKGROUND2_COLOR);

    layer_list_ = new LayerListWidget(0, 0, 0, 0);
    layer_list_->setLayers(&layers_);

    layer_list_->setSelectCallback([this](int idx) {
      current_layer_index_ = idx;
      updateTable();
    });
    layer_list_->setToggleCallback([this](int, bool) { updateMap(); });
    layer_list_->setReorderCallback([this](int from, int to) { reorderLayer(from, to); });
    layer_list_->setEditCallback([this](int idx) { editLayerAt(idx); });

    layer_panel_->end();
    layer_panel_->setContent(layer_list_);
  }

  void setupAttributesPanel(int X, int Y, int W, int H) {
    attributes_group_ = new DockPanel(X, Y, W, H, "Attributes");
    attributes_group_->color(FL_WHITE);

    attributes_panel_ = new AttributesPanel(0, 0, 0, 0);
    attributes_panel_->color(FL_WHITE);

    attributes_group_->end();
    attributes_group_->setContent(attributes_panel_);
  }

  void setupMapPanel(int X, int Y, int W, int H) {
    map_panel_ = new DockPanel(X, Y, W, H, nullptr, 2);
    map_panel_->color(FL_WHITE);

    map_widget_ = new MapWidget(0, 0, 0, 0);
    map_widget_->setLayers(&layers_);
    map_widget_->setFeatureClickCallback([this](int layer_idx, int feature_idx) {
      onFeatureClicked(layer_idx, feature_idx);
    });

    map_panel_->end();
    map_panel_->setContent(map_widget_);
  }

  void setupTablePanel(int X, int Y, int W, int H) {
    table_panel_ = new DockPanel(X, Y, W, H, "Attribute Table");
    table_panel_->color(FL_WHITE);

    table_widget_ = new TableWidget(0, 0, 0, 0);
    table_widget_->color(FL_WHITE);
    table_widget_->setLayers(&layers_);
    table_widget_->setCurrentLayer(current_layer_index_);
    table_widget_->setRowSelectCallback([this](int row) {
      if (current_layer_index_ < 0 || current_layer_index_ >= (int)layers_.size()) return;
      Layer* layer = layers_[current_layer_index_].get();
      map_widget_->setHighlightedFeature(layer, row);
      map_widget_->zoomToFeature(layer, row);
      attributes_panel_->showFeature(layer, row);
    });

    table_panel_->end();
    table_panel_->setContent(table_widget_);
  }

  void updateLeftColumn(bool layers_visible, bool attributes_visible) {
    int X = left_tile_->x(), Y = left_tile_->y(), W = left_tile_->w(), H = left_tile_->h();

    int layer_h, attrs_h;
    if (layers_visible && attributes_visible) {
      int hi = std::max(kMinPanelSize, H - kMinPanelSize);
      layer_h = std::clamp(left_split_h_, kMinPanelSize, hi);
      attrs_h = H - layer_h;
    } else if (attributes_visible) {
      layer_h = 0;
      attrs_h = H;
    } else {
      layer_h = H;
      attrs_h = 0;
    }

    layer_panel_->resize(X, Y, W, layer_h);
    attributes_group_->resize(X, Y + layer_h, W, attrs_h);
    left_tile_->redraw();
  }

  void updateRightColumn(bool map_visible, bool table_visible) {
    int X = right_tile_->x(), Y = right_tile_->y(), W = right_tile_->w(), H = right_tile_->h();

    int map_h, table_h;
    if (map_visible && table_visible) {
      int hi = std::max(kMinPanelSize, H - kMinPanelSize);
      map_h = std::clamp(right_split_h_, kMinPanelSize, hi);
      table_h = H - map_h;
    } else if (table_visible) {
      map_h = 0;
      table_h = H;
    } else {
      map_h = H;
      table_h = 0;
    }

    map_panel_->resize(X, Y, W, map_h);
    table_panel_->resize(X, Y + map_h, W, table_h);
    right_tile_->redraw();
  }

  void syncMenuCheck(int index, bool checked) {
    if (index < 0) return;
    menu_bar_->mode(index, FL_MENU_TOGGLE | (checked ? FL_MENU_VALUE : 0));
  }

  void toggleLeftPanel(bool toggle_layers) {
    bool layers_visible = layer_panel_->h() > 0;
    bool attrs_visible = attributes_group_->h() > 0;

    if (layers_visible && attrs_visible) left_split_h_ = layer_panel_->h();

    bool new_layers_visible = toggle_layers ? !layers_visible : layers_visible;
    bool new_attrs_visible = toggle_layers ? attrs_visible : !attrs_visible;

    if (!new_layers_visible && !new_attrs_visible) {
      syncMenuCheck(mi_panel_layers_, layers_visible);
      syncMenuCheck(mi_panel_attributes_, attrs_visible);
      return;
    }

    updateLeftColumn(new_layers_visible, new_attrs_visible);
    syncMenuCheck(mi_panel_layers_, new_layers_visible);
    syncMenuCheck(mi_panel_attributes_, new_attrs_visible);
  }

  void toggleRightPanel(bool toggle_map) {
    bool map_visible = map_panel_->h() > 0;
    bool table_visible = table_panel_->h() > 0;

    if (map_visible && table_visible) right_split_h_ = map_panel_->h();

    bool new_map_visible = toggle_map ? !map_visible : map_visible;
    bool new_table_visible = toggle_map ? table_visible : !table_visible;

    if (!new_map_visible && !new_table_visible) {
      syncMenuCheck(mi_panel_map_, map_visible);
      syncMenuCheck(mi_panel_table_, table_visible);
      return;
    }

    updateRightColumn(new_map_visible, new_table_visible);
    syncMenuCheck(mi_panel_map_, new_map_visible);
    syncMenuCheck(mi_panel_table_, new_table_visible);
  }

  void updateLayerList() {
    layer_list_->setSelected(current_layer_index_);
    layer_list_->redraw();
  }

  void updateMap() { map_widget_->redraw(); }
  void refreshMapExtent() { map_widget_->fitToExtent(); }

  void updateTable() {
    table_widget_->setCurrentLayer(current_layer_index_);
    table_widget_->refresh();
  }

  void onFeatureClicked(int layer_idx, int feature_idx) {
    if (layer_idx < 0 || layer_idx >= (int)layers_.size()) return;
    auto& layer = layers_[layer_idx];

    if (layer->type == LayerType::VECTOR) {
      if (feature_idx < 0 || feature_idx >= (int)layer->vector_data.features.size()) return;

      if (current_layer_index_ != layer_idx) {
        current_layer_index_ = layer_idx;
        updateLayerList();
        updateTable();
      }
      table_widget_->selectRow(feature_idx);
      map_widget_->setHighlightedFeature(layer.get(), feature_idx);
      attributes_panel_->showFeature(layer.get(), feature_idx);
    } else if (layer->type == LayerType::RASTER) {
      if (!layer->raster_data.has_rat) return;
      if (feature_idx < 0 || feature_idx >= (int)layer->raster_data.rat_rows.size()) return;

      if (current_layer_index_ != layer_idx) {
        current_layer_index_ = layer_idx;
        updateLayerList();
        updateTable();
      }
      map_widget_->clearHighlight();
      table_widget_->selectRow(feature_idx);
      attributes_panel_->showFeature(layer.get(), feature_idx);
    }
  }

  void showErrorMessage(const std::string& msg) {
    fl_message_title("Error");
    fl_message("%s", msg.c_str());
  }

  void showInfoMessage(const std::string& msg) {
    fl_message_title("Information");
    fl_message("%s", msg.c_str());
  }

  bool loadFile(const std::string& filename) {
    std::string ext = std::filesystem::path(filename).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".csv" || ext == ".tsv") return loadVectorCSV(filename);
    if (ext == ".asc" || ext == ".grd") return loadRasterASCII(filename);

    showErrorMessage("Unsupported format: " + ext);
    return false;
  }

  void applySidecarStyle(Layer& layer, const std::string& filename) {
    Spatial::LayerStyle sty;
    if (!Spatial::loadSidecarStyle(filename, sty)) return;

    uchar r, g, b;
    if (parseHexColor(sty.color, r, g, b)) layer.color = fl_rgb_color(r, g, b);
    if (parseHexColor(sty.fill_color, r, g, b)) layer.fill_color = fl_rgb_color(r, g, b);
    if (sty.fill) layer.fill = true;
    layer.opacity = sty.opacity;
    layer.style_rules = sty.rules;
    if (sty.show_labels) {
      layer.show_labels = true;
      if (!sty.label_field.empty()) layer.label_field = sty.label_field;
    }
  }

  bool loadVectorCSV(const std::string& filename) {
    auto layer = std::make_shared<Layer>();
    layer->filename = filename;
    layer->name = std::filesystem::path(filename).stem().string();
    layer->type = LayerType::VECTOR;

    Spatial::SpatialCSVReader reader;
    if (!reader.read(filename, layer->vector_data)) {
      showErrorMessage("Could not read vector file: " + filename);
      return false;
    }

    static const Fl_Color colors[] = {FL_BLUE, FL_RED, FL_GREEN, FL_YELLOW, FL_MAGENTA, FL_CYAN};
    layer->color = colors[layers_.size() % 6];
    layer->fill_color = layer->color;
    applySidecarStyle(*layer, filename);

    layers_.insert(layers_.begin(), layer);
    current_layer_index_ = 0;

    updateLayerList();
    refreshMapExtent();
    updateTable();

    return true;
  }

  bool loadRasterASCII(const std::string& filename) {
    auto layer = std::make_shared<Layer>();
    layer->filename = filename;
    layer->name = std::filesystem::path(filename).stem().string();
    layer->type = LayerType::RASTER;

    Spatial::ASCIIGridReader reader;
    if (!reader.read(filename, layer->raster_data)) {
      showErrorMessage("Could not read ASCII raster file: " + filename);
      return false;
    }

    layer->color = FL_RED;
    layer->fill = true;
    layer->opacity = 0.6;
    applySidecarStyle(*layer, filename);

    layers_.insert(layers_.begin(), layer);
    current_layer_index_ = 0;

    updateLayerList();
    refreshMapExtent();
    updateTable();

    return true;
  }

  bool saveCurrentLayer() {
    if (current_layer_index_ < 0 || current_layer_index_ >= (int)layers_.size()) {
      showErrorMessage("No layer selected");
      return false;
    }
    return saveLayer(layers_[current_layer_index_]->filename, current_layer_index_);
  }

  bool saveCurrentLayerAs() {
    if (current_layer_index_ < 0 || current_layer_index_ >= (int)layers_.size()) {
      showErrorMessage("No layer selected");
      return false;
    }

    auto& layer = layers_[current_layer_index_];
    std::string filter = (layer->type == LayerType::VECTOR) ? "CSV files (*.csv)" : "ASCII Grid files (*.asc)";

    Fl_File_Chooser chooser(current_directory_.c_str(), filter.c_str(), Fl_File_Chooser::CREATE, "Save as");
    chooser.show();
    while (chooser.shown()) Fl::wait();

    if (chooser.count() <= 0 || !chooser.value()) return false;

    std::string filename = chooser.value();
    current_directory_ = std::filesystem::path(filename).parent_path().string();
    return saveLayer(filename, current_layer_index_);
  }

  bool saveLayer(const std::string& filename, int layer_index) {
    if (layer_index < 0 || layer_index >= (int)layers_.size()) {
      showErrorMessage("Invalid layer index");
      return false;
    }

    auto& layer = layers_[layer_index];
    if (layer->type == LayerType::VECTOR) {
      writeVectorCSV(layer->vector_data, filename);
    } else {
      writeRasterASCII(layer->raster_data, filename);
    }
    showInfoMessage("Saved: " + filename);
    return true;
  }

  static void cbMenuOpen(Fl_Widget*, void* d) {
    MainWindow* win = static_cast<MainWindow*>(d);

    Fl_File_Chooser chooser(win->current_directory_.c_str(), "Spatial files (*.csv,*.asc,*.grd)",
                             Fl_File_Chooser::SINGLE, "Open layer");
    chooser.show();
    while (chooser.shown()) Fl::wait();

    if (chooser.count() > 0 && chooser.value()) {
      std::string filename = chooser.value();
      win->current_directory_ = std::filesystem::path(filename).parent_path().string();
      win->loadFile(filename);
    }
  }

  static void cbMenuSave(Fl_Widget*, void* d) {
    MainWindow* win = static_cast<MainWindow*>(d);
    if (win->current_layer_index_ >= 0 && win->current_layer_index_ < (int)win->layers_.size()) {
      win->saveCurrentLayer();
    }
  }

  static void cbMenuSaveAs(Fl_Widget*, void* d) {
    MainWindow* win = static_cast<MainWindow*>(d);
    if (win->current_layer_index_ >= 0 && win->current_layer_index_ < (int)win->layers_.size()) {
      win->saveCurrentLayerAs();
    }
  }

  static void cbMenuQuit(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->hide(); }

  static void cbMenuAbout(Fl_Widget*, void*) {
    fl_message_title("About");
    fl_message(
        "Spatial Viewer - Spatial TEC Viewer\n\n"
        "Load and visualize files:\n"
        "  - Spatial CSV (with WKT geometry)\n"
        "  - ASCII Grid (.asc, .grd)\n\n"
        "Features:\n"
        "  - Multiple vector and raster layers\n"
        "  - Attribute editing, row and column add/delete\n"
        "  - Column resizing\n"
        "  - Zoom and mouse navigation\n"
        "  - Layer export");
  }

  static void cbLayerRemove(Fl_Widget*, void* d) {
    MainWindow* win = static_cast<MainWindow*>(d);
    int idx = win->layer_list_->selected();
    if (idx < 0 || idx >= (int)win->layers_.size()) return;

    win->layers_.erase(win->layers_.begin() + idx);
    if (win->current_layer_index_ >= (int)win->layers_.size()) {
      win->current_layer_index_ = (int)win->layers_.size() - 1;
    }
    win->updateLayerList();
    win->refreshMapExtent();
    win->updateTable();
  }

  static void cbLayerEdit(Fl_Widget*, void* d) {
    MainWindow* win = static_cast<MainWindow*>(d);
    win->editLayerAt(win->layer_list_->selected());
  }

  void editLayerAt(int idx) {
    if (idx < 0 || idx >= (int)layers_.size()) return;
    if (layers_[idx]->type == LayerType::RASTER) {
      editRasterLayerAt(idx);
    } else {
      editVectorLayerAt(idx);
    }
  }

  void editRasterLayerAt(int idx) {
    auto& layer = layers_[idx];
    const auto& rat_columns = layer->raster_data.rat_columns;
    bool has_rat = layer->raster_data.has_rat;

    const int dlg_w = 420;
    const int pad = 15;
    const int dlg_h = 250;

    Fl_Double_Window dialog(dlg_w, dlg_h, "Edit Layer");
    dialog.color(FL_BACKGROUND2_COLOR);

    Fl_Box header(pad, pad, dlg_w - 2 * pad, 22, "Raster Layer");
    header.labelfont(FL_BOLD);
    header.labelsize(13);
    header.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    int name_y = pad + 30;
    Fl_Input name(pad + 70, name_y, dlg_w - 2 * pad - 70, 26, "Name:");
    name.align(FL_ALIGN_LEFT);
    name.value(layer->name.c_str());

    int field_y = name_y + 26 + 16;
    Fl_Choice color_choice(pad + 110, field_y, dlg_w - 2 * pad - 110, 26, "Color field:");
    color_choice.align(FL_ALIGN_LEFT);
    Fl_Choice label_choice(pad + 110, field_y + 34, dlg_w - 2 * pad - 110, 26, "Label field:");
    label_choice.align(FL_ALIGN_LEFT);

    if (has_rat) {
      color_choice.add("(none)");
      label_choice.add("(none)");
      int color_sel = 0, label_sel = 0;
      for (size_t i = 0; i < rat_columns.size(); ++i) {
        color_choice.add(rat_columns[i].c_str());
        label_choice.add(rat_columns[i].c_str());
        if (rat_columns[i] == layer->rat_color_field) color_sel = (int)i + 1;
        if (rat_columns[i] == layer->rat_label_field) label_sel = (int)i + 1;
      }
      color_choice.value(color_sel);
      label_choice.value(label_sel);
    } else {
      color_choice.add("(no RAT loaded)");
      color_choice.value(0);
      color_choice.deactivate();
      label_choice.add("(no RAT loaded)");
      label_choice.value(0);
      label_choice.deactivate();
    }

    int labels_y = field_y + 34 + 34;
    Fl_Check_Button show_labels(pad + 110, labels_y, 200, 24, "Show labels");
    show_labels.value(layer->show_labels ? 1 : 0);
    Fl_Box labels_hint(pad, labels_y, 110, 24, "On map:");
    labels_hint.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    int btn_y = dlg_h - pad - 30;
    Fl_Button cancel(dlg_w - pad - 190, btn_y, 90, 30, "Cancel");
    Fl_Return_Button ok(dlg_w - pad - 90, btn_y, 90, 30, "OK");

    bool accepted = false;
    ok.callback([](Fl_Widget*, void* data) {
      *static_cast<bool*>(data) = true;
      Fl::first_window()->hide();
    }, &accepted);
    cancel.callback([](Fl_Widget*, void*) { Fl::first_window()->hide(); });

    dialog.set_modal();
    dialog.end();
    dialog.show();
    while (dialog.shown()) Fl::wait();
    if (!accepted) return;

    layer->name = name.value();
    if (has_rat) {
      auto pick = [&](Fl_Choice& choice) -> std::string {
        int v = choice.value();
        int ci = v - 1;
        if (ci < 0 || ci >= (int)rat_columns.size()) return std::string();
        return rat_columns[ci];
      };
      layer->rat_color_field = pick(color_choice);
      layer->rat_label_field = pick(label_choice);
    }
    layer->show_labels = show_labels.value() != 0;

    updateLayerList();
    updateMap();
    updateTable();
    attributes_panel_->refresh();
  }

  void editVectorLayerAt(int idx) {
    auto& layer = layers_[idx];

    const int dlg_w = 460, dlg_h = 500;
    const int pad = 15;

    Fl_Double_Window dialog(dlg_w, dlg_h, "Edit Layer");
    dialog.color(FL_BACKGROUND2_COLOR);

    Fl_Box header(pad, pad, dlg_w - 2 * pad, 22, "Vector Layer");
    header.labelfont(FL_BOLD);
    header.labelsize(13);
    header.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    int name_y = pad + 30;
    Fl_Input name(pad + 60, name_y, dlg_w - 2 * pad - 60, 26, "Name:");
    name.align(FL_ALIGN_LEFT);
    name.value(layer->name.c_str());

    int colors_y = name_y + 26 + 20;
    int chooser_w = (dlg_w - 2 * pad - 20) / 2;
    int chooser_h = 200;

    Fl_Box border_label(pad, colors_y, chooser_w, 18, "Border color");
    border_label.labelfont(FL_BOLD);
    border_label.labelsize(12);
    border_label.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    Fl_Color_Chooser border(pad, colors_y + 22, chooser_w, chooser_h);

    Fl_Box fill_label(pad + chooser_w + 20, colors_y, chooser_w, 18, "Fill color");
    fill_label.labelfont(FL_BOLD);
    fill_label.labelsize(12);
    fill_label.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    Fl_Color_Chooser fill(pad + chooser_w + 20, colors_y + 22, chooser_w, chooser_h);

    uchar r, g, b;
    Fl::get_color(layer->color, r, g, b);
    border.rgb(r / 255.0, g / 255.0, b / 255.0);
    Fl::get_color(layer->fill_color, r, g, b);
    fill.rgb(r / 255.0, g / 255.0, b / 255.0);

    int fill_check_y = colors_y + 22 + chooser_h + 12;
    Fl_Check_Button fill_check(pad, fill_check_y, 260, 24, "Fill polygons / points");
    fill_check.value(layer->fill ? 1 : 0);

    int labels_check_y = fill_check_y + 34;
    Fl_Check_Button show_labels(pad, labels_check_y, 220, 24, "Show labels");
    show_labels.value(layer->show_labels ? 1 : 0);

    int label_field_y = labels_check_y + 30;
    Fl_Choice label_field_choice(pad + 110, label_field_y, dlg_w - 2 * pad - 110, 26,
                                  "Label field:");
    label_field_choice.align(FL_ALIGN_LEFT);
    label_field_choice.add("(none)");
    int label_field_sel = 0;
    int ci = 1;
    for (const auto& col : layer->vector_data.columns) {
      if (col == layer->vector_data.geometry_column) continue;
      label_field_choice.add(col.c_str());
      if (col == layer->label_field) label_field_sel = ci;
      ++ci;
    }
    label_field_choice.value(label_field_sel);

    int btn_y = dlg_h - pad - 30;
    Fl_Button cancel(dlg_w - pad - 190, btn_y, 90, 30, "Cancel");
    Fl_Return_Button ok(dlg_w - pad - 90, btn_y, 90, 30, "OK");

    bool accepted = false;
    ok.callback([](Fl_Widget*, void* data) {
      *static_cast<bool*>(data) = true;
      Fl::first_window()->hide();
    }, &accepted);
    cancel.callback([](Fl_Widget*, void*) { Fl::first_window()->hide(); });

    dialog.set_modal();
    dialog.end();
    dialog.show();
    while (dialog.shown()) Fl::wait();
    if (!accepted) return;

    layer->name = name.value();
    layer->color = fl_rgb_color((uchar)(border.r() * 255), (uchar)(border.g() * 255),
                                (uchar)(border.b() * 255));
    layer->fill_color = fl_rgb_color((uchar)(fill.r() * 255), (uchar)(fill.g() * 255),
                                     (uchar)(fill.b() * 255));
    layer->fill = fill_check.value() != 0;
    layer->show_labels = show_labels.value() != 0;
    {
      int v = label_field_choice.value();
      layer->label_field.clear();
      if (v > 0) {
        int seen = 0;
        for (const auto& col : layer->vector_data.columns) {
          if (col == layer->vector_data.geometry_column) continue;
          ++seen;
          if (seen == v) {
            layer->label_field = col;
            break;
          }
        }
      }
    }

    updateLayerList();
    updateMap();
  }

  void reorderLayer(int from, int to) {
    if (from < 0 || from >= (int)layers_.size()) return;
    if (to < 0) to = 0;
    if (to > (int)layers_.size()) to = (int)layers_.size();
    if (to == from || to == from + 1) return;

    auto moved = layers_[from];
    layers_.erase(layers_.begin() + from);
    int insert_at = (to > from) ? to - 1 : to;
    layers_.insert(layers_.begin() + insert_at, moved);

    current_layer_index_ = insert_at;
    updateLayerList();
    updateMap();
  }

  static void cbLayerMoveUp(Fl_Widget*, void* d) {
    MainWindow* win = static_cast<MainWindow*>(d);
    int idx = win->current_layer_index_;
    if (idx <= 0 || idx >= (int)win->layers_.size()) return;
    std::swap(win->layers_[idx], win->layers_[idx - 1]);
    win->current_layer_index_ = idx - 1;
    win->updateLayerList();
    win->updateMap();
  }

  static void cbLayerMoveDown(Fl_Widget*, void* d) {
    MainWindow* win = static_cast<MainWindow*>(d);
    int idx = win->current_layer_index_;
    if (idx < 0 || idx >= (int)win->layers_.size() - 1) return;
    std::swap(win->layers_[idx], win->layers_[idx + 1]);
    win->current_layer_index_ = idx + 1;
    win->updateLayerList();
    win->updateMap();
  }

  static void cbZoomIn(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->map_widget_->zoomIn(); }
  static void cbZoomOut(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->map_widget_->zoomOut(); }
  static void cbFitExtent(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->map_widget_->fitToExtent(); }

  static void cbTableAddRow(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->table_widget_->addRow(); }
  static void cbTableDeleteRow(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->table_widget_->deleteRow(); }
  static void cbTableAddColumn(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->table_widget_->addColumn(); }
  static void cbTableDeleteColumn(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->table_widget_->deleteColumn(); }

  static void cbTableHideColumn(Fl_Widget*, void* d) {
    MainWindow* win = static_cast<MainWindow*>(d);
    win->table_widget_->hideColumn();
    win->attributes_panel_->refresh();
  }

  static void cbTableShowAllColumns(Fl_Widget*, void* d) {
    MainWindow* win = static_cast<MainWindow*>(d);
    win->table_widget_->showAllColumns();
    win->attributes_panel_->refresh();
  }

  static void cbTogglePanelLayers(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->toggleLeftPanel(true); }
  static void cbTogglePanelAttributes(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->toggleLeftPanel(false); }
  static void cbTogglePanelMap(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->toggleRightPanel(true); }
  static void cbTogglePanelTable(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->toggleRightPanel(false); }

  static constexpr int kMinPanelSize = 40;

  Fl_Menu_Bar* menu_bar_ = nullptr;
  int layout_menu_h_ = 0;

  Fl_Tile* left_tile_ = nullptr;
  Fl_Tile* right_tile_ = nullptr;
  DockPanel* layer_panel_ = nullptr;
  DockPanel* attributes_group_ = nullptr;
  DockPanel* map_panel_ = nullptr;
  DockPanel* table_panel_ = nullptr;

  int left_split_h_ = 0;
  int right_split_h_ = 0;

  int mi_panel_layers_ = -1;
  int mi_panel_attributes_ = -1;
  int mi_panel_map_ = -1;
  int mi_panel_table_ = -1;

  LayerListWidget* layer_list_ = nullptr;
  AttributesPanel* attributes_panel_ = nullptr;
  MapWidget* map_widget_ = nullptr;
  TableWidget* table_widget_ = nullptr;

  std::vector<std::shared_ptr<Layer>> layers_;
  int current_layer_index_ = -1;
  std::string current_directory_;
};

}
