#define PUGIXML_HEADER_ONLY
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <vector>

#include "../include/core/spatial_csv.hpp"
#include "../include/core/spatial_string.hpp"
#include "../include/core/spatial_types.hpp"
#include "../include/pugixml.hpp"

struct OSMNode {
  long long id;
  double lat;
  double lon;
  std::map<std::string, std::string> tags;
};

struct OSMWay {
  long long id;
  std::vector<long long> node_refs;
  std::map<std::string, std::string> tags;
  bool is_closed;
};

struct OSMRelation {
  long long id;
  std::vector<std::pair<std::string, long long>> members;
  std::map<std::string, std::string> tags;
};

struct OSMData {
  std::map<long long, OSMNode> nodes;
  std::map<long long, OSMWay> ways;
  std::map<long long, OSMRelation> relations;
};

std::string escapeCSV(const std::string& value) {
  if (value.find(',') != std::string::npos || value.find('"') != std::string::npos ||
      value.find('\n') != std::string::npos) {
    std::string escaped = value;
    size_t pos = 0;
    while ((pos = escaped.find('"', pos)) != std::string::npos) {
      escaped.replace(pos, 1, "\"\"");
      pos += 2;
    }
    return "\"" + escaped + "\"";
  }
  return value;
}

std::string uniqueReservedName(const std::string& desired, const std::set<std::string>& taken) {
  if (taken.find(desired) == taken.end()) {
    return desired;
  }
  std::string candidate = "osm_" + desired;
  while (taken.find(candidate) != taken.end()) {
    candidate = "_" + candidate;
  }
  std::cerr << "Warning: tag \"" << desired << "\" collides with a reserved column; using \""
            << candidate << "\" for the structural column so the tag value is not lost.\n";
  return candidate;
}

struct TagFilter {
  std::string key;
  std::string value;
  bool pattern;

  TagFilter() : pattern(false) {}
  TagFilter(const std::string& k, const std::string& v = "") : key(k), value(v), pattern(false) {}

  bool matches(const std::map<std::string, std::string>& tags) const {
    auto it = tags.find(key);
    if (it == tags.end())
      return false;
    if (value.empty())
      return true;
    if (pattern) {
      std::regex regex_pattern(value);
      return std::regex_search(it->second, regex_pattern);
    }
    return it->second == value;
  }
};

std::vector<TagFilter> parseFilters(const std::string& filter_str) {
  std::vector<TagFilter> filters;
  auto parts = split(filter_str, ',');

  for (const auto& part : parts) {
    std::string p = trim(part);
    size_t eq_pos = p.find('=');
    if (eq_pos != std::string::npos) {
      std::string key = trim(p.substr(0, eq_pos));
      std::string value = trim(p.substr(eq_pos + 1));

      if (value == "*") {
        TagFilter f;
        f.key = key;
        f.value = "";
        f.pattern = false;
        filters.push_back(f);
      } else if (value.find('*') != std::string::npos) {
        std::string regex_str = value;
        regex_str = std::regex_replace(regex_str, std::regex("\\*"), ".*");
        TagFilter f;
        f.key = key;
        f.value = regex_str;
        f.pattern = true;
        filters.push_back(f);
      } else {
        filters.push_back(TagFilter(key, value));
      }
    } else {
      filters.push_back(TagFilter(p));
    }
  }

  return filters;
}

bool matchesFilters(const std::map<std::string, std::string>& tags,
                    const std::vector<TagFilter>& filters) {
  if (filters.empty())
    return true;

  for (const auto& filter : filters) {
    if (filter.matches(tags))
      return true;
  }
  return false;
}

