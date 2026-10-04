#pragma once
#include <cmath>
#include <string>

namespace Spatial {

struct Ellipsoid {
    double a;
    double f;
};

inline const Ellipsoid WGS84_ELLIPSOID{6378137.0, 1.0 / 298.257223563};

struct TMParams {
    double a;
    double f;
    double lon0_deg;
    double lat0_deg;
    double k0;
    double false_easting;
    double false_northing;
};

inline const TMParams CRTM05_PARAMS{
    WGS84_ELLIPSOID.a, WGS84_ELLIPSOID.f, -84.0, 0.0, 0.9999, 500000.0, 0.0};

inline const TMParams UTM16N_PARAMS{
    WGS84_ELLIPSOID.a, WGS84_ELLIPSOID.f, -87.0, 0.0, 0.9996, 500000.0, 0.0};
inline const TMParams UTM17N_PARAMS{
    WGS84_ELLIPSOID.a, WGS84_ELLIPSOID.f, -81.0, 0.0, 0.9996, 500000.0, 0.0};

namespace proj_detail {

inline double deg2rad(double d) { return d * M_PI / 180.0; }
inline double rad2deg(double r) { return r * 180.0 / M_PI; }

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

}

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

enum class ProjSystem { WGS84, CRTM05, UTM16N, UTM17N, WEBMERCATOR };

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

}
