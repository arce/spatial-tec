# Makefile for Spatial TEC -- builds the 40 console tools (spatial_info,
# spatial_buffer, ..., spatial_csv2shp) and spatial_viewer (the one FLTK
# GUI tool) natively for whichever OS `make` is run on (Linux or macOS), or
# cross-compiled to Windows from Linux/macOS using mingw-w64.
#
# The console tools are plain C++17/STL -- nothing below needs any
# PLATFORM-specific flags for them, they're identical on every platform and
# just ride along with whatever CXX/CXXFLAGS the active PLATFORM section
# sets. spatial_viewer is the only target that links against FLTK, so it's
# the only one with a per-platform FLTK_CXXFLAGS/FLTK_LDFLAGS to worry
# about. Unlike some FLTK+network projects, spatial_tools has no
# networking/HTTPS code anywhere, so there's no WinHTTP/NSURLSession/
# cpp-httplib+OpenSSL section here at all -- just FLTK.
#
# Usage:
#   make                     -> auto-detects the host OS (via `uname`) and
#                                builds every program natively into build/
#   make linux               -> forces a native Linux build regardless of
#                                host OS detection (only actually works when
#                                run ON Linux, same as `make` there -- see
#                                the LINUX/MAC section below for why this
#                                can't be cross-compiled from macOS)
#   make mac                 -> forces a native macOS build for whichever
#                                architecture this Mac actually is (only
#                                works when run ON macOS, same as `make`
#                                there)
#   make macArm / macIntel   -> macOS only: cross-arch builds using clang's
#                                built-in -arch support (no separate
#                                toolchain needed), always against the
#                                matching vendored fltk/mac-arm or
#                                fltk/mac-intel -- lets one Mac (Apple
#                                Silicon or Intel) produce a binary for the
#                                other architecture too
#   make windows              -> cross-compiles every program to build/*.exe
#                                for Windows using mingw-w64 -- run this
#                                from Linux or macOS (see the WINDOWS
#                                section below for its own requirements)
#   make spatial_info / spatial_viewer / etc.
#                              -> builds just that one target, for the
#                                PLATFORM in effect (auto-detected, or
#                                whatever `make windows`/`make linux`/
#                                `make mac`/`make macArm`/`make macIntel` set)
#   make WITH_CONSOLE=1       -> Windows only: builds spatial_viewer.exe
#                                with a visible console window (for
#                                debugging); the console tools always have
#                                one regardless of this flag
#   make clean                -> removes build/
#
# --- FLTK, per platform -----------------------------------------------------
# fltk/ vendors prebuilt FLTK for all four target environments this project
# cares about (fltk/windows, fltk/linux, fltk/mac-arm, fltk/mac-intel --
# each with its own include/ and lib/), so a fresh checkout can build
# spatial_viewer without first installing FLTK's dev packages.
#
#   - Windows: always uses fltk/windows (there's no "system FLTK" story on
#     the mingw-w64 cross-compile path -- see the WINDOWS section below).
#   - Linux/macOS: always uses the vendored fltk/linux, fltk/mac-arm or
#     fltk/mac-intel copy (matched to `uname -m` on macOS) -- never a
#     Homebrew/apt-installed FLTK or fltk-config, even if one is on PATH,
#     so the build doesn't depend on what else happens to be installed on
#     this machine. See the LINUX/MAC section below for what that
#     additionally needs from the system (X11 client libraries on Linux;
#     nothing extra on macOS beyond the Cocoa framework, which ships with
#     the OS).
#   - macArm/macIntel always use the matching vendored copy for the
#     requested architecture, regardless of the host Mac's own.
# ---------------------------------------------------------------------------
BUILD_DIR      := build
CXXSTD         := -std=c++17
WARNFLAGS      := -Wall -Wextra
OPTFLAGS       := -O2
# -Iinclude: every spatial_*.cpp includes its own headers as
# "../include/core/...", so this is only needed by spatial_viewer, which
# includes "viewer/main_window.hpp" (no ../ prefix, see src/spatial_viewer.cpp)
# -- kept here (not per-target) since it's harmless for the console tools too.
ROOT_INCLUDE   := -Iinclude
# Auto-detect the host OS for the default `make`/`make spatial_info`/etc.
# targets. `make windows`/`make linux`/`make mac`/`make macArm`/
# `make macIntel` (see the recursive re-invocation targets below) override
# this explicitly.
UNAME_S := $(shell uname -s)
ifndef PLATFORM
  ifeq ($(UNAME_S),Darwin)
    PLATFORM := mac
  else ifeq ($(UNAME_S),Linux)
    PLATFORM := linux
  else
    PLATFORM := windows
  endif