bool checkOSMFile(const std::string& filename) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file: " << filename << "\n";
    return false;
  }

  std::string line;
  int line_count = 0;
  bool has_osm_tag = false;

  while (std::getline(file, line) && line_count < 10) {
    line_count++;
    if (line.find("<osm") != std::string::npos) {
      has_osm_tag = true;
      break;
    }
    if (line.find("OSM Header") != std::string::npos ||
        line.find("Protocolbuffer") != std::string::npos) {
      std::cerr << "Error: File appears to be OSM PBF format (binary).\n";
      return false;
    }
  }

  if (!has_osm_tag) {
    std::cerr << "Error: File does not contain OSM XML data.\n";
    return false;
  }

  return true;
}

OSMData parseOSM(const std::string& filename) {
  OSMData data;

  if (!checkOSMFile(filename)) {
    return data;
  }

  pugi::xml_document doc;
  pugi::xml_parse_result result = doc.load_file(filename.c_str());

  if (!result) {
    std::cerr << "Error parsing OSM file: " << result.description() << "\n";
    return data;
  }

  pugi::xml_node osm = doc.child("osm");
  if (!osm) {
    std::cerr << "Error: Root element <osm> not found\n";
    return data;
  }

  for (pugi::xml_node node = osm.child("node"); node; node = node.next_sibling("node")) {
    OSMNode n;
    n.id = node.attribute("id").as_llong();
    n.lat = node.attribute("lat").as_double();
    n.lon = node.attribute("lon").as_double();

    for (pugi::xml_node tag = node.child("tag"); tag; tag = tag.next_sibling("tag")) {
      std::string key = tag.attribute("k").as_string();
      std::string value = tag.attribute("v").as_string();
      n.tags[key] = value;
    }

    data.nodes[n.id] = n;
  }

  for (pugi::xml_node way = osm.child("way"); way; way = way.next_sibling("way")) {
    OSMWay w;
    w.id = way.attribute("id").as_llong();

    for (pugi::xml_node nd = way.child("nd"); nd; nd = nd.next_sibling("nd")) {
      w.node_refs.push_back(nd.attribute("ref").as_llong());
    }

    for (pugi::xml_node tag = way.child("tag"); tag; tag = tag.next_sibling("tag")) {
      std::string key = tag.attribute("k").as_string();
      std::string value = tag.attribute("v").as_string();
      w.tags[key] = value;
    }

    if (w.node_refs.size() >= 3) {
      w.is_closed = (w.node_refs.front() == w.node_refs.back());
    } else {
      w.is_closed = false;
    }

    data.ways[w.id] = w;
  }

  for (pugi::xml_node rel = osm.child("relation"); rel; rel = rel.next_sibling("relation")) {
    OSMRelation r;
    r.id = rel.attribute("id").as_llong();

    for (pugi::xml_node member = rel.child("member"); member;
         member = member.next_sibling("member")) {
      std::string type = member.attribute("type").as_string();
      long long ref = member.attribute("ref").as_llong();

      r.members.push_back({type, ref});
    }

    for (pugi::xml_node tag = rel.child("tag"); tag; tag = tag.next_sibling("tag")) {
      std::string key = tag.attribute("k").as_string();
      std::string value = tag.attribute("v").as_string();
      r.tags[key] = value;
    }

    data.relations[r.id] = r;
  }

  return data;
}

std::string nodesToWKT(const std::vector<long long>& node_refs,
                       const std::map<long long, OSMNode>& nodes, bool closed = false) {
  std::string wkt;

  for (size_t i = 0; i < node_refs.size(); ++i) {
    auto it = nodes.find(node_refs[i]);
    if (it == nodes.end())
      continue;

    if (i > 0)
      wkt += ", ";
    wkt += std::to_string(it->second.lon) + " " + std::to_string(it->second.lat);
  }

  if (closed && !node_refs.empty()) {
    auto first = nodes.find(node_refs.front());
    auto last = nodes.find(node_refs.back());
    if (first != nodes.end() && last != nodes.end()) {
      if (first->second.lon != last->second.lon || first->second.lat != last->second.lat) {
        wkt += ", " + std::to_string(first->second.lon) + " " + std::to_string(first->second.lat);
      }
    }
  }

  return wkt;
}

