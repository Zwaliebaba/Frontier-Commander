#pragma once

#include "LandscapeDefinition.h"

#include <cstdint>
#include <span>
#include <vector>

// The deposits of a landscape (GameDesign.md §4): the points an extractor may be built on, and
// nowhere else. They come from the landscape's definition, which means a snapshot already carries
// them and a joining client already reproduces them; nothing here is state.
//
// A deposit is deliberately NOT an object in World. It has no id, because nothing ever refers to
// one: an extractor knows the cell it stands on and that is the whole of the relationship. Giving
// it an id would put a sixth kind in ObjectKind and a sixth map in World for a record that never
// changes, is never destroyed and is never pointed at. Feature.h says the same thing from the
// other side. What the simulation needs is one question answered quickly - is there a deposit at
// this cell - and that is what DepositField is.

namespace Frontier
{

/// No deposit is at that cell.
inline constexpr std::uint32_t NO_DEPOSIT = 0xFFFFFFFFu;

struct Deposit
{
  std::uint32_t cellX;
  std::uint32_t cellY;

  [[nodiscard]] constexpr bool operator==(const Deposit&) const noexcept = default;
};

/// The deposits of one landscape, ordered so that a cell can be found by bisection. Built from a
/// definition and rebuilt whenever the landscape is created or restored; it holds no state of its
/// own, so two hosts that built it from the same definition hold the same thing and it is in
/// neither the state hash nor the snapshot.
class DepositField
{
public:
  /// Takes the definition's deposits, drops any outside the landscape and any repeat of a cell,
  /// and orders what is left by row then column. A definition is authored by a tool and validated
  /// before it is loaded, so both of those are guards on a hostile file rather than expected
  /// cases - but a duplicate cell would let two extractors stand on one deposit, which is exactly
  /// the rule this file exists to enforce.
  void Build(const LandscapeDefinition& _definition);
  void Clear() noexcept;

  [[nodiscard]] std::uint32_t IndexAt(std::uint32_t _cellX, std::uint32_t _cellY) const noexcept;

  [[nodiscard]] bool Has(std::uint32_t _cellX, std::uint32_t _cellY) const noexcept
  {
    return IndexAt(_cellX, _cellY) != NO_DEPOSIT;
  }

  [[nodiscard]] std::span<const Deposit> All() const noexcept
  {
    return m_deposits;
  }

  [[nodiscard]] std::size_t Count() const noexcept
  {
    return m_deposits.size();
  }

private:
  std::vector<Deposit> m_deposits; ///< Ascending by cellY then cellX
};

} // namespace Frontier
