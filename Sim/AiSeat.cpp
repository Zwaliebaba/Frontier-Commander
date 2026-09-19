#include "pch.h"

#include "AiSeat.h"

#include "Construction.h"
#include "Placement.h"
#include "Plan.h"
#include "Sim.h"

#include <algorithm>

namespace Frontier
{

namespace
{

/// The row of the first structure with this role, or NO_ROW.
inline constexpr std::uint32_t NO_ROW = 0xFFFFFFFFu;

[[nodiscard]] std::uint32_t RowWithRole(const Sim& _sim, std::uint8_t _seat, StructureRole _role) noexcept
{
  const ContentTree& content = _sim.Content();
  const Seat& seat = _sim.Seats()[_seat];
  for (std::uint32_t row = 0; row < content.structures.structures.size(); ++row)
  {
    const StructureDesc& desc = content.structures.structures[row];
    if (desc.role == _role && UnlockedFor(seat, content, desc.unlockedBy))
    {
      return row;
    }
  }
  return NO_ROW;
}

void Submit(Sim& _sim, std::uint8_t _seat, OrderKind _kind, std::int32_t _a = 0, std::int32_t _b = 0, std::int32_t _c = 0,
            std::int32_t _d = 0)
{
  Order order{};
  order.tick = _sim.Tick() + AI_ORDER_DELAY_TICKS;
  order.seat = _seat;
  order.kind = _kind;
  order.operands = {_a, _b, _c, _d};
  _sim.Submit(order);
}

/// True when this commander could place this structure here. The same check order validation makes,
/// asked before the order rather than after it: a scripted commander that fired off placements and
/// let them be refused would fill the rejection list every decision, and a rejection list is hashed.
[[nodiscard]] bool CanPlace(const Sim& _sim, std::uint8_t _seat, std::uint32_t _row, std::uint32_t _cellX, std::uint32_t _cellY)
{
  const ContentTree& content = _sim.Content();
  if (_row >= content.structures.structures.size())
  {
    return false;
  }
  const StructureDesc& desc = content.structures.structures[_row];
  const Seat& seat = _sim.Seats()[_seat];
  if (seat.powerHundredths < desc.costHundredths || seat.structureCount >= seat.structureCap)
  {
    return false;
  }
  if (PlanCount(_sim.Objects(), _seat) >= MAX_PLANS_PER_SEAT)
  {
    return false;
  }
  const Footprint footprint = FootprintAt(desc, _cellX, _cellY);
  const PlacementQuery query{&_sim.Terrain(), &_sim.Objects(), &seat, &desc, &content, &_sim.Power().Deposits()};
  return CheckPlacement(footprint, query) == PlacementFault::Accepted;
}

/// A free spot for a structure, spiralling out from a cell. Square rings rather than a real spiral:
/// the order is the same on every machine and that is all a placement needs.
[[nodiscard]] bool SpotNear(const Sim& _sim, std::uint8_t _seat, std::uint32_t _row, std::uint32_t _cellX, std::uint32_t _cellY,
                            std::uint32_t _rings, std::uint32_t& _outX, std::uint32_t& _outY)
{
  for (std::uint32_t ring = 0; ring <= _rings; ++ring)
  {
    for (std::int32_t offsetY = -static_cast<std::int32_t>(ring); offsetY <= static_cast<std::int32_t>(ring); ++offsetY)
    {
      for (std::int32_t offsetX = -static_cast<std::int32_t>(ring); offsetX <= static_cast<std::int32_t>(ring); ++offsetX)
      {
        const bool onTheRing =
          static_cast<std::uint32_t>(std::abs(offsetX)) == ring || static_cast<std::uint32_t>(std::abs(offsetY)) == ring;
        if (!onTheRing)
        {
          continue;
        }
        const std::int64_t x = static_cast<std::int64_t>(_cellX) + offsetX;
        const std::int64_t y = static_cast<std::int64_t>(_cellY) + offsetY;
        if (x < 0 || y < 0)
        {
          continue;
        }
        const auto cellX = static_cast<std::uint32_t>(x);
        const auto cellY = static_cast<std::uint32_t>(y);
        if (CanPlace(_sim, _seat, _row, cellX, cellY))
        {
          _outX = cellX;
          _outY = cellY;
          return true;
        }
      }
    }
  }
  return false;
}

/// SaveDesign's operand 3 packs up to four module rows, one per byte (Sim/Order.h).
[[nodiscard]] std::int32_t PackModules(const DeviceDesign& _design) noexcept
{
  std::uint32_t packed = 0;
  for (std::uint8_t index = 0; index < 4; ++index)
  {
    const std::uint32_t row = index < _design.moduleCount ? _design.modules[index] : NO_PACKED_MODULE;
    packed |= (row & 0xFFu) << (8 * index);
  }
  return static_cast<std::int32_t>(packed);
}

/// Where this commander's command post is, or his first structure, or nothing.
[[nodiscard]] bool HomeCell(const Sim& _sim, std::uint8_t _seat, std::uint32_t& _outX, std::uint32_t& _outY)
{
  bool found = false;
  _sim.Objects().ForEachStructure(
    [&](ObjectId, const Structure& _structure)
    {
      if (found || _structure.seat != _seat)
      {
        return;
      }
      _outX = _structure.cellX;
      _outY = _structure.cellY;
      found = true;
    });
  if (found)
  {
    return true;
  }
  _sim.Objects().ForEachDevice(
    [&](ObjectId, const Device& _device)
    {
      if (found || _device.seat != _seat)
      {
        return;
      }
      _outX = static_cast<std::uint32_t>(std::max<std::int32_t>(_device.x, 0) / Neuron::SUBUNITS_PER_CELL);
      _outY = static_cast<std::uint32_t>(std::max<std::int32_t>(_device.z, 0) / Neuron::SUBUNITS_PER_CELL);
      found = true;
    });
  return found;
}

// ── The behaviours, in the order the header lists them ──────────────────────────────────────

[[nodiscard]] bool Design(Sim& _sim, std::uint8_t _seat, const AiBlackboard& _blackboard)
{
  const Seat& seat = _sim.Seats()[_seat];
  // Index 0 is the builder design and index 1 the fighter, always, so that the produce behaviour
  // names a slot rather than searching for one.
  for (std::uint32_t index = 0; index < 2; ++index)
  {
    if (index < seat.designs.size())
    {
      continue;
    }
    DeviceDesign design{};
    if (!BestDesign(_sim, _seat, index == 0 ? AiRole::Builder : AiRole::Fighter, _blackboard, design))
    {
      continue;
    }
    Submit(_sim, _seat, OrderKind::SaveDesign, static_cast<std::int32_t>(index), static_cast<std::int32_t>(design.chassis),
           static_cast<std::int32_t>(design.drive), PackModules(design));
    return true;
  }

  // And a fighter design that no longer suits what it is meeting: re-designed against the
  // composition it has actually seen, which is what the damage matrix is for.
  if (seat.designs.size() >= 2 && _blackboard.enemyDevicesSeen > 0)
  {
    DeviceDesign wanted{};
    if (BestDesign(_sim, _seat, AiRole::Fighter, _blackboard, wanted) && !(wanted == seat.designs[1]))
    {
      Submit(_sim, _seat, OrderKind::SaveDesign, 1, static_cast<std::int32_t>(wanted.chassis), static_cast<std::int32_t>(wanted.drive),
             PackModules(wanted));
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool Build(Sim& _sim, std::uint8_t _seat, const AiBlackboard& _blackboard, StructureRole _role, std::uint32_t _wanted,
                         std::uint32_t _nearX, std::uint32_t _nearY, std::uint32_t _rings)
{
  const std::uint32_t row = RowWithRole(_sim, _seat, _role);
  if (row == NO_ROW)
  {
    return false;
  }
  AiNeed need{};
  switch (_role)
  {
  case StructureRole::CommandPost:
    need = AiNeed::CommandPost;
    break;
  case StructureRole::Extractor:
    need = AiNeed::Extractor;
    break;
  case StructureRole::Generator:
    need = AiNeed::Generator;
    break;
  case StructureRole::Factory:
    need = AiNeed::Factory;
    break;
  default:
    need = AiNeed::Lab;
    break;
  }
  if (_blackboard.standing[static_cast<std::size_t>(need)] >= _wanted)
  {
    return false;
  }
  std::uint32_t cellX = 0;
  std::uint32_t cellY = 0;
  if (!SpotNear(_sim, _seat, row, _nearX, _nearY, _rings, cellX, cellY))
  {
    return false;
  }
  Submit(_sim, _seat, OrderKind::PlaceStructure, static_cast<std::int32_t>(row), static_cast<std::int32_t>(cellX),
         static_cast<std::int32_t>(cellY));
  return true;
}

[[nodiscard]] bool Scout(Sim& _sim, std::uint8_t _seat, const AiBlackboard& _blackboard, const CellPosition& _deposit);

/// How far this device's builder modules reach, in subunits; 0 when it carries none.
[[nodiscard]] std::int64_t BuilderReachSubunits(const Sim& _sim, std::uint8_t _seat, const Device& _device) noexcept
{
  const Seat& seat = _sim.Seats()[_seat];
  if (_device.design >= seat.designs.size())
  {
    return 0;
  }
  const ContentTree& content = _sim.Content();
  const DeviceDesign& design = seat.designs[_device.design];
  std::int64_t reach = 0;
  for (std::uint8_t index = 0; index < design.moduleCount && index < MAX_MOUNTS; ++index)
  {
    const std::uint32_t row = design.modules[index];
    if (row < content.components.modules.size() && content.components.modules[row].systemKind == SystemKind::Builder)
    {
      reach = std::max<std::int64_t>(reach, content.components.modules[row].systemRangeSubunits);
    }
  }
  return reach;
}

/// Walks an idle builder to the oldest thing this commander has placed and not finished. A plan
/// nobody is standing next to is a plan that never goes up, and a commander with one truck and six
/// plans finishes none of them - which is what the first run of this AI actually did.
[[nodiscard]] bool Finish(Sim& _sim, std::uint8_t _seat, const AiBlackboard& _blackboard)
{
  if (_blackboard.unfinished.empty())
  {
    return false;
  }
  const CellPosition site = _blackboard.unfinishedPlaces.front();
  for (const ObjectId& builder : _blackboard.builderDevices)
  {
    const Device* device = _sim.Objects().FindDevice(builder);
    if (device == nullptr || device->primaryOrder != PrimaryOrder::Stop)
    {
      continue;
    }
    // Already in reach: construction is stage 5's and needs no order, so there is nothing for this
    // behaviour to do and the list moves on. The threshold is the builder's OWN reach halved rather
    // than a round number of cells - the first version used four cells against a module that
    // reaches two, so the truck stood beside the plan deciding it was close enough while the site
    // sat at zero effort for four thousand ticks. Halved, because the reach is to the nearest part
    // of a footprint and this measures to its corner.
    const std::int32_t x = static_cast<std::int32_t>(site.x * Neuron::SUBUNITS_PER_CELL + Neuron::SUBUNITS_PER_CELL / 2);
    const std::int32_t z = static_cast<std::int32_t>(site.y * Neuron::SUBUNITS_PER_CELL + Neuron::SUBUNITS_PER_CELL / 2);
    const std::int64_t dx = static_cast<std::int64_t>(device->x) - x;
    const std::int64_t dz = static_cast<std::int64_t>(device->z) - z;
    const std::int64_t reach = BuilderReachSubunits(_sim, _seat, *device) / 2;
    if (reach > 0 && dx * dx + dz * dz <= reach * reach)
    {
      return false;
    }
    Submit(_sim, _seat, OrderKind::Move, static_cast<std::int32_t>(builder.value), x, z);
    return true;
  }
  return false;
}

/// Claiming deposits, which is two behaviours that have to be one: a deposit must be STOOD ON by
/// its extractor, so a spot cannot be found near it - the list of deposits is the list of spots.
/// The commander therefore walks it in order and takes the first it can build on, walking a builder
/// to the first it has not SEEN yet when it reaches one, because CheckPlacement refuses an
/// unexplored footprint (GameDesign.md §5's "anywhere the commander has explored"). A deposit it
/// has seen and cannot build on - a summit too steep for a footprint - is skipped for good.
[[nodiscard]] bool Expand(Sim& _sim, std::uint8_t _seat, const AiBlackboard& _blackboard)
{
  if (_blackboard.standing[static_cast<std::size_t>(AiNeed::Extractor)] >= AI_WANTED_EXTRACTORS)
  {
    return false;
  }
  const std::uint32_t row = RowWithRole(_sim, _seat, StructureRole::Extractor);
  if (row == NO_ROW)
  {
    return false;
  }
  const Seat& seat = _sim.Seats()[_seat];
  for (const CellPosition& deposit : _blackboard.freeDeposits)
  {
    if (CanPlace(_sim, _seat, row, deposit.x, deposit.y))
    {
      Submit(_sim, _seat, OrderKind::PlaceStructure, static_cast<std::int32_t>(row), static_cast<std::int32_t>(deposit.x),
             static_cast<std::int32_t>(deposit.y));
      return true;
    }
    if (seat.fog.Inside(deposit.x, deposit.y) && seat.fog.Explored(deposit.x, deposit.y))
    {
      continue; // Seen, and the ground refuses it: there is no walking that will change the answer.
    }
    return Scout(_sim, _seat, _blackboard, deposit);
  }
  return false;
}

/// A deposit a commander has never seen cannot be built on - CheckPlacement refuses an unexplored
/// footprint, which is GameDesign.md §5's "anywhere the commander has explored". So the commander
/// walks to it first. That is a behaviour rather than an oversight in the list: a scripted seat
/// that could place on ground it had not seen would be reading the map through the fog, which is
/// exactly what the placement rule exists to stop.
[[nodiscard]] bool Scout(Sim& _sim, std::uint8_t _seat, const AiBlackboard& _blackboard, const CellPosition& _deposit)
{
  // A builder that is standing still. One that is already walking is on its way somewhere, and
  // re-ordering it every decision is how a scripted commander walks in circles.
  for (const ObjectId& builder : _blackboard.builderDevices)
  {
    const Device* device = _sim.Objects().FindDevice(builder);
    if (device == nullptr || device->primaryOrder != PrimaryOrder::Stop)
    {
      continue;
    }
    Submit(_sim, _seat, OrderKind::Move, static_cast<std::int32_t>(builder.value),
           static_cast<std::int32_t>(_deposit.x * Neuron::SUBUNITS_PER_CELL + Neuron::SUBUNITS_PER_CELL / 2),
           static_cast<std::int32_t>(_deposit.y * Neuron::SUBUNITS_PER_CELL + Neuron::SUBUNITS_PER_CELL / 2));
    return true;
  }
  return false; // Every builder is busy; the deposit waits for one.
}

[[nodiscard]] bool Produce(Sim& _sim, std::uint8_t _seat, const AiBlackboard& _blackboard)
{
  if (_blackboard.idleFactories.empty() || _sim.Seats()[_seat].designs.size() < 2)
  {
    return false;
  }
  const Seat& seat = _sim.Seats()[_seat];
  if (seat.deviceCount >= seat.deviceCap)
  {
    return false;
  }
  // Two builders first, then fighters. A commander with no builder cannot rebuild what it loses,
  // which is the one way a scripted seat gets stuck for the rest of a match.
  const std::uint32_t design = _blackboard.builders < AI_WANTED_BUILDERS ? 0u : 1u;
  if (design >= seat.designs.size())
  {
    return false;
  }
  Submit(_sim, _seat, OrderKind::SetProduction, static_cast<std::int32_t>(_blackboard.idleFactories.front().value),
         static_cast<std::int32_t>(design), 1);
  return true;
}

/// Where to send a group when the commander has seen nothing yet: the START POSITION furthest from
/// its own base. The landscape's definition is public to every commander from the first tick
/// (TechnicalDesign.md §5.2 names that as the one leak this model keeps), so a scripted commander
/// knowing where the other seats start is exactly what a human knows - and without it two
/// commanders on opposite corners of a landscape never meet at all, which is what the first run of
/// this AI did for twelve thousand ticks.
/// _target moved _cells towards (_homeX, _homeY), never past it and never off the landscape's
/// lower edge. Integer throughout, so two hosts aim at the same cell.
[[nodiscard]] CellPosition ShortOf(const CellPosition& _target, std::uint32_t _homeX, std::uint32_t _homeY, std::uint32_t _cells) noexcept
{
  const std::int64_t dx = static_cast<std::int64_t>(_homeX) - _target.x;
  const std::int64_t dy = static_cast<std::int64_t>(_homeY) - _target.y;
  const std::int64_t length = Neuron::Length(static_cast<std::int32_t>(dx), static_cast<std::int32_t>(dy));
  if (length <= static_cast<std::int64_t>(_cells))
  {
    return _target;
  }
  const std::int64_t x = static_cast<std::int64_t>(_target.x) + dx * _cells / length;
  const std::int64_t y = static_cast<std::int64_t>(_target.y) + dy * _cells / length;
  return {static_cast<std::uint32_t>(std::max<std::int64_t>(x, 0)), static_cast<std::uint32_t>(std::max<std::int64_t>(y, 0))};
}

[[nodiscard]] bool EnemyStart(const Sim& _sim, std::uint32_t _homeX, std::uint32_t _homeY, CellPosition& _out)
{
  std::int64_t furthest = -1;
  for (const CellPosition& start : _sim.Terrain().Definition().starts)
  {
    const std::int64_t dx = static_cast<std::int64_t>(start.x) - _homeX;
    const std::int64_t dy = static_cast<std::int64_t>(start.y) - _homeY;
    const std::int64_t distance = dx * dx + dy * dy;
    if (distance > furthest)
    {
      furthest = distance;
      _out = start;
    }
  }
  return furthest > 0;
}

[[nodiscard]] bool Attack(Sim& _sim, std::uint8_t _seat, const AiBlackboard& _blackboard, std::uint32_t _homeX, std::uint32_t _homeY)
{
  if (_blackboard.idleFighters.size() < ATTACK_GROUP_SIZE)
  {
    return false;
  }
  CellPosition target{};
  if (!_blackboard.knownEnemyPlaces.empty())
  {
    target = _blackboard.knownEnemyPlaces.front();
  }
  else if (!EnemyStart(_sim, _homeX, _homeY, target))
  {
    return false;
  }
  // A few cells short of it, on the line from home. The cell a commander starts on is where his
  // command post stands, and a structure's own cells are impassable - so a route to the middle of
  // an enemy base is unreachable and the group stops where it was built. Short of it is also where
  // a group meets what is defending the base, which is the point of sending one.
  target = ShortOf(target, _homeX, _homeY, APPROACH_CELLS);
  const auto x = static_cast<std::int32_t>(target.x * Neuron::SUBUNITS_PER_CELL + Neuron::SUBUNITS_PER_CELL / 2);
  const auto z = static_cast<std::int32_t>(target.y * Neuron::SUBUNITS_PER_CELL + Neuron::SUBUNITS_PER_CELL / 2);
  // One order a device: an order names ONE object (Sim/Order.h), so a group of four is four orders,
  // which is what a human's selection is too. A GROUP, and not everything standing still: the
  // planner's budget is 2,000 nodes a tick shared across every request (Sim/PathPlanner.h), so
  // twenty-nine cross-map searches at once each get seventy nodes and none of them finishes.
  std::size_t sent = 0;
  for (const ObjectId& fighter : _blackboard.idleFighters)
  {
    if (sent >= ATTACK_GROUP_SIZE)
    {
      break;
    }
    Submit(_sim, _seat, OrderKind::AttackMove, static_cast<std::int32_t>(fighter.value), x, z);
    ++sent;
  }
  return true;
}

} // namespace

void DecideForSeat(Sim& _sim, std::uint8_t _seat)
{
  AiBlackboard blackboard;
  Observe(_sim, _seat, blackboard);

  std::uint32_t homeX = 0;
  std::uint32_t homeY = 0;
  const bool home = HomeCell(_sim, _seat, homeX, homeY);
  if (!home)
  {
    return; // Nothing left and nowhere to start from; stage 12 is what ends a match like that.
  }

  // The army and the base do not compete for anything: a group of fighters standing idle is not a
  // resource the economy wants, so the attack behaviour is evaluated on every decision rather than
  // waiting its turn behind the build list. Without that it never runs at all - a factory with
  // something to produce answers first, every decision, for the whole match.
  (void)Attack(_sim, _seat, blackboard, homeX, homeY);

  if (Design(_sim, _seat, blackboard))
  {
    return;
  }
  if (Build(_sim, _seat, blackboard, StructureRole::CommandPost, 1, homeX, homeY, 8))
  {
    return;
  }
  if (Finish(_sim, _seat, blackboard))
  {
    return;
  }
  // Nothing new is placed while there are already more unfinished sites than builders to work them.
  // It is the rule that turns a list of plans into a base: without it one truck is spread over six
  // sites and finishes none. The slack of one is deliberate - a site no builder can reach, a
  // deposit on a summit no drive can climb, would otherwise stop the commander placing anything
  // else for the rest of the match, and one spare slot lets the base go up around it.
  const bool roomToBuild = blackboard.unfinished.size() <= std::max<std::size_t>(blackboard.builders, 1);
  // The generator comes BEFORE the next extractor, and that order is the whole of GameDesign.md §4:
  // an extractor earns nothing until a generator reaches it. A commander that claimed four deposits
  // before building one of these would have spent four extractors' worth of power on an income of
  // nothing, which is exactly what the first run of this AI did.
  const std::size_t generators = blackboard.standing[static_cast<std::size_t>(AiNeed::Generator)];
  const bool wantsGenerator =
    blackboard.standing[static_cast<std::size_t>(AiNeed::Extractor)] > 0 && (generators == 0 || blackboard.unservedExtractors > 0);
  if (roomToBuild && wantsGenerator &&
      Build(_sim, _seat, blackboard, StructureRole::Generator, static_cast<std::uint32_t>(generators) + 1, homeX, homeY,
            AI_BASE_SEARCH_RINGS))
  {
    return;
  }
  if (roomToBuild && Expand(_sim, _seat, blackboard))
  {
    return;
  }
  if (roomToBuild && Build(_sim, _seat, blackboard, StructureRole::Factory, 1, homeX, homeY, AI_BASE_SEARCH_RINGS))
  {
    return;
  }
  if (roomToBuild && Build(_sim, _seat, blackboard, StructureRole::ResearchLab, 1, homeX, homeY, AI_BASE_SEARCH_RINGS))
  {
    return;
  }
  (void)Produce(_sim, _seat, blackboard);
}

void AdvanceAiSeats(Sim& _sim)
{
  const std::uint32_t tick = _sim.Tick();
  const std::span<const Seat> seats = _sim.Seats();
  for (std::size_t index = 0; index < seats.size(); ++index)
  {
    if (!seats[index].Scripted() || seats[index].victory != VictoryState::Playing)
    {
      continue;
    }
    // Staggered by seat, so that eight scripted commanders never decide on one tick and the budget
    // of §7 is a tenth of what it would otherwise be.
    if (tick % AI_DECISION_INTERVAL_TICKS != index % AI_DECISION_INTERVAL_TICKS)
    {
      continue;
    }
    DecideForSeat(_sim, static_cast<std::uint8_t>(index));
  }
}

} // namespace Frontier
