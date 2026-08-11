#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "../include/core/ascii_grid.hpp"
#include "../include/core/spatial_io.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

struct Condition {
  std::string column;
  std::string op;
  std::string value;
};

std::vector<Condition> parseCondition(const std::string& cond) {
  std::vector<Condition> result;
  std::string str = trim(cond);

  if (str.empty())
    return result;

  std::string upper = toUpper(str);
  std::vector<std::string> parts;
  size_t pos = 0;

  while (true) {
    size_t and_pos = upper.find(" AND ", pos);
    if (and_pos == std::string::npos) {
      parts.push_back(str.substr(pos));
      break;
    }
    parts.push_back(str.substr(pos, and_pos - pos));
    pos = and_pos + 5;
  }

  for (const auto& part : parts) {
    Condition c;
    std::string p = trim(part);

    size_t op_pos = std::string::npos;
    std::string ops[] = {">=", "<=", "!=", "=", ">", "<"};

    for (const std::string& op : ops) {
      op_pos = p.find(op);
      if (op_pos != std::string::npos) {
        c.op = op;
        break;
      }
    }

    if (op_pos == std::string::npos || op_pos == 0 || op_pos >= p.length() - 1) {
      std::cerr << "Warning: Invalid condition part: " << p << "\n";
      continue;
    }

    c.column = trim(p.substr(0, op_pos));
    c.value = trim(p.substr(op_pos + c.op.length()));

    if (!c.column.empty() && !c.value.empty()) {
      result.push_back(c);
    }
  }

  return result;
}

bool evaluateValue(double left, const std::string& op, double right) {
  if (op == "=")
    return std::abs(left - right) < 1e-9;
  if (op == "!=")
    return std::abs(left - right) >= 1e-9;
  if (op == ">")
    return left > right;
  if (op == "<")
    return left < right;
  if (op == ">=")
    return left >= right;
  if (op == "<=")
    return left <= right;
  return false;
}

void printUsage() {
  std::cerr << "spatial_filter_raster - Filter raster cells by value\n\n";
  std::cerr << "Usage: spatial_filter_raster <input> <output> <condition>\n\n";
  std::cerr << "Condition format: value>10, value<5, value=3\n";
  std::cerr << "Multiple conditions: value>10 AND value<20\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_filter_raster elevation.asc high.asc \"value>20\"\n";
  std::cerr << "  spatial_filter_raster elevation.asc range.asc \"value>=15 AND value<=25\"\n";
  std::cerr << "  spatial_filter_raster elevation.asc filtered.asc \"value>10 AND value<30\"\n\n";
  std::cerr << "Note: Always quote the condition to prevent shell interpretation.\n";
}

int main(int argc, char* argv[]) {
  if (argc < 4) {
    printUsage();
    return 1;
  }

  std::string input_file = argv[1];
  std::string output_file = argv[2];
  std::string condition = argv[3];

  for (int i = 4; i < argc; ++i) {
    condition += " " + std::string(argv[i]);
  }

  std::string ext = std::filesystem::path(input_file).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  if (ext != ".asc" && ext != ".grd") {
    std::cerr << "Error: Input must be ASCII Grid file\n";
    return 1;
  }

  std::cout << "Reading: " << input_file << "\n";
  std::cout << "Writing: " << output_file << "\n";
  std::cout << "Condition: " << condition << "\n\n";

  Spatial::ASCIIGridReader reader;
  Spatial::RasterDataset dataset;

  if (!reader.read(input_file, dataset)) {
    std::cerr << "Error: Could not read input file\n";
    return 1;
  }

  int total_cells = dataset.nrows * dataset.ncols;
  std::cout << "Input cells: " << total_cells << "\n";
  std::cout << "Dimensions: " << dataset.ncols << "x" << dataset.nrows << "\n";

  auto conditions = parseCondition(condition);
  if (conditions.empty()) {
    std::cerr << "Error: Invalid condition\n";
    return 1;
  }

  std::cout << "Conditions: " << conditions.size() << "\n";
  for (const auto& c : conditions) {
    std::cout << "  " << c.column << " " << c.op << " " << c.value << "\n";
  }
  std::cout << "\n";

  std::vector<std::pair<double, std::string>> numeric_conditions;
  for (const auto& c : conditions) {
    try {
      double val = std::stod(c.value);
      numeric_conditions.push_back({val, c.op});
    } catch (const std::exception&) {
      std::cerr << "Error: Condition value must be numeric for raster: " << c.value << "\n";
      return 1;
    }
  }

  Spatial::RasterDataset output = dataset;
  int changed = 0;

  for (int r = 0; r < dataset.nrows; ++r) {
    for (int c = 0; c < dataset.ncols; ++c) {
      double val = dataset.at(r, c);
      if (std::abs(val - dataset.nodata_value) < 1e-9)
        continue;

      bool passes = true;
      for (const auto& cond : conditions) {
        double right = std::stod(cond.value);
        if (!evaluateValue(val, cond.op, right)) {
          passes = false;
          break;
        }
      }

      if (!passes) {
        output.at(r, c) = dataset.nodata_value;
        changed++;
      }
    }
  }

  writeRasterASCII(output, output_file, 2);

  int valid_cells = total_cells - changed;
  std::cout << "Filtered: " << total_cells << " -> " << valid_cells << " valid cells\n";
  std::cout << "Cells removed: " << changed << "\n";
  std::cout << "Output dimensions: " << output.ncols << "x" << output.nrows << "\n";
  std::cout << "Filtering complete.\n";

  return 0;
}
