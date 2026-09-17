#include "pch.h"

#include "StateHash.h"

namespace Frontier
{

void StateHash::AddSeat(const Seat& _seat) noexcept
{
  Add(_seat.kind);
  Add(_seat.alliance);
  Add(_seat.powerHundredths);
  AddBool(_seat.defeated);
}

void StateHash::AddOrder(const Order& _order) noexcept
{
  Add(_order.tick);
  AddSpan(std::span<const std::int32_t>(_order.operands));
  Add(_order.seat);
  Add(_order.kind);
}

} // namespace Frontier
