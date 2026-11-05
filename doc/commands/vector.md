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

Calculates the centroid of each vector feature (lines and polygons by default). Points use their own coordinates, lines use the point at the midpoint of their path length, and polygons use the area-weighted (shoelace) centroid. The output always has `POINT` geometry — there is no option to keep the original geometry alongside it. Every attribute from the input feature is copied to the corresponding output point as-is, plus a `source_type` column recording the original geometry type (`point`, `line`, `polygon`, `multipoint`, `multiline`, or `multipolygon`).

`MULTIPOINT`/`MULTILINESTRING`/`MULTIPOLYGON` input features are fully supported. Each is treated under its family's `-types` category (`MULTIPOINT` under `point`, `MULTILINESTRING` under `line`, `MULTIPOLYGON` under `polygon`) and its centroid is computed as the area- (or length-) weighted combination of each part's own centroid — the same convention used by PostGIS/GEOS/Shapely — so a `MULTIPOLYGON` with a large main body and a small separate piece is pulled toward the larger one rather than averaged with equal weight. `MULTIPOINT` centroids are the simple average of all points. If every part is degenerate (zero total area/length), the calculation falls back to a plain average of all vertices.

**OPTIONS**

| Option | Description |
|---|---|
| `-types <list>` | Types to process: `point,line,polygon` (default: `line,polygon`) — also governs the matching multi-geometry family |
| `-verbose` | Show detailed statistics |

**EXAMPLES**

