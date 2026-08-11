#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../include/core/spatial_csv.hpp"
#include "../include/core/spatial_types.hpp"

struct ColorClass {
  double min_val;
  double max_val;
  std::string color;
  std::string label;
  std::string style;
};

struct ColorMapConfig {
  std::vector<ColorClass> classes;
  std::string attribute;
  std::string palette = "YlOrRd";
  std::string method = "quantile";
  int num_classes = 5;
  int layer_num = 1;
  bool include_legend = true;
  std::string legend_title = "";
  std::string output_file = "";
};

class ColorPalette {
private:
  std::map<std::string, std::vector<std::string>> palettes;

  void initializePalettes() {
    palettes["YlOrRd"] = {"#FFFFB2", "#FED976", "#FEB24C", "#FD8D3C", "#F03B20", "#BD0026"};

    palettes["YlOrBr"] = {"#FFFFE5", "#FFF7BC", "#FEE391", "#FEC44F",
                          "#FE9929", "#EC7014", "#CC4C02"};

    palettes["YlGn"] = {"#FFFFE5", "#F7FCD9", "#D9F0A3", "#ADDD8E",
                        "#78C679", "#41AB5D", "#238443", "#006837"};

    palettes["Blues"] = {"#EFF3FF", "#BDD7E7", "#6BAED6", "#3182BD", "#08519C"};

    palettes["Reds"] = {"#FFF5F0", "#FEE0D2", "#FCBBA1", "#FC9272",
                        "#FB6A4A", "#EF3B2D", "#CB181D", "#99000D"};

    palettes["Greens"] = {"#EDF8E9", "#BAE4B3", "#74C476", "#31A354", "#006D2C"};

    palettes["Purples"] = {"#F2F0F7", "#DADAEB", "#BCBDDC", "#9E9AC8", "#756BB1", "#54278F"};

    palettes["Oranges"] = {"#FFF5EB", "#FEE6CE", "#FDD0A2", "#FDAE6B",
                           "#FD8D3C", "#F16913", "#D94801"};

    palettes["Greys"] = {"#F7F7F7", "#D9D9D9", "#BDBDBD", "#969696",
                         "#737373", "#525252", "#252525"};

    palettes["Spectral"] = {"#D53E4F", "#F46D43", "#FDAE61", "#FEE08B", "#FFFFBF",
                            "#E6F598", "#ABDDA4", "#66C2A5", "#3288BD"};

    palettes["RdYlGn"] = {"#D73027", "#F46D43", "#FDAE61", "#FEE08B", "#FFFFBF",
                          "#D9EF8B", "#A6D96A", "#66BD63", "#1A9850"};

    palettes["RdYlBu"] = {"#D73027", "#F46D43", "#FDAE61", "#FEE090", "#FFFFBF",
                          "#E0F3F8", "#ABD9E9", "#74ADD1", "#4575B4"};

    palettes["RdBu"] = {"#67001F", "#B2182B", "#D6604D", "#F4A582", "#FDDBC7",
                        "#D1E5F0", "#92C5DE", "#4393C3", "#2166AC", "#053061"};

    palettes["PuOr"] = {"#7F3B08", "#B35806", "#E08214", "#FDB863", "#FEE0B6",
                        "#D8DAEB", "#B2ABD2", "#8073AC", "#542788", "#2D004B"};

    palettes["Viridis"] = {"#440154", "#482878", "#3E4989", "#31688E", "#26828E",
                           "#1F9E89", "#35B779", "#6DCD59", "#B4DE2C", "#FDE725"};

    palettes["Inferno"] = {"#000004", "#1B0A41", "#4B0C6B", "#781C6D", "#A52C60",
                           "#CF4446", "#ED6925", "#FB9A06", "#F7D03C", "#FCFFA4"};

    palettes["Plasma"] = {"#0D0887", "#46039F", "#7201A8", "#9C179E", "#BD3786",
                          "#D8576B", "#ED7953", "#FB9F3A", "#FDC928", "#F0F921"};

    palettes["Cividis"] = {"#00204D", "#00345E", "#0A486B", "#335C77", "#537081",
                           "#72858C", "#929A9A", "#B1B0AA", "#D0C6BB", "#F0DDCD"};

    palettes["Topo"] = {"#4B0030", "#6B0030", "#8B0030", "#AB0030", "#CB0030",
                        "#E8471A", "#F08E2E", "#F5C542", "#F9E97A", "#FCFFB2"};

    palettes["Ocean"] = {"#003366", "#004488", "#0055AA", "#0066CC", "#0077EE",
                         "#3399FF", "#66BBFF", "#99DDFF", "#CCEEFF", "#E6F5FF"};

    palettes["Forest"] = {"#004B23", "#006B2F", "#008B3B", "#00AB47", "#00CB53",
                          "#33CC66", "#66CC80", "#99CC99", "#CCE6CC", "#E6F5E6"};

    palettes["Categorical"] = {"#E41A1C", "#377EB8", "#4DAF4A", "#984EA3", "#FF7F00",
                               "#FFFF33", "#A65628", "#F781BF", "#999999"};

    palettes["Tableau"] = {"#4E79A7", "#F28E2B", "#E15759", "#76B7B2", "#59A14F",
                           "#EDC948", "#B07AA1", "#FF9DA7", "#9C755F", "#BAB0AC"};
  }

