// include/core/ascii_grid.hpp
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

            // Once the grid is fully populated, any further (non-@RAT)
            // content is left alone rather than mis-parsed as more cell
            // values -- this keeps old files with stray trailing lines
            // working exactly as before.
            if (dataset.data.size() >= total_cells) continue;

            processDataLine(trimmed, dataset);
        }

        calculateStats(dataset);
        return true;
    }

private:
    // Parses an optional Raster Attribute Table appended after the grid
    // data, in the form:
    //   @RAT
    //   value,count,class_name,color
    //   1,1523,Water,0000FF
    //   2,890,Forest,00FF00
    // `file` is positioned right after the "@RAT" line; the next line is
    // taken as the CSV header (column names), and every following non-empty
    // line as a CSV data row. Rows with fewer fields than there are columns
    // get the missing trailing fields as empty strings.
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

    // Comma-splitting variant of split() that preserves empty fields (so
    // column positions stay aligned even if a row omits a trailing value),
    // unlike split(s, ' ') which is used for the whitespace-separated
    // header/grid tokens and intentionally drops empty tokens there.
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

} // namespace Spatial