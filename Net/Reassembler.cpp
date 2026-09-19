#include "pch.h"

#include "Reassembler.h"

#include <algorithm>

namespace Frontier
{

Reassembler::Partial* Reassembler::SlotFor(const Fragment& _fragment)
{
  for (Partial& partial : m_partials)
  {
    if (partial.sequence == _fragment.frameSequence)
    {
      // A fragment that disagrees with the ones already here about how many there are cannot be
      // trusted to be a piece of the same frame.
      if (partial.count != _fragment.count)
      {
        ++m_counters.mismatched;
        return nullptr;
      }
      return &partial;
    }
  }

  if (m_partials.size() < MAX_PARTIAL_FRAMES)
  {
    m_partials.push_back({});
  }
  else
  {
    // Room is made by giving up on the oldest frame in hand. That is the discard the design asks
    // for, and it happens here rather than on a timer: a newer frame arriving IS the news that the
    // older one is not going to complete.
    const auto oldest = std::min_element(m_partials.begin(), m_partials.end(),
                                         [](const Partial& _a, const Partial& _b) { return _a.sequence < _b.sequence; });
    if (oldest->sequence > _fragment.frameSequence)
    {
      // Every slot holds a frame newer than this fragment's: this is a piece of a frame that was
      // given up on, and taking it would throw away a newer one to hold it.
      ++m_counters.abandoned;
      return nullptr;
    }
    ++m_counters.abandoned;
    *oldest = {};
    return &*oldest;
  }

  Partial& fresh = m_partials.back();
  fresh.sequence = _fragment.frameSequence;
  fresh.count = _fragment.count;
  fresh.pieces.resize(_fragment.count);
  fresh.arrived.assign(_fragment.count, false);
  return &fresh;
}

bool Reassembler::Add(const Fragment& _fragment, std::vector<std::byte>& _out)
{
  Partial* partial = SlotFor(_fragment);
  if (partial == nullptr)
  {
    return false;
  }
  if (partial->count == 0)
  {
    partial->sequence = _fragment.frameSequence;
    partial->count = _fragment.count;
    partial->pieces.resize(_fragment.count);
    partial->arrived.assign(_fragment.count, false);
  }
  if (_fragment.index >= partial->count)
  {
    ++m_counters.mismatched;
    return false;
  }
  if (partial->arrived[_fragment.index])
  {
    ++m_counters.duplicates;
    return false;
  }
  partial->pieces[_fragment.index] = _fragment.bytes;
  partial->arrived[_fragment.index] = true;
  ++partial->have;
  if (partial->have < partial->count)
  {
    return false;
  }

  _out.clear();
  for (const std::vector<std::byte>& piece : partial->pieces)
  {
    _out.insert(_out.end(), piece.begin(), piece.end());
  }
  ++m_counters.completed;
  const std::uint32_t completed = partial->sequence;
  // The frame that completed, and every partial older than it. A frame the client can apply makes
  // an older half-arrived one worthless: the host encodes the next frame against what this one
  // acknowledges, so the missing piece would complete a frame nothing is a delta from any more.
  for (const Partial& older : m_partials)
  {
    if (older.sequence < completed && older.have > 0)
    {
      ++m_counters.abandoned;
    }
  }
  m_partials.erase(
    std::remove_if(m_partials.begin(), m_partials.end(), [completed](const Partial& _partial) { return _partial.sequence <= completed; }),
    m_partials.end());
  return true;
}

void Reassembler::Clear() noexcept
{
  m_partials.clear();
}

} // namespace Frontier
