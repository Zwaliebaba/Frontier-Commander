#pragma once

#include "Random.h"
#include "Transport.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <span>
#include <vector>

// The in-process network (TechnicalDesign.md §5.6): a host end and any number of client ends in
// one process, so that Net and Replica are tested without a socket and single-player runs the
// same code path as a match over the network. Delivery happens on Poll, never on Send, and the
// faults a real network has are dialled in from a seeded Random, so that a test's loss is the
// same loss every run: a datagram may be dropped, duplicated, delayed by a number of the
// receiver's polls, or reordered before the one queued ahead of it.

namespace Neuron
{

/// Rates in parts per thousand, drawn per datagram; a delay in the receiver's polls.
struct LoopbackFaults
{
  std::uint32_t dropPerMille = 0;
  std::uint32_t duplicatePerMille = 0;
  std::uint32_t reorderPerMille = 0;
  std::uint32_t delayPerMille = 0;
  std::uint32_t delayPolls = 0; ///< How many polls a delayed datagram waits
};

class LoopbackTransport
{
public:
  /// _faultSeed drives every fault draw, so that the same seed and the same traffic lose the same datagrams.
  explicit LoopbackTransport(std::uint64_t _faultSeed);
  ~LoopbackTransport();
  LoopbackTransport(const LoopbackTransport&) = delete;
  LoopbackTransport& operator=(const LoopbackTransport&) = delete;

  void SetFaults(const LoopbackFaults& _faults) noexcept
  {
    m_faults = _faults;
  }

  [[nodiscard]] const LoopbackFaults& Faults() const noexcept
  {
    return m_faults;
  }

  /// The host end. It exists from construction and never closes.
  [[nodiscard]] Transport& Host() noexcept;

  /// A new client end, connected to the host; the host accepts it on its next Poll. The end
  /// belongs to this object and lives as long as it does.
  [[nodiscard]] Transport& Connect();

  /// The datagrams the faults dropped, duplicated, delayed and reordered, over the whole network.
  struct Counters
  {
    std::uint32_t sent = 0;
    std::uint32_t delivered = 0;
    std::uint32_t dropped = 0;
    std::uint32_t duplicated = 0;
    std::uint32_t delayed = 0;
    std::uint32_t reordered = 0;
  };

  [[nodiscard]] const Counters& Statistics() const noexcept
  {
    return m_counters;
  }

private:
  class End;
  friend class End;

  struct InFlight
  {
    ConnectionId from;
    std::vector<std::byte> bytes;
    std::uint32_t deliverAtPoll; ///< The receiver's poll count at which it arrives
  };

  /// Applies the faults and puts a datagram on its way to _to; called by an end's Poll.
  void Carry(ConnectionId _from, ConnectionId _to, std::span<const std::byte> _bytes);
  [[nodiscard]] End* EndOf(ConnectionId _connection) noexcept;

  LoopbackFaults m_faults;
  Random m_random;
  Counters m_counters;
  std::vector<std::unique_ptr<End>> m_ends; ///< [0] is the host
  ConnectionId m_nextClient = HOST_CONNECTION + 1;
};

} // namespace Neuron
