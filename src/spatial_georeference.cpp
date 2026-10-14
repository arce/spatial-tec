// spatial_georeference -- FLTK graphical tool to georeference a PNG/JPEG
// image: click points on the image with known geographic coordinates, then
// export the result as an .asc raster (Arc/Info ASCII Grid), the same
// format the rest of Spatial TEC uses (see include/core/ascii_grid.hpp /
// spatial_io.hpp).
//
// Important design limitation: this project's .asc format only supports a
// north-up grid with a single square cell (ncols/nrows/xllcorner/
// yllcorner/cellsize) -- no rotation or shear. So the geometric fit here is
// a constrained affine model (independent scale + translation on X and on
// Y, no rotation): 2 control points solve it exactly; 3 or more are fit by
// least squares, and the RMS error of each axis is reported. For best
// results, spread control points across the image's corners/edges and
// avoid marking two points in the same image column or row.
//
// The output raster is a single band (as RasterDataset requires), but that
// doesn't mean color is lost: by default every cell stores the INDEX of a
// palette of up to 256 colors (quantized by "median cut" if the image has
// more distinct colors than that), and the real color of each index is
// appended as a RAT table (@RAT) -- the same mechanism spatial_rat/
// ascii_grid.hpp already use. Alternatively the image can be exported in
// grayscale mode (the cell value is the luminance itself, useful for
// numeric analysis with spatial_calc, with an optional grayscale RAT for
// display only). In both modes resampling is nearest-neighbor.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Float_Input.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Shared_Image.H>
#include <FL/Fl_Slider.H>
#include <FL/Fl_Value_Output.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>

#include "core/spatial_io.hpp"
#include "core/spatial_types.hpp"

namespace Georef {

// A control point (Ground Control Point): a pixel of the image (column,
// row -- can be fractional, a click is not rounded) with its known
// geographic coordinate.
struct ControlPoint {
  double col = 0.0;
  double row = 0.0;
  double x = 0.0;
  double y = 0.0;
};

// Widget that displays the loaded image with zoom (mouse wheel, centered
// on the cursor) and pan (drag with the left button, or the mouse wheel
// itself: a trackpad's two-finger swipe pans directly, and Shift+wheel
// pans vertically on a plain mouse). When "mark mode" is on, a left click
// that doesn't turn into a drag adds a control point at that pixel; a
// right click near an existing marker deletes it. Both gestures share the
// left button, telling a click from a drag apart by how far the mouse
// moved.
class ImageCanvas : public Fl_Box {
 public:
  ImageCanvas(int X, int Y, int W, int H) : Fl_Box(X, Y, W, H) {
    box(FL_DOWN_BOX);
    color(FL_BACKGROUND2_COLOR);
  }

  ~ImageCanvas() override {
    if (scaled_cache_) delete scaled_cache_;
    if (image_) image_->release();
  }

  bool loadImage(const std::string& path) {
    Fl_Shared_Image* img = Fl_Shared_Image::get(path.c_str());
    if (!img || img->w() <= 0 || img->h() <= 0) {
      if (img) img->release();
      return false;
    }
    if (image_) image_->release();
    if (scaled_cache_) {
      delete scaled_cache_;
      scaled_cache_ = nullptr;
    }
    image_ = img;
    image_path_ = path;
    img_width_ = image_->w();
    img_height_ = image_->h();
    zoom_ = 1.0;
    offset_x_ = offset_y_ = 0.0;
    redraw();
    return true;
  }

  bool hasImage() const { return image_ != nullptr; }
  int imageWidth() const { return img_width_; }
  int imageHeight() const { return img_height_; }
  const std::string& imagePath() const { return image_path_; }

  void setPoints(const std::vector<ControlPoint>* points) { points_ = points; }
  void setSelectedIndex(int idx) {
    selected_index_ = idx;
    redraw();
  }
  void setMarkMode(bool on) { mark_mode_ = on; }

  void setOnPointClick(std::function<void(double, double)> cb) { on_point_click_ = std::move(cb); }
  void setOnPointRightClick(std::function<void(int)> cb) { on_point_right_click_ = std::move(cb); }

  void zoomIn() { setZoom(zoom_ * 1.25); }
  void zoomOut() { setZoom(zoom_ / 1.25); }

  void setZoom(double z) {
    if (z < 0.02) z = 0.02;
    if (z > 40.0) z = 40.0;
    zoom_ = z;
    redraw();
  }

  double getZoom() const { return zoom_; }

  void resetView() {
    zoom_ = 1.0;
    offset_x_ = offset_y_ = 0.0;
    redraw();
  }

  // Samples the ORIGINAL (unscaled) image at the nearest pixel to
  // (fcol, frow) and converts it to luminance (grayscale). Returns false,
  // leaving 'out' untouched, when the coordinates fall outside the image --
  // so the caller can fall back to the NODATA value in that case.
  bool sampleGray(double fcol, double frow, double& out) const {
    if (!image_) return false;
    int col = static_cast<int>(std::lround(fcol));
    int row = static_cast<int>(std::lround(frow));
    if (col < 0 || col >= img_width_ || row < 0 || row >= img_height_) return false;
    const unsigned char* buf = reinterpret_cast<const unsigned char*>(image_->data()[0]);
    int depth = image_->d();
    if (depth <= 0) depth = 1;
    int stride = image_->ld();
    if (stride == 0) stride = img_width_ * depth;
    const unsigned char* px = buf + static_cast<size_t>(row) * stride + static_cast<size_t>(col) * depth;
    if (depth >= 3) {
      out = std::round(0.299 * px[0] + 0.587 * px[1] + 0.114 * px[2]);
    } else {
      out = std::round(static_cast<double>(px[0]));
    }
    return true;
  }

  // Like sampleGray, but returns the real RGB color of the nearest pixel
  // (without converting to luminance) -- what "true color" export mode
  // uses to build the palette and fill the grid with indices into it.
  bool sampleRGB(double fcol, double frow, unsigned char& r, unsigned char& g, unsigned char& b) const {
    if (!image_) return false;
    int col = static_cast<int>(std::lround(fcol));
    int row = static_cast<int>(std::lround(frow));
    if (col < 0 || col >= img_width_ || row < 0 || row >= img_height_) return false;
    const unsigned char* buf = reinterpret_cast<const unsigned char*>(image_->data()[0]);
    int depth = image_->d();
    if (depth <= 0) depth = 1;
    int stride = image_->ld();
    if (stride == 0) stride = img_width_ * depth;
    const unsigned char* px = buf + static_cast<size_t>(row) * stride + static_cast<size_t>(col) * depth;
    if (depth >= 3) {
      r = px[0];
      g = px[1];
      b = px[2];
    } else {
      r = g = b = px[0];
    }
    return true;
  }