std::string wayToWKT(const OSMWay& way, const std::map<long long, OSMNode>& nodes) {
  if (way.node_refs.size() < 2)
    return "";

  std::string coords = nodesToWKT(way.node_refs, nodes, way.is_closed);
  if (coords.empty())
    return "";

  if (way.is_closed && way.node_refs.size() >= 4) {
    return "POLYGON((" + coords + "))";
  } else {
    return "LINESTRING(" + coords + ")";
  }
}

std::string nodeToWKT(const OSMNode& node) {
  return "POINT(" + std::to_string(node.lon) + " " + std::to_string(node.lat) + ")";
}

struct LayerFeature {
  long long id;
  std::map<std::string, std::string> tags;
  std::string wkt_geometry;
  std::string geometry_type;
};

struct LayerInfo {
  std::string name;
  std::string geometry_type;
  std::vector<LayerFeature> features;
};

std::vector<LayerInfo> detectLayers(const OSMData& data, const std::string& split_mode,
                                    const std::vector<TagFilter>& filters,
                                    const std::set<std::string>& types) {
  std::vector<LayerInfo> layers;
  std::map<std::string, LayerInfo> layer_map;

  auto add_to_layer = [&](const std::string& layer_name, long long id,
                          const std::map<std::string, std::string>& tags,
                          const std::string& geom_type, const std::string& wkt) {
    if (layer_map.find(layer_name) == layer_map.end()) {
      LayerInfo info;
      info.name = layer_name;
      info.geometry_type = geom_type;
      layer_map[layer_name] = info;
    }
    LayerFeature feature;
    feature.id = id;
    feature.tags = tags;
    feature.wkt_geometry = wkt;
    feature.geometry_type = geom_type;
    layer_map[layer_name].features.push_back(feature);
  };

  if (split_mode == "geometry") {
    for (const auto& [id, node] : data.nodes) {
      if (!types.empty() && types.find("node") == types.end())
        continue;
      if (!filters.empty() && !matchesFilters(node.tags, filters))
        continue;
      std::string wkt = nodeToWKT(node);
      add_to_layer("nodes", id, node.tags, "node", wkt);
    }

    for (const auto& [id, way] : data.ways) {
      if (way.node_refs.size() < 2)
        continue;
      if (!types.empty()) {
        std::string geom_type = way.is_closed ? "polygon" : "line";
        if (types.find(geom_type) == types.end() && types.find("way") == types.end())
          continue;
      }
      if (!filters.empty() && !matchesFilters(way.tags, filters))
        continue;
      std::string wkt = wayToWKT(way, data.nodes);
      if (wkt.empty())
        continue;
      std::string type = way.is_closed ? "polygon" : "line";
      add_to_layer(type == "polygon" ? "polygons" : "lines", id, way.tags, type, wkt);
    }

  } else if (split_mode.find("tag:") == 0) {
    std::string tag_key = split_mode.substr(4);

    std::set<std::string> values;
    for (const auto& [id, way] : data.ways) {
      auto it = way.tags.find(tag_key);
      if (it != way.tags.end()) {
        values.insert(it->second);
      }
    }
    for (const auto& [id, node] : data.nodes) {
      auto it = node.tags.find(tag_key);
      if (it != node.tags.end()) {
        values.insert(it->second);
      }
    }

    for (const auto& value : values) {
      std::string layer_name = tag_key + "_" + value;
      std::replace(layer_name.begin(), layer_name.end(), '/', '_');
      std::replace(layer_name.begin(), layer_name.end(), ' ', '_');

      TagFilter filter(tag_key, value);

      for (const auto& [id, way] : data.ways) {
        if (way.node_refs.size() < 2)
          continue;
        if (!types.empty()) {
          std::string geom_type = way.is_closed ? "polygon" : "line";
          if (types.find(geom_type) == types.end() && types.find("way") == types.end())
            continue;
        }
        if (!filters.empty() && !matchesFilters(way.tags, filters))
          continue;
        if (filter.matches(way.tags)) {
          std::string wkt = wayToWKT(way, data.nodes);
          if (wkt.empty())
            continue;
          std::string type = way.is_closed ? "polygon" : "line";
          add_to_layer(layer_name, id, way.tags, type, wkt);
        }
      }

      for (const auto& [id, node] : data.nodes) {
        if (!types.empty() && types.find("node") == types.end())
          continue;
        if (!filters.empty() && !matchesFilters(node.tags, filters))
          continue;
        if (filter.matches(node.tags)) {
          std::string wkt = nodeToWKT(node);
          add_to_layer(layer_name, id, node.tags, "node", wkt);
        }
      }
    }

  } else {
    std::set<std::string> main_tags;
    for (const auto& [id, way] : data.ways) {
      for (const auto& [key, value] : way.tags) {
        if (key != "name" && key != "ref" && key != "source") {
          main_tags.insert(key);
        }
      }
    }
    for (const auto& [id, node] : data.nodes) {
      for (const auto& [key, value] : node.tags) {
        if (key != "name" && key != "ref" && key != "source") {
          main_tags.insert(key);
        }
      }
    }

    for (const auto& tag : main_tags) {
      std::string layer_name = tag;
      TagFilter filter(tag);

      for (const auto& [id, way] : data.ways) {
        if (way.node_refs.size() < 2)
          continue;
        if (!types.empty()) {
          std::string geom_type = way.is_closed ? "polygon" : "line";
          if (types.find(geom_type) == types.end() && types.find("way") == types.end())
            continue;
        }
        if (!filters.empty() && !matchesFilters(way.tags, filters))
          continue;
        if (filter.matches(way.tags)) {
          std::string wkt = wayToWKT(way, data.nodes);
          if (wkt.empty())
            continue;
          std::string type = way.is_closed ? "polygon" : "line";
          add_to_layer(layer_name, id, way.tags, type, wkt);
        }
      }

      for (const auto& [id, node] : data.nodes) {
        if (!types.empty() && types.find("node") == types.end())
          continue;
        if (!filters.empty() && !matchesFilters(node.tags, filters))
          continue;
        if (filter.matches(node.tags)) {
          std::string wkt = nodeToWKT(node);
          add_to_layer(layer_name, id, node.tags, "node", wkt);
        }
      }
    }
  }

  for (auto& [name, info] : layer_map) {
    if (!info.features.empty()) {
      layers.push_back(info);
    }
  }

  return layers;
}

