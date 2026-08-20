# Coordinate Reference Systems

Tools for converting vector coordinates between geographic and projected coordinate reference systems.

> Syntax conventions: `<argument>` is required, `[optional]` is optional, `...` means repetition. Options use a single dash (`-option`).

---

## spatial_reproject

**NAME**

`spatial_reproject` — convert vector coordinates between coordinate systems

**SYNOPSIS**

```
spatial_reproject <input.csv> <output.csv> -from <system> -to <system> [options]
```

**DESCRIPTION**

Converts every coordinate pair of every feature in a spatial CSV from one coordinate reference system to another. `POINT`, `LINESTRING`, `POLYGON`, and their `MULTI*` variants are all supported the same way — every `(x, y)` pair is transformed regardless of how the geometry's parts are structured.

The projected systems are implemented directly (no PROJ/GDAL dependency) using the classic Snyder Transverse Mercator series (USGS Professional Paper 1395), which gives sub-millimeter accuracy within a few degrees of a projection's central meridian — comfortably enough for Costa Rica's extent under CRTM05 or either UTM zone. Converting between two projected systems (e.g. `-from utm16n -to crtm05`) routes through geographic WGS84 as an intermediate step automatically.

**Practical note on datum:** CRTM05 is formally defined over the CR05 geographic datum, not WGS84 directly. However, CR05 is declared by Costa Rica's own reference-frame documentation to be equivalent to WGS84 within centimeter-level accuracy, and this is standard civilian GIS/GPS practice in the country — `spatial_reproject` treats `wgs84` lat/lon as the input/output of the CRTM05 and UTM conversions directly, without an intermediate CR05↔WGS84 datum shift. This is accurate enough for any civilian GPS-grade dataset; only centimeter-level geodetic survey work would need that extra step, which is out of scope here.

**SUPPORTED SYSTEMS**

| Identifier | Description |
|---|---|
| `wgs84` | Geographic WGS84 (EPSG:4326). Coordinates are `lon,lat` in degrees, matching standard WKT X,Y order. |
| `crtm05` | CR05 / CRTM05 (EPSG:5367) — Costa Rica's official coordinate system since 2007. Transverse Mercator, central meridian 84°W, k₀=0.9999, false easting 500000 m. |
| `utm16n` | WGS 84 / UTM zone 16N (EPSG:32616). Central meridian 87°W, k₀=0.9996. Covers the western part of Costa Rica. |
| `utm17n` | WGS 84 / UTM zone 17N (EPSG:32617). Central meridian 81°W, k₀=0.9996. Covers the eastern part of Costa Rica. |
| `webmercator` | WGS 84 / Pseudo-Mercator (EPSG:3857) — the spherical projection used by OpenStreetMap, Google Maps, Leaflet and Mapbox for map tiles. |

Identifiers are case-insensitive; common aliases are also accepted (e.g. `geographic`/`epsg:4326` for `wgs84`, `mercator`/`epsg:3857` for `webmercator`).

**OPTIONS**

| Option | Description |
|---|---|
| `-from <system>` | Coordinate system of the input file (**required**) |
| `-to <system>` | Coordinate system of the output file (**required**) |
| `-verbose` | Show detailed statistics |

**EXAMPLES**

```
spatial_reproject cities.csv cities_crtm05.csv -from wgs84 -to crtm05
spatial_reproject cities_crtm05.csv cities.csv -from crtm05 -to wgs84
spatial_reproject roads.csv roads_utm.csv -from wgs84 -to utm17n
spatial_reproject cities.csv cities_web.csv -from wgs84 -to webmercator
spatial_reproject zone_utm16n.csv zone_crtm05.csv -from utm16n -to crtm05
```

**SEE ALSO**

[spatial_csv2geo](conversion.md#spatial_csv2geo), [spatial_info](info.md#spatial_info)
