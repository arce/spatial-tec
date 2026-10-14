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

    struct VectorFeature {
        enum class GeometryType { POINT, LINESTRING, POLYGON, MULTIPOINT, MULTILINESTRING, MULTIPOLYGON };

        GeometryType type;
        std::vector<double> coordinates;
        std::unordered_map<std::string, std::string> attributes;

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

        std::vector<std::string> rat_columns;
        std::vector<std::unordered_map<std::string, std::string>> rat_rows;
        bool has_rat = false;

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

}