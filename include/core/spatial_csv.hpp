#pragma once
#include "spatial_types.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>

namespace Spatial {

class SpatialCSVReader {
public:
    bool read(const std::string& filename, VectorDataset& dataset) {
        std::ifstream file(filename);
        if (!file.is_open()) return false;

        std::string line;
        bool is_header = true;

        dataset.min_x = std::numeric_limits<double>::max();
        dataset.min_y = std::numeric_limits<double>::max();
        dataset.max_x = std::numeric_limits<double>::lowest();
        dataset.max_y = std::numeric_limits<double>::lowest();

		while (std::getline(file, line)) {
		            line = trim(line);
		            if (line.empty() || line[0] == '#') {
		                if (line.find("CRS:") != std::string::npos) {
		                    dataset.crs = trim(line.substr(line.find(":") + 1));
		                }
		                continue;
		            }

		            if (is_header) {
		                dataset.columns = splitCSV(line);
		                for (const auto& col : dataset.columns) {
		                    std::string lower = toLower(col);
		                    if (lower == "geometry" || lower == "geom" || lower == "wkt") {
		                        dataset.geometry_column = col;
		                        break;
		                    }
		                }
		                if (dataset.geometry_column.empty() && !dataset.columns.empty()) {
		                    dataset.geometry_column = dataset.columns.back();
		                }
		                is_header = false;
		                continue;
		            }

		            VectorFeature feature;
		            std::vector<std::string> values = splitCSV(line);

		            if (values.size() >= dataset.columns.size()) {
		                for (size_t i = 0; i < dataset.columns.size(); ++i) {
		                    if (dataset.columns[i] != dataset.geometry_column) {
		                        feature.attributes[dataset.columns[i]] = values[i];
		                    }
		                }

		                int geom_index = -1;
		                for (size_t i = 0; i < dataset.columns.size(); ++i) {
		                    if (dataset.columns[i] == dataset.geometry_column) {
		                        geom_index = static_cast<int>(i);
		                        break;
		                    }
		                }

		                if (geom_index >= 0 && static_cast<size_t>(geom_index) < values.size()) {
		                    parseGeometry(values[geom_index], feature);
		                }

		                updateStats(feature, dataset);
		                dataset.features.push_back(feature);
		            }
		        }

        dataset.feature_count = dataset.features.size();
        calculateTypeCounts(dataset);
        return true;
    }

private:
    void parseGeometry(const std::string& geom_str, VectorFeature& feature) {
        std::string str = trim(geom_str);

        if (str.find("POINT") == 0) {
            feature.type = VectorFeature::GeometryType::POINT;
            auto coords = extractCoordinates(str);
            if (coords.size() >= 2) {
                feature.coordinates = {std::stod(coords[0]), std::stod(coords[1])};
            }
        }
        else if (str.find("LINESTRING") == 0) {
            feature.type = VectorFeature::GeometryType::LINESTRING;
            auto points = extractPoints(str);
            for (const auto& p : points) {
                auto coords = split(trim(p), ' ');
                if (coords.size() >= 2) {
                    feature.coordinates.push_back(std::stod(coords[0]));
                    feature.coordinates.push_back(std::stod(coords[1]));
                }
            }
        }
        else if (str.find("POLYGON") == 0) {
            feature.type = VectorFeature::GeometryType::POLYGON;
            auto points = extractPolygonPoints(str);
            for (const auto& p : points) {
                auto coords = split(trim(p), ' ');
                if (coords.size() >= 2) {
                    feature.coordinates.push_back(std::stod(coords[0]));
                    feature.coordinates.push_back(std::stod(coords[1]));
                }
            }
        }
        else if (str.find("MULTIPOINT") == 0) {
            feature.type = VectorFeature::GeometryType::MULTIPOINT;
            parseMultiParts(str, feature);
        }
        else if (str.find("MULTILINESTRING") == 0) {
            feature.type = VectorFeature::GeometryType::MULTILINESTRING;
            parseMultiParts(str, feature);
        }
        else if (str.find("MULTIPOLYGON") == 0) {
            feature.type = VectorFeature::GeometryType::MULTIPOLYGON;
            parseMultiParts(str, feature);
        }
        else {
            auto coords = split(str, ',');
            for (const auto& c : coords) {
                feature.coordinates.push_back(std::stod(trim(c)));
            }
            feature.type = (feature.coordinates.size() == 2) ?
                          VectorFeature::GeometryType::POINT :
                          VectorFeature::GeometryType::LINESTRING;
        }
    }

    void updateStats(const VectorFeature& feature, VectorDataset& dataset) {
        for (size_t i = 0; i < feature.coordinates.size(); i += 2) {
            double x = feature.coordinates[i];
            double y = feature.coordinates[i+1];
            dataset.min_x = std::min(dataset.min_x, x);
            dataset.min_y = std::min(dataset.min_y, y);
            dataset.max_x = std::max(dataset.max_x, x);
            dataset.max_y = std::max(dataset.max_y, y);
            dataset.has_bbox = true;
        }
    }

