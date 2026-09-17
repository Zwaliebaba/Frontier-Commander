#pragma once

#include "MatchSettings.h"

#include <cstdint>

namespace Frontier
{

/// A commander's simulation state: per-commander state is an array indexed by seat
/// (TechnicalDesign.md §4.3). Plain fields (AGENTS.md R8); the hash reads every one and the
/// snapshot writes every one, in this order.
struct Seat
{
  SeatKind kind;
  std::uint8_t alliance;
  std::int32_t powerHundredths; ///< The stockpile, in hundredths of a power unit.
  bool defeated;                ///< Surrendered or annihilated; a defeated seat's orders are dropped.

  [[nodiscard]] constexpr bool operator==(const Seat&) const noexcept = default;
};

} // namespace Frontier
