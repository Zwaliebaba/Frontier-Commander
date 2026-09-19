#pragma once

#include "Landscape.h"

#include "ComponentDesc.h"
#include "ContentTree.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

// The abstraction a hierarchical planner searches (TechnicalDesign.md §4.5). The landscape is cut
// into square clusters; inside a cluster, the cells a drive class can stand on fall into one or
// more CONNECTED COMPONENTS, and each component is a node. Two components of neighbouring clusters
// are joined where a cell of one is across a border from a cell of the other and both are
// passable. A search over a few thousand components then replaces a search over a million cells.
//
// WHY COMPONENTS RATHER THAN ENTRANCES. The usual hierarchical abstraction puts a node in the
// middle of each run of passable border cells. It is smaller, it gives better costs, and it is not
// exact: a run can span a wall inside its own cluster, and then one node stands for two places a
// drive cannot get between, so the graph reports a route that does not exist and a device walks
// into a wall. Both halves of that were built here first and both were caught by the flood-fill
// test - one node per run missed routes that existed, and treating a run as interchangeable found
// one that did not. A component cannot have either fault, because a component IS the set of cells
// that are mutually reachable. Reachability over this graph is the flood fill's answer exactly, by
// construction rather than by testing.
//
// WHAT IT COSTS. The abstract cost between two components is the octile distance between their
// representative cells, which is an estimate and not the true path length - a component is a
// region and has no single length. The refinement is exact, being a cell search inside each
// cluster, so a route is optimal in the small and approximate in the large. TechnicalDesign.md §12
// leaves hierarchical A* against flow fields to be settled by measurement; this is one of the
// numbers that measurement weighs.
//
// PER DRIVE CLASS, AND BUILT ON FIRST USE. Passability is the drive's. A class's whole graph is
// built the first time that class asks for anything, and dropped when the obstruction grid changes
// - on a Frontier landscape one flood fill of 256 cells per cluster, about a million cell visits,
// on an event that happens when a structure is placed rather than every tick.

namespace Frontier
{

/// Cells on a cluster's side. 16 makes a Small landscape 8 by 8 clusters and a Frontier one 64 by
/// 64, and puts a cluster's flood fill over at most 256 cells.
inline constexpr std::uint32_t CLUSTER_CELLS = 16;

/// No component: what an impassable cell belongs to, and what a lookup off the landscape answers.
inline constexpr std::uint32_t NO_COMPONENT = 0xFFFFFFFFu;

/// What a step costs. Integers, and 10 and 14 rather than 1 and 1.414, so that a diagonal is
/// dearer than a step by the right ratio without a float anywhere (AGENTS.md R16).
inline constexpr std::uint32_t STEP_COST = 10;
inline constexpr std::uint32_t DIAGONAL_COST = 14;

/// One way out of a component: which component it leads to, and the pair of cells the crossing is
/// made at. The cells are what the refinement walks to and steps onto.
struct ComponentLink
{
  std::uint32_t to;
  std::uint32_t fromCellX; ///< In this component, against the border
  std::uint32_t fromCellY;
  std::uint32_t toCellX; ///< Across the border, in the component this leads to
  std::uint32_t toCellY;

  [[nodiscard]] constexpr bool operator==(const ComponentLink&) const noexcept = default;
};

class ClusterGraph
{
public:
  /// Reads each class's passability rules from the tables and sizes the clusters. No class's graph
  /// is built here; each is built the first time it is asked for.
  void Build(const Landscape& _landscape, const ContentTree& _content);
  void Clear() noexcept;

  [[nodiscard]] bool Built() const noexcept
  {
    return m_clustersPerSide != 0;
  }

  [[nodiscard]] std::uint32_t ClustersPerSide() const noexcept
  {
    return m_clustersPerSide;
  }

  [[nodiscard]] std::uint32_t ClusterOf(std::uint32_t _cellX, std::uint32_t _cellY) const noexcept
  {
    return (_cellY / CLUSTER_CELLS) * m_clustersPerSide + (_cellX / CLUSTER_CELLS);
  }

