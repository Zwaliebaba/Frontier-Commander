#pragma once

#include "ByteReader.h"
#include "ByteWriter.h"

#include <array>
#include <cstddef>
#include <cstdint>

// An order is the only input the simulation has (TechnicalDesign.md §4.7): a fixed-layout record
// of the seat, the tick it is for, the kind and up to four operands. Twenty kinds, under 32 bytes,
// and one byte layout on the wire, in the replay and in a snapshot's queue (ADR-003).
//
// THE OPERAND LAYOUT OF EVERY KIND (m1-vertical-slice/S2). An operand the table leaves blank is
// written zero and ignored. An order names ONE object: a selection of twelve devices is twelve
// orders, which is what keeps the record fixed-layout and the validation per object.
//
// An id operand is an ObjectId's value; where the kind may name more than one kind of object, a
// second operand carries the ObjectKind. A position is two operands, x then z, in subunits
// (1/256 of a world unit, Core/FixedPoint.h) - never in cells, so that an order is as precise as
// the simulation is. A cell position, where a kind wants one, is two operands of cell indices and
// says so.
//
// | Kind             | 0            | 1            | 2            | 3          | Applied by |
// |------------------|--------------|--------------|--------------|------------|------------|
// | Move             | device id    | x subunits   | z subunits   |            | S2         |
// | AttackMove       | device id    | x subunits   | z subunits   |            | S2         |
// | Attack           | device id    | target id    | target kind  |            | S10        |
// | Patrol           | device id    | x subunits   | z subunits   |            | S8         |
// | Guard            | device id    | x subunits   | z subunits   | device id  | S8         |
// | Stop             | device id    |              |              |            | S2         |
// | ReturnToRepair   | device id    |              |              |            | S10        |
// | SetStance        | device id    | stance axis  | stance value |            | S2         |
// | PlaceStructure   | structure row| cell x       | cell y       |            | S4         |
// | CancelStructure  | structure id |              |              |            | S4         |
// | Demolish         | structure id |              |              |            | S4         |
// | BuildModule      | structure id | module row   |              |            | S4         |
// | SetProduction    | structure id | design index | repeat count |            | S5         |
// | CancelProduction | structure id | queue slot   |              |            | S5         |
// | SetResearch      | structure id | research row |              |            | S6         |
// | CancelResearch   | structure id |              |              |            | S6         |
// | SaveDesign       | design index | chassis row  | drive row    | module rows| S5         |
// | Group            | device id    | group number |              |            | S2         |
// | Surrender        |              |              |              |            | S2         |
// | Chat             | message id   |              |              |            | S2         |
//
// Guard's operand 3 is the device it guards, or 0 to guard the position in operands 1 and 2.
// SaveDesign's operand 3 packs the module rows one per byte, which bounds a design to four
// mounts on the wire against MAX_MOUNTS of eight; S5 widens it if a chassis ever carries more.
// Chat's operand is an index into the message the client sent Net alongside: the simulation
// carries no text, because text is not state (ADR-002).

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

/// Why an order was dropped. Accepted is not a reason and is the only value that applies an order.
/// The client displays these, so the set is the vocabulary of "why did that not work", and it
/// lives here rather than beside the validator because a seat carries its rejections.
enum class RejectReason : std::uint8_t
{
  Accepted,
  NotOwned,         ///< The order names an object the seat does not own, or none at all
  NotVisible,       ///< The seat cannot see the target and has no record of where it was
  CannotAfford,     ///< The stockpile does not cover it
  AtCap,            ///< The device or structure cap is reached
  InvalidTarget,    ///< The target cannot be the object of this kind of order
  InvalidPlacement, ///< Off the landscape, in water, or over something standing
  NotResearched,    ///< The row it names is not unlocked for this seat
  NoCommandPost,    ///< The order needs a standing command post and the seat has none
  Malformed         ///< An operand outside its range: a client fault, not a game one
};

inline constexpr std::uint8_t REJECT_REASON_COUNT = 10;

/// One dropped order, for Net to report to the commander who sent it. The kind and the reason are
/// what a client needs to say "your Move was refused: you do not own that"; the order itself is
/// not kept, because the client still has it.
struct OrderRejection
{
  OrderKind kind;
  RejectReason reason;

  [[nodiscard]] constexpr bool operator==(const OrderRejection&) const noexcept = default;
};

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
