#pragma once
// include/viewer/multipart_geometry.hpp
//
// Helper to draw/hit-test/label a MULTIPOINT/MULTILINESTRING/MULTIPOLYGON
// feature one real part at a time (e.g. a region made of several islands
// rendered as the separate shapes they actually are, instead of one shape
// with a stray edge connecting every island to the next). Despite living
// under include/viewer/, this header has no FLTK dependency -- it only
// touches core/spatial_geom.hpp and core/spatial_types.hpp -- so
// spatial_svg.cpp reuses it too (see ADR-0005) to anchor per-feature
// labels on the same "biggest part" as spatial_viewer does, instead of
// re-implementing the same geometry a second time.
//
// The real part boundaries come straight from VectorFeature::part_starts
// (see spatial_types.hpp / spatial_csv.hpp), which SpatialCSVReader
// records for free while parsing the WKT -- no text re-parsing and no
// duplicated storage of the geometry. Callers get the [begin,end) ranges
// from core/spatial_geom.hpp's featurePartRanges() (call it once per
// feature, not once per part) and turn each into a flat coordinate list
// with extractRange() below, since the existing point/line/polygon math
// (pointToLineDistance, pointInPolygon, polygonCentroid, ...) all take a
// `const std::vector<double>&`. That copy is transient -- built for one
// draw/hit-test call and freed right after, not kept around anywhere.

#include <utility>
#include <vector>

#include "core/spatial_geom.hpp"
#include "core/spatial_types.hpp"

namespace Viewer {

inline std::vector<double> extractRange(const Spatial::VectorFeature& feature,
                                        const std::pair<size_t, size_t>& range) {
  std::vector<double> part;
  part.reserve((range.second - range.first) * 2);
  for (size_t p = range.first; p < range.second; ++p) {
    part.push_back(feature.coordinates[p * 2]);
    part.push_back(feature.coordinates[p * 2 + 1]);
  }
  return part;
}

}  // namespace Viewer
