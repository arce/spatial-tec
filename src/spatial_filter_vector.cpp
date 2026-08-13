#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "../include/core/spatial_csv.hpp"
#include "../include/core/spatial_geom.hpp"
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

    if (c.value.length() >= 2) {
      if ((c.value.front() == '"' && c.value.back() == '"') ||
          (c.value.front() == '\'' && c.value.back() == '\'')) {
        c.value = c.value.substr(1, c.value.length() - 2);
      }
    }

    if (!c.column.empty() && !c.value.empty()) {
      result.push_back(c);
    }
  }

  return result;
}

bool evaluateValue(const std::string& left_str, const std::string& op,
                   const std::string& right_str) {
  try {
    double left = std::stod(left_str);
    double right = std::stod(right_str);

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
  } catch (const std::exception&) {
    if (op == "=")
      return left_str == right_str;
    if (op == "!=")
      return left_str != right_str;
    if (op == ">")
      return left_str > right_str;
    if (op == "<")
      return left_str < right_str;
    if (op == ">=")
      return left_str >= right_str;
    if (op == "<=")
      return left_str <= right_str;
  }

  return false;
}

bool evaluateCondition(const Spatial::VectorFeature& feature,
                       const std::vector<Condition>& conditions) {
  for (const auto& cond : conditions) {
    auto it = feature.attributes.find(cond.column);
    if (it == feature.attributes.end()) {
      return false;
    }
    if (!evaluateValue(it->second, cond.op, cond.value)) {
      return false;
    }
  }
  return true;
}

void printUsage() {
  std::cerr << "spatial_filter_vector - Filter vector data by attributes\n\n";
  std::cerr << "Usage: spatial_filter_vector <input> <output> <condition>\n\n";
  std::cerr << "Condition format: column=value, column>10, column<5\n";
  std::cerr << "Multiple conditions: column>10 AND column<20\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_filter_vector cities.csv large.csv \"population>100000\"\n";
  std::cerr << "  spatial_filter_vector cities.csv capitals.csv \"type=capital\"\n";
  std::cerr
      << "  spatial_filter_vector cities.csv filtered.csv \"population>50000 AND type=city\"\n";
  std::cerr << "  spatial_filter_vector cities.csv san_jose.csv \"name=\\\"San Jose\\\"\"\n\n";
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

  if (ext != ".csv" && ext != ".tsv") {
    std::cerr << "Error: Input must be CSV file\n";
    return 1;
  }

  std::cout << "Reading: " << input_file << "\n";
  std::cout << "Writing: " << output_file << "\n";
  std::cout << "Condition: " << condition << "\n\n";

  Spatial::SpatialCSVReader reader;
  Spatial::VectorDataset dataset;

  if (!reader.read(input_file, dataset)) {
    std::cerr << "Error: Could not read input file\n";
    return 1;
  }

  std::cout << "Input features: " << dataset.features.size() << "\n";

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

  Spatial::VectorDataset output;
  output.columns = dataset.columns;
  output.geometry_column = dataset.geometry_column;
  output.crs = dataset.crs;

  for (const auto& feature : dataset.features) {
    if (evaluateCondition(feature, conditions)) {
      output.features.push_back(feature);
    }
  }

  writeVectorCSV(output, output_file, {}, {"# Filtered from original dataset"});

  std::cout << "Filtered: " << dataset.features.size() << " -> " << output.features.size()
            << " features\n";
  std::cout << "Filtering complete.\n";

  return 0;
}

