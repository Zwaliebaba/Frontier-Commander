#include "pch.h"

#include "Construction.h"

#include "Economy.h"
#include "Plan.h"
#include "Sim.h"

#include <algorithm>
#include <vector>

namespace Frontier
{

namespace
{

/// What a device can build with: the sum of its builder modules' rates and the longest of their
/// reaches. A design with two builders builds twice as fast and reaches as far as its better one.
struct BuilderReach
{
  std::int64_t rangeSubunits = 0;
  std::int64_t powerHundredthsPerTick = 0;
};

[[nodiscard]] BuilderReach BuilderOf(const Device& _device, std::span<const Seat> _seats, const ContentTree& _content)
{
  BuilderReach reach;
  if (_device.seat >= _seats.size())
  {
    return reach;
  }
  const Seat& seat = _seats[_device.seat];
  if (_device.design >= seat.designs.size())
  {
    return reach;
  }
  const DeviceDesign& design = seat.designs[_device.design];
  for (std::uint8_t index = 0; index < design.moduleCount && index < MAX_MOUNTS; ++index)
  {
    const std::uint32_t row = design.modules[index];
    if (row >= _content.components.modules.size())
    {
      continue;
    }
    const ModuleDesc& module = _content.components.modules[row];
    if (module.systemKind != SystemKind::Builder)
    {
      continue;
    }
    reach.rangeSubunits = std::max<std::int64_t>(reach.rangeSubunits, module.systemRangeSubunits);
    reach.powerHundredthsPerTick += module.buildPowerHundredthsPerTick;
  }
  return reach;
}

/// The square of the distance from a point to the nearest part of a footprint, in subunits.
[[nodiscard]] std::int64_t DistanceSquaredTo(const Footprint& _footprint, std::int32_t _x, std::int32_t _z) noexcept
{
  const std::int64_t minX = static_cast<std::int64_t>(_footprint.cellX) * Neuron::SUBUNITS_PER_CELL;
  const std::int64_t minZ = static_cast<std::int64_t>(_footprint.cellY) * Neuron::SUBUNITS_PER_CELL;
  const std::int64_t maxX = minX + static_cast<std::int64_t>(_footprint.cellsX) * Neuron::SUBUNITS_PER_CELL;
  const std::int64_t maxZ = minZ + static_cast<std::int64_t>(_footprint.cellsY) * Neuron::SUBUNITS_PER_CELL;
  const std::int64_t dx = _x < minX ? minX - _x : (_x > maxX ? _x - maxX : 0);
  const std::int64_t dz = _z < minZ ? minZ - _z : (_z > maxZ ? _z - maxZ : 0);
  return dx * dx + dz * dz;
}

[[nodiscard]] const StructureDesc* RowOf(const ContentTree& _content, const Structure& _structure) noexcept
{
  return _structure.design < _content.structures.structures.size() ? &_content.structures.structures[_structure.design] : nullptr;
}

/// Marks or clears every cell of a footprint. Through Sim, because obstruction is what the cluster
/// graph is cut on and Sim is what tells it (Sim::SetObstruction).
void MarkFootprint(Sim& _sim, const Footprint& _footprint, std::uint8_t _obstruction)
{
  for (std::uint32_t y = _footprint.cellY; y < _footprint.cellY + _footprint.cellsY; ++y)
  {
    for (std::uint32_t x = _footprint.cellX; x < _footprint.cellX + _footprint.cellsX; ++x)
    {
      _sim.SetObstruction(x, y, _obstruction);
    }
  }
}

} // namespace

void MarkStandingObstructions(Sim& _sim)
{
  // Ascending by id, which is the order everything else walks the world in, so that two hosts
  // write the same cells in the same order and the graph is cut the same way.
  std::vector<ObjectId> standing;
  _sim.Objects().ForEachStructure(
    [&standing](ObjectId _id, const Structure& _structure)
    {
      if (Occupies(_structure.state))
      {
        standing.push_back(_id);
      }
    });
  for (const ObjectId id : standing)
  {
    const Structure* structure = _sim.Objects().FindStructure(id);
    if (structure != nullptr)
    {
      MarkFootprint(_sim, FootprintOf(*structure, &_sim.Content()), OBSTRUCTION_STRUCTURE);
    }
  }
}

std::int32_t ProgressHundredths(std::int32_t _effortHundredths, std::uint32_t _buildTimeTicks) noexcept
{
  const std::int32_t required = RequiredEffortHundredths(_buildTimeTicks);
  if (required <= 0)
  {
    return 10000; // A row that states no build time is finished the moment it is begun.
  }
  return static_cast<std::int32_t>(std::clamp<std::int64_t>(static_cast<std::int64_t>(_effortHundredths) * 10000 / required, 0, 10000));
}

std::int64_t AttendingBuildPower(const World& _world, std::span<const Seat> _seats, const ContentTree& _content, std::uint8_t _seat,
                                 const Footprint& _footprint)
{
  std::int64_t power = 0;
  _world.ForEachDevice(
    [&power, &_seats, &_content, _seat, &_footprint](ObjectId, const Device& _device)
    {
      if (_device.seat != _seat)
      {
        return;
      }
      const BuilderReach reach = BuilderOf(_device, _seats, _content);
      if (reach.powerHundredthsPerTick <= 0)
      {
        return;
      }
      if (DistanceSquaredTo(_footprint, _device.x, _device.z) <= reach.rangeSubunits * reach.rangeSubunits)
      {
        power += reach.powerHundredthsPerTick;
      }
    });
  return power;
}

std::int32_t ModuleTimeReductionPercent(const Structure& _structure, const ContentTree& _content, StructureModuleEffect _effect)
{
  std::int32_t percent = 0;
  for (std::uint8_t index = 0; index < _structure.moduleCount && index < MAX_STRUCTURE_MODULES; ++index)
  {
    const std::uint32_t row = _structure.modules[index];
    if (row < _content.structures.modules.size() && _content.structures.modules[row].effect == _effect)
    {
      percent += _content.structures.modules[row].amount;
    }
  }
  return std::clamp(percent, 0, 99);
}

std::uint32_t ShortenedTicks(std::uint32_t _ticks, std::int32_t _reductionPercent) noexcept
{
  const std::int64_t shortened = static_cast<std::int64_t>(_ticks) * (100 - std::clamp(_reductionPercent, 0, 99)) / 100;
  return static_cast<std::uint32_t>(std::max<std::int64_t>(shortened, 1));
}

std::uint32_t PlanCount(const World& _world, std::uint8_t _seat)
{
  std::uint32_t plans = 0;
  _world.ForEachStructure(
    [&plans, _seat](ObjectId, const Structure& _structure)
    {
      if (_structure.seat == _seat && _structure.state == StructureState::Plan)
      {
        ++plans;
      }
    });
  return plans;
}

bool PlaceStructurePlan(Sim& _sim, std::uint8_t _seat, std::uint32_t _row, std::uint32_t _cellX, std::uint32_t _cellY)
{
  if (PlanCount(_sim.Objects(), _seat) >= MAX_PLANS_PER_SEAT)
  {
    return false;
  }
  Structure structure{};
  structure.seat = _seat;
  structure.design = _row;
  structure.cellX = _cellX;
  structure.cellY = _cellY;
  structure.state = StructureState::Plan;
  structure.hitPoints = 0; // A plan is not there yet: nothing can shoot it and nothing repairs it.
  structure.working = NO_OBJECT;
  structure.moduleUnderConstruction = NO_STRUCTURE_MODULE;
  // The height it would stand at, so that the client draws the ghost on the ground rather than at
  // zero. The flatten has not happened, so this is the ground as it is.
  const Footprint footprint = FootprintOf(structure, &_sim.Content());
  structure.y = FootprintMeanHeightWorldUnits(_sim.Terrain(), footprint) * Neuron::SUBUNITS_PER_WORLD_UNIT;
  (void)_sim.Objects().Create(structure);
  return true;
}

bool CancelStructure(Sim& _sim, std::uint8_t _seat, ObjectId _structure)
{
  Structure* structure = _sim.Objects().FindStructure(_structure);
  if (structure == nullptr || structure->seat != _seat)
  {
    return false;
  }
  if (structure->state != StructureState::Plan && structure->state != StructureState::UnderConstruction)
  {
    return false; // A standing structure is demolished, not cancelled.
  }
  const StructureDesc* row = RowOf(_sim.Content(), *structure);
  if (structure->state == StructureState::UnderConstruction)
  {
    if (row != nullptr)
    {
      Economy::RefundCanceled(_sim.SeatAt(_seat), row->costHundredths,
                              ProgressHundredths(structure->buildEffortHundredths, row->buildTimeTicks));
    }
    MarkFootprint(_sim, FootprintOf(*structure, &_sim.Content()), 0);
  }
  return _sim.Objects().Remove(_structure);
}

bool DemolishStructure(Sim& _sim, std::uint8_t _seat, ObjectId _structure)
{
  Structure* structure = _sim.Objects().FindStructure(_structure);
  if (structure == nullptr || structure->seat != _seat || structure->state != StructureState::Standing)
  {
    return false;
  }
  if (const StructureDesc* row = RowOf(_sim.Content(), *structure); row != nullptr)
  {
    Economy::RefundDemolished(_sim.SeatAt(_seat), row->costHundredths);
  }
  MarkFootprint(_sim, FootprintOf(*structure, &_sim.Content()), 0);
  return _sim.Objects().Remove(_structure);
}

bool BeginModule(Sim& _sim, std::uint8_t _seat, ObjectId _structure, std::uint32_t _module)
{
  Structure* structure = _sim.Objects().FindStructure(_structure);
  if (structure == nullptr || structure->seat != _seat || structure->state != StructureState::Standing)
  {
    return false;
  }
  if (structure->moduleUnderConstruction != NO_STRUCTURE_MODULE || structure->moduleCount >= MAX_STRUCTURE_MODULES)
  {
    return false;
  }
  const StructureDesc* row = RowOf(_sim.Content(), *structure);
  if (row == nullptr || structure->moduleCount >= row->moduleSlots || _module >= _sim.Content().structures.modules.size())
  {
    return false;
  }
  // The row lists the modules it takes, by id, so a lab module cannot be built onto a factory.
  const std::string& id = _sim.Content().structures.modules[_module].id;
  if (std::find(row->modules.begin(), row->modules.end(), id) == row->modules.end())
  {
    return false;
  }
  structure->moduleUnderConstruction = _module;
  structure->moduleEffortHundredths = 0;
  return true;
}

void DestroyStructure(Sim& _sim, ObjectId _structure)
{
  Structure* structure = _sim.Objects().FindStructure(_structure);
  if (structure == nullptr)
  {
    return;
  }
  const Footprint footprint = FootprintOf(*structure, &_sim.Content());
  if (Occupies(structure->state))
  {
    MarkFootprint(_sim, footprint, 0);
    Wreck wreck{};
    wreck.seat = structure->seat;
    wreck.origin = _structure;
    wreck.design = structure->design;
    // The middle of the footprint, in subunits: a wreck is drawn at a point and a structure holds
    // cells, so this is the one place the two meet.
    wreck.x = static_cast<std::int32_t>(footprint.cellX * Neuron::SUBUNITS_PER_CELL + footprint.cellsX * Neuron::SUBUNITS_PER_CELL / 2);
    wreck.z = static_cast<std::int32_t>(footprint.cellY * Neuron::SUBUNITS_PER_CELL + footprint.cellsY * Neuron::SUBUNITS_PER_CELL / 2);
    wreck.y = structure->y;
    wreck.decayTicks = WRECK_DECAY_TICKS;
    (void)_sim.Objects().Create(wreck);
  }
  // A destroyed structure refunds nothing (GameDesign.md §4); only a demolition does.
  (void)_sim.Objects().Remove(_structure);
}

void AdvanceWrecks(World& _world)
{
  // The ids first: removing while the map is being walked is the one thing a slot map will not
  // forgive, and a wreck's whole tick is one subtraction.
  std::vector<ObjectId> living;
  _world.ForEachWreck([&living](ObjectId _id, const Wreck&) { living.push_back(_id); });
  for (const ObjectId id : living)
  {
    Wreck* wreck = _world.FindWreck(id);
    if (wreck == nullptr)
    {
      continue;
    }
    if (wreck->decayTicks > 0)
    {
      --wreck->decayTicks;
    }
    if (wreck->decayTicks == 0)
    {
      (void)_world.Remove(id);
    }
  }
}

void AdvanceConstruction(Sim& _sim)
{
  const ContentTree& content = _sim.Content();
  World& world = _sim.Objects();

  // Collected first, because beginning one flattens the terrain and that reaches the landscape,
  // the fog and the cluster graph - none of which may move while the world is being walked.
  std::vector<ObjectId> sites;
  world.ForEachStructure(
    [&sites](ObjectId _id, const Structure& _structure)
    {
      if (_structure.state == StructureState::Plan || _structure.state == StructureState::UnderConstruction ||
          (_structure.state == StructureState::Standing && _structure.moduleUnderConstruction != NO_STRUCTURE_MODULE))
      {
        sites.push_back(_id);
      }
    });

  for (const ObjectId id : sites)
  {
    Structure* structure = world.FindStructure(id);
    if (structure == nullptr)
    {
      continue;
    }
    const StructureDesc* row = RowOf(content, *structure);
    if (row == nullptr)
    {
      continue;
    }
    const std::uint8_t seatIndex = structure->seat;
    const Footprint footprint = FootprintOf(*structure, &content);
    const std::int64_t power = AttendingBuildPower(world, _sim.Seats(), content, seatIndex, footprint);
    if (power <= 0)
    {
      continue; // Nobody is there; a site waits rather than decaying.
    }

    if (structure->state == StructureState::Plan)
    {
      // The ground may have been built over since the plan was placed, so the rule is asked again
      // rather than trusted; a plan that can no longer be built stays a plan until it is cancelled.
      const PlacementQuery query{&_sim.Terrain(), &world, &_sim.Seats()[seatIndex], row, &content, &_sim.Power().Deposits()};
      if (CheckPlacement(footprint, query) != PlacementFault::Accepted)
      {
        continue;
      }
      // The cost is drawn when construction BEGINS and not when the plan was placed
      // (GameDesign.md §4). A commander who spent the power meanwhile keeps the plan and not the
      // structure.
      // The one terrain modification the game makes (GameDesign.md §5). Worked out before the
      // power is drawn, so that a delta the landscape would refuse cannot cost a commander the
      // cost of a structure they do not get.
      const HeightDelta delta = FlattenDelta(_sim.Terrain(), footprint);
      if (delta.heights.empty())
      {
        continue;
      }
      if (!Economy::Draw(_sim.SeatAt(seatIndex), row->costHundredths))
      {
        continue;
      }
      const std::int32_t mean = FootprintMeanHeightWorldUnits(_sim.Terrain(), footprint);
      (void)_sim.FlattenTerrain(delta);
      structure = world.FindStructure(id);
      if (structure == nullptr)
      {
        continue;
      }
      structure->y = mean * Neuron::SUBUNITS_PER_WORLD_UNIT;
      structure->state = StructureState::UnderConstruction;
      structure->buildEffortHundredths = 0;
      structure->hitPoints = 1; // Proportional to progress, and never zero while it stands
      MarkFootprint(_sim, footprint, OBSTRUCTION_STRUCTURE);
      continue; // The tick that begins it puts in no effort; the next one does.
    }

    if (structure->state == StructureState::UnderConstruction)
    {
      structure->buildEffortHundredths = static_cast<std::int32_t>(std::min<std::int64_t>(
        static_cast<std::int64_t>(structure->buildEffortHundredths) + power, RequiredEffortHundredths(row->buildTimeTicks)));
      const std::int32_t progress = ProgressHundredths(structure->buildEffortHundredths, row->buildTimeTicks);
      if (progress >= 10000)
      {
        structure->state = StructureState::Standing;
        structure->hitPoints = row->hitPoints;
      }
      else
      {
        structure->hitPoints =
          std::max<std::int32_t>(1, static_cast<std::int32_t>(static_cast<std::int64_t>(row->hitPoints) * progress / 10000));
      }
      continue;
    }

    // A module onto a standing structure. Its cost is drawn the first tick a builder attends it,
    // for the same reason a structure's is.
    const std::uint32_t moduleRow = structure->moduleUnderConstruction;
    if (moduleRow >= content.structures.modules.size())
    {
      structure->moduleUnderConstruction = NO_STRUCTURE_MODULE;
      continue;
    }
    const StructureModuleDesc& module = content.structures.modules[moduleRow];
    if (structure->moduleEffortHundredths == 0 && !Economy::Draw(_sim.SeatAt(seatIndex), module.costHundredths))
    {
      continue;
    }
    structure->moduleEffortHundredths = static_cast<std::int32_t>(std::min<std::int64_t>(
      static_cast<std::int64_t>(structure->moduleEffortHundredths) + power, RequiredEffortHundredths(module.buildTimeTicks)));
    if (ProgressHundredths(structure->moduleEffortHundredths, module.buildTimeTicks) >= 10000)
    {
      structure->modules[structure->moduleCount] = moduleRow;
      ++structure->moduleCount;
      structure->moduleUnderConstruction = NO_STRUCTURE_MODULE;
      structure->moduleEffortHundredths = 0;
    }
  }
}

} // namespace Frontier