  std::vector<std::string> getPaletteColors(const std::string& name, int num_colors) {
    auto it = palettes.find(name);
    if (it == palettes.end()) {
      std::vector<std::string> fallback;
      for (int i = 0; i < num_colors; i++) {
        int intensity = 100 + (155 * i / num_colors);
        char hex[8];
        sprintf(hex, "#%02x%02x%02x", 0, 0, intensity);
        fallback.push_back(std::string(hex));
      }
      return fallback;
    }

    const auto& full_palette = it->second;
    if (num_colors <= (int)full_palette.size()) {
      return std::vector<std::string>(full_palette.begin(), full_palette.begin() + num_colors);
    }

    return interpolatePalette(full_palette, num_colors);
  }

  std::vector<std::string> interpolatePalette(const std::vector<std::string>& source,
                                              int target_size) {
    std::vector<std::string> result;
    if (source.empty())
      return result;
    if (target_size <= 1)
      return {source[0]};

    for (int i = 0; i < target_size; i++) {
      double pos = (double)i / (target_size - 1);
      double index = pos * (source.size() - 1);
      int idx1 = (int)std::floor(index);
      int idx2 = std::min(idx1 + 1, (int)source.size() - 1);
      double frac = index - idx1;

      if (idx1 == idx2 || frac < 0.001) {
        result.push_back(source[idx1]);
      } else {
        result.push_back(interpolateColor(source[idx1], source[idx2], frac));
      }
    }
    return result;
  }

  std::string interpolateColor(const std::string& c1, const std::string& c2, double t) {
    auto hexToRgb = [](const std::string& hex) {
      int r, g, b;
      if (hex[0] == '#') {
        sscanf(hex.c_str(), "#%02x%02x%02x", &r, &g, &b);
      } else {
        sscanf(hex.c_str(), "%02x%02x%02x", &r, &g, &b);
      }
      return std::tuple<int, int, int>(r, g, b);
    };

    auto [r1, g1, b1] = hexToRgb(c1);
    auto [r2, g2, b2] = hexToRgb(c2);

    int r = (int)(r1 + (r2 - r1) * t);
    int g = (int)(g1 + (g2 - g1) * t);
    int b = (int)(b1 + (b2 - b1) * t);

    char hex[8];
    sprintf(hex, "#%02x%02x%02x", std::min(255, std::max(0, r)), std::min(255, std::max(0, g)),
            std::min(255, std::max(0, b)));
    return std::string(hex);
  }

public:
  ColorPalette() {
    initializePalettes();
  }

  std::vector<std::string> getColors(const std::string& name, int num_colors) {
    return getPaletteColors(name, num_colors);
  }

