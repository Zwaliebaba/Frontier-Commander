#pragma once

#include "FrameEncoder.h"
#include "Fragmenter.h"
#include "Interest.h"
#include "Messages.h"
#include "ReliableStream.h"

#include "Sim.h"

#include "Datagram.h"
#include "Liveness.h"
#include "Transport.h"

#include <cstdint>
#include <vector>

// The host end of the conversation (TechnicalDesign.md §5.2 to §5.4): it answers joins, publishes
// each client exactly what its commander may know, takes the orders back and hands them to the
// simulation's queue. One simulation runs and the host is what tells everyone else about it.
//
// THE FOG IS ENFORCED HERE. GameDesign.md §10 is explicit that a modified client must see nothing
// an honest one does not, so the filter is the interest set of Net/Interest.h and the encoder never
// names an object it does not carry. That is the security property NetTests::InterestTests asserts
// over a whole scripted match rather than over one frame.
//
// IT HAS NO THREAD AND NO CLOCK. Advance() is called once a pass with the tick the host loop
// counts, which is the simulation's tick, and publishing happens on the ticks Sim::PublishDue says.

namespace Frontier
{

/// Where a seat's player is (§5.4). A seat under AI control is still played - the simulation never
/// stops for an absent commander - and a rejoin with the same token takes it back.
enum class SeatConnection : std::uint8_t
{
  Open,    ///< Nobody has taken this seat
  Playing, ///< A client is connected and answering
  Suspect, ///< It has gone quiet, and the grace period is running
  UnderAi  ///< The grace period ran out; it plays as a scripted seat until its player rejoins
};

/// One connected client and everything the host holds about it.
struct HostClient
{
  Neuron::ConnectionId connection = Neuron::NO_CONNECTION;
  std::uint64_t token = 0; ///< A rejoin with the same token takes this seat back (§5.4)
  ClientView view;
  ReliableStream orders; ///< The receiving half of §5.5's stream; the client holds the other
  std::uint32_t lastHeardTick = 0;
  SeatConnection state = SeatConnection::Open;
};

class Host
{
public:
  struct Counters
  {
    std::uint32_t joins = 0;
    std::uint32_t rejoins = 0;
    std::uint32_t refusals = 0;
    std::uint32_t framesPublished = 0;
    std::uint32_t fullFrames = 0;
    std::uint32_t fragments = 0;
    std::uint32_t ordersApplied = 0;
    std::uint32_t ordersRefusedShape = 0;
    std::uint32_t droppedToAi = 0;
    std::uint32_t largestFrameBytes = 0;
  };

  /// The simulation and the transport must both outlive the host. The content hash is what a join
  /// is checked against (ADR-009's binding, at the join this time).
  Host(Sim& _sim, Neuron::Transport& _transport, std::uint64_t _contentHash, std::uint32_t _tick);

  /// One pass: accept new connections, read what arrived, and publish to every client when the
  /// simulation says a publish is due. The transport is polled by the caller.
  void Advance(std::uint32_t _tick);

  /// The events of the interval, to go out with the next frame. The host loop fills this from what
  /// the tick did; it is emptied by the publish.
  [[nodiscard]] std::vector<Event>& Events() noexcept
  {
    return m_events;
  }

  [[nodiscard]] SeatConnection SeatState(std::uint8_t _seat) const noexcept;

  [[nodiscard]] const Counters& Statistics() const noexcept
  {
    return m_counters;
  }

  [[nodiscard]] const HostClient* ClientForSeat(std::uint8_t _seat) const noexcept;

  /// The bytes the last publish to this seat took, whole, before fragmentation. What the ADR's
  /// bandwidth figures are measured from.
  [[nodiscard]] std::uint32_t LastFrameBytes(std::uint8_t _seat) const noexcept;

private:
  void Receive(std::uint32_t _tick);
  void OnJoin(Neuron::ConnectionId _connection, const Join& _join, std::uint32_t _tick);
  void OnOrders(HostClient& _client, const Orders& _orders, std::uint32_t _tick);
  void Publish(std::uint32_t _tick);
  void PublishTo(HostClient& _client, std::uint32_t _tick);
  void SendPayload(Neuron::ConnectionId _connection, const Neuron::ByteWriter& _payload);
  void Refuse(Neuron::ConnectionId _connection, RefusalReason _reason);
  [[nodiscard]] std::uint8_t FreeSeat() const noexcept;
  [[nodiscard]] HostClient* Find(Neuron::ConnectionId _connection) noexcept;

  Sim* m_sim;
  Neuron::Transport* m_transport;
  std::uint64_t m_contentHash;

  std::vector<HostClient> m_clients;
  std::vector<Event> m_events;
  std::vector<std::uint32_t> m_lastFrameBytes; ///< By seat, for the ADR's measurements

  InterestSet m_interest; ///< Scratch, reused every publish
  Frame m_frame;          ///< Scratch
  FrameRecord m_record;   ///< Scratch
  std::vector<Fragment> m_fragments;

  Counters m_counters;
  Neuron::FramingCounters m_framing;
};

} // namespace Frontier
