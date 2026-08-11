# Vector Processing

Tools that create, transform, or combine vector geometries (points, lines, polygons) stored as spatial CSV.

> Syntax conventions: `<argument>` is required, `[optional]` is optional, `...` means repetition. Options use a single dash (`-option`).

---

## spatial_buffer

**NAME**

`spatial_buffer` — create buffer zones around vector geometries

**SYNOPSIS**

```
spatial_buffer <input> <output> -distance <value> [options]
```

> The tool's own `--help`/error text still calls itself `spatial_buffer_vector` (an older name) — that's just its self-printed banner, not a separate binary. `make` only ever produces `build/spatial_buffer`; invoke it by that name.

**DESCRIPTION**

Creates buffer polygons around points, lines, or polygons. A positive distance grows the geometry outward; a negative distance shrinks it inward.

**OPTIONS**

| Option | Description |
|---|---|
| `-distance <value>` | Buffer distance (positive = outward, negative = inward) |
| `-units <units>` | `units` (default, CRS units) or `meters`/`kilometers` |
| `-segments <n>` | Number of segments used to approximate circles (default: `36`) |
| `-cap <style>` | Cap style: `round` (default) or `flat` |
| `-dissolve` | Dissolve overlapping buffers into one |

**EXAMPLES**

```
spatial_buffer cities.csv buffers.csv -distance 10
spatial_buffer roads.csv corridors.csv -distance 100 -units meters
spatial_buffer zones.csv inner.csv -distance -5
spatial_buffer cities.csv dissolved.csv -distance 10 -dissolve
```

**SEE ALSO**

[spatial_union](#spatial_union), [spatial_clip_vector](#spatial_clip_vector)

---

## spatial_centroid

**NAME**

`spatial_centroid` — calculate centroids of vector geometries

**SYNOPSIS**

```
spatial_centroid <input> <output> [options]
```

**DESCRIPTION**

Calculates the centroid of each vector feature (lines and polygons by default). The output always has `POINT` geometry — there is no option to keep the original geometry alongside it. Every attribute from the input feature is copied to the corresponding output point as-is, plus a `source_type` column recording the original geometry type (`point`, `line`, or `polygon`).

`MULTIPOINT`/`MULTILINESTRING`/`MULTIPOLYGON` input features are not currently supported and are skipped (they don't match any `-types` value, since that option only recognizes `point`, `line`, `polygon`).

**OPTIONS**

| Option | Description |
|---|---|
| `-method <method>` | `centroid`, `mass`, or `midpoint` (default: `centroid` for polygons, `midpoint` for lines) |
| `-types <list>` | Types to process: `point,line,polygon` (default: `line,polygon`) |
| `-verbose` | Show detailed statistics |

**EXAMPLES**

```
spatial_centroid zones.csv centroids.csv
spatial_centroid polygons.csv centroids.csv -method mass
spatial_centroid lines.csv midpoints.csv
spatial_centroid polygons.csv centroids.csv -types polygon
```

**SEE ALSO**

[spatial_simplify](#spatial_simplify), [spatial_union](#spatial_union)

---

## spatial_clip_vector

**NAME**

`spatial_clip_vector` — clip vector data by spatial extent

**SYNOPSIS**

```
spatial_clip_vector <input> <output> [options]
```

**DESCRIPTION**

Clips vector features to a bounding box, a polygon, or the extent of another file.

**OPTIONS**

| Option | Description |
|---|---|
| `-bbox <minx> <miny> <maxx> <maxy>` | Clip by bounding box |
| `-polygon <file>` | Clip using a polygon defined in a CSV |
| `-clip_to <file>` | Clip to the extent of another file |
| `-invert` | Invert the clip |

**EXAMPLES**

```
spatial_clip_vector cities.csv area.csv -bbox -84.5 9.5 -84.0 10.0
spatial_clip_vector cities.csv polygon.csv -polygon boundary.csv
spatial_clip_vector cities.csv zone.csv -clip_to elevation.asc
```

**SEE ALSO**

[spatial_clip_raster](raster.md#spatial_clip_raster), [spatial_query](analysis.md#spatial_query)

---

## spatial_simplify

**NAME**

`spatial_simplify` — simplify vector geometries

**SYNOPSIS**

```
spatial_simplify <input> <output> [options]
```

**DESCRIPTION**

Reduces the number of vertices in lines and polygons while preserving their overall shape.

**OPTIONS**

| Option | Description |
|---|---|
| `-tolerance <value>` | Simplification tolerance (Douglas-Peucker) |
| `-factor <value>` | Reduction factor (0–1; e.g. `0.5` = 50% reduction) |
| `-method <method>` | `douglas` or `visvalingam` (default: `douglas`) |
| `-types <list>` | Types to simplify: `point,line,polygon` (default: `line,polygon`) |
| `-verbose` | Show detailed statistics |

**EXAMPLES**

```
spatial_simplify curves.csv simplified_curves.csv -tolerance 0.001
spatial_simplify polygons.csv simplified_polygons.csv -factor 0.5
spatial_simplify data.csv simplified_data.csv -tolerance 0.01 -verbose
```

**SEE ALSO**

[spatial_centroid](#spatial_centroid)

---

## spatial_union

**NAME**

`spatial_union` — union/dissolve polygons

**SYNOPSIS**

```
spatial_union <input> <output> [options]
```

**DESCRIPTION**

Dissolves boundaries between polygons, optionally grouping by an attribute or merging everything into a single feature.

**OPTIONS**

| Option | Description |
|---|---|
| `-all` | Union all polygons into one |
| `-group_by <attr>` | Group and union by an attribute value |
| `-method <method>` | `intersect`, `adjacent`, or `both` (default: `both`) |

**EXAMPLES**

```
spatial_union polygons.csv merged_polygons.csv
spatial_union polygons.csv single_polygon.csv -all
spatial_union parcels.csv grouped_parcels.csv -group_by zone
```

**SEE ALSO**

[spatial_buffer](#spatial_buffer), [spatial_centroid](#spatial_centroid)

---

## spatial_merge_vector

**NAME**

`spatial_merge_vector` — merge multiple vector files into one

**SYNOPSIS**

```
spatial_merge_vector <input1> [input2 ...] <output>
```

**DESCRIPTION**

Combines the contents of two or more vector layers into a single output file.

**OPTIONS**

| Option | Description |
|---|---|
| `-auto_attrs` | Automatically merge columns from all inputs |
| `-drop_dups` | Drop duplicate features (by geometry) |

**EXAMPLES**

```
spatial_merge_vector cities1.csv cities2.csv merged.csv
spatial_merge_vector c1.csv c2.csv c3.csv merged.csv -auto_attrs
```

**SEE ALSO**

[spatial_merge_raster](raster.md#spatial_merge_raster)
