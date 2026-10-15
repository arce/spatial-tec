# Networks, Linear Referencing & Geocoding

Tools for building routable networks from street data, finding routes, working with Linear Referencing Systems (LRS), and geocoding addresses.

> Syntax conventions: `<argument>` is required, `[optional]` is optional, `...` means repetition. Options use a single dash (`-option`).

---

## spatial_address

**NAME**

`spatial_address` — geocode addresses by street interpolation

**SYNOPSIS**

```
spatial_address <streets.csv> <addresses.csv> <output.csv> [options]
```

**DESCRIPTION**

Locates postal addresses along street geometry by linearly interpolating the house number within the ranges declared on each street segment. The point is placed on the side of the street whose range contains the number (left or right of the digitizing direction) and shifted laterally by `-offset`. When both sides cover the number, the parity decides: each range is even, odd or both, inferred from its end numbers or taken from the optional `left_parity`/`right_parity` columns.

The streets file must contain: `street_name`, `left_from`, `left_to`, `right_from`, `right_to`, `geometry` (optional: `left_parity`, `right_parity`, `city`, `postal_code`). Ranges may be descending.

The addresses file must contain: `id`, `house_number`, `street_name` (optional: `city`, `postal_code`; any other column is copied to the output). `city` and `postal_code` only filter when both the address and the street provide a value.

The output keeps the address columns and adds `status`, `score`, `side`, `street_segment` (row of the street file used), `measure` (distance along the street segment), `offset` (signed lateral shift, positive to the left), `longitude`, `latitude` and `geometry`.

| `status` | Meaning |
|---|---|
| `matched` | street and number found; the point is in `geometry` |
| `partial` | street found, but the number is outside every range (or has the wrong parity) |
| `not_found` | no street with that name |
| `invalid` | missing street name or non-numeric house number |

`score` is the similarity of the street name (1.00 for an exact match). Addresses that are not `matched` have an empty geometry (`POINT EMPTY`), so they are skipped by `spatial_csv2geo`.

Each side of a segment only accepts numbers of the parity of its range when both ends of the range share it (a side declared `100-198` takes even numbers, `101-199` odd ones). Left-side ranges are placed to the left of the digitizing direction and right-side ranges to the right.

`city` and `postal_code` only discard a street segment when **both** files declare a value and the values differ. If the streets file has no such columns, they are ignored.

Street names are compared case-insensitively. By default the match is exact (`Main Street` does not match `Main St`); use `-fuzzy` or run `spatial_address_clean -standardize_streets` first.

**OPTIONS**

| Option | Description |
|---|---|
| `-offset <value>` | Lateral offset from the centerline (default: `5.0`) |
| `-fuzzy` | Accept street names that are similar but not identical (case and extra spaces are always ignored) |
| `-tolerance <value>` | Minimum name similarity in [0, 1] for `-fuzzy` (default: `0.8`); similarity is `1 - edit distance / length of the longer name` |
| `-verbose` | Print the result of every address |

**EXAMPLES**

```
spatial_address streets.csv addresses.csv result.csv
spatial_address streets.csv addresses.csv result.csv -offset 8 -fuzzy -tolerance 0.7
```

**SEE ALSO**