endif
# Every command's own .cpp is a single, self-contained translation unit
# (no header-only "modules/" split for these -- each spatial_*.cpp already
# includes what it needs straight from include/core/), so the only extra
# prerequisite worth tracking here is include/core/*.hpp itself, so `make`
# rebuilds every console tool (and spatial_viewer) when a shared core
# header changes, not just when a .cpp does. spatial_viewer additionally
# depends on include/viewer/*.hpp (see its own rule further down).
CORE_HEADERS   := $(wildcard include/core/*.hpp)
VIEWER_HEADERS := $(wildcard include/viewer/*.hpp)
# The 40 console tools: plain C++17/STL, no FLTK, no networking, identical
# build recipe on every platform (see the pattern rule in each PLATFORM
# section below) -- spatial_viewer is deliberately not in this list since
# it needs its own per-platform FLTK flags and is built by its own rule.
CONSOLE_PROGRAMS := \
    spatial_address \
    spatial_buffer \
    spatial_calc \
    spatial_calc_vector \
    spatial_centroid \
    spatial_clip_raster \
    spatial_info \
    spatial_filter_vector \
    spatial_filter_raster \
    spatial_clip_vector \
    spatial_merge_vector \
    spatial_merge_raster \
    spatial_rasterize \
    spatial_rat \
    spatial_reclass \
    spatial_validate \
    spatial_vectorize \
    spatial_query \
    spatial_join \
    spatial_resample \
    spatial_statistics \
    spatial_difference \
    spatial_simplify \
    spatial_union \
    spatial_distance \
    spatial_geo2csv \
    spatial_csv2geo \
    spatial_osm2csv \
    spatial_csv2osm \
    spatial_shp2csv \
    spatial_csv2shp \
    spatial_colormap \
    spatial_network \
    spatial_shortest_path \
    spatial_svg \
    spatial_lrs_create \
    spatial_lrs_locate \
    spatial_lrs_segment \
    spatial_interpolate \
    spatial_zonal \
    spatial_reproject
# Comandos "cascaron" (placeholder): analizan sus argumentos igual que el
# resto del proyecto pero todavia no implementan la funcionalidad real (ver
# cada archivo fuente y doc/commands/ para el detalle). Se agregaron a partir
# de la revision de practicas/, que asumia que ya existian. Se agrupan aparte
# de CONSOLE_PROGRAMS solo para que quede claro en el Makefile cuales de los
# binarios que produce `make` son estables y cuales son cascarones; en el
# build no hay ninguna diferencia de tratamiento entre ambos grupos.
#
# spatial_calc_vector, spatial_reclass, spatial_rat y spatial_validate se
# movieron de esta lista a CONSOLE_PROGRAMS (arriba): son los 4 comandos
# cascaron que resultaron ser requeridos por los tutoriales 1-3 del manual,
# y ya tienen funcionalidad real implementada.
#
# spatial_reproject tambien se movio: convierte coordenadas de vector entre
# WGS84 geografico, CRTM05 (EPSG:5367, el sistema oficial de Costa Rica) y
# UTM zonas 16N/17N (EPSG:32616/32617), ademas de Web Mercator (EPSG:3857,
# usado por OpenStreetMap/Google Maps) -- ver include/core/spatial_projection.hpp
# para las formulas (serie de Snyder para las proyecciones Transversa de
# Mercator, sin dependencia de PROJ/GDAL) y doc/commands/projection.md para
# el detalle de uso.
#
# Los 8 restantes no son uno por cada operacion imaginable: donde varias
# operaciones son variantes cercanas de la misma tarea, comparten un solo
# binario con un flag -operation/-mode, exactamente como ya hacen
# spatial_vectorize (-contour/-polygonize/-points), spatial_distance
# (-matrix/-point/-nearest/-self) y spatial_merge_raster
# (-method first/last/min/max/average/sum) en el resto del proyecto -- en
# vez de un binario nuevo por variante.
STUB_PROGRAMS := \
    spatial_terrain \
    spatial_hydrology \
    spatial_cost \
    spatial_lrs_info \
    spatial_network_info \
    spatial_network_service \
    spatial_address_validate \
    spatial_reverse_geocode
CONSOLE_PROGRAMS += $(STUB_PROGRAMS)
ALL_PROGRAMS := $(CONSOLE_PROGRAMS) spatial_viewer spatial_georeference spatial_editor
.PHONY: all clean windows linux mac macArm macIntel help $(ALL_PROGRAMS)
all: $(ALL_PROGRAMS)
# Re-invokes this same Makefile with PLATFORM forced, so `make windows` (or
# linux/mac) works no matter what the host-OS auto-detection above landed
# on. Also lets one machine build for a different PLATFORM than its own
# host OS where that's actually possible (Linux/macOS -> Windows via
# mingw-w64; Linux <-> macOS native builds are NOT cross-compilable this
# way, since FLTK's GUI backend is platform-native code either way -- see
# the LINUX/MAC section below).
windows:
	$(MAKE) PLATFORM=windows all
linux:
	$(MAKE) PLATFORM=linux all
mac:
	$(MAKE) PLATFORM=mac all
macArm:
	$(MAKE) PLATFORM=mac MAC_ARCH=arm64 all
macIntel:
	$(MAKE) PLATFORM=mac MAC_ARCH=x86_64 all
ifeq ($(PLATFORM),windows)
# =====================================================================
# WINDOWS -- cross-compiled from Linux/macOS with mingw-w64, against the
# Windows FLTK build vendored at fltk/windows/ (fltk/windows/include/FL,
# fltk/windows/lib/*.a). Every binary (console tools and spatial_viewer
# alike) is statically linked, so a single .exe can be handed to someone
# without also shipping mingw's DLLs alongside it.
#
# Requirements on macOS (Homebrew):
#   brew install mingw-w64
# Requirements on Linux (Debian/Ubuntu):
#   apt install mingw-w64
# This installs x86_64-w64-mingw32-g++ (and gcc/ar/windres with the same
# prefix). If your install uses a different prefix, override it like this:
#   make windows MINGW_PREFIX=x86_64-w64-mingw32
# =====================================================================
MINGW_PREFIX   ?= x86_64-w64-mingw32
CXX            := $(MINGW_PREFIX)-g++
CC             := $(MINGW_PREFIX)-gcc
WINDRES        := $(MINGW_PREFIX)-windres
EXE_EXT        := .exe
FLTK_DIR       := fltk/windows/lib
FLTK_INC       := fltk/windows/include
# -ffunction-sections -fdata-sections: puts every function/global variable
# in its own linker section instead of one big section per .o file, so
# -Wl,--gc-sections below (see CONSOLE_LDFLAGS/VIEWER_LDFLAGS) can discard
# the ones nothing actually references at a fine grain -- without this, the
# linker can only keep-or-drop a whole .o file at a time.
#
# -DWIN32: FLTK 1.3 picks its Windows backend (instead of X11/Unix, see
# FL/x.H) by checking the WIN32 macro (no underscore). MinGW automatically
# defines _WIN32 and __WIN32__, but does NOT define WIN32 on its own, so it
# has to be passed by hand or FL/x.H ends up including X11/Xlib.h and the
# build fails with "X11/Xlib.h: No such file or directory". Harmless for the
# console tools (they never look at it), so it's set globally here instead
# of only for spatial_viewer.
#
# -D_USE_MATH_DEFINES: mingw-w64's math.h follows MSVC's convention of
# hiding M_PI/M_E/etc. behind this macro instead of exposing them
# unconditionally the way glibc/macOS's math.h do -- without it, anything
# using M_PI (spatial_buffer.cpp's circle/arc generation) fails to build on
# Windows only with "'M_PI' was not declared in this scope". Set globally,
# same reasoning as -DWIN32 above: harmless for files that don't use it.
CXXFLAGS := $(CXXSTD) $(WARNFLAGS) $(OPTFLAGS) -ffunction-sections -fdata-sections -DWIN32 -D_USE_MATH_DEFINES $(ROOT_INCLUDE)
VIEWER_CXXFLAGS := $(CXXFLAGS) -I$(FLTK_INC)
# FLTK libraries: --start-group/--end-group because they have cross
# dependencies on each other (libfltk_images needs symbols from libfltk and
# vice versa in some cases); with --{start,end}-group the linker keeps
# retrying until everything resolves, without depending on the exact order.
FLTK_LIBS := -Wl,--start-group \
             -lfltk_images -lfltk_png -lfltk_jpeg -lfltk_z -lfltk \
             -Wl,--end-group
# Windows system libraries FLTK needs to link on mingw: GDI for drawing,
# comctl32 for native controls, ole32/uuid/oleaut32/comdlg32 for the native
# file chooser and clipboard, winspool/gdiplus for printing and image
# support. No -lwinhttp/-lws2_32 here -- spatial_tools has no networking.
WIN_SYS_LIBS := -lcomctl32 -lgdi32 -lole32 -luuid -lcomdlg32 -loleaut32 \
                -lwinspool -lgdiplus
# -static: embeds the mingw/libstdc++/libwinpthread runtime into the .exe,
# so a single file can be distributed without also copying mingw's DLLs
# alongside it. Applies to every target, console tools included.
#
# -mwindows: marks spatial_viewer.exe as a GUI application (WINDOWS
# subsystem, not CONSOLE) so Windows doesn't pop a black console window
# behind the FLTK interface when it runs. Only applies to spatial_viewer --
# the console tools are console tools by design and always keep their
# window. To debug spatial_viewer with a visible console for printf/error
# messages, build with `make windows WITH_CONSOLE=1`.
WITH_CONSOLE ?= 0
ifeq ($(WITH_CONSOLE),1)
  WINFLAG :=
else
  WINFLAG := -mwindows
endif
# -s: strips the symbol table and relocation info from the final .exe --
# nothing at runtime needs exported symbol names for a statically-linked
# executable, but -static otherwise bakes in the full mingw/libstdc++/
# libwinpthread runtime (plus, for spatial_viewer, FLTK's static libs)
# wholesale. Usually the single biggest win for shrinking a -static mingw
# binary.
#
# -Wl,--gc-sections: works with -ffunction-sections/-fdata-sections above
# to let the linker drop functions/globals that ended up in the final link
# but are never actually referenced.
CONSOLE_LDFLAGS := -static -static-libgcc -static-libstdc++ -s -Wl,--gc-sections
VIEWER_LDFLAGS := $(CONSOLE_LDFLAGS) -L$(FLTK_DIR) $(WINFLAG) $(FLTK_LIBS) $(WIN_SYS_LIBS)
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
$(BUILD_DIR)/%.exe: src/%.cpp $(CORE_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@ $(CONSOLE_LDFLAGS)
$(BUILD_DIR)/spatial_viewer.exe: src/spatial_viewer.cpp $(CORE_HEADERS) $(VIEWER_HEADERS) | $(BUILD_DIR)
	$(CXX) $(VIEWER_CXXFLAGS) $< -o $@ $(VIEWER_LDFLAGS)
# spatial_georeference: GCP-based image georeferencer (zoom/pan, mark control
# points, export .asc) -- a second, independent FLTK GUI tool, built with the
# exact same FLTK_CXXFLAGS/FLTK_LDFLAGS as spatial_viewer above. Single
# self-contained .cpp (no include/georeference/*.hpp split), so it only
# depends on CORE_HEADERS, not VIEWER_HEADERS.
$(BUILD_DIR)/spatial_georeference.exe: src/spatial_georeference.cpp $(CORE_HEADERS) | $(BUILD_DIR)
	$(CXX) $(VIEWER_CXXFLAGS) $< -o $@ $(VIEWER_LDFLAGS)
# spatial_editor: point/line/polygon digitizer, reuses Viewer::MapWidget/
# Layer (include/viewer/*.hpp) for rendering and pan/zoom, so -- unlike
# spatial_georeference -- it depends on VIEWER_HEADERS too, same as
# spatial_viewer.
$(BUILD_DIR)/spatial_editor.exe: src/spatial_editor.cpp $(CORE_HEADERS) $(VIEWER_HEADERS) | $(BUILD_DIR)
	$(CXX) $(VIEWER_CXXFLAGS) $< -o $@ $(VIEWER_LDFLAGS)
else
# =====================================================================
# LINUX / MAC -- native builds. FLTK's GUI backend (X11/Wayland on Linux,
# Cocoa on macOS) is platform-native code, so this can only ever build for
# whichever of the two `make` actually runs on -- there's no cross-compiling
# a Linux binary from macOS or vice versa the way Windows .exe's are
# cross-compiled above (see the top-of-file comment for `make macArm`/
# `make macIntel`, which cross only the CPU architecture, not the OS, using
# clang's built-in -arch support -- that's a much smaller ask than crossing
# OSes and doesn't need a separate toolchain).
#
# The console tools need nothing from this section beyond CXX/CXXFLAGS --
# only spatial_viewer touches FLTK_CXXFLAGS/FLTK_LDFLAGS below.
#
# spatial_viewer always builds against the vendored copy under fltk/ --
# never against a Homebrew/apt-installed FLTK or fltk-config, even if one
# happens to be on PATH, so the build is reproducible from a fresh checkout
# without depending on what else is installed on this machine. The vendored
# copy still needs, already installed on the system (these are NOT
# vendored, since they're not FLTK itself):
#   Linux:  X11 client libraries + fontconfig dev headers, e.g. on
#           Debian/Ubuntu: apt install libx11-dev libxext-dev libxft-dev
#           libxinerama-dev libxcursor-dev libxfixes-dev libxrender-dev
#           libfontconfig1-dev libpng-dev zlib1g-dev
#   macOS:  nothing extra -- Cocoa and zlib both ship with the OS.
# =====================================================================
CXX := g++
EXE_EXT :=
ifeq ($(PLATFORM),mac)
  MAC_ARCH ?= $(shell uname -m)
  ifeq ($(MAC_ARCH),arm64)
    VENDOR_FLTK_DIR := fltk/mac-arm
  else
    VENDOR_FLTK_DIR := fltk/mac-intel
  endif
  # Only meaningful for macArm/macIntel (cross-arch): forces clang to
  # target the requested architecture regardless of the host Mac's own. A
  # no-op flag-wise on a plain `make mac` run natively (host arch and
  # target arch are the same), included unconditionally for simplicity.
  ARCH_FLAG := -arch $(MAC_ARCH)
  # macOS ships Cocoa (FLTK's windowing backend here) and zlib; nothing
  # else needs to come from Homebrew for spatial_viewer.
  FLTK_LDFLAGS := -L$(VENDOR_FLTK_DIR)/lib -lfltk_images -lfltk_png -lfltk_jpeg -lfltk \
                  -framework Cocoa -framework OpenGL -lz
else
  VENDOR_FLTK_DIR := fltk/linux
  ARCH_FLAG :=
  # fltk/linux/lib has no libfltk_png.a/libfltk_z.a of its own (unlike the
  # macOS/Windows vendored copies) -- it was built against the system's
  # own libpng/zlib, so those come from -lpng -lz below instead.
  FLTK_LDFLAGS := -L$(VENDOR_FLTK_DIR)/lib -lfltk_images -lfltk_jpeg -lfltk \
                  -lX11 -lXext -lXft -lXinerama -lXfixes -lXcursor -lXrender -lfontconfig \
                  -lpng -lz -ldl -lm -lpthread
endif
FLTK_CXXFLAGS := -I$(VENDOR_FLTK_DIR)/include
CXXFLAGS := $(CXXSTD) $(WARNFLAGS) $(OPTFLAGS) $(ARCH_FLAG) $(ROOT_INCLUDE)
VIEWER_CXXFLAGS := $(CXXFLAGS) $(FLTK_CXXFLAGS)
VIEWER_LDFLAGS := $(ARCH_FLAG) $(FLTK_LDFLAGS)
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
$(BUILD_DIR)/%: src/%.cpp $(CORE_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@
$(BUILD_DIR)/spatial_viewer: src/spatial_viewer.cpp $(CORE_HEADERS) $(VIEWER_HEADERS) | $(BUILD_DIR)
	$(CXX) $(VIEWER_CXXFLAGS) $< -o $@ $(VIEWER_LDFLAGS)
# spatial_georeference: same FLTK flags as spatial_viewer, single .cpp so
# only CORE_HEADERS (not VIEWER_HEADERS) is a prerequisite.
$(BUILD_DIR)/spatial_georeference: src/spatial_georeference.cpp $(CORE_HEADERS) | $(BUILD_DIR)
	$(CXX) $(VIEWER_CXXFLAGS) $< -o $@ $(VIEWER_LDFLAGS)
# spatial_editor: same FLTK flags as spatial_viewer; depends on
# VIEWER_HEADERS too since it reuses Viewer::MapWidget/Layer.
$(BUILD_DIR)/spatial_editor: src/spatial_editor.cpp $(CORE_HEADERS) $(VIEWER_HEADERS) | $(BUILD_DIR)
	$(CXX) $(VIEWER_CXXFLAGS) $< -o $@ $(VIEWER_LDFLAGS)
endif
# Thin wrappers so `make spatial_info`, `make spatial_viewer`, etc. work
# directly (matching the per-program targets the project had before build/
# existed), on top of the real file-based rules above.
$(CONSOLE_PROGRAMS): %: $(BUILD_DIR)/%$(EXE_EXT)
spatial_viewer: $(BUILD_DIR)/spatial_viewer$(EXE_EXT)
spatial_georeference: $(BUILD_DIR)/spatial_georeference$(EXE_EXT)
spatial_editor: $(BUILD_DIR)/spatial_editor$(EXE_EXT)
clean:
	rm -rf $(BUILD_DIR) output_*.csv output_*.asc
help:
	@echo "Spatial TEC - Commands:"
	@echo "  spatial_info             - Get file information"
	@echo "  spatial_filter_vector    - Filter vector by attributes"
	@echo "  spatial_filter_raster    - Filter raster by cell values"
	@echo "  spatial_clip_vector      - Clip vector by spatial extent"
	@echo "  spatial_clip_raster      - Clip raster by spatial extent"
	@echo "  spatial_calc             - Perform raster calculations"
	@echo "  spatial_merge_vector     - Merge multiple vector files"
	@echo "  spatial_merge_raster     - Merge multiple rasters (mosaic)"
	@echo "  spatial_buffer           - Create buffers around geometries"
	@echo "  spatial_rasterize        - Convert vector to raster"
	@echo "  spatial_vectorize        - Convert raster to vector"
	@echo "  spatial_query            - Perform spatial queries (filtering)"
	@echo "  spatial_join             - Perform spatial joins (attribute transfer)"
	@echo "  spatial_resample         - Change resolution of rasters"
	@echo "  spatial_statistics       - Calculate spatial statistics"
	@echo "  spatial_difference       - Find features in one dataset not in another"
	@echo "  spatial_simplify         - Simplify vector geometries"
	@echo "  spatial_centroid         - Calculate centroids of geometries"
	@echo "  spatial_union            - Union/dissolve polygons"
	@echo "  spatial_distance         - Calculate distances between features"
	@echo "  spatial_geo2csv          - Convert GeoJSON to spatial CSV"
	@echo "  spatial_csv2geo          - Convert spatial CSV to GeoJSON"
	@echo "  spatial_osm2csv          - Convert OpenStreetMap XML to spatial CSV"
	@echo "  spatial_csv2osm          - Convert spatial CSV to OpenStreetMap XML"
	@echo "  spatial_shp2csv          - Convert a Shapefile (.shp/.shx/.dbf) to spatial CSV"
	@echo "  spatial_csv2shp          - Convert spatial CSV to Shapefile (.shp/.shx/.dbf)"
	@echo "  spatial_colormap         - Generate choropleth color mapping"
	@echo "  spatial_address          - Geocode addresses by street interpolation"
	@echo "  spatial_svg              - Generate SVG map from vector data"
	@echo "  spatial_network          - Build network from street lines"
	@echo "  spatial_shortest_path    - Find shortest path between two nodes"
	@echo "  spatial_viewer           - Interactive graphical layer viewer"
	@echo "  spatial_georeference     - Georeference a PNG/JPEG image into a .asc raster via control points"
	@echo "  spatial_editor           - Create and edit point/line/polygon features, save as spatial CSV"
	@echo "  spatial_calc_vector      - Compute vector attribute columns via an expression"
	@echo "  spatial_reclass          - Reclassify raster values via a lookup table"
	@echo "  spatial_rat              - Export/import a raster attribute table: -mode export|import"
	@echo "  spatial_validate         - Validate/repair vector geometry"
	@echo "  spatial_reproject        - Convert vector coordinates between CRS: -from/-to wgs84|crtm05|utm16n|utm17n|webmercator"
	@echo ""
	@echo "Placeholder commands (accept documented arguments, not yet implemented):"
	@echo "  spatial_terrain            - Terrain analysis: -operation slope|aspect|hillshade|curvature"
	@echo "  spatial_hydrology          - DEM conditioning: -operation fill|flow_direction|flow_accumulation"
	@echo "  spatial_cost               - Cost-distance analysis: -operation cost|viewshed|corridor"
	@echo "  spatial_lrs_info           - LRS network information, optionally -validate"
	@echo "  spatial_network_info       - Network information, optionally -validate"
	@echo "  spatial_network_service    - Service areas / isochrones: -mode alloc|iso"
	@echo "  spatial_address_validate   - Validate street address ranges"
	@echo "  spatial_reverse_geocode    - Reverse geocoding (nearest address to a point)"
	@echo ""
	@echo "Cross-platform build targets: make linux | mac | macArm | macIntel | windows"
.PHONY: all clean test help