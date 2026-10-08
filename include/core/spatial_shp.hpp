#pragma once
#include "spatial_types.hpp"
#include "spatial_geom.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace Spatial {

namespace shp_detail {

inline int32_t readBE32(std::istream& in) {
    unsigned char b[4] = {0, 0, 0, 0};
    in.read(reinterpret_cast<char*>(b), 4);
    return (int32_t)((uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | uint32_t(b[3]));
}

inline int32_t readLE32(std::istream& in) {
    unsigned char b[4] = {0, 0, 0, 0};
    in.read(reinterpret_cast<char*>(b), 4);
    return (int32_t)((uint32_t(b[3]) << 24) | (uint32_t(b[2]) << 16) | (uint32_t(b[1]) << 8) | uint32_t(b[0]));
}

inline double readLEDouble(std::istream& in) {
    unsigned char b[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    in.read(reinterpret_cast<char*>(b), 8);
    uint64_t bits = 0;
    for (int i = 7; i >= 0; --i) bits = (bits << 8) | b[i];
    double d;
    std::memcpy(&d, &bits, 8);
    return d;
}

inline void writeBE32(std::ostream& out, int32_t value) {
    uint32_t v = (uint32_t)value;
    unsigned char b[4] = {(unsigned char)((v >> 24) & 0xFF), (unsigned char)((v >> 16) & 0xFF),
                           (unsigned char)((v >> 8) & 0xFF), (unsigned char)(v & 0xFF)};
    out.write(reinterpret_cast<char*>(b), 4);
}

inline void writeLE32(std::ostream& out, int32_t value) {
    uint32_t v = (uint32_t)value;
    unsigned char b[4] = {(unsigned char)(v & 0xFF), (unsigned char)((v >> 8) & 0xFF),
                           (unsigned char)((v >> 16) & 0xFF), (unsigned char)((v >> 24) & 0xFF)};
    out.write(reinterpret_cast<char*>(b), 4);
}

inline void writeLEDouble(std::ostream& out, double value) {
    uint64_t bits;
    std::memcpy(&bits, &value, 8);
    unsigned char b[8];
    for (int i = 0; i < 8; ++i) {
        b[i] = (unsigned char)(bits & 0xFF);
        bits >>= 8;
    }
    out.write(reinterpret_cast<char*>(b), 8);
}

enum ShapeTypeCode {
    SHP_NULL = 0,
    SHP_POINT = 1,
    SHP_POLYLINE = 3,
    SHP_POLYGON = 5,
    SHP_MULTIPOINT = 8,
    SHP_POINTZ = 11,
    SHP_POLYLINEZ = 13,
    SHP_POLYGONZ = 15,
    SHP_MULTIPOINTZ = 18,
    SHP_POINTM = 21,
    SHP_POLYLINEM = 23,
    SHP_POLYGONM = 25,
    SHP_MULTIPOINTM = 28
};

inline bool isPointFamily(int32_t t) { return t == SHP_POINT || t == SHP_POINTZ || t == SHP_POINTM; }
inline bool isMultiPointFamily(int32_t t) {
    return t == SHP_MULTIPOINT || t == SHP_MULTIPOINTZ || t == SHP_MULTIPOINTM;
}
inline bool isPolyLineFamily(int32_t t) { return t == SHP_POLYLINE || t == SHP_POLYLINEZ || t == SHP_POLYLINEM; }
inline bool isPolygonFamily(int32_t t) { return t == SHP_POLYGON || t == SHP_POLYGONZ || t == SHP_POLYGONM; }

inline std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r\0", 0, 5);
    size_t end = s.find_last_not_of(" \t\n\r\0", std::string::npos, 5);
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

inline bool isNumericToken(const std::string& s) {
    if (s.empty()) return false;
    try {
        size_t consumed = 0;
        std::stod(s, &consumed);
        return consumed == s.size();
    } catch (...) {
        return false;
    }
}

inline std::string stripShapefileExtension(const std::string& path) {
    if (path.size() >= 4) {
        std::string ext = path.substr(path.size() - 4);
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext == ".shp" || ext == ".shx" || ext == ".dbf") {
            return path.substr(0, path.size() - 4);
        }
    }
    return path;
}

}

class ShapefileReader {
public:
    bool read(const std::string& path, VectorDataset& dataset, std::string* error = nullptr) {
        using namespace shp_detail;

        std::string base = stripShapefileExtension(path);
        std::string shp_path = base + ".shp";

        std::ifstream shp(shp_path, std::ios::binary);
        if (!shp.is_open()) {
            if (error) *error = "Could not open " + shp_path;
            return false;
        }

        int32_t file_code = readBE32(shp);
        if (file_code != 9994) {
            if (error) *error = shp_path + " does not look like a Shapefile (bad file code)";
            return false;
        }
        shp.seekg(100, std::ios::beg);

        dataset.min_x = std::numeric_limits<double>::max();
        dataset.min_y = std::numeric_limits<double>::max();
        dataset.max_x = std::numeric_limits<double>::lowest();
        dataset.max_y = std::numeric_limits<double>::lowest();

        std::vector<VectorFeature> features;
        size_t unsupported_count = 0;

        while (true) {
            std::streampos before = shp.tellg();
            int32_t record_number = readBE32(shp);
            int32_t content_len_words = readBE32(shp);
            if (!shp || shp.eof()) break;
            (void)record_number;
            (void)before;

            std::streampos content_start = shp.tellg();
            std::streamoff content_bytes = std::streamoff(content_len_words) * 2;
            std::streampos record_end = content_start + content_bytes;

            int32_t rec_shape_type = readLE32(shp);
            VectorFeature feature;
            feature.type = VectorFeature::GeometryType::POINT;
            bool ok = parseRecord(shp, rec_shape_type, feature);
            if (!ok) ++unsupported_count;
            if (!feature.coordinates.empty()) updateBBox(feature, dataset);
            features.push_back(std::move(feature));

            shp.seekg(record_end);
            if (!shp) break;
        }

        if (unsupported_count > 0) {
            std::cerr << "Warning: " << unsupported_count
                      << " record(s) had an unsupported shape type (e.g. MultiPatch) and were read as "
                         "empty geometry.\n";
        }

        std::vector<std::string> field_names;
        std::vector<std::vector<std::string>> rows;
        bool has_dbf = readDbf(base + ".dbf", field_names, rows);
        if (!has_dbf) {
            std::cerr << "Warning: no companion .dbf found for " << shp_path << " -- attributes will be "
                      << "limited to the synthetic id column.\n";
        }

        buildColumns(dataset, field_names);

        for (size_t i = 0; i < features.size(); ++i) {
            features[i].attributes[dataset.columns.front()] = std::to_string(i + 1);
            if (has_dbf && i < rows.size()) {
                for (size_t c = 0; c < field_names_safe_.size() && c < rows[i].size(); ++c) {
                    features[i].attributes[field_names_safe_[c]] = rows[i][c];
                }
            }
        }

        dataset.features = std::move(features);
        dataset.feature_count = dataset.features.size();
        calculateTypeCounts(dataset);
        return true;
    }

private:
    std::vector<std::string> field_names_safe_;

