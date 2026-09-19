#pragma once

#include "Structure.h"

#include <cstdint>

// A plan is a placed structure no builder has begun (GameDesign.md §5), and it is NOT a record of
// its own: it is a Structure in StructurePhase::Plan. A second record would be a second thing to
// keep in the hash, in the snapshot and in the render view, and every one of the four transitions
// below would have to move a structure between two containers without ever losing or duplicating
// it. One record with a state is the same information and cannot desynchronise from itself.
//
// Plan -> UnderConstruction -> Standing, with Demolishing between Standing and gone, and a plan or
// a site cancelled straight out of the world. What the state decides, and the whole of what it
// decides, is in this header: whether the cells are occupied, and whether the commander has paid.

namespace Frontier
{

/// The most plans one commander may have waiting (GameDesign.md §5). A plan costs nothing, so
/// without a limit a commander could paper the landscape with them for free and read the fog off
/// the placement rejections.
inline constexpr std::uint32_t MAX_PLANS_PER_SEAT = 64;

/// Whether a structure in this state occupies its cells - blocks a placement, obstructs the
/// pathfinder, and is a thing a shot can hit. A plan does not; everything else does.
[[nodiscard]] constexpr bool Occupies(StructurePhase _state) noexcept
{
  return _state != StructurePhase::Plan;
}

/// Whether the commander has paid for it. The cost is drawn when construction BEGINS
/// (GameDesign.md §4), so a plan is the one state that has cost nothing.
[[nodiscard]] constexpr bool Paid(StructurePhase _state) noexcept
{
  return _state != StructurePhase::Plan;
}

} // namespace Frontier