[spatial_reverse_geocode](#spatial_reverse_geocode), [spatial_address_validate](#spatial_address_validate), [spatial_network](#spatial_network), [spatial_lrs_locate](#spatial_lrs_locate)

---

## spatial_reverse_geocode

**NAME**

`spatial_reverse_geocode` — find the approximate street address of points

**SYNOPSIS**

```
spatial_reverse_geocode <streets.csv> <points.csv> <output.csv> [options]
```

**DESCRIPTION**

The inverse of `spatial_address`. Each point is snapped to the nearest street segment, the side of the street (left or right of the digitizing direction) is taken from the position of the point, and the house number is interpolated from the range of that side, rounded to a number with the right parity.

The streets file is the same as for `spatial_address`. The points file must contain a `geometry` column (POINT WKT) or `x`,`y` columns (also `longitude`,`latitude`), and optionally `id`; any other column is copied to the output.

The output adds `status` (`found`, `not_found` when the nearest street is farther than `-max_dist`, `invalid` when the point has no readable coordinates), `street_name`, `house_number`, `side`, `city`, `postal_code`, `distance` (point to street), `measure`, `street_segment`, `snapped_x`, `snapped_y`, `x`, `y` and `geometry` (the original point).

**OPTIONS**

| Option | Description |
|---|---|
| `-max_dist <value>` | Maximum distance from the point to a street (default: `50.0`) |
| `-verbose` | Print the result of every point |

**EXAMPLES**

```
spatial_reverse_geocode streets.csv points.csv result.csv
spatial_reverse_geocode streets.csv points.csv result.csv -max_dist 20
```

**SEE ALSO**

[spatial_address](#spatial_address), [spatial_address_validate](#spatial_address_validate)

---

## spatial_address_validate

**NAME**

`spatial_address_validate` — validate the numbering ranges of a street layer

**SYNOPSIS**

```
spatial_address_validate <streets.csv> [options]
```

**DESCRIPTION**

Checks the numbering ranges of a street layer before geocoding. Segments are grouped by `street_name`, `city` and `postal_code`, and each issue is reported with the row of the street file (counted from 1, first data row after the header). Without any `-check_*` option, all checks are run.

| Issue | Severity | Check | Meaning |
|---|---|---|---|
| `EMPTY_NAME` | error | ranges | segment without street name |
| `BAD_GEOMETRY` | error | ranges | geometry with fewer than 2 vertices |
| `NO_RANGE` | error | ranges | no numbering on either side |
| `NEGATIVE_NUMBER` | error | ranges | negative number in a range |
| `PARITY_MISMATCH` | error | ranges | range ends do not match the declared `left_parity`/`right_parity` |
| `DESCENDING_RANGE` | warning | ranges | `from` is greater than `to` |
| `SAME_PARITY_SIDES` | warning | ranges | both sides use the same parity, so the sides cannot be told apart |
| `OVERLAP` | error | overlaps | numbers covered by two segments of the same street |
| `GAP` | warning | gaps | numbers of a street not covered by any segment |

Even and odd numbers are checked separately, so the usual left 100–198 / right 101–199 numbering produces neither overlaps nor gaps.

**OPTIONS**

| Option | Description |
|---|---|
| `-check_ranges` | Check each range on its own |
| `-check_overlaps` | Detect overlapping ranges between segments of the same street |
| `-check_gaps` | Detect gaps in the numbering of a street |
| `-report <file>` | Save the issues as a CSV (`row,street_name,severity,issue,detail`) |

**EXIT STATUS**

`0` if no errors were found (warnings are allowed); `1` on a usage or I/O error, or when errors were found.

**EXAMPLES**

```
spatial_address_validate streets.csv
spatial_address_validate streets.csv -check_overlaps -check_gaps -report issues.csv
```

**SEE ALSO**

[spatial_address](#spatial_address), [spatial_reverse_geocode](#spatial_reverse_geocode)

---

## spatial_network

**NAME**

`spatial_network` — build a routable network from street lines

**SYNOPSIS**

```
spatial_network <streets.csv> <output.network> [options]
```

**DESCRIPTION**

Builds a graph (nodes and edges) from a layer of street lines, suitable for route analysis.

**OPTIONS**

| Option | Description |
|---|---|
| `-speed <value>` | Default speed in km/h (default: `50`) |
| `-crs <value>` | Coordinate system (default: `EPSG:4326`) |
| `-oneway` | Treat edges as one-way |

**OUTPUT**

`<output.network>`: single file containing all network data.

**EXAMPLES**

```
spatial_network streets.csv roads.network -speed 40
```

**SEE ALSO**

[spatial_shortest_path](#spatial_shortest_path), [spatial_lrs_create](#spatial_lrs_create)

---

## spatial_shortest_path

**NAME**

`spatial_shortest_path` — find the shortest path between two nodes

**SYNOPSIS**

```
spatial_shortest_path <network.network> -from <origin> -to <destination> [options]
```

**DESCRIPTION**

Computes the lowest-cost path between two nodes of a network built with `spatial_network`.

**OPTIONS**

| Option | Description |
|---|---|
| `-from <node>` | Origin node (ID or name) |
| `-to <node>` | Destination node (ID or name) |
| `-output <file>` | Output CSV file with the path (default: `path.csv`) |
| `-verbose` | Show detailed path information |

**EXAMPLES**

```
spatial_shortest_path roads.network -from 0 -to 10
spatial_shortest_path roads.network -from "San Jose" -to "Alajuela"
spatial_shortest_path roads.network -from 0 -to 10 -output route.csv -verbose
```

**SEE ALSO**

[spatial_network](#spatial_network)

---

## spatial_lrs_create

**NAME**

`spatial_lrs_create` — create a Linear Referencing System (LRS) network

**SYNOPSIS**

```
spatial_lrs_create <input.csv> <output.lrs> [options]
```

**DESCRIPTION**

Builds a Linear Referencing System from routes represented as lines, assigning measures (M-values) along each route.

The input CSV must contain: `route_id` (unique route identifier), `geometry` (LINESTRING WKT); optionally `from_measure`, `to_measure` for custom measures.

**OPTIONS**

| Option | Description |
|---|---|
| `-crs <value>` | Coordinate system (default: `EPSG:4326`) |
| `-verbose` | Show detailed information |

**EXAMPLES**

```
spatial_lrs_create streets.csv lrs_network.lrs -crs EPSG:5367
```

**SEE ALSO**

[spatial_lrs_locate](#spatial_lrs_locate), [spatial_lrs_segment](#spatial_lrs_segment)

---

## spatial_lrs_locate

**NAME**

`spatial_lrs_locate` — locate point events along an LRS network

**SYNOPSIS**

```
spatial_lrs_locate <network.lrs> <events.csv> <output.csv> [options]
```

**DESCRIPTION**

Projects points (events) onto the nearest route of an LRS network and computes their measure (M-value) along that route.

The events CSV must contain: `id`, `geometry` (POINT WKT or `x`,`y` columns), `route_id`.

**OPTIONS**

| Option | Description |
|---|---|
| `-max_dist <value>` | Maximum distance to locate the event (default: `0.01`) |
| `-route_id <col>` | Column name for the route ID (default: `route_id`) |
| `-verbose` | Show detailed information |

**EXAMPLES**

```
spatial_lrs_locate lrs_network.lrs accidents.csv located_accidents.csv
```

**SEE ALSO**

[spatial_lrs_create](#spatial_lrs_create), [spatial_lrs_segment](#spatial_lrs_segment)

---

## spatial_lrs_segment

**NAME**

`spatial_lrs_segment` — create segments from linear events

**SYNOPSIS**

```
spatial_lrs_segment <network.lrs> <events.csv> <output.csv> [options]
```

**DESCRIPTION**

Generates line segment geometry from linear events defined by a measure range (`from_measure`/`to_measure`) over an LRS network.

The events CSV must contain: `id`, `route_id`, `from_measure`, `to_measure`; optionally `offset`.

**OPTIONS**

| Option | Description |
|---|---|
| `-offset <value>` | Default lateral offset |
| `-merge` | Merge overlapping/adjacent segments |
| `-verbose` | Show detailed information |

**EXAMPLES**

```
spatial_lrs_segment lrs_network.lrs conditions.csv segments.csv -offset 3
spatial_lrs_segment lrs_network.lrs conditions.csv segments.csv -merge
```

**SEE ALSO**

[spatial_lrs_create](#spatial_lrs_create), [spatial_lrs_locate](#spatial_lrs_locate)


---

## spatial_lrs_info

**NAME**

`spatial_lrs_info` — inspect and validate an LRS network

**SYNOPSIS**

```
spatial_lrs_info <network.lrs> [options]
```

**DESCRIPTION**

Reads a `.lrs` file created by `spatial_lrs_create` and reports the CRS, the number of routes and vertices, the total length, the bounding box, and, for each route, its vertex count, length and measure range (M-range). When the network contains routes calibrated with `spatial_lrs_calibrate`, the table also has a `Calibrated` column. With `-route`, it lists every vertex of that route with its measure.

With `-validate`, it also checks the M-values of every route and prints the issues found. Issues are errors (the exit code becomes `1`) or warnings.

| Check | Severity | Meaning |
|---|---|---|
| `DUPLICATE_ROUTE` | error | `route_id` appears more than once |
| `TOO_FEW_VERTICES` | error | route has fewer than 2 vertices |
| `NEGATIVE_MEASURE` | error | a vertex has a negative measure |
| `MEASURE_OVERLAP` | error | measure does not increase between two vertices (decreases or repeats) |
| `MEASURE_GAP` | error | measure grows more than the distance between two vertices |
| `MEASURE_SHORT` | error | measure grows less than the distance between two vertices |
| `LENGTH_MISMATCH` | error | last measure differs from the declared route length |
| `START_NOT_ZERO` | warning | first measure is not 0 |
| `ZERO_LENGTH_SEGMENT` | warning | two consecutive vertices share the same position |

On calibrated routes the ratio between measure and distance changes between calibration points, so `MEASURE_GAP`, `MEASURE_SHORT`, `LENGTH_MISMATCH` and `START_NOT_ZERO` are not checked there (the measure must still increase), and the length shown is that of the geometry. Measure and distance are compared with a tolerance of `0.001`. A network created with `spatial_lrs_create` always passes validation; the checks are useful for `.lrs` files that were edited by hand or produced by other tools.

**OPTIONS**

| Option | Description |
|---|---|
| `-route <id>` | Show the detail (every vertex and its measure) of a single route |
| `-validate` | Also validate geometry and M-values |
| `-report <file>` | With `-validate`, save the issues as a CSV (`route_id,vertex,severity,issue,detail`) |

**EXIT STATUS**

`0` if the file was read and no validation errors were found; `1` on a usage or I/O error, an unknown route, or validation errors.

**EXAMPLES**

```
spatial_lrs_info roads.lrs
spatial_lrs_info roads.lrs -route 101
spatial_lrs_info roads.lrs -validate -report lrs_errors.csv
```

**SEE ALSO**

[spatial_lrs_create](#spatial_lrs_create), [spatial_lrs_locate](#spatial_lrs_locate), [spatial_lrs_segment](#spatial_lrs_segment), [spatial_lrs_calibrate](#spatial_lrs_calibrate)

---

## spatial_lrs_calibrate

**NAME**

`spatial_lrs_calibrate` — calibrate the measures of an LRS network using points of known measure

**SYNOPSIS**

```
spatial_lrs_calibrate <network.lrs> <points.csv> <output.lrs> [options]
```

**DESCRIPTION**

The measures written by `spatial_lrs_create` are distances along the digitized geometry. Real roads use the kilometers marked by mileposts, which rarely match that distance. `spatial_lrs_calibrate` adjusts the measures of each route using calibration points whose true measure is known.

Each point is projected onto its route and a vertex is created at the projection (unless it coincides with an existing vertex) carrying the measure of the point. The remaining vertices get measures interpolated in proportion to the distance along the route between the two surrounding calibration points. Before the first point and after the last one the slope (measure per unit of distance) of the nearest calibrated interval is used; with a single point the slope is 1.

The points file needs a route column, a measure column and the position of each point, either as a `geometry` column (WKT `POINT`) or as `x` and `y` columns. The measures of the points of a route must increase along the route; a route that violates this is not calibrated and is reported, and the command exits with `1` after processing the other routes. Routes without points are copied unchanged. Calibrated routes are listed in a `@CALIBRATED` section of the output file.

**OPTIONS**

| Option | Description |
|---|---|
| `-max_dist <value>` | Maximum distance from a point to its route; farther points are ignored with a warning (default `1.0`) |
| `-route_id <col>` | Name of the route column in the points file (default `route_id`) |
| `-measure <col>` | Name of the measure column in the points file (default `measure`) |
| `-verbose` | Show the result for every route |

**EXIT STATUS**

`0` if every route with points was calibrated; `1` on a usage or I/O error, if no route could be calibrated, or if some route could not be calibrated.

**EXAMPLES**

```
spatial_lrs_calibrate roads.lrs mileposts.csv roads_km.lrs -max_dist 5
spatial_lrs_calibrate roads.lrs mileposts.csv roads_km.lrs -measure km -verbose
```

**SEE ALSO**

[spatial_lrs_create](#spatial_lrs_create), [spatial_lrs_info](#spatial_lrs_info), [spatial_lrs_locate](#spatial_lrs_locate)

---

