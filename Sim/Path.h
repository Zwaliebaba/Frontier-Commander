#pragma once

#include "ObjectId.h"

#include <cstdint>
#include <vector>

// What a planner hands a mover (TechnicalDesign.md §4.5). A path is cells, because movement is on
// the heightfield and a cell is what the obstruction grid and the passability rules speak in; the
// subunit positions a device actually walks are S8's, steering between these.
//
// A PATH IS REFINED FROM THE FRONT. Hierarchical planning finds the sequence of clusters first and
// turns clusters into cells afterwards, so a long path exists as a few refined clusters and a tail
// of abstract nodes. The device walks the refined part while the planner refines the next, which
// is what keeps a Frontier-sized request off one tick.

namespace Frontier
{

struct PathCell
{
  std::uint32_t x;
  std::uint32_t y;

  [[nodiscard]] constexpr bool operator==(const PathCell&) const noexcept = default;
};

/// Why a request has no path, or that it has one. Reported rather than left as an empty path,
/// because "nowhere to go" and "not worked out yet" ask different things of the mover.
enum class PathState : std::uint8_t
{
  Planning,    ///< Accepted, not finished; the budget has not reached it yet
  Partial,     ///< The route is known and the front of it is cells; the tail is still clusters
  Complete,    ///< Refined all the way to the destination
  Unreachable, ///< No route exists for this drive class, which a flood fill would agree with
  Refused      ///< The request itself was impossible: off the landscape, or no landscape at all
};

struct Path
{
  PathState state = PathState::Planning;
  /// The refined prefix, first cell first. The device's own cell is not in it.
  std::vector<PathCell> cells;
  /// The cluster-graph nodes still to refine, in order, ending at the destination's cluster.
  std::vector<std::uint32_t> nodes;
  /// What the request cost, for the ADR the flow-field question of TechnicalDesign.md §12 needs.
  std::uint32_t nodesExpanded = 0;

  [[nodiscard]] bool Usable() const noexcept
  {
    return state == PathState::Partial || state == PathState::Complete;
  }

  [[nodiscard]] bool operator==(const Path&) const noexcept = default;
};

} // namespace Frontier
