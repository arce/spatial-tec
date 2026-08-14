#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../include/core/ascii_grid.hpp"
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
  std::string attribute;  // empty for a raster (or "single") style
  std::string palette = "YlOrRd";
  std::string method = "quantile";
  int num_classes = 5;
  int layer_num = 1;
  bool include_legend = true;
  std::string legend_title = "";
  std::string legend_position = "bottom-right";
  std::string output_file = "";
  // Set (and `classes` left empty) for -method single: no classification,
  // just one fixed color for the whole layer.
  std::string fixed_color = "";

  // Per-feature label metadata (ADR-0005) -- see -show-labels/-label-field
  // in printUsage() below.
  bool show_labels = false;
  std::string label_field;
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
    return getPaletteColors(name, std::max(1, num_colors));
  }

  std::vector<std::string> listPalettes() {
    std::vector<std::string> names;
    for (const auto& [name, _] : palettes) {
      names.push_back(name);
    }
    return names;
  }
};

// Classifies a plain list of numeric values (regardless of whether they
// came from a CSV attribute column or from raster cell values -- see
// extractVectorValues/extractRasterValues below) into color classes.
// Every method here produces the same output shape: a list of
// {min_val, max_val, color, label} classes with the color already
// resolved, ready to be looked up by value with no further computation --
// see ADR-0004 for why there is no interpolation-at-render-time method.
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

  // Classes centered on the mean, one standard deviation wide each. The
  // first/last class are stretched to actually cover the data's min/max
  // (a few outliers several sigma away from the mean shouldn't fall
  // outside every class).
  std::vector<ColorClass> generateStandardDeviation(const std::vector<double>& values, int num_classes,
                                                     const std::vector<std::string>& colors) {
    std::vector<ColorClass> classes;
    if (values.empty())
      return classes;

    double sum = 0;
    for (double v : values) sum += v;
    double mean = sum / values.size();

    double sq_sum = 0;
    for (double v : values) sq_sum += (v - mean) * (v - mean);
    double stdev = values.size() > 1 ? std::sqrt(sq_sum / (values.size() - 1)) : 0.0;

    if (stdev < 1e-12) {
      return generateEqualInterval(values, num_classes, colors);
    }

    double min_val = *std::min_element(values.begin(), values.end());
    double max_val = *std::max_element(values.begin(), values.end());

    double half = num_classes / 2.0;
    std::vector<double> breaks;
    for (int i = 0; i <= num_classes; i++) {
      breaks.push_back(mean + (i - half) * stdev);
    }
    breaks.front() = std::min(breaks.front(), min_val);
    breaks.back() = std::max(breaks.back(), max_val);

    for (int i = 0; i < num_classes; i++) {
      ColorClass cls;
      cls.min_val = breaks[i];
      cls.max_val = breaks[i + 1];
      cls.color = colors[i % colors.size()];
      cls.label = formatLabel(cls.min_val, cls.max_val);
      classes.push_back(cls);
    }
    return classes;
  }

  // Rounds a magnitude to a "nice" 1/2/5 * 10^n step, the same trick R's
  // pretty() and most charting libraries use for readable axis ticks.
  static double niceNum(double range, bool round) {
    if (range <= 0) return 1.0;
    double exponent = std::floor(std::log10(range));
    double fraction = range / std::pow(10, exponent);
    double nice_fraction;
    if (round) {
      if (fraction < 1.5) nice_fraction = 1;
      else if (fraction < 3) nice_fraction = 2;
      else if (fraction < 7) nice_fraction = 5;
      else nice_fraction = 10;
    } else {
      if (fraction <= 1) nice_fraction = 1;
      else if (fraction <= 2) nice_fraction = 2;
      else if (fraction <= 5) nice_fraction = 5;
      else nice_fraction = 10;
    }
    return nice_fraction * std::pow(10, exponent);
  }

  // Breaks at round numbers (10, 25, 50...) instead of raw quantile/
  // equal-interval cut points. The resulting class count is close to but
  // not guaranteed to be exactly num_classes -- that's inherent to
  // rounding to nice steps.
  std::vector<ColorClass> generatePrettyBreaks(const std::vector<double>& values, int num_classes,
                                               const std::string& palette_name) {
    std::vector<ColorClass> classes;
    if (values.empty())
      return classes;

    double min_val = *std::min_element(values.begin(), values.end());
    double max_val = *std::max_element(values.begin(), values.end());
    double range = niceNum(max_val - min_val, false);
    double step = niceNum(range / std::max(1, num_classes - 1), true);
    if (step < 1e-12) step = 1.0;

    double nice_min = std::floor(min_val / step) * step;
    double nice_max = std::ceil(max_val / step) * step;

    std::vector<double> breaks;
    for (double b = nice_min; b <= nice_max + step * 0.5; b += step) {
      breaks.push_back(b);
      if (breaks.size() > 500) break;  // safety valve against a pathological step
    }
    if (breaks.size() < 2) breaks = {min_val, max_val};

    int n = (int)breaks.size() - 1;
    auto colors = palette.getColors(palette_name, n);
    for (int i = 0; i < n; i++) {
      ColorClass cls;
      cls.min_val = breaks[i];
      cls.max_val = breaks[i + 1];
      cls.color = colors[i % colors.size()];
      cls.label = formatLabel(cls.min_val, cls.max_val);
      classes.push_back(cls);
    }
    return classes;
  }

  // Class widths grow in geometric progression -- useful for very skewed
  // data (population, income) where equal_interval leaves almost
  // everything in one class. Values <= 0 are handled by shifting the
  // whole range positive before computing the progression.
  std::vector<ColorClass> generateGeometricInterval(const std::vector<double>& values, int num_classes,
                                                     const std::vector<std::string>& colors) {
    std::vector<ColorClass> classes;
    if (values.empty())
      return classes;

    double min_val = *std::min_element(values.begin(), values.end());
    double max_val = *std::max_element(values.begin(), values.end());

    double shift = (min_val <= 0) ? (-min_val + 1.0) : 0.0;
    double gmin = std::max(1e-12, min_val + shift);
    double gmax = std::max(gmin, max_val + shift);

    double ratio = (gmax > gmin) ? std::pow(gmax / gmin, 1.0 / num_classes) : 1.0;

    double prev = gmin;
    for (int i = 0; i < num_classes; i++) {
      double next = (i == num_classes - 1) ? gmax : gmin * std::pow(ratio, i + 1);
      ColorClass cls;
      cls.min_val = prev - shift;
      cls.max_val = next - shift;
      cls.color = colors[i % colors.size()];
      cls.label = formatLabel(cls.min_val, cls.max_val);
      classes.push_back(cls);
      prev = next;
    }
    return classes;
  }

  // Fixed-width classes: the user gives the step, the class count falls
  // out of dividing the data range by it.
  std::vector<ColorClass> generateDefinedInterval(const std::vector<double>& values, double interval_width,
                                                   const std::vector<std::string>& colors) {
    std::vector<ColorClass> classes;
    if (values.empty() || interval_width <= 0)
      return classes;

    double min_val = *std::min_element(values.begin(), values.end());
    double max_val = *std::max_element(values.begin(), values.end());
    int n = std::max(1, (int)std::ceil((max_val - min_val) / interval_width));

    for (int i = 0; i < n; i++) {
      ColorClass cls;
      cls.min_val = min_val + i * interval_width;
      cls.max_val = (i == n - 1) ? max_val : min_val + (i + 1) * interval_width;
      cls.color = colors[i % colors.size()];
      cls.label = formatLabel(cls.min_val, cls.max_val);
      classes.push_back(cls);
    }
    return classes;
  }

  // User-supplied break points, taken as-is (sorted) instead of computed.
  std::vector<ColorClass> generateManual(const std::vector<double>& breaks_in,
                                         const std::vector<std::string>& colors) {
    std::vector<ColorClass> classes;
    if (breaks_in.size() < 2)
      return classes;

    std::vector<double> breaks = breaks_in;
    std::sort(breaks.begin(), breaks.end());

    int n = (int)breaks.size() - 1;
    for (int i = 0; i < n; i++) {
      ColorClass cls;
      cls.min_val = breaks[i];
      cls.max_val = breaks[i + 1];
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

public:
  // -attribute value from a vector dataset, numeric entries only (same
  // behavior as before this was renamed from the unqualified
  // extractValues).
  std::vector<double> extractVectorValues(const Spatial::VectorDataset& dataset,
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

  // Every non-nodata cell value of a raster, flattened. There is no
  // "attribute" to name for a raster -- the value classified is the cell
  // itself.
  std::vector<double> extractRasterValues(const Spatial::RasterDataset& dataset) {
    std::vector<double> values;
    values.reserve(dataset.data.size());
    for (double v : dataset.data) {
      if (std::abs(v - dataset.nodata_value) < 1e-9) continue;
      if (std::isnan(v) || std::isinf(v)) continue;
      values.push_back(v);
    }
    return values;
  }

  // The single fixed color for -method single: just the first stop of the
  // chosen palette, so "single" still reads as "a color from that
  // palette" rather than an arbitrary hardcoded default.
  std::string singleColor(const std::string& palette_name) {
    return palette.getColors(palette_name, 1)[0];
  }

  ColorMapConfig generate(const std::vector<double>& values, const std::string& palette_name,
                          const std::string& method, int num_classes, int layer_num,
                          double interval_width, const std::vector<double>& manual_breaks) {
    ColorMapConfig config;
    config.palette = palette_name;
    config.method = method;
    config.num_classes = num_classes;
    config.layer_num = layer_num;

    if (method != "manual" && values.empty()) {
      std::cerr << "Error: No numeric values found to classify\n";
      return config;
    }

    if (method == "quantile") {
      config.classes = generateQuantile(values, num_classes, palette.getColors(palette_name, num_classes));
    } else if (method == "equal_interval") {
      config.classes =
          generateEqualInterval(values, num_classes, palette.getColors(palette_name, num_classes));
    } else if (method == "natural_breaks") {
      config.classes =
          generateNaturalBreaks(values, num_classes, palette.getColors(palette_name, num_classes));
    } else if (method == "standard_deviation") {
      config.classes = generateStandardDeviation(values, num_classes,
                                                  palette.getColors(palette_name, num_classes));
    } else if (method == "pretty_breaks") {
      config.classes = generatePrettyBreaks(values, num_classes, palette_name);
      config.num_classes = (int)config.classes.size();
    } else if (method == "geometric_interval") {
      config.classes = generateGeometricInterval(values, num_classes,
                                                  palette.getColors(palette_name, num_classes));
    } else if (method == "continuous") {
      // Same as equal_interval, just meant to be called with a much finer
      // num_classes (main() bumps the default) -- see ADR-0004: many
      // narrow precomputed classes read as continuous, with no
      // interpolation needed from any consumer.
      config.classes =
          generateEqualInterval(values, num_classes, palette.getColors(palette_name, num_classes));
    } else if (method == "defined_interval") {
      double min_val = *std::min_element(values.begin(), values.end());
      double max_val = *std::max_element(values.begin(), values.end());
      int n = std::max(1, (int)std::ceil((max_val - min_val) / interval_width));
      config.classes = generateDefinedInterval(values, interval_width, palette.getColors(palette_name, n));
      config.num_classes = n;
    } else if (method == "manual") {
      int n = std::max(1, (int)manual_breaks.size() - 1);
      config.classes = generateManual(manual_breaks, palette.getColors(palette_name, n));
      config.num_classes = n;
    } else {
      std::cerr << "Warning: Unknown method '" << method << "', using quantile\n";
      config.classes = generateQuantile(values, num_classes, palette.getColors(palette_name, num_classes));
    }

    return config;
  }
};

class StyleWriter {
public:
  void write(const ColorMapConfig& config, std::ostream& out, const std::string& input_file) {
    out << "# =============================================\n";
    out << "# STYLE GENERATED BY spatial_colormap\n";
    out << "# =============================================\n";
    out << "# Date: " << getCurrentDate() << "\n";
    if (!config.attribute.empty()) out << "# Attribute: " << config.attribute << "\n";
    out << "# Method: " << config.method << "\n";
    if (!config.classes.empty()) {
      out << "# Palette: " << config.palette << "\n";
      out << "# Classes: " << config.classes.size() << "\n";
    }
    out << "# =============================================\n\n";

    out << "# Layer configuration\n";
    out << "layer." << config.layer_num << ".file=" << input_file << "\n";
    out << "layer." << config.layer_num << ".choropleth=" << (config.classes.empty() ? "false" : "true")
        << "\n";
    if (!config.attribute.empty()) {
      out << "layer." << config.layer_num << ".attribute=" << config.attribute << "\n";
    }
    if (!config.classes.empty()) {
      out << "layer." << config.layer_num << ".num_classes=" << config.classes.size() << "\n";
      out << "layer." << config.layer_num << ".method=" << config.method << "\n";
      out << "layer." << config.layer_num << ".palette=" << config.palette << "\n";
    }
    if (!config.fixed_color.empty()) {
      out << "layer." << config.layer_num << ".fill=" << config.fixed_color << "\n";
    }
    if (config.show_labels) {
      out << "layer." << config.layer_num << ".show_labels=true\n";
      if (!config.label_field.empty()) {
        out << "layer." << config.layer_num << ".label_field=" << config.label_field << "\n";
      }
    }
    out << "\n";

    if (!config.classes.empty()) {
      out << "# Classes\n";
      for (size_t i = 0; i < config.classes.size(); i++) {
        const auto& cls = config.classes[i];
        out << "layer." << config.layer_num << ".class." << i << ".min=" << cls.min_val << "\n";
        out << "layer." << config.layer_num << ".class." << i << ".max=" << cls.max_val << "\n";
        out << "layer." << config.layer_num << ".class." << i << ".color=" << cls.color << "\n";
        out << "layer." << config.layer_num << ".class." << i << ".label=" << cls.label << "\n";
        out << "\n";
      }
    }

    // Legend keys are namespaced under layer.N. like everything else (see
    // ADR-0005) -- a legend always belongs to one layer, so a hand-written
    // multi-layer -style file can turn each on/off and position each
    // independently. Only enabled/title/position have a renderer that
    // consumes them (spatial_svg) -- the finer appearance knobs an earlier
    // version of this writer emitted (font_size, font_color, background,
    // border_color, border_width, padding, margin, columns) were metadata
    // nobody read, so they're not written anymore (ADR-0005, Decision 2).
    if (config.include_legend) {
      out << "# Legend configuration\n";
      out << "layer." << config.layer_num << ".legend.enabled=true\n";
      std::string default_title = config.attribute.empty() ? "Style" : config.attribute;
      out << "layer." << config.layer_num << ".legend.title="
          << (config.legend_title.empty() ? default_title : config.legend_title) << "\n";
      out << "layer." << config.layer_num << ".legend.position=" << config.legend_position << "\n\n";
    } else {
      out << "# Legend configuration\n";
      out << "layer." << config.layer_num << ".legend.enabled=false\n\n";
    }

    if (!config.classes.empty()) {
      out << "# Statistics\n";
      double min_val = config.classes.front().min_val;
      double max_val = config.classes.back().max_val;
      out << "# Min value: " << min_val << "\n";
      out << "# Max value: " << max_val << "\n";
      out << "# Range: " << (max_val - min_val) << "\n";
    }

    out << "\n# =============================================\n";
    out << "# END OF STYLE\n";
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
  std::cerr << "spatial_colormap - Generate a .sty color map / style file for a vector or raster layer\n\n";
  std::cerr << "Usage: spatial_colormap <input_file> [options]\n\n";
  std::cerr << "Required:\n";
  std::cerr << "  <input_file>         Input CSV (vector, needs -attribute) or ASCII Grid .asc/.grd (raster)\n";
  std::cerr << "  -attribute <col>     Column name for values (required for CSV input, ignored for raster\n";
  std::cerr << "                       and for -method single)\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -palette <name>      Color palette (default: YlOrRd)\n";
  std::cerr << "  -classes <num>       Number of classes (default: 5, or 32 for -method continuous)\n";
  std::cerr << "  -method <method>     Classification method:\n";
  std::cerr << "                       quantile, equal_interval, natural_breaks, standard_deviation,\n";
  std::cerr << "                       pretty_breaks, geometric_interval, defined_interval, manual,\n";
  std::cerr << "                       continuous, single\n";
  std::cerr << "  -interval <width>    Class width (required for -method defined_interval)\n";
  std::cerr << "  -breaks <v1,v2,...>  Explicit break points, at least 2 values (required for -method manual)\n";
  std::cerr << "  -layer <num>         Layer number (default: 1)\n";
  std::cerr << "  -title <text>        Legend title (default: attribute name)\n";
  std::cerr << "  -no-legend           Disable legend generation\n";
  std::cerr << "  -legend-position <p> Legend corner for spatial_svg: top-left, top-right,\n";
  std::cerr << "                       bottom-left, bottom-right (default: bottom-right)\n";
  std::cerr << "  -show-labels         Emit per-feature/per-cell labels (spatial_svg, spatial_viewer)\n";
  std::cerr << "  -label-field <col>   Attribute column to use as label text (vector only; defaults\n";
  std::cerr << "                       to -attribute if -show-labels is given without this)\n";
  std::cerr << "  -output <file>       Output .sty file (default: <input_file>.sty; use '-' for stdout)\n";
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
  std::cerr << "  spatial_colormap dem.asc -method continuous -palette Viridis\n";
  std::cerr << "  spatial_colormap slopes.asc -method equal_interval -classes 6\n";
  std::cerr << "  spatial_colormap zones.csv -method single -palette Blues\n";
  std::cerr << "  spatial_colormap data.csv -attribute density -method manual -breaks 0,10,50,200,1000\n";
  std::cerr << "  spatial_colormap cities.csv -attribute population -show-labels -label-field name\n";
  std::cerr << "  (writes countries.sty next to countries.csv unless -output is given)\n";
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

static std::vector<double> parseBreaks(const std::string& csv) {
  std::vector<double> breaks;
  std::stringstream ss(csv);
  std::string token;
  while (std::getline(ss, token, ',')) {
    try {
      breaks.push_back(std::stod(token));
    } catch (...) {
    }
  }
  return breaks;
}

static bool isRasterFile(const std::string& filename) {
  std::string ext = std::filesystem::path(filename).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  return ext == ".asc" || ext == ".grd";
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    printUsage();
    return 1;
  }

  std::string input_file;
  std::string attribute;
  std::string palette = "YlOrRd";
  std::string method = "quantile";
  int num_classes = 5;
  bool classes_explicit = false;
  int layer_num = 1;
  std::string legend_title;
  bool include_legend = true;
  std::string legend_position = "bottom-right";
  std::string output_file;
  bool list_palettes = false;
  double interval_width = 0.0;
  std::vector<double> manual_breaks;
  bool show_labels = false;
  std::string label_field;

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];

    if (arg == "-attribute" && i + 1 < argc) {
      attribute = argv[++i];
    } else if (arg == "-palette" && i + 1 < argc) {
      palette = argv[++i];
    } else if (arg == "-classes" && i + 1 < argc) {
      num_classes = std::stoi(argv[++i]);
      classes_explicit = true;
      if (num_classes < 2)
        num_classes = 2;
      if (num_classes > 256)
        num_classes = 256;
    } else if (arg == "-method" && i + 1 < argc) {
      method = argv[++i];
    } else if (arg == "-interval" && i + 1 < argc) {
      interval_width = std::stod(argv[++i]);
    } else if (arg == "-breaks" && i + 1 < argc) {
      manual_breaks = parseBreaks(argv[++i]);
    } else if (arg == "-layer" && i + 1 < argc) {
      layer_num = std::stoi(argv[++i]);
    } else if (arg == "-title" && i + 1 < argc) {
      legend_title = argv[++i];
    } else if (arg == "-no-legend") {
      include_legend = false;
    } else if (arg == "-legend-position" && i + 1 < argc) {
      legend_position = argv[++i];
    } else if (arg == "-show-labels") {
      show_labels = true;
    } else if (arg == "-label-field" && i + 1 < argc) {
      label_field = argv[++i];
      show_labels = true;
    } else if (arg == "-output" && i + 1 < argc) {
      output_file = argv[++i];
    } else if (arg == "-list-palettes") {
      list_palettes = true;
    } else if (arg == "-help" || arg == "--help") {
      printUsage();
      return 0;
    } else if (input_file.empty() && arg[0] != '-') {
      input_file = arg;
    }
  }

  if (list_palettes) {
    listPalettes();
    return 0;
  }

  if (input_file.empty()) {
    std::cerr << "Error: Input file required\n";
    printUsage();
    return 1;
  }

  bool is_raster = isRasterFile(input_file);

  if (!is_raster && method != "single" && attribute.empty()) {
    std::cerr << "Error: -attribute is required for vector (CSV) input\n";
    printUsage();
    return 1;
  }

  if (method == "defined_interval" && interval_width <= 0) {
    std::cerr << "Error: -method defined_interval requires -interval <width> > 0\n";
    return 1;
  }

  if (method == "manual" && manual_breaks.size() < 2) {
    std::cerr << "Error: -method manual requires -breaks with at least 2 values\n";
    return 1;
  }

  if (method == "continuous" && !classes_explicit) {
    num_classes = 32;
  }

  ColorMapGenerator generator;
  ColorMapConfig config;

  if (method == "single") {
    // No classification at all -- just one fixed color for the whole
    // layer, taken from the chosen palette (see ADR-0004, Grupo 2).
    config.method = method;
    config.palette = palette;
    config.layer_num = layer_num;
    config.fixed_color = generator.singleColor(palette);
  } else if (method == "manual") {
    // Manual breaks don't need any data read at all -- the classes come
    // straight from -breaks.
    config = generator.generate({}, palette, method, num_classes, layer_num, interval_width, manual_breaks);
  } else if (is_raster) {
    Spatial::ASCIIGridReader reader;
    Spatial::RasterDataset dataset;
    if (!reader.read(input_file, dataset)) {
      std::cerr << "Error: Could not read raster file: " << input_file << "\n";
      return 1;
    }

    auto values = generator.extractRasterValues(dataset);
    if (values.empty()) {
      std::cerr << "Error: No valid (non-nodata) cell values found in: " << input_file << "\n";
      return 1;
    }

    config = generator.generate(values, palette, method, num_classes, layer_num, interval_width, manual_breaks);
  } else {
    Spatial::SpatialCSVReader reader;
    Spatial::VectorDataset dataset;
    if (!reader.read(input_file, dataset)) {
      std::cerr << "Error: Could not read CSV file: " << input_file << "\n";
      return 1;
    }

    auto values = generator.extractVectorValues(dataset, attribute);
    if (values.empty()) {
      std::cerr << "Error: No numeric values found for attribute '" << attribute << "'\n";
      return 1;
    }

    config = generator.generate(values, palette, method, num_classes, layer_num, interval_width, manual_breaks);
    config.attribute = attribute;
  }

  if (config.classes.empty() && config.fixed_color.empty()) {
    std::cerr << "Error: Failed to generate a style (no classes and no fixed color)\n";
    return 1;
  }

  config.legend_title = legend_title;
  config.include_legend = include_legend;
  config.legend_position = legend_position;

  // -show-labels with no explicit -label-field on a vector layer falls
  // back to the classified attribute itself -- better than emitting
  // show_labels=true with nothing to show (see ADR-0005, Decision 5). For
  // raster there's no attribute to fall back to; -show-labels there just
  // enables spatial_viewer's existing per-cell label mechanism (RAT label
  // field or raw cell value), which doesn't use label_field at all.
  config.show_labels = show_labels;
  config.label_field = label_field;
  if (config.show_labels && config.label_field.empty() && !is_raster) {
    config.label_field = attribute;
  }

  std::string out_path = output_file;
  if (out_path.empty()) {
    out_path = std::filesystem::path(input_file).replace_extension(".sty").string();
  }

  StyleWriter writer;
  if (out_path == "-") {
    writer.write(config, std::cout, input_file);
  } else {
    std::ofstream file(out_path);
    if (!file.is_open()) {
      std::cerr << "Error: Could not create output file: " << out_path << "\n";
      return 1;
    }
    writer.write(config, file, input_file);
    file.close();
    std::cout << "Style written to: " << out_path << "\n";
  }

  return 0;
}
