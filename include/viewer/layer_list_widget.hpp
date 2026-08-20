#pragma once

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <FL/Fl.H>
#include <FL/Fl_Widget.H>
#include <FL/fl_draw.H>

#include "viewer/layer.hpp"

namespace Viewer {

class LayerListWidget : public Fl_Widget {
public:
  LayerListWidget(int X, int Y, int W, int H, const char* L = 0)
      : Fl_Widget(X, Y, W, H, L) {
    box(FL_DOWN_BOX);
    color(FL_WHITE);
  }

  void setLayers(std::vector<std::shared_ptr<Layer>>* layers) {
    layers_ = layers;
    clampScroll();
    redraw();
  }

  void setSelected(int idx) {
    selected_ = idx;
    redraw();
  }

  int selected() const { return selected_; }

  void setSelectCallback(std::function<void(int)> cb) { select_cb_ = std::move(cb); }
  void setToggleCallback(std::function<void(int, bool)> cb) { toggle_cb_ = std::move(cb); }
  void setReorderCallback(std::function<void(int, int)> cb) { reorder_cb_ = std::move(cb); }
  void setEditCallback(std::function<void(int)> cb) { edit_cb_ = std::move(cb); }

  void draw() override {
    draw_box();

    int X = x() + Fl::box_dx(box());
    int Y = y() + Fl::box_dy(box());
    int W = w() - Fl::box_dw(box());
    int H = h() - Fl::box_dh(box());

    fl_push_clip(X, Y, W, H);
    fl_color(color());
    fl_rectf(X, Y, W, H);

    if (layers_ && !layers_->empty()) {
      int ry = Y - scroll_y_;
      for (int i = 0; i < (int)layers_->size(); ++i) {
        drawRow(i, X, ry, W, row_h_);
        ry += row_h_;
      }

      if (dragging_ && drop_index_ >= 0) {
        int line_y = Y - scroll_y_ + drop_index_ * row_h_;
        fl_color(FL_BLUE);
        fl_line_style(FL_SOLID, 2);
        fl_line(X + 2, line_y, X + W - 2, line_y);
        fl_line_style(0);
      }
    } else {
      fl_color(FL_GRAY);
      fl_font(FL_HELVETICA_ITALIC, 12);
      fl_draw("No layers loaded", X, Y, W, H, FL_ALIGN_CENTER);
    }

    fl_pop_clip();
  }

  int handle(int event) override {
    switch (event) {
      case FL_PUSH: {
        take_focus();
        int idx = rowAt(Fl::event_y());
        if (idx < 0 || !layers_ || idx >= (int)layers_->size()) return 1;

        int cb_x = checkboxX();
        int cb_y = rowTop(idx) + (row_h_ - checkbox_size_) / 2;
        if (Fl::event_x() >= cb_x && Fl::event_x() <= cb_x + checkbox_size_ &&
            Fl::event_y() >= cb_y && Fl::event_y() <= cb_y + checkbox_size_) {
          auto& layer = (*layers_)[idx];
          layer->visible = !layer->visible;
          if (toggle_cb_) toggle_cb_(idx, layer->visible);
          redraw();
          return 1;
        }

        press_index_ = idx;
        press_y_ = Fl::event_y();
        dragging_ = false;
        selected_ = idx;
        if (select_cb_) select_cb_(idx);
        if (Fl::event_clicks() > 0 && edit_cb_) edit_cb_(idx);
        redraw();
        return 1;
      }

      case FL_DRAG: {
        if (press_index_ < 0) return 1;
        if (!dragging_ && std::abs(Fl::event_y() - press_y_) > 4) dragging_ = true;
        if (dragging_) {
          int idx = rowAt(Fl::event_y());
          int count = layers_ ? (int)layers_->size() : 0;
          if (idx < 0) {
            int Y = y() + Fl::box_dy(box());
            idx = (Fl::event_y() < Y) ? 0 : count;
          }
          drop_index_ = std::clamp(idx, 0, count);
          redraw();
        }
        return 1;
      }

      case FL_RELEASE: {
        if (dragging_ && press_index_ >= 0 && drop_index_ >= 0 &&
            drop_index_ != press_index_ && drop_index_ != press_index_ + 1) {
          if (reorder_cb_) reorder_cb_(press_index_, drop_index_);
        }
        dragging_ = false;
        press_index_ = -1;
        drop_index_ = -1;
        redraw();
        return 1;
      }

      case FL_MOUSEWHEEL: {
        scroll_y_ += Fl::event_dy() * row_h_;
        clampScroll();
        redraw();
        return 1;
      }

      case FL_FOCUS:
        return 1;
      case FL_UNFOCUS:
        return 1;

      default:
        break;
    }
    return Fl_Widget::handle(event);
  }

private:
  int rowTop(int idx) const {
    return y() + Fl::box_dy(box()) - scroll_y_ + idx * row_h_;
  }

