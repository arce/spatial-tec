#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../include/core/ascii_grid.hpp"
#include "../include/core/spatial_io.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

void printUsage() {
  std::cerr << "spatial_rat - Export/import a raster attribute table (RAT) for an ASCII Grid file\n\n";
  std::cerr << "Usage: spatial_rat <input.asc> <table.csv> [output.asc] -mode <export|import>\n\n";
  std::cerr << "Modes:\n";
  std::cerr << "  -mode export    Export the RAT already embedded in <input.asc> to <table.csv>\n";
  std::cerr << "  -mode import    Import <table.csv> as the RAT of <input.asc>, writing <output.asc> (required for this mode)\n\n";
  std::cerr << "Notes:\n";
  std::cerr << "  <table.csv> for import must have a column named 'value' (any case), matched\n";
  std::cerr << "  against cell values when looking up a class (see RasterDataset::findRatRow).\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_rat classes.asc classes_rat.csv -mode export\n";
  std::cerr << "  spatial_rat classes.asc rat_table.csv classes_with_rat.asc -mode import\n";
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printUsage();
    return 1;
  }

  std::string input_file = argv[1];
  std::string table_file = argv[2];
  std::string output_file;
  std::string mode;

  int i = 3;
  if (i < argc && !std::string(argv[i]).empty() && argv[i][0] != '-') {
    output_file = argv[i];
    ++i;
  }

  for (; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-mode" && i + 1 < argc) {
      mode = argv[++i];
    } else if (arg == "-help" || arg == "-h") {
      printUsage();
      return 0;
    } else {
      std::cerr << "Error: Unknown option: " << arg << "\n";
      printUsage();
      return 1;
    }
  }

  if (mode != "export" && mode != "import") {
    std::cerr << "Error: -mode is required and must be one of: export, import\n";
    printUsage();
    return 1;
  }
  if (mode == "import" && output_file.empty()) {
    std::cerr << "Error: -mode import requires the third positional argument <output.asc>\n";
    printUsage();
    return 1;
  }

  std::cout << "========================================\n";
  std::cout << "  RAT " << mode << "\n";
  std::cout << "========================================\n\n";
  std::cout << "input.asc: " << input_file << "\n";
  std::cout << "table.csv: " << table_file << "\n";
  if (!output_file.empty())
    std::cout << "output.asc: " << output_file << "\n";
  std::cout << "-mode: " << mode << "\n\n";

  Spatial::ASCIIGridReader reader;
  Spatial::RasterDataset dataset;
  if (!reader.read(input_file, dataset)) {
    std::cerr << "Error: Could not read input file: " << input_file << "\n";
    return 1;
  }
  std::cout << "Input raster: " << dataset.ncols << "x" << dataset.nrows << " cells\n";

  if (mode == "export") {
    if (!dataset.has_rat || dataset.rat_columns.empty()) {
      std::cerr << "Error: " << input_file
                << " has no embedded @RAT section -- nothing to export.\n";
      std::cerr << "Use -mode import to attach a RAT to this raster first.\n";
      return 1;
    }

    std::ofstream out(table_file);
    if (!out.is_open()) {
      std::cerr << "Error: Could not create file: " << table_file << "\n";
      return 1;
    }
    for (size_t c = 0; c < dataset.rat_columns.size(); ++c) {
      out << dataset.rat_columns[c];
      if (c + 1 < dataset.rat_columns.size()) out << ",";
    }
    out << "\n";
    for (const auto& row : dataset.rat_rows) {
      for (size_t c = 0; c < dataset.rat_columns.size(); ++c) {
        auto it = row.find(dataset.rat_columns[c]);
        if (it != row.end()) out << it->second;
        if (c + 1 < dataset.rat_columns.size()) out << ",";
      }
      out << "\n";
    }

    std::cout << "RAT columns: ";
    for (size_t c = 0; c < dataset.rat_columns.size(); ++c) {
      std::cout << dataset.rat_columns[c] << (c + 1 < dataset.rat_columns.size() ? ", " : "\n");
    }
    std::cout << "RAT rows exported: " << dataset.rat_rows.size() << "\n";
    std::cout << "\nOutput written to: " << table_file << "\n";
    std::cout << "Export complete.\n";
    return 0;
  }

  std::ifstream in(table_file);
  if (!in.is_open()) {
    std::cerr << "Error: Could not open table file: " << table_file << "\n";
    return 1;
  }

  std::vector<std::string> columns;
  std::vector<std::unordered_map<std::string, std::string>> rows;
  std::string line;
  bool header_seen = false;
  int value_col_index = -1;

  while (std::getline(in, line)) {
    std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#') continue;

    auto fields = splitCSV(trimmed);
    if (!header_seen) {
      columns = fields;
      for (size_t c = 0; c < columns.size(); ++c) {
        if (toLower(trim(columns[c])) == "value") {
          value_col_index = static_cast<int>(c);

          columns[c] = "value";
          break;
        }
      }
      if (value_col_index < 0) {
        std::cerr << "Error: " << table_file
                  << " has no 'value' column -- required to match cells to RAT rows.\n";
        return 1;
      }
      header_seen = true;
      continue;
    }

    std::unordered_map<std::string, std::string> row;
    for (size_t c = 0; c < columns.size(); ++c) {
      row[columns[c]] = (c < fields.size()) ? fields[c] : "";
    }
    rows.push_back(std::move(row));
  }

  if (!header_seen) {
    std::cerr << "Error: " << table_file << " has no header row.\n";
    return 1;
  }

  dataset.rat_columns = columns;
  dataset.rat_rows = rows;
  dataset.has_rat = !dataset.rat_rows.empty();

  writeRasterASCII(dataset, output_file);

  std::cout << "RAT columns: ";
  for (size_t c = 0; c < dataset.rat_columns.size(); ++c) {
    std::cout << dataset.rat_columns[c] << (c + 1 < dataset.rat_columns.size() ? ", " : "\n");
  }
  std::cout << "RAT rows imported: " << dataset.rat_rows.size() << "\n";
  std::cout << "\nOutput written to: " << output_file << "\n";
  std::cout << "Import complete.\n";

  return 0;
}

