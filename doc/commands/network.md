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

Locates postal addresses along street geometry by linearly interpolating the house number within the ranges declared on each street segment.

The streets file must contain: `street_name`, `left_from`, `left_to`, `right_from`, `right_to`, `geometry` (optional: `city`, `postal_code`).

The addresses file must contain: `id`, `house_number`, `street_name` (optional: `city`, `postal_code`).

`city` and `postal_code` only discard a street segment when **both** files declare a value and the values differ. If the streets file has no such columns, they are ignored.

Street names are compared case-insensitively. By default the match is exact (`Main Street` does not match `Main St`); use `-fuzzy` or run `spatial_address_clean -standardize_streets` first.

**OPTIONS**

| Option | Description |
|---|---|
| `-offset <value>` | Lateral offset from the centerline (default: `5.0`) |
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

**EXAMPLES**

```
spatial_address streets.csv addresses.csv result.csv
spatial_address streets.csv addresses.csv result.csv -fuzzy -tolerance 0.7 -stats
spatial_address streets.csv addresses.csv result.csv -include_segment -include_side -report run.txt
```

**SEE ALSO**

[spatial_address_clean](#spatial_address_clean), [spatial_address_validate](#spatial_address_validate), [spatial_network](#spatial_network), [spatial_lrs_locate](#spatial_lrs_locate)

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

