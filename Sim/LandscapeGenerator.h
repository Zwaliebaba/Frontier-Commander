#pragma once

#include "LandscapeDefinition.h"

#include <cstdint>
#include <vector>

// The landscape generator (TechnicalDesign.md §4.4), the C++ of Tools/LandscapeTool.py: the tool
// is the specification and this file copies it operation for operation, in the same integer
// arithmetic on the same generator, so that the same definition gives the same int16 samples on
// every machine, and the tests hold this port to the goldens the tool wrote. Every product the
// tool marks as 64-bit widens here; every right shift of a negative value is arithmetic (C++20);
// every division is on non-negative operands.

namespace Frontier
{

/// Heights inside the generator are 24.8 fixed point: whole units times FIXED_ONE.
inline constexpr std::int32_t HEIGHT_FIXED_ONE = 256;

class LandscapeGenerator
{
public:
  /// Whether a tile is one the generator can take: a power-of-two extent of at least 2, a fractal
  /// dimension inside the table, a lowland exponent the table has, a method of 0, 1 or 2.
  [[nodiscard]] static bool IsValid(const LandscapeTile& _tile) noexcept;

  /// The tile's grid, (extent + 1)^2 samples in 24.8 fixed point, row-major, generated on a
  /// generator seeded with _tileSeed exactly as the tool's generate_tile.
  static void GenerateTile(const LandscapeTile& _tile, std::uint64_t _tileSeed, std::vector<std::int32_t>& _grid);

  /// Merges a tile's grid into the landscape by maximum, rescaled to the tile's desired height,
  /// exactly as the tool's merge_tile.
  static void MergeTile(std::vector<std::int32_t>& _land, std::uint32_t _samplesPerSide, const LandscapeTile& _tile,
                        const std::vector<std::int32_t>& _grid);

  /// The whole field of a definition as int16 whole units, samplesPerSide^2 row-major: every tile
  /// generated on DeriveSeed(seed, index) and merged in order. False, with _heights untouched, for
  /// a tile the generator cannot take or a height outside int16.
  [[nodiscard]] static bool Generate(const LandscapeDefinition& _definition, std::vector<std::int16_t>& _heights);
};

} // namespace Frontier
