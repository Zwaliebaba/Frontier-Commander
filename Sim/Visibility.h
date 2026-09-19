#pragma once

#include "FogGrid.h"
#include "GhostStore.h"
#include "Landscape.h"
#include "ObjectId.h"
#include "Seat.h"
#include "World.h"

#include "ContentTree.h"

#include <cstdint>
#include <span>
#include <vector>

// Stage 7 of the tick (TechnicalDesign.md §4.8): fog of war, computed on the host, which is also
// the replication filter (§4.6). It is the system whose cost the design names first, so it is
// budgeted and deterministic from the start rather than made so later.
//
// HOW A DISC IS COUNTED, AND WHY STAMPS EXIST. A cell is visible while its viewer count is
// positive. A viewer that moves must therefore un-count the disc it last counted BEFORE counting
// the new one, and the only way to un-count exactly what was counted is to remember it. That
// memory is the stamp: the cell a viewer's disc is currently counted at, and the radius it was
// counted with. It is simulation state - the fog counts depend on it and nothing else determines
// it - so it is in the hash and in the snapshot beside the grids.
//
// ALLIED SEATS SHARE VISION by a viewer stamping its disc into EVERY seat of its own alliance
// (GameDesign.md §2). The alternative, a union taken when the grid is read, was rejected: explored
// is history, so a union at read would leave a commander unable to see the ground their ally
// scouted an hour ago unless the history were unioned too, and unioning history every read costs
// more than writing it once. Seats of one alliance therefore hold identical grids, which is a
// little memory for a filter that is one subscript.
//
// THE BUDGET IS IN VIEWERS, NOT IN WORK. REFRESH_BUDGET_VIEWERS a tick, moved viewers first and
// then the least recently refreshed, so the result of a tick depends only on the state and never
// on how long the last tick took. Eight seats at the army caps of GameDesign.md §4 is 4,000
// viewers, and 200 a tick at 20 ticks is exactly 4,000 a second, so every viewer refreshes at
// least once a second at the worst case the caps allow.

namespace Frontier
{

/// A viewer's disc as it is currently counted into the grids. The radius is stored rather than
/// re-derived because a sight upgrade (S6) changes it, and un-counting with the new radius what
/// was counted with the old one would leave cells lit for ever.
struct ViewerStamp
{
  ObjectId viewer;
  std::uint8_t seat; ///< Whose it was: the disc was counted into this seat's alliance and must come off the same one
  std::uint32_t cellX;
  std::uint32_t cellY;
  std::uint32_t radiusCells;
  std::uint32_t refreshedTick;

  [[nodiscard]] constexpr bool operator==(const ViewerStamp&) const noexcept = default;
};

/// Viewers refreshed in one tick (TechnicalDesign.md §4.6).
inline constexpr std::uint32_t REFRESH_BUDGET_VIEWERS = 200;

/// Height extends sight: a cell of radius for every this many world units the viewer stands above
/// the cell it is looking at (GameDesign.md §8).
inline constexpr std::int32_t WORLD_UNITS_PER_SIGHT_CELL = 32;

/// The most height may add, so that a viewer on a peak does not scan a landscape-sized disc. It is
/// the base radius again, so height may at most double a viewer's reach.
inline constexpr std::uint32_t MAX_HEIGHT_SIGHT_BONUS_FACTOR = 2;

class Visibility
{
public:
  /// Stage 7. Un-counts and re-counts the discs of up to REFRESH_BUDGET_VIEWERS viewers, moved
  /// ones first, then updates the ghost stores from what each seat can see now.
  void Advance(const World& _world, std::span<Seat> _seats, const Landscape& _landscape, const ContentTree& _content, std::uint32_t _tick);

  /// Drops every stamp and clears every grid: what a new landscape means, since a stamp names a
  /// cell of the old one.
  void Reset(std::span<Seat> _seats, const Landscape& _landscape);

  /// Un-counts the discs that reach the given cells and drops their stamps, so that the budget
  /// counts them again against ground that has changed. **Called BEFORE the heights change**,
  /// because un-counting a disc is only exact against the heights it was counted on.
  ///
  /// This is what a flatten costs, and it is what m1-vertical-slice/S9 left for S4 to narrow.
  /// Reset was the honest answer while nothing called it; it is the wrong one now, because a
  /// structure going up would black out every commander's explored map - history included - and
  /// a game where building an extractor un-scouts the landscape is not the game. A disc that does
  /// not reach the changed cells cannot have had a cell's visibility changed by them, so dropping
  /// exactly the ones that do is not an approximation.
  void InvalidateRegion(std::span<Seat> _seats, const Landscape& _landscape, std::uint32_t _cellX0, std::uint32_t _cellY0,
                        std::uint32_t _cellX1, std::uint32_t _cellY1);

  [[nodiscard]] std::span<const ViewerStamp> Stamps() const noexcept
  {
    return m_stamps;
  }

  [[nodiscard]] bool Restore(std::vector<ViewerStamp> _stamps);

  // ── What the tick-cost measurement of m1-vertical-slice/G3 reads ──────────────────────────
  [[nodiscard]] std::uint32_t LastRefreshedViewers() const noexcept
  {
    return m_lastRefreshedViewers;
  }
  [[nodiscard]] std::uint64_t LastHeightReads() const noexcept
  {
    return m_lastHeightReads;
  }
  [[nodiscard]] std::uint64_t TotalHeightReads() const noexcept
  {
    return m_totalHeightReads;
  }

  /// Whether a cell is on the line of sight from a viewer cell, walking the heightfield between
  /// them. Free of the grids, so a test can ask it directly.
  [[nodiscard]] static bool LineOfSight(const Landscape& _landscape, std::uint32_t _fromX, std::uint32_t _fromY, std::uint32_t _toX,
                                        std::uint32_t _toY, std::uint64_t& _heightReads);

  /// The height the line of sight is measured from and against, in whole world units: the tallest
  /// sample of the cell, so that a one-cell ridge blocks rather than being stepped over.
  [[nodiscard]] static std::int32_t CellHeight(const Landscape& _landscape, std::uint32_t _cellX, std::uint32_t _cellY) noexcept;

private:
  /// One viewer as this tick found it: where it is and how far it sees.
  struct Viewer
  {
    ObjectId id;
    std::uint8_t seat;
    std::uint32_t cellX;
    std::uint32_t cellY;
    std::uint32_t radiusCells;
  };

  void CollectViewers(const World& _world, std::span<const Seat> _seats, const Landscape& _landscape, const ContentTree& _content);
  void StampDisc(std::span<Seat> _seats, const Landscape& _landscape, const Viewer& _viewer, std::uint32_t _tick);
  void UnstampDisc(std::span<Seat> _seats, const Landscape& _landscape, const ViewerStamp& _stamp);
  void RefreshGhosts(const World& _world, std::span<Seat> _seats, const ContentTree& _content, std::uint32_t _tick);
  [[nodiscard]] ViewerStamp* FindStamp(ObjectId _viewer) noexcept;

  std::vector<ViewerStamp> m_stamps; ///< Ascending by viewer id; state
  std::vector<Viewer> m_viewers;     ///< This tick's, ascending by id; scratch
  std::vector<std::uint32_t> m_due;  ///< Indices into m_viewers, in refresh order; scratch
  std::uint32_t m_lastRefreshedViewers = 0;
  std::uint64_t m_lastHeightReads = 0;
  std::uint64_t m_totalHeightReads = 0;
};

} // namespace Frontier
