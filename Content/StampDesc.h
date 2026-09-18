#pragma once

#include <cstdint>
#include <string>
#include <vector>

// An authored terrain patch (SpeciesTerrain.md §2; TechnicalDesign.md §8): a small grid of height
// offsets a landscape definition places at a cell, so that a plateau, a pass or a crater can be
// authored rather than generated. Heights are whole world units, as the landscape's samples are.

namespace Frontier
{

inline constexpr std::uint32_t STAMP_DESC_VERSION = 1;

struct StampDesc
{
  std::uint32_t version;
  std::string id;
  std::string name;
  std::uint32_t samplesPerSideX;
  std::uint32_t samplesPerSideY;
  /// samplesPerSideX * samplesPerSideY offsets, row by row, added to the generated heights.
  std::vector<std::int32_t> heightOffsets;
  /// Whether the stamp replaces the generated heights instead of adding to them.
  bool replaces;

  [[nodiscard]] bool operator==(const StampDesc&) const noexcept = default;
};

} // namespace Frontier
