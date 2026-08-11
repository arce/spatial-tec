# Spatial Queries & Analysis

Tools for relating two vector datasets to each other (queries, joins, differences, distances) and for computing descriptive statistics.

> Syntax conventions: `<argument>` is required, `[optional]` is optional, `...` means repetition. Options use a single dash (`-option`).

---

## spatial_query

**NAME**

`spatial_query` — run spatial queries between vector datasets

**SYNOPSIS**

```
spatial_query <input> <output> <operation> <target> [options]
```

**DESCRIPTION**

Filters the features of a layer according to their spatial relationship with another layer (the target).

**OPERATIONS**

| Option | Description |
|---|---|
| `-within <target>` | Features completely inside the target |
| `-contains <target>` | Features that contain the target |
| `-intersects <target>` | Features that intersect the target |
| `-distance <target> <value>` | Features within a given distance |
| `-nearest <target>` | Find the nearest feature in the target |

**OPTIONS**

| Option | Description |
|---|---|
| `-invert` | Invert the query result |
| `-units <units>` | Distance units (default: same as the CRS) |

**EXAMPLES**

```
spatial_query cities.csv result.csv -within protected_zones.csv
spatial_query roads.csv result.csv -intersects rivers.csv
spatial_query cities.csv result.csv -distance roads.csv 10
spatial_query cities.csv result.csv -nearest hospitals.csv
spatial_query cities.csv result.csv -within zones.csv -invert
```

**SEE ALSO**

