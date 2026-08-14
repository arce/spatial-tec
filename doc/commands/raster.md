# Raster Processing

Tools for working with raster grids (Arc/Info ASCII Grid, `.asc`/`.grd`): map algebra, clipping, mosaicking, resampling, and conversion to/from vector data.

> Syntax conventions: `<argument>` is required, `[optional]` is optional, `...` means repetition. Options use a single dash (`-option`).

---

## spatial_calc

**NAME**

`spatial_calc` — perform mathematical operations on rasters (map algebra)

**SYNOPSIS**

```
spatial_calc <input1> [input2 ...] <output> <expression>
```

**DESCRIPTION**

Evaluates a mathematical expression cell-by-cell over one or more input rasters and writes the result to a new raster.

**EXPRESSION SYNTAX**

| Element | Meaning |
|---|---|
| `value` | Value of the current cell |
| `raster1`, `raster2`, ... | Input raster names, in command-line order |
| Operators | `+ - * / ^ > < >= <= == !=` |
| Functions | `sqrt() abs() sin() cos() tan() log() log10() exp() pow() min() max() mean()` |
| Conditional | `condition ? true_value : false_value` |

**EXAMPLES**

```
spatial_calc elev.asc elev_ft.asc "value * 0.3048"
spatial_calc band1.asc band2.asc result.asc "raster1 + raster2"
spatial_calc elev.asc slope.asc "atan(value) * 57.2958"
spatial_calc elev.asc mask.asc "value > 20 ? 1 : 0"
spatial_calc r1.asc r2.asc r3.asc result.asc "(r1 + r2) / r3"
```

**SEE ALSO**

