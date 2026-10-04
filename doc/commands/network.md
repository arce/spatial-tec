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

**OPTIONS**

| Option | Description |
|---|---|
| `-offset <value>` | Lateral offset from the centerline (default: `5.0`) |

**EXAMPLES**

```
spatial_address streets.csv addresses.csv result.csv
```

**SEE ALSO**

[spatial_network](#spatial_network), [spatial_lrs_locate](#spatial_lrs_locate)

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

