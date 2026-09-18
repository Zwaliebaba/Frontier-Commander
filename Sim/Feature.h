#pragma once

#include <cstdint>

// Scenery the simulation treats as an obstacle (GameDesign.md §3): rock, ruins, the Species
// temples and caves. Features block movement and line of sight and do not take damage in the
// first version, so a feature is placed once and never changes; it is a record rather than a
// constant because the landscape generator scatters them and the snapshot must carry them.
//
// A deposit is not a feature. It is a point on the landscape an extractor is built on, and it
// lives in the landscape's definition (Content/LandscapeDefinition.h), not here.

namespace Frontier
{

struct Feature
{
  std::uint32_t design; ///< Row index in the feature table
  std::uint32_t cellX;  ///< A feature occupies whole cells, as a structure does
  std::uint32_t cellY;
  std::int32_t y;       ///< Subunits
  std::uint16_t facing; ///< Binary angle, so that scattered scenery is not all aligned

  [[nodiscard]] constexpr bool operator==(const Feature&) const noexcept = default;
};

} // namespace Frontier
