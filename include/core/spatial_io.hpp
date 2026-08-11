// include/core/spatial_io.hpp
//
// Escritores de archivos compartidos (CSV vectorial y ASCII Grid raster).
// Cada programa spatial_* tenia su propia copia casi identica de estas
// funciones; la unica diferencia real entre ellas eran 1-3 lineas de
// comentario de cabecera (writeVectorCSV) o la precision decimal usada al
// volcar celdas (writeRasterASCII). Ambas diferencias se exponen ahora como
// parametros explicitos para no cambiar el output de ningun programa.
#pragma once
#include "spatial_types.hpp"
#include "spatial_geom.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

// Escribe un VectorDataset como CSV con geometria WKT.
//
// Cada programa spatial_* agregaba sus propias lineas de comentario en la
// cabecera (metodo usado, operacion, tolerancia, total de features, etc.),
// en dos posiciones distintas: algunas ANTES de "# Geometry column" y otras
// DESPUES. comments_before_geometry / comments_after_geometry reproducen
// exactamente esas dos posiciones sin cambiar el output de ningun programa.
// close_polygon_ring se reenvia a geometryToWKT (ver spatial_geom.hpp);
// solo spatial_union lo usa como true.
inline void writeVectorCSV(const Spatial::VectorDataset& dataset,
                            const std::string& filename,
                            const std::vector<std::string>& comments_before_geometry = {},
                            const std::vector<std::string>& comments_after_geometry = {},
                            bool close_polygon_ring = false) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not create file: " << filename << "\n";
        return;
    }

    if (!dataset.crs.empty()) file << "# CRS: " << dataset.crs << "\n";
    for (const auto& comment : comments_before_geometry) {
        file << comment << "\n";
    }
    file << "# Geometry column: " << dataset.geometry_column << "\n";
    for (const auto& comment : comments_after_geometry) {
        file << comment << "\n";
    }

    // Write header
    for (size_t i = 0; i < dataset.columns.size(); ++i) {
        file << dataset.columns[i];
        if (i < dataset.columns.size() - 1) file << ",";
    }
    file << "\n";

    // Write data
    for (const auto& feature : dataset.features) {
        for (size_t i = 0; i < dataset.columns.size(); ++i) {
            const std::string& col = dataset.columns[i];
            if (col == dataset.geometry_column) {
                file << geometryToWKT(feature, close_polygon_ring);
            } else {
                auto it = feature.attributes.find(col);
                if (it != feature.attributes.end()) {
                    file << it->second;
                }
            }
            if (i < dataset.columns.size() - 1) file << ",";
        }
        file << "\n";
    }
}

// Escribe un RasterDataset en formato Arc/Info ASCII Grid.
// precision: decimales usados para los valores de celda (la cabecera
// xllcorner/yllcorner/cellsize siempre usa 6, como en el original).
inline void writeRasterASCII(const Spatial::RasterDataset& dataset,
                              const std::string& filename,
                              int precision = 6) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not create file: " << filename << "\n";
        return;
    }

    file << "ncols " << dataset.ncols << "\n";
    file << "nrows " << dataset.nrows << "\n";
    file << "xllcorner " << std::fixed << std::setprecision(6) << dataset.xllcorner << "\n";
    file << "yllcorner " << std::fixed << std::setprecision(6) << dataset.yllcorner << "\n";
    file << "cellsize " << std::fixed << std::setprecision(6) << dataset.cellsize << "\n";
    file << "NODATA_value " << dataset.nodata_value << "\n";

    for (int r = 0; r < dataset.nrows; ++r) {
        for (int c = 0; c < dataset.ncols; ++c) {
            file << std::fixed << std::setprecision(precision) << dataset.at(r, c) << " ";
        }
        file << "\n";
    }

    // Optional Raster Attribute Table, appended the same way spatial_network
    // appends its @NODES/@EDGES/... CSV sections to a .net file: a "@RAT"
    // marker line, a CSV header, then one CSV row per class.
    if (dataset.has_rat && !dataset.rat_columns.empty()) {
        file << "\n@RAT\n";
        for (size_t i = 0; i < dataset.rat_columns.size(); ++i) {
            file << dataset.rat_columns[i];
            if (i + 1 < dataset.rat_columns.size()) file << ",";
        }
        file << "\n";
        for (const auto& row : dataset.rat_rows) {
            for (size_t i = 0; i < dataset.rat_columns.size(); ++i) {
                auto it = row.find(dataset.rat_columns[i]);
                if (it != row.end()) file << it->second;
                if (i + 1 < dataset.rat_columns.size()) file << ",";
            }
            file << "\n";
        }
    }
}