    void updateBBox(const VectorFeature& feature, VectorDataset& dataset) {
        for (size_t i = 0; i + 1 < feature.coordinates.size(); i += 2) {
            double x = feature.coordinates[i];
            double y = feature.coordinates[i + 1];
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
            switch (f.type) {
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

    void buildColumns(VectorDataset& dataset, const std::vector<std::string>& field_names) {
        std::set<std::string> taken;
        std::string id_col = "id";
        taken.insert(id_col);

        field_names_safe_.clear();
        for (const auto& fn : field_names) {
            std::string safe = fn;
            if (taken.count(safe)) {
                std::string candidate = "_" + safe;
                while (taken.count(candidate)) candidate = "_" + candidate;
                std::cerr << "Warning: DBF field \"" << safe << "\" collides with a reserved column; "
                          << "reading it as \"" << candidate << "\" instead.\n";
                safe = candidate;
            }
            taken.insert(safe);
            field_names_safe_.push_back(safe);
        }

        std::string geom_col = "geometry";
        while (taken.count(geom_col)) geom_col = "_" + geom_col;
        taken.insert(geom_col);

        dataset.geometry_column = geom_col;
        dataset.columns.clear();
        dataset.columns.push_back(id_col);
        for (const auto& fn : field_names_safe_) dataset.columns.push_back(fn);
        dataset.columns.push_back(geom_col);
    }

    bool parseRecord(std::istream& shp, int32_t shape_type, VectorFeature& feature) {
        using namespace shp_detail;

        if (shape_type == SHP_NULL) {
            return true;
        }

        if (isPointFamily(shape_type)) {
            double x = readLEDouble(shp);
            double y = readLEDouble(shp);
            feature.type = VectorFeature::GeometryType::POINT;
            feature.coordinates = {x, y};
            return true;
        }

        if (isMultiPointFamily(shape_type)) {
            for (int i = 0; i < 4; ++i) readLEDouble(shp);
            int32_t num_points = readLE32(shp);
            feature.type = VectorFeature::GeometryType::MULTIPOINT;
            if (num_points > 0) {
                feature.coordinates.reserve((size_t)num_points * 2);
                for (int32_t i = 0; i < num_points; ++i) {
                    double x = readLEDouble(shp);
                    double y = readLEDouble(shp);
                    feature.coordinates.push_back(x);
                    feature.coordinates.push_back(y);
                }
            }
            return true;
        }

        if (isPolyLineFamily(shape_type) || isPolygonFamily(shape_type)) {
            bool is_polygon = isPolygonFamily(shape_type);
            for (int i = 0; i < 4; ++i) readLEDouble(shp);
            int32_t num_parts = readLE32(shp);
            int32_t num_points = readLE32(shp);
            feature.type = is_polygon ? VectorFeature::GeometryType::POLYGON
                                       : VectorFeature::GeometryType::LINESTRING;
            if (num_parts <= 0 || num_points <= 0) return true;

            std::vector<int32_t> parts((size_t)num_parts);
            for (int32_t i = 0; i < num_parts; ++i) parts[(size_t)i] = readLE32(shp);

            std::vector<double> xy((size_t)num_points * 2);
            for (int32_t i = 0; i < num_points; ++i) {
                xy[(size_t)i * 2] = readLEDouble(shp);
                xy[(size_t)i * 2 + 1] = readLEDouble(shp);
            }

            if (num_parts == 1) {
                feature.coordinates = std::move(xy);
                return true;
            }

            feature.type = is_polygon ? VectorFeature::GeometryType::MULTIPOLYGON
                                       : VectorFeature::GeometryType::MULTILINESTRING;
            for (int32_t p = 0; p < num_parts; ++p) {
                int32_t start = parts[(size_t)p];
                int32_t end = (p + 1 < num_parts) ? parts[(size_t)p + 1] : num_points;
                if (start < 0 || start >= num_points || end <= start) continue;
                feature.part_starts.push_back(feature.coordinates.size() / 2);
                for (int32_t k = start; k < end && k < num_points; ++k) {
                    feature.coordinates.push_back(xy[(size_t)k * 2]);
                    feature.coordinates.push_back(xy[(size_t)k * 2 + 1]);
                }
            }
            if (feature.part_starts.size() <= 1) feature.part_starts.clear();
            return true;
        }

        return false;
    }

    bool readDbf(const std::string& path, std::vector<std::string>& field_names,
                 std::vector<std::vector<std::string>>& rows) {
        std::ifstream dbf(path, std::ios::binary);
        if (!dbf.is_open()) return false;

        unsigned char header[32];
        dbf.read(reinterpret_cast<char*>(header), 32);
        if (!dbf) return false;

        uint32_t num_records = (uint32_t)header[4] | ((uint32_t)header[5] << 8) | ((uint32_t)header[6] << 16) |
                                ((uint32_t)header[7] << 24);
        uint16_t header_bytes = (uint16_t)header[8] | ((uint16_t)header[9] << 8);

        struct Field {
            std::string name;
            int length;
        };
        std::vector<Field> fields;

        int field_area_bytes = (int)header_bytes - 32 - 1;
        int num_fields = field_area_bytes > 0 ? field_area_bytes / 32 : 0;
        for (int i = 0; i < num_fields; ++i) {
            unsigned char fd[32];
            dbf.read(reinterpret_cast<char*>(fd), 32);
            if (!dbf) break;
            std::string name(reinterpret_cast<char*>(fd), 11);
            size_t nul = name.find('\0');
            if (nul != std::string::npos) name = name.substr(0, nul);
            Field field;
            field.name = shp_detail::trim(name);
            field.length = fd[16];
            fields.push_back(field);
        }

        dbf.seekg(header_bytes, std::ios::beg);

        field_names.clear();
        for (auto& f : fields) field_names.push_back(f.name);

        rows.clear();
        rows.reserve(num_records);
        for (uint32_t r = 0; r < num_records; ++r) {
            char delete_flag = ' ';
            dbf.read(&delete_flag, 1);
            if (!dbf) break;
            std::vector<std::string> row;
            row.reserve(fields.size());
            for (auto& f : fields) {
                std::string raw((size_t)std::max(f.length, 0), ' ');
                if (f.length > 0) dbf.read(&raw[0], f.length);
                row.push_back(shp_detail::trim(raw));
            }
            rows.push_back(std::move(row));
        }
        return true;
    }
};


struct ShapefileWriteResult {
    bool ok = false;
    size_t written = 0;
    size_t skipped_wrong_family = 0;
    std::string target_family;
};

namespace shp_detail {

inline int geometryFamily(const VectorFeature& feature) {
    if (feature.coordinates.empty()) return -1;
    switch (feature.type) {
        case VectorFeature::GeometryType::POINT:
        case VectorFeature::GeometryType::MULTIPOINT:
            return 0;
        case VectorFeature::GeometryType::LINESTRING:
        case VectorFeature::GeometryType::MULTILINESTRING:
            return 1;
        case VectorFeature::GeometryType::POLYGON:
        case VectorFeature::GeometryType::MULTIPOLYGON:
            return 2;
    }
    return -1;
}

inline const char* familyName(int family) {
    switch (family) {
        case 0: return "point";
        case 1: return "line";
        case 2: return "polygon";
        default: return "unknown";
    }
}

inline std::vector<std::vector<double>> buildParts(const VectorFeature& feature, bool is_polygon) {
    std::vector<std::vector<double>> parts;
    for (const auto& range : featurePartRanges(feature)) {
        std::vector<double> part;
        part.reserve((range.second - range.first) * 2);
        for (size_t p = range.first; p < range.second; ++p) {
            part.push_back(feature.coordinates[p * 2]);
            part.push_back(feature.coordinates[p * 2 + 1]);
        }
        if (part.size() < 2) continue;

        if (is_polygon && part.size() >= 4) {
            if (std::fabs(part[0] - part[part.size() - 2]) > 1e-12 ||
                std::fabs(part[1] - part[part.size() - 1]) > 1e-12) {
                part.push_back(part[0]);
                part.push_back(part[1]);
            }

            size_t n = part.size() / 2;
            double area2 = 0.0;
            for (size_t i = 0; i < n; ++i) {
                size_t j = (i + 1) % n;
                area2 += part[i * 2] * part[j * 2 + 1] - part[j * 2] * part[i * 2 + 1];
            }
            if (area2 > 0.0) {
                std::vector<double> reversed;
                reversed.reserve(part.size());
                for (size_t i = n; i-- > 0;) {
                    reversed.push_back(part[i * 2]);
                    reversed.push_back(part[i * 2 + 1]);
                }
                part = std::move(reversed);
            }
        }

        parts.push_back(std::move(part));
    }
    return parts;
}

inline void writeShpFileHeader(std::ostream& out, int32_t file_length_words, int32_t shape_type, double minx,
                                double miny, double maxx, double maxy) {
    writeBE32(out, 9994);
    for (int i = 0; i < 5; ++i) writeBE32(out, 0);
    writeBE32(out, file_length_words);
    writeLE32(out, 1000);
    writeLE32(out, shape_type);
    writeLEDouble(out, minx);
    writeLEDouble(out, miny);
    writeLEDouble(out, maxx);
    writeLEDouble(out, maxy);
    writeLEDouble(out, 0.0);
    writeLEDouble(out, 0.0);
    writeLEDouble(out, 0.0);
    writeLEDouble(out, 0.0);
}

inline void writeGeometryContent(std::ostream& buf, const VectorFeature& feature, int32_t target_shape_type) {
    switch (target_shape_type) {
        case SHP_POINT: {
            writeLE32(buf, SHP_POINT);
            double x = feature.coordinates.size() >= 2 ? feature.coordinates[0] : 0.0;
            double y = feature.coordinates.size() >= 2 ? feature.coordinates[1] : 0.0;
            writeLEDouble(buf, x);
            writeLEDouble(buf, y);
            break;
        }
        case SHP_MULTIPOINT: {
            writeLE32(buf, SHP_MULTIPOINT);
            double minx = std::numeric_limits<double>::max(), miny = std::numeric_limits<double>::max();
            double maxx = std::numeric_limits<double>::lowest(), maxy = std::numeric_limits<double>::lowest();
            int32_t num_points = (int32_t)(feature.coordinates.size() / 2);
            for (int32_t i = 0; i < num_points; ++i) {
                minx = std::min(minx, feature.coordinates[(size_t)i * 2]);
                maxx = std::max(maxx, feature.coordinates[(size_t)i * 2]);
                miny = std::min(miny, feature.coordinates[(size_t)i * 2 + 1]);
                maxy = std::max(maxy, feature.coordinates[(size_t)i * 2 + 1]);
            }
            if (num_points == 0) minx = miny = maxx = maxy = 0.0;
            writeLEDouble(buf, minx);
            writeLEDouble(buf, miny);
            writeLEDouble(buf, maxx);
            writeLEDouble(buf, maxy);
            writeLE32(buf, num_points);
            for (int32_t i = 0; i < num_points; ++i) {
                writeLEDouble(buf, feature.coordinates[(size_t)i * 2]);
                writeLEDouble(buf, feature.coordinates[(size_t)i * 2 + 1]);
            }
            break;
        }
        case SHP_POLYLINE:
        case SHP_POLYGON: {
            writeLE32(buf, target_shape_type);
            std::vector<std::vector<double>> parts = buildParts(feature, target_shape_type == SHP_POLYGON);
            double minx = std::numeric_limits<double>::max(), miny = std::numeric_limits<double>::max();
            double maxx = std::numeric_limits<double>::lowest(), maxy = std::numeric_limits<double>::lowest();
            int32_t total_points = 0;
            for (auto& part : parts) {
                total_points += (int32_t)(part.size() / 2);
                for (size_t i = 0; i < part.size(); i += 2) {
                    minx = std::min(minx, part[i]);
                    maxx = std::max(maxx, part[i]);
                    miny = std::min(miny, part[i + 1]);
                    maxy = std::max(maxy, part[i + 1]);
                }
            }
            if (parts.empty()) minx = miny = maxx = maxy = 0.0;
            writeLEDouble(buf, minx);
            writeLEDouble(buf, miny);
            writeLEDouble(buf, maxx);
            writeLEDouble(buf, maxy);
            writeLE32(buf, (int32_t)parts.size());
            writeLE32(buf, total_points);
            int32_t running = 0;
            for (auto& part : parts) {
                writeLE32(buf, running);
                running += (int32_t)(part.size() / 2);
            }
            for (auto& part : parts) {
                for (size_t i = 0; i < part.size(); i += 2) {
                    writeLEDouble(buf, part[i]);
                    writeLEDouble(buf, part[i + 1]);
                }
            }
            break;
        }
        default:
            break;
    }
}

struct DbfFieldSpec {
    std::string source_column;
    std::string dbf_name;
    char type = 'C';
    int length = 1;
    int decimals = 0;
};

inline std::string sanitizeDbfName(const std::string& raw) {
    std::string clean;
    for (char c : raw) {
        if (std::isalnum((unsigned char)c) || c == '_') {
            clean += c;
        } else {
            clean += '_';
        }
    }
    if (clean.empty()) clean = "FIELD";
    if (std::isdigit((unsigned char)clean[0])) clean = "F" + clean;
    if (clean.size() > 10) clean = clean.substr(0, 10);
    return clean;
}

inline std::vector<DbfFieldSpec> planDbfFields(const std::vector<const VectorFeature*>& features,
                                                const std::vector<std::string>& columns,
                                                const std::string& geometry_column) {
    std::vector<DbfFieldSpec> specs;

    for (const auto& col : columns) {
        if (col == geometry_column) continue;

        DbfFieldSpec spec;
        spec.source_column = col;

        bool any_value = false, all_numeric = true, has_negative = false;
        size_t max_len = 1, max_int_digits = 1, max_decimals = 0;
        for (const auto* f : features) {
            auto it = f->attributes.find(col);
            if (it == f->attributes.end() || it->second.empty()) continue;
            const std::string& v = it->second;
            any_value = true;
            max_len = std::max(max_len, v.size());
            if (!shp_detail::isNumericToken(v)) {
                all_numeric = false;
                continue;
            }
            if (!v.empty() && v[0] == '-') has_negative = true;
            size_t dot = v.find('.');
            size_t sign = (!v.empty() && (v[0] == '-' || v[0] == '+')) ? 1 : 0;
            if (dot != std::string::npos) {
                size_t decs = v.size() - dot - 1;
                max_decimals = std::max(max_decimals, std::min(decs, size_t(6)));
                max_int_digits = std::max(max_int_digits, dot - sign);
            } else {
                max_int_digits = std::max(max_int_digits, v.size() - sign);
            }
        }

        if (any_value && all_numeric) {
            spec.type = 'N';
            spec.decimals = (int)max_decimals;
            int width = (int)max_int_digits + (spec.decimals > 0 ? spec.decimals + 1 : 0) + (has_negative ? 1 : 0);
            spec.length = std::min(std::max(width, 1), 20);
        } else {
            spec.type = 'C';
            spec.length = (int)std::min(max_len, size_t(254));
            spec.decimals = 0;
        }
        specs.push_back(spec);
    }

    std::set<std::string> taken;
    for (auto& spec : specs) {
        std::string clean = sanitizeDbfName(spec.source_column);
        std::string candidate = clean;
        int suffix = 1;
        while (taken.count(candidate)) {
            std::string suf = std::to_string(suffix++);
            size_t keep = suf.size() < clean.size() ? clean.size() - suf.size() : 0;
            candidate = clean.substr(0, keep) + suf;
        }
        taken.insert(candidate);
        spec.dbf_name = candidate;
    }
    return specs;
}

inline bool writeDbf(const std::string& path, const std::vector<const VectorFeature*>& features,
                      const std::vector<DbfFieldSpec>& specs) {
    std::ofstream dbf(path, std::ios::binary);
    if (!dbf.is_open()) return false;

    uint32_t num_records = (uint32_t)features.size();
    uint16_t header_bytes = (uint16_t)(32 + specs.size() * 32 + 1);
    uint16_t record_bytes = 1;
    for (auto& s : specs) record_bytes += (uint16_t)s.length;

    unsigned char header[32] = {0};
    header[0] = 0x03;
    header[1] = 1;
    header[2] = 1;
    header[3] = 1;
    header[4] = (unsigned char)(num_records & 0xFF);
    header[5] = (unsigned char)((num_records >> 8) & 0xFF);
    header[6] = (unsigned char)((num_records >> 16) & 0xFF);
    header[7] = (unsigned char)((num_records >> 24) & 0xFF);
    header[8] = (unsigned char)(header_bytes & 0xFF);
    header[9] = (unsigned char)((header_bytes >> 8) & 0xFF);
    header[10] = (unsigned char)(record_bytes & 0xFF);
    header[11] = (unsigned char)((record_bytes >> 8) & 0xFF);
    dbf.write(reinterpret_cast<char*>(header), 32);

    for (auto& s : specs) {
        unsigned char fd[32] = {0};
        std::memcpy(fd, s.dbf_name.c_str(), std::min<size_t>(s.dbf_name.size(), 10));
        fd[11] = (unsigned char)s.type;
        fd[16] = (unsigned char)s.length;
        fd[17] = (unsigned char)s.decimals;
        dbf.write(reinterpret_cast<char*>(fd), 32);
    }
    char terminator = 0x0D;
    dbf.write(&terminator, 1);

    for (const auto* f : features) {
        char delete_flag = ' ';
        dbf.write(&delete_flag, 1);
        for (auto& s : specs) {
            std::string value;
            auto it = f->attributes.find(s.source_column);
            if (it != f->attributes.end()) value = it->second;
            if ((int)value.size() > s.length) value = value.substr(0, (size_t)s.length);

            std::string field;
            if (s.type == 'N') {
                field = std::string((size_t)s.length - value.size(), ' ') + value;
            } else {
                field = value + std::string((size_t)s.length - value.size(), ' ');
            }
            dbf.write(field.c_str(), s.length);
        }
    }
    char eof_marker = 0x1A;
    dbf.write(&eof_marker, 1);
    return true;
}

}

inline ShapefileWriteResult writeShapefile(const VectorDataset& dataset, const std::string& path,
                                            std::string* error = nullptr) {
    using namespace shp_detail;
    ShapefileWriteResult result;

    std::string base = stripShapefileExtension(path);
    std::string shp_path = base + ".shp";
    std::string shx_path = base + ".shx";
    std::string dbf_path = base + ".dbf";

    int dominant_family = -1;
    for (const auto& f : dataset.features) {
        int fam = geometryFamily(f);
        if (fam >= 0) {
            dominant_family = fam;
            break;
        }
    }
    if (dominant_family < 0) dominant_family = 0;
    result.target_family = familyName(dominant_family);

    std::vector<const VectorFeature*> kept;
    kept.reserve(dataset.features.size());
    bool any_multipoint = false;
    for (const auto& f : dataset.features) {
        int fam = geometryFamily(f);
        if (fam < 0) continue;
        if (fam != dominant_family) {
            ++result.skipped_wrong_family;
            continue;
        }
        if (f.type == VectorFeature::GeometryType::MULTIPOINT) any_multipoint = true;
        kept.push_back(&f);
    }

    int32_t target_shape_type;
    switch (dominant_family) {
        case 0: target_shape_type = any_multipoint ? SHP_MULTIPOINT : SHP_POINT; break;
        case 1: target_shape_type = SHP_POLYLINE; break;
        default: target_shape_type = SHP_POLYGON; break;
    }

    std::ofstream shp(shp_path, std::ios::binary);
    std::ofstream shx(shx_path, std::ios::binary);
    if (!shp.is_open() || !shx.is_open()) {
        if (error) *error = "Could not create " + shp_path + " / " + shx_path;
        return result;
    }

    writeShpFileHeader(shp, 0, target_shape_type, 0, 0, 0, 0);
    writeShpFileHeader(shx, 0, target_shape_type, 0, 0, 0, 0);

    double minx = std::numeric_limits<double>::max(), miny = std::numeric_limits<double>::max();
    double maxx = std::numeric_limits<double>::lowest(), maxy = std::numeric_limits<double>::lowest();
    bool has_any_point = false;

    int32_t record_number = 1;
    for (const auto* f : kept) {
        std::ostringstream content;
        writeGeometryContent(content, *f, target_shape_type);
        std::string content_str = content.str();
        int32_t content_len_words = (int32_t)(content_str.size() / 2);

        std::streampos record_offset = shp.tellp();
        writeBE32(shp, record_number);
        writeBE32(shp, content_len_words);
        shp.write(content_str.data(), (std::streamsize)content_str.size());

        int32_t offset_words = (int32_t)(std::streamoff(record_offset) / 2);
        writeBE32(shx, offset_words);
        writeBE32(shx, content_len_words);

        for (size_t i = 0; i + 1 < f->coordinates.size(); i += 2) {
            has_any_point = true;
            minx = std::min(minx, f->coordinates[i]);
            maxx = std::max(maxx, f->coordinates[i]);
            miny = std::min(miny, f->coordinates[i + 1]);
            maxy = std::max(maxy, f->coordinates[i + 1]);
        }

        ++record_number;
    }
    if (!has_any_point) minx = miny = maxx = maxy = 0.0;

    std::streampos shp_end = shp.tellp();
    std::streampos shx_end = shx.tellp();
    int32_t shp_length_words = (int32_t)(std::streamoff(shp_end) / 2);
    int32_t shx_length_words = (int32_t)(std::streamoff(shx_end) / 2);

    shp.seekp(0, std::ios::beg);
    writeShpFileHeader(shp, shp_length_words, target_shape_type, minx, miny, maxx, maxy);
    shx.seekp(0, std::ios::beg);
    writeShpFileHeader(shx, shx_length_words, target_shape_type, minx, miny, maxx, maxy);
    shp.close();
    shx.close();

    std::vector<DbfFieldSpec> specs = planDbfFields(kept, dataset.columns, dataset.geometry_column);
    if (!writeDbf(dbf_path, kept, specs)) {
        if (error) *error = "Could not create " + dbf_path;
        return result;
    }

    result.ok = true;
    result.written = kept.size();
    return result;
}

}