  std::vector<std::string> listPalettes() {
    std::vector<std::string> names;
    for (const auto& [name, _] : palettes) {
      names.push_back(name);
    }
    return names;
  }
};

class ColorMapGenerator {
private:
  ColorPalette palette;

  std::vector<ColorClass> generateQuantile(const std::vector<double>& values, int num_classes,
                                           const std::vector<std::string>& colors) {
    std::vector<ColorClass> classes;
    if (values.empty())
      return classes;

    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());

    int n = sorted.size();
    int class_size = std::max(1, n / num_classes);

    for (int i = 0; i < num_classes; i++) {
      ColorClass cls;
      int start_idx = i * class_size;
      int end_idx = std::min((i + 1) * class_size - 1, n - 1);

      cls.min_val = sorted[start_idx];
      cls.max_val = sorted[end_idx];
      cls.color = colors[i % colors.size()];
      cls.label = formatLabel(cls.min_val, cls.max_val);

      classes.push_back(cls);
    }
    return classes;
  }

  std::vector<ColorClass> generateEqualInterval(const std::vector<double>& values, int num_classes,
                                                const std::vector<std::string>& colors) {
    std::vector<ColorClass> classes;
    if (values.empty())
      return classes;

    double min_val = *std::min_element(values.begin(), values.end());
    double max_val = *std::max_element(values.begin(), values.end());
    double interval = (max_val - min_val) / num_classes;

    for (int i = 0; i < num_classes; i++) {
      ColorClass cls;
      cls.min_val = min_val + i * interval;
      cls.max_val = (i == num_classes - 1) ? max_val : min_val + (i + 1) * interval;
      cls.color = colors[i % colors.size()];
      cls.label = formatLabel(cls.min_val, cls.max_val);

      classes.push_back(cls);
    }
    return classes;
  }

  std::vector<ColorClass> generateNaturalBreaks(const std::vector<double>& values, int num_classes,
                                                const std::vector<std::string>& colors) {
    std::vector<ColorClass> classes;
    if (values.empty())
      return classes;

    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());

    if (num_classes >= (int)sorted.size()) {
      return generateQuantile(values, num_classes, colors);
    }

    std::vector<double> centroids(num_classes);
    for (int i = 0; i < num_classes; i++) {
      centroids[i] = sorted[(i * sorted.size()) / num_classes];
    }

    std::vector<int> assignments(sorted.size());
    bool changed = true;
    int max_iter = 100;

    while (changed && max_iter-- > 0) {
      changed = false;

      for (size_t i = 0; i < sorted.size(); i++) {
        int best_cluster = 0;
        double best_dist = std::abs(sorted[i] - centroids[0]);
        for (int j = 1; j < num_classes; j++) {
          double dist = std::abs(sorted[i] - centroids[j]);
          if (dist < best_dist) {
            best_dist = dist;
            best_cluster = j;
          }
        }
        if (assignments[i] != best_cluster) {
          assignments[i] = best_cluster;
          changed = true;
        }
      }

      std::vector<double> sum(num_classes, 0);
      std::vector<int> count(num_classes, 0);
      for (size_t i = 0; i < sorted.size(); i++) {
        sum[assignments[i]] += sorted[i];
        count[assignments[i]]++;
      }
      for (int i = 0; i < num_classes; i++) {
        if (count[i] > 0) {
          centroids[i] = sum[i] / count[i];
        }
      }
    }

    for (int i = 0; i < num_classes; i++) {
      ColorClass cls;
      cls.min_val = sorted[0];
      cls.max_val = sorted[sorted.size() - 1];

      bool found_min = false, found_max = false;
      for (size_t j = 0; j < sorted.size(); j++) {
        if (assignments[j] == i) {
          if (!found_min) {
            cls.min_val = sorted[j];
            found_min = true;
          }
          cls.max_val = sorted[j];
        }
      }

      cls.color = colors[i % colors.size()];
      cls.label = formatLabel(cls.min_val, cls.max_val);
      classes.push_back(cls);
    }

    return classes;
  }

  std::string formatLabel(double min_val, double max_val) {
    char label[64];

    if (min_val >= 1000000000) {
      sprintf(label, "%.1fB - %.1fB", min_val / 1000000000.0, max_val / 1000000000.0);
    } else if (min_val >= 1000000) {
      sprintf(label, "%.1fM - %.1fM", min_val / 1000000.0, max_val / 1000000.0);
    } else if (min_val >= 1000) {
      sprintf(label, "%.0fK - %.0fK", min_val / 1000.0, max_val / 1000.0);
    } else if (min_val >= 100) {
      sprintf(label, "%.0f - %.0f", min_val, max_val);
    } else if (min_val >= 1) {
      sprintf(label, "%.1f - %.1f", min_val, max_val);
    } else {
      sprintf(label, "%.2f - %.2f", min_val, max_val);
    }

    std::string result(label);
    if (result.find("-inf") != std::string::npos) {
      result = "0 - " + std::to_string((int)max_val);
    }
    if (result.find("inf") != std::string::npos) {
      result = std::to_string((int)min_val) + " - ∞";
    }

    return result;
  }

  std::vector<double> extractValues(const Spatial::VectorDataset& dataset,
                                    const std::string& attribute) {
    std::vector<double> values;

    for (const auto& feature : dataset.features) {
      auto it = feature.attributes.find(attribute);
      if (it != feature.attributes.end()) {
        try {
          double val = std::stod(it->second);
          if (!std::isnan(val) && !std::isinf(val)) {
            values.push_back(val);
          }
        } catch (const std::exception&) {
        }
      }
    }

    return values;
  }

