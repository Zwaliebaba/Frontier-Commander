#pragma once

#include "Messages.h"
#include "Reassembler.h"
#include "ReliableStream.h"

#include "Datagram.h"
#include "Liveness.h"
#include "Transport.h"

#include <cstdint>
#include <string_view>
#include <vector>

// The client end of the conversation (TechnicalDesign.md §5.5): it joins, receives frames and
// fragments, applies what is complete and in order, acknowledges what it applied in every datagram
// it sends, and carries its commander's orders reliably through loss. It holds no world - Replica
// does that - so what it does with a frame is hand it to a sink.
//
// IT IS PASSIVE ABOUT TIME. Advance() is called once a pass with the tick the caller counts, and
// everything the client does happens inside it: nothing here has a thread, a timer or a clock.

namespace Frontier
{

/// What a client does with a frame it has applied. Replica implements this; the tests implement it
/// to count and to remember.
class FrameSink
{
public:
  virtual ~FrameSink() = default;
  virtual void Apply(const Frame& _frame) = 0;

protected:
  FrameSink() = default;
  FrameSink(const FrameSink&) = default;
  FrameSink& operator=(const FrameSink&) = default;
};

enum class ClientState : std::uint8_t
{
  Joining, ///< A Join has gone out and no answer has come back
  Playing, ///< Joined, with a seat and the match's settings
  Refused, ///< The host said no, and Refusal() says why
  Lost     ///< The host stopped answering for longer than the grace period
};

class Client
{
public:
  struct Counters
  {
    std::uint32_t framesApplied = 0;
    std::uint32_t framesSkipped = 0; ///< Arrived out of order, or against a baseline not in hand
    std::uint32_t datagramsSent = 0;
    std::uint32_t datagramsReceived = 0;
    std::uint32_t unreadable = 0; ///< A payload this build could not parse at all
  };

  /// The transport must outlive the client, and its connection to the host is HOST_CONNECTION.
  Client(Neuron::Transport& _transport, const Neuron::LivenessSettings& _liveness, std::uint32_t _tick);

  /// Sends the Join of §5.4. Called once; a rejoin is the same call with the same token. Named for
  /// the sending rather than for the message, because a member function called Join would hide the
  /// Join record from every line of this class that names it.
  void SendJoin(std::uint64_t _contentHash, std::uint64_t _token, std::string_view _name, std::uint32_t _tick);

  /// Queues one of this commander's orders. False when the unacknowledged window is full, which is
  /// a host that has stopped acknowledging rather than a commander who clicked too fast.
  [[nodiscard]] bool Submit(const Order& _order);

  /// One pass: read everything that has arrived, apply what can be applied, and send the orders and
  /// the acknowledgement that are due. The transport is polled by the caller, not here, because one
  /// Poll serves every end in a process.
  void Advance(std::uint32_t _tick, FrameSink& _sink);

  [[nodiscard]] ClientState State() const noexcept
  {
    return m_state;
  }

  [[nodiscard]] RefusalReason Refusal() const noexcept
  {
    return m_refusal;
  }

  [[nodiscard]] std::uint8_t Seat() const noexcept
  {
    return m_seat;
  }

  [[nodiscard]] const MatchSettings& Settings() const noexcept
  {
    return m_settings;
  }

  [[nodiscard]] const LandscapeDefinition& Landscape() const noexcept
  {
    return m_landscape;
  }

  /// The simulation tick the newest applied frame carried.
  [[nodiscard]] std::uint32_t Tick() const noexcept
  {
    return m_simulationTick;
  }

  /// The sequence of the newest applied frame: what this client acknowledges and what the host
  /// encodes its next frame against.
  [[nodiscard]] std::uint32_t AppliedSequence() const noexcept
  {
    return m_appliedSequence;
  }

  [[nodiscard]] const ReliableStream& OrderStream() const noexcept
  {
    return m_orders;
  }

  [[nodiscard]] const Reassembler& Fragments() const noexcept
  {
    return m_reassembler;
  }

  [[nodiscard]] const Counters& Statistics() const noexcept
  {
    return m_counters;
  }

  [[nodiscard]] const Neuron::FramingCounters& Framing() const noexcept
  {
    return m_framing;
  }

private:
  void Deliver(std::span<const std::byte> _payload, std::uint32_t _tick, FrameSink& _sink);
  void TakeFrame(const Frame& _frame, FrameSink& _sink);
  void SendOrdersAndAck(std::uint32_t _tick);
  void SendPayload(const Neuron::ByteWriter& _payload, std::uint32_t _tick);

  Neuron::Transport* m_transport;
  Neuron::Liveness m_liveness;
  Reassembler m_reassembler;
  ReliableStream m_orders;

  ClientState m_state = ClientState::Joining;
  RefusalReason m_refusal = RefusalReason::NoSeat;
  std::uint8_t m_seat = 0;
  MatchSettings m_settings{};
  LandscapeDefinition m_landscape{};
  std::uint32_t m_simulationTick = 0;
  std::uint32_t m_appliedSequence = NO_BASELINE;

  Join m_join{}; ///< Kept so that a Join can be sent again while no answer has come back
  std::uint32_t m_lastJoinTick = 0;
  /// Set when this client has applied a frame the host may not know about, or has SKIPPED one -
  /// a skip means the host is encoding against a baseline this client has already moved past, and
  /// the only way out of that is to tell it again. Cleared by the datagram that carries the
  /// acknowledgement. Without the skip half, a lost acknowledgement leaves the two encoding and
  /// discarding past each other until the next heartbeat.
  bool m_acknowledgementDue = false;

  Counters m_counters;
  Neuron::FramingCounters m_framing;
};

/// How often an unanswered Join is sent again, in the caller's ticks. A join is the one message
/// with no acknowledgement of its own, so it is the one that needs a repeat.
inline constexpr std::uint32_t JOIN_RETRY_TICKS = 20;

} // namespace Frontier
