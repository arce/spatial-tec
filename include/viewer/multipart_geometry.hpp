#pragma once

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

}
