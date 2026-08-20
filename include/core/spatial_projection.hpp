// include/core/spatial_projection.hpp
//
// Conversion geodesica y proyeccion Transversa de Mercator (Gauss-Kruger),
// mas Web Mercator (esferica), implementadas directamente con las formulas
// clasicas de Snyder ("Map Projections: A Working Manual", USGS Professional
// Paper 1395, 1987) -- sin dependencia externa (PROJ/GDAL), consistente con
// el resto del proyecto (parser propio de Shapefile, CSV, GeoJSON, etc.).
//
// La serie de Snyder (orden e^6/e^8) da precision sub-milimetrica dentro de
// unos pocos grados del meridiano central, que es exactamente el caso de
// CRTM05 y de las zonas UTM que cubren Costa Rica -- no hace falta la serie
// de Kruger de orden superior (Karney 2011) que usan PROJ/GeographicLib para
// exactitud a escala de todo el elipsoide.
#pragma once
#include <cmath>
#include <string>

namespace Spatial {

// ===== Elipsoide =====
// Todas las proyecciones soportadas (CRTM05, UTM, y la esfera usada por Web
// Mercator) se definen sobre el elipsoide WGS84 -- ver la nota de scope en
// doc/commands/projection.md sobre CR05 vs WGS84.
struct Ellipsoid {
    double a;  // semieje mayor (m)
    double f;  // achatamiento
};

inline const Ellipsoid WGS84_ELLIPSOID{6378137.0, 1.0 / 298.257223563};

// ===== Parametros de una proyeccion Transversa de Mercator =====
struct TMParams {
    double a;                // semieje mayor (m)
    double f;                // achatamiento
    double lon0_deg;         // meridiano central (grados)
    double lat0_deg;         // latitud de origen (grados)
    double k0;                // factor de escala en el meridiano central
    double false_easting;    // falso este (m)
    double false_northing;   // falso norte (m)
};

// CRTM05 (EPSG:5367 / EPSG:8908) -- sistema oficial de Costa Rica desde 2007.
inline const TMParams CRTM05_PARAMS{
    WGS84_ELLIPSOID.a, WGS84_ELLIPSOID.f, -84.0, 0.0, 0.9999, 500000.0, 0.0};

// UTM zona 16N (EPSG:32616) y 17N (EPSG:32617), WGS84 -- Costa Rica queda
// dividido entre ambas, que fue la razon original para crear CRTM05 como
// zona unica centrada en el pais.
inline const TMParams UTM16N_PARAMS{
    WGS84_ELLIPSOID.a, WGS84_ELLIPSOID.f, -87.0, 0.0, 0.9996, 500000.0, 0.0};
inline const TMParams UTM17N_PARAMS{
    WGS84_ELLIPSOID.a, WGS84_ELLIPSOID.f, -81.0, 0.0, 0.9996, 500000.0, 0.0};

namespace proj_detail {

inline double deg2rad(double d) { return d * M_PI / 180.0; }
inline double rad2deg(double r) { return r * 180.0 / M_PI; }

// Longitud de arco de meridiano desde el ecuador hasta la latitud phi
// (radianes), serie de Snyder eq. 3-21 (orden e^6).
inline double meridionalArc(double phi, double e2, double a) {
    double e4 = e2 * e2;
    double e6 = e4 * e2;
    double c0 = 1.0 - e2 / 4.0 - 3.0 * e4 / 64.0 - 5.0 * e6 / 256.0;
    double c2 = 3.0 * e2 / 8.0 + 3.0 * e4 / 32.0 + 45.0 * e6 / 1024.0;
    double c4 = 15.0 * e4 / 256.0 + 45.0 * e6 / 1024.0;
    double c6 = 35.0 * e6 / 3072.0;
    return a * (c0 * phi - c2 * std::sin(2.0 * phi) + c4 * std::sin(4.0 * phi) -
                c6 * std::sin(6.0 * phi));
}

}  // namespace proj_detail

// Directa: geografica (lat, lon en grados, WGS84) -> TM (easting, northing
// en metros). Formulas de Snyder eq. 8-9 a 8-11.
inline void geographicToTM(double lat_deg, double lon_deg, const TMParams& p, double& easting,
                            double& northing) {
    using namespace proj_detail;

    double e2 = p.f * (2.0 - p.f);
    double ep2 = e2 / (1.0 - e2);

    double phi = deg2rad(lat_deg);
    double lambda = deg2rad(lon_deg);
    double lambda0 = deg2rad(p.lon0_deg);
    double phi0 = deg2rad(p.lat0_deg);

    double sin_phi = std::sin(phi);
    double cos_phi = std::cos(phi);
    double tan_phi = std::tan(phi);

    double N = p.a / std::sqrt(1.0 - e2 * sin_phi * sin_phi);
    double T = tan_phi * tan_phi;
    double C = ep2 * cos_phi * cos_phi;
    double A = (lambda - lambda0) * cos_phi;

    double A2 = A * A;
    double A3 = A2 * A;
    double A4 = A2 * A2;
    double A5 = A4 * A;
    double A6 = A4 * A2;

    double M = meridionalArc(phi, e2, p.a);
    double M0 = meridionalArc(phi0, e2, p.a);

    easting = p.k0 * N *
                  (A + (1.0 - T + C) * A3 / 6.0 +
                   (5.0 - 18.0 * T + T * T + 72.0 * C - 58.0 * ep2) * A5 / 120.0) +
              p.false_easting;

    northing = p.k0 * (M - M0 +
                        N * tan_phi *
                            (A2 / 2.0 + (5.0 - T + 9.0 * C + 4.0 * C * C) * A4 / 24.0 +
                             (61.0 - 58.0 * T + T * T + 600.0 * C - 330.0 * ep2) * A6 / 720.0)) +
               p.false_northing;
}

// Inversa: TM (easting, northing en metros) -> geografica (lat, lon en
// grados, WGS84). Formulas de Snyder eq. 8-17 a 8-22 (latitud de pie de
// meridiano via serie, sin iteracion).
inline void tmToGeographic(double easting, double northing, const TMParams& p, double& lat_deg,
                            double& lon_deg) {
    using namespace proj_detail;

    double e2 = p.f * (2.0 - p.f);
    double ep2 = e2 / (1.0 - e2);
    double e4 = e2 * e2;
    double e6 = e4 * e2;

    double lambda0 = deg2rad(p.lon0_deg);
    double phi0 = deg2rad(p.lat0_deg);

    double M0 = meridionalArc(phi0, e2, p.a);
    double M = M0 + (northing - p.false_northing) / p.k0;

    double c0 = 1.0 - e2 / 4.0 - 3.0 * e4 / 64.0 - 5.0 * e6 / 256.0;
    double mu = M / (p.a * c0);

    double e1 = (1.0 - std::sqrt(1.0 - e2)) / (1.0 + std::sqrt(1.0 - e2));
    double e1_2 = e1 * e1;
    double e1_3 = e1_2 * e1;
    double e1_4 = e1_3 * e1;

    double phi1 = mu + (3.0 * e1 / 2.0 - 27.0 * e1_3 / 32.0) * std::sin(2.0 * mu) +
                  (21.0 * e1_2 / 16.0 - 55.0 * e1_4 / 32.0) * std::sin(4.0 * mu) +
                  (151.0 * e1_3 / 96.0) * std::sin(6.0 * mu) +
                  (1097.0 * e1_4 / 512.0) * std::sin(8.0 * mu);

    double sin_phi1 = std::sin(phi1);
    double cos_phi1 = std::cos(phi1);
    double tan_phi1 = std::tan(phi1);

    double C1 = ep2 * cos_phi1 * cos_phi1;
    double T1 = tan_phi1 * tan_phi1;
    double N1 = p.a / std::sqrt(1.0 - e2 * sin_phi1 * sin_phi1);
    double R1 = p.a * (1.0 - e2) / std::pow(1.0 - e2 * sin_phi1 * sin_phi1, 1.5);
    double D = (easting - p.false_easting) / (N1 * p.k0);

    double D2 = D * D;
    double D3 = D2 * D;
    double D4 = D2 * D2;
    double D5 = D4 * D;
    double D6 = D4 * D2;

    double phi = phi1 - (N1 * tan_phi1 / R1) *
                             (D2 / 2.0 -
                              (5.0 + 3.0 * T1 + 10.0 * C1 - 4.0 * C1 * C1 - 9.0 * ep2) * D4 /
                                  24.0 +
                              (61.0 + 90.0 * T1 + 298.0 * C1 + 45.0 * T1 * T1 - 252.0 * ep2 -
                               3.0 * C1 * C1) *
                                  D6 / 720.0);

    double lambda = lambda0 + (D - (1.0 + 2.0 * T1 + C1) * D3 / 6.0 +
                                (5.0 - 2.0 * C1 + 28.0 * T1 - 3.0 * C1 * C1 + 8.0 * ep2 +
                                 24.0 * T1 * T1) *
                                    D5 / 120.0) /
                                   cos_phi1;

    lat_deg = rad2deg(phi);
    lon_deg = rad2deg(lambda);
}

// Web Mercator / Pseudo-Mercator (EPSG:3857), la proyeccion que usan
// OpenStreetMap, Google Maps, Leaflet, Mapbox, etc. para sus tiles.
// Formulas esfericas usando el semieje mayor de WGS84 como radio (asi es
// como EPSG:3857 esta definida oficialmente -- no es una esfera de radio
// medio, y por eso "Pseudo-Mercator" no es una proyeccion conforme estricta
// del elipsoide).
inline void geographicToWebMercator(double lat_deg, double lon_deg, double& x, double& y) {
    using namespace proj_detail;
    double R = WGS84_ELLIPSOID.a;
    double phi = deg2rad(lat_deg);
    double lambda = deg2rad(lon_deg);
    x = R * lambda;
    y = R * std::log(std::tan(M_PI / 4.0 + phi / 2.0));
}

inline void webMercatorToGeographic(double x, double y, double& lat_deg, double& lon_deg) {
    using namespace proj_detail;
    double R = WGS84_ELLIPSOID.a;
    double lambda = x / R;
    double phi = 2.0 * std::atan(std::exp(y / R)) - M_PI / 2.0;
    lat_deg = rad2deg(phi);
    lon_deg = rad2deg(lambda);
}

// ===== Sistema de referencia soportado por spatial_reproject =====
enum class ProjSystem { WGS84, CRTM05, UTM16N, UTM17N, WEBMERCATOR };

// Reconoce el identificador de un sistema (alias comunes incluidos) y
// devuelve false si no se reconoce.
inline bool parseProjSystem(const std::string& id_lower, ProjSystem& out) {
    if (id_lower == "wgs84" || id_lower == "geographic" || id_lower == "geo" ||
        id_lower == "epsg:4326" || id_lower == "4326") {
        out = ProjSystem::WGS84;
        return true;
    }
    if (id_lower == "crtm05" || id_lower == "epsg:5367" || id_lower == "5367" ||
        id_lower == "epsg:8908" || id_lower == "8908") {
        out = ProjSystem::CRTM05;
        return true;
    }
    if (id_lower == "utm16n" || id_lower == "utm16" || id_lower == "epsg:32616" ||
        id_lower == "32616") {
        out = ProjSystem::UTM16N;
        return true;
    }
    if (id_lower == "utm17n" || id_lower == "utm17" || id_lower == "epsg:32617" ||
        id_lower == "32617") {
        out = ProjSystem::UTM17N;
        return true;
    }
    if (id_lower == "webmercator" || id_lower == "web_mercator" || id_lower == "mercator" ||
        id_lower == "pseudomercator" || id_lower == "epsg:3857" || id_lower == "3857") {
        out = ProjSystem::WEBMERCATOR;
        return true;
    }
    return false;
}

inline bool isGeographicSystem(ProjSystem s) { return s == ProjSystem::WGS84; }

// Nombre canonico corto, usado en mensajes y en el nombre de columna
// source_x/source_y si hiciera falta.
inline std::string projSystemName(ProjSystem s) {
    switch (s) {
        case ProjSystem::WGS84:
            return "wgs84";
        case ProjSystem::CRTM05:
            return "crtm05";
        case ProjSystem::UTM16N:
            return "utm16n";
        case ProjSystem::UTM17N:
            return "utm17n";
        case ProjSystem::WEBMERCATOR:
            return "webmercator";
    }
    return "unknown";
}

// Etiqueta descriptiva, usada para el encabezado "# CRS: ..." del CSV de
// salida.
inline std::string projSystemLabel(ProjSystem s) {
    switch (s) {
        case ProjSystem::WGS84:
            return "EPSG:4326 (WGS84 geographic)";
        case ProjSystem::CRTM05:
            return "EPSG:5367 (CR05 / CRTM05)";
        case ProjSystem::UTM16N:
            return "EPSG:32616 (WGS 84 / UTM zone 16N)";
        case ProjSystem::UTM17N:
            return "EPSG:32617 (WGS 84 / UTM zone 17N)";
        case ProjSystem::WEBMERCATOR:
            return "EPSG:3857 (WGS 84 / Pseudo-Mercator)";
    }
    return "unknown";
}

// Convierte un punto (x, y) desde `from` hacia `to`, pasando por geografica
// WGS84 como paso intermedio cuando ninguno de los dos es ya geografico (p.
// ej. UTM16N -> CRTM05 hace UTM16N -> WGS84 -> CRTM05). Si from == to, copia
// directo sin perder precision por una vuelta redundante.
inline void reprojectPoint(double x, double y, ProjSystem from, ProjSystem to, double& out_x,
                            double& out_y) {
    if (from == to) {
        out_x = x;
        out_y = y;
        return;
    }

    double lon = x, lat = y;
    if (!isGeographicSystem(from)) {
        switch (from) {
            case ProjSystem::CRTM05:
                tmToGeographic(x, y, CRTM05_PARAMS, lat, lon);
                break;
            case ProjSystem::UTM16N:
                tmToGeographic(x, y, UTM16N_PARAMS, lat, lon);
                break;
            case ProjSystem::UTM17N:
                tmToGeographic(x, y, UTM17N_PARAMS, lat, lon);
                break;
            case ProjSystem::WEBMERCATOR:
                webMercatorToGeographic(x, y, lat, lon);
                break;
            default:
                break;
        }
    }

    if (isGeographicSystem(to)) {
        out_x = lon;
        out_y = lat;
        return;
    }

    switch (to) {
        case ProjSystem::CRTM05:
            geographicToTM(lat, lon, CRTM05_PARAMS, out_x, out_y);
            break;
        case ProjSystem::UTM16N:
            geographicToTM(lat, lon, UTM16N_PARAMS, out_x, out_y);
            break;
        case ProjSystem::UTM17N:
            geographicToTM(lat, lon, UTM17N_PARAMS, out_x, out_y);
            break;
        case ProjSystem::WEBMERCATOR:
            geographicToWebMercator(lat, lon, out_x, out_y);
            break;
        default:
            out_x = lon;
            out_y = lat;
            break;
    }
}

}  // namespace Spatial
