# Cartography & Visualization

Tools for styling data and rendering maps: choropleth color classification, SVG map generation, and the interactive graphical viewer.

> Syntax conventions: `<argument>` is required, `[optional]` is optional, `...` means repetition. Options use a single dash (`-option`).

---

## spatial_colormap

**NAME**

`spatial_colormap` — generate a `.sty` color map / style file for a vector or raster layer

**SYNOPSIS**

```
spatial_colormap <input_file> [options]
```

**DESCRIPTION**

Classifies a set of numeric values into color classes and writes a `.sty` style file: a CSV attribute column for a vector `<input_file>` (`.csv`/`.tsv`, needs `-attribute`), or every non-`NODATA` cell value for a raster `<input_file>` (`.asc`/`.grd`, detected automatically by extension — `-attribute` is ignored). Every value's color is decided ahead of time and written out as a list of classes; there is no color interpolation at render time, so both `spatial_svg` and `spatial_viewer` just look up which class a value falls into (see ADR-0004 in the project's `adr/` directory for the design rationale).

Unless `-output` is given, the result is written to `<input_file>` with its extension replaced by `.sty` (e.g. `countries.csv` → `countries.sty`, `dem.asc` → `dem.sty`) — this is the **sidecar convention**: a `.sty` with the same base name as its data file is picked up automatically by `spatial_svg` (`-layer`) and by `spatial_viewer` when that layer is loaded, with no extra flag needed. Pass `-output -` to write to `stdout` instead.

**OPTIONS**

| Option | Description |
|---|---|
| `-attribute <col>` | Column with the values to classify (**required** for CSV input; ignored for raster input and for `-method single`) |
| `-palette <name>` | Color palette (default: `YlOrRd`) |
| `-classes <num>` | Number of classes (default: `5`, or `32` for `-method continuous` if not given explicitly) |
| `-method <method>` | See **CLASSIFICATION METHODS** below |
| `-interval <width>` | Class width (**required** for `-method defined_interval`) |
| `-breaks <v1,v2,...>` | Explicit break points, at least 2 values (**required** for `-method manual`) |
| `-layer <num>` | Layer number (default: `1`) |
| `-title <text>` | Legend title (default: attribute name) |
| `-no-legend` | Disable legend generation |
| `-legend-position <corner>` | Legend corner for `spatial_svg`: `top-left`, `top-right`, `bottom-left`, `bottom-right` (default: `bottom-right`) |
| `-show-labels` | Emit per-feature/per-cell labels for `spatial_svg`/`spatial_viewer` |
| `-label-field <col>` | Attribute column to use as label text (vector only; defaults to `-attribute` if `-show-labels` is given without this) |
| `-output <file>` | Output `.sty` file (default: `<input_file>.sty`; `-` for `stdout`) |
| `-list-palettes` | List all available palettes |
| `-help` | Show this help |

Legend and label settings are written per layer into the `.sty` (`layer.N.legend.*`, `layer.N.show_labels`, `layer.N.label_field`) — see ADR-0005 in the project's `adr/` directory. `spatial_svg` reads them to draw a legend box and per-feature text; `spatial_viewer` reads `show_labels`/`label_field` the same way when a layer's sidecar is loaded.

**CLASSIFICATION METHODS**

| Method | Description |
|---|---|
| `quantile` | Equal number of values per class |
| `equal_interval` | Equal-width value ranges |
| `natural_breaks` | Jenks-style clustering to minimize within-class variance |
| `standard_deviation` | Classes one standard deviation wide, centered on the mean |
| `pretty_breaks` | Rounds class boundaries to "nice" numbers (10, 25, 50...); the resulting class count may differ slightly from `-classes` |
| `geometric_interval` | Class widths grow in geometric progression — useful for skewed data (population, income) |
| `defined_interval` | Fixed-width classes; the class count is derived from `-interval` instead of `-classes` |
| `manual` | Explicit break points from `-breaks`, no data read |
| `continuous` | Like `equal_interval` but meant for a much finer `-classes` count (defaults to 32), reading as a smooth gradient even though it's still precomputed discrete classes under the hood — best suited to continuous raster surfaces (elevation, NDVI) |
| `single` | No classification: one fixed color (the first stop of `-palette`) for the whole layer |

**AVAILABLE PALETTES**

- Sequential: `YlOrRd`, `YlOrBr`, `YlGn`, `Blues`, `Reds`, `Greens`, `Purples`, `Oranges`, `Greys`
- Diverging: `Spectral`, `RdYlGn`, `RdYlBu`, `RdBu`, `PuOr`
- Scientific: `Viridis`, `Inferno`, `Plasma`, `Cividis`
- Thematic: `Topo`, `Ocean`, `Forest`
- Categorical: `Categorical`, `Tableau`

**EXAMPLES**

```
spatial_colormap countries.csv -attribute population
spatial_colormap data.csv -attribute temp -palette RdBu -classes 7
spatial_colormap cities.csv -attribute population -method quantile -output style.sty
spatial_colormap data.csv -attribute density -method manual -breaks 0,10,50,200,1000

# Raster input (detected by extension, -attribute not needed):
spatial_colormap dem.asc -method continuous -palette Viridis
spatial_colormap slopes.asc -method equal_interval -classes 6

# Flat color, no classification:
spatial_colormap zones.csv -method single -palette Blues

# With a legend title/corner and per-feature labels (name column):
spatial_colormap countries.csv -attribute population -title "Population" \
  -legend-position top-left -show-labels -label-field name

# Writes countries.sty next to countries.csv -- spatial_svg and
# spatial_viewer pick it up automatically, no -style/-output needed there.
```