void writeLayerCSV(const LayerInfo& layer, const std::string& output_dir,
                   const std::string& base_name, const std::vector<std::string>& columns) {
  std::string filename = output_dir + "/" + base_name + "_" + layer.name + ".csv";
  std::ofstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not create file: " << filename << "\n";
    return;
  }

  std::set<std::string> tag_keys;
  for (const auto& feature : layer.features) {
    for (const auto& [k, v] : feature.tags) {
      if (!v.empty())
        tag_keys.insert(k);
    }
  }

  std::string id_col = uniqueReservedName("id", tag_keys);
  std::string type_col = uniqueReservedName("type", tag_keys);
  std::string geom_col = uniqueReservedName("geometry", tag_keys);

  std::vector<std::string> final_columns;
  if (columns.empty()) {
    final_columns.push_back(id_col);
    final_columns.push_back(type_col);
    for (const auto& key : tag_keys) {
      final_columns.push_back(key);
    }
    final_columns.push_back(geom_col);
  } else {
    final_columns = columns;
    bool has_id = false, has_type = false, has_geom = false;
    for (const auto& col : final_columns) {
      if (col == id_col)
        has_id = true;
      if (col == type_col)
        has_type = true;
      if (col == geom_col)
        has_geom = true;
    }
    if (!has_id)
      final_columns.insert(final_columns.begin(), id_col);
    if (!has_type)
      final_columns.insert(final_columns.begin() + 1, type_col);
    if (!has_geom)
      final_columns.push_back(geom_col);
  }

  file << "# Layer: " << layer.name << "\n";
  file << "# Geometry type: " << layer.geometry_type << "\n";
  file << "# Features: " << layer.features.size() << "\n";
  for (size_t i = 0; i < final_columns.size(); ++i) {
    file << final_columns[i];
    if (i < final_columns.size() - 1)
      file << ",";
  }
  file << "\n";

  for (const auto& feature : layer.features) {
    for (size_t i = 0; i < final_columns.size(); ++i) {
      const std::string& col = final_columns[i];
      if (col == id_col) {
        file << feature.id;
      } else if (col == type_col) {
        file << feature.geometry_type;
      } else if (col == geom_col) {
        file << escapeCSV(feature.wkt_geometry);
      } else {
        auto it = feature.tags.find(col);
        if (it != feature.tags.end()) {
          file << escapeCSV(it->second);
        }
      }
      if (i < final_columns.size() - 1)
        file << ",";
    }
    file << "\n";
  }

  std::cout << "  Layer '" << layer.name << "': " << layer.features.size() << " features -> "
            << filename << "\n";
}

