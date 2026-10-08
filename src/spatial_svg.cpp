#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <vector>

#include "../include/core/spatial_csv.hpp"
#include "../include/core/spatial_geom.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_style.hpp"
#include "../include/core/spatial_types.hpp"
#include "../include/viewer/multipart_geometry.hpp"

struct BBox {
  double minx, miny, maxx, maxy;
  BBox()
      : minx(std::numeric_limits<double>::max()),
        miny(std::numeric_limits<double>::max()),
        maxx(std::numeric_limits<double>::lowest()),
        maxy(std::numeric_limits<double>::lowest()) {}

  bool hasData() const {
    return minx != std::numeric_limits<double>::max();
  }

  void expand(double x, double y) {
    minx = std::min(minx, x);
    miny = std::min(miny, y);
    maxx = std::max(maxx, x);
    maxy = std::max(maxy, y);
  }

  void expand(const BBox& other) {
    if (other.hasData()) {
      minx = std::min(minx, other.minx);
      miny = std::min(miny, other.miny);
      maxx = std::max(maxx, other.maxx);
      maxy = std::max(maxy, other.maxy);
    }
  }

  double width() const {
    return maxx - minx;
  }
  double height() const {
    return maxy - miny;
  }
};

BBox getFeatureBBox(const Spatial::VectorFeature& feature) {
  BBox bbox;
  for (size_t i = 0; i < feature.coordinates.size(); i += 2) {
    bbox.expand(feature.coordinates[i], feature.coordinates[i + 1]);
  }
  return bbox;
}

BBox getDatasetBBox(const Spatial::VectorDataset& dataset) {
  BBox bbox;
  for (const auto& feature : dataset.features) {
    bbox.expand(getFeatureBBox(feature));
  }
  return bbox;
}

class CoordinateTransformer {
private:
  BBox bbox;
  int width, height;
  double margin;
  double scale;
  double offset_x, offset_y;

public:
  CoordinateTransformer(const BBox& bbox, int width, int height, double margin = 40)
      : bbox(bbox), width(width), height(height), margin(margin) {
    double pad_x = bbox.width() * 0.05;
    double pad_y = bbox.height() * 0.05;
    this->bbox.minx -= pad_x;
    this->bbox.miny -= pad_y;
    this->bbox.maxx += pad_x;
    this->bbox.maxy += pad_y;

    double plot_width = width - 2 * margin;
    double plot_height = height - 2 * margin;
    double sx = plot_width / this->bbox.width();
    double sy = plot_height / this->bbox.height();
    scale = std::min(sx, sy);

    double cx = (this->bbox.minx + this->bbox.maxx) / 2;
    double cy = (this->bbox.miny + this->bbox.maxy) / 2;
    offset_x = width / 2.0 - cx * scale;
    offset_y = height / 2.0 + cy * scale;
  }

  std::string transform(double x, double y) const {
    double px = x * scale + offset_x;
    double py = -y * scale + offset_y;
    return std::to_string(px) + " " + std::to_string(py);
  }

  double getScale() const {
    return scale;
  }
  BBox getBBox() const {
    return bbox;
  }
};

std::string pointsToSVGPath(const std::vector<double>& coords,
                            const CoordinateTransformer& transformer, bool close = false) {
  if (coords.size() < 2)
    return "";

  std::string path = "M ";
  for (size_t i = 0; i < coords.size(); i += 2) {
    std::string pt = transformer.transform(coords[i], coords[i + 1]);
    path += pt;
    if (i < coords.size() - 2) {
      path += " L ";
    }
  }

  if (close && coords.size() >= 4) {
    path += " Z";
  }

  return path;
}