**SEE ALSO**

[spatial_svg](#spatial_svg), [spatial_viewer](#spatial_viewer), [spatial_statistics](analysis.md#spatial_statistics)

---

## spatial_svg

**NAME**

`spatial_svg` — generate an SVG map from vector data

**SYNOPSIS**

```
spatial_svg <output> -style <file>
spatial_svg <output> -layer <file> [options]
```

**DESCRIPTION**

Renders one or more vector layers as an SVG map. Layers and their symbology can be defined directly on the command line (`-layer`), through an explicit multi-layer style file (`-style`), or picked up automatically: if `<file>.sty` exists next to a `-layer <file>` (e.g. one generated by `spatial_colormap`), it's loaded and applied to that layer with no extra flag needed — this is the same sidecar convention `spatial_viewer` uses. Explicit `-color`/`-fill`/`-stroke`/`-opacity`/`-point_size` flags on the command line still override whatever a sidecar `.sty` set.

Each layer can also carry a **legend** and **per-feature labels** (ADR-0005), both read entirely from that layer's `.sty` — there's no command-line flag for either, since both need data (class colors, an attribute column) that only `spatial_colormap`'s output has. A legend is drawn for any layer with `layer.N.legend.enabled=true` and at least one classified rule: a boxed title + one color swatch and label per class, stacked in the corner given by `layer.N.legend.position` (`top-left`/`top-right`/`bottom-left`/`bottom-right`, default `bottom-right` — multiple layers requesting the same corner stack instead of overlapping). Labels are drawn for any layer with `layer.N.show_labels=true` and `layer.N.label_field` set: the attribute's value is drawn at each feature's centroid (polygons), length-wise midpoint (lines), or next to the marker (points) — for a `MULTI*` feature made of several parts, the label anchors on the largest part, the same rule `spatial_viewer` uses. See `spatial_colormap`'s `-show-labels`/`-label-field`/`-legend-position` above to produce these.

**OPTIONS**

| Option | Description |
|---|---|
| `-style <file>` | Style file describing one or more layers explicitly (`.sty` format) |
| `-layer <file>` | Add a layer. If `<file>.sty` exists, it's used automatically as that layer's style |
| `-color <color>` | Stroke color (default: `black`, or the sidecar's if present) |
| `-fill <color>` | Fill color (default: `#CCCCCC`, or the sidecar's if present) |
| `-stroke <width>` | Stroke width (default: `1`) |
| `-opacity <value>` | Opacity (default: `0.8`) |
| `-point_size <num>` | Point size (default: `5`) |
| `-width <px>` | SVG width (default: `800`) |
| `-height <px>` | SVG height (default: `600`) |
| `-background <color>` | Background color (default: `white`) |
| `-title <text>` | Map title |

**EXAMPLES**

```
spatial_svg map.svg -style style.sty
spatial_svg map.svg -layer countries.csv -color blue -fill lightblue

# Combined with spatial_colormap, via the sidecar convention -- no
# -style/-output needed, countries.sty is picked up automatically:
spatial_colormap countries.csv -attribute population
spatial_svg map.svg -layer countries.csv

# With a legend and per-feature labels (from the .sty, see spatial_colormap):
spatial_colormap countries.csv -attribute population -title "Population" \
  -show-labels -label-field name
spatial_svg map.svg -layer countries.csv -title "Countries by population"
```

**SEE ALSO**

[spatial_colormap](#spatial_colormap), [spatial_viewer](#spatial_viewer)

---

## spatial_viewer

**NAME**

`spatial_viewer` — interactive graphical viewer for spatial layers

**SYNOPSIS**

```
spatial_viewer [file]
```

**DESCRIPTION**

Unlike the other tools, `spatial_viewer` is a graphical (GUI, FLTK-based) application for exploring vector and raster layers: open and save layers, edit the attribute table (add/remove rows and columns), and view geometry on an interactive map.

`MULTIPOINT`/`MULTILINESTRING`/`MULTIPOLYGON` geometries are rendered, clicked, and labeled one real part at a time — e.g. a region made of several separate islands draws, selects, and fills each island on its own instead of as one shape with a stray edge connecting every island to the next; the on-map label anchors on the largest part (by area for polygons, by length for lines) rather than an average position that could land outside all of them.

If a file is given as an argument, it is loaded automatically on startup.

When a layer is loaded, `spatial_viewer` looks for a `<file>.sty` sidecar next to it (the same convention `spatial_svg` uses, typically produced by `spatial_colormap`) and applies it automatically: for a vector layer, each feature is colored by looking up which class its attribute value falls into; for a raster layer, each cell is colored the same way from its own value, taking priority over both a RAT color field and the default gradient. There is no color interpolation — every class's color was already decided when the `.sty` was generated (see ADR-0004 in the project's `adr/` directory).

If the sidecar has `show_labels`/`label_field` set (e.g. via `spatial_colormap -show-labels -label-field <col>`, see ADR-0005), the layer opens with on-map labels already turned on, showing the same attribute `spatial_svg` would label from the same `.sty` — no need to turn labels on by hand from the UI first.

**MENU**

| Menu | Actions |
|---|---|
| File | Open layer, Save layer, Save layer as..., Quit |
| Table | Add row, Delete row, Add column, Delete column |
| Help | About... |

**EXAMPLES**

```
spatial_viewer
spatial_viewer cities.csv
```

**SEE ALSO**

[spatial_colormap](#spatial_colormap), [spatial_info](info.md#spatial_info)
