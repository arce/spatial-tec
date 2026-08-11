// include/core/spatial_types.hpp
#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <variant>
#include <limits>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace Spatial {

    // ===== VECTOR TYPES =====
    struct VectorFeature {
        // MULTIPOINT/MULTILINESTRING/MULTIPOLYGON were added so multi-part
        // WKT geometries survive a CSV round-trip instead of crashing
        // SpatialCSVReader::parseGeometry (see spatial_csv.hpp). Tools that
        // don't special-case them are not required to -- they can treat an
        // unrecognized value from this enum as a generic fallback (most
        // existing code already has a `default:` branch for that), but
        // parsing/counting code in this header must stay exhaustive.
        enum class GeometryType { POINT, LINESTRING, POLYGON, MULTIPOINT, MULTILINESTRING, MULTIPOLYGON };

        GeometryType type;
        std::vector<double> coordinates;
        std::unordered_map<std::string, std::string> attributes;

        // Coordinate-PAIR index (not raw double index -- divide by 2) where
        // each part of a MULTIPOINT/MULTILINESTRING/MULTIPOLYGON starts
        // within `coordinates`, e.g. {0, 5, 8} means part 0 is pairs
        // [0,5), part 1 is [5,8), part 2 is [8, coordinates.size()/2).
        // Populated by SpatialCSVReader::read (spatial_csv.hpp) directly
        // while parsing the WKT, no extra pass needed.
        //
        // Left empty for every POINT/LINESTRING/POLYGON feature and for any
        // Multi* feature that only has one part -- `coordinates` alone is
        // already unambiguous then, so there's no reason to carry the
        // metadata (this is the common case: this vector costs nothing for
        // the overwhelming majority of features).
        //
        // A raw sentinel value spliced into `coordinates` to mark part
        // boundaries was considered and rejected: every consumer that
        // already iterates `coordinates` two-at-a-time (spatial_buffer,
        // spatial_centroid, spatial_union, spatial_join, spatial_statistics,
        // ...) would need to learn to recognize and skip it, or silently
        // treat the marker as a real point. A parallel index list needs no
        // such awareness -- `coordinates` stays exactly the plain flat pair
        // list every existing tool already assumes, and this field is
        // ignorable metadata for anything that doesn't look for it.
        std::vector<size_t> part_starts;
    };

    struct VectorDataset {
        std::vector<std::string> columns;
        std::vector<VectorFeature> features;
        std::string geometry_column = "geometry";
        std::string crs;
        size_t feature_count = 0;
        
        std::unordered_map<std::string, size_t> type_counts;
        double min_x = 0, min_y = 0, max_x = 0, max_y = 0;
        bool has_bbox = false;
    };

    // ===== RASTER TYPES =====
    struct RasterDataset {
        int ncols = 0;
        int nrows = 0;
        double xllcorner = 0.0;
        double yllcorner = 0.0;
        double cellsize = 1.0;
        double nodata_value = -9999.0;
        
        std::vector<double> data;
        
        std::string crs;
        std::string description;
        
        bool has_stats = false;
        double min_val = 0.0;
        double max_val = 0.0;
        double mean_val = 0.0;
        double sum_val = 0.0;
        size_t valid_cells = 0;
        size_t nodata_cells = 0;
        
        double& at(int row, int col) { return data[row * ncols + col]; }
        const double& at(int row, int col) const { return data[row * ncols + col]; }

        bool is_valid(int row, int col) const {
            return at(row, col) != nodata_value;
        }

        // ===== RASTER ATTRIBUTE TABLE (RAT) =====
        // Optional classification table appended after the grid data in an
        // ASCII Grid file (see ascii_grid.hpp). rat_columns always has
        // "value" as its first entry -- the column matched against cell
        // values -- plus whatever attribute columns the file defines (e.g.
        // count, class_name, color). Each row of rat_rows is keyed by
        // column name, values stored as strings (same convention as
        // VectorFeature::attributes) so it can be shown generically in the
        // same table widgets used for vector attribute tables.
        std::vector<std::string> rat_columns;
        std::vector<std::unordered_map<std::string, std::string>> rat_rows;
        bool has_rat = false;

        // Finds the RAT row whose "value" column matches the given cell
        // value. Returns -1 if there's no RAT, no match, or a malformed
        // "value" entry. A linear scan is fine here: RATs classify a raster
        // into a handful of categories, not one row per cell.
        int findRatRow(double value) const {
            if (!has_rat) return -1;
            for (size_t i = 0; i < rat_rows.size(); ++i) {
                auto it = rat_rows[i].find("value");
                if (it == rat_rows[i].end()) continue;
                try {
                    if (std::fabs(std::stod(it->second) - value) < 1e-6) return (int)i;
                } catch (...) {
                }
            }
            return -1;
        }
    };

} // namespace Spatial