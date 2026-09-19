#pragma once

#include "Design.h"
#include "Device.h"
#include "FogGrid.h"
#include "GhostStore.h"
#include "MatchSettings.h"
#include "Order.h"
#include "ObjectId.h"
#include "Victory.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

/// A research item a LAB is part-way through (Sim/Research.h). The lab is part of the record
/// because "each lab researches one item" and "a destroyed lab loses the progress" are both rules,
/// and neither can be enforced by a list that does not say whose progress it is. The countdown
/// lives here rather than on the structure - which is where a factory's does - because a lab has
/// one item and a factory has a queue, so for a factory the entry cannot say which countdown it is.
struct ResearchProgress
{
  ObjectId lab;
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

  /// Every hundredth of power this commander's extractors have produced since the first tick,
  /// whether it was banked, spent or lost to the stockpile cap. The Survival condition is decided
  /// on it (GameDesign.md §2: "the side that extracted the most power wins"), which the stockpile
  /// cannot answer - a commander who spent everything he dug would lose to one who sat on it, and
  /// a cap that throws income away would hide the difference between them entirely. 64 bits
  /// because it only ever grows: a full seat at the cap's income for a day of ticks is far past
  /// what 32 would hold.
  std::int64_t extractedHundredths;

  std::vector<std::uint32_t> researchComplete; ///< Row indices, ascending, so two runs hash alike
  std::vector<ResearchProgress> researchActive;

  std::vector<DeviceDesign> designs; ///< What this commander may build; S5 owns the rules

  /// What research has added to each class, as percentages (Content/DesignStats.h). S6 fills it on
  /// an upgrade completing and S5 reads it wherever a statistic is derived, so an upgrade reaches
  /// every device of the class at once rather than being written into each of them.
  ClassUpgrades upgrades;

  /// The factories' queues, in the order the commander asked for them (Sim/Design.h). One list a
  /// seat rather than one a factory, because a structure is a fixed-layout record.
  std::vector<ProductionEntry> production;

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

  /// Where this commander stands (Sim/Victory.h). Stage 12 writes it and nothing else does. There
  /// is no separate defeated flag: it would be this field spelled a second way, and a second way
  /// to spell a fact is a way for the two to disagree.
  VictoryState victory;
  bool surrendered; ///< Eliminated by his own order rather than by annihilation

  /// True once the commander has held a structure or a builder. The annihilation rule cannot
  /// eliminate a seat that never had either: a match is set up over several ticks and a seat is
  /// empty-handed until its base level is placed, so without this a match would be over before it
  /// began. Once true it stays true - it records that the seat WAS established, not that it is.
  bool everHeldBase;

  [[nodiscard]] bool Scripted() const noexcept
  {
    return kind == SeatKind::Ai;
  }

  /// Out of the match: surrendered or annihilated. A seat that was still standing when the match
  /// ended and did not win is Lost rather than this, because it was never defeated in play.
  [[nodiscard]] bool Defeated() const noexcept
  {
    return victory == VictoryState::Eliminated;
  }

  [[nodiscard]] bool operator==(const Seat&) const noexcept = default;
};

} // namespace Frontier
