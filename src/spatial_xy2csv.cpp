// spatial_xy2csv - Convert a plain CSV with coordinate columns (x/y or
// longitude/latitude) into this project's spatial CSV format: the same rows
// and columns plus a WKT "geometry" column with POINT(x y).
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../include/core/spatial_string.hpp"

namespace {

const std::vector<std::string> kLonNames = {"lon", "long", "longitude", "longitud", "lng"};
const std::vector<std::string> kLatNames = {"lat", "latitude", "latitud"};
const std::vector<std::string> kXNames = {"x", "este", "easting", "east"};
const std::vector<std::string> kYNames = {"y", "norte", "northing", "north"};

// RFC 4180 CSV parser: quoted fields, "" escapes and newlines inside quotes.
bool readCSVRecord(std::istream& in, char delim, std::vector<std::string>& fields) {
  fields.clear();
  std::string field;
  bool in_quotes = false;
  bool any = false;
  char ch;
  while (in.get(ch)) {
    any = true;
    if (in_quotes) {
      if (ch == '"') {
        if (in.peek() == '"') {
          in.get(ch);
          field += '"';
        } else {
          in_quotes = false;
        }
      } else {
        field += ch;
      }
    } else if (ch == '"') {
      in_quotes = true;
    } else if (ch == delim) {
      fields.push_back(field);
      field.clear();
    } else if (ch == '\r') {
      // ignore (CRLF line endings)
    } else if (ch == '\n') {
      fields.push_back(field);
      return true;
    } else {
      field += ch;
    }
  }
  if (!any) return false;
  fields.push_back(field);
  return true;
}

std::string quoteField(const std::string& s) {
  bool needs = s.find_first_of(",\"\n\r()") != std::string::npos ||
               (!s.empty() && (s.front() == ' ' || s.back() == ' '));
  if (!needs) return s;
  std::string out = "\"";
  for (char c : s) {
    if (c == '"') out += '"';
    out += c;
  }
  return out + "\"";
}

// Parses a coordinate; accepts a decimal comma. Returns the normalized text.
bool parseCoord(std::string s, double& value, std::string& text) {
  s = trim(s);
  if (s.empty()) return false;
  if (s.find('.') == std::string::npos) std::replace(s.begin(), s.end(), ',', '.');
  try {
    size_t pos = 0;
    value = std::stod(s, &pos);
    if (pos != s.size() || !std::isfinite(value)) return false;
  } catch (...) {
    return false;
  }
  text = s;
  return true;
}

bool inList(const std::string& name, const std::vector<std::string>& list) {
  return std::find(list.begin(), list.end(), toLower(trim(name))) != list.end();
}

int findColumn(const std::vector<std::string>& header, const std::string& name) {
  for (size_t i = 0; i < header.size(); ++i)
    if (toLower(trim(header[i])) == toLower(trim(name))) return static_cast<int>(i);
  return -1;
}

int findColumnIn(const std::vector<std::string>& header, const std::vector<std::string>& names) {
  for (size_t i = 0; i < header.size(); ++i)
    if (inList(header[i], names)) return static_cast<int>(i);
  return -1;
}

void printUsage() {
  std::cerr << "spatial_xy2csv - Convert a CSV with coordinate columns to spatial CSV\n\n";
  std::cerr << "Usage: spatial_xy2csv <input.csv> <output.csv> [options]\n\n";
  std::cerr << "Adds a WKT geometry column POINT(x y) built from two coordinate columns.\n";
  std::cerr << "All original columns and rows are kept.\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -x <col>                 X / longitude column (default: auto-detect\n";
  std::cerr << "                           lon, long, longitude, longitud, lng, x, este)\n";
  std::cerr << "  -y <col>                 Y / latitude column (default: auto-detect\n";
  std::cerr << "                           lat, latitude, latitud, y, norte)\n";
  std::cerr << "  -crs <value>             CRS written to the header (default: EPSG:4326\n";
  std::cerr << "                           when lat/lon columns are used)\n";
  std::cerr << "  -delimiter <char>        Input field separator: ',', ';' or 'tab'\n";
  std::cerr << "                           (default: auto-detect from the header)\n";
  std::cerr << "  -geometry_column <name>  Name of the new column (default: geometry)\n";
  std::cerr << "  -drop_xy                 Remove the coordinate columns from the output\n";
  std::cerr << "  -verbose                 Show every skipped row\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_xy2csv sismos.csv sismos_geo.csv\n";
  std::cerr << "  spatial_xy2csv sismos.csv sismos_geo.csv -x Longitud -y Latitud\n";
  std::cerr << "  spatial_xy2csv puntos.csv puntos_geo.csv -x ESTE -y NORTE -crs EPSG:5367\n";
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printUsage();
    return 1;
  }