 protected:
  void draw() override {
    Fl_Box::draw();

    if (!image_) {
      fl_color(FL_GRAY0);
      fl_font(FL_HELVETICA, 14);
      fl_draw("Load a PNG or JPEG image to get started", x() + 10, y() + h() / 2);
      return;
    }

    if (!scaled_cache_ || std::fabs(scaled_cache_zoom_ - zoom_) > 1e-9) rebuildScaledCache();

    int display_w = scaled_cache_ ? scaled_cache_->w() : img_width_;
    int display_h = scaled_cache_ ? scaled_cache_->h() : img_height_;
    draw_x_ = x() + (w() - display_w) / 2.0 + offset_x_;
    draw_y_ = y() + (h() - display_h) / 2.0 + offset_y_;

    fl_push_clip(x(), y(), w(), h());
    if (scaled_cache_)
      scaled_cache_->draw(static_cast<int>(std::lround(draw_x_)), static_cast<int>(std::lround(draw_y_)));
    drawMarkers();
    fl_pop_clip();

    char info[160];
    snprintf(info, sizeof(info), "Zoom: %.0f%%   Image: %d x %d px", zoom_ * 100.0, img_width_, img_height_);
    fl_color(FL_WHITE);
    fl_rectf(x(), y(), w(), 22);
    fl_color(FL_BLACK);
    fl_font(FL_HELVETICA, 11);
    fl_draw(info, x() + 6, y() + 15);
  }

