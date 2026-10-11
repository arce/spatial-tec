#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../include/core/spatial_address_utils.hpp"
#include "../include/core/spatial_string.hpp"

void printUsage() {
  std::cerr << "spatial_address_clean - Limpia y estandariza un archivo de direcciones antes de geocodificar\n\n";
  std::cerr << "Usage: spatial_address_clean <input.csv> <output.csv> [options]\n\n";
  std::cerr << "El archivo debe tener, como minimo, las columnas id, house_number y street_name\n";
  std::cerr << "(opcionales: city, postal_code). Las demas columnas se conservan sin cambios.\n\n";
  std::cerr << "Options (al menos una):\n";
  std::cerr << "  -standardize           Quita espacios y signos sobrantes, capitaliza calle y ciudad,\n";
  std::cerr << "                         deja house_number como entero (\"#150\", \" 150 \" -> 150)\n";
  std::cerr << "  -standardize_streets   Unifica tipos de via y puntos cardinales\n";
  std::cerr << "                         (Street -> St, Avenue -> Ave, North -> N, ...)\n";
  std::cerr << "  -validate              Elimina direcciones incompletas (sin house_number valido\n";
  std::cerr << "                         o sin street_name)\n";
  std::cerr << "  -rejects <file>        Con -validate, guarda las filas eliminadas en este archivo\n";
  std::cerr << "  -h, -help              Mostrar esta ayuda\n\n";
  std::cerr << "Las opciones se aplican en este orden: -standardize, -standardize_streets, -validate.\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_address_clean addresses_raw.csv addresses_clean.csv -standardize\n";
  std::cerr << "  spatial_address_clean addresses_raw.csv addresses_ok.csv -standardize -standardize_streets -validate\n";
}

// Extrae el entero de un numero de casa sucio: "#150", " 150 ", "150." -> 150.
// Devuelve 0 si no hay un entero positivo utilizable.
static int parseHouseNumber(const std::string& s) {
  size_t i = 0;
  while (i < s.size() && !std::isdigit((unsigned char)s[i])) ++i;
  if (i == s.size()) return 0;
  size_t j = i;
  while (j < s.size() && std::isdigit((unsigned char)s[j])) ++j;
  try {
    return std::stoi(s.substr(i, j - i));
  } catch (...) {
    return 0;
  }
}

static void writeRow(std::ofstream& out, const std::vector<std::string>& v) {
  for (size_t i = 0; i < v.size(); ++i) {
    if (i) out << ",";
    out << addr::csvEscape(v[i]);
  }
  out << "\n";
}