  std::string input_file, output_file, x_col, y_col, crs, geom_col = "geometry";
  char delim = 0;
  bool drop_xy = false, verbose = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-x" && i + 1 < argc) {
      x_col = argv[++i];
    } else if (arg == "-y" && i + 1 < argc) {
      y_col = argv[++i];
    } else if (arg == "-crs" && i + 1 < argc) {
      crs = argv[++i];
    } else if (arg == "-delimiter" && i + 1 < argc) {
      std::string d = argv[++i];
      if (toLower(d) == "tab" || d == "\\t") delim = '\t';
      else if (d.size() == 1) delim = d[0];
      else {
        std::cerr << "Error: -delimiter must be a single character or 'tab'\n";
        return 1;
      }
    } else if (arg == "-geometry_column" && i + 1 < argc) {
      geom_col = argv[++i];
    } else if (arg == "-drop_xy") {
      drop_xy = true;
    } else if (arg == "-verbose") {
      verbose = true;
    } else if (arg.size() > 1 && arg[0] == '-') {
      std::cerr << "Error: Unknown option or missing value: " << arg << "\n\n";
      printUsage();
      return 1;
    } else if (input_file.empty()) {
      input_file = arg;
    } else if (output_file.empty()) {
      output_file = arg;
    } else {
      std::cerr << "Error: Unexpected argument: " << arg << "\n";
      return 1;
    }
  }

  if (input_file.empty() || output_file.empty()) {
    std::cerr << "Error: Input and output files required\n";
    return 1;
  }
  if (input_file == output_file) {
    std::cerr << "Error: Output file must be different from input file\n";
    return 1;
  }

  std::ifstream in(input_file, std::ios::binary);
  if (!in.is_open()) {
    std::cerr << "Error: Could not open file: " << input_file << "\n";
    return 1;
  }

  // Skip UTF-8 BOM
  if (in.peek() == 0xEF) {
    char bom[3];
    in.read(bom, 3);
    if (!(static_cast<unsigned char>(bom[1]) == 0xBB && static_cast<unsigned char>(bom[2]) == 0xBF))
      in.seekg(0);
  }

  // Leading comment lines (# ...): keep them, and read an existing CRS
  std::vector<std::string> comments;
  std::string input_crs;
  while (in.peek() == '#') {
    std::string line;
    std::getline(in, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::string body = trim(line.substr(1));
    if (body.rfind("CRS:", 0) == 0) {
      input_crs = trim(body.substr(4));
      size_t comma = input_crs.find(",,");  // spreadsheet-exported padding
      if (comma != std::string::npos) input_crs = trim(input_crs.substr(0, comma));
    } else if (body.rfind("Geometry column:", 0) != 0) {
      comments.push_back(line);
    }
  }

  // Header (auto-detect delimiter from it)
  std::streampos header_pos = in.tellg();
  std::string header_line;
  if (!std::getline(in, header_line)) {
    std::cerr << "Error: Input file is empty\n";
    return 1;
  }
  if (delim == 0) {
    size_t n_comma = std::count(header_line.begin(), header_line.end(), ',');
    size_t n_semi = std::count(header_line.begin(), header_line.end(), ';');
    size_t n_tab = std::count(header_line.begin(), header_line.end(), '\t');
    delim = ',';
    if (n_semi > n_comma && n_semi >= n_tab) delim = ';';
    else if (n_tab > n_comma && n_tab > n_semi) delim = '\t';
  }
  in.clear();
  in.seekg(header_pos);

  std::vector<std::string> header;
  readCSVRecord(in, delim, header);
  for (auto& h : header) h = trim(h);

  if (findColumn(header, geom_col) >= 0 || findColumn(header, "geom") >= 0 ||
      findColumn(header, "wkt") >= 0) {
    std::cerr << "Error: Input already has a geometry column; it is already a spatial CSV\n";
    return 1;
  }

  // Resolve coordinate columns
  int xi = -1, yi = -1;
  bool geographic = false;
  if (!x_col.empty()) {
    xi = findColumn(header, x_col);
    if (xi < 0) {
      std::cerr << "Error: Column not found: " << x_col << "\n";
      return 1;
    }
  }
  if (!y_col.empty()) {
    yi = findColumn(header, y_col);
    if (yi < 0) {
      std::cerr << "Error: Column not found: " << y_col << "\n";
      return 1;
    }
  }
  if (xi < 0) {
    xi = findColumnIn(header, kLonNames);
    if (xi < 0) xi = findColumnIn(header, kXNames);
  }
  if (yi < 0) {
    yi = findColumnIn(header, kLatNames);
    if (yi < 0) yi = findColumnIn(header, kYNames);
  }
  if (xi < 0 || yi < 0) {
    std::cerr << "Error: Could not detect the " << (xi < 0 ? "X/longitude" : "Y/latitude")
              << " column. Use -x and -y. Columns found:";
    for (const auto& h : header) std::cerr << " " << h;
    std::cerr << "\n";
    return 1;
  }
  if (xi == yi) {
    std::cerr << "Error: X and Y must be different columns\n";
    return 1;
  }
  geographic = inList(header[xi], kLonNames) || inList(header[yi], kLatNames);

  if (crs.empty()) crs = !input_crs.empty() ? input_crs : (geographic ? "EPSG:4326" : "");

  std::cout << "Input: " << input_file << "\n";
  std::cout << "Delimiter: " << (delim == '\t' ? std::string("tab") : std::string(1, delim)) << "\n";
  std::cout << "X column: " << header[xi] << "\n";
  std::cout << "Y column: " << header[yi] << "\n";
  std::cout << "CRS: " << (crs.empty() ? "(not set)" : crs) << "\n";

  // Read rows
  std::vector<std::vector<std::string>> rows;
  std::vector<std::string> fields;
  int line_no = 1;
  int skipped = 0, swapped_hint = 0, out_of_range = 0;
  double minx = 1e300, miny = 1e300, maxx = -1e300, maxy = -1e300;

  auto skip = [&](const std::string& why) {
    skipped++;
    if (verbose || skipped <= 10)
      std::cerr << "Warning: row " << line_no << " skipped: " << why << "\n";
  };

  while (readCSVRecord(in, delim, fields)) {
    line_no++;
    bool blank = true;
    for (const auto& f : fields)
      if (!trim(f).empty()) blank = false;
    if (blank) continue;

    if (fields.size() < header.size()) fields.resize(header.size());
    if (fields.size() > header.size()) {
      skip("has " + std::to_string(fields.size()) + " fields, header has " +
           std::to_string(header.size()));
      continue;
    }

    double x, y;
    std::string xs, ys;
    if (!parseCoord(fields[xi], x, xs)) {
      skip("invalid " + header[xi] + " value '" + fields[xi] + "'");
      continue;
    }
    if (!parseCoord(fields[yi], y, ys)) {
      skip("invalid " + header[yi] + " value '" + fields[yi] + "'");
      continue;
    }
    if (geographic && (std::abs(x) > 180 || std::abs(y) > 90)) {
      out_of_range++;
      if (std::abs(y) <= 180 && std::abs(x) <= 90) swapped_hint++;
      skip("coordinates out of range (lon=" + xs + ", lat=" + ys + ")");
      continue;
    }

    minx = std::min(minx, x); maxx = std::max(maxx, x);
    miny = std::min(miny, y); maxy = std::max(maxy, y);

    fields[xi] = xs;  // normalized (decimal point) so other tools read them as numbers
    fields[yi] = ys;
    fields.push_back("POINT(" + xs + " " + ys + ")");
    rows.push_back(fields);
  }
  if (skipped > 10 && !verbose)
    std::cerr << "Warning: " << (skipped - 10) << " more rows skipped (use -verbose to list)\n";

  if (rows.empty()) {
    std::cerr << "Error: No valid rows with coordinates\n";
    if (swapped_hint > 0) std::cerr << "Hint: latitude and longitude seem swapped; check -x/-y\n";
    return 1;
  }
  if (swapped_hint > 0)
    std::cerr << "Hint: " << swapped_hint
              << " rows look like latitude and longitude are swapped; check -x/-y\n";

  // Write spatial CSV
  std::ofstream out(output_file);
  if (!out.is_open()) {
    std::cerr << "Error: Could not write file: " << output_file << "\n";
    return 1;
  }
  if (!crs.empty()) out << "# CRS: " << crs << "\n";
  out << "# Geometry column: " << geom_col << "\n";
  out << "# Converted from " << input_file << " (X=" << header[xi] << ", Y=" << header[yi]
      << ")\n";
  for (const auto& c : comments) out << c << "\n";

  auto keep = [&](size_t i) { return !(drop_xy && (static_cast<int>(i) == xi || static_cast<int>(i) == yi)); };

  bool first = true;
  for (size_t i = 0; i < header.size(); ++i) {
    if (!keep(i)) continue;
    out << (first ? "" : ",") << quoteField(header[i]);
    first = false;
  }
  out << (first ? "" : ",") << quoteField(geom_col) << "\n";

  for (const auto& row : rows) {
    first = true;
    for (size_t i = 0; i < header.size(); ++i) {
      if (!keep(i)) continue;
      out << (first ? "" : ",") << quoteField(trim(row[i]));
      first = false;
    }
    out << (first ? "" : ",") << row.back() << "\n";
  }
  out.close();

  std::cout << "\n========================================\n";
  std::cout << "  CONVERSION COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Points written: " << rows.size() << "\n";
  if (skipped) std::cout << "Rows skipped: " << skipped << "\n";
  std::cout << "Extent: " << minx << " " << miny << " " << maxx << " " << maxy << "\n";
  std::cout << "Output written to: " << output_file << "\n";
  return 0;
}
