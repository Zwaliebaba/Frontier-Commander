#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// The landscape definition (TechnicalDesign.md §4.4; ImplementationPlan.md §6, item 3): what a
// landscape is generated from, and all a snapshot or a joining client needs to reproduce every
// sample. A plain aggregate (AGENTS.md R8) in Content from M0, so that Sim reads it and the JSON
// loader of M1 (m1-vertical-slice/C1) fills it without moving it. Its JSON form is what
// `Tools/LandscapeTool.py --define` writes, field for field, and the fixtures under
// Tests/SimTests/Fixtures/Landscape are twenty of them.

namespace Frontier
{

/// The landscape sizes of GameDesign.md §3; SIZE_CLASS_CELLS is the extent of each, in cells.
enum class SizeClass : std::uint8_t
{
  Small,
  Medium,
  Large,
  Frontier
};

inline constexpr std::array<std::uint32_t, 4> SIZE_CLASS_CELLS = {128, 256, 512, 1024};

/// Samples per cell edge and their spacing: a cell of 64 world units holds four samples a side.
inline constexpr std::uint32_t SAMPLES_PER_CELL_EDGE = 4;
inline constexpr std::int32_t SAMPLE_SPACING_WORLD_UNITS = 16;

/// The height every sample starts at and the plain a tile's border sits on, in whole world units;
/// sea level is 0, so the outside is under water.
inline constexpr std::int32_t OUTSIDE_HEIGHT = -26;

/// The samples a side of a landscape: cells times four, plus the closing sample.
[[nodiscard]] constexpr std::uint32_t SamplesPerSide(std::uint32_t _cellsPerSide) noexcept
{
  return _cellsPerSide * SAMPLES_PER_CELL_EDGE + 1;
}

/// One diamond-square tile of the recipe, as the tool's Tile dataclass has it.
struct LandscapeTile
{
  std::int32_t x; ///< The tile's origin in landscape samples; may be off the landscape
  std::int32_t y;
  std::uint32_t extent; ///< A power of two of at least 2; the grid is extent + 1 samples a side
  std::int32_t fractalDimensionHundredths;
  std::int32_t amplitude;     ///< Whole units, the first level's noise
  std::int32_t desiredHeight; ///< Whole units, the tile's peak after the merge
  std::int32_t heightShift;   ///< Whole units, added to every sample before the falloff
  std::int32_t lowlandExponentHundredths;
  std::uint8_t method;       ///< 0 mean of four, 1 one pair, 2 one sample
  std::uint32_t edgeFalloff; ///< Samples from the border over which the tile is pulled to the plain
  /// The biome this tile is coloured by, or empty for the landscape's own (OpenQuestions.md Q18,
  /// owner 2026-09-18). Two tiles of different biomes blend across their overlap by the same
  /// edgeFalloff weight their heights merge by, so a region boundary is where the ground already
  /// changes. It colours and never generates: the heights are the same whatever this says, which
  /// is why it is not in the state hash (Landscape::AddToHash) though the snapshot carries it.
  std::string palette;

  [[nodiscard]] bool operator==(const LandscapeTile&) const noexcept = default;
};

struct CellPosition
{
  std::uint32_t x;
  std::uint32_t y;

  [[nodiscard]] constexpr bool operator==(const CellPosition&) const noexcept = default;
};

inline constexpr std::uint32_t LANDSCAPE_DEFINITION_VERSION = 1;

struct LandscapeDefinition
{
  std::uint32_t version;
  SizeClass sizeClass;
  std::uint32_t cellsPerSide;
  std::uint64_t seed;
  std::string palette;
  std::vector<LandscapeTile> tiles;
  std::vector<CellPosition> starts;
  std::vector<CellPosition> deposits;

  [[nodiscard]] bool operator==(const LandscapeDefinition&) const noexcept = default;
};

} // namespace Frontier
