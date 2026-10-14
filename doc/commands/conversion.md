# Format Conversion

Tools for converting between the spatial formats used throughout this project: spatial CSV (WKT geometry column), GeoJSON, OpenStreetMap XML, and Shapefile.

> Syntax conventions: `<argument>` is required, `[optional]` is optional, `...` means repetition. Options use a single dash (`-option`).

---

## spatial_csv2geo

**NAME**

`spatial_csv2geo` — convert a spatial CSV to GeoJSON

**SYNOPSIS**

```
spatial_csv2geo <input.csv> <output.geojson> [options]
```

**DESCRIPTION**

Converts a CSV file with WKT geometry into a valid GeoJSON file, including the attribute columns as feature properties. `POINT`, `LINESTRING`, `POLYGON`, `MULTIPOINT`, `MULTILINESTRING`, and `MULTIPOLYGON` WKT are all supported — each real part of a multi-part geometry (e.g. a region made of several separate islands) is written out as its own separate GeoJSON array entry, not merged into one shape.

**OPTIONS**

| Option | Description |
|---|---|
| `-geometry_column <col>` | Name of the geometry column (default: `geometry`) |
| `-crs <value>` | Coordinate system (default: `EPSG:4326`) |
| `-props <list>` | Only include the given properties, comma-separated |
| `-pretty` | Pretty-print (indented) |
| `-compact` | Compact output (no spaces) |
| `-verbose` | Show detailed information |

**EXAMPLES**

```
spatial_csv2geo cities.csv cities.geojson
spatial_csv2geo cities.csv cities.geojson -pretty
spatial_csv2geo cities.csv cities.geojson -props id,name,population
```

**SEE ALSO**