public:
  ColorMapConfig generate(const std::string& csv_file, const std::string& attribute,
                          const std::string& palette_name, const std::string& method,
                          int num_classes, int layer_num) {
    ColorMapConfig config;
    config.attribute = attribute;
    config.palette = palette_name;
    config.method = method;
    config.num_classes = num_classes;
    config.layer_num = layer_num;

    Spatial::SpatialCSVReader reader;
    Spatial::VectorDataset dataset;

    if (!reader.read(csv_file, dataset)) {
      std::cerr << "Error: Could not read CSV file: " << csv_file << "\n";
      return config;
    }

    auto values = extractValues(dataset, attribute);

    if (values.empty()) {
      std::cerr << "Error: No numeric values found for attribute '" << attribute << "'\n";
      return config;
    }

    auto colors = palette.getColors(palette_name, num_classes);

    if (method == "quantile") {
      config.classes = generateQuantile(values, num_classes, colors);
    } else if (method == "equal_interval") {
      config.classes = generateEqualInterval(values, num_classes, colors);
    } else if (method == "natural_breaks") {
      config.classes = generateNaturalBreaks(values, num_classes, colors);
    } else {
      std::cerr << "Warning: Unknown method '" << method << "', using quantile\n";
      config.classes = generateQuantile(values, num_classes, colors);
    }

    return config;
  }
};

