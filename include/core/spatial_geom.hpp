// include/core/spatial_geom.hpp
//
// Primitivas geometricas compartidas (point-in-polygon, distancias
// punto-punto/segmento/linea y serializacion a WKT). Consolidadas desde
// copias identicas (o casi identicas) repetidas en 4-11 archivos de src/.
#pragma once
#include "spatial_types.hpp"
#include <string>
#include <vector>
#include <utility>
#include <cmath>
#include <limits>
#include <algorithm>

// Ray casting. polygon es una lista plana [x0,y0,x1,y1,...].
inline bool pointInPolygon(double x, double y, const std::vector<double>& polygon) {
    bool inside = false;
    int n = static_cast<int>(polygon.size() / 2);

    for (int i = 0, j = n - 1; i < n; j = i++) {
        double xi = polygon[i * 2];
        double yi = polygon[i * 2 + 1];
        double xj = polygon[j * 2];
        double yj = polygon[j * 2 + 1];

        if (((yi > y) != (yj > y)) &&
            (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) {
            inside = !inside;
        }
    }

    return inside;
}

inline double pointToPointDistance(double x1, double y1, double x2, double y2) {
    double dx = x1 - x2;
    double dy = y1 - y2;
    return std::sqrt(dx * dx + dy * dy);
}

inline double pointToSegmentDistance(double px, double py,
                                      double x1, double y1,
                                      double x2, double y2) {
    double dx = x2 - x1;
    double dy = y2 - y1;
    double len2 = dx * dx + dy * dy;

    if (len2 < 1e-12) {
        return pointToPointDistance(px, py, x1, y1);
    }

    double t = ((px - x1) * dx + (py - y1) * dy) / len2;
    t = std::max(0.0, std::min(1.0, t));

    double cx = x1 + t * dx;
    double cy = y1 + t * dy;

    return pointToPointDistance(px, py, cx, cy);
}

// Distancia minima de un punto a cualquier segmento de una polilinea plana
// [x0,y0,x1,y1,...].
inline double pointToLineDistance(double px, double py, const std::vector<double>& line) {
    if (line.size() < 2) return std::numeric_limits<double>::max();
    if (line.size() == 2) {
        return pointToPointDistance(px, py, line[0], line[1]);
    }

    double min_dist = std::numeric_limits<double>::max();

    for (size_t i = 0; i < line.size() - 2; i += 2) {
        double dist = pointToSegmentDistance(px, py, line[i], line[i + 1], line[i + 2], line[i + 3]);
        min_dist = std::min(min_dist, dist);
    }

    return min_dist;
}

// Centroide ponderado por area de un anillo (posiblemente abierto)
// [x0,y0,x1,y1,...]. Usado para posicionar la etiqueta de un POLYGON en su
// centro. Si el area es ~0 (anillo degenerado: vertices colineales o
// duplicados), retrocede al promedio simple de los vertices.
inline void polygonCentroid(const std::vector<double>& coords, double& cx, double& cy) {
    int n = static_cast<int>(coords.size() / 2);
    cx = 0.0;
    cy = 0.0;
    if (n == 0) return;

    double area2 = 0.0, cxa = 0.0, cya = 0.0;
    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        double xi = coords[i * 2], yi = coords[i * 2 + 1];
        double xj = coords[j * 2], yj = coords[j * 2 + 1];
        double cross = xi * yj - xj * yi;
        area2 += cross;
        cxa += (xi + xj) * cross;
        cya += (yi + yj) * cross;
    }

    if (std::fabs(area2) < 1e-12) {
        for (int i = 0; i < n; ++i) {
            cx += coords[i * 2];
            cy += coords[i * 2 + 1];
        }
        cx /= n;
        cy /= n;
        return;
    }

    double area = area2 / 2.0;
    cx = cxa / (6.0 * area);
    cy = cya / (6.0 * area);
}

