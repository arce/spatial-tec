#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../include/core/ascii_grid.hpp"
#include "../include/core/spatial_io.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

void printUsage() {
  std::cerr << "spatial_reclass - reclasifica los valores de un raster segun una tabla de rangos o valores\n";
  std::cerr << "\n";
  std::cerr << "Usage: spatial_reclass <input.asc> <output.asc> [options]\n";
  std::cerr << "\n";
  std::cerr << "Options:\n";
  std::cerr << "  -table <file.csv>   Tabla de reclasificacion: columnas from,to,new_value o value,new_value (requerido)\n";
  std::cerr << "  -nodata <value>     Valor NODATA de salida, tambien usado para celdas sin regla (default: -9999)\n";
  std::cerr << "\n";
  std::cerr << "Table format (rangos):\n";
  std::cerr << "  from,to,new_value\n";
  std::cerr << "  0,10,1\n";
  std::cerr << "  10,20,2\n";
  std::cerr << "\n";
  std::cerr << "Table format (valor exacto):\n";
  std::cerr << "  value,new_value\n";
  std::cerr << "  1,100\n";
  std::cerr << "  2,200\n";
  std::cerr << "\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_reclass landuse.asc reclass.asc -table rules.csv\n";
}

struct RangeRule {
  double from, to, new_value;
};

struct ExactRule {
  double value, new_value;
};

bool loadTable(const std::string& filename, bool& is_range_mode, std::vector<RangeRule>& ranges,
               std::vector<ExactRule>& exacts, std::string& error) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    error = "Could not open table file: " + filename;
    return false;
  }

  std::string line;
  bool header_seen = false;
  int line_no = 0;
  while (std::getline(file, line)) {
    ++line_no;
    std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#') continue;

    if (!header_seen) {
      auto cols = splitCSV(trimmed);
      std::vector<std::string> lower_cols;
      for (auto& c : cols) lower_cols.push_back(toLower(trim(c)));

      bool has_from = false, has_to = false, has_new = false, has_value = false;
      for (const auto& c : lower_cols) {
        if (c == "from" || c == "value_from") has_from = true;
        if (c == "to" || c == "value_to") has_to = true;
        if (c == "new_value") has_new = true;
        if (c == "value") has_value = true;
      }

      if (has_from && has_to && has_new) {
        is_range_mode = true;
      } else if (has_value && has_new) {
        is_range_mode = false;
      } else {
        error = "Table header must be 'from,to,new_value' or 'value,new_value' (got: " + trimmed + ")";
        return false;
      }
      header_seen = true;
      continue;
    }

    auto values = splitCSV(trimmed);
    try {
      if (is_range_mode) {
        if (values.size() < 3) {
          error = "Line " + std::to_string(line_no) + ": expected 3 columns (from,to,new_value)";
          return false;
        }
        RangeRule r;
        r.from = std::stod(values[0]);
        r.to = std::stod(values[1]);
        r.new_value = std::stod(values[2]);
        ranges.push_back(r);
      } else {
        if (values.size() < 2) {
          error = "Line " + std::to_string(line_no) + ": expected 2 columns (value,new_value)";
          return false;
        }
        ExactRule r;
        r.value = std::stod(values[0]);
        r.new_value = std::stod(values[1]);
        exacts.push_back(r);
      }
    } catch (const std::exception&) {
      error = "Line " + std::to_string(line_no) + ": could not parse numeric values: " + trimmed;
      return false;
    }
  }

  if (!header_seen) {
    error = "Table file has no header row: " + filename;
    return false;
  }
  return true;
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printUsage();
    return 1;
  }

  std::string input_file = argv[1];
  std::string output_file = argv[2];
  std::string table_file = "";
  double nodata = -9999.0;
  for (int i = 3; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-table" && i + 1 < argc) {
      table_file = argv[++i];
    } else if (arg == "-nodata" && i + 1 < argc) {
      nodata = std::stod(argv[++i]);
    } else if (arg == "-help" || arg == "-h") {
      printUsage();
      return 0;
    } else {
      std::cerr << "Error: Unknown option: " << arg << "\n";
      printUsage();
      return 1;
    }
  }
  if (table_file.empty()) {
    std::cerr << "Error: -table is required\n";
    printUsage();
    return 1;
  }

  std::cout << "========================================\n";
  std::cout << "  RECLASSIFY (RECLASIFICACION)\n";
  std::cout << "========================================\n\n";
  std::cout << "input.asc: " << input_file << "\n";
  std::cout << "output.asc: " << output_file << "\n";
  std::cout << "-table: " << table_file << "\n";
  std::cout << "-nodata: " << nodata << "\n\n";

  bool is_range_mode = true;
  std::vector<RangeRule> ranges;
  std::vector<ExactRule> exacts;
  std::string table_error;
  if (!loadTable(table_file, is_range_mode, ranges, exacts, table_error)) {
    std::cerr << "Error: " << table_error << "\n";
    return 1;
  }
  std::cout << "Table mode: " << (is_range_mode ? "range (from/to/new_value)" : "exact (value/new_value)")
            << "\n";
  std::cout << "Rules loaded: " << (is_range_mode ? ranges.size() : exacts.size()) << "\n\n";

  Spatial::ASCIIGridReader reader;
  Spatial::RasterDataset input;
  if (!reader.read(input_file, input)) {
    std::cerr << "Error: Could not read input file: " << input_file << "\n";
    return 1;
  }
  std::cout << "Input raster: " << input.ncols << "x" << input.nrows << " cells\n";

  Spatial::RasterDataset output = input;
  output.nodata_value = nodata;
  output.has_rat = false;
  output.rat_columns.clear();
  output.rat_rows.clear();

  size_t reclassified = 0;
  size_t unmatched = 0;
  size_t input_nodata = 0;

  for (int r = 0; r < input.nrows; ++r) {
    for (int c = 0; c < input.ncols; ++c) {
      double v = input.at(r, c);
      if (std::fabs(v - input.nodata_value) < 1e-9) {
        output.at(r, c) = nodata;
        ++input_nodata;
        continue;
      }

      bool matched = false;
      double new_val = nodata;
      if (is_range_mode) {
        for (const auto& rule : ranges) {
          if (v >= rule.from && v <= rule.to) {
            new_val = rule.new_value;
            matched = true;
            break;
          }
        }
      } else {
        for (const auto& rule : exacts) {
          if (std::fabs(v - rule.value) < 1e-9) {
            new_val = rule.new_value;
            matched = true;
            break;
          }
        }
      }

      if (matched) {
        output.at(r, c) = new_val;
        ++reclassified;
      } else {
        output.at(r, c) = nodata;
        ++unmatched;
      }
    }
  }

  writeRasterASCII(output, output_file);

  std::cout << "Reclassified cells: " << reclassified << "\n";
  std::cout << "Unmatched cells (-> NODATA): " << unmatched << "\n";
  std::cout << "Input NODATA cells (-> NODATA): " << input_nodata << "\n";
  std::cout << "\nOutput written to: " << output_file << "\n";
  std::cout << "Reclassification complete.\n";

  return 0;
}

