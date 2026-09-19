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

/// A module slot that holds no module, and what moduleUnderConstruction carries when a structure
/// is building none. It is not zero because zero is a real row, and a Structure{} has to mean "no
/// module" rather than "building the first one in the table" - which is why the field below
/// carries a default member initialiser rather than relying on value initialisation.
inline constexpr std::uint32_t NO_STRUCTURE_MODULE = 0xFFFFFFFFu;

/// What a structure is doing, which is all a tick needs to tell apart. S4 owns the transitions.
/// Where a structure is in its life (GameDesign.md §5). Named for the phase rather than the state
/// because Net's wire record for a structure is StructureState (TechnicalDesign.md §5.3), and one
/// namespace cannot hold both.
enum class StructurePhase : std::uint8_t
{
  Plan, ///< Placed by the commander, no builder has started; occupies nothing
  UnderConstruction,
  Standing,
  Demolishing
};

inline constexpr std::uint8_t STRUCTURE_PHASE_COUNT = 4;

struct Structure
{
  std::uint8_t seat;
  std::uint32_t design; ///< Row index in the structure table

  std::uint32_t cellX; ///< The footprint's lowest cell; the row gives its extent
  std::uint32_t cellY;
  std::int32_t y; ///< The flattened height under the footprint, in subunits

  StructurePhase state;
  std::int32_t hitPoints;
  /// What the attending builders have put in, in hundredths of build power summed over ticks
  /// (Sim/Construction.h): complete at the row's buildTimeTicks times the reference builder's
  /// rate. The accumulator is the builders' own contribution rather than a percentage of the
  /// whole, because a percentage per tick does not divide - a hundred percent over a 1,200-tick
  /// factory is 8.33 hundredths a tick - and a truncated percentage never reaches a hundred.
  /// Sim/Construction.h turns it into the percentage the hit points and the refund want.
  std::int32_t buildEffortHundredths;

  std::array<std::uint32_t, MAX_STRUCTURE_MODULES> modules; ///< Row indices; the first moduleCount count
  std::uint8_t moduleCount;

  /// Ticks until the one weapon this structure's row names may fire again (Sim/Weapons.h). A
  /// field of its own rather than a second use of workRemainingTicks below, which is a factory's
  /// production countdown: a mod that puts a weapon on a factory would otherwise have the two
  /// share one counter and each reset the other.
  std::uint32_t reloadTicks;

  /// The module a builder is putting onto this standing structure, or NO_STRUCTURE_MODULE, and
  /// what has gone into it, in the unit buildEffortHundredths uses. A module is built onto a
  /// STANDING structure, so this cannot share the field above: that one reads 10,000 for the
  /// whole of a module's construction and the hit points follow it.
  std::uint32_t moduleUnderConstruction = NO_STRUCTURE_MODULE;
  std::int32_t moduleEffortHundredths;

  /// What the structure is working on: the device a factory is building, the item a lab is
  /// researching, or NO_OBJECT. A production queue is S5's and hangs off the seat, not here.
  ObjectId working;
  std::uint32_t workRemainingTicks;

  [[nodiscard]] constexpr bool operator==(const Structure&) const noexcept = default;
};

} // namespace Frontier
