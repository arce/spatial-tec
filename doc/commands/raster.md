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

`-contour` traces contour lines with marching squares over the cell centers and writes one `LINESTRING` per continuous line (columns `id`, `value`).

`-polygonize` merges contiguous cells with the same value into polygons (columns `id`, `value`, `cell_count`). The cell-edge boundaries are then simplified to remove the stair-step effect, preserving topology: each boundary shared by two polygons is split into arcs at junctions (points where three or more regions meet, plus the raster corners), each arc is simplified once with Douglas–Peucker and both neighbors use the same vertices, so no gaps or overlaps are introduced. An arc whose simplified form would cross or touch another arc, or leave another vertex on the wrong side, is simplified with half the tolerance until it is valid (or kept exact). Use `-no_simplify` for the exact cell edges.

**OPTIONS**

| Option | Description |
|---|---|
| `-contour -interval <value>` | Extract contour lines at regular intervals |
| `-contour -values <list>` | Extract contour lines at specific values |
| `-polygonize` | Convert the raster to polygons |
| `-points` | Convert the raster to points with their value |
| `-filter <value>` | Only include cells with that value |
| `-tolerance <dist>` | `-polygonize`: boundary simplification tolerance in map units (default: 1.5 × cell size) |
| `-no_simplify` | `-polygonize`: keep the exact cell edges (stair-stepped boundaries) |

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

## spatial_terrain

**NAME**

`spatial_terrain` — slope, aspect, hillshade and curvature from a DEM

**SYNOPSIS**

```
spatial_terrain <input.asc> <output.asc> -operation <slope|aspect|hillshade|curvature> [options]
```

**DESCRIPTION**

Derives a terrain raster from a digital elevation model (DEM). The output has the same extent, cell size and NODATA value as the input. Slope, aspect and hillshade use Horn's method on the 3x3 window around each cell:

```
a b c        dz/dx = ((c + 2f + i) - (a + 2d + g)) / (8 * cellsize)
d e f        dz/dy = ((g + 2h + i) - (a + 2b + c)) / (8 * cellsize)
g h i
```

Curvature uses the Zevenbergen-Thorne polynomial and is reported multiplied by 100 (positive = convex, negative = concave). Cells outside the grid, or with a NODATA neighbor, reuse the value of the center cell; cells whose own value is NODATA stay NODATA.

The cell size must be in the same units as the elevations (for example meters). Spatial TEC rasters carry no coordinate system, so for a DEM in geographic degrees the command prints a warning: reproject the DEM first (`spatial_reproject`) or use `-z_factor` to convert the elevation units.

**OPERATIONS**

| Operation | Result |
|---|---|
| `slope` | Steepness: `atan(sqrt(dz/dx² + dz/dy²))` in degrees (0-90), or `100 * sqrt(dz/dx² + dz/dy²)` in percent |
| `aspect` | Direction the slope faces, in degrees clockwise from north (0 = N, 90 = E, 180 = S, 270 = W). Flat cells get `-flat_value` |
| `hillshade` | Illumination of the surface by a light source, from 0 (shadow) to 255 |
| `curvature` | Surface curvature (`general`, `profile` or `planform`) |

**OPTIONS**

| Option | Description |
|---|---|
| `-operation <op>` | `slope`, `aspect`, `hillshade` or `curvature` (required) |
| `-units <degrees\|percent>` | Slope units (default: `degrees`) |
| `-z_factor <value>` | Multiplier applied to the elevations (default: `1.0`). Vertical exaggeration, or conversion of z units to the horizontal units |
| `-flat_value <value>` | Aspect value for flat cells (default: `-1`) |
| `-azimuth <value>` | Direction of the light source, degrees clockwise from north (default: `315`) |
| `-altitude <value>` | Height of the light source above the horizon, 0-90 degrees (default: `45`) |
| `-type <type>` | Curvature type: `general` (default), `profile` (along the slope) or `planform` (across the slope) |

**EXAMPLES**

```
spatial_terrain dem.asc slope.asc -operation slope
spatial_terrain dem.asc slope_pct.asc -operation slope -units percent
spatial_terrain dem.asc aspect.asc -operation aspect
spatial_terrain dem.asc hillshade.asc -operation hillshade -azimuth 315 -altitude 45
spatial_terrain dem.asc profile.asc -operation curvature -type profile
```

For the 3x3 window `52 50 48 / 54 51 47 / 56 52 46` with a 10 m cell size, the center cell has a slope of 19.69° (35.79 %) and an aspect of 77.9° (east-northeast).

