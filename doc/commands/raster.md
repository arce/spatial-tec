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

## Terrain, Hydrology & Cost Analysis (placeholder commands)

> ⚠️ **Estado:** los tres comandos de esta sección (`spatial_terrain`, `spatial_hydrology`, `spatial_cost`) son cascarones (placeholders). Analizan y validan sus argumentos igual que el resto de Spatial TEC, pero no calculan todavía ningún resultado real: terminan imprimiendo un aviso de "no implementado" y no generan un archivo de salida. Se agregaron a partir de la revisión del manual `practicas/`, que ya asumía que existían. Siguiendo la misma convención que ya usan [`spatial_vectorize`](#spatial_vectorize) (`-contour`/`-polygonize`/`-points`), [`spatial_distance`](analysis.md#spatial_distance) (`-matrix`/`-point`/`-nearest`/`-self`) y [`spatial_merge_raster`](#spatial_merge_raster) (`-method`), cada uno agrupa varias operaciones cercanas bajo un solo binario con un flag `-operation`/`-mode`, en vez de un comando separado por operación. Consulte el código fuente en `src/` para el estado exacto de cada uno. (`spatial_reclass`, documentado más abajo, se agregó junto con estos pero ya tiene funcionalidad real implementada.)

## spatial_terrain

**NAME**

`spatial_terrain` — análisis de terreno a partir de un DEM: pendiente, orientación, sombreado, curvatura *(placeholder)*

**SYNOPSIS**

```
spatial_terrain <input.asc> <output.asc> -operation <slope|aspect|hillshade|curvature> [options]
```

**OPERATIONS**

| Operation | Description |
|---|---|
| `-operation slope` | Raster de pendiente |
| `-operation aspect` | Raster de orientación (aspecto) |
| `-operation hillshade` | Raster de sombreado (relieve sombreado) |
| `-operation curvature` | Raster de curvatura del terreno |

**OPTIONS** (aplican solo a la operación correspondiente)

| Option | Description |
|---|---|
| `-units <degrees|percent>` | Unidades de la pendiente (`slope`; default: `degrees`) |
| `-z_factor <value>` | Factor de exageración vertical (`slope`, `hillshade`; default: `1.0`) |
| `-flat_value <value>` | Valor asignado a celdas planas (`aspect`; default: `-1`) |
| `-azimuth <value>` | Azimut de la fuente de luz en grados (`hillshade`; default: `315`) |
| `-altitude <value>` | Altitud de la fuente de luz en grados (`hillshade`; default: `45`) |
| `-type <profile|planform|general>` | Tipo de curvatura (`curvature`; default: `general`) |

**EXAMPLES**

```
spatial_terrain dem.asc slope.asc -operation slope
spatial_terrain dem.asc slope.asc -operation slope -units percent
spatial_terrain dem.asc aspect.asc -operation aspect
spatial_terrain dem.asc hillshade.asc -operation hillshade -azimuth 315 -altitude 45
spatial_terrain dem.asc curvature.asc -operation curvature -type profile
```

**SEE ALSO**

[spatial_hydrology](#spatial_hydrology), [spatial_cost](#spatial_cost)

---

## spatial_hydrology

**NAME**

`spatial_hydrology` — condicionamiento hidrológico de un DEM: relleno de sumideros, dirección y acumulación de flujo *(placeholder)*

**SYNOPSIS**

```
spatial_hydrology <input.asc> <output.asc> -operation <fill|flow_direction|flow_accumulation> [options]
```

**OPERATIONS**

| Operation | Description |
|---|---|
| `-operation fill` | Rellena sumideros (depresiones) en un DEM |
| `-operation flow_direction` | Dirección de flujo (D8) a partir de un DEM ya rellenado |
| `-operation flow_accumulation` | Acumulación de flujo a partir de un raster de dirección de flujo (aquí `<input.asc>` es ese raster, no el DEM) |

**OPTIONS** (aplican solo a la operación correspondiente)

| Option | Description |
|---|---|
| `-max_depth <value>` | Profundidad máxima a rellenar (`fill`; default: sin límite) |
| `-method <d8>` | Algoritmo de dirección de flujo (`flow_direction`; default: `d8`) |
| `-weights <file>` | Raster opcional de pesos (`flow_accumulation`; default: cada celda pesa 1) |
| `-verbose` | Mostrar información detallada |

**EXAMPLES**

```
spatial_hydrology dem.asc dem_filled.asc -operation fill
spatial_hydrology dem_filled.asc flowdir.asc -operation flow_direction
spatial_hydrology flowdir.asc flowacc.asc -operation flow_accumulation
```

**SEE ALSO**

[spatial_terrain](#spatial_terrain), [spatial_cost](#spatial_cost)

---

## spatial_cost

**NAME**

`spatial_cost` — costo-distancia, visibilidad y corredores de menor costo sobre un raster *(placeholder)*

**SYNOPSIS**

```
spatial_cost <input1> [input2] <output> -operation <cost|viewshed|corridor> [options]
```

**DESCRIPTION**

Acepta uno o dos rasters de entrada según la operación, con el mismo patrón variádico que [`spatial_merge_raster`](#spatial_merge_raster) (`<input1> [input2...] <output>`): `-operation corridor` necesita dos superficies de costo ya calculadas; `cost`/`viewshed` necesitan solo una.

**OPERATIONS**

| Operation | Inputs | Description |
|---|---|---|
| `-operation cost` | 1 (raster de fricción) | Superficie de costo acumulado desde un punto de origen |
| `-operation viewshed` | 1 (DEM) | Máscara de visibilidad desde un punto de observación |
| `-operation corridor` | 2 (superficies de costo acumulado) | Corredor de menor costo entre ambas |

**OPTIONS** (aplican solo a la operación correspondiente)

| Option | Description |
|---|---|
| `-source <x> <y>` | Punto de origen (`cost`; **requerido** para esa operación) |
| `-max_cost <value>` | Costo máximo acumulado (`cost`; default: sin límite) |
| `-observer <x> <y>` | Punto de observación (`viewshed`; **requerido** para esa operación) |
| `-obs_height <value>` | Altura del observador sobre el terreno (`viewshed`; default: `1.7`) |
| `-radius <value>` | Radio máximo de análisis (`viewshed`; default: sin límite) |
| `-threshold <value>` | Umbral de costo del corredor (`corridor`; default: mínimo + 10%) |

**EXAMPLES**

```
spatial_cost friction.asc cost.asc -operation cost -source -84.09 9.93
spatial_cost dem.asc view.asc -operation viewshed -observer -84.09 9.93
spatial_cost cost_a.asc cost_b.asc corridor.asc -operation corridor
```

**SEE ALSO**

[spatial_terrain](#spatial_terrain), [spatial_hydrology](#spatial_hydrology)

---

## spatial_reclass

**NAME**

`spatial_reclass` — reclasifica los valores de un raster según una tabla de rangos o valores

**SYNOPSIS**

```
spatial_reclass <input.asc> <output.asc> -table <file> [options]
```

**DESCRIPTION**

Lee una tabla CSV de reglas y reasigna cada celda del ráster de entrada según ella. La tabla admite dos formatos, detectados automáticamente por su cabecera: rangos (`from,to,new_value`, con `from <= v <= to` inclusivo en ambos extremos, primera regla que coincide en el orden de la tabla) o valor exacto (`value,new_value`, con tolerancia `1e-9`). Toda celda que no coincida con ninguna regla, así como las celdas que ya eran NODATA en la entrada, se escriben como NODATA en la salida (con el valor dado por `-nodata`).

**OPTIONS**

| Option | Description |
|---|---|
| `-table <file.csv>` | Tabla de reclasificación: columnas `from,to,new_value` o `value,new_value` (**requerido**) |
| `-nodata <value>` | Valor NODATA de salida, también usado para celdas sin regla (default: `-9999`) |

**EXAMPLES**

```
spatial_reclass landuse.asc reclass.asc -table rules.csv
```

**SEE ALSO**

[spatial_calc](#spatial_calc), [spatial_filter_raster](#spatial_filter_raster)

---

## spatial_rat

**NAME**

`spatial_rat` — exporta o importa la tabla de atributos raster (RAT) de un archivo ASCII Grid

**SYNOPSIS**

```
spatial_rat <input.asc> <table.csv> [output.asc] -mode <export|import>
```

**DESCRIPTION**

El formato ASCII Grid del proyecto admite internamente una sección opcional `@RAT` con una tabla de clasificación, leída y escrita automáticamente por el resto de comandos raster (ver `include/core/spatial_types.hpp` y `spatial_io.hpp`). `spatial_rat` es la interfaz de línea de comandos para gestionarla desde afuera del propio archivo `.asc`, en ambas direcciones bajo un solo binario con `-mode`.

`-mode export` falla con un error si `<input.asc>` no tiene una sección `@RAT` embebida. `-mode import` requiere que `<table.csv>` tenga una columna llamada `value` (cualquier capitalización; se normaliza a minúsculas al importar), usada para emparejar cada fila de la tabla con las celdas del ráster que tengan ese valor.

**OPTIONS**

| Option | Description |
|---|---|
| `-mode export` | Exporta la RAT ya incluida en `<input.asc>` hacia `<table.csv>` |
| `-mode import` | Importa `<table.csv>` como la RAT de `<input.asc>`, escribiendo `<output.asc>` (**requerido** para este modo) |

**EXAMPLES**

```
spatial_rat classes.asc classes_rat.csv -mode export
spatial_rat classes.asc rat_table.csv classes_with_rat.asc -mode import
```

**SEE ALSO**

[spatial_reclass](#spatial_reclass)
