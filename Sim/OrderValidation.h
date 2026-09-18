#pragma once

#include "Landscape.h"
#include "Order.h"
#include "Seat.h"
#include "World.h"

#include <cstdint>
#include <span>

// The whole of the trust model (TechnicalDesign.md §4.7): a client's order is a request, and the
// host validates every one against what the seat owns, sees and can afford before stage 1 applies
// it. A client that lies about what it sees gains nothing, because the host decides visibility
// (§4.6) from its own state.
//
// One function, so that every later system adds its kinds' rules to one place rather than to its
// own stage: S4 tightens placement, S5 production and designs, S6 research, S10 targeting. What
// each check does today, and which task tightens it, is on the check.

namespace Frontier
{

/// The order to apply and whether to apply it. The order is not always the one submitted: an
/// Attack on a target the seat cannot see becomes an AttackMove to where it last saw it
/// (GameDesign.md §8), which is a rewrite rather than a rejection because the commander's
/// intention is still servable.
struct OrderCheck
{
  Order order;
  RejectReason reason;

  [[nodiscard]] constexpr bool Accepted() const noexcept
  {
    return reason == RejectReason::Accepted;
  }
};

/// What the simulation knows when it judges an order. A struct rather than five parameters,
/// because every later task adds to it and a signature that grows is a signature every caller
/// edits.
struct OrderContext
{
  const World* world;
  std::span<const Seat> seats;
  const Landscape* landscape;
  std::uint32_t tick;
};

/// Validates one order, rewriting it where the design says to. The seat is assumed to be a live
/// one: Sim drops an order from a seat outside the match, an empty seat or a defeated one before
/// it gets here, because that is a property of the seat rather than of the order.
[[nodiscard]] OrderCheck ValidateOrder(const Order& _order, const OrderContext& _context);

/// Where a seat last saw a structure, in subunits, or false when it has no record of it. What an
/// Attack on an unseen target is rewritten to.
[[nodiscard]] bool LastKnownPosition(const Seat& _seat, ObjectId _target, std::int32_t& _x, std::int32_t& _z);

/// Whether _seat can see the cell a world position falls in. Until S9 fills the fog grids every
/// cell is unexplored, so this is false for everything; that is the honest answer to the state
/// rather than a special case to remove later, and it is why an Attack is rewritten or refused
/// until visibility exists.
[[nodiscard]] bool CanSee(const Seat& _seat, const Landscape& _landscape, std::int32_t _x, std::int32_t _z);

} // namespace Frontier
