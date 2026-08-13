# Spatial TEC

A collection of C++ command-line utilities for spatial (vector and raster) data processing, plus an interactive graphical viewer.

This site is the full command reference, written in the style of Unix `man` pages. Use the sidebar to browse by category, or jump straight to a command:

| Group | Commands |
|---|---|
| [General & Information](commands/info.md) | `spatial_info` |
| [Format Conversion](commands/conversion.md) | `spatial_csv2geo`, `spatial_geo2csv`, `spatial_osm2csv`, `spatial_csv2osm` |
| [Vector Processing](commands/vector.md) | `spatial_buffer`, `spatial_centroid`, `spatial_clip_vector`, `spatial_simplify`, `spatial_union`, `spatial_merge_vector`, `spatial_validate`, `spatial_calc_vector` |
| [Raster Processing](commands/raster.md) | `spatial_calc`, `spatial_clip_raster`, `spatial_filter_raster`, `spatial_merge_raster`, `spatial_rasterize`, `spatial_vectorize`, `spatial_resample`, `spatial_reclass`, `spatial_rat`, `spatial_terrain` ⚠️, `spatial_hydrology` ⚠️, `spatial_cost` ⚠️ |
| [Spatial Queries & Analysis](commands/analysis.md) | `spatial_query`, `spatial_join`, `spatial_difference`, `spatial_distance`, `spatial_filter_vector`, `spatial_interpolate`, `spatial_statistics`, `spatial_zonal` |
| [Networks, LRS & Geocoding](commands/network.md) | `spatial_address`, `spatial_network`, `spatial_shortest_path`, `spatial_lrs_create`, `spatial_lrs_locate`, `spatial_lrs_segment`, `spatial_lrs_info` ⚠️, `spatial_network_info` ⚠️, `spatial_network_service` ⚠️, `spatial_address_validate` ⚠️, `spatial_reverse_geocode` ⚠️ |
| [Cartography & Visualization](commands/cartography.md) | `spatial_colormap`, `spatial_svg`, `spatial_viewer` |
| [Coordinate Reference Systems](commands/projection.md) | `spatial_reproject` ⚠️ |

⚠️ = comando cascarón (placeholder): valida sus argumentos pero todavía no implementa la funcionalidad real. Ver la nota al inicio de cada archivo de comandos para el detalle. Son 9 en total (no uno por cada operación imaginable): donde varias operaciones son variantes cercanas de la misma tarea comparten un solo binario con un flag `-operation`/`-mode`, igual que ya hacen `spatial_vectorize`, `spatial_distance` o `spatial_merge_raster` en el resto del proyecto. Otros 4 comandos que empezaron como cascarón (`spatial_calc_vector`, `spatial_validate`, `spatial_reclass`, `spatial_rat`) ya tienen funcionalidad real implementada, tras revisarse que eran los que efectivamente usaban los primeros tutoriales del manual.

## Quick start

```bash
make            # build all binaries
./spatial_info example.csv
```

## Exit status

By convention, every command-line tool returns `0` on success and a non-zero value when arguments are missing, the input file doesn't exist, or a processing error occurs. Running any command with no arguments (or `-help`, where available) prints its usage message to `stderr`.

See the project's root `README.md` for build and installation instructions.
