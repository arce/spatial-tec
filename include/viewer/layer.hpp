#pragma once

#include <set>
#include <string>

#include <FL/Enumerations.H>

#include "core/spatial_style.hpp"
#include "core/spatial_types.hpp"

namespace Viewer {

enum class LayerType { VECTOR, RASTER };

struct Layer {
  std::string name;
  std::string filename;
  LayerType type = LayerType::VECTOR;
  bool visible = true;
  Fl_Color color = FL_BLUE;
  Fl_Color fill_color = FL_BLUE;
  double opacity = 0.8;
  int line_width = 1;
  bool fill = true;

  std::set<std::string> hidden_columns;
  bool geometry_hidden_initialized = false;

  Spatial::VectorDataset vector_data;
  Spatial::RasterDataset raster_data;

  std::string rat_color_field;
  std::string rat_label_field;

  std::vector<Spatial::StyleRule> style_rules;

  bool show_labels = false;
  std::string label_field;

  bool getBBox(double& minx, double& miny, double& maxx, double& maxy) const {
    if (type == LayerType::VECTOR) {
      if (!vector_data.has_bbox || vector_data.features.empty()) return false;
      minx = vector_data.min_x;
      miny = vector_data.min_y;
      maxx = vector_data.max_x;
      maxy = vector_data.max_y;
      return true;
    }

    if (raster_data.ncols <= 0 || raster_data.nrows <= 0) return false;
    minx = raster_data.xllcorner;
    miny = raster_data.yllcorner;
    maxx = raster_data.xllcorner + raster_data.ncols * raster_data.cellsize;
    maxy = raster_data.yllcorner + raster_data.nrows * raster_data.cellsize;
    return true;
  }
};

}