class StyleWriter {
public:
  void write(const ColorMapConfig& config, std::ostream& out) {
    out << "# =============================================\n";
    out << "# COLORMAP GENERATED BY spatial_colormap\n";
    out << "# =============================================\n";
    out << "# Date: " << getCurrentDate() << "\n";
    out << "# Attribute: " << config.attribute << "\n";
    out << "# Palette: " << config.palette << "\n";
    out << "# Method: " << config.method << "\n";
    out << "# Classes: " << config.num_classes << "\n";
    out << "# =============================================\n\n";

    out << "# Layer configuration\n";
    out << "layer." << config.layer_num << ".choropleth=true\n";
    out << "layer." << config.layer_num << ".attribute=" << config.attribute << "\n";
    out << "layer." << config.layer_num << ".num_classes=" << config.num_classes << "\n";
    out << "layer." << config.layer_num << ".method=" << config.method << "\n";
    out << "layer." << config.layer_num << ".palette=" << config.palette << "\n\n";

    out << "# Classes\n";
    for (size_t i = 0; i < config.classes.size(); i++) {
      const auto& cls = config.classes[i];
      out << "layer." << config.layer_num << ".class." << i << ".min=" << cls.min_val << "\n";
      out << "layer." << config.layer_num << ".class." << i << ".max=" << cls.max_val << "\n";
      out << "layer." << config.layer_num << ".class." << i << ".color=" << cls.color << "\n";
      out << "layer." << config.layer_num << ".class." << i << ".label=" << cls.label << "\n";
      out << "\n";
    }

    if (config.include_legend) {
      out << "# Legend configuration\n";
      out << "legend.enabled=true\n";
      out << "legend.title="
          << (config.legend_title.empty() ? config.attribute : config.legend_title) << "\n";
      out << "legend.position=bottom-right\n";
      out << "legend.font_size=12\n";
      out << "legend.font_color=#333333\n";
      out << "legend.background=#FFFFFF\n";
      out << "legend.border_color=#CCCCCC\n";
      out << "legend.border_width=1\n";
      out << "legend.padding=10\n";
      out << "legend.margin=20\n";
      out << "legend.columns=1\n\n";
    }

    out << "# Statistics\n";
    if (!config.classes.empty()) {
      double min_val = config.classes.front().min_val;
      double max_val = config.classes.back().max_val;
      out << "# Min value: " << min_val << "\n";
      out << "# Max value: " << max_val << "\n";
      out << "# Range: " << (max_val - min_val) << "\n";
    }

    out << "\n# =============================================\n";
    out << "# END OF COLORMAP\n";
    out << "# =============================================\n";
  }

private:
  std::string getCurrentDate() {
    time_t now = time(nullptr);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return std::string(buf);
  }
};

void printUsage() {
  std::cerr << "spatial_colormap - Generate choropleth color mapping\n\n";
  std::cerr << "Usage: spatial_colormap <csv_file> [options]\n\n";
  std::cerr << "Required:\n";
  std::cerr << "  <csv_file>           Input CSV file with spatial data\n";
  std::cerr << "  -attribute <col>     Column name for values\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -palette <name>      Color palette (default: YlOrRd)\n";
  std::cerr << "  -classes <num>       Number of classes (default: 5)\n";
  std::cerr << "  -method <method>     Classification method:\n";
  std::cerr << "                       quantile, equal_interval, natural_breaks\n";
  std::cerr << "  -layer <num>         Layer number (default: 1)\n";
  std::cerr << "  -title <text>        Legend title (default: attribute name)\n";
  std::cerr << "  -no-legend           Disable legend generation\n";
  std::cerr << "  -output <file>       Output style file (default: stdout)\n";
  std::cerr << "  -list-palettes       List all available palettes\n";
  std::cerr << "  -help                Show this help\n\n";
  std::cerr << "Available Palettes:\n";
  std::cerr << "  Sequential:  YlOrRd, YlOrBr, YlGn, Blues, Reds,\n";
  std::cerr << "               Greens, Purples, Oranges, Greys\n";
  std::cerr << "  Diverging:   Spectral, RdYlGn, RdYlBu, RdBu, PuOr\n";
  std::cerr << "  Scientific:  Viridis, Inferno, Plasma, Cividis\n";
  std::cerr << "  Thematic:    Topo, Ocean, Forest\n";
  std::cerr << "  Categorical: Categorical, Tableau\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_colormap countries.csv -attribute population\n";
  std::cerr << "  spatial_colormap data.csv -attribute temp -palette RdBu -classes 7\n";
  std::cerr << "  spatial_colormap cities.csv -attribute population -method quantile > style.ini\n";
  std::cerr << "  spatial_colormap data.csv -attribute density -palette Greens -output style.ini\n";
}

