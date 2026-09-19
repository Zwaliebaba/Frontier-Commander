#pragma once

#include "Deposit.h"
#include "HeightDelta.h"
#include "Landscape.h"
#include "Seat.h"
#include "Structure.h"
#include "World.h"

#include "ContentTree.h"
#include "StructureDesc.h"

#include <cstdint>

// Where a structure may stand (GameDesign.md §5), as pure functions over the landscape, the world
// and the seat. Order validation asks before it accepts a PlaceStructure, and the construction
// system asks again before a builder begins one, because a site legal when it was planned may have
// been built over since.
//
// THE SLOPE RULE IS THE STEEPEST CELL UNDER THE FOOTPRINT, which is Landscape::Cell's
// slopePercent - the steepest step between two adjacent samples as a percentage of the spacing.
// The design says "the slope across the footprint is under 25%", and the other reading of that -
// the relief from the lowest sample to the highest over how far the footprint reaches - was built
// first and then measured against this one. Two things decided it.
//
// The number. 25 is the same number and the same unit GameData\Components.json gives Wheels for
// maxSlopePercent, and that one is read per cell (Sim/ClusterGraph.h). One figure written twice in
// one design means one unit.
//
// What each refuses. Over the 795 dry three-by-three sites of the built-in Small landscape, the
// average gradient puts 4 of them over 25% and the steepest cell puts 31 - so neither is anywhere
// near prohibitive, and the harshness this reading was first rejected for is 3.4% of the map. What
// the steepest cell refuses and the average waves through is a footprint straddling a CLIFF EDGE:
// eight gentle cells and one vertical one average out to nothing, and that is exactly the site a
// factory must not stand on. The reverse case does not exist, because ground that is uniformly
// steep enough to fail the average has every one of its cells steep too.

namespace Frontier
{

/// The most slope a footprint may lie across, as a percentage (GameDesign.md §5).
inline constexpr std::uint32_t MAX_PLACEMENT_SLOPE_PERCENT = 25;

/// The rectangle of cells a structure stands on.
struct Footprint
{
  std::uint32_t cellX; ///< The lowest cell, which is what Structure stores
  std::uint32_t cellY;
  std::uint32_t cellsX;
  std::uint32_t cellsY;

  [[nodiscard]] constexpr bool operator==(const Footprint&) const noexcept = default;
};

/// Why a footprint cannot be built on; Accepted is the only value a placement goes ahead on. The
/// order they are tested in is the order a commander would want to be told about them.
enum class PlacementFault : std::uint8_t
{
  Accepted,
  OffLandscape,
  Occupied,   ///< Another structure that occupies its cells, or a feature
  Water,      ///< A cell whose lowest sample is under the sea
  TooSteep,   ///< More than MAX_PLACEMENT_SLOPE_PERCENT across it
  Unexplored, ///< The commander has not seen every cell of it
  NotOnDeposit
};

[[nodiscard]] constexpr Footprint FootprintAt(const StructureDesc& _row, std::uint32_t _cellX, std::uint32_t _cellY) noexcept
{
  return {_cellX, _cellY, _row.footprintCellsX, _row.footprintCellsY};
}

/// The footprint a structure occupies. Without the tables its row is unknown and one cell is the
/// honest answer: it is where the structure certainly is, and never claims ground it may not hold.
[[nodiscard]] Footprint FootprintOf(const Structure& _structure, const ContentTree* _content) noexcept;

[[nodiscard]] bool Overlaps(const Footprint& _a, const Footprint& _b) noexcept;

/// What the placement rule is judged against. Null members are absences rather than defaults: no
/// tables means the row's size and role cannot be read, no deposits means the extractor rule
/// cannot be tested, and neither is taken as permission.
struct PlacementQuery
{
  const Landscape* landscape;
  const World* world;
  const Seat* seat;
  const StructureDesc* row = nullptr;
  const ContentTree* content = nullptr;
  const DepositField* deposits = nullptr;
};

/// The whole of GameDesign.md §5's placement rule.
[[nodiscard]] PlacementFault CheckPlacement(const Footprint& _footprint, const PlacementQuery& _query);

/// The steepest cell under the footprint, as the percentage Landscape::Cell carries; UINT32_MAX
/// when the footprint is not on the landscape at all.
[[nodiscard]] std::uint32_t FootprintSlopePercent(const Landscape& _landscape, const Footprint& _footprint) noexcept;

/// The mean of the samples under the footprint, in whole world units, rounded toward zero. What
/// the flatten levels it to (GameDesign.md §5).
[[nodiscard]] std::int32_t FootprintMeanHeightWorldUnits(const Landscape& _landscape, const Footprint& _footprint) noexcept;

/// The delta that levels the footprint to its mean. Its rectangle is every sample the footprint's
/// cells own, which is four a cell plus the boundary sample on the far side, so that the cells the
/// structure stands on are flat to their edges; an empty rectangle when the footprint is off the
/// landscape.
[[nodiscard]] HeightDelta FlattenDelta(const Landscape& _landscape, const Footprint& _footprint);

} // namespace Frontier
