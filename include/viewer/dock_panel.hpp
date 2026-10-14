#pragma once

#include <algorithm>

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Widget.H>

namespace Viewer {

class DockPanel : public Fl_Group {
public:
  DockPanel(int X, int Y, int W, int H, const char* title_text = nullptr, int margin = 5)
      : Fl_Group(X, Y, W, H), margin_(margin) {
    box(FL_DOWN_BOX);
    if (title_text && *title_text) {
      title_ = new Fl_Box(FL_NO_BOX, X + margin_, Y + margin_, std::max(0, W - 2 * margin_), kTitleH,
                           title_text);
      title_->labelfont(FL_BOLD);
      title_->labelsize(14);
      title_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    }
  }

  void setContent(Fl_Widget* content) {
    content_ = content;
    layoutChildren();
  }

  void resize(int X, int Y, int W, int H) override {
    Fl_Widget::resize(X, Y, W, H);
    layoutChildren();
  }

private:
  void layoutChildren() {
    int top = margin_;
    if (title_) {
      title_->resize(x() + margin_, y() + margin_, std::max(0, w() - 2 * margin_), kTitleH);
      top = margin_ + kTitleH + kTitleGap;
    }
    if (content_) {
      content_->resize(x() + margin_, y() + top, std::max(0, w() - 2 * margin_),
                        std::max(0, h() - top - margin_));
    }
  }

  static const int kTitleH = 20;
  static const int kTitleGap = 5;

  int margin_ = 5;
  Fl_Box* title_ = nullptr;
  Fl_Widget* content_ = nullptr;
};

}
