#pragma once

#include "Records.h"

#include <cstdint>
#include <deque>
#include <span>
#include <vector>

// The one reliable stream in this protocol (TechnicalDesign.md §5.5): orders, client to host, with
// sequence numbers, a cumulative acknowledgement in every datagram, and a resend of anything still
// unacknowledged after a round-trip estimate. Frames are not carried here and never will be - a
// lost frame is answered by the next delta, and a retransmitted one would arrive after the frame
// that replaced it.
//
// ONE CLASS, BOTH ENDS. The sending half and the receiving half are here together because a
// conversation has one of each at both ends, and because the acknowledgement the receiving half
// produces is the acknowledgement the sending half consumes. The client sends orders and the host
// receives them today; nothing here knows which end it is.
//
// THE CLOCK IS THE CALLER'S TICK, not wall time. Net is not the simulation and nothing here is
// hashed, but a tick is what both ends already count and a resend measured in ticks is a resend
// two machines describe the same way.

namespace Frontier
{

/// Sequence 0 is never issued, so a cumulative acknowledgement of 0 means "nothing yet".
inline constexpr std::uint32_t NO_SEQUENCE = 0;

/// The estimate before a single round trip has been measured, and the floor under it afterwards:
/// two publishes at 10 Hz, which is the interval an acknowledgement can arrive on at best.
inline constexpr std::uint32_t INITIAL_ROUND_TRIP_TICKS = 8;
inline constexpr std::uint32_t MIN_RESEND_TICKS = 4;

/// How much of the estimate a new measurement moves: a smoothed average over eight samples, the
/// shift being what keeps it integer.
inline constexpr std::uint32_t ROUND_TRIP_SMOOTHING_SHIFT = 3;

/// The most orders held unacknowledged, and the most held out of order at the receiving end. A
/// commander issues a handful a second; a peer that has run past either is not one this end can
/// keep up with, and the counters say so.
inline constexpr std::size_t MAX_UNACKNOWLEDGED_ORDERS = 256;
inline constexpr std::size_t MAX_OUT_OF_ORDER_ORDERS = 64;

class ReliableStream
{
public:
  struct Counters
  {
    std::uint32_t sent = 0;
    std::uint32_t resent = 0;
    std::uint32_t delivered = 0;
    std::uint32_t duplicates = 0; ///< Arrived again after being delivered
    std::uint32_t buffered = 0;   ///< Arrived before the one in front of it
    std::uint32_t refused = 0;    ///< Dropped because a window was full
  };

  // ── The sending half ──────────────────────────────────────────────────────────────────────

  /// Queues an order and gives it the next sequence. 0, with the order dropped and counted, when
  /// the unacknowledged window is full: an order nobody can be told about is better refused here,
  /// where the caller can say so, than silently.
  [[nodiscard]] std::uint32_t Send(const Order& _order);

  /// What the next datagram should carry at _tick: every order not yet acknowledged that has never
  /// been sent, or whose last send was a round-trip estimate ago. Appended to _out in sequence
  /// order.
  void Due(std::uint32_t _tick, std::vector<OrderMessage>& _out);

  /// The peer's cumulative acknowledgement: everything up to and including _sequence has arrived.
  /// _tick is when it arrived, which is what measures the round trip.
  void Acknowledged(std::uint32_t _sequence, std::uint32_t _tick);

  [[nodiscard]] std::uint32_t RoundTripTicks() const noexcept
  {
    return (m_roundTripEighths + 4) / 8;
  }

  /// When an unacknowledged order is sent again: the round-trip estimate, with a floor under it so
  /// that a very fast link does not resend an order the acknowledgement for is already on its way.
  [[nodiscard]] std::uint32_t ResendTicks() const noexcept;

  [[nodiscard]] std::size_t Unacknowledged() const noexcept
  {
    return m_pending.size();
  }

  // ── The receiving half ────────────────────────────────────────────────────────────────────

  /// Takes what a datagram carried. Appends to _out, in sequence order, every order that has not
  /// been delivered before, holding one that arrived early until its predecessor does.
  void Receive(std::span<const OrderMessage> _orders, std::vector<Order>& _out);

  /// What to tell the peer: the newest sequence such that it and everything before it has arrived.
  [[nodiscard]] std::uint32_t Acknowledgement() const noexcept
  {
    return m_delivered;
  }

  [[nodiscard]] const Counters& Statistics() const noexcept
  {
    return m_counters;
  }

private:
  struct Pending
  {
    OrderMessage message;
    std::uint32_t sentTick;
    bool everSent;
    bool resent; ///< Karn's rule: an order that was sent twice measures no round trip
  };

  /// Delivers everything in the out-of-order buffer that has become the next one expected.
  void Drain(std::vector<Order>& _out);

  std::deque<Pending> m_pending;    ///< Sent and not acknowledged, in sequence order
  std::uint32_t m_nextSequence = 1; ///< 0 is never issued

  /// The estimate held in eighths of a tick, which is what keeps a smoothed average integer and
  /// still able to move: held in whole ticks, a sample within eight of the estimate would shift it
  /// by nothing at all and the estimate would never reach the link.
  std::uint32_t m_roundTripEighths = INITIAL_ROUND_TRIP_TICKS * 8;

  std::vector<OrderMessage> m_early; ///< Arrived before their predecessor, in sequence order
  std::uint32_t m_delivered = NO_SEQUENCE;

  Counters m_counters;
};

} // namespace Frontier
