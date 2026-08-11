#pragma once

#include <set>
#include <string>

#include <FL/Enumerations.H>

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
  // Polygons and point markers are filled by default (the "fill" flag has
  // no effect on LINESTRING geometries, which have no interior to fill --
  // see MapWidget::drawVectorLayer).
  bool fill = true;

  // Names of attribute columns currently hidden in the attribute table.
  std::set<std::string> hidden_columns;
  // Tracks whether the geometry column has already been auto-hidden once,
  // so that re-showing it via "Show all columns" doesn't get re-hidden on
  // the next table refresh.
  bool geometry_hidden_initialized = false;

  Spatial::VectorDataset vector_data;
  Spatial::RasterDataset raster_data;

  // For a raster layer whose raster_data has a RAT: names of the RAT
  // columns (raster_data.rat_columns) used, if any, to colorize the raster
  // by class (rat_color_field, expected to hold "RRGGBB"/"#RRGGBB" hex
  // values) and to label each class in the UI (rat_label_field, used both
  // for attribute-table row headers and, when show_labels is on, as the
  // on-map cell text -- see below). Empty means "not set" -- the raster
  // falls back to the default min/max color gradient and numeric row
  // numbers.
  std::string rat_color_field;
  std::string rat_label_field;

  // Draws a text label per feature/cell on the map when true. For a vector
  // layer, label_field picks which attribute column supplies the text
  // (polygons are labeled at their centroid, points just to the side of
  // the marker, lines at their midpoint). For a raster layer, label_field
  // is unused -- it shows rat_label_field's value if that's set, otherwise
  // the raw cell value, centered in each cell.
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

}  // namespace Viewer