// Punto a la mitad (por longitud recorrida, no por indice de vertice) de
// una polilinea [x0,y0,x1,y1,...]. Usado para posicionar la etiqueta de un
// LINESTRING.
inline void lineMidpoint(const std::vector<double>& coords, double& mx, double& my) {
    int n = static_cast<int>(coords.size() / 2);
    if (n == 0) {
        mx = my = 0.0;
        return;
    }
    if (n == 1) {
        mx = coords[0];
        my = coords[1];
        return;
    }

    double total_len = 0.0;
    for (int i = 0; i + 1 < n; ++i) {
        total_len += pointToPointDistance(coords[i * 2], coords[i * 2 + 1], coords[(i + 1) * 2],
                                           coords[(i + 1) * 2 + 1]);
    }

    double half = total_len / 2.0;
    double acc = 0.0;
    for (int i = 0; i + 1 < n; ++i) {
        double x1 = coords[i * 2], y1 = coords[i * 2 + 1];
        double x2 = coords[(i + 1) * 2], y2 = coords[(i + 1) * 2 + 1];
        double seg = pointToPointDistance(x1, y1, x2, y2);
        if (acc + seg >= half || i + 2 == n) {
            double t = (seg > 1e-12) ? (half - acc) / seg : 0.0;
            t = std::max(0.0, std::min(1.0, t));
            mx = x1 + t * (x2 - x1);
            my = y1 + t * (y2 - y1);
            return;
        }
        acc += seg;
    }

    mx = coords[(n - 1) * 2];
    my = coords[(n - 1) * 2 + 1];
}

// Area (shoelace, siempre positiva) de un anillo posiblemente abierto
// [x0,y0,x1,y1,...]. Usada por spatial_viewer para elegir la parte mas
// grande de un MULTIPOLYGON (p.ej. la isla principal) al posicionar su
// etiqueta -- ver include/viewer/multipart_geometry.hpp.
inline double polygonArea(const std::vector<double>& coords) {
    int n = static_cast<int>(coords.size() / 2);
    double area2 = 0.0;
    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        area2 += coords[i * 2] * coords[j * 2 + 1] - coords[j * 2] * coords[i * 2 + 1];
    }
    return std::fabs(area2) / 2.0;
}

// Longitud total de una polilinea [x0,y0,x1,y1,...]. Usada por
// spatial_viewer para elegir la parte mas larga de un MULTILINESTRING al
// posicionar su etiqueta.
inline double lineLength(const std::vector<double>& coords) {
    int n = static_cast<int>(coords.size() / 2);
    double total = 0.0;
    for (int i = 0; i + 1 < n; ++i) {
        total += pointToPointDistance(coords[i * 2], coords[i * 2 + 1], coords[(i + 1) * 2],
                                       coords[(i + 1) * 2 + 1]);
    }
    return total;
}

// Rangos [begin,end) de indice de PAR de coordenada para cada parte de una
// feature, usando VectorFeature::part_starts cuando esta presente. Una
// feature sin part_starts (todo POINT/LINESTRING/POLYGON, y cualquier
// Multi* con una sola parte real) entrega un unico rango que cubre todo
// `coordinates` -- asi el llamador no necesita un camino separado para el
// caso "no es multi-parte".
inline std::vector<std::pair<size_t, size_t>> featurePartRanges(const Spatial::VectorFeature& feature) {
    std::vector<std::pair<size_t, size_t>> ranges;
    size_t total_pairs = feature.coordinates.size() / 2;
    if (feature.part_starts.empty()) {
        if (total_pairs > 0) ranges.push_back({0, total_pairs});
        return ranges;
    }
    for (size_t i = 0; i < feature.part_starts.size(); ++i) {
        size_t begin = feature.part_starts[i];
        size_t end = (i + 1 < feature.part_starts.size()) ? feature.part_starts[i + 1] : total_pairs;
        ranges.push_back({begin, end});
    }
    return ranges;
}

