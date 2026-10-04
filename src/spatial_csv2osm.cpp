#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

#include "../include/core/spatial_csv.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"

struct OsmNode {
  long long id;
  double lat;
  double lon;
  std::vector<std::pair<std::string, std::string>> tags;
};

struct OsmWay {
  long long id;
  std::vector<long long> node_ids;
  std::vector<std::pair<std::string, std::string>> tags;
};

std::string escapeXML(const std::string& value) {
  std::string result;
  result.reserve(value.size());
  for (char c : value) {
    switch (c) {
      case '&':
        result += "&amp;";
        break;
      case '<':
        result += "&lt;";
        break;
      case '>':
        result += "&gt;";
        break;
      case '"':
        result += "&quot;";
        break;
      case '\'':
        result += "&apos;";
        break;
      default:
        result += c;
    }
  }
  return result;
}

bool isValidLat(double lat) {
  return lat >= -90.0 && lat <= 90.0;
}

bool isValidLon(double lon) {
  return lon >= -180.0 && lon <= 180.0;
}

std::vector<std::pair<std::string, std::string>> buildTags(
    const std::unordered_map<std::string, std::string>& attributes,
    const std::vector<std::string>& tags_filter, const std::vector<std::string>& exclude_filter) {
  std::vector<std::pair<std::string, std::string>> keys;
  for (const auto& [key, value] : attributes) {
    if (value.empty())
      continue;

    if (!tags_filter.empty()) {
      bool found = false;
      for (const auto& t : tags_filter) {
        if (toLower(t) == toLower(key)) {
          found = true;
          break;
        }
      }
      if (!found)
        continue;
    }

    bool excluded = false;
    for (const auto& e : exclude_filter) {
      if (toLower(e) == toLower(key)) {
        excluded = true;
        break;
      }
    }
    if (excluded)
      continue;

    keys.push_back({key, value});
  }

  std::sort(keys.begin(), keys.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  return keys;
}

void writeTags(std::ofstream& file, const std::vector<std::pair<std::string, std::string>>& tags,
               const std::string& indent) {
  for (const auto& [key, value] : tags) {
    file << indent << "<tag k=\"" << escapeXML(key) << "\" v=\"" << escapeXML(value) << "\"/>\n";
  }
}

void printUsage() {
  std::cerr << "spatial_csv2osm - Convert spatial CSV to OpenStreetMap XML (.osm)\n\n";
  std::cerr << "Usage: spatial_csv2osm <input.csv> <output.osm> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -tags <list>       Only include these attribute columns as tags "
               "(comma-separated)\n";
  std::cerr << "  -exclude <list>    Exclude these attribute columns from tags "
               "(comma-separated)\n";
  std::cerr << "  -precision <n>     Decimal digits for lat/lon (default: 7)\n";
  std::cerr << "  -generator <name>  Value of the generator attribute (default: spatial_csv2osm)\n";
  std::cerr << "  -no-area-tag       Do not add area=yes to ways created from POLYGON features\n";
  std::cerr << "  -force             Keep features with out-of-range coordinates instead of "
               "skipping them\n";
  std::cerr << "  -osmchange         Write an osmChange file (<osmChange><create>...) instead of\n";
  std::cerr << "                     a plain .osm file, for uploading straight to the OSM API\n";
  std::cerr << "                     without going through an editor\n";
  std::cerr << "  -changeset <id>    Changeset id to stamp on every element in -osmchange mode\n";
  std::cerr << "                     (default: CHANGESET_ID, a placeholder you must replace)\n";
  std::cerr << "  -verbose           Show detailed information\n\n";
  std::cerr << "Notes:\n";
  std::cerr << "  Coordinates are read as-is and written as lon/lat (WGS84, EPSG:4326). The\n";
  std::cerr << "  input CSV must already contain geographic coordinates in degrees; this tool\n";
  std::cerr << "  does not reproject data from a projected CRS.\n";
  std::cerr << "  Every node/way gets a new negative id. In the default .osm mode the file is\n";
  std::cerr << "  meant to be opened in an editor such as JOSM and reviewed/uploaded from\n";
  std::cerr << "  there -- it is not merged against existing OSM data. In -osmchange mode the\n";
  std::cerr << "  file is meant to be POSTed directly to PUT/POST\n";
  std::cerr << "  /api/0.6/changeset/{id}/upload after opening a changeset yourself -- there is\n";
  std::cerr << "  still no conflict detection against existing OSM data, so only use it for\n";
  std::cerr << "  features you are sure do not already exist on the map.\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_csv2osm places.csv places.osm\n";
  std::cerr << "  spatial_csv2osm places.csv places.osm -tags name,amenity\n";
  std::cerr << "  spatial_csv2osm parcels.csv parcels.osm -exclude internal_id -precision 6\n";
  std::cerr << "  spatial_csv2osm places.csv places.osc -osmchange -changeset 123456\n";
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printUsage();
    return 1;
  }

  std::string input_file = argv[1];
  std::string output_file = argv[2];
  std::vector<std::string> tags_filter;
  std::vector<std::string> exclude_filter;
  int precision = 7;
  std::string generator = "spatial_csv2osm";
  bool area_tag = true;
  bool force = false;
  bool verbose = false;
  bool osmchange = false;
  std::string changeset_id = "CHANGESET_ID";

  for (int i = 3; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-tags" && i + 1 < argc) {
      for (const auto& t : split(std::string(argv[++i]), ',')) {
        std::string tag = trim(t);
        if (!tag.empty())
          tags_filter.push_back(tag);
      }
    } else if (arg == "-exclude" && i + 1 < argc) {
      for (const auto& t : split(std::string(argv[++i]), ',')) {
        std::string tag = trim(t);
        if (!tag.empty())
          exclude_filter.push_back(tag);
      }
    } else if (arg == "-precision" && i + 1 < argc) {
      precision = std::stoi(argv[++i]);
    } else if (arg == "-generator" && i + 1 < argc) {
      generator = argv[++i];
    } else if (arg == "-no-area-tag") {
      area_tag = false;
    } else if (arg == "-force") {
      force = true;
    } else if (arg == "-osmchange") {
      osmchange = true;
    } else if (arg == "-changeset" && i + 1 < argc) {
      changeset_id = argv[++i];
    } else if (arg == "-verbose") {
      verbose = true;
    }
  }

  Spatial::SpatialCSVReader reader;
  Spatial::VectorDataset dataset;

  if (!reader.read(input_file, dataset)) {
    std::cerr << "Error: Could not read input file\n";
    return 1;
  }

  std::cout << "========================================\n";
  std::cout << "  CSV TO OSM CONVERTER\n";
  std::cout << "========================================\n\n";
  std::cout << "Input: " << input_file << "\n";
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Features read: " << dataset.features.size() << "\n";
  std::cout << "Format: " << (osmchange ? "osmChange" : ".osm") << "\n";
  std::cout << "Precision: " << precision << " decimals\n";
  if (osmchange) {
    std::cout << "Changeset id: " << changeset_id << "\n";
    if (changeset_id == "CHANGESET_ID") {
      std::cerr << "Warning: -changeset not set, writing placeholder \"CHANGESET_ID\" -- "
                   "replace it with a real open changeset id before uploading.\n";
    }
  }
  if (!tags_filter.empty()) {
    std::cout << "Tags included: ";
    for (const auto& t : tags_filter) std::cout << t << " ";
    std::cout << "\n";
  }
  if (!exclude_filter.empty()) {
    std::cout << "Tags excluded: ";
    for (const auto& t : exclude_filter) std::cout << t << " ";
    std::cout << "\n";
  }
  std::cout << "\n";

  std::vector<OsmNode> nodes;
  std::vector<OsmWay> ways;
  long long next_id = -1;
  int points_written = 0;
  int lines_written = 0;
  int polygons_written = 0;
  int skipped = 0;
  int skipped_multi = 0;

  for (const auto& feature : dataset.features) {
    auto tags = buildTags(feature.attributes, tags_filter, exclude_filter);

    if (feature.type == Spatial::VectorFeature::GeometryType::MULTIPOINT ||
        feature.type == Spatial::VectorFeature::GeometryType::MULTILINESTRING ||
        feature.type == Spatial::VectorFeature::GeometryType::MULTIPOLYGON) {

      std::cerr << "Skipping feature with multi-part geometry (MultiPoint/MultiLineString/"
                   "MultiPolygon) -- OSM XML has no node/way equivalent for it without a "
                   "hand-built relation, which this tool does not create.\n";
      skipped++;
      skipped_multi++;
      continue;
    }

    if (feature.type == Spatial::VectorFeature::GeometryType::POINT) {
      if (feature.coordinates.size() < 2) {
        skipped++;
        continue;
      }
      double lon = feature.coordinates[0];
      double lat = feature.coordinates[1];
      if (!force && (!isValidLat(lat) || !isValidLon(lon))) {
        if (verbose)
          std::cerr << "Skipping POINT with out-of-range coordinates (lon=" << lon
                    << ", lat=" << lat << ")\n";
        skipped++;
        continue;
      }

      OsmNode node;
      node.id = next_id--;
      node.lat = lat;
      node.lon = lon;
      node.tags = tags;
      nodes.push_back(node);
      points_written++;
      continue;
    }

    bool is_polygon = feature.type == Spatial::VectorFeature::GeometryType::POLYGON;
    size_t vertex_count = feature.coordinates.size() / 2;
    if (vertex_count < 2) {
      skipped++;
      continue;
    }

    bool out_of_range = false;
    for (size_t i = 0; i < feature.coordinates.size(); i += 2) {
      double lon = feature.coordinates[i];
      double lat = feature.coordinates[i + 1];
      if (!isValidLat(lat) || !isValidLon(lon)) {
        out_of_range = true;
        break;
      }
    }
    if (!force && out_of_range) {
      if (verbose)
        std::cerr << "Skipping " << (is_polygon ? "POLYGON" : "LINESTRING")
                  << " with out-of-range coordinates\n";
      skipped++;
      continue;
    }

    OsmWay way;
    way.id = next_id--;
    way.tags = tags;
    if (is_polygon && area_tag) {
      way.tags.push_back({"area", "yes"});
    }

    double first_lon = feature.coordinates[0];
    double first_lat = feature.coordinates[1];
    long long first_node_id = 0;

    for (size_t i = 0; i < feature.coordinates.size(); i += 2) {
      double lon = feature.coordinates[i];
      double lat = feature.coordinates[i + 1];

      bool is_closing_point = is_polygon && i > 0 && std::abs(lon - first_lon) < 1e-9 &&
                              std::abs(lat - first_lat) < 1e-9 &&
                              i == feature.coordinates.size() - 2;
      if (is_closing_point) {
        way.node_ids.push_back(first_node_id);
        continue;
      }

      OsmNode node;
      node.id = next_id--;
      node.lat = lat;
      node.lon = lon;
      nodes.push_back(node);
      way.node_ids.push_back(node.id);
      if (i == 0)
        first_node_id = node.id;
    }

    if (is_polygon && way.node_ids.front() != way.node_ids.back()) {
      way.node_ids.push_back(first_node_id);
    }

    ways.push_back(way);
    if (is_polygon)
      polygons_written++;
    else
      lines_written++;
  }

  std::ofstream file(output_file);
  if (!file.is_open()) {
    std::cerr << "Error: Could not create output file\n";
    return 1;
  }

  file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";

  std::string node_indent = "  ";
  std::string way_indent = "  ";
  std::string tag_indent = "    ";
  std::string changeset_attr;

  if (osmchange) {
    file << "<osmChange version=\"0.6\" generator=\"" << escapeXML(generator) << "\">\n";
    file << "  <create>\n";
    node_indent = "    ";
    way_indent = "    ";
    tag_indent = "      ";
    changeset_attr = " changeset=\"" + escapeXML(changeset_id) + "\"";
  } else {
    file << "<osm version=\"0.6\" generator=\"" << escapeXML(generator) << "\">\n";
  }

  for (const auto& node : nodes) {
    file << node_indent << "<node id=\"" << node.id << "\"" << changeset_attr;
    if (!osmchange)
      file << " visible=\"true\"";
    file << " lat=\"" << std::fixed << std::setprecision(precision) << node.lat << "\" lon=\""
         << std::fixed << std::setprecision(precision) << node.lon << "\"";
    if (node.tags.empty()) {
      file << "/>\n";
    } else {
      file << ">\n";
      writeTags(file, node.tags, tag_indent);
      file << node_indent << "</node>\n";
    }
  }

  for (const auto& way : ways) {
    file << way_indent << "<way id=\"" << way.id << "\"" << changeset_attr;
    if (!osmchange)
      file << " visible=\"true\"";
    file << ">\n";
    for (long long ref : way.node_ids) {
      file << tag_indent << "<nd ref=\"" << ref << "\"/>\n";
    }
    writeTags(file, way.tags, tag_indent);
    file << way_indent << "</way>\n";
  }

  if (osmchange) {
    file << "  </create>\n";
    file << "</osmChange>\n";
  } else {
    file << "</osm>\n";
  }
  file.close();

  std::cout << "========================================\n";
  std::cout << "  CONVERSION COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Nodes written: " << nodes.size() << "\n";
  std::cout << "Ways written: " << ways.size() << "\n";
  std::cout << "  Points: " << points_written << "\n";
  std::cout << "  Lines: " << lines_written << "\n";
  std::cout << "  Polygons: " << polygons_written << "\n";
  if (skipped > 0) {
    std::cout << "Skipped (invalid geometry, out-of-range coordinates, or unsupported multi-part "
                  "geometry): "
              << skipped << "\n";
    if (skipped_multi > 0)
      std::cout << "  Of which multi-part geometries (no OSM equivalent): " << skipped_multi
                << "\n";
    if (!force && skipped_multi < skipped)
      std::cout << "Use -force to include out-of-range coordinates anyway.\n";
  }
  std::cout << "Output written to: " << output_file << "\n";

  return 0;
}

