#pragma once

#include "Device.h"
#include "MatchSettings.h"
#include "Order.h"
#include "ObjectId.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

/// The two bits of fog a cell carries for a commander (GameDesign.md §3; TechnicalDesign.md §4.6).
/// Explored is not derivable from the viewer count, because a cell stays explored after the last
/// viewer leaves, which is why the state is stored beside the count rather than computed from it.
enum class FogState : std::uint8_t
{
  Unexplored,
  Explored,
  Visible
};

/// A research item the seat is part-way through. S6 owns the rules; this is what S1 stores.
struct ResearchProgress
{
  std::uint32_t item; ///< Row index in the research table
  std::uint32_t remainingTicks;

  [[nodiscard]] constexpr bool operator==(const ResearchProgress&) const noexcept = default;
};

/// The last-seen record of a structure (TechnicalDesign.md §4.6): what an explored-but-not-visible
/// map shows, what an attack on an unseen target is redirected to, and what a rejoining client
/// gets back. S9 fills the store; S1 gives it its shape and carries it in the hash and snapshot.
struct Ghost
{
  ObjectId structure; ///< Stale by design: the structure may be long gone
  std::uint8_t seat;  ///< Who owned it when it was last seen
  std::uint32_t design;
  std::uint32_t cellX;
  std::uint32_t cellY;
  std::uint32_t seenTick;

  [[nodiscard]] constexpr bool operator==(const Ghost&) const noexcept = default;
};

/// A commander's simulation state: per-commander state is an array indexed by seat
/// (TechnicalDesign.md §4.3). Plain fields (AGENTS.md R8); the hash reads every one and the
/// snapshot writes every one, in this order.
///
/// There is no AI flag of its own: `kind` is SeatKind::Ai or it is not, and a second field saying
/// the same thing is a field that can disagree with it. `Scripted()` is the question spelled out.
struct Seat
{
  SeatKind kind;
  std::uint8_t alliance;

  std::int32_t powerHundredths;        ///< The stockpile, in hundredths of a power unit.
  std::int32_t stockpileCapHundredths; ///< What the stockpile may not exceed (GameDesign.md §4)

  std::vector<std::uint32_t> researchComplete; ///< Row indices, ascending, so two runs hash alike
  std::vector<ResearchProgress> researchActive;

  std::vector<DeviceDesign> designs; ///< What this commander may build; S5 owns the rules

  std::uint32_t deviceCount; ///< Against deviceCap; kept rather than counted, because the cap is
  std::uint32_t deviceCap;   ///< tested on every production tick and World would be walked for it
  std::uint32_t structureCount;
  std::uint32_t structureCap;

  /// Per cell, in cell-row-major order, sized by SizeFog. Empty until S9 fills them.
  std::vector<std::uint8_t> fogViewers; ///< A viewer count, so a moved viewer's old disc un-sees
  std::vector<FogState> fogState;

  std::vector<Ghost> ghosts; ///< Ascending by structure id, so two runs hash alike

  /// This tick's dropped orders, in the order stage 1 judged them, for Net to report. Cleared at
  /// the start of every stage 1, so it is what the tick refused rather than a running tally; it is
  /// in the hash, because two hosts that refuse different orders have diverged.
  std::vector<OrderRejection> rejections;

  bool defeated;    ///< Surrendered or annihilated; a defeated seat's orders are dropped.
  bool surrendered; ///< Which of the two it was, which the victory condition of S11 reads

  [[nodiscard]] bool Scripted() const noexcept
  {
    return kind == SeatKind::Ai;
  }

  [[nodiscard]] bool operator==(const Seat&) const noexcept = default;
};

/// Sizes the fog grids for a landscape of _cellsPerSide and clears them to unexplored. Called when
/// the landscape is created, because a seat exists before the landscape does.
void SizeFog(Seat& _seat, std::uint32_t _cellsPerSide);

} // namespace Frontier
