#pragma once

#include "ClusterGraph.h"
#include "ObjectId.h"
#include "Path.h"

#include "ComponentDesc.h"

#include <cstdint>
#include <span>
#include <vector>

// Hierarchical A* over the cluster graph, amortised (TechnicalDesign.md §4.5). This is the system
// that decides whether a large landscape works, so it is built in the shape the design gives and
// measured rather than assumed.
//
// THE BUDGET IS IN NODES EXPANDED, NOT IN TIME. A budget in milliseconds makes the result of a
// tick depend on how fast the machine is, which is a desynchronisation dressed up as an
// optimisation. A budget in nodes is a property of the state: two hosts expand the same nodes in
// the same order and reach the same answer on the same tick.
//
// A REQUEST IS SEARCHED ACROSS TICKS. Its open set, its costs and its came-from live in the
// request until it finishes, so a Frontier-sized route is spread over as many ticks as it needs
// instead of stalling one. The abstract search finishes first; the cells come after, and only for
// the clusters the device is about to walk (REFINE_CLUSTERS of them), the rest refined as it goes.
//
// THE ABSTRACT ROUTE IS A SEQUENCE OF COMPONENTS, not of cells. Reachability over that graph is
// exactly a flood fill's answer (Sim/ClusterGraph.h says why), so a route the search finds is a
// route that exists; what the abstraction gives up is the exact COST, since a component is a
// region and has no one length. The cells are exact, because refining a leg is a cell search.
//
// WHAT IS NOT IN THE SNAPSHOT, AND THE QUESTION THAT LEAVES FOR S8. A path is a pure function of
// the graph and the request, so nothing here is state that determines the simulation: a host that
// reloaded and re-requested would compute the same path. What it would NOT reproduce is the TICK
// the path arrives on, because the budget would start again - and if a device's movement depends
// on when its path arrives, that is a divergence. S8 is what makes paths matter, and it decides
// then: either the pending queue joins the snapshot, or a device re-requests on load and the
// arrival tick is made not to matter. Written down here rather than discovered there.

namespace Frontier
{

/// Nodes expanded per tick, across every pending request (TechnicalDesign.md §4.5).
inline constexpr std::uint32_t PLAN_BUDGET_NODES = 2000;

/// How many clusters at the front of a route are turned into cells at once. Enough that a device
/// has somewhere to walk while the next are refined, few enough that refining is not the abstract
/// search's cost all over again.
inline constexpr std::uint32_t REFINE_CLUSTERS = 2;

class PathPlanner
{
public:
  void SetGraph(const ClusterGraph* _graph) noexcept;

  /// Asks for a route. A second request for the same device replaces the first, because a
  /// commander who gives a new order has withdrawn the old one.
  void Request(ObjectId _device, std::uint32_t _fromX, std::uint32_t _fromY, std::uint32_t _toX, std::uint32_t _toY, DriveClass _drive);

  /// Forgets a device's request and its result; what a destroyed device means.
  void Cancel(ObjectId _device);

  /// One tick's planning: expands at most _budgetNodes across the pending requests, oldest first.
  void Advance(std::uint32_t _budgetNodes = PLAN_BUDGET_NODES);

  /// What a device's request came to, or null if it never asked.
  [[nodiscard]] const Path* Result(ObjectId _device) const noexcept;

  /// Refines the next clusters of a device's route into cells, as it advances along the ones it
  /// has. False when there is nothing left to refine or no usable path.
  bool RefineFurther(ObjectId _device);

  [[nodiscard]] std::uint32_t PendingRequests() const noexcept;
  [[nodiscard]] std::uint32_t LastNodesExpanded() const noexcept
  {
    return m_lastNodesExpanded;
  }
  [[nodiscard]] std::uint64_t TotalNodesExpanded() const noexcept
  {
    return m_totalNodesExpanded;
  }

  void Clear() noexcept;

private:
  /// One request, and the search over it as far as it has got.
  struct Job
  {
    ObjectId device;
    std::uint32_t fromX;
    std::uint32_t fromY;
    std::uint32_t toX;
    std::uint32_t toY;
    DriveClass drive;
    Path path;
    bool searching = false;
    std::uint32_t atX = 0; ///< How far the refinement has walked; where the next leg starts
    std::uint32_t atY = 0;

    // The abstract search's state, kept between ticks. Indices are components of the drive class's
    // graph, and the device's own component is where the search starts.
    std::vector<std::uint32_t> openNodes; ///< The open set, searched linearly for the cheapest
    std::vector<std::uint32_t> openCost;  ///< Parallel to openNodes: the f the entry was pushed with
    std::vector<std::uint32_t> gScore;    ///< Indexed by component, UNREACHED where not yet reached
    std::vector<std::uint32_t> cameFrom;
  };

  [[nodiscard]] Job* Find(ObjectId _device) noexcept;
  [[nodiscard]] const Job* Find(ObjectId _device) const noexcept;
  void Begin(Job& _job);
  /// Expands up to _budget nodes of one request; returns how many it used.
  std::uint32_t Step(Job& _job, std::uint32_t _budget);
  void Finish(Job& _job, std::uint32_t _reachedEnd);
  void Refine(Job& _job, std::uint32_t _clusters);

  const ClusterGraph* m_graph = nullptr;
  std::vector<Job> m_requests; ///< In arrival order, which is the order the budget serves
  std::uint32_t m_lastNodesExpanded = 0;
  std::uint64_t m_totalNodesExpanded = 0;
};

} // namespace Frontier
