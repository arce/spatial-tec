#include <FL/Fl.H>

#include "viewer/main_window.hpp"

int main(int argc, char** argv) {
  Fl::scheme("gtk+");
  Viewer::MainWindow win(1200, 800, "Spatial Viewer - Spatial TEC Viewer");

  if (argc > 1)
    win.loadFileFromArgs(argv[1]);

  return Fl::run();
}