```
spatial_centroid zones.csv centroids.csv
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

Reduces the number of vertices in lines and polygons while preserving their overall shape. Points are never changed. Attributes are copied unchanged.

Four methods are available with `-method`:

| Method | Parameter | What it does |
|---|---|---|
| `douglas` (default) | `-tolerance` (distance) or `-factor` | Douglas-Peucker. Keeps a vertex when it lies farther than the tolerance from the line that joins its already-kept neighbours. Preserves sharp features (spikes, corners). Same result as GEOS `simplify(tolerance, preserve_topology=False)`. |
| `visvalingam` | `-tolerance` (area) or `-factor` | Visvalingam-Whyatt. Repeatedly removes the vertex whose triangle with its two neighbours has the smallest area, until the smallest area reaches the tolerance. Smooths small features and removes narrow spikes. |
| `radial` | `-tolerance` (distance) | Radial distance. Removes the vertices closer than the tolerance to the last vertex kept. Fast; ignores the shape of the line. |
| `nth` | `-n` | Keeps one vertex out of every `n` (the first and the last are always kept). Fastest; ignores both distances and shape. |

Units follow the CRS of the data: the `douglas` and `radial` tolerance is a distance in CRS units, the `visvalingam` tolerance is an area in CRS units squared. With geographic coordinates (degrees) a tolerance is not a constant distance on the ground; reproject first with `spatial_reproject` and use meters.

`-factor <f>` fixes the fraction of the vertices to remove from every line and ring (`0.7` keeps about 30% of them) and is available for `douglas` and `visvalingam`; it is an alternative to `-tolerance`, not a complement.

**Minimum geometry.** Every line keeps its first and last vertex (at least 2 vertices) and every closed ring at least 3 different vertices (a triangle), so a small polygon or a short line never collapses; a note reports how many were reduced to that minimum. A line whose first and last vertices coincide is treated as a ring. Rings of a polygon (outer ring and holes) and the parts of `MULTILINESTRING`/`MULTIPOLYGON` features are simplified separately.

**Topology.** By default every line and ring is simplified on its own, so two neighbouring polygons can stop sharing exactly the same border (gaps and overlaps) and a street can stop touching the street it joins. With `-topology` the vertices where lines or rings meet (junctions, points where three or more polygons touch, end points of open lines) are kept, and each stretch between two of them is simplified once, so shared borders remain identical in all the features that use them. With `-topology`, `-factor` applies to each stretch.

**Limitations.**

- Neither mode checks that the result is free of self-intersections; with a very large tolerance on shapes with narrow inlets a ring can cross itself. Check the output with `spatial_validate`.
- Polygon holes are simplified ring by ring, but the CSV reader/writer of Spatial TEC keeps all the rings of a `POLYGON` in a single coordinate list.
- Coordinates in the output are written with 6 decimals.

**OPTIONS**

| Option | Description |
|---|---|
| `-method <method>` | `douglas`, `visvalingam`, `radial` or `nth` (default: `douglas`) |
| `-tolerance <value>` | `douglas`/`radial`: distance; `visvalingam`: minimum triangle area. Must be greater than 0 |
| `-factor <value>` | `douglas`/`visvalingam`: fraction of the vertices of each line/ring to remove, `0 < f ≤ 1`. Use instead of `-tolerance` |
| `-n <value>` | `nth`: keep every n-th vertex (whole number ≥ 2) |
| `-types <list>` | Types to simplify: `line,polygon` (default). `point` is accepted but points are never changed |
| `-topology` | Keep shared borders and junctions identical between features |
| `-verbose` | Show detailed statistics by type |
| `-help` | Show the usage |

`douglas` and `visvalingam` need exactly one of `-tolerance` or `-factor`; `radial` needs `-tolerance`; `nth` needs `-n`. Other combinations are rejected.

**EXAMPLES**

```
spatial_simplify rivers.csv rivers_s.csv -tolerance 50
spatial_simplify districts.csv districts_s.csv -tolerance 25 -topology
spatial_simplify coast.csv coast_s.csv -method visvalingam -factor 0.8
spatial_simplify tracks.csv tracks_s.csv -method radial -tolerance 10
spatial_simplify tracks.csv tracks_n.csv -method nth -n 5
spatial_simplify data.csv simplified.csv -tolerance 20 -types polygon -verbose
```

**SEE ALSO**

[spatial_validate](#spatial_validate), [spatial_centroid](#spatial_centroid), [spatial_reproject](projection.md#spatial_reproject)

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

---

## spatial_validate

**NAME**

`spatial_validate` — validate (and optionally repair) the geometry of a vector dataset

**SYNOPSIS**

```
spatial_validate <input> <output> [options]
```

**DESCRIPTION**

Without a full geometry library (GEOS) available, checks each part (ring/line/point) of each feature with a bounded but real set of checks: empty geometry, too few vertices, consecutive duplicate vertices, unclosed rings, self-intersections ("X"-style crossings between non-adjacent segments), and ring orientation (counter-clockwise/CCW is expected). Self-intersection detection does not cover collinear overlaps (two segments overlapping on the same line).

The output file is always written: identical to the input if `-fix` wasn't passed, or with repairs applied if it was. Without `-fix`, issues are reported but the geometry is not modified. Of the detected issues, `unclosed_ring`, `duplicate_vertex`, and `cw_orientation` are repairable with `-fix`; `empty_geometry`, `too_few_vertices`, and `self_intersection` are not (they are only reported).

**OPTIONS**

| Option | Description |
|---|---|
| `-fix` | Attempt to repair invalid geometries (closes rings, removes consecutive duplicate vertices, reverses clockwise rings) |
| `-report <file>` | Save a CSV report of the issues found (columns: `feature_id,geometry_type,part,issue,severity,fixed,message`) |

**EXAMPLES**

```
spatial_validate parcels.csv parcels_valid.csv -fix -report errors.csv
spatial_validate parcels.csv parcels_checked.csv -report errors.csv
```

**SEE ALSO**

[spatial_simplify](#spatial_simplify), [spatial_union](#spatial_union)

---

## spatial_calc_vector

**NAME**

`spatial_calc_vector` — compute or transform attribute columns of a vector CSV via an expression

**SYNOPSIS**

```
spatial_calc_vector <input.csv> <output.csv> -column <name> -expression <expr> [options]
```

**DESCRIPTION**

The vector counterpart to [`spatial_calc`](raster.md#spatial_calc) (which is raster-only): instead of operating cell-by-cell over one or more rasters, it evaluates the given expression row-by-row over a vector CSV's attribute columns, writing the result into a new column (inserted right before the geometry column) or overwriting an existing one.

The expression supports `+ - * / ^`, comparisons (`> < >= <= == !=`), the ternary operator `condition ? yes : no`, parentheses, unary negation, and the functions `sqrt abs sin cos tan log log10 exp pow min max`. Any identifier that isn't a function name is interpreted as the name of an attribute column in the input CSV; if the expression references a column that doesn't exist, the command fails with an error before processing any row.

**OPTIONS**

| Option | Description |
|---|---|
| `-column <name>` | Column to create or overwrite (**required**) |
| `-expression <expr>` | Expression to evaluate per row (**required**) |

**EXAMPLES**

```
spatial_calc_vector cities.csv cities_out.csv -column density -expression "population / area"
spatial_calc_vector cities.csv cities_out.csv -column tier -expression "population > 1000000 ? 1 : 0"
```

**SEE ALSO**

[spatial_calc](raster.md#spatial_calc), [spatial_filter_vector](analysis.md#spatial_filter_vector)
