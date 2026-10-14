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
<<<<<<< HEAD
| `-fuzzy` | Accept street names that are similar but not identical (case and extra spaces are always ignored) |
| `-tolerance <value>` | Minimum name similarity in [0, 1] for `-fuzzy` (default: `0.8`); similarity is `1 - edit distance / length of the longer name` |
| `-verbose` | Print the result of every address |
=======
| `-offset_field <col>` | Column of the addresses file holding a per-address offset (falls back to `-offset`) |
| `-exact` | Exact street-name match (default) |
| `-fuzzy` | Approximate match: abbreviations (`Street`/`St`), case, punctuation and typos |
| `-tolerance <0-1>` | Minimum similarity for `-fuzzy` (default: `0.8`) |
| `-city <name>` | Only use street segments of this city |
| `-postal <code>` | Only use street segments of this postal code |
| `-bbox "x0 y0 x1 y1"` | Only use street segments with a vertex inside the box |
| `-fallback centroid` | If the street exists but the number is out of range, place the point at the street centroid |
| `-reverse_direction` | Reverse the digitizing direction of every street |
| `-include_segment` | Add `street_segment` (segment `id`, or row number) and `measure` (0–1 along the segment) |
| `-include_side` | Add `side` (`left`/`right`) and `offset` (offset applied) |
| `-stats` | Print counts per status and the mean score |
| `-report <file>` | Write a text report with parameters, counts and unresolved addresses |

**OUTPUT**

Columns: the original ones, `status`, `score`, optional columns, `longitude`, `latitude`, `geometry`.

| `status` | Meaning | Coordinates |
|---|---|---|
| `matched` | Street and number found in exactly one segment | yes |
| `ambiguous` | Number found in several equally good segments (first one used) | yes |
| `partial` | Street found but number outside every range | `0 0`, or the centroid with `-fallback centroid` |
| `not_found` | Street not found | `0 0` |

`score` is the name similarity (1 for exact matches); `partial` results get half of it.
>>>>>>> 9612af9c91ae858debce8b24c2c80f22bd6c523e

**EXAMPLES**

```
spatial_address streets.csv addresses.csv result.csv
<<<<<<< HEAD
spatial_address streets.csv addresses.csv result.csv -offset 8 -fuzzy -tolerance 0.7
=======
spatial_address streets.csv addresses.csv result.csv -fuzzy -tolerance 0.7 -stats
spatial_address streets.csv addresses.csv result.csv -include_segment -include_side -report run.txt
>>>>>>> 9612af9c91ae858debce8b24c2c80f22bd6c523e
```

**SEE ALSO**

