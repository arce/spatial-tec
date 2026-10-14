#pragma once

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace Spatial {

struct StyleRule {
  double min_val = 0.0;
  double max_val = 0.0;
  std::string color;
  std::string label;
};

struct LayerStyle {
  std::string file;
  std::string color = "black";
  std::string fill_color = "#CCCCCC";
  bool fill = false;
  double stroke_width = 1;
  double opacity = 0.8;
  double point_size = 5;
  std::vector<StyleRule> rules;

  bool legend_enabled = true;
  std::string legend_title;
  std::string legend_position = "bottom-right";

  bool show_labels = false;
  std::string label_field;
};

inline std::vector<LayerStyle> parseStyleFile(const std::string& filename) {
  std::vector<LayerStyle> layers;
  std::ifstream file(filename);
  if (!file.is_open()) return layers;

  std::string line;
  std::map<int, LayerStyle> layer_map;

  while (std::getline(file, line)) {
    line.erase(0, line.find_first_not_of(" \t"));
    if (line.empty() || line[0] == '#') continue;

    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;

    std::string key = line.substr(0, eq);
    std::string value = line.substr(eq + 1);

    key.erase(0, key.find_first_not_of(" \t"));
    key.erase(key.find_last_not_of(" \t") + 1);
    value.erase(0, value.find_first_not_of(" \t"));
    value.erase(value.find_last_not_of(" \t") + 1);

    if (key.rfind("layer.", 0) != 0) continue;

    size_t dot1 = key.find('.');
    size_t dot2 = key.find('.', dot1 + 1);
    if (dot2 == std::string::npos) continue;

    int num;
    try {
      num = std::stoi(key.substr(dot1 + 1, dot2 - dot1 - 1));
    } catch (...) {
      continue;
    }
    std::string field = key.substr(dot2 + 1);

    auto& layer = layer_map[num];

    if (field == "file") {
      layer.file = value;
    } else if (field == "color") {
      layer.color = value;
    } else if (field == "fill") {
      layer.fill = true;
      layer.fill_color = value;
    } else if (field == "stroke") {
      try { layer.stroke_width = std::stod(value); } catch (...) {}
    } else if (field == "opacity") {
      try { layer.opacity = std::stod(value); } catch (...) {}
    } else if (field == "point_size") {
      try { layer.point_size = std::stod(value); } catch (...) {}
    } else if (field == "show_labels") {
      layer.show_labels = (value == "true" || value == "1" || value == "yes");
    } else if (field == "label_field") {
      layer.label_field = value;
    } else if (field.rfind("legend.", 0) == 0) {
      std::string legend_field = field.substr(7);
      if (legend_field == "enabled") {
        layer.legend_enabled = (value == "true" || value == "1" || value == "yes");
      } else if (legend_field == "title") {
        layer.legend_title = value;
      } else if (legend_field == "position") {
        layer.legend_position = value;
      }
    } else if (field.rfind("class.", 0) == 0) {
      size_t dot_class = field.find('.');
      std::string class_part = field.substr(dot_class + 1);
      size_t dot_field = class_part.find('.');
      if (dot_field == std::string::npos) continue;

      int class_num;
      try {
        class_num = std::stoi(class_part.substr(0, dot_field));
      } catch (...) {
        continue;
      }
      std::string class_field = class_part.substr(dot_field + 1);

      if (class_num < 0) continue;
      if ((size_t)class_num >= layer.rules.size()) {
        layer.rules.resize(class_num + 1);
      }

      auto& rule = layer.rules[class_num];
      if (class_field == "min") {
        try { rule.min_val = std::stod(value); } catch (...) {}
      } else if (class_field == "max") {
        try { rule.max_val = std::stod(value); } catch (...) {}
      } else if (class_field == "color") {
        rule.color = value;
      } else if (class_field == "label") {
        rule.label = value;
      }
    }
  }

  for (auto& [num, layer] : layer_map) {
    layers.push_back(layer);
  }
  return layers;
}

inline std::string sidecarStylePath(const std::string& data_filename) {
  std::filesystem::path p(data_filename);
  p.replace_extension(".sty");
  return p.string();
}

inline bool loadSidecarStyle(const std::string& data_filename, LayerStyle& out) {
  std::string sty_path = sidecarStylePath(data_filename);
  if (!std::filesystem::exists(sty_path)) return false;
  auto layers = parseStyleFile(sty_path);
  if (layers.empty()) return false;
  out = layers.front();
  return true;
}

}