  int handle(int event) override {
    switch (event) {
      case FL_PUSH: {
        Fl::focus(this);
        if (Fl::event_button() == FL_LEFT_MOUSE) {
          dragging_ = true;
          moved_ = false;
          press_x_ = Fl::event_x();
          press_y_ = Fl::event_y();
          offset_start_x_ = offset_x_;
          offset_start_y_ = offset_y_;
          return 1;
        }
        if (Fl::event_button() == FL_RIGHT_MOUSE && mark_mode_ && on_point_right_click_) {
          int idx = hitTestMarker(Fl::event_x(), Fl::event_y());
          if (idx >= 0) on_point_right_click_(idx);
          return 1;
        }
        break;
      }
      case FL_DRAG: {
        if (dragging_) {
          int dx = Fl::event_x() - press_x_;
          int dy = Fl::event_y() - press_y_;
          if (std::abs(dx) > 3 || std::abs(dy) > 3) moved_ = true;
          offset_x_ = offset_start_x_ + dx;
          offset_y_ = offset_start_y_ + dy;
          redraw();
          return 1;
        }
        break;
      }
      case FL_RELEASE: {
        if (dragging_) {
          dragging_ = false;
          if (!moved_ && mark_mode_ && image_ && on_point_click_) {
            double col, row;
            screenToImage(press_x_, press_y_, col, row);
            if (col >= 0 && col < img_width_ && row >= 0 && row < img_height_) on_point_click_(col, row);
          }
          return 1;
        }
        break;
      }
      case FL_MOUSEWHEEL: {
        // Horizontal wheel motion (a trackpad's two-finger swipe, or
        // Shift+wheel on mice that turn that into a horizontal event) pans
        // the image left/right instead of zooming.
        int dx = Fl::event_dx();
        if (dx != 0) {
          offset_x_ -= dx * kWheelPanStep;
          redraw();
          return 1;
        }
        int dy = Fl::event_dy();
        if (dy != 0) {
          if (Fl::event_state() & FL_SHIFT) {
            // Shift + vertical wheel: pan up/down -- lets a plain mouse
            // (single scroll axis) drag the image via the wheel too, not
            // only by click-dragging.
            offset_y_ -= dy * kWheelPanStep;
            redraw();
          } else {
            // Plain vertical wheel: zoom, centered on the cursor.
            double factor = (dy > 0) ? (1.0 / 1.15) : 1.15;
            zoomAtScreenPoint(Fl::event_x(), Fl::event_y(), zoom_ * factor);
          }
          return 1;
        }
        break;
      }
      case FL_KEYDOWN: {
        int key = Fl::event_key();
        if (key == '+' || key == '=') {
          zoomIn();
          return 1;
        }
        if (key == '-' || key == '_') {
          zoomOut();
          return 1;
        }
        if (key == 'r' || key == 'R') {
          resetView();
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
    return Fl_Box::handle(event);
  }

 private:
  void rebuildScaledCache() {
    if (scaled_cache_) {
      delete scaled_cache_;
      scaled_cache_ = nullptr;
    }
    if (!image_) return;
    int dw = std::max(1, static_cast<int>(std::lround(img_width_ * zoom_)));
    int dh = std::max(1, static_cast<int>(std::lround(img_height_ * zoom_)));
    scaled_cache_ = image_->copy(dw, dh);
    scaled_cache_zoom_ = zoom_;
  }

  void screenToImage(int sx, int sy, double& col, double& row) const {
    col = (sx - draw_x_) / zoom_;
    row = (sy - draw_y_) / zoom_;
  }

  void imageToScreen(double col, double row, double& sx, double& sy) const {
    sx = draw_x_ + col * zoom_;
    sy = draw_y_ + row * zoom_;
  }

  void zoomAtScreenPoint(int mx, int my, double new_zoom) {
    if (new_zoom < 0.02) new_zoom = 0.02;
    if (new_zoom > 40.0) new_zoom = 40.0;
    double col, row;
    screenToImage(mx, my, col, row);
    zoom_ = new_zoom;
    double display_w = img_width_ * zoom_;
    double display_h = img_height_ * zoom_;
    offset_x_ = mx - x() - (w() - display_w) / 2.0 - col * zoom_;
    offset_y_ = my - y() - (h() - display_h) / 2.0 - row * zoom_;
    redraw();
  }

  int hitTestMarker(int mx, int my) const {
    if (!points_) return -1;
    int best = -1;
    double best_dist = 10.0;  // screen pixels
    for (size_t i = 0; i < points_->size(); ++i) {
      double sx, sy;
      imageToScreen((*points_)[i].col, (*points_)[i].row, sx, sy);
      double dist = std::hypot(sx - mx, sy - my);
      if (dist < best_dist) {
        best_dist = dist;
        best = static_cast<int>(i);
      }
    }
    return best;
  }

  void drawMarkers() {
    if (!points_) return;
    for (size_t i = 0; i < points_->size(); ++i) {
      double sx, sy;
      imageToScreen((*points_)[i].col, (*points_)[i].row, sx, sy);
      int ix = static_cast<int>(std::lround(sx));
      int iy = static_cast<int>(std::lround(sy));
      bool selected = (static_cast<int>(i) == selected_index_);
      Fl_Color mcolor = selected ? FL_YELLOW : FL_RED;
      fl_color(mcolor);
      fl_line(ix - 7, iy, ix + 7, iy);
      fl_line(ix, iy - 7, ix, iy + 7);
      fl_font(FL_HELVETICA_BOLD, 12);
      char label[8];
      snprintf(label, sizeof(label), "%d", static_cast<int>(i + 1));
      fl_draw(label, ix + 8, iy - 8);
    }
  }

  Fl_Shared_Image* image_ = nullptr;
  Fl_Image* scaled_cache_ = nullptr;
  double scaled_cache_zoom_ = -1.0;
  std::string image_path_;
  int img_width_ = 0;
  int img_height_ = 0;

  double zoom_ = 1.0;
  double offset_x_ = 0.0, offset_y_ = 0.0;
  double draw_x_ = 0.0, draw_y_ = 0.0;

  bool dragging_ = false;
  bool moved_ = false;
  int press_x_ = 0, press_y_ = 0;
  double offset_start_x_ = 0.0, offset_start_y_ = 0.0;

  bool mark_mode_ = false;
  const std::vector<ControlPoint>* points_ = nullptr;
  int selected_index_ = -1;

  static constexpr double kWheelPanStep = 30.0;  // screen pixels per wheel notch

  std::function<void(double, double)> on_point_click_;
  std::function<void(int)> on_point_right_click_;
};

// Modal dialog to enter (or edit) the known geographic coordinate of a
// freshly marked point.
class GCPDialog : public Fl_Double_Window {
 public:
  GCPDialog(double pixel_col, double pixel_row, double init_x, double init_y, bool has_init)
      : Fl_Double_Window(360, 190, "Control Point Coordinates") {
    char info[128];
    snprintf(info, sizeof(info), "Image pixel:  column %.1f,  row %.1f", pixel_col, pixel_row);
    Fl_Box* info_box = new Fl_Box(10, 12, 340, 24, "");
    info_box->copy_label(info);
    info_box->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    x_input_ = new Fl_Float_Input(160, 50, 180, 26, "X coordinate (East/Lon.):");
    y_input_ = new Fl_Float_Input(160, 86, 180, 26, "Y coordinate (North/Lat.):");
    if (has_init) {
      char buf[64];
      snprintf(buf, sizeof(buf), "%.6f", init_x);
      x_input_->value(buf);
      snprintf(buf, sizeof(buf), "%.6f", init_y);
      y_input_->value(buf);
    }

    Fl_Return_Button* ok = new Fl_Return_Button(90, 138, 90, 32, "OK");
    ok->callback(cbOk, this);
    Fl_Button* cancel = new Fl_Button(190, 138, 90, 32, "Cancel");
    cancel->callback(cbCancel, this);

    set_modal();
    end();
  }

  bool accepted = false;
  double result_x = 0.0;
  double result_y = 0.0;

 private:
  static void cbOk(Fl_Widget*, void* d) {
    GCPDialog* dlg = static_cast<GCPDialog*>(d);
    const char* xs = dlg->x_input_->value();
    const char* ys = dlg->y_input_->value();
    if (!xs || !*xs || !ys || !*ys) {
      fl_alert("Please enter both coordinates (X and Y).");
      return;
    }
    char* endx = nullptr;
    char* endy = nullptr;
    double vx = std::strtod(xs, &endx);
    double vy = std::strtod(ys, &endy);
    if (endx == xs || endy == ys) {
      fl_alert("Coordinates must be valid numbers.");
      return;
    }
    dlg->result_x = vx;
    dlg->result_y = vy;
    dlg->accepted = true;
    dlg->hide();
  }

  static void cbCancel(Fl_Widget*, void* d) { static_cast<GCPDialog*>(d)->hide(); }

  Fl_Float_Input* x_input_ = nullptr;
  Fl_Float_Input* y_input_ = nullptr;
};


// A palette color (index -> real RGB), used by the "true color" export
// mode.
struct PaletteColor {
  unsigned char r = 0, g = 0, b = 0;
};

// Reduces a list of colors (packed as 0xRRGGBB) with their frequency to at
// most max_colors representative colors, using the "median cut" algorithm
// (the same technique GIF/PNG-8 encoders use): recursively splits the
// color box with the largest range along its widest channel -- at the
// point that leaves half the WEIGHT (cell count) on each side, not just
// half the distinct colors -- until reaching max_colors boxes, and returns
// the average color (weighted by frequency) of each one. If there are
// already max_colors or fewer distinct colors, the caller doesn't need
// this: it just uses the exact colors as-is.
inline std::vector<PaletteColor> medianCutQuantize(
    const std::vector<std::pair<uint32_t, long long>>& colors_with_counts, int max_colors) {
  struct Box {
    std::vector<int> indices;
    unsigned char rmin = 255, rmax = 0, gmin = 255, gmax = 0, bmin = 255, bmax = 0;
  };

  auto unpack = [](uint32_t c, unsigned char& r, unsigned char& g, unsigned char& b) {
    r = static_cast<unsigned char>((c >> 16) & 0xFF);
    g = static_cast<unsigned char>((c >> 8) & 0xFF);
    b = static_cast<unsigned char>(c & 0xFF);
  };

  auto makeBox = [&](std::vector<int> idx) {
    Box box;
    box.indices = std::move(idx);
    for (int i : box.indices) {
      unsigned char r, g, b;
      unpack(colors_with_counts[i].first, r, g, b);
      box.rmin = std::min(box.rmin, r);
      box.rmax = std::max(box.rmax, r);
      box.gmin = std::min(box.gmin, g);
      box.gmax = std::max(box.gmax, g);
      box.bmin = std::min(box.bmin, b);
      box.bmax = std::max(box.bmax, b);
    }
    return box;
  };

  std::vector<int> all_idx(colors_with_counts.size());
  for (size_t i = 0; i < colors_with_counts.size(); ++i) all_idx[i] = static_cast<int>(i);
  std::vector<Box> boxes;
  boxes.push_back(makeBox(std::move(all_idx)));

  while (static_cast<int>(boxes.size()) < max_colors) {
    int best = -1;
    int best_range = 0;
    for (size_t i = 0; i < boxes.size(); ++i) {
      if (boxes[i].indices.size() < 2) continue;
      int rr = boxes[i].rmax - boxes[i].rmin;
      int gr = boxes[i].gmax - boxes[i].gmin;
      int br = boxes[i].bmax - boxes[i].bmin;
      int range = std::max({rr, gr, br});
      if (range > best_range) {
        best_range = range;
        best = static_cast<int>(i);
      }
    }
    if (best < 0) break;  // no remaining box is worth splitting

    Box box = std::move(boxes[best]);
    boxes.erase(boxes.begin() + best);

    int rr = box.rmax - box.rmin, gr = box.gmax - box.gmin, br = box.bmax - box.bmin;
    int channel = 0;  // 0=R, 1=G, 2=B
    if (gr >= rr && gr >= br) channel = 1;
    else if (br >= rr && br >= gr) channel = 2;

    std::sort(box.indices.begin(), box.indices.end(), [&](int a, int c2) {
      unsigned char ra, ga, ba, rc, gc, bc;
      unpack(colors_with_counts[a].first, ra, ga, ba);
      unpack(colors_with_counts[c2].first, rc, gc, bc);
      unsigned char va = (channel == 0) ? ra : (channel == 1 ? ga : ba);
      unsigned char vc = (channel == 0) ? rc : (channel == 1 ? gc : bc);
      return va < vc;
    });

    // Frequency-weighted cut (weighted median by cell count), not by
    // number of distinct colors -- a very common color pulls its actual
    // weight in the split.
    long long total = 0;
    for (int i : box.indices) total += colors_with_counts[i].second;
    long long half = total / 2;
    long long running = 0;
    size_t split_at = box.indices.size() / 2;
    for (size_t i = 0; i < box.indices.size(); ++i) {
      running += colors_with_counts[box.indices[i]].second;
      if (running >= half) {
        split_at = i + 1;
        break;
      }
    }
    split_at = std::max<size_t>(1, std::min(split_at, box.indices.size() - 1));

    std::vector<int> left(box.indices.begin(), box.indices.begin() + split_at);
    std::vector<int> right(box.indices.begin() + split_at, box.indices.end());
    boxes.push_back(makeBox(std::move(left)));
    boxes.push_back(makeBox(std::move(right)));
  }

  std::vector<PaletteColor> palette;
  palette.reserve(boxes.size());
  for (const auto& box : boxes) {
    double sr = 0, sg = 0, sb = 0;
    long long total = 0;
    for (int i : box.indices) {
      unsigned char r, g, b;
      unpack(colors_with_counts[i].first, r, g, b);
      long long cnt = colors_with_counts[i].second;
      sr += r * cnt;
      sg += g * cnt;
      sb += b * cnt;
      total += cnt;
    }
    PaletteColor pc;
    if (total > 0) {
      pc.r = static_cast<unsigned char>(std::lround(sr / total));
      pc.g = static_cast<unsigned char>(std::lround(sg / total));
      pc.b = static_cast<unsigned char>(std::lround(sb / total));
    }
    palette.push_back(pc);
  }
  return palette;
}

// Index of the palette color nearest (squared Euclidean distance in RGB)
// to (r, g, b). Linear search: with at most 256 colors in the palette this
// is cheap per cell, and the caller also caches by exact source color (see
// exportASC), so in practice this only runs once per distinct color that
// appears in the image, not once per cell.
inline int nearestPaletteIndex(const std::vector<PaletteColor>& palette, unsigned char r, unsigned char g,
                                unsigned char b) {
  int best = 0;
  long best_dist = std::numeric_limits<long>::max();
  for (size_t i = 0; i < palette.size(); ++i) {
    long dr = static_cast<long>(palette[i].r) - r;
    long dg = static_cast<long>(palette[i].g) - g;
    long db = static_cast<long>(palette[i].b) - b;
    long dist = dr * dr + dg * dg + db * db;
    if (dist < best_dist) {
      best_dist = dist;
      best = static_cast<int>(i);
      if (dist == 0) break;
    }
  }
  return best;
}

// Main window: image area on the left, control panel on the right (load
// image, zoom, control point list, export).
class MainWindow : public Fl_Double_Window {
 public:
  MainWindow(int W, int H, const char* L = nullptr) : Fl_Double_Window(W, H, L) {
    const int mb_h = 26;
    menu_bar_ = new Fl_Menu_Bar(0, 0, W, mb_h);
    menu_bar_->add("&File/&Load Image...", 0, cbLoad, this);
    menu_bar_->add("&File/&Export .asc...", 0, cbExport, this, FL_MENU_DIVIDER);
    menu_bar_->add("&Zoom/Zoom &In", "+", cbZoomIn, this);
    menu_bar_->add("&Zoom/Zoom &Out", "-", cbZoomOut, this);
    menu_bar_->add("&Zoom/&Reset View", "r", cbReset, this);

    canvas_ = new ImageCanvas(10, 10 + mb_h, W - 300, H - 20 - mb_h);
    canvas_->setPoints(&points_);
    canvas_->setOnPointClick([this](double col, double row) { onCanvasPointClick(col, row); });
    canvas_->setOnPointRightClick([this](int idx) { deletePoint(idx); });

    const int px = W - 280;
    const int pw = 270;
    int y = 10 + mb_h;

    Fl_Box* zoom_label = new Fl_Box(px, y, pw, 16, "Zoom (see also the Zoom menu)");
    zoom_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += 16 + 4;

    zoom_slider_ = new Fl_Slider(px, y, pw - 70, 24);
    zoom_slider_->type(FL_HORIZONTAL);
    zoom_slider_->bounds(0.02, 10.0);
    zoom_slider_->value(1.0);
    zoom_slider_->callback(cbZoomSlider, this);
    zoom_value_ = new Fl_Value_Output(px + pw - 65, y, 65, 24);
    zoom_value_->precision(0);
    zoom_value_->value(100.0);
    y += 24 + 14;

    Fl_Box* gcp_label = new Fl_Box(px, y, pw, 16, "Control Points (GCP)");
    gcp_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += 16 + 4;

    chk_mark_mode_ = new Fl_Check_Button(px, y, pw, 22, "Mark mode: click to add");
    chk_mark_mode_->callback(cbMarkMode, this);
    y += 22 + 6;

    help_box_ = new Fl_Box(px, y, pw, 80,
                            "Wheel: zoom (at cursor)\n"
                            "Shift+wheel or trackpad swipe: pan\n"
                            "Drag: pan\n"
                            "Mark mode + click: add point\n"
                            "Mark mode + right click: delete nearest\n"
                            "Keys: +/- zoom, R resets view\n"
                            "File and Zoom menus: load image, export, zoom");
    help_box_->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    help_box_->labelsize(11);
    help_box_->labelfont(FL_HELVETICA_ITALIC);
    y += 80 + 10;

    point_list_ = new Fl_Hold_Browser(px, y, pw, 150);
    {
      static int widths[] = {24, 95, 75, 75, 0};
      point_list_->column_widths(widths);
    }
    point_list_->callback(cbPointSelect, this);
    y += 150 + 8;

    btn_delete_point_ = new Fl_Button(px, y, 130, 26, "Delete point");
    btn_delete_point_->callback(cbDeleteSelected, this);
    btn_delete_all_ = new Fl_Button(px + 138, y, 132, 26, "Delete all");
    btn_delete_all_->callback(cbDeleteAll, this);
    y += 26 + 14;

    Fl_Box* edit_label = new Fl_Box(px, y, pw, 16, "Edit selected point");
    edit_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += 16 + 4;

    edit_x_ = new Fl_Float_Input(px + 24, y, pw - 24, 24, "X:");
    y += 24 + 6;
    edit_y_ = new Fl_Float_Input(px + 24, y, pw - 24, 24, "Y:");
    y += 24 + 8;

    btn_update_point_ = new Fl_Button(px, y, pw, 26, "Update coordinates");
    btn_update_point_->callback(cbUpdateSelected, this);
    y += 26 + 14;

    btn_export_points_ = new Fl_Button(px, y, 130, 24, "Save points (CSV)");
    btn_export_points_->callback(cbExportPoints, this);
    btn_import_points_ = new Fl_Button(px + 138, y, 132, 24, "Load points (CSV)");
    btn_import_points_->callback(cbImportPoints, this);
    y += 24 + 14;

    Fl_Box* out_label = new Fl_Box(px, y, pw, 16, "Export .asc raster");
    out_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += 16 + 4;

    mode_choice_ = new Fl_Choice(px + 110, y, pw - 110, 24, "Values:");
    mode_choice_->add("True color (palette + RAT)");
    mode_choice_->add("Grayscale (numeric)");
    mode_choice_->value(0);
    mode_choice_->callback(cbModeChoice, this);
    y += 24 + 6;

    chk_auto_cellsize_ = new Fl_Check_Button(px, y, pw, 22, "Automatic cell size");
    chk_auto_cellsize_->value(1);
    chk_auto_cellsize_->callback(cbAutoCellsize, this);
    y += 22 + 6;

    input_cellsize_ = new Fl_Float_Input(px + 110, y, pw - 110, 24, "Cellsize:");
    input_cellsize_->deactivate();
    y += 24 + 6;

    input_nodata_ = new Fl_Float_Input(px + 110, y, pw - 110, 24, "NODATA:");
    input_nodata_->value("-9999");
    y += 24 + 6;

    input_crs_ = new Fl_Input(px + 110, y, pw - 110, 24, "CRS:");
    y += 24 + 10;

    chk_include_rat_ = new Fl_Check_Button(px, y, pw, 22, "Include RAT table (colors)");
    chk_include_rat_->value(1);
    chk_include_rat_->deactivate();  // mandatory in "True color" mode: the value
                                     // by itself is just a palette index, meaningless without the RAT.
    y += 22 + 10;

    Fl_Box* export_hint = new Fl_Box(px, y, pw, 18, "Use File > Export .asc... to save");
    export_hint->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    export_hint->labelsize(11);
    export_hint->labelfont(FL_HELVETICA_ITALIC);
    y += 18 + 14;

    status_label_ = new Fl_Box(px, y, pw, std::max(60, H - y - 10),
                                "Load an image and mark at least 2 control points.");
    status_label_->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    status_label_->box(FL_BORDER_BOX);

    size_range(760, 560);
    resizable(canvas_);
    end();
    show();
    Fl::focus(canvas_);
  }

  bool loadImage(const std::string& path) {
    if (!canvas_->loadImage(path)) {
      fl_alert("Could not load the image: %s", path.c_str());
      return false;
    }
    points_.clear();
    refreshPointList();
    canvas_->setSelectedIndex(-1);
    updateStatus("Image loaded: " + path + "  (" + std::to_string(canvas_->imageWidth()) + "x" +
                 std::to_string(canvas_->imageHeight()) + " px). Mark at least 2 control points.");
    zoom_slider_->value(1.0);
    zoom_value_->value(100.0);
    return true;
  }

 private:
  // ---- static callbacks ----
  static void cbLoad(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->onLoad(); }

  static void cbZoomSlider(Fl_Widget* w, void* d) {
    MainWindow* win = static_cast<MainWindow*>(d);
    double z = static_cast<Fl_Slider*>(w)->value();
    win->canvas_->setZoom(z);
    win->zoom_value_->value(z * 100.0);
  }

  static void cbZoomIn(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->syncZoom(1.25); }
  static void cbZoomOut(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->syncZoom(1.0 / 1.25); }

  static void cbReset(Fl_Widget*, void* d) {
    MainWindow* win = static_cast<MainWindow*>(d);
    win->canvas_->resetView();
    win->zoom_slider_->value(1.0);
    win->zoom_value_->value(100.0);
  }

  static void cbMarkMode(Fl_Widget* w, void* d) {
    static_cast<MainWindow*>(d)->canvas_->setMarkMode(static_cast<Fl_Check_Button*>(w)->value());
  }

  static void cbPointSelect(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->onPointSelect(); }
  static void cbDeleteSelected(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->deleteSelected(); }
  static void cbDeleteAll(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->deleteAll(); }
  static void cbUpdateSelected(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->updateSelected(); }
  static void cbExportPoints(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->exportPointsCSV(); }
  static void cbImportPoints(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->importPointsCSV(); }

  static void cbModeChoice(Fl_Widget* w, void* d) {
    MainWindow* win = static_cast<MainWindow*>(d);
    int mode = static_cast<Fl_Choice*>(w)->value();
    if (mode == 0) {  // True color: the RAT is mandatory, not optional
      win->chk_include_rat_->value(1);
      win->chk_include_rat_->deactivate();
    } else {  // Grayscale: the RAT is an optional visual aid
      win->chk_include_rat_->activate();
    }
  }

  static void cbAutoCellsize(Fl_Widget* w, void* d) {
    MainWindow* win = static_cast<MainWindow*>(d);
    if (static_cast<Fl_Check_Button*>(w)->value())
      win->input_cellsize_->deactivate();
    else
      win->input_cellsize_->activate();
  }

  static void cbExport(Fl_Widget*, void* d) { static_cast<MainWindow*>(d)->exportASC(); }

  // ---- actions ----
  void onLoad() {
    Fl_File_Chooser chooser(current_directory_.c_str(), "Images (*.{png,jpg,jpeg})", Fl_File_Chooser::SINGLE,
                             "Load image");
    chooser.show();
    while (chooser.shown()) Fl::wait();
    if (chooser.count() <= 0 || !chooser.value()) return;
    std::string filename = chooser.value();
    current_directory_ = std::filesystem::path(filename).parent_path().string();
    loadImage(filename);
  }

  void syncZoom(double factor) {
    canvas_->setZoom(canvas_->getZoom() * factor);
    zoom_slider_->value(canvas_->getZoom());
    zoom_value_->value(canvas_->getZoom() * 100.0);
  }

  void onCanvasPointClick(double col, double row) {
    double init_x = 0.0, init_y = 0.0;
    bool has_init = !points_.empty();
    if (has_init) {
      init_x = points_.back().x;
      init_y = points_.back().y;
    }
    GCPDialog dlg(col, row, init_x, init_y, has_init);
    dlg.show();
    while (dlg.shown()) Fl::wait();
    if (!dlg.accepted) return;

    ControlPoint p;
    p.col = col;
    p.row = row;
    p.x = dlg.result_x;
    p.y = dlg.result_y;
    points_.push_back(p);
    refreshPointList();
    point_list_->select(static_cast<int>(points_.size()));
    canvas_->setSelectedIndex(static_cast<int>(points_.size()) - 1);
    updateStatus("Point " + std::to_string(points_.size()) + " added. Total: " +
                 std::to_string(points_.size()) + " points.");
  }

  void onPointSelect() {
    int sel = point_list_->value();
    if (sel <= 0 || sel > static_cast<int>(points_.size())) {
      canvas_->setSelectedIndex(-1);
      return;
    }
    const ControlPoint& p = points_[sel - 1];
    char buf[64];
    snprintf(buf, sizeof(buf), "%.6f", p.x);
    edit_x_->value(buf);
    snprintf(buf, sizeof(buf), "%.6f", p.y);
    edit_y_->value(buf);
    canvas_->setSelectedIndex(sel - 1);
  }

  void deleteSelected() {
    int sel = point_list_->value();
    if (sel <= 0 || sel > static_cast<int>(points_.size())) {
      fl_alert("Select a point from the list first.");
      return;
    }
    deletePoint(sel - 1);
  }

  void deletePoint(int index) {
    if (index < 0 || index >= static_cast<int>(points_.size())) return;
    points_.erase(points_.begin() + index);
    refreshPointList();
    canvas_->setSelectedIndex(-1);
    updateStatus("Point deleted. " + std::to_string(points_.size()) + " points remain.");
  }

  void deleteAll() {
    if (points_.empty()) return;
    if (fl_choice("Delete all %d control points?", "Cancel", "Delete", nullptr,
                  static_cast<int>(points_.size())) != 1)
      return;
    points_.clear();
    refreshPointList();
    canvas_->setSelectedIndex(-1);
    updateStatus("All points were deleted.");
  }

  void updateSelected() {
    int sel = point_list_->value();
    if (sel <= 0 || sel > static_cast<int>(points_.size())) {
      fl_alert("Select a point from the list first.");
      return;
    }
    const char* xs = edit_x_->value();
    const char* ys = edit_y_->value();
    char* endx = nullptr;
    char* endy = nullptr;
    double vx = std::strtod(xs, &endx);
    double vy = std::strtod(ys, &endy);
    if (endx == xs || endy == ys) {
      fl_alert("Coordinates must be valid numbers.");
      return;
    }
    points_[sel - 1].x = vx;
    points_[sel - 1].y = vy;
    refreshPointList();
    point_list_->select(sel);
    canvas_->redraw();
    updateStatus("Coordinates of point " + std::to_string(sel) + " updated.");
  }

  void refreshPointList() {
    point_list_->clear();
    for (size_t i = 0; i < points_.size(); ++i) {
      const ControlPoint& p = points_[i];
      char line[160];
      snprintf(line, sizeof(line), "%d\t%.0f,%.0f\t%.3f\t%.3f", static_cast<int>(i + 1), p.col, p.row, p.x, p.y);
      point_list_->add(line);
    }
    canvas_->redraw();
  }

  void exportPointsCSV() {
    if (points_.empty()) {
      fl_alert("There are no control points to save.");
      return;
    }
    Fl_File_Chooser chooser(current_directory_.c_str(), "CSV files (*.csv)", Fl_File_Chooser::CREATE,
                             "Save control points");
    chooser.show();
    while (chooser.shown()) Fl::wait();
    if (chooser.count() <= 0 || !chooser.value()) return;
    std::string filename = chooser.value();
    if (filename.size() < 4 || filename.substr(filename.size() - 4) != ".csv") filename += ".csv";
    std::ofstream out(filename);
    if (!out.is_open()) {
      fl_alert("Could not create the file: %s", filename.c_str());
      return;
    }
    out << "pixel_col,pixel_row,geo_x,geo_y\n";
    out << std::fixed << std::setprecision(6);
    for (const auto& p : points_) out << p.col << "," << p.row << "," << p.x << "," << p.y << "\n";
    current_directory_ = std::filesystem::path(filename).parent_path().string();
    updateStatus("Points saved to: " + filename);
  }

  void importPointsCSV() {
    Fl_File_Chooser chooser(current_directory_.c_str(), "CSV files (*.csv)", Fl_File_Chooser::SINGLE,
                             "Load control points");
    chooser.show();
    while (chooser.shown()) Fl::wait();
    if (chooser.count() <= 0 || !chooser.value()) return;
    std::string filename = chooser.value();
    std::ifstream in(filename);
    if (!in.is_open()) {
      fl_alert("Could not open the file: %s", filename.c_str());
      return;
    }
    std::vector<ControlPoint> loaded;
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
      if (line.empty()) continue;
      if (first) {
        first = false;
        if (!std::isdigit(static_cast<unsigned char>(line[0])) && line[0] != '-') continue;  // skip header
      }
      std::istringstream ss(line);
      std::string tok;
      std::vector<double> vals;
      bool ok = true;
      while (std::getline(ss, tok, ',')) {
        try {
          vals.push_back(std::stod(tok));
        } catch (...) {
          ok = false;
          break;
        }
      }
      if (!ok || vals.size() != 4) continue;
      ControlPoint p;
      p.col = vals[0];
      p.row = vals[1];
      p.x = vals[2];
      p.y = vals[3];
      loaded.push_back(p);
    }
    if (loaded.empty()) {
      fl_alert("No valid points were found in the file.");
      return;
    }
    points_ = loaded;
    refreshPointList();
    canvas_->setSelectedIndex(-1);
    current_directory_ = std::filesystem::path(filename).parent_path().string();
    updateStatus("Loaded " + std::to_string(points_.size()) + " points from: " + filename);
  }

  struct AffineFit {
    double a = 0, b = 0, c = 0, d = 0;
    double rms_x = 0, rms_y = 0;
  };

  static bool fitAxis(const std::vector<double>& indep, const std::vector<double>& dep, double& slope,
                       double& intercept, double& rms) {
    int n = static_cast<int>(indep.size());
    double sum_i = 0, sum_i2 = 0, sum_d = 0, sum_id = 0;
    for (int k = 0; k < n; ++k) {
      sum_i += indep[k];
      sum_i2 += indep[k] * indep[k];
      sum_d += dep[k];
      sum_id += indep[k] * dep[k];
    }
    double denom = n * sum_i2 - sum_i * sum_i;
    if (std::fabs(denom) < 1e-9) return false;
    slope = (n * sum_id - sum_i * sum_d) / denom;
    intercept = (sum_d - slope * sum_i) / n;
    if (std::fabs(slope) < 1e-12) return false;
    double sse = 0;
    for (int k = 0; k < n; ++k) {
      double diff = (slope * indep[k] + intercept) - dep[k];
      sse += diff * diff;
    }
    rms = std::sqrt(sse / n);
    return true;
  }

  bool computeAffine(AffineFit& fit, std::string& error_msg) {
    int n = static_cast<int>(points_.size());
    if (n < 2) {
      error_msg = "At least 2 control points are needed.";
      return false;
    }
    std::vector<double> cols, rows, xs, ys;
    cols.reserve(n);
    rows.reserve(n);
    xs.reserve(n);
    ys.reserve(n);
    for (const auto& p : points_) {
      cols.push_back(p.col);
      rows.push_back(p.row);
      xs.push_back(p.x);
      ys.push_back(p.y);
    }
    if (!fitAxis(cols, xs, fit.a, fit.b, fit.rms_x)) {
      error_msg =
          "The control points don't vary enough in column to fit the horizontal scale. "
          "Mark points in different columns of the image.";
      return false;
    }
    if (!fitAxis(rows, ys, fit.c, fit.d, fit.rms_y)) {
      error_msg =
          "The control points don't vary enough in row to fit the vertical scale. "
          "Mark points in different rows of the image.";
      return false;
    }
    return true;
  }

  void exportASC() {
    if (!canvas_->hasImage()) {
      fl_alert("Load an image first.");
      return;
    }
    AffineFit fit;
    std::string err;
    if (!computeAffine(fit, err)) {
      fl_alert("%s", err.c_str());
      return;
    }

    double pixel_w = std::fabs(fit.a);
    double pixel_h = std::fabs(fit.c);
    double cellsize;
    if (chk_auto_cellsize_->value()) {
      cellsize = (pixel_w + pixel_h) / 2.0;
    } else {
      cellsize = std::atof(input_cellsize_->value());
      if (!(cellsize > 0.0)) {
        fl_alert("Enter a valid manual cell size (greater than 0) or enable 'automatic'.");
        return;
      }
    }

    int iw = canvas_->imageWidth();
    int ih = canvas_->imageHeight();
    double x0 = fit.b, x1 = fit.a * iw + fit.b;
    double y0 = fit.d, y1 = fit.c * ih + fit.d;
    double minx = std::min(x0, x1), maxx = std::max(x0, x1);
    double miny = std::min(y0, y1), maxy = std::max(y0, y1);

    int out_ncols = std::max(1, static_cast<int>(std::lround((maxx - minx) / cellsize)));
    int out_nrows = std::max(1, static_cast<int>(std::lround((maxy - miny) / cellsize)));

    long long total_cells = static_cast<long long>(out_ncols) * static_cast<long long>(out_nrows);
    if (total_cells > 40000000LL) {
      if (fl_choice(
              "The resulting grid would be %d x %d cells (%lld total), which may take quite a while "
              "and use a lot of memory.\n\nContinue anyway?",
              "Cancel", "Continue", nullptr, out_ncols, out_nrows, total_cells) != 1) {
        return;
      }
    }

    const char* nodata_text = input_nodata_->value();
    double nodata = (nodata_text && *nodata_text) ? std::atof(nodata_text) : -9999.0;
    nodata = std::round(nodata);  // cell values are integers now (see sampleGray)

    Spatial::RasterDataset dataset;
    dataset.ncols = out_ncols;
    dataset.nrows = out_nrows;
    dataset.xllcorner = minx;
    dataset.yllcorner = miny;
    dataset.cellsize = cellsize;
    dataset.nodata_value = nodata;
    dataset.data.assign(static_cast<size_t>(out_ncols) * static_cast<size_t>(out_nrows), nodata);
    dataset.crs = input_crs_->value() ? input_crs_->value() : "";

    bool true_color_mode = (mode_choice_->value() == 0);
    int rat_value_count = 0;

    if (true_color_mode) {
      // "True color" mode: each cell's value is a palette index, not a
      // magnitude -- the RAT is what gives it meaning, so it's always
      // written here (chk_include_rat_ is disabled in this mode, see
      // cbModeChoice).
      //
      // Step 1: walk the output grid and collect, with their frequency,
      // the RGB colors actually sampled from the source image (not the
      // cell value yet -- that's decided after the palette is built).
      std::unordered_map<uint32_t, long long> color_counts;
      for (int r = 0; r < out_nrows; ++r) {
        double Y = dataset.yllcorner + (out_nrows - r - 0.5) * cellsize;
        double src_row = (Y - fit.d) / fit.c;
        for (int c = 0; c < out_ncols; ++c) {
          double X = dataset.xllcorner + (c + 0.5) * cellsize;
          double src_col = (X - fit.b) / fit.a;
          unsigned char pr, pg, pb;
          if (canvas_->sampleRGB(src_col, src_row, pr, pg, pb)) {
            uint32_t packed = (static_cast<uint32_t>(pr) << 16) | (static_cast<uint32_t>(pg) << 8) | pb;
            color_counts[packed]++;
          }
        }
      }

      if (color_counts.empty()) {
        fl_alert("The output area doesn't fall over any pixel of the image -- check the control points.");
        return;
      }

      std::vector<std::pair<uint32_t, long long>> flat(color_counts.begin(), color_counts.end());
      std::vector<PaletteColor> palette;
      const int kMaxColors = 256;  // practical limit for a RAT (see ADR-0004)
      if (static_cast<int>(flat.size()) <= kMaxColors) {
        // Few distinct colors (typical of a scanned map or a screenshot):
        // used as-is, nothing lost.
        std::sort(flat.begin(), flat.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& entry : flat) {
          PaletteColor pc;
          pc.r = static_cast<unsigned char>((entry.first >> 16) & 0xFF);
          pc.g = static_cast<unsigned char>((entry.first >> 8) & 0xFF);
          pc.b = static_cast<unsigned char>(entry.first & 0xFF);
          palette.push_back(pc);
        }
      } else {
        // Near-continuous-tone photo: quantized to 256 representative
        // colors (median cut), same as a GIF/PNG-8.
        palette = medianCutQuantize(flat, kMaxColors);
      }

      // Step 2: walk the grid again, assigning each cell the palette index
      // nearest to its source color. Cached by exact source color (not by
      // cell) because many cells tend to share the same source pixel or
      // very repeated colors.
      std::unordered_map<uint32_t, int> nearest_cache;
      std::vector<long long> index_counts(palette.size(), 0);
      for (int r = 0; r < out_nrows; ++r) {
        double Y = dataset.yllcorner + (out_nrows - r - 0.5) * cellsize;
        double src_row = (Y - fit.d) / fit.c;
        for (int c = 0; c < out_ncols; ++c) {
          double X = dataset.xllcorner + (c + 0.5) * cellsize;
          double src_col = (X - fit.b) / fit.a;
          unsigned char pr, pg, pb;
          if (!canvas_->sampleRGB(src_col, src_row, pr, pg, pb)) continue;
          uint32_t packed = (static_cast<uint32_t>(pr) << 16) | (static_cast<uint32_t>(pg) << 8) | pb;
          int idx;
          auto it = nearest_cache.find(packed);
          if (it != nearest_cache.end()) {
            idx = it->second;
          } else {
            idx = nearestPaletteIndex(palette, pr, pg, pb);
            nearest_cache[packed] = idx;
          }
          dataset.at(r, c) = static_cast<double>(idx);
          index_counts[idx]++;
        }
      }

      dataset.rat_columns = {"value", "color", "count"};
      dataset.rat_rows.reserve(palette.size());
      for (size_t i = 0; i < palette.size(); ++i) {
        if (index_counts[i] == 0) continue;  // no cell ended up using this palette color
        char hex[8];
        snprintf(hex, sizeof(hex), "%02X%02X%02X", palette[i].r, palette[i].g, palette[i].b);
        std::unordered_map<std::string, std::string> row;
        row["value"] = std::to_string(i);
        row["color"] = hex;
        row["count"] = std::to_string(index_counts[i]);
        dataset.rat_rows.push_back(std::move(row));
      }
      dataset.has_rat = true;
      rat_value_count = static_cast<int>(dataset.rat_rows.size());
    } else {
      // "Grayscale" mode: the cell value is a numeric magnitude in its own
      // right (useful for spatial_calc and similar tools), with an
      // optional grayscale RAT just so it also looks right in a viewer.
      std::map<int, long long> value_counts;  // gray value -> cell count
      for (int r = 0; r < out_nrows; ++r) {
        double Y = dataset.yllcorner + (out_nrows - r - 0.5) * cellsize;
        double src_row = (Y - fit.d) / fit.c;
        for (int c = 0; c < out_ncols; ++c) {
          double X = dataset.xllcorner + (c + 0.5) * cellsize;
          double src_col = (X - fit.b) / fit.a;
          double value;
          if (canvas_->sampleGray(src_col, src_row, value)) {
            dataset.at(r, c) = value;
            value_counts[static_cast<int>(std::lround(value))]++;
          }
        }
      }

      if (chk_include_rat_->value() && !value_counts.empty()) {
        // Grayscale palette RAT: one row per distinct gray value (0-255,
        // at most 256 rows), with its hex color code 'RRGGBB' repeating
        // that same value in R, G and B -- so spatial_viewer can show the
        // raster in its real grayscale (choosing 'color' as "Color field")
        // instead of the default blue-green-red gradient it uses for any
        // raster without a RAT (see ADR-0002).
        dataset.rat_columns = {"value", "color", "count"};
        dataset.rat_rows.reserve(value_counts.size());
        for (const auto& entry : value_counts) {
          int gray = entry.first;
          char hex[8];
          snprintf(hex, sizeof(hex), "%02X%02X%02X", gray, gray, gray);
          std::unordered_map<std::string, std::string> row;
          row["value"] = std::to_string(gray);
          row["color"] = hex;
          row["count"] = std::to_string(entry.second);
          dataset.rat_rows.push_back(std::move(row));
        }
        dataset.has_rat = true;
        rat_value_count = static_cast<int>(dataset.rat_rows.size());
      }
    }

    std::ostringstream desc;
    desc << "Georeferenced from '" << canvas_->imagePath() << "' with " << points_.size()
         << " control points (RMS X=" << std::fixed << std::setprecision(4) << fit.rms_x
         << ", RMS Y=" << fit.rms_y << "), mode "
         << (true_color_mode ? "true color (indexed palette)" : "grayscale");
    dataset.description = desc.str();

    Fl_File_Chooser chooser(current_directory_.c_str(), "ASCII Grid files (*.asc)", Fl_File_Chooser::CREATE,
                             "Save georeferenced raster");
    chooser.show();
    while (chooser.shown()) Fl::wait();
    if (chooser.count() <= 0 || !chooser.value()) return;
    std::string filename = chooser.value();
    if (filename.size() < 4 || filename.substr(filename.size() - 4) != ".asc") filename += ".asc";
    current_directory_ = std::filesystem::path(filename).parent_path().string();

    writeRasterASCII(dataset, filename, 0);

    char msg[600];
    if (true_color_mode) {
      snprintf(msg, sizeof(msg),
               "Raster saved to:\n%s\n\nDimensions: %d x %d cells\nCellsize: %.6f\n"
               "RMS error: X=%.4f  Y=%.4f\nTrue color palette: %d colors (RAT table value/color/count).\n\n"
               "To see it with its colors in spatial_viewer, open the layer's properties and choose "
               "'color' as the Color field (the cell value by itself is a palette index, not a magnitude).",
               filename.c_str(), out_ncols, out_nrows, cellsize, fit.rms_x, fit.rms_y, rat_value_count);
    } else if (rat_value_count > 0) {
      snprintf(msg, sizeof(msg),
               "Raster saved to:\n%s\n\nDimensions: %d x %d cells\nCellsize: %.6f\n"
               "RMS error: X=%.4f  Y=%.4f\nRAT table included: %d distinct gray values (columns value/color/count).\n"
               "To see its real tones in spatial_viewer, choose 'color' as the Color field "
               "in the layer's properties.",
               filename.c_str(), out_ncols, out_nrows, cellsize, fit.rms_x, fit.rms_y, rat_value_count);
    } else {
      snprintf(msg, sizeof(msg),
               "Raster saved to:\n%s\n\nDimensions: %d x %d cells\nCellsize: %.6f\nRMS error: X=%.4f  Y=%.4f",
               filename.c_str(), out_ncols, out_nrows, cellsize, fit.rms_x, fit.rms_y);
    }
    fl_message("%s", msg);
    updateStatus("Exported: " + filename);
  }

  void updateStatus(const std::string& text) { status_label_->copy_label(text.c_str()); }

  Fl_Menu_Bar* menu_bar_ = nullptr;
  ImageCanvas* canvas_ = nullptr;
  Fl_Slider* zoom_slider_ = nullptr;
  Fl_Value_Output* zoom_value_ = nullptr;
  Fl_Check_Button* chk_mark_mode_ = nullptr;
  Fl_Box* help_box_ = nullptr;
  Fl_Hold_Browser* point_list_ = nullptr;
  Fl_Button* btn_delete_point_ = nullptr;
  Fl_Button* btn_delete_all_ = nullptr;
  Fl_Float_Input* edit_x_ = nullptr;
  Fl_Float_Input* edit_y_ = nullptr;
  Fl_Button* btn_update_point_ = nullptr;
  Fl_Button* btn_export_points_ = nullptr;
  Fl_Button* btn_import_points_ = nullptr;
  Fl_Choice* mode_choice_ = nullptr;
  Fl_Check_Button* chk_auto_cellsize_ = nullptr;
  Fl_Check_Button* chk_include_rat_ = nullptr;
  Fl_Float_Input* input_cellsize_ = nullptr;
  Fl_Float_Input* input_nodata_ = nullptr;
  Fl_Input* input_crs_ = nullptr;
  Fl_Box* status_label_ = nullptr;

  std::vector<ControlPoint> points_;
  std::string current_directory_ = ".";
};

}  // namespace Georef

int main(int argc, char** argv) {
  fl_register_images();
  Fl::scheme("gtk+");
  Georef::MainWindow win(1100, 920, "Spatial Georeference - Spatial TEC");

  if (argc > 1) win.loadImage(argv[1]);

  return Fl::run();
}
