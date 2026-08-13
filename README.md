# Spatial TEC

A collection of C++17 command-line utilities for spatial (vector and raster) data processing, built in the Unix philosophy: each command does one thing and can be chained with others through CSV/GeoJSON/ASCII Grid files as an intermediate format. Also includes `spatial_viewer`, an interactive graphical viewer (FLTK) for exploring layers.

## Requirements

- `g++` (Linux/macOS) with C++17 support, or `mingw-w64` for cross-compiling to Windows
- The 38 stable command-line tools, plus 9 placeholder commands (see below), are plain C++17/STL and build the same way on Linux, macOS and Windows
- `spatial_viewer` additionally needs FLTK, vendored per platform under `fltk/` (`fltk/linux`, `fltk/mac-arm`, `fltk/mac-intel`, `fltk/windows` -- each with its own `include/` and `lib/`), so a fresh checkout can build it without installing FLTK's dev packages first. See the comments at the top of the `Makefile` for what each platform additionally needs from the system itself (X11 client + fontconfig dev headers on Linux; nothing extra on macOS; `mingw-w64` on whichever machine cross-compiles to Windows).

## Build

Binaries are built into `build/`, native to whichever OS `make` runs on unless told otherwise:

```bash
make               # auto-detects the host OS, builds everything into build/
make help          # list available commands and cross-platform targets
make clean         # remove build/ and generated output files
```

Cross-platform / cross-arch targets:

```bash
make linux         # force a native Linux build (only works when run on Linux)
make mac           # force a native macOS build for the host Mac's own architecture
make macArm        # macOS only: cross-arch build for Apple Silicon
make macIntel      # macOS only: cross-arch build for Intel
make windows       # cross-compile every tool to build/*.exe using mingw-w64,
                    # run from Linux or macOS
```

Each binary can also be built individually, for example:

```bash
make spatial_info          # -> build/spatial_info
make windows spatial_info  # -> build/spatial_info.exe
```

## Usage

Binaries are built into `build/` and invoked from there:

```bash
./build/spatial_info examples/cities.csv
./build/spatial_filter_vector examples/cities.csv large.csv "population>100000"
```

Running any command with no arguments prints its usage help.

## Included commands

| Command | Description |
|---|---|
| `spatial_info` | Information and metadata about spatial files |
| `spatial_address` | Address geocoding by street interpolation |
| `spatial_buffer` | Buffers around vector geometries |
| `spatial_calc` | Map algebra (operations between rasters) |
| `spatial_centroid` | Centroid calculation |
| `spatial_clip_raster` | Clip raster by extent |
| `spatial_clip_vector` | Clip vector data by extent |
| `spatial_colormap` | Choropleth color classification |
| `spatial_csv2geo` | Spatial CSV → GeoJSON |
| `spatial_geo2csv` | GeoJSON → spatial CSV |
| `spatial_osm2csv` | OSM XML → spatial CSV |
| `spatial_csv2osm` | Spatial CSV → OpenStreetMap XML |
| `spatial_difference` | Difference between vector datasets |
| `spatial_distance` | Distances between features |
| `spatial_filter_raster` | Filter raster cells by value |
| `spatial_filter_vector` | Filter vectors by attributes |
| `spatial_interpolate` | Spatial interpolation (IDW, kriging, RBF, etc.) |
| `spatial_join` | Spatial attribute join |
| `spatial_query` | Spatial queries (within, intersects, nearest...) |
| `spatial_merge_raster` | Mosaic multiple rasters |
| `spatial_merge_vector` | Merge multiple vector layers |
| `spatial_rasterize` | Vector → raster |
| `spatial_vectorize` | Raster → vector (contours, polygons, points) |
| `spatial_resample` | Change raster resolution |
| `spatial_simplify` | Geometry simplification |
| `spatial_union` | Union/dissolve polygons |
| `spatial_statistics` | Spatial statistics |
| `spatial_zonal` | Zonal statistics |
| `spatial_network` | Build routable networks |
| `spatial_shortest_path` | Shortest path over a network |
| `spatial_lrs_create` | Create Linear Referencing System (LRS) networks |
| `spatial_lrs_locate` | Locate point events along an LRS network |
| `spatial_lrs_segment` | Build segments from linear events over an LRS |
| `spatial_svg` | Generate SVG maps |
| `spatial_viewer` | Interactive graphical layer viewer |
| `spatial_calc_vector` | Compute vector attribute columns via an expression |
| `spatial_reclass` | Reclassify raster values via a lookup table |
| `spatial_rat` | Export/import a raster attribute table: `-mode export\|import` |
| `spatial_validate` | Validate/repair vector geometry |

The detailed reference for each command (syntax, options, and examples, in the style of Unix `man` pages) lives in the **[command manual](doc/README.md)**, grouped by category:

- [General & Information](doc/commands/info.md)
- [Format Conversion](doc/commands/conversion.md)
- [Vector Processing](doc/commands/vector.md)
- [Raster Processing](doc/commands/raster.md)
- [Spatial Queries & Analysis](doc/commands/analysis.md)
- [Networks, LRS & Geocoding](doc/commands/network.md)
- [Cartography & Visualization](doc/commands/cartography.md)
- [Coordinate Reference Systems](doc/commands/projection.md)

## Project structure

```
spatial_tools/
├── src/          # source code (.cpp) for each command
├── include/      # in-house headers (core/) and third-party ones (FLTK, pugixml, json.hpp)
├── fltk/         # prebuilt FLTK, one copy per platform (linux, mac-arm, mac-intel, windows)
├── build/        # compiled binaries (created by `make`, not checked in)
├── examples/     # sample data (CSV, GeoJSON, OSM, ASCII Grid)
├── doc/          # command manual (docsify)
└── Makefile
```
