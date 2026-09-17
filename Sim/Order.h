#pragma once

#include "ByteReader.h"
#include "ByteWriter.h"

#include <array>
#include <cstddef>
#include <cstdint>

// An order is the only input the simulation has (TechnicalDesign.md §4.7): a fixed-layout record
// of the seat, the tick it is for, the kind and up to four operands. Twenty kinds, under 32 bytes,
// and one byte layout on the wire, in the replay and in a snapshot's queue (ADR-003).

namespace Frontier
{

/// The twenty kinds of TechnicalDesign.md §4.7, in its order; the value is the wire value.
enum class OrderKind : std::uint8_t
{
  Move,
  AttackMove,
  Attack,
  Patrol,
  Guard,
  Stop,
  ReturnToRepair,
  SetStance,
  PlaceStructure,
  CancelStructure,
  Demolish,
  BuildModule,
  SetProduction,
  CancelProduction,
  SetResearch,
  CancelResearch,
  SaveDesign,
  Group,
  Surrender,
  Chat
};

inline constexpr std::uint8_t ORDER_KIND_COUNT = 20;
inline constexpr std::size_t ORDER_OPERAND_COUNT = 4;

struct Order
{
  std::uint32_t tick; ///< The tick it is for: the host gives an arriving order the next tick, an AI seat a later one.
  std::array<std::int32_t, ORDER_OPERAND_COUNT>
    operands; ///< Ids, a position as two operands, a design or research id; the kind says which.
  std::uint8_t seat;
  OrderKind kind;

  [[nodiscard]] constexpr bool operator==(const Order&) const noexcept = default;
};

static_assert(sizeof(Order) < 32, "an order is under 32 bytes (TechnicalDesign.md §4.7)");

/// The bytes one order takes in a stream: the tick, the operands, the seat and the kind.
inline constexpr std::size_t ORDER_STREAM_BYTES = 4 + 4 * ORDER_OPERAND_COUNT + 1 + 1;

inline void WriteOrder(Neuron::ByteWriter& _writer, const Order& _order)
{
  _writer.Write(_order.tick);
  for (const std::int32_t operand : _order.operands)
  {
    _writer.Write(operand);
  }
  _writer.Write(_order.seat);
  _writer.Write(static_cast<std::uint8_t>(_order.kind));
}

/// False, with _out untouched, when the stream ends or names a kind outside the twenty.
[[nodiscard]] inline bool ReadOrder(Neuron::ByteReader& _reader, Order& _out)
{
  Order order{};
  if (!_reader.Read(order.tick))
  {
    return false;
  }
  for (std::int32_t& operand : order.operands)
  {
    if (!_reader.Read(operand))
    {
      return false;
    }
  }
  std::uint8_t kind = 0;
  if (!_reader.Read(order.seat) || !_reader.Read(kind) || kind >= ORDER_KIND_COUNT)
  {
    return false;
  }
  order.kind = static_cast<OrderKind>(kind);
  _out = order;
  return true;
}

} // namespace Frontier
