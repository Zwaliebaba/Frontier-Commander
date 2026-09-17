#include "pch.h"

#include "OrderQueue.h"

#include <algorithm>
#include <utility>

namespace Frontier
{

void OrderQueue::Push(const Order& _order)
{
  m_entries.push_back({m_nextArrival, _order});
  ++m_nextArrival;
}

void OrderQueue::Drain(std::uint32_t _tick, std::vector<Order>& _out)
{
  std::vector<Entry> due;
  std::vector<Entry> later;
  for (const Entry& entry : m_entries)
  {
    (entry.order.tick <= _tick ? due : later).push_back(entry);
  }
  // m_entries is in arrival order, so a stable sort by seat leaves each seat's orders in arrival order.
  std::stable_sort(due.begin(), due.end(), [](const Entry& _a, const Entry& _b) { return _a.order.seat < _b.order.seat; });
  for (const Entry& entry : due)
  {
    _out.push_back(entry.order);
  }
  m_entries = std::move(later);
}

void OrderQueue::Restore(std::uint32_t _nextArrival, std::vector<Entry> _entries)
{
  m_nextArrival = _nextArrival;
  m_entries = std::move(_entries);
}

} // namespace Frontier