<<<<<<< HEAD
[spatial_reverse_geocode](#spatial_reverse_geocode), [spatial_address_validate](#spatial_address_validate), [spatial_network](#spatial_network), [spatial_lrs_locate](#spatial_lrs_locate)
=======
[spatial_address_clean](#spatial_address_clean), [spatial_address_validate](#spatial_address_validate), [spatial_reverse_geocode](#spatial_reverse_geocode), [spatial_network](#spatial_network), [spatial_lrs_locate](#spatial_lrs_locate)

---

## spatial_address_clean

**NAME**

`spatial_address_clean` — clean and standardize an address file before geocoding

**SYNOPSIS**

```
spatial_address_clean <input.csv> <output.csv> [options]
```

**DESCRIPTION**

Reads an addresses file (`id`, `house_number`, `street_name`; optional `city`, `postal_code`) and writes a cleaned copy. All other columns are preserved. At least one of the options below is required; they are applied in the order listed.

**OPTIONS**

| Option | Description |
|---|---|
| `-standardize` | Trim and collapse spaces, drop stray periods, capitalize street and city, turn `#150` / ` 150 ` into `150`, remove spaces inside postal codes |
| `-standardize_streets` | Unify street types and cardinal points (`Street`→`St`, `Avenue`→`Ave`, `Road`→`Rd`, `Boulevard`→`Blvd`, `Drive`→`Dr`, `Lane`→`Ln`, `Court`→`Ct`, `Place`→`Pl`, `Highway`→`Hwy`, `North`→`N`, …) |
| `-validate` | Remove incomplete rows (no street, no valid house number, empty `id`) |
| `-rejects <file>` | With `-validate`, write the removed rows to this file |

**EXAMPLES**

```
spatial_address_clean addresses_raw.csv addresses_clean.csv -standardize
spatial_address_clean addresses_raw.csv addresses_ok.csv -standardize -standardize_streets -validate -rejects rejected.csv
```

**SEE ALSO**

[spatial_address](#spatial_address)

---

## spatial_address_validate

**NAME**

`spatial_address_validate` — validate street address ranges and geocoding accuracy

**SYNOPSIS**

```
spatial_address_validate <streets.csv> [options]
spatial_address_validate <result.csv> -check_accuracy -reference <truth.csv>
```

**OPTIONS**

| Option | Description |
|---|---|
| `-check_overlaps` | Detect overlapping numbering ranges on the same street side |
| `-check_gaps` | Detect gaps in the numbering of a street side |
| `-check_ranges` | Detect inconsistent ranges: mixed parity at the ends of a side (`PARITY_MISMATCH`), same parity on both sides (`SAME_PARITY_SIDES`), negative values, segments without range (`NO_RANGE`) or without valid geometry (`BAD_GEOMETRY`) |
| `-fix <out.csv>` | Write a copy of the streets file with inverted ranges (`from > to`) swapped |
| `-report <file>` | Write the issues found as CSV (or, with `-check_accuracy`, the accuracy summary) |
| `-check_accuracy` | Compare a `spatial_address` result with a reference file |
| `-reference <file>` | Reference CSV with `id` and `longitude`,`latitude` (or a `POINT` geometry) |

Inverted ranges are always reported. Exit code: `0` no issues, `2` issues found, `1` error. With `-check_accuracy` the tool prints mean, median, RMSE and maximum error, in the units of the coordinates, for the ids present in both files (unlocated results are skipped).

**EXAMPLES**

```
spatial_address_validate streets.csv -check_overlaps -check_gaps -report issues.csv
spatial_address_validate streets.csv -check_ranges -fix streets_fixed.csv
spatial_address_validate result.csv -check_accuracy -reference ground_truth.csv
```

**SEE ALSO**

[spatial_address](#spatial_address)
>>>>>>> 9612af9c91ae858debce8b24c2c80f22bd6c523e

---

## spatial_reverse_geocode

**NAME**

<<<<<<< HEAD
`spatial_reverse_geocode` — find the approximate street address of points
=======
`spatial_reverse_geocode` — estimate the street address of one or more points
>>>>>>> 9612af9c91ae858debce8b24c2c80f22bd6c523e

**SYNOPSIS**

```
spatial_reverse_geocode <streets.csv> <points.csv> <output.csv> [options]
```

**DESCRIPTION**

<<<<<<< HEAD
The inverse of `spatial_address`. Each point is snapped to the nearest street segment, the side of the street (left or right of the digitizing direction) is taken from the position of the point, and the house number is interpolated from the range of that side, rounded to a number with the right parity.

The streets file is the same as for `spatial_address`. The points file must contain a `geometry` column (POINT WKT) or `x`,`y` columns (also `longitude`,`latitude`), and optionally `id`; any other column is copied to the output.

The output adds `status` (`found`, `not_found` when the nearest street is farther than `-max_dist`, `invalid` when the point has no readable coordinates), `street_name`, `house_number`, `side`, `city`, `postal_code`, `distance` (point to street), `measure`, `street_segment`, `snapped_x`, `snapped_y`, `x`, `y` and `geometry` (the original point).
=======
The inverse of [spatial_address](#spatial_address). For each query point it finds the nearest street segment, projects the point onto it and interpolates the house number within the range of the side the point lies on (left or right of the digitizing direction). The number respects the parity of the side's range: on a side numbered 101–199 the result is always odd.

The streets file is the same one used by `spatial_address` (`street_name`, `left_from`, `left_to`, `right_from`, `right_to`, `geometry`; optional `id`, `city`, `postal_code`). The points file needs an `id` and either `longitude`,`latitude` (or `x`,`y`) columns or a `POINT` `geometry`.
>>>>>>> 9612af9c91ae858debce8b24c2c80f22bd6c523e

**OPTIONS**

| Option | Description |
|---|---|
<<<<<<< HEAD
| `-max_dist <value>` | Maximum distance from the point to a street (default: `50.0`) |
| `-verbose` | Print the result of every point |
=======
| `-max_dist <value>` | Maximum point-to-street distance (default: `50.0`, in coordinate units) |
| `-addresses <file>` | A `spatial_address` result: adds the nearest already-geocoded address (only rows with status `matched`/`ambiguous`) within `-max_dist` |

**OUTPUT**

Columns: `id`, `longitude`, `latitude`, `status`, `house_number`, `street_name`, `city`, `postal_code`, `side`, `distance` (point to street), `street_segment` (segment `id`, or row number), `measure` (0–1 along the segment), optionally `nearest_address_id`, `nearest_address`, `nearest_address_distance`, and `geometry` (the query point).

| `status` | Meaning |
|---|---|
| `matched` | The side where the point lies has a numbering range |
| `approximate` | The segment only declares the range of the opposite side, which was used |
| `too_far` | No street within `-max_dist`; address columns are empty |

The estimate is exact only for a point produced by `spatial_address`. Near an intersection or at the joint between two segments of the same street the nearest segment can be ambiguous (the first one wins on ties), and in real data the result is the *probable* number, not a verified postal address.
>>>>>>> 9612af9c91ae858debce8b24c2c80f22bd6c523e

**EXAMPLES**

```
spatial_reverse_geocode streets.csv points.csv result.csv
<<<<<<< HEAD
spatial_reverse_geocode streets.csv points.csv result.csv -max_dist 20
=======
spatial_reverse_geocode streets.csv points.csv result.csv -max_dist 20 -addresses geocoded.csv
>>>>>>> 9612af9c91ae858debce8b24c2c80f22bd6c523e
```

**SEE ALSO**

<<<<<<< HEAD
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
=======
[spatial_address](#spatial_address)
>>>>>>> 9612af9c91ae858debce8b24c2c80f22bd6c523e

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

Reads a `.lrs` file created by `spatial_lrs_create` and reports the CRS, the number of routes and vertices, the total length, the bounding box, and, for each route, its vertex count, length and measure range (M-range). With `-route`, it lists every vertex of that route with its measure.

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

Measure and distance are compared with a tolerance of `0.001`. A network created with `spatial_lrs_create` always passes validation; the checks are useful for `.lrs` files that were edited by hand or produced by other tools.

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

[spatial_lrs_create](#spatial_lrs_create), [spatial_lrs_locate](#spatial_lrs_locate), [spatial_lrs_segment](#spatial_lrs_segment)

---

