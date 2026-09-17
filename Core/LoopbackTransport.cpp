#include "pch.h"

#include "LoopbackTransport.h"

#include "Assert.h"

#include <utility>

namespace Neuron
{

/// One end of the loopback: the host's, or a client's. A client end has one connection, the host;
/// the host end has one per client, under the id it assigned.
class LoopbackTransport::End final : public Transport
{
public:
  End(LoopbackTransport& _network, ConnectionId _id)
    : m_network(_network),
      m_id(_id)
  {
  }

  bool Send(ConnectionId _connection, std::span<const std::byte> _bytes) override
  {
    if (_bytes.size() > MAX_DATAGRAM_BYTES || !IsOpen(_connection))
    {
      ++m_refused;
      return false;
    }
    m_outbox.push_back({_connection, std::vector<std::byte>(_bytes.begin(), _bytes.end())});
    return true;
  }

  void Poll() override
  {
    // Out: everything queued since the last poll, through the faults, to its destination.
    std::vector<Outgoing> outgoing = std::move(m_outbox);
    m_outbox.clear();
    for (const Outgoing& datagram : outgoing)
    {
      if (IsOpen(datagram.to))
      {
        m_network.Carry(m_id, datagram.to, datagram.bytes);
      }
    }
    // In: what has arrived for this poll, in the order the network queued it.
    ++m_pollCount;
    for (std::size_t index = 0; index < m_inFlight.size();)
    {
      if (m_inFlight[index].deliverAtPoll <= m_pollCount)
      {
        m_inbox.push_back(std::move(m_inFlight[index]));
        m_inFlight.erase(m_inFlight.begin() + static_cast<std::ptrdiff_t>(index));
      }
      else
      {
        ++index;
      }
    }
    // Connections that completed since the last poll become acceptable.
    for (const ConnectionId connection : m_connecting)
    {
      m_acceptable.push_back(connection);
    }
    m_connecting.clear();
  }

  bool Receive(ConnectionId& _connection, std::span<const std::byte>& _bytes) override
  {
    if (m_inbox.empty())
    {
      return false;
    }
    m_held = std::move(m_inbox.front());
    m_inbox.pop_front();
    _connection = m_held.from;
    _bytes = m_held.bytes;
    return true;
  }

  bool Accept(ConnectionId& _connection) override
  {
    if (m_acceptable.empty())
    {
      return false;
    }
    _connection = m_acceptable.front();
    m_acceptable.pop_front();
    return true;
  }

  void Close(ConnectionId _connection) override
  {
    if (!IsOpen(_connection))
    {
      return;
    }
    Forget(_connection);
    // The other side: the host closing a client's id closes that client's view of the host, and a
    // client closing the host closes the host's view of that client.
    if (End* other = m_network.EndOf(m_id == HOST_CONNECTION ? _connection : HOST_CONNECTION))
    {
      other->Forget(m_id == HOST_CONNECTION ? HOST_CONNECTION : m_id);
    }
  }

  [[nodiscard]] bool IsOpen(ConnectionId _connection) const override
  {
    for (const ConnectionId open : m_open)
    {
      if (open == _connection)
      {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] std::uint32_t Refused() const override
  {
    return m_refused;
  }

  // ── Used by the network ──────────────────────────────────────────────────────────────────

  void Open(ConnectionId _connection)
  {
    if (!IsOpen(_connection))
    {
      m_open.push_back(_connection);
    }
  }

  void Connecting(ConnectionId _connection)
  {
    Open(_connection);
    m_connecting.push_back(_connection);
  }

  void Forget(ConnectionId _connection)
  {
    for (std::size_t index = 0; index < m_open.size(); ++index)
    {
      if (m_open[index] == _connection)
      {
        m_open.erase(m_open.begin() + static_cast<std::ptrdiff_t>(index));
        return;
      }
    }
  }

  /// A datagram on its way here, to arrive at the given poll; _reorder puts it ahead of the one queued last.
  void Queue(InFlight _datagram, bool _reorder)
  {
    if (_reorder && !m_inFlight.empty())
    {
      m_inFlight.insert(m_inFlight.end() - 1, std::move(_datagram));
    }
    else
    {
      m_inFlight.push_back(std::move(_datagram));
    }
  }

  [[nodiscard]] std::uint32_t PollCount() const noexcept
  {
    return m_pollCount;
  }

private:
  struct Outgoing
  {
    ConnectionId to;
    std::vector<std::byte> bytes;
  };

  LoopbackTransport& m_network;
  ConnectionId m_id;
  std::vector<ConnectionId> m_open;
  std::vector<ConnectionId> m_connecting;
  std::deque<ConnectionId> m_acceptable;
  std::vector<Outgoing> m_outbox;
  std::vector<InFlight> m_inFlight;
  std::deque<InFlight> m_inbox;
  InFlight m_held{};
  std::uint32_t m_pollCount = 0;
  std::uint32_t m_refused = 0;
};

LoopbackTransport::LoopbackTransport(std::uint64_t _faultSeed)
  : m_random(_faultSeed)
{
  m_ends.push_back(std::make_unique<End>(*this, HOST_CONNECTION));
}

LoopbackTransport::~LoopbackTransport() = default;

Transport& LoopbackTransport::Host() noexcept
{
  return *m_ends[0];
}

Transport& LoopbackTransport::Connect()
{
  const ConnectionId id = m_nextClient++;
  m_ends.push_back(std::make_unique<End>(*this, id));
  End& client = *m_ends.back();
  client.Open(HOST_CONNECTION);
  m_ends[0]->Connecting(id);
  return client;
}

LoopbackTransport::End* LoopbackTransport::EndOf(ConnectionId _connection) noexcept
{
  if (_connection == HOST_CONNECTION)
  {
    return m_ends[0].get();
  }
  const std::size_t index = _connection - HOST_CONNECTION;
  return index < m_ends.size() ? m_ends[index].get() : nullptr;
}

void LoopbackTransport::Carry(ConnectionId _from, ConnectionId _to, std::span<const std::byte> _bytes)
{
  End* destination = EndOf(_to);
  FRONTIER_ASSERT(destination != nullptr);
  if (destination == nullptr)
  {
    return;
  }
  ++m_counters.sent;
  // One draw per fault kind per datagram, always, so that turning a fault off changes no other
  // fault's draws and a test's loss stays the same when it adds a kind.
  const bool drop = m_random.Below(1000) < m_faults.dropPerMille;
  const bool duplicate = m_random.Below(1000) < m_faults.duplicatePerMille;
  const bool delay = m_random.Below(1000) < m_faults.delayPerMille;
  const bool reorder = m_random.Below(1000) < m_faults.reorderPerMille;
  if (drop)
  {
    ++m_counters.dropped;
    return;
  }
  const std::uint32_t arrival = destination->PollCount() + 1 + (delay ? m_faults.delayPolls : 0);
  if (delay)
  {
    ++m_counters.delayed;
  }
  if (reorder)
  {
    ++m_counters.reordered;
  }
  const int copies = duplicate ? 2 : 1;
  if (duplicate)
  {
    ++m_counters.duplicated;
  }
  for (int copy = 0; copy < copies; ++copy)
  {
    destination->Queue({_from, std::vector<std::byte>(_bytes.begin(), _bytes.end()), arrival}, reorder);
    ++m_counters.delivered;
  }
}

} // namespace Neuron
