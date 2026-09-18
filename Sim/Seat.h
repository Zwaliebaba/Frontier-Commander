#pragma once

#include "Device.h"
#include "FogGrid.h"
#include "GhostStore.h"
#include "MatchSettings.h"
#include "Order.h"
#include "ObjectId.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

/// A research item the seat is part-way through. S6 owns the rules; this is what S1 stores.
struct ResearchProgress
{
  std::uint32_t item; ///< Row index in the research table
  std::uint32_t remainingTicks;

  [[nodiscard]] constexpr bool operator==(const ResearchProgress&) const noexcept = default;
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

  /// What this commander can see and has seen (Sim/FogGrid.h). Sized when the landscape is
  /// created; S9's Visibility is what counts viewers into it.
  FogGrid fog;

  /// The last-seen record of every structure this commander has ever seen (Sim/GhostStore.h).
  GhostStore ghosts;

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

} // namespace Frontier
