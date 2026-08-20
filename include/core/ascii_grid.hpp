#pragma once
#include "spatial_types.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace Spatial {

class ASCIIGridReader {
public:
    bool read(const std::string& filename, RasterDataset& dataset) {
        std::ifstream file(filename);
        if (!file.is_open()) return false;

        std::string line;
        std::vector<std::string> tokens;
        bool in_header = true;

        while (in_header && std::getline(file, line)) {
            line = trim(line);
            if (line.empty()) continue;

            tokens = split(line, ' ');
            if (tokens.empty()) continue;

            std::string key = toLower(tokens[0]);

            if (key == "ncols") dataset.ncols = std::stoi(tokens[1]);
            else if (key == "nrows") dataset.nrows = std::stoi(tokens[1]);
            else if (key == "xllcorner" || key == "xllcenter")
                dataset.xllcorner = std::stod(tokens[1]);
            else if (key == "yllcorner" || key == "yllcenter")
                dataset.yllcorner = std::stod(tokens[1]);
            else if (key == "cellsize") dataset.cellsize = std::stod(tokens[1]);
            else if (key == "nodata_value") dataset.nodata_value = std::stod(tokens[1]);
            else {
                try {
                    std::stod(key);
                    in_header = false;
                    processDataLine(line, dataset);
                } catch (...) {}
            }
        }

        size_t total_cells = static_cast<size_t>(dataset.nrows) * static_cast<size_t>(dataset.ncols);
        while (std::getline(file, line)) {
            std::string trimmed = trim(line);
            if (trimmed.empty()) continue;

            if (trimmed == "@RAT") {
                readRAT(file, dataset);
                break;
            }

            if (dataset.data.size() >= total_cells) continue;

            processDataLine(trimmed, dataset);
        }

        calculateStats(dataset);
        return true;
    }

private:
    void readRAT(std::ifstream& file, RasterDataset& dataset) {
        std::string line;
        if (!std::getline(file, line)) return;
        dataset.rat_columns = splitCSVLine(trim(line));
        if (dataset.rat_columns.empty()) return;

        while (std::getline(file, line)) {
            std::string trimmed = trim(line);
            if (trimmed.empty()) continue;

            std::vector<std::string> values = splitCSVLine(trimmed);
            std::unordered_map<std::string, std::string> row;
            for (size_t i = 0; i < dataset.rat_columns.size(); ++i) {
                row[dataset.rat_columns[i]] = (i < values.size()) ? values[i] : "";
            }
            dataset.rat_rows.push_back(std::move(row));
        }

        dataset.has_rat = !dataset.rat_rows.empty();
    }

    std::vector<std::string> splitCSVLine(const std::string& s) {
        std::vector<std::string> tokens;
        std::string token;
        std::istringstream tokenStream(s);
        while (std::getline(tokenStream, token, ',')) {
            tokens.push_back(trim(token));
        }
        return tokens;
    }

    void processDataLine(const std::string& line, RasterDataset& dataset) {
        auto tokens = split(line, ' ');
        for (const auto& token : tokens) {
            if (dataset.data.size() < static_cast<size_t>(dataset.nrows * dataset.ncols)) {
                dataset.data.push_back(std::stod(token));
            }
        }
    }

    void calculateStats(RasterDataset& dataset) {
        if (dataset.data.empty()) return;

        dataset.min_val = std::numeric_limits<double>::max();
        dataset.max_val = std::numeric_limits<double>::lowest();
        dataset.sum_val = 0.0;
        dataset.valid_cells = 0;
        dataset.nodata_cells = 0;

        for (double val : dataset.data) {
            if (std::abs(val - dataset.nodata_value) < 1e-9) {
                dataset.nodata_cells++;
            } else {
                dataset.min_val = std::min(dataset.min_val, val);
                dataset.max_val = std::max(dataset.max_val, val);
                dataset.sum_val += val;
                dataset.valid_cells++;
            }
        }

        if (dataset.valid_cells > 0) {
            dataset.mean_val = dataset.sum_val / dataset.valid_cells;
        }
        dataset.has_stats = true;
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