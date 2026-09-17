#pragma once

#include "Order.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Frontier
{

/// The orders waiting for their tick (TechnicalDesign.md §4.8, stage 1). Held in arrival order and
/// handed out per tick in seat order then arrival order, so that two hosts fed the same orders in
/// the same order apply them alike; the arrival counter travels in the snapshot so that a reloaded
/// queue hands its orders out in the order the original would have.
class OrderQueue
{
public:
  struct Entry
  {
    std::uint32_t arrival;
    Order order;

    [[nodiscard]] constexpr bool operator==(const Entry&) const noexcept = default;
  };

  void Push(const Order& _order);

  /// Moves every order for _tick or earlier into _out, appended in seat order then arrival order.
  /// An order for an earlier tick is a late one and goes out with this tick's, so nothing is lost.
  void Drain(std::uint32_t _tick, std::vector<Order>& _out);

  [[nodiscard]] std::size_t Size() const noexcept
  {
    return m_entries.size();
  }

  [[nodiscard]] bool Empty() const noexcept
  {
    return m_entries.empty();
  }

  /// The pending entries, in arrival order.
  [[nodiscard]] const std::vector<Entry>& Entries() const noexcept
  {
    return m_entries;
  }

  [[nodiscard]] std::uint32_t NextArrival() const noexcept
  {
    return m_nextArrival;
  }

  /// The queue as a snapshot carried it: the counter and the entries, verbatim.
  void Restore(std::uint32_t _nextArrival, std::vector<Entry> _entries);

private:
  std::vector<Entry> m_entries;
  std::uint32_t m_nextArrival = 0;
};

} // namespace Frontier
