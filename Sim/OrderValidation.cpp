#include "pch.h"

#include "OrderValidation.h"

#include "Construction.h"
#include "Design.h"
#include "Placement.h"
#include "Plan.h"
#include "Production.h"
#include "Research.h"

#include "FixedPoint.h"

#include <algorithm>
#include <string>

namespace Frontier
{

namespace
{

[[nodiscard]] OrderCheck Reject(const Order& _order, RejectReason _reason)
{
  return {_order, _reason};
}

[[nodiscard]] OrderCheck Accept(const Order& _order)
{
  return {_order, RejectReason::Accepted};
}

/// The device the order's first operand names, or nullptr when it names none the seat owns. Every
/// device order starts here, so ownership is asked once and in one way.
[[nodiscard]] const Device* OwnedDevice(const Order& _order, const OrderContext& _context)
{
  if (_order.operands[0] <= 0)
  {
    return nullptr;
  }
  const Device* device = _context.world->FindDevice({static_cast<std::uint32_t>(_order.operands[0]), ObjectKind::Device});
  return device != nullptr && device->seat == _order.seat ? device : nullptr;
}

[[nodiscard]] const Structure* OwnedStructure(const Order& _order, const OrderContext& _context)
{
  if (_order.operands[0] <= 0)
  {
    return nullptr;
  }
  const Structure* structure = _context.world->FindStructure({static_cast<std::uint32_t>(_order.operands[0]), ObjectKind::Structure});
  return structure != nullptr && structure->seat == _order.seat ? structure : nullptr;
}

/// A position operand pair is inside the landscape. Orders carry subunits, so the bound is the
/// landscape's extent in subunits and a negative is out by definition.
[[nodiscard]] bool InsideLandscape(const Landscape& _landscape, std::int32_t _x, std::int32_t _z)
{
  if (!_landscape.Created())
  {
    return false;
  }
  const std::int64_t extent = static_cast<std::int64_t>(_landscape.Definition().cellsPerSide) * Neuron::SUBUNITS_PER_CELL;
  return _x >= 0 && _z >= 0 && _x < extent && _z < extent;
}

/// The seat owns the object the order's first operand names, whatever kind it is; what the orders
/// that name a structure need before their own system exists to say more.
[[nodiscard]] OrderCheck OwnershipOnly(const Order& _order, const OrderContext& _context)
{
  return OwnedStructure(_order, _context) == nullptr ? Reject(_order, RejectReason::NotOwned) : Accept(_order);
}

/// Every design fault is one rejection, for the same reason every placement fault is: RejectReason
/// is the vocabulary a client speaks. A part that does not exist is the client's fault and a
/// combination the tables refuse is the commander's, which is the line between the two.
[[nodiscard]] constexpr RejectReason ReasonFor(DesignFault _fault) noexcept
{
  switch (_fault)
  {
  case DesignFault::None:
    return RejectReason::Accepted;
  case DesignFault::UnknownChassis:
  case DesignFault::UnknownDrive:
  case DesignFault::UnknownModule:
    return RejectReason::NotResearched;
  case DesignFault::NoModules:
  case DesignFault::TooManyModules:
  case DesignFault::ModuleRefusesChassis:
    return RejectReason::InvalidTarget;
  }
  return RejectReason::Malformed;
}

/// Every placement fault is one rejection, because RejectReason is the vocabulary a client speaks
/// and widening it changes the wire. Which fault it was belongs in the client's own preview, which
/// asks CheckPlacement directly (Interface.md §7).
[[nodiscard]] constexpr RejectReason ReasonFor(PlacementFault _fault) noexcept
{
  return _fault == PlacementFault::Accepted ? RejectReason::Accepted : RejectReason::InvalidPlacement;
}

/// Whether the seat has a standing command post, which is what GameDesign.md §5 lets a commander
/// build from. Without the tables the role of a row is unknown, so any standing structure counts:
/// the check stays real rather than becoming either a refusal of everything or a permission for it.
[[nodiscard]] bool HasCommandPost(const Order& _order, const OrderContext& _context)
{
  bool found = false;
  _context.world->ForEachStructure(
    [&found, &_order, &_context](ObjectId, const Structure& _structure)
    {
      if (found || _structure.seat != _order.seat || _structure.state != StructureState::Standing)
      {
        return;
      }
      if (_context.content == nullptr)
      {
        found = true;
        return;
      }
      const std::vector<StructureDesc>& rows = _context.content->structures.structures;
      found = _structure.design < rows.size() && rows[_structure.design].role == StructureRole::CommandPost;
    });
  return found;
}

} // namespace

bool CanSee(const Seat& _seat, const Landscape& _landscape, std::int32_t _x, std::int32_t _z)
{
  if (!_landscape.Created() || _seat.fog.Empty() || _x < 0 || _z < 0)
  {
    return false;
  }
  const std::uint32_t cellX = static_cast<std::uint32_t>(_x >> Neuron::SUBUNITS_PER_CELL_SHIFT);
  const std::uint32_t cellY = static_cast<std::uint32_t>(_z >> Neuron::SUBUNITS_PER_CELL_SHIFT);
  return _seat.fog.Visible(cellX, cellY);
}

bool LastKnownPosition(const Seat& _seat, ObjectId _target, std::int32_t& _x, std::int32_t& _z)
{
  const Ghost* ghost = _seat.ghosts.Find(_target);
  if (ghost == nullptr)
  {
    return false;
  }
  // The centre of the cell it stood in, because a ghost records a footprint and an order wants a
  // point; half a cell is the nearest thing to "there" a cell can give.
  _x = static_cast<std::int32_t>(ghost->cellX) * Neuron::SUBUNITS_PER_CELL + Neuron::SUBUNITS_PER_CELL / 2;
  _z = static_cast<std::int32_t>(ghost->cellY) * Neuron::SUBUNITS_PER_CELL + Neuron::SUBUNITS_PER_CELL / 2;
  return true;
}

OrderCheck ValidateOrder(const Order& _order, const OrderContext& _context)
{
  if (_order.seat >= _context.seats.size())
  {
    return Reject(_order, RejectReason::Malformed);
  }
  const Seat& seat = _context.seats[_order.seat];

  switch (_order.kind)
  {
  case OrderKind::Move:
  case OrderKind::AttackMove:
  case OrderKind::Patrol:
  {
    if (OwnedDevice(_order, _context) == nullptr)
    {
      return Reject(_order, RejectReason::NotOwned);
    }
    return InsideLandscape(*_context.landscape, _order.operands[1], _order.operands[2]) ? Accept(_order)
                                                                                        : Reject(_order, RejectReason::InvalidPlacement);
  }

  case OrderKind::Guard:
  {
    if (OwnedDevice(_order, _context) == nullptr)
    {
      return Reject(_order, RejectReason::NotOwned);
    }
    if (_order.operands[3] != 0)
    {
      // Guarding a device: it must be one of the seat's own, or there is nothing to follow.
      if (_order.operands[3] < 0)
      {
        return Reject(_order, RejectReason::Malformed);
      }
      const Device* guarded = _context.world->FindDevice({static_cast<std::uint32_t>(_order.operands[3]), ObjectKind::Device});
      return guarded != nullptr && guarded->seat == _order.seat ? Accept(_order) : Reject(_order, RejectReason::InvalidTarget);
    }
    return InsideLandscape(*_context.landscape, _order.operands[1], _order.operands[2]) ? Accept(_order)
                                                                                        : Reject(_order, RejectReason::InvalidPlacement);
  }

  case OrderKind::Attack:
  {
    if (OwnedDevice(_order, _context) == nullptr)
    {
      return Reject(_order, RejectReason::NotOwned);
    }
    if (_order.operands[1] <= 0 || _order.operands[2] < 0 || _order.operands[2] >= OBJECT_KIND_COUNT)
    {
      return Reject(_order, RejectReason::Malformed);
    }
    const ObjectId target{static_cast<std::uint32_t>(_order.operands[1]), static_cast<ObjectKind>(_order.operands[2])};
    std::int32_t x = 0;
    std::int32_t z = 0;
    bool located = false;
    if (const Device* device = _context.world->FindDevice(target); device != nullptr)
    {
      if (device->seat == _order.seat)
      {
        return Reject(_order, RejectReason::InvalidTarget); // No firing on one's own
      }
      x = device->x;
      z = device->z;
      located = true;
    }
    else if (const Structure* structure = _context.world->FindStructure(target); structure != nullptr)
    {
      if (structure->seat == _order.seat)
      {
        return Reject(_order, RejectReason::InvalidTarget);
      }
      x = static_cast<std::int32_t>(structure->cellX) * Neuron::SUBUNITS_PER_CELL + Neuron::SUBUNITS_PER_CELL / 2;
      z = static_cast<std::int32_t>(structure->cellY) * Neuron::SUBUNITS_PER_CELL + Neuron::SUBUNITS_PER_CELL / 2;
      located = true;
    }
    else if (target.kind != ObjectKind::Device && target.kind != ObjectKind::Structure)
    {
      return Reject(_order, RejectReason::InvalidTarget); // A projectile, a feature or a wreck is not a target
    }
    if (located && CanSee(seat, *_context.landscape, x, z))
    {
      return Accept(_order);
    }
    // GameDesign.md §8: an order to attack an unseen unit becomes an attack-move to its last known
    // position. That is the ghost store's answer, and without one there is nowhere to send it.
    std::int32_t lastX = 0;
    std::int32_t lastZ = 0;
    if (!LastKnownPosition(seat, target, lastX, lastZ))
    {
      return Reject(_order, RejectReason::NotVisible);
    }
    Order rewritten = _order;
    rewritten.kind = OrderKind::AttackMove;
    rewritten.operands[1] = lastX;
    rewritten.operands[2] = lastZ;
    rewritten.operands[3] = 0;
    return Accept(rewritten);
  }

  case OrderKind::Stop:
  case OrderKind::ReturnToRepair:
    return OwnedDevice(_order, _context) == nullptr ? Reject(_order, RejectReason::NotOwned) : Accept(_order);

  case OrderKind::SetStance:
  {
    if (OwnedDevice(_order, _context) == nullptr)
    {
      return Reject(_order, RejectReason::NotOwned);
    }
    if (_order.operands[1] < 0 || _order.operands[1] >= STANCE_AXIS_COUNT)
    {
      return Reject(_order, RejectReason::Malformed);
    }
    const std::uint8_t values = STANCE_VALUE_COUNTS[static_cast<std::size_t>(_order.operands[1])];
    return _order.operands[2] >= 0 && _order.operands[2] < values ? Accept(_order) : Reject(_order, RejectReason::Malformed);
  }

  case OrderKind::Group:
  {
    if (OwnedDevice(_order, _context) == nullptr)
    {
      return Reject(_order, RejectReason::NotOwned);
    }
    return _order.operands[1] >= 0 && _order.operands[1] <= MAX_CONTROL_GROUP ? Accept(_order) : Reject(_order, RejectReason::Malformed);
  }

  case OrderKind::PlaceStructure:
  {
    if (_order.operands[0] < 0)
    {
      return Reject(_order, RejectReason::Malformed);
    }
    // The row the order names, when there are tables to name it in. Without them the price, the
    // size and the role are unknown, and the checks that need one are skipped rather than guessed
    // at.
    const StructureDesc* row = nullptr;
    if (_context.content != nullptr)
    {
      const std::vector<StructureDesc>& rows = _context.content->structures.structures;
      if (static_cast<std::size_t>(_order.operands[0]) >= rows.size())
      {
        return Reject(_order, RejectReason::Malformed);
      }
      row = &rows[static_cast<std::size_t>(_order.operands[0])];
    }
    // A commander with no command post may still build one (GameDesign.md §5), and nothing else.
    if (!HasCommandPost(_order, _context) && (row == nullptr || row->role != StructureRole::CommandPost))
    {
      return Reject(_order, RejectReason::NoCommandPost);
    }
    if (seat.structureCount >= seat.structureCap)
    {
      return Reject(_order, RejectReason::AtCap);
    }
    // Exactly the row's cost now that S3 prices one; "the stockpile is empty" is what is left when
    // there are no tables to price against. The draw itself is S4's, at the moment construction
    // begins (GameDesign.md §4) - this is the check that the commander could pay if it did.
    const std::int32_t cost = row != nullptr ? row->costHundredths : 1;
    if (seat.powerHundredths < cost || seat.powerHundredths <= 0)
    {
      return Reject(_order, RejectReason::CannotAfford);
    }
    if (row != nullptr && !UnlockedFor(seat, *_context.content, row->unlockedBy))
    {
      return Reject(_order, RejectReason::NotResearched);
    }
    // A plan costs nothing, so the plan limit is the only thing that stops a commander papering
    // the landscape with them and reading the fog off the rejections (GameDesign.md §5).
    if (PlanCount(*_context.world, _order.seat) >= MAX_PLANS_PER_SEAT)
    {
      return Reject(_order, RejectReason::AtCap);
    }
    if (!_context.landscape->Created() || _order.operands[1] < 0 || _order.operands[2] < 0)
    {
      return Reject(_order, RejectReason::InvalidPlacement);
    }
    // The whole of GameDesign.md §5's placement rule (Sim/Placement.h), which is also what the
    // construction system asks again the moment a builder reaches the plan.
    const std::uint32_t cellX = static_cast<std::uint32_t>(_order.operands[1]);
    const std::uint32_t cellY = static_cast<std::uint32_t>(_order.operands[2]);
    const Footprint footprint = row != nullptr ? FootprintAt(*row, cellX, cellY) : Footprint{cellX, cellY, 1, 1};
    const PlacementQuery query{_context.landscape, _context.world, &seat, row, _context.content, _context.deposits};
    return {_order, ReasonFor(CheckPlacement(footprint, query))};
  }

  case OrderKind::SetProduction:
  {
    const Structure* structure = OwnedStructure(_order, _context);
    if (structure == nullptr)
    {
      return Reject(_order, RejectReason::NotOwned);
    }
    if (_order.operands[1] < 0 || static_cast<std::size_t>(_order.operands[1]) >= seat.designs.size())
    {
      return Reject(_order, RejectReason::NotResearched);
    }
    if (_order.operands[2] <= 0 || static_cast<std::uint32_t>(_order.operands[2]) > MAX_PRODUCTION_REPEAT)
    {
      return Reject(_order, RejectReason::Malformed);
    }
    if (seat.production.size() >= MAX_PRODUCTION_ENTRIES)
    {
      return Reject(_order, RejectReason::AtCap);
    }
    if (_context.content == nullptr)
    {
      return Accept(_order); // Without the tables the role and the design cannot be judged.
    }
    const std::vector<StructureDesc>& rows = _context.content->structures.structures;
    if (structure->state != StructureState::Standing || structure->design >= rows.size() ||
        rows[structure->design].role != StructureRole::Factory)
    {
      return Reject(_order, RejectReason::InvalidTarget);
    }
    // THE DEVICE CAP IS NOT CHECKED HERE, and it was until this task. GameDesign.md §4's cap is on
    // what stands in the field, and the acceptance says a factory at it PAUSES - so a commander
    // may queue against a cap they are at and the factory waits, which is better play than a
    // refusal and is what the stage does.
    return {_order, ReasonFor(CheckDesign(seat, *_context.content, seat.designs[static_cast<std::size_t>(_order.operands[1])]))};
  }

  case OrderKind::SetResearch:
  {
    const Structure* structure = OwnedStructure(_order, _context);
    if (structure == nullptr)
    {
      return Reject(_order, RejectReason::NotOwned);
    }
    if (_order.operands[1] < 0)
    {
      return Reject(_order, RejectReason::Malformed);
    }
    const auto item = static_cast<std::uint32_t>(_order.operands[1]);
    if (_context.content == nullptr)
    {
      // Without the tables there is no tree; the one thing that can still be said is that an item
      // already complete is not one to start.
      return std::find(seat.researchComplete.begin(), seat.researchComplete.end(), item) != seat.researchComplete.end()
               ? Reject(_order, RejectReason::NotResearched)
               : Accept(_order);
    }
    const std::vector<StructureDesc>& rows = _context.content->structures.structures;
    if (structure->state != StructureState::Standing || structure->design >= rows.size() ||
        rows[structure->design].role != StructureRole::ResearchLab)
    {
      return Reject(_order, RejectReason::InvalidTarget);
    }
    if (item >= _context.content->research.size())
    {
      return Reject(_order, RejectReason::Malformed);
    }
    // One item a lab: a lab already working is not idle, whatever it is working on.
    const bool busy = std::any_of(seat.researchActive.begin(), seat.researchActive.end(), [&_order](const ResearchProgress& _progress)
                                  { return _progress.lab.value == static_cast<std::uint32_t>(_order.operands[0]); });
    if (busy)
    {
      return Reject(_order, RejectReason::InvalidTarget);
    }
    if (!Available(seat, *_context.content, item))
    {
      return Reject(_order, RejectReason::NotResearched);
    }
    return seat.powerHundredths >= _context.content->research[item].costHundredths ? Accept(_order)
                                                                                   : Reject(_order, RejectReason::CannotAfford);
  }

  case OrderKind::CancelStructure:
  {
    const Structure* structure = OwnedStructure(_order, _context);
    if (structure == nullptr)
    {
      return Reject(_order, RejectReason::NotOwned);
    }
    // A standing structure is demolished, not cancelled; the two refund different shares and the
    // commander pressed different buttons.
    return structure->state == StructureState::Plan || structure->state == StructureState::UnderConstruction
             ? Accept(_order)
             : Reject(_order, RejectReason::InvalidTarget);
  }

  case OrderKind::Demolish:
  {
    const Structure* structure = OwnedStructure(_order, _context);
    if (structure == nullptr)
    {
      return Reject(_order, RejectReason::NotOwned);
    }
    return structure->state == StructureState::Standing ? Accept(_order) : Reject(_order, RejectReason::InvalidTarget);
  }

  case OrderKind::BuildModule:
  {
    const Structure* structure = OwnedStructure(_order, _context);
    if (structure == nullptr)
    {
      return Reject(_order, RejectReason::NotOwned);
    }
    if (structure->state != StructureState::Standing || structure->moduleUnderConstruction != NO_STRUCTURE_MODULE)
    {
      return Reject(_order, RejectReason::InvalidTarget);
    }
    if (_order.operands[1] < 0)
    {
      return Reject(_order, RejectReason::Malformed);
    }
    if (_context.content == nullptr)
    {
      return Accept(_order); // Without the tables the module and its slot cannot be judged.
    }
    const std::vector<StructureModuleDesc>& modules = _context.content->structures.modules;
    const std::vector<StructureDesc>& rows = _context.content->structures.structures;
    if (static_cast<std::size_t>(_order.operands[1]) >= modules.size() || structure->design >= rows.size())
    {
      return Reject(_order, RejectReason::Malformed);
    }
    const StructureModuleDesc& module = modules[static_cast<std::size_t>(_order.operands[1])];
    const StructureDesc& row = rows[structure->design];
    // The row lists the modules it takes, so a lab module cannot go onto a factory, and the slots
    // it has bound how many (GameDesign.md §5).
    if (std::find(row.modules.begin(), row.modules.end(), module.id) == row.modules.end() || structure->moduleCount >= row.moduleSlots ||
        structure->moduleCount >= MAX_STRUCTURE_MODULES)
    {
      return Reject(_order, RejectReason::InvalidTarget);
    }
    if (!UnlockedFor(seat, *_context.content, module.unlockedBy))
    {
      return Reject(_order, RejectReason::NotResearched);
    }
    return seat.powerHundredths >= module.costHundredths ? Accept(_order) : Reject(_order, RejectReason::CannotAfford);
  }

  case OrderKind::CancelProduction:
  {
    if (OwnedStructure(_order, _context) == nullptr)
    {
      return Reject(_order, RejectReason::NotOwned);
    }
    return _order.operands[1] >= 0 ? Accept(_order) : Reject(_order, RejectReason::Malformed);
  }

  case OrderKind::CancelResearch:
  {
    if (OwnedStructure(_order, _context) == nullptr)
    {
      return Reject(_order, RejectReason::NotOwned);
    }
    // There is nothing to cancel in a lab that is not working, and saying so is more use to a
    // commander than a silent success.
    const bool working = std::any_of(seat.researchActive.begin(), seat.researchActive.end(), [&_order](const ResearchProgress& _progress)
                                     { return _progress.lab.value == static_cast<std::uint32_t>(_order.operands[0]); });
    return working ? Accept(_order) : Reject(_order, RejectReason::InvalidTarget);
  }

  case OrderKind::SaveDesign:
  {
    if (_order.operands[0] < 0 || static_cast<std::uint32_t>(_order.operands[0]) >= MAX_SAVED_DESIGNS || _order.operands[1] < 0 ||
        _order.operands[2] < 0)
    {
      return Reject(_order, RejectReason::Malformed);
    }
    // A slot past the end leaves holes nothing would ever fill, and a design index is what a
    // production order names.
    if (static_cast<std::size_t>(_order.operands[0]) > seat.designs.size())
    {
      return Reject(_order, RejectReason::Malformed);
    }
    if (_context.content == nullptr)
    {
      return Accept(_order);
    }
    const DeviceDesign design = DesignFromOrder(_order.operands[1], _order.operands[2], _order.operands[3]);
    return {_order, ReasonFor(CheckDesign(seat, *_context.content, design))};
  }

  case OrderKind::Surrender:
  case OrderKind::Chat:
    return Accept(_order);
  }
  return Reject(_order, RejectReason::Malformed);
}

} // namespace Frontier
