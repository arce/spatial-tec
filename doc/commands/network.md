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

Builds a graph (nodes and edges) from a layer of street lines, suitable for route analysis with `spatial_shortest_path`, `spatial_network_info` and `spatial_network_service`.

Every vertex of a `LINESTRING` becomes a node and every straight piece between two consecutive vertices becomes an edge. Vertices at the same position (or closer than `-snap`) are the same node, which is what connects two streets. `MULTILINESTRING` features are split into their parts, so the end of one part is never joined to the start of the next. Features that are not lines are ignored with a warning, and segments that collapse to zero length or repeat another segment are dropped (of two repeated segments, the faster one is kept).

Streets that cross without sharing a vertex are **not** connected, as happens at an overpass. With `-split`, a node is added wherever two streets cross, or where the end of one street lies on another (a T junction), and both streets are cut there. Use it only when the layer has no overpasses or tunnels.

**Checking a street layer.** `-report <file>` lists every place where streets are not properly joined, with or without `-split`, so the layer can be corrected at the source:

| `issue` | Meaning |
|---|---|
| `CROSSING` | two streets cross without a shared vertex |
| `T_JUNCTION` | the end of street A lies on the interior of street B (`row_a` is A) |
| `OVERLAP` | two streets run over each other; one row per pair of streets, with the number of overlapping pieces and the total length in metres |
| `SNAPPED_VERTEX` | with `-snap`, a vertex was moved to join an existing node (a gap smaller than the tolerance); the distance moved is in `detail` |

The CSV columns are `issue,x,y,row_a,row_b,id_a,id_b,action,detail`. `x,y` is the position (the middle of the overlap, for `OVERLAP`); `row_a,row_b` are the rows of the streets file, counted from 1 below the header; `id_a,id_b` are the values of its `id` column, if it has one. `action` says what the build did about it: `split` (with `-split`), `merged` (snapped vertices) or `none`. Shared end points are not findings: they are the normal way to connect two streets.

To find the streets that still do not connect, run `spatial_network_info -validate` on the result: each remaining `DISCONNECTED_COMPONENT` is a real gap, island or dead end, and its position is shown.

Edge costs are computed when the network is built:

- `length` is in metres: great-circle distance (haversine) when the CRS is geographic, and the Euclidean length times `-unit_scale` when it is projected. If the CRS is geographic but the coordinates are outside ±180/±90, the command stops and asks for `-crs`.
- `travel_time` is in minutes: `length / 1000 / speed * 60`.

The CRS is taken from `-crs`, otherwise from a `# CRS:` line in the streets file, otherwise `EPSG:4326`. Each street may carry its own values in these optional columns (names are case-insensitive):

| Column | Meaning |
|---|---|
| `speed`, `maxspeed` | Speed in km/h (`50`, `50 km/h` or `30 mph`); unreadable values use `-speed` |
| `oneway` | `yes`, `true`, `1`, `forward` or `ft`: only in the digitizing direction. `-1`, `reverse`, `backward` or `tf`: only against it. `no`, `false`, `0`, `both` or `b`: two-way. Anything else uses the default |

`-oneway` makes every street one-way (in the digitizing direction) when it has no `oneway` value.

**OPTIONS**

| Option | Description |
|---|---|
| `-speed <value>` | Default speed in km/h (default: `50`) |
| `-crs <value>` | Coordinate system (default: the one in the streets file, else `EPSG:4326`) |
| `-oneway` | Treat streets as one-way unless a `oneway` column says otherwise |
| `-snap <dist>` | Merge vertices closer than `<dist>`, in CRS units (default: exact match) |
| `-split` | Add a node where streets cross or touch without sharing a vertex |
| `-report <file>` | Save a CSV with every crossing, T junction, overlap and snapped vertex found (see above) |
| `-names <points.csv>` | Name the nodes: each point of the file with a `name` (or `nombre`) column is assigned to its nearest node |
| `-names_tol <dist>` | Ignore named points farther than `<dist>` (CRS units) from every node |
| `-unit_scale <value>` | Metres per CRS unit when the CRS is projected (default: `1`) |
| `-centrality` | Also compute betweenness and closeness centrality (slow on large networks) |
| `-verbose` | Show the columns and tolerances used |

**OUTPUT**

