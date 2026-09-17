#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

// The transport seam (TechnicalDesign.md §5.6): the whole network conversation of Net and Replica
// runs over this interface, with LoopbackTransport as its in-process implementation from M1 and
// UdpTransport slotting in at M3 with no protocol change. It names no socket type. A connection is
// a number the host end assigns when a client end connects; a client end talks to one connection,
// the host's, and a host end to as many as have connected. Datagrams are unreliable and unordered
// by contract, whatever the implementation happens to do: Net builds its reliability on top.

namespace Neuron
{

using ConnectionId = std::uint32_t;

/// No connection; never assigned.
inline constexpr ConnectionId NO_CONNECTION = 0;

/// The connection a client end talks to.
inline constexpr ConnectionId HOST_CONNECTION = 1;

/// The most a datagram carries, header included: under the common path MTU, so that Net's
/// fragments (TechnicalDesign.md §5.3) never meet IP fragmentation.
inline constexpr std::size_t MAX_DATAGRAM_BYTES = 1200;

class Transport
{
public:
  virtual ~Transport() = default;

  /// Queues a datagram to a connection. False, and counted in Refused(), for a datagram over
  /// MAX_DATAGRAM_BYTES or a connection that is not open; the bytes are copied.
  virtual bool Send(ConnectionId _connection, std::span<const std::byte> _bytes) = 0;

  /// Moves the network: what was queued goes out, what has arrived comes in. Once per loop pass.
  virtual void Poll() = 0;

  /// The next datagram that has arrived, or false when there is none. The bytes stay valid until
  /// the next Receive or Poll on this end.
  [[nodiscard]] virtual bool Receive(ConnectionId& _connection, std::span<const std::byte>& _bytes) = 0;

  /// Host end: the next connection that has completed since the last call, with the id this end
  /// assigned it. A client end never has one.
  [[nodiscard]] virtual bool Accept(ConnectionId& _connection) = 0;

  /// Closes a connection from this side; the other side sees it as not open from then on, and
  /// what was in flight to either side is dropped.
  virtual void Close(ConnectionId _connection) = 0;

  [[nodiscard]] virtual bool IsOpen(ConnectionId _connection) const = 0;

  /// Sends refused for size or for a closed connection.
  [[nodiscard]] virtual std::uint32_t Refused() const = 0;

protected:
  Transport() = default;
  Transport(const Transport&) = default;
  Transport& operator=(const Transport&) = default;
};

} // namespace Neuron