[spatial_geo2csv](#spatial_geo2csv)

---

## spatial_geo2csv

**NAME**

`spatial_geo2csv` — convert GeoJSON to spatial CSV

**SYNOPSIS**

```
spatial_geo2csv <input.geojson> <output.csv> [options]
```

**DESCRIPTION**

Converts a GeoJSON file into a spatial CSV with geometry encoded as WKT, flattening each feature's properties into columns. A feature's top-level GeoJSON `id` (RFC 7946 §3.2, distinct from `properties`) is preserved as the CSV row's `id` column when present, falling back to a sequential number otherwise. If a real property happens to be named `id`, `geometry_type`, or `geometry` — colliding with a column this tool needs for its own bookkeeping — the reserved column is renamed (e.g. to `_id`) and a warning is printed, so the property's value is never silently overwritten.

**OPTIONS**

| Option | Description |
|---|---|
| `-no_geometry` | Exclude the geometry column |
| `-include_type` | Include a geometry type column |
| `-props <list>` | Only include the given properties, comma-separated |

**EXAMPLES**

```
spatial_geo2csv data.geojson data.csv
spatial_geo2csv data.geojson data.csv -props id,name,population
spatial_geo2csv data.geojson data.csv -include_type
```

**SEE ALSO**

[spatial_csv2geo](#spatial_csv2geo)

---

## spatial_osm2csv

**NAME**

`spatial_osm2csv` — convert OpenStreetMap XML to spatial CSV

**SYNOPSIS**

```
spatial_osm2csv <input.osm> <output> [options]
```

**DESCRIPTION**

Converts an OSM extract (XML) into one or more CSV layers with WKT geometry, with optional tag-based filtering and layer splitting. Unless `-columns` is given, every tag key that has a non-empty value on at least one selected node/way/relation is included automatically as its own column (mirroring how `spatial_geo2csv` auto-unions GeoJSON property names) — you don't need to know every tag key in the file ahead of time to keep its data. If a real tag happens to be named `id`, `type`, or `geometry` — colliding with the structural columns this tool needs (`type` is common on relations, e.g. `type=multipolygon`) — the structural column is renamed instead (e.g. to `osm_type`) and a warning is printed, so the tag's value is never shadowed.

**OPTIONS**

| Option | Description |
|---|---|
| `-filter <filters>` | Filter by tags, comma-separated: `key=value`, `key=*`, `key=pattern*` |
| `-types <types>` | Filter by geometry type: `node, line, polygon, way, relation` |
| `-columns <cols>` | Only include the given columns (disables the automatic all-tags-with-a-value default above) |
| `-split <mode>` | Split output into layers: `geometry` (by geometry type), `tag` (by main tags), `tag:key` (by values of a specific tag) |
| `-output_dir <dir>` | Output directory for split layers (default: `.`) |
| `-verbose` | Show detailed information |

**EXAMPLES**

```
# Single file output
spatial_osm2csv map.osm map.csv

# Split by geometry
spatial_osm2csv map.osm layer -split geometry

# Split by tag
spatial_osm2csv map.osm layer -split tag

# Split by highway values
spatial_osm2csv map.osm layer -split tag:highway
```

**SEE ALSO**

[spatial_csv2geo](#spatial_csv2geo), [spatial_csv2osm](#spatial_csv2osm), [spatial_network](network.md#spatial_network)

---

## spatial_csv2osm

**NAME**

`spatial_csv2osm` — convert a spatial CSV to OpenStreetMap XML (`.osm`)

**SYNOPSIS**

```
spatial_csv2osm <input.csv> <output.osm> [options]
```

**DESCRIPTION**

Converts a spatial CSV (WKT geometry column) into an OpenStreetMap XML file: `POINT` features become `<node>` elements, `LINESTRING`/`POLYGON` features become `<way>` elements (polygons get `area=yes` unless `-no-area-tag` is given), and every other non-geometry column becomes a `<tag>` on the corresponding element — unless narrowed down with `-tags`/`-exclude`.

`MULTIPOINT`/`MULTILINESTRING`/`MULTIPOLYGON` features are skipped (counted and reported at the end, with a reason printed for each) rather than converted: OSM XML has no node/way primitive for a multi-part geometry without hand-building a `<relation>`, which this tool doesn't attempt, so it declines rather than emit a way that silently merges separate parts into one.

Every node/way is written with a new negative id — this tool does not merge against or check for conflicts with existing OSM data. In the default `.osm` mode, the output is meant to be opened in an editor such as JOSM and reviewed/uploaded from there. In `-osmchange` mode, the output is meant to be POSTed directly to `PUT/POST /api/0.6/changeset/{id}/upload` after opening a changeset yourself — use it only for features you're sure don't already exist on the map.

**OPTIONS**

| Option | Description |
|---|---|
| `-tags <list>` | Only include these attribute columns as tags, comma-separated |
| `-exclude <list>` | Exclude these attribute columns from tags, comma-separated |
| `-precision <n>` | Decimal digits for lat/lon (default: `7`) |
| `-generator <name>` | Value of the `generator` attribute (default: `spatial_csv2osm`) |
| `-no-area-tag` | Do not add `area=yes` to ways created from `POLYGON` features |
| `-force` | Keep features with out-of-range coordinates instead of skipping them |
| `-osmchange` | Write an `<osmChange><create>...` file instead of a plain `.osm` file, for uploading straight to the OSM API without going through an editor |
| `-changeset <id>` | Changeset id to stamp on every element in `-osmchange` mode (default: `CHANGESET_ID`, a placeholder you must replace) |
| `-verbose` | Show detailed information |

**NOTES**

Coordinates are read as-is and written as lon/lat (WGS84, `EPSG:4326`) — the input CSV must already contain geographic coordinates in degrees; this tool does not reproject data from a projected CRS.

**EXAMPLES**

```
spatial_csv2osm places.csv places.osm
spatial_csv2osm places.csv places.osm -tags name,amenity
spatial_csv2osm parcels.csv parcels.osm -exclude internal_id -precision 6
spatial_csv2osm places.csv places.osc -osmchange -changeset 123456
```

**SEE ALSO**

[spatial_osm2csv](#spatial_osm2csv), [spatial_csv2geo](#spatial_csv2geo)

---

## spatial_shp2csv

**NAME**

`spatial_shp2csv` — convert a Shapefile to spatial CSV

**SYNOPSIS**

```
spatial_shp2csv <input.shp> <output.csv> [options]
```

**DESCRIPTION**

Converts an ESRI Shapefile (the `.shp`/`.shx`/`.dbf` trio) into a spatial CSV with geometry encoded as WKT. `<input.shp>` may also be given as the `.dbf` path or with no extension at all — the base name is used to look up the pair. A companion `.dbf` next to the `.shp` supplies the attribute columns; if it's missing, the output falls back to just a synthetic `id` column with a warning.

`Point`, `PolyLine`, `Polygon`, and `MultiPoint` shapes are supported, including their `Z`/`M` variants (Z/M values are read and discarded — every other tool in this project is 2D-only, see [spatial_types.hpp](../../include/core/spatial_types.hpp)). A `PolyLine`/`Polygon` with more than one part becomes a `MULTILINESTRING`/`MULTIPOLYGON`, one part per array entry — same convention `spatial_csv2geo`/`spatial_geo2csv` already use for multi-part WKT. A Shapefile `Polygon` ring that's actually a hole is read as an extra part of the `MULTIPOLYGON` rather than an inner ring, the same simplification the rest of this project already applies to polygon geometry (it has no hole/inner-ring concept anywhere). `MultiPatch` shapes (and any other unrecognized shape code) aren't supported — those records are read with empty geometry and counted in a warning.

If a `.dbf` field happens to be named `id` or `geometry` — colliding with the columns this tool needs for its own bookkeeping — the field is renamed (e.g. to `_id`) and a warning is printed, so its data is never silently overwritten.

**OPTIONS**

| Option | Description |
|---|---|
| `-no_geometry` | Exclude the geometry column |
| `-include_type` | Include a geometry type column |
| `-props <list>` | Only include the given attribute fields, comma-separated |
| `-verbose` | Show detailed information |

**EXAMPLES**

```
spatial_shp2csv parcels.shp parcels.csv
spatial_shp2csv parcels.shp parcels.csv -props name,zoning
spatial_shp2csv parcels.shp parcels.csv -include_type
```

**SEE ALSO**

[spatial_csv2shp](#spatial_csv2shp)

---

## spatial_csv2shp

**NAME**

`spatial_csv2shp` — convert a spatial CSV to Shapefile

**SYNOPSIS**

```
spatial_csv2shp <input.csv> <output.shp> [options]
```

**DESCRIPTION**

Converts a spatial CSV (WKT geometry column) into an ESRI Shapefile, writing the `.shp`/`.shx`/`.dbf` trio next to `<output.shp>`.

A Shapefile can only hold one geometry family per file — point, line, or polygon — unlike this project's own CSV/GeoJSON, which allow mixed geometry types in one dataset. The family is taken from the first feature with geometry; `POINT` and `MULTIPOINT` count as the same family (the file is written as `MultiPoint` if any `MULTIPOINT` feature is present, `Point` otherwise), and likewise `LINESTRING`/`MULTILINESTRING` and `POLYGON`/`MULTIPOLYGON` (Shapefile's `PolyLine`/`Polygon` records already support multiple parts, so a single-part feature and a multi-part one share the same shape type). Any feature outside the dominant family is skipped, counted, and reported in a warning at the end — the same skip-and-report pattern `spatial_csv2osm` uses for geometry it can't represent in OSM XML.

Every non-geometry column becomes a `.dbf` attribute field. Field names are limited to 10 characters (the dBase III limit the Shapefile format inherits) — longer column names are truncated, and if two columns truncate to the same name, a numeric suffix is appended to keep them unique. A field's type (numeric or text) and width are inferred by scanning every feature's value for that column.

Polygon rings are closed explicitly and reordered to clockwise winding if needed, per the Shapefile spec's requirement for outer rings — regardless of the order they were written in the source CSV/WKT.

**OPTIONS**

| Option | Description |
|---|---|
| `-geometry_column <col>` | Name of the geometry column (default: auto-detected, same as `spatial_csv2geo`) |
| `-exclude <list>` | Exclude these attribute columns from the `.dbf`, comma-separated |
| `-verbose` | Show detailed information |

**EXAMPLES**

```
spatial_csv2shp cities.csv cities.shp
spatial_csv2shp parcels.csv parcels.shp -exclude internal_id
```

**SEE ALSO**

[spatial_shp2csv](#spatial_shp2csv)
