#include "pch.h"

#include "Damage.h"
#include "Movement.h"
#include "Placement.h"
#include "Plan.h"
#include "Production.h"
#include "Sim.h"

#include <algorithm>
#include <vector>

// Who leaves a fight (GameDesign.md §8; m1-vertical-slice/S10). "A device whose hit points fall
// under its retreat threshold breaks off, returns to the nearest repair bay or repair device...
// It retreats only if one is within 60 cells; otherwise it holds and fights, so that a heavy on a
// large landscape does not spend the match commuting."
//
// M1 SHIPS NEITHER THE BAY NOR THE MODULE, and that is the content's state rather than a gap here:
// StructureRole::RepairBay and SystemKind::Repair both exist and neither has a row in GameData, so
// RepairPointNear always answers no and every device holds and fights. The rule is written and
// tested against a fixture that defines them, which is what the task's acceptance asks for; the
// day a row is authored, the behaviour arrives with it and nothing here changes.
//
// WHAT IS NOT HERE is the repairing and the return afterwards. The design has a retreating device
// "wait to be repaired and return to its guard position"; with nothing in M1 that repairs, a
// device that reached a bay would wait for ever, so the walk back is the task that ships the bay.

namespace Frontier
{

namespace
{

/// The share of its hit points a stance breaks off at. Never is not a threshold and is handled by
/// the caller, so that "never" cannot be a number somebody later compares against.
[[nodiscard]] std::int32_t ThresholdPercent(RetreatStance _stance) noexcept
{
  return _stance == RetreatStance::AtQuarter ? 25 : 50;
}

} // namespace

bool RepairPointNear(const Sim& _sim, std::uint8_t _seat, std::int32_t _x, std::int32_t _z, std::int32_t& _outX, std::int32_t& _outZ)
{
  const ContentTree& content = _sim.Content();
  const auto range = static_cast<std::int64_t>(RETREAT_REPAIR_RANGE_SUBUNITS);
  std::int64_t nearest = 0;
  bool found = false;
  const auto consider = [&](std::int32_t _pointX, std::int32_t _pointZ)
  {
    const std::int64_t distance = Neuron::LengthSquared(_pointX - _x, _pointZ - _z);
    if (distance > range * range || (found && distance >= nearest))
    {
      return;
    }
    nearest = distance;
    _outX = _pointX;
    _outZ = _pointZ;
    found = true;
  };

  _sim.Objects().ForEachStructure(
    [&](ObjectId, const Structure& _structure)
    {
      if (_structure.seat != _seat || _structure.state != StructureState::Standing ||
          _structure.design >= content.structures.structures.size() ||
          content.structures.structures[_structure.design].role != StructureRole::RepairBay)
      {
        return;
      }
      const Footprint footprint = FootprintOf(_structure, &content);
      consider(static_cast<std::int32_t>(footprint.cellX * Neuron::SUBUNITS_PER_CELL + footprint.cellsX * Neuron::SUBUNITS_PER_CELL / 2),
               static_cast<std::int32_t>(footprint.cellY * Neuron::SUBUNITS_PER_CELL + footprint.cellsY * Neuron::SUBUNITS_PER_CELL / 2));
    });

  _sim.Objects().ForEachDevice(
    [&](ObjectId, const Device& _device)
    {
      if (_device.seat != _seat || _seat >= _sim.Seats().size() || _device.design >= _sim.Seats()[_seat].designs.size())
      {
        return;
      }
      const DeviceDesign& design = _sim.Seats()[_seat].designs[_device.design];
      for (std::uint8_t mount = 0; mount < std::min<std::uint8_t>(design.moduleCount, static_cast<std::uint8_t>(MAX_MOUNTS)); ++mount)
      {
        const std::uint32_t row = design.modules[mount];
        if (row < content.components.modules.size() && content.components.modules[row].systemKind == SystemKind::Repair)
        {
          consider(_device.x, _device.z);
          return;
        }
      }
    });
  return found;
}

void AdvanceRetreat(Sim& _sim)
{
  World& world = _sim.Objects();
  std::vector<ObjectId> devices;
  world.ForEachDevice([&devices](ObjectId _id, const Device&) { devices.push_back(_id); });

  for (const ObjectId id : devices)
  {
    Device* device = world.FindDevice(id);
    if (device == nullptr || device->seat >= _sim.Seats().size() || device->retreat == RetreatStance::Never ||
        device->primaryOrder == PrimaryOrder::ReturnToRepair)
    {
      continue;
    }
    const Seat& seat = _sim.Seats()[device->seat];
    DesignStats stats{};
    if (DeriveSeatDesign(seat, _sim.Content(), device->design, stats) != DesignFault::None || stats.hitPoints <= 0)
    {
      continue;
    }
    if (device->hitPoints * 100 >= stats.hitPoints * ThresholdPercent(device->retreat))
    {
      continue;
    }
    std::int32_t x = 0;
    std::int32_t z = 0;
    if (!RepairPointNear(_sim, device->seat, device->x, device->z, x, z))
    {
      continue; // Nothing within 60 cells: it holds and fights, which is the rule and not a failure.
    }
    device->primaryOrder = PrimaryOrder::ReturnToRepair;
    device->destinationX = x;
    device->destinationZ = z;
    device->target = NO_OBJECT;
    // The route it was walking was to somewhere else; stage 6 asks for another from where it is.
    _sim.Planner().Cancel(id);
    device->pathIndex = NO_PATH_INDEX;
    device->stalledTicks = 0;
  }
}

} // namespace Frontier