[spatial_filter_raster](#spatial_filter_raster), [spatial_statistics](analysis.md#spatial_statistics)

---

## spatial_clip_raster

**NAME**

`spatial_clip_raster` — clip raster data by spatial extent

**SYNOPSIS**

```
spatial_clip_raster <input> <output> [options]
```

**DESCRIPTION**

Clips a raster to a bounding box, a polygon mask, or the extent of another raster.

**OPTIONS**

| Option | Description |
|---|---|
| `-bbox <minx> <miny> <maxx> <maxy>` | Clip by bounding box |
| `-polygon <file>` | Clip using a polygon as a mask |
| `-clip_to <file>` | Clip to the extent of another raster |
| `-invert` | Invert the mask |

**EXAMPLES**

```
spatial_clip_raster elevation.asc area.asc -bbox -84.5 9.5 -84.0 10.0
spatial_clip_raster elevation.asc mask.asc -polygon boundary.csv
spatial_clip_raster elevation.asc matched.asc -clip_to other.asc
```

**SEE ALSO**

[spatial_clip_vector](vector.md#spatial_clip_vector), [spatial_resample](#spatial_resample)

---

## spatial_filter_raster

**NAME**

`spatial_filter_raster` — filter raster cells by value

**SYNOPSIS**

```
spatial_filter_raster <input> <output> <condition>
```

**DESCRIPTION**

Keeps only the raster cells that satisfy the given condition; the rest are marked `NODATA`.

Condition format: `value>10`, `value<5`, `value=3`. Multiple conditions: `value>10 AND value<20`.

**EXAMPLES**

```
spatial_filter_raster elevation.asc high.asc "value>20"
spatial_filter_raster elevation.asc range.asc "value>=15 AND value<=25"
spatial_filter_raster elevation.asc filtered.asc "value>10 AND value<30"
```

> **Note:** always quote the condition to prevent shell interpretation.

**SEE ALSO**

[spatial_filter_vector](analysis.md#spatial_filter_vector), [spatial_calc](#spatial_calc)

---

## spatial_merge_raster

**NAME**

`spatial_merge_raster` — build a raster mosaic from multiple files

**SYNOPSIS**

```
spatial_merge_raster <input1> [input2 ...] <output> [options]
```

**DESCRIPTION**

Combines two or more overlapping or adjacent rasters into a single mosaic.

**OPTIONS**

| Option | Description |
|---|---|
| `-method <method>` | `first`, `last`, `min`, `max`, `average`, `sum` (default: `first`) |
| `-nodata <value>` | Output NODATA value (default: `-9999`) |

**EXAMPLES**

```
spatial_merge_raster tile1.asc tile2.asc tile3.asc mosaic.asc
spatial_merge_raster t1.asc t2.asc mosaic.asc -method average
spatial_merge_raster t1.asc t2.asc t3.asc mosaic.asc -method max
```

**SEE ALSO**

[spatial_merge_vector](vector.md#spatial_merge_vector), [spatial_resample](#spatial_resample)

---

## spatial_rasterize

**NAME**

`spatial_rasterize` — convert vector data to raster

**SYNOPSIS**

```
spatial_rasterize <input> <output> [options]
```

**DESCRIPTION**

Converts vector features into a regular raster surface, either using a constant value or the value of an attribute.

**OPTIONS**

| Option | Description |
|---|---|
| `-cellsize <value>` | Cell size of the output raster |
| `-attribute <col>` | Attribute column to use as the value |
| `-value <value>` | Constant value for all cells |
| `-bbox <minx> <miny> <maxx> <maxy>` | Output extent |
| `-nodata <value>` | NODATA value (default: `-9999`) |

**EXAMPLES**

```
spatial_rasterize cities.csv density.asc -attribute population -cellsize 0.01
spatial_rasterize cities.csv mask.asc -value 1 -cellsize 0.01
spatial_rasterize zones.csv zones.asc -attribute id -cellsize 0.005
```

**SEE ALSO**

[spatial_vectorize](#spatial_vectorize), [spatial_interpolate](analysis.md#spatial_interpolate)

---

## spatial_vectorize

**NAME**

`spatial_vectorize` — convert raster data to vector

**SYNOPSIS**

```
spatial_vectorize <input> <output> [options]
```

**DESCRIPTION**

Extracts vector geometry (contours, polygons, or points) from a raster.

**OPTIONS**

| Option | Description |
|---|---|
| `-contour -interval <value>` | Extract contour lines at regular intervals |
| `-contour -values <list>` | Extract contour lines at specific values |
| `-polygonize` | Convert the raster to polygons |
| `-points` | Convert the raster to points with their value |
| `-filter <value>` | Only include cells with that value |

**EXAMPLES**

```
spatial_vectorize elevation.asc curves.csv -contour -interval 10
spatial_vectorize classification.asc polygons.csv -polygonize
spatial_vectorize elevation.asc points.csv -points
```

**SEE ALSO**

[spatial_rasterize](#spatial_rasterize)

---

## spatial_resample

**NAME**

`spatial_resample` — change the resolution of raster data

**SYNOPSIS**

```
spatial_resample <input> <output> [options]
```

**DESCRIPTION**

Resamples a raster to a new resolution, a scale factor, or aligns it to a reference raster.

**OPTIONS**

| Option | Description |
|---|---|
| `-resolution <value>` | New cell size |
| `-scale <factor>` | Scale factor (`0.5` = double resolution, `2` = half resolution) |
| `-method <method>` | `nearest`, `bilinear`, `cubic`, `average`, `min`, `max` (default: `average`) |
| `-align_to <file>` | Align to another raster (same resolution and extent) |
| `-bbox <minx> <miny> <maxx> <maxy>` | Output extent |
| `-nodata <value>` | NODATA value (default: `-9999`) |

**EXAMPLES**

```
spatial_resample elev.asc elev_100m.asc -resolution 100 -method average
spatial_resample elev.asc elev_10m.asc -resolution 10 -method bilinear
spatial_resample elev.asc elev_align.asc -align_to other.asc
spatial_resample elev.asc elev_half.asc -scale 2 -method average
```

**SEE ALSO**

[spatial_clip_raster](#spatial_clip_raster), [spatial_merge_raster](#spatial_merge_raster)

---

## spatial_reclass

**NAME**

`spatial_reclass` — reclassify raster values using a table of ranges or exact values

**SYNOPSIS**

```
spatial_reclass <input.asc> <output.asc> -table <file> [options]
```

**DESCRIPTION**

Reads a CSV table of rules and reassigns each cell of the input raster according to it. The table supports two formats, auto-detected from its header: ranges (`from,to,new_value`, with `from <= v <= to` inclusive on both ends, first matching rule in table order wins) or exact value (`value,new_value`, with `1e-9` tolerance). Any cell that matches no rule, as well as cells that were already NODATA in the input, are written as NODATA in the output (using the value given by `-nodata`).

**OPTIONS**

| Option | Description |
|---|---|
| `-table <file.csv>` | Reclassification table: `from,to,new_value` or `value,new_value` columns (**required**) |
| `-nodata <value>` | Output NODATA value, also used for cells with no matching rule (default: `-9999`) |

**EXAMPLES**

```
spatial_reclass landuse.asc reclass.asc -table rules.csv
```

**SEE ALSO**

[spatial_calc](#spatial_calc), [spatial_filter_raster](#spatial_filter_raster)

---

## spatial_rat

**NAME**

`spatial_rat` — export or import a raster's Raster Attribute Table (RAT) from an ASCII Grid file

**SYNOPSIS**

```
spatial_rat <input.asc> <table.csv> [output.asc] -mode <export|import>
```

**DESCRIPTION**

The project's ASCII Grid format internally supports an optional `@RAT` section holding a classification table, read and written automatically by the rest of the raster commands (see `include/core/spatial_types.hpp` and `spatial_io.hpp`). `spatial_rat` is the command-line interface for managing it from outside the `.asc` file itself, in both directions under a single binary with `-mode`.

`-mode export` fails with an error if `<input.asc>` has no embedded `@RAT` section. `-mode import` requires `<table.csv>` to have a column named `value` (any capitalization; normalized to lowercase on import), used to match each table row against the raster cells that hold that value.

**OPTIONS**

| Option | Description |
|---|---|
| `-mode export` | Export the RAT already embedded in `<input.asc>` to `<table.csv>` |
| `-mode import` | Import `<table.csv>` as the RAT for `<input.asc>`, writing `<output.asc>` (**required** for this mode) |

**EXAMPLES**

```
spatial_rat classes.asc classes_rat.csv -mode export
spatial_rat classes.asc rat_table.csv classes_with_rat.asc -mode import
```

**SEE ALSO**

[spatial_reclass](#spatial_reclass)
