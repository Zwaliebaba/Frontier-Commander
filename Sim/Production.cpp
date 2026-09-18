#include "pch.h"

#include "Production.h"

#include "Construction.h"
#include "Economy.h"
#include "Sim.h"

#include <algorithm>
#include <string>
#include <vector>

namespace Frontier
{

namespace
{

[[nodiscard]] const StructureDesc* RowOf(const ContentTree& _content, const Structure& _structure) noexcept
{
  return _structure.design < _content.structures.structures.size() ? &_content.structures.structures[_structure.design] : nullptr;
}

/// The ticks this factory takes over a design of that cost: the design's own build time, less what
/// its ShortenBuildTime modules take off (GameDesign.md §5, §6).
[[nodiscard]] std::uint32_t FactoryTicksFor(const Structure& _factory, const ContentTree& _content, std::int32_t _costHundredths)
{
  const std::int32_t reduction = ModuleTimeReductionPercent(_factory, _content, StructureModuleEffect::ShortenBuildTime);
  return ShortenedTicks(BuildTimeTicksFor(_costHundredths), reduction);
}

/// The entry at the front of a factory's queue, or nullptr. The seat's list is in the order the
/// commander asked, so the front of one factory's queue is the first entry naming it.
[[nodiscard]] ProductionEntry* FrontOf(Seat& _seat, ObjectId _factory) noexcept
{
  for (ProductionEntry& entry : _seat.production)
  {
    if (entry.factory == _factory)
    {
      return &entry;
    }
  }
  return nullptr;
}

} // namespace

DesignRecipe RecipeFor(const ContentTree& _content, const DeviceDesign& _design)
{
  DesignRecipe recipe;
  if (_design.chassis < _content.components.chassis.size())
  {
    recipe.chassis = _content.components.chassis[_design.chassis].id;
  }
  if (_design.drive < _content.components.drives.size())
  {
    recipe.drive = _content.components.drives[_design.drive].id;
  }
  for (std::uint8_t index = 0; index < _design.moduleCount && index < MAX_MOUNTS; ++index)
  {
    const std::uint32_t row = _design.modules[index];
    recipe.modules.push_back(row < _content.components.modules.size() ? _content.components.modules[row].id : std::string{});
  }
  return recipe;
}

DeviceDesign DesignFromOrder(std::int32_t _chassis, std::int32_t _drive, std::int32_t _packedModules) noexcept
{
  DeviceDesign design{};
  design.chassis = static_cast<std::uint32_t>(_chassis);
  design.drive = static_cast<std::uint32_t>(_drive);
  const auto packed = static_cast<std::uint32_t>(_packedModules);
  for (std::uint8_t slot = 0; slot < 4; ++slot)
  {
    const auto row = static_cast<std::uint8_t>((packed >> (8u * slot)) & 0xFFu);
    if (row == NO_PACKED_MODULE)
    {
      break;
    }
    design.modules[design.moduleCount] = row;
    ++design.moduleCount;
  }
  return design;
}

bool UnlockedFor(const Seat& _seat, const ContentTree& _content, std::string_view _unlockedBy)
{
  if (_unlockedBy.empty())
  {
    return true;
  }
  for (std::size_t index = 0; index < _content.research.size(); ++index)
  {
    if (_content.research[index].id == _unlockedBy)
    {
      return std::find(_seat.researchComplete.begin(), _seat.researchComplete.end(), static_cast<std::uint32_t>(index)) !=
             _seat.researchComplete.end();
    }
  }
  return false;
}

DesignFault CheckDesign(const Seat& _seat, const ContentTree& _content, const DeviceDesign& _design)
{
  if (_design.chassis >= _content.components.chassis.size())
  {
    return DesignFault::UnknownChassis;
  }
  if (_design.drive >= _content.components.drives.size())
  {
    return DesignFault::UnknownDrive;
  }
  // "modules the seat has researched" - and the chassis and the drive too, because a commander who
  // may not build the part may not build the thing made of it.
  if (!UnlockedFor(_seat, _content, _content.components.chassis[_design.chassis].unlockedBy))
  {
    return DesignFault::UnknownChassis;
  }
  if (!UnlockedFor(_seat, _content, _content.components.drives[_design.drive].unlockedBy))
  {
    return DesignFault::UnknownDrive;
  }
  for (std::uint8_t index = 0; index < _design.moduleCount && index < MAX_MOUNTS; ++index)
  {
    const std::uint32_t row = _design.modules[index];
    if (row >= _content.components.modules.size() || !UnlockedFor(_seat, _content, _content.components.modules[row].unlockedBy))
    {
      return DesignFault::UnknownModule;
    }
  }
  // The rest of the rules - a module the chassis refuses, too many for its mounts, none at all -
  // are the derivation's, so that the design screen and the simulation refuse the same designs.
  DesignStats stats{};
  return DeriveDesignStats(_content, RecipeFor(_content, _design), _seat.upgrades, stats);
}

DesignFault DeriveSeatDesign(const Seat& _seat, const ContentTree& _content, std::uint32_t _design, DesignStats& _out)
{
  if (_design >= _seat.designs.size())
  {
    return DesignFault::UnknownChassis; // A slot the commander never saved names no chassis at all.
  }
  return DeriveDesignStats(_content, RecipeFor(_content, _seat.designs[_design]), _seat.upgrades, _out);
}

bool ExitCell(const Landscape& _landscape, const Footprint& _footprint, std::uint32_t& _cellX, std::uint32_t& _cellY)
{
  if (!_landscape.Created())
  {
    return false;
  }
  const std::uint32_t side = _landscape.CellsPerSide();
  // The ring one cell out, walked in one fixed order: along the low edge, up the high side, back
  // along the high edge, down the low side. Two hosts walk it identically, which is the only
  // property that matters beyond it being outside the footprint.
  const std::int64_t low = static_cast<std::int64_t>(_footprint.cellY) - 1;
  const std::int64_t high = static_cast<std::int64_t>(_footprint.cellY) + _footprint.cellsY;
  const std::int64_t left = static_cast<std::int64_t>(_footprint.cellX) - 1;
  const std::int64_t right = static_cast<std::int64_t>(_footprint.cellX) + _footprint.cellsX;
  std::vector<std::pair<std::int64_t, std::int64_t>> ring;
  ring.reserve(static_cast<std::size_t>(2 * (_footprint.cellsX + _footprint.cellsY) + 4));
  for (std::int64_t x = left; x <= right; ++x)
  {
    ring.emplace_back(x, low);
  }
  for (std::int64_t y = low + 1; y <= high; ++y)
  {
    ring.emplace_back(right, y);
  }
  for (std::int64_t x = right - 1; x >= left; --x)
  {
    ring.emplace_back(x, high);
  }
  for (std::int64_t y = high - 1; y > low; --y)
  {
    ring.emplace_back(left, y);
  }
  for (const auto& [x, y] : ring)
  {
    if (x < 0 || y < 0 || static_cast<std::uint32_t>(x) >= side || static_cast<std::uint32_t>(y) >= side)
    {
      continue;
    }
    const Landscape::Cell& cell = _landscape.CellAt(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    if ((cell.flags & Landscape::CELL_WATER) != 0 || cell.obstruction != 0)
    {
      continue;
    }
    _cellX = static_cast<std::uint32_t>(x);
    _cellY = static_cast<std::uint32_t>(y);
    return true;
  }
  return false;
}

bool SaveDesign(Sim& _sim, std::uint8_t _seat, std::uint32_t _slot, const DeviceDesign& _design)
{
  if (_slot >= MAX_SAVED_DESIGNS)
  {
    return false;
  }
  Seat& seat = _sim.SeatAt(_seat);
  if (CheckDesign(seat, _sim.Content(), _design) != DesignFault::None)
  {
    return false;
  }
  if (_slot < seat.designs.size())
  {
    seat.designs[_slot] = _design;
    return true;
  }
  // The next slot extends the list; a gap beyond it would leave slots nothing ever filled, and a
  // design index is what an order names.
  if (_slot != seat.designs.size())
  {
    return false;
  }
  seat.designs.push_back(_design);
  return true;
}

bool SetProduction(Sim& _sim, std::uint8_t _seat, ObjectId _factory, std::uint32_t _design, std::uint32_t _repeat)
{
  Seat& seat = _sim.SeatAt(_seat);
  const Structure* factory = _sim.Objects().FindStructure(_factory);
  if (factory == nullptr || factory->seat != _seat || factory->state != StructureState::Standing)
  {
    return false;
  }
  const StructureDesc* row = RowOf(_sim.Content(), *factory);
  if (row == nullptr || row->role != StructureRole::Factory)
  {
    return false;
  }
  if (_design >= seat.designs.size() || _repeat == 0 || _repeat > MAX_PRODUCTION_REPEAT || seat.production.size() >= MAX_PRODUCTION_ENTRIES)
  {
    return false;
  }
  if (CheckDesign(seat, _sim.Content(), seat.designs[_design]) != DesignFault::None)
  {
    return false;
  }
  seat.production.push_back({_factory, _design, _repeat});
  return true;
}

bool CancelProduction(Sim& _sim, std::uint8_t _seat, ObjectId _factory, std::uint32_t _slot)
{
  Seat& seat = _sim.SeatAt(_seat);
  std::uint32_t seen = 0;
  for (std::size_t index = 0; index < seat.production.size(); ++index)
  {
    if (seat.production[index].factory != _factory)
    {
      continue;
    }
    if (seen != _slot)
    {
      ++seen;
      continue;
    }
    // Only the front of a factory's queue has been paid for, so only it has a refund; the rest
    // cost nothing and give nothing back.
    Structure* factory = _sim.Objects().FindStructure(_factory);
    if (seen == 0 && factory != nullptr && factory->workRemainingTicks > 0)
    {
      DesignStats stats{};
      if (DeriveSeatDesign(seat, _sim.Content(), seat.production[index].design, stats) == DesignFault::None)
      {
        const std::uint32_t total = FactoryTicksFor(*factory, _sim.Content(), stats.costHundredths);
        const std::int32_t progress =
          total == 0 ? 10000 : static_cast<std::int32_t>(10000 - static_cast<std::int64_t>(factory->workRemainingTicks) * 10000 / total);
        Economy::RefundCanceled(seat, stats.costHundredths, progress);
      }
      factory->workRemainingTicks = 0;
    }
    seat.production.erase(seat.production.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
  }
  return false;
}

void AdvanceProduction(Sim& _sim)
{
  const ContentTree& content = _sim.Content();
  World& world = _sim.Objects();

  // The standing factories, collected first: spawning a device writes to the world, and a device
  // spawned this tick must not be one this tick's walk finds.
  std::vector<ObjectId> factories;
  world.ForEachStructure(
    [&factories, &content](ObjectId _id, const Structure& _structure)
    {
      const StructureDesc* row = RowOf(content, _structure);
      if (_structure.state == StructureState::Standing && row != nullptr && row->role == StructureRole::Factory)
      {
        factories.push_back(_id);
      }
    });

  for (const ObjectId id : factories)
  {
    Structure* factory = world.FindStructure(id);
    if (factory == nullptr || factory->seat >= _sim.Seats().size())
    {
      continue;
    }
    Seat& seat = _sim.SeatAt(factory->seat);
    ProductionEntry* entry = FrontOf(seat, id);
    if (entry == nullptr)
    {
      factory->workRemainingTicks = 0;
      continue;
    }
    // "A factory whose seat is at the device cap pauses" (GameDesign.md §4's caps): it neither
    // starts the next one nor advances the one it holds, and it keeps what it has paid for.
    if (seat.deviceCount >= seat.deviceCap)
    {
      continue;
    }
    DesignStats stats{};
    if (DeriveSeatDesign(seat, content, entry->design, stats) != DesignFault::None)
    {
      // A design that stopped being buildable - a content reload, a slot overwritten - is dropped
      // rather than built wrong.
      seat.production.erase(seat.production.begin() + (entry - seat.production.data()));
      factory->workRemainingTicks = 0;
      continue;
    }

    if (factory->workRemainingTicks == 0)
    {
      if (!Economy::Draw(seat, stats.costHundredths))
      {
        continue; // Waits at the front of the queue until the commander can pay for it.
      }
      factory->workRemainingTicks = std::max(FactoryTicksFor(*factory, content, stats.costHundredths), 1u);
    }
    --factory->workRemainingTicks;
    if (factory->workRemainingTicks > 0)
    {
      continue;
    }

    // Done: it comes out beside the factory, facing the way it left, at rank nothing.
    const Footprint footprint = FootprintOf(*factory, &content);
    std::uint32_t cellX = footprint.cellX;
    std::uint32_t cellY = footprint.cellY;
    (void)ExitCell(_sim.Terrain(), footprint, cellX, cellY);
    Device device{};
    device.seat = factory->seat;
    device.design = entry->design;
    device.x = static_cast<std::int32_t>(cellX) * Neuron::SUBUNITS_PER_CELL + Neuron::SUBUNITS_PER_CELL / 2;
    device.z = static_cast<std::int32_t>(cellY) * Neuron::SUBUNITS_PER_CELL + Neuron::SUBUNITS_PER_CELL / 2;
    device.y = static_cast<std::int32_t>(Visibility::CellHeight(_sim.Terrain(), cellX, cellY)) * Neuron::SUBUNITS_PER_WORLD_UNIT;
    device.hitPoints = stats.hitPoints;
    device.primaryOrder = PrimaryOrder::Stop;
    device.target = NO_OBJECT;
    device.destinationX = device.x;
    device.destinationZ = device.z;
    // The stances a commander has not changed: the first value of each axis (Sim/Device.h), which
    // is fire at will, optimal range, retreat at half and pursue.
    device.fire = FireStance::FireAtWill;
    device.range = RangeStance::Optimal;
    device.retreat = RetreatStance::AtHalf;
    device.movement = MovementStance::Pursue;
    (void)world.Create(device);
    // Counted here as well as by stage 2's walk, so that two factories finishing in one tick cannot
    // together pass a cap that the next tick would have caught.
    ++seat.deviceCount;

    --entry->remaining;
    if (entry->remaining == 0)
    {
      seat.production.erase(seat.production.begin() + (entry - seat.production.data()));
    }
  }
}

} // namespace Frontier