// Serializa una VectorFeature a WKT.
//
// close_ring: si es true, los POLYGON repiten el primer vertice al final
// del anillo (WKT valido / anillo cerrado). La mayoria de los programas
// escriben el anillo tal cual viene almacenado (sin cerrar); spatial_union
// es la excepcion y pasa close_ring=true.
inline std::string geometryToWKT(const Spatial::VectorFeature& feature, bool close_ring = false) {
    std::string result;
    switch (feature.type) {
        case Spatial::VectorFeature::GeometryType::POINT: {
            if (feature.coordinates.size() >= 2) {
                result = "POINT(" + std::to_string(feature.coordinates[0]) +
                         " " + std::to_string(feature.coordinates[1]) + ")";
            }
            break;
        }
        case Spatial::VectorFeature::GeometryType::LINESTRING: {
            result = "LINESTRING(";
            for (size_t i = 0; i < feature.coordinates.size(); i += 2) {
                if (i > 0) result += ", ";
                result += std::to_string(feature.coordinates[i]) + " " +
                          std::to_string(feature.coordinates[i + 1]);
            }
            result += ")";
            break;
        }
        case Spatial::VectorFeature::GeometryType::POLYGON: {
            result = "POLYGON((";
            for (size_t i = 0; i < feature.coordinates.size(); i += 2) {
                if (i > 0) result += ", ";
                result += std::to_string(feature.coordinates[i]) + " " +
                          std::to_string(feature.coordinates[i + 1]);
            }
            if (close_ring && !feature.coordinates.empty()) {
                result += ", " + std::to_string(feature.coordinates[0]) + " " +
                          std::to_string(feature.coordinates[1]);
            }
            result += "))";
            break;
        }
        // MULTIPOINT/MULTILINESTRING/MULTIPOLYGON: featurePartRanges() gives
        // the real part boundaries recorded in feature.part_starts by
        // spatial_csv.hpp's parser, so each part (island/segment/point) is
        // written out as its own separate element -- not merged into one,
        // the way an earlier version of this switch (and every other
        // MULTI*-reading tool before part_starts existed) had to.
        case Spatial::VectorFeature::GeometryType::MULTIPOINT: {
            result = "MULTIPOINT(";
            bool first = true;
            for (const auto& range : featurePartRanges(feature)) {
                for (size_t p = range.first; p < range.second; ++p) {
                    if (!first) result += ", ";
                    first = false;
                    result += "(" + std::to_string(feature.coordinates[p * 2]) + " " +
                              std::to_string(feature.coordinates[p * 2 + 1]) + ")";
                }
            }
            result += ")";
            break;
        }
        case Spatial::VectorFeature::GeometryType::MULTILINESTRING: {
            result = "MULTILINESTRING(";
            bool first_part = true;
            for (const auto& range : featurePartRanges(feature)) {
                if (!first_part) result += ", ";
                first_part = false;
                result += "(";
                bool first_pt = true;
                for (size_t p = range.first; p < range.second; ++p) {
                    if (!first_pt) result += ", ";
                    first_pt = false;
                    result += std::to_string(feature.coordinates[p * 2]) + " " +
                              std::to_string(feature.coordinates[p * 2 + 1]);
                }
                result += ")";
            }
            result += ")";
            break;
        }
        case Spatial::VectorFeature::GeometryType::MULTIPOLYGON: {
            result = "MULTIPOLYGON(";
            bool first_part = true;
            for (const auto& range : featurePartRanges(feature)) {
                if (!first_part) result += ", ";
                first_part = false;
                result += "((";
                bool first_pt = true;
                double first_x = 0, first_y = 0;
                for (size_t p = range.first; p < range.second; ++p) {
                    if (first_pt) {
                        first_x = feature.coordinates[p * 2];
                        first_y = feature.coordinates[p * 2 + 1];
                    } else {
                        result += ", ";
                    }
                    first_pt = false;
                    result += std::to_string(feature.coordinates[p * 2]) + " " +
                              std::to_string(feature.coordinates[p * 2 + 1]);
                }
                if (close_ring && range.second > range.first) {
                    result += ", " + std::to_string(first_x) + " " + std::to_string(first_y);
                }
                result += "))";
            }
            result += ")";
            break;
        }
    }
    return result;
}
