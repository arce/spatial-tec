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

> The tool's own `--help`/error text still calls itself `spatial_buffer_vector` (an older name) — that's just its self-printed banner, not a separate binary. `make` only ever produces `build/spatial_buffer`; invoke it by that name.

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

Calculates the centroid of each vector feature (lines and polygons by default). The output always has `POINT` geometry — there is no option to keep the original geometry alongside it. Every attribute from the input feature is copied to the corresponding output point as-is, plus a `source_type` column recording the original geometry type (`point`, `line`, or `polygon`).

`MULTIPOINT`/`MULTILINESTRING`/`MULTIPOLYGON` input features are not currently supported and are skipped (they don't match any `-types` value, since that option only recognizes `point`, `line`, `polygon`).

**OPTIONS**

| Option | Description |
|---|---|
| `-method <method>` | `centroid`, `mass`, or `midpoint` (default: `centroid` for polygons, `midpoint` for lines) |
| `-types <list>` | Types to process: `point,line,polygon` (default: `line,polygon`) |
| `-verbose` | Show detailed statistics |

**EXAMPLES**

```
spatial_centroid zones.csv centroids.csv
spatial_centroid polygons.csv centroids.csv -method mass
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

Reduces the number of vertices in lines and polygons while preserving their overall shape.

**OPTIONS**

| Option | Description |
|---|---|
| `-tolerance <value>` | Simplification tolerance (Douglas-Peucker) |
| `-factor <value>` | Reduction factor (0–1; e.g. `0.5` = 50% reduction) |
| `-method <method>` | `douglas` or `visvalingam` (default: `douglas`) |
| `-types <list>` | Types to simplify: `point,line,polygon` (default: `line,polygon`) |
| `-verbose` | Show detailed statistics |

**EXAMPLES**

```
spatial_simplify curves.csv simplified_curves.csv -tolerance 0.001
spatial_simplify polygons.csv simplified_polygons.csv -factor 0.5
spatial_simplify data.csv simplified_data.csv -tolerance 0.01 -verbose
```

**SEE ALSO**

[spatial_centroid](#spatial_centroid)

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

`spatial_validate` — valida (y opcionalmente repara) la geometría de un conjunto de datos vectoriales

**SYNOPSIS**

```
spatial_validate <input> <output> [options]
```

**DESCRIPTION**

Sin una librería de geometría completa (GEOS) disponible, revisa cada parte (anillo/línea/punto) de cada feature con un conjunto acotado pero real de comprobaciones: geometría vacía, muy pocos vértices, vértices duplicados consecutivos, anillos no cerrados, auto-intersecciones (cruces tipo "X" entre segmentos no adyacentes) y orientación de anillos (se espera sentido antihorario/CCW). La detección de auto-intersección no cubre traslapes colineales (dos segmentos superpuestos sobre la misma recta).

El archivo de salida siempre se escribe: idéntico a la entrada si no se pasó `-fix`, o con las reparaciones aplicadas si se pasó. Sin `-fix`, los problemas se reportan pero la geometría no se modifica. De los problemas detectados, `unclosed_ring`, `duplicate_vertex` y `cw_orientation` son reparables con `-fix`; `empty_geometry`, `too_few_vertices` y `self_intersection` no lo son (solo se reportan).

**OPTIONS**

| Option | Description |
|---|---|
| `-fix` | Intentar reparar geometrías inválidas (cierra anillos, elimina vértices duplicados consecutivos, invierte anillos en sentido horario) |
| `-report <file>` | Guardar un reporte CSV de los problemas encontrados (columnas: `feature_id,geometry_type,part,issue,severity,fixed,message`) |

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

`spatial_calc_vector` — calcula o transforma columnas de atributos de un CSV vectorial mediante una expresión

**SYNOPSIS**

```
spatial_calc_vector <input.csv> <output.csv> -column <name> -expression <expr> [options]
```

**DESCRIPTION**

Contraparte vectorial de [`spatial_calc`](raster.md#spatial_calc) (que es exclusivamente para ráster): en vez de operar celda por celda sobre uno o más rásteres, evalúa la expresión dada fila por fila sobre las columnas de atributos de un CSV vectorial, escribiendo el resultado en una columna nueva (insertada justo antes de la columna de geometría) o sobrescribiendo una existente.

La expresión admite `+ - * / ^`, comparaciones (`> < >= <= == !=`), el operador ternario `condición ? si : no`, paréntesis, negación unaria y las funciones `sqrt abs sin cos tan log log10 exp pow min max`. Cualquier identificador que no sea el nombre de una función se interpreta como el nombre de una columna de atributos del CSV de entrada; si la expresión referencia una columna inexistente, el comando falla con un error antes de procesar ninguna fila.

**OPTIONS**

| Option | Description |
|---|---|
| `-column <name>` | Columna a crear o sobrescribir (**requerido**) |
| `-expression <expr>` | Expresión a evaluar por fila (**requerido**) |

**EXAMPLES**

```
spatial_calc_vector cities.csv cities_out.csv -column density -expression "population / area"
spatial_calc_vector cities.csv cities_out.csv -column tier -expression "population > 1000000 ? 1 : 0"
```

**SEE ALSO**

[spatial_calc](raster.md#spatial_calc), [spatial_filter_vector](analysis.md#spatial_filter_vector)
