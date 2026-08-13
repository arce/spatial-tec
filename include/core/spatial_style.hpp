// include/core/spatial_style.hpp
//
// Shared reader for Spatial TEC ".sty" style files (color maps / symbology),
// produced by spatial_colormap and consumed by spatial_svg and
// spatial_viewer. Both consumers used to carry their own copy of this
// parser; it's extracted here so a change to the format only has to be
// made once. See ADR-0004 (adr/0004-raster-colormap-and-sty-format.md) for
// the design rationale.
//
// FORMAT
// ------
// A .sty file is a plain-text "properties" file: full-line comments start
// with '#', keys use dot notation under a "layer.N." namespace (N is a
// 1-based layer number -- a sidecar .sty describing a single layer always
// uses "layer.1."; a hand-written multi-layer style file for spatial_svg's
// "-style" flag can describe layer.1, layer.2, ... in one file):
//
//   layer.1.file=cities.csv
//   layer.1.color=black
//   layer.1.fill=#3388FF
//   layer.1.stroke=1
//   layer.1.opacity=0.8
//   layer.1.point_size=5
//   layer.1.class.0.min=0
//   layer.1.class.0.max=100
//   layer.1.class.0.color=#FFFFB2
//   layer.1.class.0.label=0 - 100
//   ...
//
// When "class.*" entries are present, a consumer that draws per-feature
// (spatial_svg) or per-cell (spatial_viewer) symbology is expected to pick
// the color of whichever class's [min,max] range a value falls into,
// instead of the plain "fill" color -- see StyleRule below. There is no
// color interpolation at render time: every value's color is decided
// ahead of time by spatial_colormap and written out as a (possibly large)
// list of narrow classes -- see ADR-0003/ADR-0004 for why "continuous"
// classification is just many equal-width classes, not interpolation.
//
// SIDECAR CONVENTION
// -------------------
// A .sty file with the same base name as a data file (cities.csv ->
// cities.sty, dem.asc -> dem.sty) is considered that layer's style and
// picked up automatically -- see sidecarStylePath()/loadSidecarStyle()
// below. A layer with no matching .sty simply has no style to apply.
#pragma once

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace Spatial {

// One class rule: covers values in [min_val, max_val] with the given
// color ("#RRGGBB" or a CSS color name). label is a human-readable range
// description, used for legends.
struct StyleRule {
  double min_val = 0.0;
  double max_val = 0.0;
  std::string color;
  std::string label;
};

// The style of a single layer, as parsed from one "layer.N.*" block of a
// .sty file. Defaults here match what spatial_svg used historically for a
// layer with no style at all.
struct LayerStyle {
  std::string file;
  std::string color = "black";
  std::string fill_color = "#CCCCCC";
  bool fill = false;
  double stroke_width = 1;
  double opacity = 0.8;
  double point_size = 5;
  std::vector<StyleRule> rules;
};

// Parses a .sty file, returning one LayerStyle per "layer.N.*" block found
// (ordered by N). Returns an empty vector if the file can't be opened or
// has no layer.* keys. Unrecognized keys (legend.*, layer.N.attribute=,
// layer.N.palette=, etc. -- metadata spatial_colormap writes for its own
// documentation purposes) are silently ignored, same as before this was
// shared code.
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
    // Other fields (choropleth, attribute, num_classes, method, palette,
    // legend.*) are informational metadata written by spatial_colormap;
    // no consumer needs them to render, so they're intentionally ignored.
  }

  for (auto& [num, layer] : layer_map) {
    layers.push_back(layer);
  }
  return layers;
}

// The sidecar .sty path for a given data file: same directory and base
// name, ".sty" extension. Does not check whether it exists.
inline std::string sidecarStylePath(const std::string& data_filename) {
  std::filesystem::path p(data_filename);
  p.replace_extension(".sty");
  return p.string();
}

// Looks for <data_filename-without-extension>.sty next to data_filename
// and, if found and non-empty, returns its first (and normally only)
// layer's style in `out`. A sidecar always describes exactly one layer,
// so the layer number used inside the file is irrelevant here -- the
// first one found is used regardless of its "layer.N." number. Returns
// false (leaving `out` untouched) if there's no sidecar or it has no
// layer.* content.
inline bool loadSidecarStyle(const std::string& data_filename, LayerStyle& out) {
  std::string sty_path = sidecarStylePath(data_filename);
  if (!std::filesystem::exists(sty_path)) return false;
  auto layers = parseStyleFile(sty_path);
  if (layers.empty()) return false;
  out = layers.front();
  return true;
}

}  // namespace Spatial