int main(int argc, char* argv[]) {
  if (argc == 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "-help")) {
    printUsage();
    return 0;
  }
  if (argc < 3) {
    printUsage();
    return 1;
  }

  std::string input_file = argv[1];
  std::string output_file = argv[2];
  std::string rejects_file;
  bool standardize = false, standardize_streets = false, validate = false;

  for (int i = 3; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-standardize") {
      standardize = true;
    } else if (arg == "-standardize_streets") {
      standardize_streets = true;
    } else if (arg == "-validate") {
      validate = true;
    } else if (arg == "-rejects" && i + 1 < argc) {
      rejects_file = argv[++i];
    } else if (arg == "-h" || arg == "-help") {
      printUsage();
      return 0;
    } else {
      std::cerr << "Error: Unknown option or missing value: " << arg << "\n\n";
      printUsage();
      return 1;
    }
  }

  if (!standardize && !standardize_streets && !validate) {
    std::cerr << "Error: specify at least one of -standardize, -standardize_streets, -validate\n\n";
    printUsage();
    return 1;
  }
  if (!rejects_file.empty() && !validate) {
    std::cerr << "Error: -rejects only makes sense together with -validate\n";
    return 1;
  }

  std::ifstream in(input_file);
  if (!in.is_open()) {
    std::cerr << "Error: Could not open file: " << input_file << "\n";
    return 1;
  }
  std::ofstream out(output_file);
  if (!out.is_open()) {
    std::cerr << "Error: Could not create output file: " << output_file << "\n";
    return 1;
  }
  std::ofstream rej;
  if (!rejects_file.empty()) {
    rej.open(rejects_file);
    if (!rej.is_open()) {
      std::cerr << "Error: Could not create rejects file: " << rejects_file << "\n";
      return 1;
    }
  }

  std::cout << "========================================\n";
  std::cout << "  ADDRESS CLEANING\n";
  std::cout << "========================================\n\n";
  std::cout << "Input:  " << input_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Steps: " << (standardize ? "standardize " : "")
            << (standardize_streets ? "standardize_streets " : "") << (validate ? "validate" : "")
            << "\n\n";

  std::string line;
  bool is_header = true;
  int col_id = -1, col_num = -1, col_street = -1, col_city = -1, col_postal = -1;
  std::vector<std::string> headers;
  int rows = 0, modified = 0, removed = 0;
  std::map<std::string, int> reasons;

  while (std::getline(in, line)) {
    std::string raw = trim(line);
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF) raw = raw.substr(3);
    if (raw.empty() || raw[0] == '#') continue;

    auto values = splitCSV(raw);

    if (is_header) {
      headers = values;
      for (size_t i = 0; i < headers.size(); ++i) {
        std::string l = toLower(headers[i]);
        if (l == "id") col_id = (int)i;
        else if (l == "house_number" || l == "number" || l == "num") col_num = (int)i;
        else if (l == "street_name" || l == "street" || l == "name") col_street = (int)i;
        else if (l == "city") col_city = (int)i;
        else if (l == "postal_code" || l == "zip" || l == "codigo_postal") col_postal = (int)i;
      }
      if (col_num < 0 || col_street < 0) {
        std::cerr << "Error: input must have house_number and street_name columns\n";
        return 1;
      }
      writeRow(out, headers);
      if (rej.is_open()) writeRow(rej, headers);
      is_header = false;
      continue;
    }

    ++rows;
    values.resize(headers.size());
    std::vector<std::string> original = values;

    auto field = [&](int c) -> std::string& { return values[c]; };

    if (standardize) {
      for (size_t i = 0; i < values.size(); ++i) {
        // Limpieza general de texto (sin tocar geometrias WKT).
        std::string l = toLower(headers[i]);
        if (l == "geometry" || l == "geom" || l == "wkt") continue;
        if ((int)i == col_num || (int)i == col_postal) {
          values[i] = trim(values[i]);
        } else {
          values[i] = addr::cleanText(values[i]);
        }
      }
      int n = parseHouseNumber(field(col_num));
      if (n > 0) field(col_num) = std::to_string(n);
      field(col_street) = addr::titleCase(field(col_street));
      if (col_city >= 0) field(col_city) = addr::titleCase(field(col_city));
      if (col_postal >= 0) {
        std::string p;
        for (char c : field(col_postal))
          if (!std::isspace((unsigned char)c)) p += c;
        field(col_postal) = p;
      }
    }

    if (standardize_streets) {
      field(col_street) = addr::standardizeStreet(field(col_street));
    }

    if (values != original) ++modified;

    if (validate) {
      std::string reason;
      if (trim(field(col_street)).empty()) {
        reason = "missing street_name";
      } else if (parseHouseNumber(field(col_num)) <= 0) {
        reason = "missing or invalid house_number";
      } else if (col_id >= 0 && trim(field(col_id)).empty()) {
        reason = "missing id";
      }
      if (!reason.empty()) {
        ++removed;
        ++reasons[reason];
        if (rej.is_open()) writeRow(rej, original);
        continue;
      }
    }

    writeRow(out, values);
  }

  std::cout << "Rows read:     " << rows << "\n";
  std::cout << "Rows modified: " << modified << "\n";
  if (validate) {
    std::cout << "Rows removed:  " << removed << "\n";
    for (const auto& kv : reasons) std::cout << "  - " << kv.first << ": " << kv.second << "\n";
    if (rej.is_open()) std::cout << "Rejected rows written to: " << rejects_file << "\n";
  }
  std::cout << "Rows written:  " << (rows - removed) << "\n";
  std::cout << "Output written to: " << output_file << "\n";
  return 0;
}