**SEE ALSO**

[spatial_vectorize](#spatial_vectorize), [spatial_reclass](#spatial_reclass), [spatial_calc](#spatial_calc)

---

## spatial_hydrology

**NAME**

`spatial_hydrology` — fill sinks, flow direction, flow accumulation and watersheds from a DEM

**SYNOPSIS**

```
spatial_hydrology <input.asc> <output.asc> -operation <fill|flow_direction|flow_accumulation|watershed> [options]
```

**DESCRIPTION**

Simulates how water moves over a digital elevation model (DEM). The operations are chained: each one takes the output of the previous one. The output has the same extent and cell size as the input.

**OPERATIONS**

| Operation | Input | Result |
|---|---|---|
| `fill` | DEM | DEM with every depression (sink) raised up to its spill elevation. Water leaves through the edge of the grid or through NODATA cells |
| `flow_direction` | Filled DEM | D8 flow direction: each cell drains to the neighbor with the steepest descent (the drop divided by the distance, which is `sqrt(2)` times longer on the diagonals) |
| `flow_accumulation` | Flow direction raster | Number of cells (or sum of weights) that drain through each cell, **including the cell itself**: the minimum value is 1, on ridges |
| `watershed` | Flow direction raster | Watershed (drainage basin) of one or more pour points: every cell that drains to a pour point gets that point's number (1, 2, 3... in the order given); the rest is NODATA |

Flow direction codes: `E=1`, `SE=2`, `S=4`, `SW=8`, `W=16`, `NW=32`, `N=64`, `NE=128`. The value `0` marks a cell with no flow: an outlet on the edge of the grid. Flat areas left by `fill` drain toward their spill point, so a filled DEM has no sinks. If a DEM that was not filled is used, the command warns about the interior cells that have no lower neighbor.

A watershed is every cell upstream of its pour point, so its size in cells equals the flow accumulation at that cell. When several pour points are given, a cell belongs to the nearest pour point downstream of it (a point located upstream of another one keeps its own cells, and the lower basin gets the rest). The pour point is the center of the cell that contains the coordinates; it must lie inside the raster, on a valid (non-NODATA) cell, and two points cannot fall on the same cell. A pour point placed by hand rarely falls exactly on the stream: with `-snap_raster` and `-snap_distance` it is first moved to the cell with the highest flow accumulation within that distance.

The drainage network is obtained by keeping the cells whose accumulation exceeds a threshold (`spatial_filter_raster`). The area that drains through a cell is its accumulation multiplied by the cell area.

**OPTIONS**

| Option | Description |
|---|---|
| `-operation <op>` | `fill`, `flow_direction`, `flow_accumulation` or `watershed` (required) |
| `-max_depth <value>` | Maximum depth to fill (`fill`; default: no limit). A depression is a group of connected cells that were raised; if its deepest cell exceeds this value, the whole depression is left unfilled |
| `-method <d8>` | Flow direction algorithm (`flow_direction`; default: `d8`, the only one available) |
| `-weights <file>` | Weights raster with the same grid as the flow direction raster (`flow_accumulation`; default: every cell weighs 1). A NODATA weight counts as 0 |
| `-pour_point <x> <y>` | Outlet of a watershed in map coordinates (`watershed`; required, can be repeated) |
| `-snap_raster <file>` | Flow accumulation raster with the same grid, used to snap each pour point to the cell of highest accumulation (`watershed`) |
| `-snap_distance <d>` | Snap search distance in map units (`watershed`; default: 0, no snapping; needs `-snap_raster`) |
| `-verbose` | Show detailed information |

**EXAMPLES**

```
spatial_hydrology dem.asc dem_filled.asc -operation fill
spatial_hydrology dem_filled.asc flowdir.asc -operation flow_direction
spatial_hydrology flowdir.asc flowacc.asc -operation flow_accumulation
spatial_filter_raster flowacc.asc streams.asc "value>100"
spatial_hydrology flowdir.asc runoff.asc -operation flow_accumulation -weights rainfall.asc
spatial_hydrology flowdir.asc basin.asc -operation watershed -pour_point 483200 1092500
spatial_hydrology flowdir.asc basin.asc -operation watershed -pour_point 483200 1092500 -snap_raster flowacc.asc -snap_distance 90
```

**SEE ALSO**

[spatial_terrain](#spatial_terrain), [spatial_filter_raster](#spatial_filter_raster), [spatial_vectorize](#spatial_vectorize), [spatial_cost](#spatial_cost)

---

## spatial_cost

**NAME**

`spatial_cost` — cost distance, least-cost corridor and viewshed

**SYNOPSIS**

```
spatial_cost <input1> [input2] <output> -operation <cost|corridor|viewshed> [options]
```

**DESCRIPTION**

Three analyses that move over a raster surface. The output has the same extent and cell size as the input.

**OPERATIONS**

| Operation | Input | Result |
|---|---|---|
| `cost` | Friction raster | Accumulated cost of the cheapest route from the nearest source to every cell (Dijkstra over the 8 neighbors). Moving between two adjacent cells costs the average of their friction values times the distance between their centers (the cell size, or the cell size times `sqrt(2)` on the diagonals). Friction must be `>= 0` (0 is allowed); NODATA cells are barriers. Sources cost 0. Cells that cannot be reached (or whose cost exceeds `-max_cost`) are NODATA |
| `corridor` | Two accumulated cost rasters (from point A and from point B), same grid | Sum of both surfaces: the cost of the cheapest route A → B that passes through each cell. Cells whose sum is above the threshold are NODATA, so what remains is the band of near-optimal routes. The minimum of the sum is the cost of the least-cost route |
| `viewshed` | DEM | Which cells can be seen from the observer(s). The line of sight goes from the observer cell center (elevation + `-obs_height`) to the center of each cell (elevation + `-target_height`); the terrain is sampled with bilinear interpolation every cell size along the line, and the cell is hidden if the terrain rises above the line (a line that only touches the terrain counts as visible). With one observer the value is 1 (visible) or 0 (hidden); with several it is the number of observers that see the cell. NODATA cells of the DEM stay NODATA and do not block the view |

Typical workflow for a corridor: run `cost` twice, once from each end, and give both results to `corridor`. The observer and the sources are the centers of the cells that contain the given coordinates; they must lie inside the raster and on a valid cell.

**OPTIONS**

| Option | Description |
|---|---|
| `-operation <op>` | `cost`, `corridor` or `viewshed` (required) |
| `-source <x> <y>` | Origin in map coordinates (`cost`; required, can be repeated: each cell gets the cost from its nearest source) |
| `-max_cost <value>` | Maximum accumulated cost (`cost`; default: no limit) |
| `-threshold <value>` | Largest total cost kept in the corridor, as an absolute value (`corridor`; default: the minimum total cost plus 10%). If it is below the minimum, the corridor is empty and a warning is printed |
| `-observer <x> <y>` | Observer in map coordinates (`viewshed`; required, can be repeated) |
| `-obs_height <value>` | Observer height above the terrain (`viewshed`; default: 1.7) |
| `-target_height <value>` | Height above the terrain of what is looked at, e.g. a tower or a billboard (`viewshed`; default: 0) |
| `-radius <value>` | Maximum analysis radius in map units (`viewshed`; default: no limit). Cells farther than the radius from every observer are NODATA |
| `-curvature` | Correct for the curvature of the Earth and for atmospheric refraction (`viewshed`): the terrain drops `(1 - k) d² / (2R)` at distance `d`, with `R = 6371000 m` and `k = 0.13`. It assumes that map units and elevations are meters, so use a projected CRS (CRTM05, UTM) |

The elevations and the cell size are used as they are, so for a geographic (degrees) DEM the distances of `-radius`, `-curvature` and the slope of the lines of sight are not in meters: reproject the DEM first (see `spatial_reproject` and `spatial_resample`).

**EXAMPLES**

```
spatial_cost friction.asc cost_a.asc -operation cost -source 483200 1092500
spatial_cost friction.asc cost_b.asc -operation cost -source 487900 1090100
spatial_cost cost_a.asc cost_b.asc corridor.asc -operation corridor
spatial_cost cost_a.asc cost_b.asc corridor.asc -operation corridor -threshold 1500
spatial_cost dem.asc viewshed.asc -operation viewshed -observer 483200 1092500 -radius 5000
spatial_cost dem.asc viewshed.asc -operation viewshed -observer 483200 1092500 -obs_height 12 -target_height 2 -curvature
```

**SEE ALSO**

[spatial_terrain](#spatial_terrain), [spatial_hydrology](#spatial_hydrology), [spatial_reclass](#spatial_reclass)

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

---

## spatial_georeference

**NAME**

`spatial_georeference` — graphical tool to georeference a scanned image into a `.asc` raster using control points

**SYNOPSIS**

```
spatial_georeference [image]
```

**DESCRIPTION**

Like `spatial_viewer`, this is a graphical (GUI, FLTK-based) tool rather than a batch command. It loads a PNG or JPEG image (a scanned map, an aerial photo, a site plan) and lets the user register it spatially by clicking points on the image whose real-world geographic coordinates are known, then exports the result as a `.asc` raster — the same Arc/Info ASCII Grid format every other raster tool in this suite reads and writes (`ASCIIGridReader`/`writeRasterASCII`, see `include/core/ascii_grid.hpp` / `spatial_io.hpp`). Loading the image and exporting the raster are driven from the **File** menu, and zooming from the **Zoom** menu; the window can be resized freely (the image area grows/shrinks with it, and the control panel keeps its width, hugging the right edge).

Because that `.asc` format only supports a north-up grid with a single square cell size (`ncols`/`nrows`/`xllcorner`/`yllcorner`/`cellsize` — no rotation or shear), the fit between pixel space and geographic space is a constrained affine model: independent scale and translation on X and on Y, no rotation. Two control points solve it exactly; three or more are fit by least squares, and the RMS error of each axis is reported before and after export so misplaced points are easy to spot. For a reliable fit, mark points spread across the image (especially its corners) rather than clustered in one area, and avoid marking two points in the same image column or row.

The output raster has a single numeric band, as `RasterDataset` requires -- but that doesn't mean the image's real colors are lost. A "Values:" choice selects one of two export modes:

- **True color (palette + RAT)** (default): every cell stores the *index* of a color palette (up to 256 entries) rather than a grayscale number. The palette is either the image's exact distinct colors (when there are 256 or fewer -- typical of a scanned line map or a screenshot), or a 256-color approximation built with median-cut quantization (the same technique GIF/PNG-8 encoders use) when the source has more. The true RGB of every palette entry is written as an embedded RAT (`include/core/ascii_grid.hpp`'s `@RAT` section, the same mechanism `spatial_rat` reads/writes): columns `value` (the palette index), `color` (`RRGGBB` hex), and `count` (cells using it). Because the cell value here is only meaningful through that table, the RAT is mandatory in this mode.
- **Grayscale (numeric)**: every cell stores the actual grayscale luminance (`0.299R + 0.587G + 0.114B`, rounded to a whole 0-255 number so it round-trips exactly through the `.asc` text and any RAT lookup) -- a real numeric magnitude, useful as input to `spatial_calc` and similar tools. A grayscale RAT (same `value`/`color`/`count` shape, `color` repeating the gray level in R/G/B) can optionally be attached via "Include RAT table (colors)", purely so the layer also looks right in a viewer.

In both modes, resampling is nearest-neighbor, and without *some* RAT `spatial_viewer` falls back to its fixed blue-green-red gradient for any raster (see ADR-0002) -- which would render either export as a false-color heatmap. Opening the layer's properties there and setting `color` as the "Color field" shows the real picture.

**CONTROLS**

| Action | Effect |
|---|---|
| File > Load Image... | Open a PNG/JPEG image |
| File > Export .asc... | Compute the fit and save the georeferenced raster |
| Zoom > Zoom In / Zoom Out / Reset View | Same as the `+` / `-` / `R` keys below |
| Mouse wheel | Zoom in/out, centered on the cursor |
| Shift+wheel, or a trackpad's two-finger swipe | Pan the image directly from the scroll gesture |
| Left-drag | Pan the image |
| "Mark mode: click to add" + left click | Add a control point at that pixel (opens a dialog to enter its known X/Y) |
| "Mark mode: click to add" + right click | Delete the nearest control point |
| `+` / `-` | Zoom in/out |
| `R` | Reset zoom/pan |

The control point list can be edited (select a row, change X/Y, "Update coordinates") or removed one at a time or all at once, and can be saved to / loaded from a simple CSV (`pixel_col,pixel_row,geo_x,geo_y`) so a session's points survive a restart.

Before export, the output cell size can be left automatic (the average of the fitted pixel width/height in geographic units) or set manually, and the `NODATA` value and an optional free-text CRS label are configurable; both are written into the `.asc`.

**EXAMPLES**

```
spatial_georeference
spatial_georeference scanned_map.png
```

**SEE ALSO**

[spatial_viewer](cartography.md#spatial_viewer), [spatial_rasterize](#spatial_rasterize)
