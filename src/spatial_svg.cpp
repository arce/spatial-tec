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
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

struct Layer {
  std::string file;
  std::string color;
  std::string fill_color;
  double stroke_width;
  double opacity;
  bool fill;
  double point_size;

  struct StyleRule {
    double min_val;
    double max_val;
    std::string color;
    std::string label;
  };
  std::vector<StyleRule> rules;
};

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

std::string applyRules(const Spatial::VectorFeature& feature, const Layer& layer) {
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

std::string generateSVG(const std::vector<Layer>& layers, int width, int height,
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
        title + "</text>\n";
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
    }
  }

  double map_width = global_bbox.width();
  double scale_bar_width = map_width * 0.1;
  if (scale_bar_width > 0) {
    double x1 = global_bbox.minx + map_width * 0.05;
    double y1 = global_bbox.miny + map_width * 0.05;
    double x2 = x1 + scale_bar_width;

    std::string p1 = transformer.transform(x1, y1);
    std::string p2 = transformer.transform(x2, y1);

    svg += "  <line x1=\"" + p1.substr(0, p1.find(' ')) + "\" ";
    svg += "y1=\"" + p1.substr(p1.find(' ') + 1) + "\" ";
    svg += "x2=\"" + p2.substr(0, p2.find(' ')) + "\" ";
    svg += "y2=\"" + p2.substr(p2.find(' ') + 1) + "\" ";
    svg += "stroke=\"black\" stroke-width=\"2\"/>\n";
  }

  svg += "</svg>\n";

  return svg;
}

std::vector<Layer> parseStyleFile(const std::string& filename) {
  std::vector<Layer> layers;
  std::ifstream file(filename);

  if (!file.is_open()) {
    std::cerr << "Error: Could not open style file: " << filename << "\n";
    return layers;
  }

  std::string line;
  std::map<int, Layer> layer_map;

  while (std::getline(file, line)) {
    line.erase(0, line.find_first_not_of(" \t"));
    if (line.empty() || line[0] == '#')
      continue;

    size_t comment = line.find('#');
    if (comment != std::string::npos) {
      if (line.length() >= comment + 7 && line[comment] == '#') {
      } else {
        line = line.substr(0, comment);
      }
    }

    line.erase(0, line.find_first_not_of(" \t"));
    if (line.empty())
      continue;

    size_t eq = line.find('=');
    if (eq == std::string::npos)
      continue;

    std::string key = line.substr(0, eq);
    std::string value = line.substr(eq + 1);

    key.erase(0, key.find_first_not_of(" \t"));
    key.erase(key.find_last_not_of(" \t") + 1);
    value.erase(0, value.find_first_not_of(" \t"));
    value.erase(value.find_last_not_of(" \t") + 1);

    if (key.rfind("layer.", 0) == 0) {
      size_t dot1 = key.find('.');
      size_t dot2 = key.find('.', dot1 + 1);
      if (dot2 == std::string::npos)
        continue;

      int num = std::stoi(key.substr(dot1 + 1, dot2 - dot1 - 1));
      std::string field = key.substr(dot2 + 1);

      auto& layer = layer_map[num];

      if (field == "file")
        layer.file = value;
      else if (field == "color")
        layer.color = value;
      else if (field == "fill") {
        layer.fill = true;
        layer.fill_color = value;
      } else if (field == "stroke")
        layer.stroke_width = std::stod(value);
      else if (field == "opacity")
        layer.opacity = std::stod(value);
      else if (field == "point_size")
        layer.point_size = std::stod(value);

      else if (field.rfind("class.", 0) == 0) {
        size_t dot_class = field.find('.');
        std::string class_part = field.substr(dot_class + 1);
        size_t dot_field = class_part.find('.');
        if (dot_field == std::string::npos)
          continue;

        int class_num = std::stoi(class_part.substr(0, dot_field));
        std::string class_field = class_part.substr(dot_field + 1);

        if (class_num >= (int)layer.rules.size()) {
          layer.rules.resize(class_num + 1);
        }

        auto& rule = layer.rules[class_num];
        if (class_field == "min")
          rule.min_val = std::stod(value);
        else if (class_field == "max")
          rule.max_val = std::stod(value);
        else if (class_field == "color")
          rule.color = value;
        else if (class_field == "label")
          rule.label = value;
      }
    }
  }

  for (auto& [num, layer] : layer_map) {
    if (layer.color.empty())
      layer.color = "black";
    if (layer.fill_color.empty())
      layer.fill_color = "#CCCCCC";
    if (layer.stroke_width == 0)
      layer.stroke_width = 1;
    if (layer.opacity == 0)
      layer.opacity = 0.8;
    if (layer.point_size == 0)
      layer.point_size = 5;

    layers.push_back(layer);
  }

  return layers;
}

void printUsage() {
  std::cerr << "spatial_svg - Generate SVG map from vector data\n\n";
  std::cerr << "Usage:\n";
  std::cerr << "  spatial_svg <output> -style <file>\n";
  std::cerr << "  spatial_svg <output> -layer <file> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -style <file>        Style configuration file\n";
  std::cerr << "  -layer <file>        Add a layer\n";
  std::cerr << "    -color <color>     Stroke color (default: black)\n";
  std::cerr << "    -fill <color>      Fill color (default: #CCCCCC)\n";
  std::cerr << "    -stroke <width>    Stroke width (default: 1)\n";
  std::cerr << "    -opacity <value>   Opacity (default: 0.8)\n";
  std::cerr << "    -point_size <num>  Point size (default: 5)\n";
  std::cerr << "  -width <pixels>      SVG width (default: 800)\n";
  std::cerr << "  -height <pixels>     SVG height (default: 600)\n";
  std::cerr << "  -background <color>  Background color (default: white)\n";
  std::cerr << "  -title <text>        Map title\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_svg map.svg -style style.ini\n";
  std::cerr << "  spatial_svg map.svg -layer countries.csv -color blue -fill lightblue\n";
  std::cerr << "  spatial_colormap countries.csv -attribute population > style.ini\n";
  std::cerr << "  spatial_svg map.svg -style style.ini\n";
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    printUsage();
    return 1;
  }

  std::string output_file;
  std::string style_file;
  std::vector<Layer> layers;
  int width = 800;
  int height = 600;
  std::string background = "white";
  std::string title = "";

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-style" && i + 1 < argc) {
      style_file = argv[++i];
    } else if (arg == "-layer" && i + 1 < argc) {
      Layer layer;
      layer.file = argv[++i];
      layer.color = "black";
      layer.fill_color = "#CCCCCC";
      layer.stroke_width = 1;
      layer.opacity = 0.8;
      layer.fill = false;
      layer.point_size = 5;

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
    layers = parseStyleFile(style_file);
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

