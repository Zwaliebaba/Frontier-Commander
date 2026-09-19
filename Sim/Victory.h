#pragma once

#include <cstdint>

// How a match ends (GameDesign.md §2) and where each commander stands while it runs. Stage 12 of
// the tick (TechnicalDesign.md §4.8), which is the only place either is decided.

namespace Frontier
{

class Sim;

/// Where a commander stands. One value a seat, written by stage 12 and by nothing else, carried by
/// the snapshot and reported to the client in Net's SeatState.
///
/// Eliminated and Lost are not the same answer. Eliminated is leaving the match while it runs -
/// surrendered, or annihilated - and the seat plays no further part from that tick. Lost is being
/// on the board when the match ended and not being the side that won, which is what the survival
/// clock hands out and what the last enemy alliance's elimination hands to nobody. A seat that
/// surrendered and whose alliance then won on the clock stays Eliminated: it was not there.
enum class VictoryState : std::uint8_t
{
  Playing,
  Won,
  Lost,
  Eliminated
};

inline constexpr std::uint8_t VICTORY_STATE_COUNT = 4;

/// True when the seat holds neither a structure nor a device carrying a builder module - the
/// annihilation rule of GameDesign.md §2, "every enemy structure and every enemy builder".
///
/// A plan is not a structure. It occupies no cell, nothing has been spent on it beyond the
/// reservation and no builder has touched it, so a commander left with nothing but plans has
/// nothing standing; a structure under construction, standing or demolishing is counted, because
/// each of those is a thing on the landscape an enemy has to destroy.
///
/// It answers for a seat that has never held either as it answers for one that has lost both, so
/// stage 12 rather than this predicate is what knows the difference (Seat::everHeldBase).
[[nodiscard]] bool Annihilated(const Sim& _sim, std::uint8_t _seat);

/// Stage 12: eliminate what the condition eliminates, then decide the match if it is over.
void CheckVictory(Sim& _sim);

} // namespace Frontier