[spatial_join](#spatial_join), [spatial_difference](#spatial_difference), [spatial_clip_vector](vector.md#spatial_clip_vector)

---

## spatial_join

**NAME**

`spatial_join` — join attributes between two vector layers via a spatial relationship

**SYNOPSIS**

```
spatial_join <source> <target> <output> <operation> [options]
```

**DESCRIPTION**

Transfers attributes from a target layer onto a source layer based on a spatial relationship between the two.

**OPERATIONS**

| Option | Description |
|---|---|
| `-within` | Join attributes where source is within target |
| `-contains` | Join attributes where source contains target |
| `-intersects` | Join attributes where source intersects target |
| `-nearest` | Join attributes from the nearest target feature |

**OPTIONS**

| Option | Description |
|---|---|
| `-prefix <str>` | Prefix for target columns (default: `target_`) |
| `-attrs <list>` | Only join the given attributes, comma-separated |

**EXAMPLES**

```
spatial_join cities.csv zones.csv result.csv -within
spatial_join cities.csv hospitals.csv result.csv -nearest -prefix hosp_
spatial_join points.csv roads.csv result.csv -intersects
spatial_join zones.csv cities.csv result.csv -contains -attrs name,population
```

**SEE ALSO**

[spatial_query](#spatial_query), [spatial_difference](#spatial_difference)

---

## spatial_difference

**NAME**

`spatial_difference` — find features present in one dataset and absent from another

**SYNOPSIS**

```
spatial_difference <source> <target> <output> [options]
```

**DESCRIPTION**

Computes various kinds of spatial difference between two vector layers.

**OPERATIONS**

| Option | Description |
|---|---|
| `-within` | Source features NOT within the target |
| `-intersects` | Source features NOT intersecting the target |
| `-polygon` | Source polygon areas NOT overlapping the target |
| `-symmetric` | Symmetric difference (in one set or the other, but not both) |
| `-distance <d>` | Features farther than `d` units from the target |

**EXAMPLES**

```
spatial_difference cities.csv zones.csv outside.csv -within
spatial_difference zone_a.csv zone_b.csv unique.csv -polygon
spatial_difference points.csv polygons.csv far.csv -distance 10
spatial_difference a.csv b.csv sym.csv -symmetric
```

**SEE ALSO**

[spatial_query](#spatial_query), [spatial_join](#spatial_join)

---

## spatial_distance

**NAME**

`spatial_distance` — calculate distances between features

**SYNOPSIS**

```
spatial_distance <input> <output> [options]
```

**DESCRIPTION**

Computes distances between features in a layer: a full distance matrix, distance to a reference point, distance to the nearest feature in another layer, or distance to the nearest neighbor within the same layer.

**OPTIONS**

| Option | Description |
|---|---|
| `-matrix` | Build a distance matrix between all features |
| `-point <x> <y>` | Distance to a reference point |
| `-nearest <file>` | Distance to the nearest feature in another dataset |
| `-self` | Distance to the nearest feature within the same dataset |
| `-threshold <value>` | Only include distances `<= value` (matrix mode) |

**EXAMPLES**

```
spatial_distance cities.csv matrix.csv -matrix
spatial_distance cities.csv distances.csv -point -84.09 9.93
spatial_distance cities.csv distances.csv -nearest hospitals.csv
spatial_distance cities.csv distances.csv -self
```

**SEE ALSO**

[spatial_join](#spatial_join), [spatial_query](#spatial_query)

---

## spatial_filter_vector

**NAME**

`spatial_filter_vector` — filter vector data by attributes

**SYNOPSIS**

```
spatial_filter_vector <input> <output> <condition>
```

**DESCRIPTION**

Keeps only the vector features whose attributes satisfy the given condition.

Condition format: `column=value`, `column>10`, `column<5`. Multiple conditions: `column>10 AND column<20`.

**EXAMPLES**

```
spatial_filter_vector cities.csv large.csv "population>100000"
spatial_filter_vector cities.csv capitals.csv "type=capital"
spatial_filter_vector cities.csv filtered.csv "population>50000 AND type=city"
spatial_filter_vector cities.csv san_jose.csv "name=\"San Jose\""
```

> **Note:** always quote the condition to prevent shell interpretation.

**SEE ALSO**

[spatial_filter_raster](raster.md#spatial_filter_raster), [spatial_query](#spatial_query)

---

## spatial_interpolate

**NAME**

`spatial_interpolate` — spatial interpolation from points to a raster

**SYNOPSIS**

```
spatial_interpolate <input.csv> <output.asc> -attribute <col> [options]
```

**DESCRIPTION**

Builds a continuous raster surface from points with known values, using a choice of interpolation methods.

**REQUIRED**

| Option | Description |
|---|---|
| `-attribute <col>` | Column with the values to interpolate |
| `-cellsize <value>` | Cell size of the output raster |

**METHODS**

`-method <method>`: `nearest`, `idw`, `idw_power`, `idw_neighbors`, `linear`, `kriging`, `natural`, `rbf` (default: `idw`)

**OPTIONS**

| Option | Description |
|---|---|
| `-power <value>` | Power for IDW (default: `2.0`) |
| `-neighbors <n>` | Number of neighbors for IDW (default: `12`) |
| `-radius <value>` | Search radius (default: no limit) |
| `-sigma <value>` | Sigma for RBF (default: `1.0`) |
| `-bbox <minx> <miny> <maxx> <maxy>` | Output extent |
| `-nodata <value>` | NODATA value (default: `-9999`) |
| `-cross_validate` | Perform leave-one-out cross-validation |
| `-verbose` | Show detailed information |

**EXAMPLES**

```
spatial_interpolate points.csv surface.asc -attribute value -method idw -cellsize 0.01
spatial_interpolate points.csv surface.asc -attribute value -method nearest -cellsize 0.01
spatial_interpolate points.csv surface.asc -attribute value -method idw_power -power 3 -cellsize 0.01
spatial_interpolate points.csv surface.asc -attribute value -method idw_neighbors -neighbors 8 -cellsize 0.01
```

**SEE ALSO**

[spatial_rasterize](raster.md#spatial_rasterize), [spatial_statistics](#spatial_statistics)

---

## spatial_statistics

**NAME**

`spatial_statistics` — calculate spatial statistics

**SYNOPSIS**

```
spatial_statistics <input> [options]
```

**DESCRIPTION**

Computes descriptive statistics on raster or vector data: basic statistics, histograms, and zonal statistics.

**OPTIONS FOR RASTER**

| Option | Description |
|---|---|
| `-stats` | Calculate basic statistics |
| `-histogram` | Generate a histogram |
| `-bins <n>` | Number of histogram bins (default: `10`) |
| `-zones <file>` | Calculate zonal statistics (vector zones) |
| `-zone_id <attr>` | Attribute used to identify zones |

**OPTIONS FOR VECTOR**

| Option | Description |
|---|---|
| `-attribute <col>` | Attribute column to analyze |
| `-stat <stat1,stat2>` | Statistics to calculate |

**GENERAL OPTIONS**

| Option | Description |
|---|---|
| `-output <file>` | Output CSV file |
| `-verbose` | Verbose output |

**EXAMPLES**

```
spatial_statistics elev.asc -stats
spatial_statistics elev.asc -stats -histogram -bins 20
spatial_statistics elev.asc -zones zones.csv -stat mean,min,max
spatial_statistics cities.csv -attribute population -stat min,max,mean
```

**SEE ALSO**

[spatial_zonal](#spatial_zonal), [spatial_calc](raster.md#spatial_calc)

---

## spatial_zonal

**NAME**

`spatial_zonal` — calculate zonal statistics

**SYNOPSIS**

```
spatial_zonal <raster.asc> <zones.csv> <output.csv> [options]
```

**DESCRIPTION**

Summarizes raster values within each zone defined by a vector polygon layer (equivalent to "zonal statistics" in GIS software).

**OPTIONS**

| Option | Description |
|---|---|
| `-stat <stats>` | Statistics to calculate, comma-separated: `sum, mean, min, max, range, std, variance, cv, count, nodata_count, median, p25, p75, variety, diversity, majority, minority, sum_sq, all` |
| `-zone_id <attr>` | Attribute used to identify the zone (default: `id`) |
| `-keep_attrs` | Keep zone attributes in the output |
| `-histogram` | Generate a histogram for each zone |
| `-bins <n>` | Number of histogram bins (default: `10`) |
| `-verbose` | Show detailed information |

**EXAMPLES**

```
spatial_zonal elevation.asc zones.csv stats.csv -stat mean,min,max,sum
spatial_zonal classification.asc zones.csv stats.csv -stat variety,diversity
spatial_zonal elevation.asc zones.csv stats.csv -stat all -keep_attrs
```

**SEE ALSO**

[spatial_statistics](#spatial_statistics)