void listPalettes() {
  ColorPalette pal;
  auto palettes = pal.listPalettes();

  std::cout << "Available palettes:\n";
  std::cout << "===================\n\n";

  std::cout << "Sequential:\n";
  std::vector<std::string> sequential = {"YlOrRd", "YlOrBr",  "YlGn",    "Blues", "Reds",
                                         "Greens", "Purples", "Oranges", "Greys"};
  for (const auto& p : sequential) {
    if (std::find(palettes.begin(), palettes.end(), p) != palettes.end()) {
      std::cout << "  " << p << "\n";
    }
  }

  std::cout << "\nDiverging:\n";
  std::vector<std::string> diverging = {"Spectral", "RdYlGn", "RdYlBu", "RdBu", "PuOr"};
  for (const auto& p : diverging) {
    if (std::find(palettes.begin(), palettes.end(), p) != palettes.end()) {
      std::cout << "  " << p << "\n";
    }
  }

  std::cout << "\nScientific:\n";
  std::vector<std::string> scientific = {"Viridis", "Inferno", "Plasma", "Cividis"};
  for (const auto& p : scientific) {
    if (std::find(palettes.begin(), palettes.end(), p) != palettes.end()) {
      std::cout << "  " << p << "\n";
    }
  }

  std::cout << "\nThematic:\n";
  std::vector<std::string> thematic = {"Topo", "Ocean", "Forest"};
  for (const auto& p : thematic) {
    if (std::find(palettes.begin(), palettes.end(), p) != palettes.end()) {
      std::cout << "  " << p << "\n";
    }
  }

  std::cout << "\nCategorical:\n";
  std::vector<std::string> categorical = {"Categorical", "Tableau"};
  for (const auto& p : categorical) {
    if (std::find(palettes.begin(), palettes.end(), p) != palettes.end()) {
      std::cout << "  " << p << "\n";
    }
  }
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    printUsage();
    return 1;
  }

  std::string csv_file;
  std::string attribute;
  std::string palette = "YlOrRd";
  std::string method = "quantile";
  int num_classes = 5;
  int layer_num = 1;
  std::string legend_title;
  bool include_legend = true;
  std::string output_file;
  bool list_palettes = false;

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];

    if (arg == "-attribute" && i + 1 < argc) {
      attribute = argv[++i];
    } else if (arg == "-palette" && i + 1 < argc) {
      palette = argv[++i];
    } else if (arg == "-classes" && i + 1 < argc) {
      num_classes = std::stoi(argv[++i]);
      if (num_classes < 2)
        num_classes = 2;
      if (num_classes > 20)
        num_classes = 20;
    } else if (arg == "-method" && i + 1 < argc) {
      method = argv[++i];
    } else if (arg == "-layer" && i + 1 < argc) {
      layer_num = std::stoi(argv[++i]);
    } else if (arg == "-title" && i + 1 < argc) {
      legend_title = argv[++i];
    } else if (arg == "-no-legend") {
      include_legend = false;
    } else if (arg == "-output" && i + 1 < argc) {
      output_file = argv[++i];
    } else if (arg == "-list-palettes") {
      list_palettes = true;
    } else if (arg == "-help" || arg == "--help") {
      printUsage();
      return 0;
    } else if (csv_file.empty() && arg[0] != '-') {
      csv_file = arg;
    }
  }

  if (list_palettes) {
    listPalettes();
    return 0;
  }

  if (csv_file.empty() || attribute.empty()) {
    std::cerr << "Error: CSV file and attribute required\n";
    printUsage();
    return 1;
  }

  ColorMapGenerator generator;
  auto config = generator.generate(csv_file, attribute, palette, method, num_classes, layer_num);

  if (config.classes.empty()) {
    std::cerr << "Error: Failed to generate colormap\n";
    return 1;
  }

  config.legend_title = legend_title;
  config.include_legend = include_legend;
  config.output_file = output_file;

  StyleWriter writer;
  if (!output_file.empty()) {
    std::ofstream file(output_file);
    if (!file.is_open()) {
      std::cerr << "Error: Could not create output file: " << output_file << "\n";
      return 1;
    }
    writer.write(config, file);
    file.close();
    std::cout << "Colormap written to: " << output_file << "\n";
  } else {
    writer.write(config, std::cout);
  }

  return 0;
}