  int rowAt(int screen_y) const {
    if (!layers_ || layers_->empty()) return -1;
    int Y = y() + Fl::box_dy(box());
    int rel = screen_y - Y + scroll_y_;
    if (rel < 0) return -1;
    int idx = rel / row_h_;
    if (idx >= (int)layers_->size()) return -1;
    return idx;
  }

  void clampScroll() {
    int content_h = layers_ ? (int)layers_->size() * row_h_ : 0;
    int visible_h = h() - Fl::box_dh(box());
    int max_scroll = std::max(0, content_h - visible_h);
    scroll_y_ = std::clamp(scroll_y_, 0, max_scroll);
  }

  int checkboxX() const { return x() + Fl::box_dx(box()) + 6; }

  void drawRow(int idx, int X, int ry, int W, int H) {
    const auto& layer = (*layers_)[idx];

    bool sel = (idx == selected_);
    Fl_Color bg = sel ? fl_rgb_color(200, 224, 248) : (idx % 2 == 0 ? FL_WHITE : fl_rgb_color(246, 246, 246));
    fl_color(bg);
    fl_rectf(X, ry, W, H);
    fl_color(fl_rgb_color(224, 224, 224));
    fl_line(X, ry + H, X + W, ry + H);

    int cb = checkbox_size_;
    int cb_x = X + 6;
    int cb_y = ry + (H - cb) / 2;
    fl_draw_box(FL_DOWN_BOX, cb_x, cb_y, cb, cb, FL_WHITE);
    if (layer->visible) {
      fl_color(fl_rgb_color(30, 140, 60));
      fl_line_style(FL_SOLID, 2);
      fl_line(cb_x + 3, cb_y + cb / 2, cb_x + cb / 2 - 1, cb_y + cb - 4);
      fl_line(cb_x + cb / 2 - 1, cb_y + cb - 4, cb_x + cb - 3, cb_y + 3);
      fl_line_style(0);
    }

    int sw = 18;
    int sw_x = cb_x + cb + 10;
    int sw_y = ry + (H - sw) / 2;
    fl_color(FL_WHITE);
    fl_rectf(sw_x, sw_y, sw, sw);
    if (layer->fill) {
      fl_color(layer->fill_color);
      fl_rectf(sw_x + 1, sw_y + 1, sw - 2, sw - 2);
    }
    fl_color(layer->color);
    fl_line_style(FL_SOLID, 2);
    fl_rect(sw_x, sw_y, sw, sw);
    fl_line_style(0);

    std::string label = layer->name;
    label += (layer->type == LayerType::VECTOR) ? "  [Vector]" : "  [Raster]";

    fl_font(FL_HELVETICA, 13);
    fl_color(layer->visible ? FL_BLACK : fl_rgb_color(150, 150, 150));

    int text_x = sw_x + sw + 10;
    int text_w = X + W - text_x - 6;
    if (text_w > 0) {
      fl_push_clip(text_x, ry, text_w, H);
      fl_draw(label.c_str(), text_x, ry, text_w, H, FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
      fl_pop_clip();
    }
  }

  std::vector<std::shared_ptr<Layer>>* layers_ = nullptr;
  int selected_ = -1;
  int row_h_ = 32;
  int checkbox_size_ = 16;
  int scroll_y_ = 0;

  int press_index_ = -1;
  int press_y_ = 0;
  bool dragging_ = false;
  int drop_index_ = -1;

  std::function<void(int)> select_cb_;
  std::function<void(int, bool)> toggle_cb_;
  std::function<void(int, int)> reorder_cb_;
  std::function<void(int)> edit_cb_;
};

}
