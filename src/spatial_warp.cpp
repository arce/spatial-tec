#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Slider.H>
#include <FL/Fl_Value_Output.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_Shared_Image.H>
#include <FL/fl_draw.H>
#include <iostream>
#include <cmath>

// Custom widget to display the image with zoom and pan
class ImageDisplay : public Fl_Box {
private:
    Fl_Shared_Image* image_;
    double zoom;           // Zoom factor (1.0 = 100%)
    int offset_x, offset_y; // Pan offset
    int img_width, img_height;
    int drag_start_x, drag_start_y;
    int offset_start_x, offset_start_y;
    bool dragging;

public:
    ImageDisplay(int x, int y, int w, int h, const char* label = 0)
        : Fl_Box(x, y, w, h, label),
          image_(nullptr),
          zoom(1.0),
          offset_x(0),
          offset_y(0),
          dragging(false) {
        box(FL_DOWN_BOX);
        color(FL_BACKGROUND2_COLOR);
        img_width = 0;
        img_height = 0;
    }

    ~ImageDisplay() {
        if (image_) {
            image_->release();
        }
    }

    // Load an image from a file
    bool loadImage(const char* filename) {
        if (image_) {
            image_->release();
            image_ = nullptr;
        }

        // Use Fl_Shared_Image to load PNG
        image_ = Fl_Shared_Image::get(filename);

        if (!image_ || image_->w() == 0 || image_->h() == 0) {
            std::cerr << "Error loading image: " << filename << std::endl;
            return false;
        }

        // Store original dimensions
        img_width = image_->w();
        img_height = image_->h();

        // Reset zoom and offset
        zoom = 1.0;
        offset_x = 0;
        offset_y = 0;

        redraw();
        return true;
    }

    // Zoom methods
    void zoomIn() {
        zoom *= 1.2; // Increase by 20%
        if (zoom > 10.0) zoom = 10.0;
        redraw();
    }

    void zoomOut() {
        zoom *= 0.8; // Decrease by 20%
        if (zoom < 0.1) zoom = 0.1;
        redraw();
    }

    void setZoom(double z) {
        zoom = z;
        if (zoom < 0.1) zoom = 0.1;
        if (zoom > 10.0) zoom = 10.0;
        redraw();
    }

    double getZoom() const { return zoom; }

    // Pan methods
    void setOffset(int x, int y) {
        offset_x = x;
        offset_y = y;
        redraw();
    }

    void resetView() {
        zoom = 1.0;
        offset_x = 0;
        offset_y = 0;
        redraw();
    }

protected:
    // Drawing method
    void draw() override {
        Fl_Box::draw(); // Draw the background

        if (!image_) {
            // Show a message if no image is loaded
            fl_color(FL_GRAY0);
            fl_font(FL_HELVETICA, 14);
            fl_draw("No image loaded",
                    x() + 10, y() + h()/2 + 5);
            return;
        }

        // Compute dimensions with zoom applied
        int display_w = (int)(img_width * zoom);
        int display_h = (int)(img_height * zoom);

        // Compute position to center initially or apply pan
        int draw_x = x() + (w() - display_w) / 2 + offset_x;
        int draw_y = y() + (h() - display_h) / 2 + offset_y;

        // Draw the scaled image
        fl_push_clip(x(), y(), w(), h());

        // Scale and draw the image
        fl_draw_image(image_->data(),
                      draw_x, draw_y,
                      display_w, display_h,
                      image_->d(),
                      image_->ld());

        fl_pop_clip();

        // Draw info bar
        char info[256];
        snprintf(info, sizeof(info),
                 "Zoom: %.1f%%  Size: %dx%d  Offset: (%d, %d)",
                 zoom * 100,
                 display_w, display_h,
                 offset_x, offset_y);

        fl_color(FL_WHITE);
        fl_rectf(x(), y(), w(), 25);
        fl_color(FL_BLACK);
        fl_font(FL_HELVETICA, 11);
        fl_draw(info, x() + 5, y() + 17);
    }

    // Handle mouse events
    int handle(int event) override {
        switch (event) {
            case FL_PUSH: {
                if (Fl::event_button() == FL_LEFT_MOUSE) {
                    // Start dragging for pan
                    dragging = true;
                    drag_start_x = Fl::event_x();
                    drag_start_y = Fl::event_y();
                    offset_start_x = offset_x;
                    offset_start_y = offset_y;
                    return 1;
                }
                break;
            }

            case FL_DRAG: {
                if (dragging) {
                    // Compute new offset
                    int dx = Fl::event_x() - drag_start_x;
                    int dy = Fl::event_y() - drag_start_y;
                    offset_x = offset_start_x + dx;
                    offset_y = offset_start_y + dy;
                    redraw();
                    return 1;
                }
                break;
            }

            case FL_RELEASE: {
                if (dragging) {
                    dragging = false;
                    return 1;
                }
                break;
            }

            case FL_MOUSEWHEEL: {
                // Zoom with the mouse wheel
                float delta = Fl::event_dy();
                if (delta > 0) {
                    // Scroll up = zoom in
                    zoomIn();
                } else if (delta < 0) {
                    // Scroll down = zoom out
                    zoomOut();
                }
                return 1;
            }

            case FL_KEYDOWN: {
                // Keyboard shortcuts
                int key = Fl::event_key();
                if (key == '+' || key == '=') {
                    zoomIn();
                    return 1;
                } else if (key == '-') {
                    zoomOut();
                    return 1;
                } else if (key == 'r' || key == 'R') {
                    resetView();
                    return 1;
                }
                break;
            }
        }
        return Fl_Box::handle(event);
    }
};