    void calculateTypeCounts(VectorDataset& dataset) {
        dataset.type_counts.clear();
        for (const auto& f : dataset.features) {
            std::string type_name;
            switch(f.type) {
                case VectorFeature::GeometryType::POINT: type_name = "POINT"; break;
                case VectorFeature::GeometryType::LINESTRING: type_name = "LINESTRING"; break;
                case VectorFeature::GeometryType::POLYGON: type_name = "POLYGON"; break;
                case VectorFeature::GeometryType::MULTIPOINT: type_name = "MULTIPOINT"; break;
                case VectorFeature::GeometryType::MULTILINESTRING: type_name = "MULTILINESTRING"; break;
                case VectorFeature::GeometryType::MULTIPOLYGON: type_name = "MULTIPOLYGON"; break;
            }
            dataset.type_counts[type_name]++;
        }
    }

    std::vector<std::string> extractCoordinates(const std::string& str) {
        size_t start = str.find('(');
        size_t end = str.find(')');
        if (start != std::string::npos && end != std::string::npos) {
            std::string inner = str.substr(start + 1, end - start - 1);
            return split(inner, ' ');
        }
        return {};
    }

    std::vector<std::string> extractPoints(const std::string& str) {
        size_t start = str.find('(');
        size_t end = str.find(')');
        if (start != std::string::npos && end != std::string::npos) {
            std::string inner = str.substr(start + 1, end - start - 1);
            return split(inner, ',');
        }
        return {};
    }

    std::vector<std::string> extractPolygonPoints(const std::string& str) {
        size_t start = str.find('(');
        size_t end = str.rfind(')');
        if (start != std::string::npos && end != std::string::npos) {
            std::string inner = str.substr(start + 1, end - start - 1);
            inner.erase(std::remove(inner.begin(), inner.end(), '('), inner.end());
            inner.erase(std::remove(inner.begin(), inner.end(), ')'), inner.end());
            return split(inner, ',');
        }
        return {};
    }

    std::vector<std::string> splitTopLevelGroups(const std::string& str) {
        size_t start = str.find('(');
        size_t end = str.rfind(')');
        if (start == std::string::npos || end == std::string::npos || end <= start) {
            return {};
        }
        std::string inner = str.substr(start + 1, end - start - 1);

        std::vector<std::string> groups;
        int depth = 0;
        std::string current;
        for (char c : inner) {
            if (c == '(') {
                depth++;
                current += c;
            } else if (c == ')') {
                if (depth > 0) depth--;
                current += c;
            } else if (c == ',' && depth == 0) {
                groups.push_back(current);
                current.clear();
            } else {
                current += c;
            }
        }
        groups.push_back(current);
        return groups;
    }

    std::vector<double> parseGroupPoints(const std::string& group) {
        std::string inner = group;
        inner.erase(std::remove(inner.begin(), inner.end(), '('), inner.end());
        inner.erase(std::remove(inner.begin(), inner.end(), ')'), inner.end());

        std::vector<double> coords;
        for (const auto& token : split(inner, ',')) {
            auto xy = split(token, ' ');
            if (xy.size() >= 2) {
                try {
                    coords.push_back(std::stod(xy[0]));
                    coords.push_back(std::stod(xy[1]));
                } catch (...) {
                }
            }
        }
        return coords;
    }

    void parseMultiParts(const std::string& str, VectorFeature& feature) {
        for (const auto& group : splitTopLevelGroups(str)) {
            auto pts = parseGroupPoints(group);
            if (pts.empty()) continue;
            feature.part_starts.push_back(feature.coordinates.size() / 2);
            feature.coordinates.insert(feature.coordinates.end(), pts.begin(), pts.end());
        }
        if (feature.part_starts.size() <= 1) {
            feature.part_starts.clear();
        }
    }


    std::vector<std::string> splitCSV(const std::string& line) {
        std::vector<std::string> result;
        std::string current;
        bool in_quotes = false;
        int paren_depth = 0;

        for (size_t i = 0; i < line.size(); ++i) {
            char c = line[i];
            if (c == '"') {
                if (in_quotes && i + 1 < line.size() && line[i + 1] == '"') {
                    current += '"';
                    ++i;
                } else {
                    in_quotes = !in_quotes;
                }
            } else if (c == '(' && !in_quotes) {
                paren_depth++;
                current += c;
            } else if (c == ')' && !in_quotes) {
                if (paren_depth > 0) paren_depth--;
                current += c;
            } else if (c == ',' && !in_quotes && paren_depth == 0) {
                result.push_back(trim(current));
                current.clear();
            } else {
                current += c;
            }
        }
        result.push_back(trim(current));
        return result;
    }

    std::vector<std::string> split(const std::string& s, char delimiter) {
        std::vector<std::string> tokens;
        std::string token;
        std::istringstream tokenStream(s);
        while (std::getline(tokenStream, token, delimiter)) {
            std::string trimmed = trim(token);
            if (!trimmed.empty()) tokens.push_back(trimmed);
        }
        return tokens;
    }

    std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\n\r");
        size_t end = s.find_last_not_of(" \t\n\r");
        return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
    }

    std::string toLower(const std::string& s) {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        return result;
    }
};

}