void writeCSVRow(std::ofstream& file, long long id, const std::string& type,
                 const std::map<std::string, std::string>& tags,
                 const std::vector<std::string>& columns, const std::string& wkt,
                 const std::string& id_col, const std::string& type_col,
                 const std::string& geom_col) {
  for (size_t i = 0; i < columns.size(); ++i) {
    const std::string& col = columns[i];

    if (col == id_col) {
      file << id;
    } else if (col == type_col) {
      file << type;
    } else if (col == geom_col) {
      file << escapeCSV(wkt);
    } else {
      auto it = tags.find(col);
      if (it != tags.end()) {
        file << escapeCSV(it->second);
      }
    }

    if (i < columns.size() - 1)
      file << ",";
  }
  file << "\n";
}

void printUsage() {
  std::cerr << "spatial_osm2csv - Convert OSM XML to spatial CSV\n\n";
  std::cerr << "Usage: spatial_osm2csv <input.osm> <output> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -filter <filters>    Filter by tags (comma-separated):\n";
  std::cerr << "                       key=value, key=*, key=pattern*\n";
  std::cerr << "  -types <types>       Filter by geometry type:\n";
  std::cerr << "                       node, line, polygon, way, relation\n";
  std::cerr << "  -columns <cols>      Only include specific columns\n";
  std::cerr << "  -split <mode>        Split into layers:\n";
  std::cerr
      << "                       geometry  - split by geometry type (nodes, lines, polygons)\n";
  std::cerr << "                       tag       - split by main tags (highway, building, etc.)\n";
  std::cerr << "                       tag:key   - split by values of specific tag\n";
  std::cerr << "  -output_dir <dir>    Output directory for split layers (default: .)\n";
  std::cerr << "  -verbose             Show detailed information\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  # Single file output\n";
  std::cerr << "  spatial_osm2csv map.osm map.csv\n\n";
  std::cerr << "  # Split by geometry\n";
  std::cerr << "  spatial_osm2csv map.osm layer -split geometry\n\n";
  std::cerr << "  # Split by tag\n";
  std::cerr << "  spatial_osm2csv map.osm layer -split tag\n\n";
  std::cerr << "  # Split by highway values\n";
  std::cerr << "  spatial_osm2csv map.osm layer -split tag:highway\n";
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printUsage();
    return 1;
  }

  std::string input_file = argv[1];
  std::string output_base = argv[2];
  std::vector<TagFilter> filters;
  std::set<std::string> types;
  std::vector<std::string> columns;
  std::string split_mode;
  std::string output_dir = ".";
  bool verbose = false;

  for (int i = 3; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-filter" && i + 1 < argc) {
      filters = parseFilters(argv[++i]);
    } else if (arg == "-types" && i + 1 < argc) {
      std::string types_str = argv[++i];
      auto tokens = split(types_str, ',');
      for (const auto& t : tokens) {
        std::string type = toLower(trim(t));
        types.insert(type);
      }
    } else if (arg == "-columns" && i + 1 < argc) {
      std::string cols_str = argv[++i];
      auto tokens = split(cols_str, ',');
      for (const auto& t : tokens) {
        std::string col = trim(t);
        if (!col.empty())
          columns.push_back(col);
      }
    } else if (arg == "-split" && i + 1 < argc) {
      split_mode = argv[++i];
    } else if (arg == "-output_dir" && i + 1 < argc) {
      output_dir = argv[++i];
    } else if (arg == "-verbose") {
      verbose = true;
    }
  }

  if (!split_mode.empty()) {
    if (!std::filesystem::exists(output_dir)) {
      std::filesystem::create_directories(output_dir);
      std::cout << "Created directory: " << output_dir << "\n";
    }
  }

  std::cout << "========================================\n";
  std::cout << "  OSM TO CSV CONVERTER\n";
  std::cout << "========================================\n\n";
  std::cout << "Input: " << input_file << "\n";
  std::cout << "Output: " << output_base << "\n";
  if (!split_mode.empty()) {
    std::cout << "Split mode: " << split_mode << "\n";
    std::cout << "Output dir: " << output_dir << "\n";
  }
  if (!filters.empty()) {
    std::cout << "Filters: ";
    for (const auto& f : filters) {
      std::cout << f.key;
      if (!f.value.empty())
        std::cout << "=" << f.value;
      std::cout << " ";
    }
    std::cout << "\n";
  }
  if (!types.empty()) {
    std::cout << "Types: ";
    for (const auto& t : types) std::cout << t << " ";
    std::cout << "\n";
  }
  std::cout << "\n";

  if (!std::filesystem::exists(input_file)) {
    std::cerr << "Error: File not found: " << input_file << "\n";
    return 1;
  }

  OSMData data = parseOSM(input_file);

  if (data.nodes.empty() && data.ways.empty() && data.relations.empty()) {
    std::cerr << "Error: No valid OSM data found\n";
    return 1;
  }

  if (split_mode.empty()) {
    std::cout << "Writing single file...\n";

    std::set<std::string> tag_keys;
    for (const auto& [id, node] : data.nodes) {
      if (!types.empty() && types.find("node") == types.end())
        continue;
      if (!filters.empty() && !matchesFilters(node.tags, filters))
        continue;
      for (const auto& [k, v] : node.tags)
        if (!v.empty())
          tag_keys.insert(k);
    }
    for (const auto& [id, way] : data.ways) {
      if (way.node_refs.size() < 2)
        continue;
      if (!types.empty()) {
        std::string geom_type = way.is_closed ? "polygon" : "line";
        if (types.find(geom_type) == types.end() && types.find("way") == types.end())
          continue;
      }
      if (!filters.empty() && !matchesFilters(way.tags, filters))
        continue;
      for (const auto& [k, v] : way.tags)
        if (!v.empty())
          tag_keys.insert(k);
    }
    for (const auto& [id, rel] : data.relations) {
      if (!types.empty() && types.find("relation") == types.end())
        continue;
      if (!filters.empty() && !matchesFilters(rel.tags, filters))
        continue;
      for (const auto& [k, v] : rel.tags)
        if (!v.empty())
          tag_keys.insert(k);
    }

    std::string id_col = uniqueReservedName("id", tag_keys);
    std::string type_col = uniqueReservedName("type", tag_keys);
    std::string geom_col = uniqueReservedName("geometry", tag_keys);

    std::vector<std::string> final_columns;
    if (columns.empty()) {
      final_columns.push_back(id_col);
      final_columns.push_back(type_col);
      for (const auto& key : tag_keys) {
        final_columns.push_back(key);
      }
      final_columns.push_back(geom_col);
    } else {
      final_columns = columns;
      bool has_id = false, has_type = false, has_geom = false;
      for (const auto& col : final_columns) {
        if (col == id_col)
          has_id = true;
        if (col == type_col)
          has_type = true;
        if (col == geom_col)
          has_geom = true;
      }
      if (!has_id)
        final_columns.insert(final_columns.begin(), id_col);
      if (!has_type)
        final_columns.insert(final_columns.begin() + 1, type_col);
      if (!has_geom)
        final_columns.push_back(geom_col);
    }

    std::ofstream file(output_base);
    if (!file.is_open()) {
      std::cerr << "Error: Could not create file: " << output_base << "\n";
      return 1;
    }

    for (size_t i = 0; i < final_columns.size(); ++i) {
      file << final_columns[i];
      if (i < final_columns.size() - 1)
        file << ",";
    }
    file << "\n";

    int written = 0;
    for (const auto& [id, node] : data.nodes) {
      if (!types.empty() && types.find("node") == types.end())
        continue;
      if (!filters.empty() && !matchesFilters(node.tags, filters))
        continue;
      writeCSVRow(file, id, "node", node.tags, final_columns, nodeToWKT(node), id_col, type_col,
                  geom_col);
      written++;
    }

    for (const auto& [id, way] : data.ways) {
      if (way.node_refs.size() < 2)
        continue;
      if (!types.empty()) {
        std::string geom_type = way.is_closed ? "polygon" : "line";
        if (types.find(geom_type) == types.end() && types.find("way") == types.end())
          continue;
      }
      if (!filters.empty() && !matchesFilters(way.tags, filters))
        continue;
      std::string wkt = wayToWKT(way, data.nodes);
      if (wkt.empty())
        continue;
      std::string type = way.is_closed ? "polygon" : "line";
      writeCSVRow(file, id, type, way.tags, final_columns, wkt, id_col, type_col, geom_col);
      written++;
    }

    for (const auto& [id, rel] : data.relations) {
      if (!types.empty() && types.find("relation") == types.end())
        continue;
      if (!filters.empty() && !matchesFilters(rel.tags, filters))
        continue;
      std::string wkt;
      std::string type = "relation";
      for (const auto& member : rel.members) {
        if (member.first == "way") {
          auto it = data.ways.find(member.second);
          if (it != data.ways.end()) {
            wkt = wayToWKT(it->second, data.nodes);
            if (!wkt.empty()) {
              type = it->second.is_closed ? "multipolygon" : "multiline";
              break;
            }
          }
        }
      }
      if (wkt.empty())
        continue;
      writeCSVRow(file, id, type, rel.tags, final_columns, wkt, id_col, type_col, geom_col);
      written++;
    }

    std::cout << "Written " << written << " features to " << output_base << "\n";

  } else {
    std::cout << "Splitting into layers (mode: " << split_mode << ")...\n";
    auto layers = detectLayers(data, split_mode, filters, types);
    std::cout << "Found " << layers.size() << " layers:\n";
    for (const auto& layer : layers) {
      std::cout << "  " << layer.name << " (" << layer.features.size() << " features)\n";
    }
    std::cout << "\n";

    std::string base_name = std::filesystem::path(output_base).stem().string();
    for (const auto& layer : layers) {
      writeLayerCSV(layer, output_dir, base_name, columns);
    }
  }

  std::cout << "\n========================================\n";
  std::cout << "  CONVERSION COMPLETE\n";
  std::cout << "========================================\n";
  std::cout << "Nodes: " << data.nodes.size() << "\n";
  std::cout << "Ways: " << data.ways.size() << "\n";
  std::cout << "Relations: " << data.relations.size() << "\n";

  return 0;
}

