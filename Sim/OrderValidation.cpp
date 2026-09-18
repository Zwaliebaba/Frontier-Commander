#include "pch.h"

#include "OrderValidation.h"

#include "FixedPoint.h"

#include <algorithm>

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

} // namespace

bool CanSee(const Seat& _seat, const Landscape& _landscape, std::int32_t _x, std::int32_t _z)
{
  if (!_landscape.Created() || _seat.fogState.empty() || _x < 0 || _z < 0)
  {
    return false;
  }
  const std::uint32_t side = _landscape.Definition().cellsPerSide;
  const std::uint32_t cellX = static_cast<std::uint32_t>(_x >> Neuron::SUBUNITS_PER_CELL_SHIFT);
  const std::uint32_t cellY = static_cast<std::uint32_t>(_z >> Neuron::SUBUNITS_PER_CELL_SHIFT);
  if (cellX >= side || cellY >= side)
  {
    return false;
  }
  const std::size_t cell = static_cast<std::size_t>(cellY) * side + cellX;
  return cell < _seat.fogState.size() && _seat.fogState[cell] == FogState::Visible;
}

bool LastKnownPosition(const Seat& _seat, ObjectId _target, std::int32_t& _x, std::int32_t& _z)
{
  const auto ghost =
    std::find_if(_seat.ghosts.begin(), _seat.ghosts.end(), [_target](const Ghost& _ghost) { return _ghost.structure == _target; });
  if (ghost == _seat.ghosts.end())
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
    // NoCommandPost is "the seat has no standing structure at all" until S4 knows which row is
    // the command post; the check is real and S4 narrows it to that one row.
    const bool standing = [&_context, &_order]
    {
      bool found = false;
      _context.world->ForEachStructure(
        [&found, &_order](ObjectId, const Structure& _structure)
        { found = found || (_structure.seat == _order.seat && _structure.state == StructureState::Standing); });
      return found;
    }();
    if (!standing)
    {
      return Reject(_order, RejectReason::NoCommandPost);
    }
    if (seat.structureCount >= seat.structureCap)
    {
      return Reject(_order, RejectReason::AtCap);
    }
    // CannotAfford is "the stockpile is empty" until S3 prices a row; a seat with nothing cannot
    // buy anything, which is true now and stays true when S3 makes it exact.
    if (seat.powerHundredths <= 0)
    {
      return Reject(_order, RejectReason::CannotAfford);
    }
    if (!_context.landscape->Created())
    {
      return Reject(_order, RejectReason::InvalidPlacement);
    }
    const std::uint32_t side = _context.landscape->Definition().cellsPerSide;
    const bool onMap = _order.operands[1] >= 0 && _order.operands[2] >= 0 && static_cast<std::uint32_t>(_order.operands[1]) < side &&
                       static_cast<std::uint32_t>(_order.operands[2]) < side;
    return onMap ? Accept(_order) : Reject(_order, RejectReason::InvalidPlacement);
  }

  case OrderKind::SetProduction:
  {
    if (OwnedStructure(_order, _context) == nullptr)
    {
      return Reject(_order, RejectReason::NotOwned);
    }
    if (_order.operands[1] < 0 || static_cast<std::size_t>(_order.operands[1]) >= seat.designs.size())
    {
      // A design the seat has not saved is not a design it may build; S5 adds the parts' unlocks.
      return Reject(_order, RejectReason::NotResearched);
    }
    return seat.deviceCount >= seat.deviceCap ? Reject(_order, RejectReason::AtCap) : Accept(_order);
  }

  case OrderKind::SetResearch:
  {
    if (OwnedStructure(_order, _context) == nullptr)
    {
      return Reject(_order, RejectReason::NotOwned);
    }
    if (_order.operands[1] < 0)
    {
      return Reject(_order, RejectReason::Malformed);
    }
    const std::uint32_t item = static_cast<std::uint32_t>(_order.operands[1]);
    const bool done = std::find(seat.researchComplete.begin(), seat.researchComplete.end(), item) != seat.researchComplete.end();
    // Researching what is already researched is the one thing S2 can say about the tree; S6 adds
    // the prerequisites and the rows themselves.
    return done ? Reject(_order, RejectReason::NotResearched) : Accept(_order);
  }

  case OrderKind::CancelStructure:
  case OrderKind::Demolish:
  case OrderKind::BuildModule:
  case OrderKind::CancelProduction:
  case OrderKind::CancelResearch:
    return OwnershipOnly(_order, _context);

  case OrderKind::SaveDesign:
    return _order.operands[0] >= 0 && static_cast<std::uint32_t>(_order.operands[0]) < MAX_SAVED_DESIGNS
             ? Accept(_order)
             : Reject(_order, RejectReason::Malformed);

  case OrderKind::Surrender:
  case OrderKind::Chat:
    return Accept(_order);
  }
  return Reject(_order, RejectReason::Malformed);
}

} // namespace Frontier
