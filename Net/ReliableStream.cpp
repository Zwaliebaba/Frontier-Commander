#include "pch.h"

#include "ReliableStream.h"

#include <algorithm>

namespace Frontier
{

std::uint32_t ReliableStream::Send(const Order& _order)
{
  if (m_pending.size() >= MAX_UNACKNOWLEDGED_ORDERS)
  {
    ++m_counters.refused;
    return NO_SEQUENCE;
  }
  const std::uint32_t sequence = m_nextSequence++;
  m_pending.push_back({{sequence, _order}, 0, false, false});
  return sequence;
}

void ReliableStream::Due(std::uint32_t _tick, std::vector<OrderMessage>& _out)
{
  const std::uint32_t resend = ResendTicks();
  for (Pending& pending : m_pending)
  {
    if (pending.everSent)
    {
      // Unsigned subtraction is deliberate here and safe: the tick only ever moves forward, so a
      // difference that wrapped would mean a peer had run 4.2 billion ticks past this one.
      if (_tick - pending.sentTick < resend)
      {
        continue;
      }
      ++m_counters.resent;
      pending.resent = true;
    }
    else
    {
      ++m_counters.sent;
    }
    pending.everSent = true;
    pending.sentTick = _tick;
    _out.push_back(pending.message);
  }
}

void ReliableStream::Acknowledged(std::uint32_t _sequence, std::uint32_t _tick)
{
  while (!m_pending.empty() && m_pending.front().message.sequence <= _sequence)
  {
    const Pending& acknowledged = m_pending.front();
    // Only an order sent ONCE measures a round trip: for one that was resent there is no telling
    // which copy the acknowledgement answers, and taking the last send would measure a trip that
    // never happened. This is Karn's rule, and without it a lossy link talks itself into an
    // estimate far shorter than the link, which makes it resend more, which makes it lossier.
    if (acknowledged.everSent && !acknowledged.resent)
    {
      const std::uint32_t sample = _tick - acknowledged.sentTick;
      const std::uint32_t estimate = m_roundTripEighths >> ROUND_TRIP_SMOOTHING_SHIFT;
      m_roundTripEighths = m_roundTripEighths - estimate + sample;
    }
    m_pending.pop_front();
  }
}

std::uint32_t ReliableStream::ResendTicks() const noexcept
{
  return std::max(MIN_RESEND_TICKS, RoundTripTicks());
}

void ReliableStream::Receive(std::span<const OrderMessage> _orders, std::vector<Order>& _out)
{
  for (const OrderMessage& message : _orders)
  {
    if (message.sequence == NO_SEQUENCE || message.sequence <= m_delivered)
    {
      ++m_counters.duplicates;
      continue;
    }
    if (message.sequence == m_delivered + 1)
    {
      m_delivered = message.sequence;
      _out.push_back(message.order);
      ++m_counters.delivered;
      Drain(_out);
      continue;
    }
    // Early: held until the ones in front of it arrive. A duplicate of something already held is
    // dropped here rather than held twice.
    const auto at = std::lower_bound(m_early.begin(), m_early.end(), message.sequence,
                                     [](const OrderMessage& _held, std::uint32_t _sequence) { return _held.sequence < _sequence; });
    if (at != m_early.end() && at->sequence == message.sequence)
    {
      ++m_counters.duplicates;
      continue;
    }
    if (m_early.size() >= MAX_OUT_OF_ORDER_ORDERS)
    {
      ++m_counters.refused;
      continue;
    }
    m_early.insert(at, message);
    ++m_counters.buffered;
  }
}

void ReliableStream::Drain(std::vector<Order>& _out)
{
  std::size_t taken = 0;
  while (taken < m_early.size() && m_early[taken].sequence == m_delivered + 1)
  {
    m_delivered = m_early[taken].sequence;
    _out.push_back(m_early[taken].order);
    ++m_counters.delivered;
    ++taken;
  }
  m_early.erase(m_early.begin(), m_early.begin() + static_cast<std::ptrdiff_t>(taken));
}

} // namespace Frontier
