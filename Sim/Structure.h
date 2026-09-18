#pragma once

#include "ObjectId.h"

#include <array>
#include <cstdint>

// A structure and its modules (GameDesign.md §5). Simulation fields only, in the units of
// TechnicalDesign.md §4.1. A structure occupies whole cells, so its position is the cell its
// footprint starts at rather than a subunit point: placement is on the grid and the pathfinder
// and the obstruction grid read cells (S4).

namespace Frontier
{

/// The most modules a structure may carry: a factory's three, a lab's two, with room to spare
/// (GameDesign.md §5). A module is a row index in the structure-module table, as a design is.
inline constexpr std::uint32_t MAX_STRUCTURE_MODULES = 4;

/// What a structure is doing, which is all a tick needs to tell apart. S4 owns the transitions.
enum class StructureState : std::uint8_t
{
  Plan, ///< Placed by the commander, no builder has started; occupies nothing
  UnderConstruction,
  Standing,
  Demolishing
};

struct Structure
{
  std::uint8_t seat;
  std::uint32_t design; ///< Row index in the structure table

  std::uint32_t cellX; ///< The footprint's lowest cell; the row gives its extent
  std::uint32_t cellY;
  std::int32_t y; ///< The flattened height under the footprint, in subunits

  StructureState state;
  std::int32_t hitPoints;
  std::int32_t buildProgressHundredths; ///< Of the row's cost; 10,000 is complete

  std::array<std::uint32_t, MAX_STRUCTURE_MODULES> modules; ///< Row indices; the first moduleCount count
  std::uint8_t moduleCount;

  /// What the structure is working on: the device a factory is building, the item a lab is
  /// researching, or NO_OBJECT. A production queue is S5's and hangs off the seat, not here.
  ObjectId working;
  std::uint32_t workRemainingTicks;

  [[nodiscard]] constexpr bool operator==(const Structure&) const noexcept = default;
};

} // namespace Frontier
