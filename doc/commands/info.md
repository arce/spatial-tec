# General & Information

Tools for inspecting spatial files. This is usually the first command you run on a new dataset.

> Syntax conventions: `<argument>` is required, `[optional]` is optional, `...` means repetition. Options use a single dash (`-option`), following the convention of this project's binaries (not GNU `--option`).

---

## spatial_info

**NAME**

`spatial_info` — display information and metadata about spatial files

**SYNOPSIS**

```
spatial_info <file>
```

**DESCRIPTION**

Reports metadata and summary statistics for vector and raster files. For vector data it shows the feature count, geometry types, bounding box, and attribute columns. For raster data it shows dimensions, cell size, extent, and value range.

Takes no options: the file path is the only argument.

**SUPPORTED FORMATS**

| Type   | Extensions                          |
|--------|--------------------------------------|
| Vector | `.csv` with WKT geometry             |
| Raster | `.asc`, `.grd` (Arc/Info ASCII Grid) |

**EXAMPLES**

```
spatial_info cities.csv
spatial_info elevation.asc
```

**SEE ALSO**

[spatial_filter_vector](analysis.md#spatial_filter_vector), [spatial_filter_raster](raster.md#spatial_filter_raster), [spatial_viewer](cartography.md#spatial_viewer)