// Main window
class ImageViewerWindow : public Fl_Window {
private:
    ImageDisplay* display;
    Fl_Slider* zoom_slider;
    Fl_Value_Output* zoom_value;
    Fl_Button* btn_zoom_in;
    Fl_Button* btn_zoom_out;
    Fl_Button* btn_reset;
    Fl_Button* btn_load;

public:
    ImageViewerWindow(int w, int h, const char* title = 0)
        : Fl_Window(w, h, title) {

        // Create the display area
        display = new ImageDisplay(10, 40, w - 220, h - 60);

        // Control panel on the right
        int panel_x = w - 200;

        // Labels and controls
        Fl_Box* label_zoom = new Fl_Box(panel_x, 45, 180, 25, "Zoom");
        label_zoom->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

        // Zoom slider
        zoom_slider = new Fl_Slider(panel_x, 70, 180, 30);
        zoom_slider->type(FL_HORIZONTAL);
        zoom_slider->bounds(0.1, 10.0);
        zoom_slider->value(1.0);
        zoom_slider->step(0.01);
        zoom_slider->callback(zoom_callback, this);

        // Show the zoom value
        zoom_value = new Fl_Value_Output(panel_x + 50, 105, 80, 25);
        zoom_value->precision(1);
        zoom_value->step(0.1);
        zoom_value->value(100.0);

        // Zoom buttons
        btn_zoom_in = new Fl_Button(panel_x, 140, 80, 30, "Zoom +");
        btn_zoom_in->callback(btn_zoom_in_callback, this);

        btn_zoom_out = new Fl_Button(panel_x + 90, 140, 80, 30, "Zoom -");
        btn_zoom_out->callback(btn_zoom_out_callback, this);

        // Reset button
        btn_reset = new Fl_Button(panel_x, 180, 170, 30, "Reset View");
        btn_reset->callback(btn_reset_callback, this);

        // Load image button
        btn_load = new Fl_Button(panel_x, 220, 170, 30, "Load Image");
        btn_load->callback(btn_load_callback, this);

        // Help info
        Fl_Box* help = new Fl_Box(panel_x, 270, 180, 120,
                                  "Help:\n"
                                  "- Mouse wheel: Zoom\n"
                                  "- Drag: Pan\n"
                                  "- +/-: Zoom\n"
                                  "- R: Reset");
        help->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE);
        help->box(FL_BORDER_BOX);
        help->color(FL_LIGHT2);

        end();

        // Load a default image (if present)
        // display->loadImage("image.png");
    }

    // Static callbacks
    static void zoom_callback(Fl_Widget* w, void* data) {
        ImageViewerWindow* win = (ImageViewerWindow*)data;
        Fl_Slider* slider = (Fl_Slider*)w;
        double z = slider->value();
        win->display->setZoom(z);
        win->zoom_value->value(z * 100);
    }

    static void btn_zoom_in_callback(Fl_Widget* w, void* data) {
        ImageViewerWindow* win = (ImageViewerWindow*)data;
        win->display->zoomIn();
        double z = win->display->getZoom();
        win->zoom_slider->value(z);
        win->zoom_value->value(z * 100);
    }

    static void btn_zoom_out_callback(Fl_Widget* w, void* data) {
        ImageViewerWindow* win = (ImageViewerWindow*)data;
        win->display->zoomOut();
        double z = win->display->getZoom();
        win->zoom_slider->value(z);
        win->zoom_value->value(z * 100);
    }

    static void btn_reset_callback(Fl_Widget* w, void* data) {
        ImageViewerWindow* win = (ImageViewerWindow*)data;
        win->display->resetView();
        win->zoom_slider->value(1.0);
        win->zoom_value->value(100.0);
    }

    static void btn_load_callback(Fl_Widget* w, void* data) {
        ImageViewerWindow* win = (ImageViewerWindow*)data;
        // A file chooser could be added here
        const char* filename = fl_file_chooser("Select a PNG image",
                                               "*.png",
                                               "image.png");
        if (filename) {
            if (win->display->loadImage(filename)) {
                win->zoom_slider->value(1.0);
                win->zoom_value->value(100.0);
            }
        }
    }
};

int main() {
    // Initialize FLTK
    Fl::scheme("gtk+");

    // Create the window
    ImageViewerWindow window(900, 600, "PNG Image Viewer");
    window.show();

    return Fl::run();
}