std::string escapeXML(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

bool computeLabelAnchor(const Spatial::VectorFeature& feature, double& lx, double& ly) {
  using Geom = Spatial::VectorFeature::GeometryType;
  if (feature.coordinates.size() < 2) return false;

  std::vector<double> largest_part_storage;
  const std::vector<double>* anchor_coords = &feature.coordinates;
  if (!feature.part_starts.empty()) {
    auto ranges = featurePartRanges(feature);
    size_t best = 0;
    double best_measure = -1.0;
    for (size_t i = 0; i < ranges.size(); ++i) {
      double measure = 0.0;
      if (feature.type == Geom::MULTIPOLYGON) {
        measure = polygonArea(Viewer::extractRange(feature, ranges[i]));
      } else if (feature.type == Geom::MULTILINESTRING) {
        measure = lineLength(Viewer::extractRange(feature, ranges[i]));
      }
      if (measure > best_measure) {
        best_measure = measure;
        best = i;
      }
    }
    largest_part_storage = Viewer::extractRange(feature, ranges[best]);
    anchor_coords = &largest_part_storage;
  }

  if (feature.type == Geom::POINT || feature.type == Geom::MULTIPOINT) {
    lx = (*anchor_coords)[0];
    ly = (*anchor_coords)[1];
    return true;
  } else if ((feature.type == Geom::POLYGON || feature.type == Geom::MULTIPOLYGON) &&
             anchor_coords->size() >= 6) {
    polygonCentroid(*anchor_coords, lx, ly);
    return true;
  } else if (feature.type == Geom::LINESTRING || feature.type == Geom::MULTILINESTRING) {
    lineMidpoint(*anchor_coords, lx, ly);
    return true;
  }
  return false;
}

std::string renderLegendBox(const Spatial::LayerStyle& layer_info, int canvas_width, int canvas_height,
                            std::map<std::string, double>& corner_offsets) {
  if (!layer_info.legend_enabled || layer_info.rules.empty()) return "";

  std::string title = layer_info.legend_title;
  if (title.empty()) {
    title = std::filesystem::path(layer_info.file).stem().string();
  }

  const double swatch = 12, row_h = 18, pad = 8, title_h = 20, gap = 10, margin = 15;
  size_t n = layer_info.rules.size();

  size_t max_chars = title.size();
  for (const auto& rule : layer_info.rules) max_chars = std::max(max_chars, rule.label.size());
  double box_w = std::max(120.0, max_chars * 6.2 + swatch + pad * 3);
  double box_h = title_h + n * row_h + pad * 2;

  std::string position = layer_info.legend_position.empty() ? "bottom-right" : layer_info.legend_position;
  double& offset = corner_offsets[position];

  bool from_right = position.find("right") != std::string::npos;
  bool from_bottom = position.find("bottom") != std::string::npos;
  double box_x = from_right ? (canvas_width - margin - box_w) : margin;
  double box_y = from_bottom ? (canvas_height - margin - box_h - offset) : (margin + offset);

  offset += box_h + gap;

  std::string svg;
  svg += "  <g class=\"legend\">\n";
  svg += "    <rect x=\"" + std::to_string(box_x) + "\" y=\"" + std::to_string(box_y) + "\" ";
  svg += "width=\"" + std::to_string(box_w) + "\" height=\"" + std::to_string(box_h) + "\" ";
  svg += "fill=\"#FFFFFF\" fill-opacity=\"0.9\" stroke=\"#999999\" stroke-width=\"1\"/>\n";
  svg += "    <text x=\"" + std::to_string(box_x + pad) + "\" y=\"" + std::to_string(box_y + pad + 10) + "\" ";
  svg += "font-size=\"12\" font-weight=\"bold\" font-family=\"sans-serif\" fill=\"#222222\">" +
         escapeXML(title) + "</text>\n";

  for (size_t i = 0; i < n; ++i) {
    const auto& rule = layer_info.rules[i];
    double row_y = box_y + title_h + pad + i * row_h;
    svg += "    <rect x=\"" + std::to_string(box_x + pad) + "\" y=\"" + std::to_string(row_y) + "\" ";
    svg += "width=\"" + std::to_string(swatch) + "\" height=\"" + std::to_string(swatch) + "\" ";
    svg += "fill=\"" + rule.color + "\" stroke=\"#666666\" stroke-width=\"0.5\"/>\n";
    svg += "    <text x=\"" + std::to_string(box_x + pad + swatch + 6) + "\" y=\"" +
           std::to_string(row_y + swatch - 1) + "\" font-size=\"11\" font-family=\"sans-serif\" fill=\"#222222\">" +
           escapeXML(rule.label) + "</text>\n";
  }

  svg += "  </g>\n";
  return svg;
}

// One combined box, one row per layer that has no per-class rules (i.e. not styled via
// spatial_colormap's classification): a plain swatch (as it's actually drawn: fill_color if
// -fill was given, otherwise the stroke color) plus the layer's name, so that with several
// plain-colored layers it's still clear which color belongs to which layer. Layers that already
// get their own per-class legend from renderLegendBox are skipped here to avoid double listing.
std::string renderLayersLegend(const std::vector<Spatial::LayerStyle>& layers, int canvas_width,
                               int canvas_height, std::map<std::string, double>& corner_offsets) {
  std::vector<const Spatial::LayerStyle*> entries;
  for (const auto& layer_info : layers) {
    if (!layer_info.rules.empty()) continue;
    if (!layer_info.legend_enabled) continue;
    entries.push_back(&layer_info);
  }
  if (entries.empty()) return "";

  const double swatch = 12, row_h = 18, pad = 8, title_h = 20, gap = 10, margin = 15;
  size_t n = entries.size();

  std::string title = "Capas";
  std::vector<std::string> labels;
  labels.reserve(n);
  size_t max_chars = title.size();
  for (const auto* layer_info : entries) {
    std::string label = layer_info->legend_title;
    if (label.empty()) {
      label = std::filesystem::path(layer_info->file).stem().string();
    }
    max_chars = std::max(max_chars, label.size());
    labels.push_back(label);
  }

  double box_w = std::max(120.0, max_chars * 6.2 + swatch + pad * 3);
  double box_h = title_h + n * row_h + pad * 2;

  std::string position =
      entries.front()->legend_position.empty() ? "bottom-right" : entries.front()->legend_position;
  double& offset = corner_offsets[position];

  bool from_right = position.find("right") != std::string::npos;
  bool from_bottom = position.find("bottom") != std::string::npos;
  double box_x = from_right ? (canvas_width - margin - box_w) : margin;
  double box_y = from_bottom ? (canvas_height - margin - box_h - offset) : (margin + offset);

  offset += box_h + gap;

  std::string svg;
  svg += "  <g class=\"legend layers-legend\">\n";
  svg += "    <rect x=\"" + std::to_string(box_x) + "\" y=\"" + std::to_string(box_y) + "\" ";
  svg += "width=\"" + std::to_string(box_w) + "\" height=\"" + std::to_string(box_h) + "\" ";
  svg += "fill=\"#FFFFFF\" fill-opacity=\"0.9\" stroke=\"#999999\" stroke-width=\"1\"/>\n";
  svg += "    <text x=\"" + std::to_string(box_x + pad) + "\" y=\"" + std::to_string(box_y + pad + 10) + "\" ";
  svg += "font-size=\"12\" font-weight=\"bold\" font-family=\"sans-serif\" fill=\"#222222\">" +
         escapeXML(title) + "</text>\n";

  for (size_t i = 0; i < n; ++i) {
    const auto* layer_info = entries[i];
    double row_y = box_y + title_h + pad + i * row_h;
    std::string swatch_fill = layer_info->fill ? layer_info->fill_color : "none";
    svg += "    <rect x=\"" + std::to_string(box_x + pad) + "\" y=\"" + std::to_string(row_y) + "\" ";
    svg += "width=\"" + std::to_string(swatch) + "\" height=\"" + std::to_string(swatch) + "\" ";
    svg += "fill=\"" + swatch_fill + "\" stroke=\"" + layer_info->color + "\" stroke-width=\"1.5\"/>\n";
    svg += "    <text x=\"" + std::to_string(box_x + pad + swatch + 6) + "\" y=\"" +
           std::to_string(row_y + swatch - 1) + "\" font-size=\"11\" font-family=\"sans-serif\" fill=\"#222222\">" +
           escapeXML(labels[i]) + "</text>\n";
  }

  svg += "  </g>\n";
  return svg;
}

std::string applyRules(const Spatial::VectorFeature& feature, const Spatial::LayerStyle& layer) {
  if (layer.rules.empty()) {
    return layer.fill ? layer.fill_color : "none";
  }

  for (const auto& rule : layer.rules) {
    for (const auto& [attr_name, attr_value] : feature.attributes) {
      try {
        double value = std::stod(attr_value);
        if (value >= rule.min_val && value <= rule.max_val) {
          return rule.color;
        }
      } catch (...) {
      }
    }
  }

  return layer.fill ? layer.fill_color : "none";
}

std::string generateSVG(const std::vector<Spatial::LayerStyle>& layers, int width, int height,
                        const std::string& background, const std::string& title) {
  BBox global_bbox;
  for (const auto& layer_info : layers) {
    Spatial::SpatialCSVReader reader;
    Spatial::VectorDataset layer_data;

    if (reader.read(layer_info.file, layer_data)) {
      global_bbox.expand(getDatasetBBox(layer_data));
    }
  }

  if (!global_bbox.hasData()) {
    global_bbox = BBox();
    global_bbox.minx = -180;
    global_bbox.miny = -90;
    global_bbox.maxx = 180;
    global_bbox.maxy = 90;
  }

  std::cout << "Extent: " << global_bbox.minx << " " << global_bbox.miny << " " << global_bbox.maxx
            << " " << global_bbox.maxy << "\n";

  CoordinateTransformer transformer(global_bbox, width, height);

  std::string svg;
  svg = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  svg += "<svg xmlns=\"http://www.w3.org/2000/svg\" ";
  svg += "width=\"" + std::to_string(width) + "\" ";
  svg += "height=\"" + std::to_string(height) + "\" ";
  svg += "viewBox=\"0 0 " + std::to_string(width) + " " + std::to_string(height) + "\">\n";

  if (!background.empty()) {
    svg += "  <rect width=\"100%\" height=\"100%\" fill=\"" + background + "\"/>\n";
  }

  if (!title.empty()) {
    svg +=
        "  <text x=\"50%\" y=\"30\" text-anchor=\"middle\" font-size=\"20\" font-weight=\"bold\">" +
        escapeXML(title) + "</text>\n";
  }

  for (const auto& layer_info : layers) {
    Spatial::SpatialCSVReader reader;
    Spatial::VectorDataset layer_data;

    if (!reader.read(layer_info.file, layer_data)) {
      std::cerr << "Warning: Could not read layer: " << layer_info.file << "\n";
      continue;
    }

    std::cout << "Layer: " << layer_info.file << " (" << layer_data.features.size()
              << " features)\n";

    for (const auto& feature : layer_data.features) {
      if (feature.coordinates.size() < 2)
        continue;

      std::string fill_color = applyRules(feature, layer_info);

      if (feature.type == Spatial::VectorFeature::GeometryType::POINT) {
        std::string pt = transformer.transform(feature.coordinates[0], feature.coordinates[1]);
        size_t space_pos = pt.find(' ');
        std::string cx = pt.substr(0, space_pos);
        std::string cy = pt.substr(space_pos + 1);

        svg += "  <circle cx=\"" + cx + "\" cy=\"" + cy + "\" ";
        svg += "r=\"" + std::to_string(layer_info.point_size) + "\" ";
        svg += "fill=\"" + fill_color + "\" ";
        svg += "stroke=\"" + layer_info.color + "\" ";
        svg += "stroke-width=\"" + std::to_string(layer_info.stroke_width) + "\" ";
        svg += "fill-opacity=\"" + std::to_string(layer_info.opacity) + "\" ";
        svg += "stroke-opacity=\"" + std::to_string(layer_info.opacity) + "\" ";
        svg += "/>\n";
      } else {
        std::string path =
            pointsToSVGPath(feature.coordinates, transformer,
                            feature.type == Spatial::VectorFeature::GeometryType::POLYGON);

        if (path.empty())
          continue;

        svg += "  <path d=\"" + path + "\" ";
        svg += "stroke=\"" + layer_info.color + "\" ";
        svg += "stroke-width=\"" + std::to_string(layer_info.stroke_width) + "\" ";
        svg += "fill=\"" + fill_color + "\" ";
        svg += "fill-opacity=\"" + std::to_string(layer_info.opacity) + "\" ";
        svg += "stroke-opacity=\"" + std::to_string(layer_info.opacity) + "\" ";
        svg += "/>\n";
      }

      if (layer_info.show_labels && !layer_info.label_field.empty()) {
        auto attr_it = feature.attributes.find(layer_info.label_field);
        if (attr_it != feature.attributes.end() && !attr_it->second.empty()) {
          double lx = 0, ly = 0;
          if (computeLabelAnchor(feature, lx, ly)) {
            bool is_point = (feature.type == Spatial::VectorFeature::GeometryType::POINT ||
                             feature.type == Spatial::VectorFeature::GeometryType::MULTIPOINT);
            std::string pt = transformer.transform(lx, ly);
            size_t sp = pt.find(' ');
            std::string tx = pt.substr(0, sp);
            std::string ty = pt.substr(sp + 1);

            svg += "  <text x=\"" + tx + "\" y=\"" + ty + "\" ";
            if (is_point) {
              svg += "dx=\"7\" text-anchor=\"start\" ";
            } else {
              svg += "text-anchor=\"middle\" ";
            }
            svg += "font-size=\"11\" font-family=\"sans-serif\" fill=\"black\" ";
            svg += "stroke=\"white\" stroke-width=\"3\" paint-order=\"stroke\">";
            svg += escapeXML(attr_it->second) + "</text>\n";
          }
        }
      }
    }
  }

  std::map<std::string, double> corner_offsets;
  for (const auto& layer_info : layers) {
    svg += renderLegendBox(layer_info, width, height, corner_offsets);
  }
  svg += renderLayersLegend(layers, width, height, corner_offsets);

  svg += "</svg>\n";

  return svg;
}

void printUsage() {
  std::cerr << "spatial_svg - Generate SVG map from vector data\n";
  std::cerr << "(with per-layer legend and per-feature labels from .sty -- see ADR-0005)\n\n";
  std::cerr << "Usage:\n";
  std::cerr << "  spatial_svg <output> -style <file>\n";
  std::cerr << "  spatial_svg <output> -layer <file> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -style <file>        Style configuration file (.sty), describes one or more layers\n";
  std::cerr << "  -layer <file>        Add a layer. If <file>.sty exists next to it, it's used\n";
  std::cerr << "                       automatically as that layer's style (see -color/-fill etc.\n";
  std::cerr << "                       below to override it)\n";
  std::cerr << "    -color <color>     Stroke color (default: black, or the sidecar's if present)\n";
  std::cerr << "    -fill <color>      Fill color (default: #CCCCCC, or the sidecar's if present)\n";
  std::cerr << "    -stroke <width>    Stroke width (default: 1)\n";
  std::cerr << "    -opacity <value>   Opacity (default: 0.8)\n";
  std::cerr << "    -point_size <num>  Point size (default: 5)\n";
  std::cerr << "  -width <pixels>      SVG width (default: 800)\n";
  std::cerr << "  -height <pixels>     SVG height (default: 600)\n";
  std::cerr << "  -background <color>  Background color (default: white)\n";
  std::cerr << "  -title <text>        Map title\n\n";
  std::cerr << "Per-class legends and per-feature labels (ADR-0005) come entirely from each\n";
  std::cerr << "layer's .sty -- there is no command-line flag for them, since both need data\n";
  std::cerr << "(class colors, an attribute column) that only spatial_colormap's output has. A\n";
  std::cerr << "per-class legend is drawn per layer that has layer.N.legend.enabled=true and at\n";
  std::cerr << "least one class; stacked in the corner given by layer.N.legend.position\n";
  std::cerr << "(top-left/top-right/bottom-left/bottom-right, default bottom-right). Labels\n";
  std::cerr << "are drawn per layer that has layer.N.show_labels=true and layer.N.label_field\n";
  std::cerr << "set -- see 'spatial_colormap -help' for -show-labels/-label-field/-legend-position.\n\n";
  std::cerr << "Plain layers (no .sty classification, e.g. -layer file.csv -color/-fill given\n";
  std::cerr << "directly) automatically get one row each in a single combined \"Capas\" legend box,\n";
  std::cerr << "so it stays clear which color belongs to which layer when several are drawn\n";
  std::cerr << "together. Its label is the layer's file name unless overridden by\n";
  std::cerr << "layer.N.legend.title in a .sty; set layer.N.legend.enabled=false to omit a layer\n";
  std::cerr << "from it.\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_svg map.svg -style style.sty\n";
  std::cerr << "  spatial_svg map.svg -layer countries.csv -color blue -fill lightblue\n";
  std::cerr << "  spatial_colormap countries.csv -attribute population\n";
  std::cerr << "  spatial_svg map.svg -layer countries.csv   # picks up countries.sty automatically\n\n";
  std::cerr << "  # With a legend and per-feature labels:\n";
  std::cerr << "  spatial_colormap countries.csv -attribute population -show-labels -label-field name\n";
  std::cerr << "  spatial_svg map.svg -layer countries.csv -title \"Population\"\n";
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    printUsage();
    return 1;
  }

  std::string output_file;
  std::string style_file;
  std::vector<Spatial::LayerStyle> layers;
  int width = 800;
  int height = 600;
  std::string background = "white";
  std::string title = "";

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-style" && i + 1 < argc) {
      style_file = argv[++i];
    } else if (arg == "-layer" && i + 1 < argc) {
      Spatial::LayerStyle layer;
      layer.file = argv[++i];
      layer.color = "black";
      layer.fill_color = "#CCCCCC";
      layer.stroke_width = 1;
      layer.opacity = 0.8;
      layer.fill = false;
      layer.point_size = 5;

      Spatial::LayerStyle sidecar;
      if (Spatial::loadSidecarStyle(layer.file, sidecar)) {
        layer.color = sidecar.color;
        layer.fill_color = sidecar.fill_color;
        layer.fill = sidecar.fill;
        layer.stroke_width = sidecar.stroke_width;
        layer.opacity = sidecar.opacity;
        layer.point_size = sidecar.point_size;
        layer.rules = sidecar.rules;
        layer.legend_enabled = sidecar.legend_enabled;
        layer.legend_title = sidecar.legend_title;
        layer.legend_position = sidecar.legend_position;
        layer.show_labels = sidecar.show_labels;
        layer.label_field = sidecar.label_field;
        std::cout << "Using style: " << Spatial::sidecarStylePath(layer.file) << "\n";
      }

      while (i + 1 < argc && argv[i + 1][0] == '-') {
        std::string opt = argv[i + 1];
        if (opt == "-color" && i + 2 < argc) {
          layer.color = argv[i + 2];
          i += 2;
        } else if (opt == "-fill" && i + 2 < argc) {
          layer.fill = true;
          layer.fill_color = argv[i + 2];
          i += 2;
        } else if (opt == "-stroke" && i + 2 < argc) {
          layer.stroke_width = std::stod(argv[i + 2]);
          i += 2;
        } else if (opt == "-opacity" && i + 2 < argc) {
          layer.opacity = std::stod(argv[i + 2]);
          i += 2;
        } else if (opt == "-point_size" && i + 2 < argc) {
          layer.point_size = std::stod(argv[i + 2]);
          i += 2;
        } else {
          break;
        }
      }
      layers.push_back(layer);
    } else if (arg == "-width" && i + 1 < argc) {
      width = std::stoi(argv[++i]);
    } else if (arg == "-height" && i + 1 < argc) {
      height = std::stoi(argv[++i]);
    } else if (arg == "-background" && i + 1 < argc) {
      background = argv[++i];
    } else if (arg == "-title" && i + 1 < argc) {
      title = argv[++i];
    } else if (output_file.empty() && arg[0] != '-') {
      output_file = arg;
    }
  }

  if (!style_file.empty()) {
    layers = Spatial::parseStyleFile(style_file);
    if (layers.empty()) {
      std::cerr << "Error: No layers found in style file\n";
      return 1;
    }
  }

  if (output_file.empty() || layers.empty()) {
    std::cerr << "Error: Output file and at least one layer required\n";
    printUsage();
    return 1;
  }

  std::cout << "========================================\n";
  std::cout << "  SVG GENERATION\n";
  std::cout << "========================================\n\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Dimensions: " << width << "x" << height << "\n";
  std::cout << "Layers: " << layers.size() << "\n\n";

  std::string svg = generateSVG(layers, width, height, background, title);

  std::ofstream file(output_file);
  if (!file.is_open()) {
    std::cerr << "Error: Could not create output file: " << output_file << "\n";
    return 1;
  }

  file << svg;
  file.close();

  std::cout << "\nSVG written to: " << output_file << "\n";

  return 0;
}
