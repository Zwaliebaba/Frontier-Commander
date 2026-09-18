#pragma once

#include "Landscape.h"

#include "BinaryAngle.h"
#include "FixedPoint.h"

#include <cstdint>

// Stage 6 of the tick (TechnicalDesign.md §4.8): devices walk the routes the planner found, at the
// speed their parts derive, across the terrain their drive can take (GameDesign.md §6;
// m1-vertical-slice/S8). Integers throughout, and the tick is the clock.
//
// WHAT SLOPE COSTS, AND WHY IT IS SCALED TO THE DRIVE'S OWN LIMIT. GameDesign.md §2 says "slope
// costs movement" and §6 says a drive "sets speed as a function of terrain", and then the drive
// table gives a flat speed factor, a maximum slope, a water flag and a hit-point factor - and no
// terrain column at all. OpenQuestions.md Q23 put that gap to the owner, who answered on
// 2026-09-18: speed falls LINEARLY from full on flat ground to half at the drive's OWN maximum
// slope, and beyond that maximum the ground is impassable, which is the rule the cluster graph
// already cuts on. Scaling each curve to the drive's own limit is the whole of the answer: on a
// 20% slope tracks keep three quarters of their speed where wheels keep three fifths, so the max
// slope column buys handling on ground both can cross and not merely reach on ground one cannot.
//
// A DEVICE WALKS CELLS AND STEERS IN SUBUNITS. The route is a sequence of cells, because that is
// what the obstruction grid and the passability rules speak in (Sim/Path.h); the device walks at
// the middle of the next cell of it, and at the destination itself for the last. How far along the
// route it has walked is on the DEVICE and not in the route, because two devices can share a
// destination and therefore a route, and cannot share a position.
//
// A DESTROYED DEVICE MUST HAVE ITS REQUEST CANCELLED. The planner keys its jobs by device id and
// an id is never issued twice, so a job whose device has gone is never asked about again - but it
// stays in the queue and, if it was still being searched, goes on taking budget from the devices
// that are still alive. Nothing removes a device yet; S10 is the task that will, and
// PathPlanner::Cancel is what it calls when it does.
//
// NOTHING HERE READS A CLOCK OR A FLOAT. The turn rate is a binary angle a tick, the speed is
// subunits a tick, the stall is a count of ticks, and the height under a device is a bilinear blend
// of four int16 samples with one division at the end (AGENTS.md R16).

namespace Frontier
{

class Sim;

/// How fast a device turns: a full turn a second, which at 20 ticks is 65,536 / 20. One rate for
/// every device until a chassis row states its own; a turn rate that is a property of the chassis
/// is a table column and this is a constant, and the difference is a content change and not a
/// rewrite.
inline constexpr Neuron::BinaryAngle TURN_BINARY_ANGLE_PER_TICK = 3277;

/// What is left of a drive's speed at the steepest slope it can climb (OpenQuestions.md Q23, owner
/// 2026-09-18). Full speed on the flat, this at the limit, and nothing at all beyond it.
inline constexpr std::int32_t TERRAIN_FACTOR_AT_LIMIT_PERCENT = 50;

/// How near its destination a device has to be to have arrived: a quarter of a cell, which is
/// about two device widths, so that a squad told to go to one point stops around it rather than
/// grinding into it.
inline constexpr std::int32_t ARRIVAL_SUBUNITS = Neuron::SUBUNITS_PER_CELL / 4;

/// The least a device must cover in a tick to count as having made headway. A sixteenth of a world
/// unit: enough that a device inching round an obstacle is not called stuck, small enough that one
/// pressed against a wall is.
inline constexpr std::int32_t PROGRESS_SUBUNITS = 16;

/// How long a device may make no headway before it throws its route away and asks for another.
/// Two seconds, which is longer than the one second a full turn on the spot takes, so turning is
/// never mistaken for being stuck.
inline constexpr std::uint32_t STUCK_TICKS = static_cast<std::uint32_t>(2 * Neuron::TICKS_PER_SECOND);

/// What is left of a drive's speed on a cell of this slope: 100 on the flat, falling linearly to
/// TERRAIN_FACTOR_AT_LIMIT_PERCENT at the drive's own maximum, and 0 beyond it, which is ground
/// the drive cannot stand on at all. A drive row that states no maximum climbs nothing that is not
/// flat, rather than dividing by nought.
[[nodiscard]] constexpr std::int32_t TerrainFactorPercent(std::int32_t _slopePercent, std::int32_t _maxSlopePercent) noexcept
{
  if (_slopePercent <= 0)
  {
    return 100;
  }
  if (_maxSlopePercent <= 0 || _slopePercent > _maxSlopePercent)
  {
    return 0;
  }
  return 100 - (100 - TERRAIN_FACTOR_AT_LIMIT_PERCENT) * _slopePercent / _maxSlopePercent;
}

/// The height of the ground under a point, in subunits: the bilinear blend of the four heightfield
/// samples around it. Blended rather than taken from the cell, because a device crossing a slope
/// would otherwise step down 16 world units at a time (TechnicalDesign.md §4.4 spaces the samples
/// every 16). A point off the landscape reads the nearest edge.
[[nodiscard]] std::int32_t GroundHeightSubunits(const Landscape& _landscape, std::int32_t _x, std::int32_t _z) noexcept;

/// Stage 6: plan what is waiting, walk every device along its route, then steer them apart.
void AdvanceMovement(Sim& _sim);

} // namespace Frontier