  /// Whether a drive of this class may stand on this cell. The obstruction byte stops every class,
  /// the slope stops the classes that cannot climb it, and water stops those that cannot swim.
  [[nodiscard]] bool Passable(std::uint32_t _cellX, std::uint32_t _cellY, DriveClass _drive) const noexcept;

  /// The component a cell belongs to for a class, or NO_COMPONENT. Builds the class's graph if it
  /// has not been built; const because what changes is a cache of a pure function.
  [[nodiscard]] std::uint32_t ComponentAt(std::uint32_t _cellX, std::uint32_t _cellY, DriveClass _drive) const;

  /// Where a component can go.
  [[nodiscard]] std::span<const ComponentLink> LinksFrom(std::uint32_t _component, DriveClass _drive) const;

  /// The cluster a component is in, and a cell of it: what the heuristic measures between.
  [[nodiscard]] std::uint32_t ClusterOfComponent(std::uint32_t _component, DriveClass _drive) const;
  [[nodiscard]] bool RepresentativeCell(std::uint32_t _component, DriveClass _drive, std::uint32_t& _outX, std::uint32_t& _outY) const;

  [[nodiscard]] std::uint32_t ComponentCount(DriveClass _drive) const;

  /// Aims the graph at a landscape it has already been built against, without rebuilding it: what
  /// a Sim calls on itself after being copied or moved, because the landscape it holds is the same
  /// landscape at a new address (Sim/Sim.h). Never used to point it at a DIFFERENT landscape -
  /// Build is what does that, and it re-derives everything.
  void Rebind(const Landscape* _landscape) noexcept
  {
    m_landscape = _landscape;
  }

  /// Drops every class's graph. Called when the obstruction grid changes, because a component is a
  /// statement about which cells are passable and that is exactly what changed.
  void Invalidate() noexcept;

  /// The cheapest cell path between two cells of the SAME cluster, or no path. _outCells excludes
  /// the cell it started at and ends at the cell asked for.
  [[nodiscard]] bool LocalPath(std::uint32_t _fromX, std::uint32_t _fromY, std::uint32_t _toX, std::uint32_t _toY, DriveClass _drive,
                               std::uint32_t _cluster, std::vector<std::uint32_t>& _outCells, std::uint32_t& _outCost) const;

  /// How many cell searches the refinement has run, for the measurement.
  [[nodiscard]] std::uint64_t LocalSearches() const noexcept
  {
    return m_localSearches;
  }
  /// How many times a class's whole graph has been built, which invalidation drives.
  [[nodiscard]] std::uint64_t GraphBuilds() const noexcept
  {
    return m_graphBuilds;
  }

private:
  struct Rules
  {
    std::int32_t maxSlopePercent = -1; ///< -1 for a class no row defines, which is then impassable
    bool crossesWater = false;
  };

  /// One drive class's whole abstraction. Built together so that the numbering is a function of
  /// the landscape alone and not of the order things were asked for: two hosts must number the
  /// components alike or their searches break ties differently.
  struct ClassGraph
  {
    bool built = false;
    std::vector<std::uint32_t> componentOfCell; ///< Per cell, NO_COMPONENT where impassable
    std::vector<std::uint32_t> cluster;         ///< Per component
    std::vector<std::uint32_t> representative;  ///< Per component: the lowest cell index it holds
    std::vector<std::vector<ComponentLink>> links;
  };

  void BuildClass(DriveClass _drive) const;

  const Landscape* m_landscape = nullptr;
  std::uint32_t m_clustersPerSide = 0;
  std::array<Rules, DRIVE_CLASS_COUNT> m_rules{};
  mutable std::array<ClassGraph, DRIVE_CLASS_COUNT> m_classes;
  mutable std::uint64_t m_localSearches = 0;
  mutable std::uint64_t m_graphBuilds = 0;
};

} // namespace Frontier