`<output.network>` is a text file with nodes and edges. All-pairs distance matrices are no longer stored, so the file grows with the size of the network and not with its square; routes are computed on demand.

| Section | Columns |
|---|---|
| `@META` | `version`, `crs`, `length_unit` (`meters`) |
| `@NODES` | `node_id,x,y,name,degree,degree_centrality,betweenness,closeness` |
| `@EDGES` | `edge_id,source,target,length,travel_time,speed,oneway` |

`degree_centrality` is `degree / (nodes - 1)`. `betweenness` and `closeness` are filled only with `-centrality` (empty otherwise): betweenness is the fraction of shortest paths between other node pairs that go through the node, and closeness is `(r / (n - 1)) * (r / sum of distances)` over the `r` reachable nodes, with distances in metres.

Files written by earlier versions (without `@META`, with distance matrices) can still be read by the other commands; their lengths stay in CRS units.

**EXIT STATUS**

`0` on success; `1` on a usage or I/O error, or if no usable streets were found.

**EXAMPLES**

```
spatial_network streets.csv roads.network -speed 40
spatial_network streets.csv roads.network -crs EPSG:5367 -split -names towns.csv -names_tol 200
spatial_network streets.csv check.network -snap 0.5 -report problems.csv
spatial_network_info check.network -validate
```

**SEE ALSO**

