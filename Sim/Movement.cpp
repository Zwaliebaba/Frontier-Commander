#include "pch.h"

#include "Movement.h"
#include "Production.h"
#include "Sim.h"
#include "Steering.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace Frontier
{

namespace
{

/// The spacing of the heightfield samples, in subunits.
constexpr std::int32_t SAMPLE_SPACING_SUBUNITS = SAMPLE_SPACING_WORLD_UNITS * Neuron::SUBUNITS_PER_WORLD_UNIT;

/// One device as this stage needs it: what it is, what it drives on, and how fast. Collected once
/// a tick so that the walk and the separation that follows read the same derivation rather than
/// deriving it twice and risking two answers.
struct Mover
{
  ObjectId id;
  DesignStats stats;
  DriveClass drive;
  bool derived; ///< False when the design no longer names a buildable thing; such a device stands
};

[[nodiscard]] std::uint32_t CellOf(std::int32_t _subunits, std::uint32_t _cellsPerSide) noexcept
{
  const std::int32_t cell = _subunits >> Neuron::SUBUNITS_PER_CELL_SHIFT; // Arithmetic: left of nought floors
  return static_cast<std::uint32_t>(std::clamp(cell, 0, static_cast<std::int32_t>(_cellsPerSide) - 1));
}

[[nodiscard]] std::int32_t CellMiddle(std::uint32_t _cell) noexcept
{
  return static_cast<std::int32_t>(_cell) * Neuron::SUBUNITS_PER_CELL + Neuron::SUBUNITS_PER_CELL / 2;
}

/// The statistics and the drive class a device moves by. False when its design no longer names a
/// buildable thing - a content reload, a design slot overwritten - which is a device that stands
/// still rather than one that moves by a guess.
[[nodiscard]] bool MovementOf(const Sim& _sim, const Device& _device, DesignStats& _stats, DriveClass& _drive)
{
  if (_device.seat >= _sim.Seats().size())
  {
    return false;
  }
  const Seat& seat = _sim.Seats()[_device.seat];
  if (DeriveSeatDesign(seat, _sim.Content(), _device.design, _stats) != DesignFault::None)
  {
    return false;
  }
  const std::uint32_t row = seat.designs[_device.design].drive;
  if (row >= _sim.Content().components.drives.size())
  {
    return false;
  }
  _drive = _sim.Content().components.drives[row].driveClass;
  return true;
}

/// Asks the planner for a route from where the device stands to where it is going, and marks the
/// device as being at the start of one. The search itself begins on the next tick's budget, which
/// is what the standing still of the tick after an order is.
void RequestRoute(Sim& _sim, ObjectId _id, Device& _device, DriveClass _drive)
{
  const std::uint32_t cells = _sim.Terrain().CellsPerSide();
  _sim.Planner().Request(_id, CellOf(_device.x, cells), CellOf(_device.z, cells), CellOf(_device.destinationX, cells),
                         CellOf(_device.destinationZ, cells), _drive);
  _device.pathIndex = 0;
  _device.stalledTicks = 0;
}

void DropRoute(Sim& _sim, ObjectId _id, Device& _device)
{
  if (_device.pathIndex != NO_PATH_INDEX)
  {
    _sim.Planner().Cancel(_id);
    _device.pathIndex = NO_PATH_INDEX;
  }
  _device.stalledTicks = 0;
}

/// What each order does on reaching what it was sent to (Sim/Order.h's table).
void Arrive(Sim& _sim, ObjectId _id, Device& _device)
{
  if (_device.primaryOrder == PrimaryOrder::Move || _device.primaryOrder == PrimaryOrder::AttackMove)
  {
    // There is nothing further to do. An AttackMove that met something on the way was interrupted
    // by S10 before it ever got here; one that met nothing has simply walked to where it was sent.
    _device.primaryOrder = PrimaryOrder::Stop;
  }
  else if (_device.primaryOrder == PrimaryOrder::Patrol)
  {
    // The far end becomes the near one and it sets off back. GameDesign.md names the order and
    // says no more about it; a patrol that stopped at the far end would not be a patrol.
    std::swap(_device.destinationX, _device.anchorX);
    std::swap(_device.destinationZ, _device.anchorZ);
  }
  // Guard is the one that does nothing here: it is at its post, the order stands, and whatever
  // pushes it off sends it back on a later tick.
  DropRoute(_sim, _id, _device);
}

/// One device's tick of walking. Separation comes after every device has walked, so that what it
/// steers around is where things ended up and not a mixture of this tick and the last.
void Walk(Sim& _sim, ObjectId _id, Device& _device, const Mover& _mover)
{
  const Landscape& landscape = _sim.Terrain();
  const std::int32_t startX = _device.x;
  const std::int32_t startZ = _device.z;

  if (_device.primaryOrder == PrimaryOrder::Stop)
  {
    DropRoute(_sim, _id, _device);
    return;
  }
  if (_device.primaryOrder == PrimaryOrder::Attack || _device.primaryOrder == PrimaryOrder::ReturnToRepair)
  {
    // Both are S10's: one closes on a target and one falls back to a repair bay, and neither has
    // anywhere to walk until the system that chooses those places exists. They hold their ground.
    return;
  }

  if (Neuron::LengthSquared(_device.destinationX - _device.x, _device.destinationZ - _device.z) <=
      static_cast<std::int64_t>(ARRIVAL_SUBUNITS) * ARRIVAL_SUBUNITS)
  {
    Arrive(_sim, _id, _device);
    return;
  }

  if (_device.pathIndex == NO_PATH_INDEX)
  {
    RequestRoute(_sim, _id, _device, _mover.drive);
    return; // The budget searches it on the next tick; standing still is what waiting for it is.
  }

  const Path* path = _sim.Planner().Result(_id);
  if (path == nullptr || path->state == PathState::Unreachable || path->state == PathState::Refused)
  {
    // Nowhere to go. The device stops rather than walking at ground it cannot cross: the order was
    // valid when it was given, so there is no rejection to report, and a commander who asked for
    // the impossible sees it stand where it was.
    _device.primaryOrder = PrimaryOrder::Stop;
    DropRoute(_sim, _id, _device);
    return;
  }
  if (!path->Usable())
  {
    return; // Still being searched. Waiting is not being stuck, so the stall count does not move.
  }

  // Refine the next clusters as it reaches the end of the ones it has, which is what keeps a
  // Frontier-sized route off one tick (Sim/PathPlanner.h).
  if (path->state == PathState::Partial && _device.pathIndex + 1 >= path->cells.size())
  {
    (void)_sim.Planner().RefineFurther(_id);
    path = _sim.Planner().Result(_id);
    if (path == nullptr || !path->Usable())
    {
      return;
    }
  }

  const std::uint32_t cells = landscape.CellsPerSide();
  const std::int32_t slopePercent = landscape.CellAt(CellOf(_device.x, cells), CellOf(_device.z, cells)).slopePercent;
  const std::int32_t step =
    Neuron::MulDiv(_mover.stats.speedSubunitsPerTick, TerrainFactorPercent(slopePercent, _mover.stats.maxSlopePercent), 100);

  // The tick's distance is spent along the route, over as many legs as it reaches: a device that
  // passes a cell's middle with something left of its step goes on down the next leg with it,
  // rather than stopping short and losing the remainder. Losing it would cost a twentieth of the
  // speed on a route of cell-sized legs, which is a crossing time that no longer matches the
  // speed the design screen printed.
  std::int32_t left = step;
  bool turned = false;
  while (left > 0)
  {
    std::int32_t targetX = _device.destinationX;
    std::int32_t targetZ = _device.destinationZ;
    const bool onRoute = _device.pathIndex < path->cells.size();
    if (onRoute)
    {
      const PathCell& cell = path->cells[_device.pathIndex];
      targetX = CellMiddle(cell.x);
      targetZ = CellMiddle(cell.y);
      // The last cell of a finished route is the destination's own cell, so the device walks at
      // the point it was sent to rather than at the middle of the cell holding it.
      if (path->state == PathState::Complete && _device.pathIndex + 1 == path->cells.size())
      {
        targetX = _device.destinationX;
        targetZ = _device.destinationZ;
      }
    }
    else if (path->state != PathState::Complete)
    {
      break; // Walked to the end of what is refined and the planner has not reached the rest yet.
    }

    const std::int32_t toTargetX = targetX - _device.x;
    const std::int32_t toTargetZ = targetZ - _device.z;
    if (!turned)
    {
      // The heading turns once a tick, toward the leg being walked. A device more than a quarter
      // turn off it turns on the spot rather than sliding sideways, so coming about costs time.
      const Neuron::BinaryAngle wanted = Neuron::AngleOf(toTargetX, toTargetZ);
      _device.facing = Neuron::TurnToward(_device.facing, wanted, TURN_BINARY_ANGLE_PER_TICK);
      const std::int32_t remainingTurn = Neuron::TurnBetween(_device.facing, wanted);
      turned = true;
      if ((remainingTurn < 0 ? -remainingTurn : remainingTurn) >= Neuron::QUARTER_TURN)
      {
        break;
      }
    }

    const auto reach = static_cast<std::int32_t>(Neuron::Length(toTargetX, toTargetZ));
    const bool reached = reach <= left;
    const std::int32_t nextX = reached ? targetX : _device.x + Neuron::MulDiv(toTargetX, left, reach);
    const std::int32_t nextZ = reached ? targetZ : _device.z + Neuron::MulDiv(toTargetZ, left, reach);
    if (!_sim.Clusters().Passable(CellOf(nextX, cells), CellOf(nextZ, cells), _mover.drive))
    {
      // A step onto ground the drive cannot hold is not taken. The tick then counts as no headway,
      // which is what sends the device back to the planner if it goes on happening.
      break;
    }
    _device.x = nextX;
    _device.z = nextZ;
    if (!reached)
    {
      break;
    }
    left -= reach;
    if (!onRoute)
    {
      break; // At the destination itself; the next tick's arrival is what ends the order.
    }
    ++_device.pathIndex;
  }

  _device.y = GroundHeightSubunits(landscape, _device.x, _device.z);

  if (Neuron::LengthSquared(_device.x - startX, _device.z - startZ) >= static_cast<std::int64_t>(PROGRESS_SUBUNITS) * PROGRESS_SUBUNITS)
  {
    _device.stalledTicks = 0;
    return;
  }
  ++_device.stalledTicks;
  if (_device.stalledTicks >= STUCK_TICKS)
  {
    // It has not moved for two seconds, so the route it holds is not one it can walk: it throws
    // the route away and asks for another from where it actually is.
    DropRoute(_sim, _id, _device);
  }
}

} // namespace

std::int32_t GroundHeightSubunits(const Landscape& _landscape, std::int32_t _x, std::int32_t _z) noexcept
{
  if (!_landscape.Created())
  {
    return OUTSIDE_HEIGHT * Neuron::SUBUNITS_PER_WORLD_UNIT;
  }
  const auto last = static_cast<std::int64_t>(_landscape.SamplesPerSide()) - 1;
  const std::int64_t edge = last * SAMPLE_SPACING_SUBUNITS;
  const std::int64_t x = std::clamp<std::int64_t>(_x, 0, edge);
  const std::int64_t z = std::clamp<std::int64_t>(_z, 0, edge);
  const auto sampleX = static_cast<std::uint32_t>(x / SAMPLE_SPACING_SUBUNITS);
  const auto sampleZ = static_cast<std::uint32_t>(z / SAMPLE_SPACING_SUBUNITS);
  const auto nextX = static_cast<std::uint32_t>(std::min<std::int64_t>(sampleX + 1, last));
  const auto nextZ = static_cast<std::uint32_t>(std::min<std::int64_t>(sampleZ + 1, last));
  const std::int64_t fractionX = x % SAMPLE_SPACING_SUBUNITS;
  const std::int64_t fractionZ = z % SAMPLE_SPACING_SUBUNITS;

  // Bilinear between the four samples, multiplied out over the spacing squared so that there is
  // one division at the end and no rounding for two hosts to disagree about (AGENTS.md R16).
  const std::int64_t blended =
    static_cast<std::int64_t>(_landscape.HeightAt(sampleX, sampleZ)) * (SAMPLE_SPACING_SUBUNITS - fractionX) *
      (SAMPLE_SPACING_SUBUNITS - fractionZ) +
    static_cast<std::int64_t>(_landscape.HeightAt(nextX, sampleZ)) * fractionX * (SAMPLE_SPACING_SUBUNITS - fractionZ) +
    static_cast<std::int64_t>(_landscape.HeightAt(sampleX, nextZ)) * (SAMPLE_SPACING_SUBUNITS - fractionX) * fractionZ +
    static_cast<std::int64_t>(_landscape.HeightAt(nextX, nextZ)) * fractionX * fractionZ;
  constexpr std::int64_t SQUARE = static_cast<std::int64_t>(SAMPLE_SPACING_SUBUNITS) * SAMPLE_SPACING_SUBUNITS;
  return static_cast<std::int32_t>(blended * Neuron::SUBUNITS_PER_WORLD_UNIT / SQUARE);
}

void AdvanceMovement(Sim& _sim)
{
  // Planning first: a device that asked last tick may have its route now, and the budget is what
  // bounds the wait (TechnicalDesign.md §4.5).
  _sim.Planner().Advance();
  if (!_sim.Terrain().Created())
  {
    return;
  }

  World& world = _sim.Objects();
  std::vector<Mover> movers;
  world.ForEachDevice(
    [&_sim, &movers](ObjectId _id, const Device& _device)
    {
      Mover mover{_id, {}, DriveClass::Wheels, false};
      mover.derived = MovementOf(_sim, _device, mover.stats, mover.drive);
      movers.push_back(mover);
    });

  // One entry per mover and in the same order, because the separation that follows indexes the two
  // together: nothing removes a device between the walk above and the pass below.
  std::vector<SteeredDevice> steered;
  steered.reserve(movers.size());
  for (const Mover& mover : movers)
  {
    Device* device = world.FindDevice(mover.id);
    FRONTIER_ASSERT(device != nullptr);
    if (device == nullptr)
    {
      steered.push_back({mover.id, 0, 0});
      continue;
    }
    if (mover.derived)
    {
      Walk(_sim, mover.id, *device, mover);
    }
    steered.push_back({mover.id, device->x, device->z});
  }

  // Separation last, over where every device ended up, so that what one steers around is this
  // tick's positions and not a mixture of this tick's and the last's.
  std::vector<SeparationPush> pushes(steered.size());
  Separate(steered, pushes);
  const std::uint32_t cells = _sim.Terrain().CellsPerSide();
  for (std::size_t index = 0; index < steered.size(); ++index)
  {
    if ((pushes[index].x == 0 && pushes[index].z == 0) || !movers[index].derived)
    {
      continue;
    }
    Device* device = world.FindDevice(steered[index].id);
    if (device == nullptr)
    {
      continue;
    }
    const std::int32_t nextX = device->x + pushes[index].x;
    const std::int32_t nextZ = device->z + pushes[index].z;
    if (!_sim.Clusters().Passable(CellOf(nextX, cells), CellOf(nextZ, cells), movers[index].drive))
    {
      continue; // Steering never pushes a device onto ground its drive cannot hold.
    }
    device->x = nextX;
    device->z = nextZ;
    device->y = GroundHeightSubunits(_sim.Terrain(), device->x, device->z);
  }
}

} // namespace Frontier
