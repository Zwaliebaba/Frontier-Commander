#pragma once

#include "Records.h"

#include <cstdint>
#include <span>
#include <vector>

// What a client knows about the devices on the field (TechnicalDesign.md §5.3). A DeviceState names
// its design by an index into its owner's list and nothing else, because a design is sent once -
// with the first device of it a commander sees - and every device of that design after it is four
// bytes lighter for it. This is where the one copy is kept.
//
// IT HOLDS EVERY SEAT'S DESIGNS AND NOT ONLY THE COMMANDER'S. A client is told the design of an
// enemy device it can see, which is what lets the selection panel say what it is fighting rather
// than name a box; the fog decides which enemy devices it sees at all, and that is the host's
// judgement, made before the record was sent (Net/Interest.h).

namespace Frontier
{

/// The designs a client has been sent, by seat and index. A linear search, because a seat holds at
/// most sixteen designs (Design/Interface.md §9) and eight seats is a hundred and twenty-eight rows
/// at the very most - a map would cost more to build than every lookup a match makes, which is the
/// same bargain Content/ContentTree.h strikes for its tables.
class DesignStore
{
public:
  /// Takes a design, replacing the row of the same seat and index. A host re-sends a design when a
  /// client rejoins and rebuilds from a full frame, so a second copy must land on the first rather
  /// than beside it.
  void Remember(const DesignState& _design)
  {
    for (DesignState& held : m_designs)
    {
      if (held.seat == _design.seat && held.index == _design.index)
      {
        held = _design;
        return;
      }
    }
    m_designs.push_back(_design);
  }

  [[nodiscard]] const DesignState* Find(std::uint8_t _seat, std::uint32_t _index) const noexcept
  {
    for (const DesignState& held : m_designs)
    {
      if (held.seat == _seat && held.index == _index)
      {
        return &held;
      }
    }
    return nullptr;
  }

  [[nodiscard]] std::span<const DesignState> All() const noexcept
  {
    return m_designs;
  }

  void Clear() noexcept
  {
    m_designs.clear();
  }

private:
  std::vector<DesignState> m_designs;
};

} // namespace Frontier