[spatial_shortest_path](#spatial_shortest_path), [spatial_network_info](#spatial_network_info), [spatial_network_service](#spatial_network_service), [spatial_lrs_create](#spatial_lrs_create)

---

## spatial_shortest_path

**NAME**

`spatial_shortest_path` — find the shortest path between two nodes

**SYNOPSIS**

```
spatial_shortest_path <network.network> -from <origin> -to <destination> [options]
```

**DESCRIPTION**

Computes the lowest-cost path between two nodes of a network built with `spatial_network`, using Dijkstra's algorithm over the nodes and edges (one-way edges can only be travelled in their direction).

A node is given by its ID or by its name (see `-names` in `spatial_network`). An ID is tried first, then an exact name, then part of a name, all ignoring case; if several nodes match, the command lists them and stops.

The output CSV has one row with `id`, `node_start`, `node_end`, `total_cost` (in the unit of `-cost`), `total_length` (m), `total_time` (min), `node_count` and `geometry` (a `LINESTRING` through every node of the path, a `POINT` if origin and destination are the same node), so it can be drawn with `spatial_csv2geo`.

**OPTIONS**

| Option | Description |
|---|---|
| `-from <node>` | Origin node (ID or name) |
| `-to <node>` | Destination node (ID or name) |
| `-cost <kind>` | Cost to minimise: `length` (default) or `time`. The fastest route is not always the shortest one |
| `-output <file>` | Output CSV file with the path (default: `path.csv`) |
| `-verbose` | Show the nodes of the path and the length and time of every segment |

**EXIT STATUS**

`0` if a path was found and written; `1` on a usage or I/O error, an unknown or ambiguous node, or when there is no path (for example, disconnected parts, or one-way edges in the way).

**EXAMPLES**

```
spatial_shortest_path roads.network -from 0 -to 10
spatial_shortest_path roads.network -from "San Jose" -to "Alajuela" -cost time
spatial_shortest_path roads.network -from 0 -to 10 -output route.csv -verbose
```

**SEE ALSO**

[spatial_network](#spatial_network), [spatial_network_service](#spatial_network_service), [spatial_tsp](#spatial_tsp)

---

## spatial_network_info

**NAME**

`spatial_network_info` — inspect and validate a network

**SYNOPSIS**

```
spatial_network_info <network.network> [options]
```

**DESCRIPTION**

Reads a `.network` file and reports its CRS, the number of nodes and edges (and how many are one-way), the bounding box, the total and per-edge length, the node degree distribution and the number of connected components. With `-verbose`, it also lists every component.

With `-validate`, it checks the integrity and connectivity of the network and prints the issues found. Issues are errors (the exit code becomes `1`) or warnings.

| Check | Severity | Meaning |
|---|---|---|
| `BAD_ROW` | error | a node or edge row could not be read (skipped) |
| `DUPLICATE_NODE_ID` | error | `node_id` appears more than once |
| `UNKNOWN_NODE` | error | an edge refers to a node that does not exist (skipped) |
| `BAD_COST` | error | an edge has a negative or non-numeric `length` or `travel_time` |
| `ISOLATED_NODE` | warning | node without edges (its position is given) |
| `DISCONNECTED_COMPONENT` | warning | group of nodes not connected to the main (largest) network; an example node and its position are given |
| `NOT_STRONGLY_CONNECTED` | warning | inside the main network, one-way edges leave nodes that cannot reach each other |
| `COINCIDENT_NODES` | warning | two nodes at the same position (a missing `-snap`) |
| `DUPLICATE_EDGE` | warning | two edges join the same nodes in the same way |
| `SELF_LOOP` | warning | an edge starts and ends at the same node |
| `ZERO_LENGTH_EDGE` | warning | an edge of length 0 |

**OPTIONS**

| Option | Description |
|---|---|
| `-validate` | Also validate the network |
| `-report <file>` | With `-validate`, save the issues as a CSV (`severity,issue,element,id,detail`) |
| `-verbose` | List every component and every issue (otherwise at most 10 per type are printed) |

**EXIT STATUS**

`0` if the file was read and no validation errors were found; `1` on a usage or I/O error, or validation errors.

**EXAMPLES**

```
spatial_network_info roads.network
spatial_network_info roads.network -validate -report issues.csv
```

**SEE ALSO**

[spatial_network](#spatial_network), [spatial_shortest_path](#spatial_shortest_path), [spatial_lrs_info](#spatial_lrs_info)

---

## spatial_network_service

**NAME**

`spatial_network_service` — service areas and isochrones from a source node

**SYNOPSIS**

```
spatial_network_service <network.network> -from <node> -mode <alloc|iso> [options]
```

**DESCRIPTION**

Finds what can be reached from a node of the network within a given cost, following the direction of one-way edges. The cost is travel time in minutes (default) or length in metres (`-cost length`). The origin is an ID or a name, as in `spatial_shortest_path`.

In `-mode alloc`, the output lists the street segments under the cost limit `-max_cost` (all the reachable network if it is not given). A segment that is only partly within the limit is cut at the point where the cost runs out. Its columns are `edge_id`, `source`, `target`, `cost_min` and `cost_max` (the lowest and highest accumulated cost along the piece), `length` (of the piece, in metres), `partial` (`1` when it was cut) and `geometry` (`LINESTRING`).

In `-mode iso`, one polygon is written for every value of `-intervals`, each containing everything reachable within that cost, so the polygons are nested. The polygon is the area within `-buffer` of the reached streets (including the cut pieces), traced on a grid of cells one third of the buffer wide, so its outline is a step approximation. The columns are `interval`, `cost_limit`, `reached_nodes`, `reached_edges`, `network_length` (m) and `geometry` (`POLYGON`, or `MULTIPOLYGON` when the area has several parts). Blocks surrounded by reached streets appear as holes in the polygon unless the buffer is large enough to close them.

By default the buffer is about 1/60 of the diagonal of the area reached by the largest interval, and it is the same for all intervals; use `-buffer` (in CRS units, so degrees on a geographic network) for a tighter or looser fit.

**OPTIONS**

| Option | Description |
|---|---|
| `-from <node>` | Origin node, ID or name (required) |
| `-mode <alloc\|iso>` | Service mode (required) |
| `-cost <kind>` | `time` (minutes, default) or `length` (metres) |
| `-max_cost <value>` | Maximum accumulated cost (mode `alloc`; default: no limit) |
| `-intervals <list>` | Comma-separated cost breaks, e.g. `5,10,15` (mode `iso`; required) |
| `-buffer <dist>` | Mode `iso`: how far the polygons extend around the reached streets, in CRS units |
| `-output <file>` | Output CSV file (default: `service_result.csv`) |
| `-verbose` | Show more details |

**EXIT STATUS**

`0` on success; `1` on a usage or I/O error, or an unknown or ambiguous origin node.

**EXAMPLES**

```
spatial_network_service roads.network -from 0 -mode alloc -max_cost 10 -output service_area.csv
spatial_network_service roads.network -from "San Jose" -mode iso -intervals 5,10,15 -output isochrones.csv
spatial_network_service roads.network -from 0 -mode alloc -cost length -max_cost 500
```

**SEE ALSO**

[spatial_network](#spatial_network), [spatial_shortest_path](#spatial_shortest_path), [spatial_network_info](#spatial_network_info), [spatial_tsp](#spatial_tsp)

---

## spatial_tsp

**NAME**

`spatial_tsp` — travelling salesman: best order to visit a set of stops over a network

**SYNOPSIS**

```
spatial_tsp <network.network> (-stops <stops.csv> | -nodes <list>) [options]
```

**DESCRIPTION**

Given a set of stops, finds the order of visit with the lowest total cost, travelling along the real streets of a network built with `spatial_network` (one-way edges can only be travelled in their direction). The cost is travel time in minutes (default) or length in metres (`-cost length`).

The stops are given in one of two ways:

- `-stops <file>`: a spatial CSV. A row with a `POINT` geometry is snapped to the nearest node of the network (the distance is shown with `-verbose`, and `-stop_tol` makes the command fail if a point is too far from the network). A row with a `node` column (a node ID or name) uses that node directly; this also works for a CSV without a geometry column. An optional `name` (or `nombre`) column labels the stops in the output.
- `-nodes <list>`: comma-separated node IDs or names, such as `"3,Hospital,17"`.

Two stops on the same node are merged (with a warning).

The route is a **closed tour** by default: it starts at the first stop (or at `-start`), visits every stop once and comes back. With `-open` it does not return; with `-end` it finishes at a given stop (which implies `-open`). `-start` and `-end` accept the name of a stop of the file or a node ID or name, and a stop that is not in the list is added to it. If the end is the same as the start, the tour is closed.

First, the command computes the lowest cost between every pair of stops with Dijkstra's algorithm. Because of one-way streets the cost from A to B is not always the cost from B to A, so this is the *asymmetric* travelling salesman problem. A route may pass through the node of another stop without counting it as a visit. Then:

- with up to 15 stops (`-method auto`) the answer is **exact** (Held-Karp dynamic programming); `-method exact` extends this to 18 stops;
- with more stops it uses a **heuristic**: nearest neighbour, improved with 2-opt and Or-opt moves, repeated after random perturbations (`-iterations`, `-seed`). The result is good but not guaranteed to be optimal, and it can change a little with `-seed`. The same seed always gives the same tour. The output shows how much the heuristic improved on the nearest-neighbour tour. Tens of stops take a fraction of a second and a thousand stops a few seconds; `-time_limit` caps the time (and makes the result depend on the speed of the computer).

If a stop cannot be reached from the start (or the start cannot be reached again from a stop of a closed tour), the command names the stops involved and stops; see `spatial_network_info -validate` for the disconnected parts and the one-way edges that cause it.

The output CSV has one row per leg with `id` (the order of the leg), `from_name`, `to_name`, `from_node`, `to_node`, `cost` and `cumulative_cost` (in the unit of `-cost`), `length` (m), `time` (min), `node_count` and `geometry` (a `LINESTRING` along the streets of the leg), so it can be drawn with `spatial_csv2geo`. The comments at the top give the CRS, the cost, the type of route, the method and the totals.

**OPTIONS**

| Option | Description |
|---|---|
| `-stops <file>` | CSV of stops: `POINT` geometries (snapped to the nearest node) or a `node` column; optional `name` column |
| `-nodes <list>` | Comma-separated node IDs or names (instead of `-stops`) |
| `-cost <kind>` | Cost to minimise: `time` (minutes, default) or `length` (metres) |
| `-start <stop>` | First stop: a stop name or a node ID/name; added to the stops if missing (default: the first stop) |
| `-end <stop>` | Last stop of an open route; implies `-open` |
| `-open` | Do not return to the start; the last stop is free unless `-end` is given |
| `-method <m>` | `auto` (default: exact up to 15 stops), `exact` (up to 18 stops) or `heuristic` |
| `-iterations <n>` | Perturbation rounds of the heuristic (default: automatic, up to 20000, stopping early when there is no progress; `0` = local search only) |
| `-seed <n>` | Random seed of the heuristic (default: `1`) |
| `-time_limit <s>` | Stop the heuristic after this many seconds and keep the best tour found (default: no limit) |
| `-stop_tol <dist>` | Fail if a stop point is farther than `<dist>` (CRS units) from its nearest node |
| `-output <file>` | Output CSV file (default: `tour.csv`) |
| `-verbose` | List the stops (with the snapping distance), the cost matrix (up to 15 stops) and the whole order of visit |

**EXIT STATUS**

`0` on success; `1` on a usage or I/O error, an unknown or ambiguous node, fewer than two different stops, a stop that cannot be reached, or when no route visits every stop (for example, because of one-way streets).

**EXAMPLES**

```
spatial_tsp roads.network -stops clients.csv -start Depot -output tour.csv
spatial_tsp roads.network -nodes "Escuela,Hospital,Terminal,Mercado" -cost length
spatial_tsp roads.network -stops clients.csv -start Depot -end Airport -open
spatial_tsp roads.network -stops clients.csv -iterations 5000 -seed 7
spatial_csv2geo tour.csv tour.geojson
```

**SEE ALSO**

[spatial_network](#spatial_network), [spatial_shortest_path](#spatial_shortest_path), [spatial_network_service](#spatial_network_service), [spatial_network_info](#spatial_network_info)

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

`spatial_lrs_segment` — create line segments from linear events, or points from point events

**SYNOPSIS**

```
spatial_lrs_segment <network.lrs> <events.csv> <output.csv> [options]
```

**DESCRIPTION**

Generates line segment geometry from linear events defined by a measure range (`from_measure`/`to_measure`) over an LRS network. It can also aggregate the events first (concatenate and/or dissolve) and keep only some of their attribute columns, which together allow splitting a multi-attribute event table into one table per attribute.

**Point events.** If the events CSV has a `measure` column instead of `from_measure`/`to_measure`, each event is a point on its route: the output has the columns `id`, `route_id`, `measure`, `offset`, the attribute columns, `x`, `y` and `geometry` (`POINT`). The point lies on the route at that measure, moved sideways by the offset (positive to the left of the direction of the route). Events whose measure is outside the measure range of the route, or whose route does not exist, are reported as errors and omitted. A file cannot have both `measure` and `from_measure`/`to_measure`, and `-merge`, `-merge_by` and `-mode` do not apply to point events; `-offset`, `-keep` and `-verbose` do.

For linear events, the events CSV must contain: `route_id`, `from_measure`, `to_measure`; optionally `id`, `offset` and any number of attribute columns. Events whose measures exceed the measure range of the route are clipped to it; events with `from_measure` greater than `to_measure`, an unknown route or no part on the route are reported as errors.

**Merging events.** `-merge`, `-merge_by` and `-mode` merge events of the same route:

- *Concatenate*: events where the end of one coincides with the start of the next.
- *Dissolve*: events that overlap.
- With `-merge_by`, only events with the same value in all the given columns are merged.

When events are merged, the output events are numbered 1, 2, 3… in route and measure order, and the column `source_event` lists the original ids separated by `;`. The offset of a merged event is the mean of the offsets. An attribute that differs between the merged events and is not in `-merge_by` is left empty.

**Splitting a table by attribute.** `-keep` leaves only the given attribute columns in the output. Combined with `-merge_by` on the same columns, it produces the event table of a single attribute with its redundant events concatenated; running the command once per attribute splits the original table.

**OPTIONS**

| Option | Description |
|---|---|
| `-offset <value>` | Default lateral offset, used for events without their own `offset` |
| `-merge` | Merge events of the same route that touch or overlap |
| `-merge_by <f1,f2,...>` | Merge only events with the same value in all these columns (implies `-merge`) |
| `-mode <mode>` | `concatenate` (adjacent only), `dissolve` (overlapping only) or `both` (default); implies `-merge` |
| `-keep <f1,f2,...>` | Keep only these attribute columns in the output |
| `-verbose` | Show detailed information |

The output has the columns `id`, `route_id`, `from_measure`, `to_measure`, `offset`, `length` (length of the segment geometry, before applying the offset), the attribute columns, `source_event` and `geometry`.

**EXAMPLES**

```
spatial_lrs_segment lrs_network.lrs conditions.csv segments.csv -offset 3
spatial_lrs_segment lrs_network.lrs signs.csv signs_points.csv
spatial_lrs_segment lrs_network.lrs conditions.csv segments.csv -merge
spatial_lrs_segment lrs_network.lrs pavement.csv material.csv -keep material -merge_by material -mode concatenate
spatial_lrs_segment lrs_network.lrs reports.csv reports_dissolved.csv -merge_by type -mode dissolve
```

**SEE ALSO**

[spatial_lrs_create](#spatial_lrs_create), [spatial_lrs_calibrate](#spatial_lrs_calibrate), [spatial_lrs_locate](#spatial_lrs_locate)

---

## spatial_lrs_calibrate

**NAME**

`spatial_lrs_calibrate` — calibrate the measures of an LRS network with points of known measure

**SYNOPSIS**

```
spatial_lrs_calibrate <network.lrs> <points.csv> <output.lrs> [options]
```

**DESCRIPTION**

Adjusts the measures of the routes of an LRS network using calibration points, for example the kilometer posts of a road. For every route with calibration points:

1. each point is projected onto the route and a vertex is created there (or an existing vertex is reused) with the measure of the point;
2. the measures of the other vertices are interpolated between calibration points, proportionally to the distance along the route;
3. before the first and after the last point they are extrapolated with the slope (measure per distance unit) of the nearest calibrated interval, or with 1 measure unit per distance unit if the route has a single calibration point.

The calibration points of a route must have increasing measures along the direction of the route; otherwise the route is not calibrated and an error is reported. Routes without points are copied unchanged. Calibrated routes are marked in the output file, so that `spatial_lrs_info -validate` does not expect the measure to grow like the distance.

The points CSV must contain: `route_id`, `measure` and either `geometry` (POINT WKT) or `x`,`y` columns; optionally `id`.

**OPTIONS**

| Option | Description |
|---|---|
| `-max_dist <value>` | Maximum distance from a calibration point to its route; farther points are ignored (default: `1.0`) |
| `-route_id <col>` | Column with the route identifier (default: `route_id`) |
| `-measure <col>` | Column with the known measure (default: `measure`) |
| `-verbose` | Show the result for every route |

**EXIT STATUS**

`0` if every route that had calibration points was calibrated; `1` on a usage or I/O error, if no route could be calibrated, or if some route could not.

**EXAMPLES**

```
spatial_lrs_calibrate roads.lrs milestones.csv roads_km.lrs
spatial_lrs_calibrate roads.lrs milestones.csv roads_km.lrs -max_dist 5 -verbose
```

**SEE ALSO**

[spatial_lrs_create](#spatial_lrs_create), [spatial_lrs_info](#spatial_lrs_info), [spatial_lrs_segment](#spatial_lrs_segment), [spatial_lrs_locate](#spatial_lrs_locate)

---

## spatial_lrs_info

**NAME**

`spatial_lrs_info` — inspect and validate an LRS network

**SYNOPSIS**

```
spatial_lrs_info <network.lrs> [options]
```

**DESCRIPTION**

Reads a `.lrs` file created by `spatial_lrs_create` and reports the CRS, the number of routes and vertices, the total length, the bounding box, and, for each route, its vertex count, length of the geometry, measure range (M-range) and whether it was calibrated with `spatial_lrs_calibrate`. With `-route`, it lists every vertex of that route with its measure.

With `-validate`, it also checks the M-values of every route and prints the issues found. Issues are errors (the exit code becomes `1`) or warnings.

| Check | Severity | Meaning |
|---|---|---|
| `DUPLICATE_ROUTE` | error | `route_id` appears more than once |
| `TOO_FEW_VERTICES` | error | route has fewer than 2 vertices |
| `NEGATIVE_MEASURE` | error | a vertex has a negative measure |
| `MEASURE_OVERLAP` | error | measure does not increase between two vertices (decreases or repeats) |
| `MEASURE_GAP` | error | measure grows more than the route's measure/distance ratio allows between two vertices |
| `MEASURE_SHORT` | error | measure grows less than the route's measure/distance ratio allows between two vertices |
| `LENGTH_MISMATCH` | error | the declared route length differs from the length of the geometry |
| `START_NOT_ZERO` | warning | first measure is not 0 |
| `ZERO_LENGTH_SEGMENT` | warning | two consecutive vertices share the same position |

The expected measure increment between two vertices is the distance between them multiplied by the measure/distance ratio of the route (1 for a network created by `spatial_lrs_create`, or the ratio given by `from_measure`/`to_measure`); the comparison uses a tolerance of `0.001`. These two checks and `START_NOT_ZERO` are skipped on routes calibrated with `spatial_lrs_calibrate`, whose ratio legitimately changes between calibration points; the measures must still increase. A network created with `spatial_lrs_create` always passes validation; the checks are useful for `.lrs` files that were edited by hand or produced by other tools.

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

