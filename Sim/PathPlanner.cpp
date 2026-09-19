#include "pch.h"

#include "PathPlanner.h"

#include <algorithm>

namespace Frontier
{

namespace
{

constexpr std::uint32_t UNREACHED = 0xFFFFFFFFu;

/// The octile distance in the cost model's own units: a diagonal is 14 and a step is 10, so the
/// cheapest conceivable route between two cells is 14 for each diagonal it can take and 10 for the
/// rest. Admissible, which is what makes the A* find the cheapest route over the graph it is given
/// rather than merely a route.
[[nodiscard]] std::uint32_t Octile(std::uint32_t _fromX, std::uint32_t _fromY, std::uint32_t _toX, std::uint32_t _toY) noexcept
{
  const std::uint32_t dx = _fromX > _toX ? _fromX - _toX : _toX - _fromX;
  const std::uint32_t dy = _fromY > _toY ? _fromY - _toY : _toY - _fromY;
  const std::uint32_t diagonal = std::min(dx, dy);
  const std::uint32_t straight = std::max(dx, dy) - diagonal;
  return diagonal * DIAGONAL_COST + straight * STEP_COST;
}

} // namespace

void PathPlanner::SetGraph(const ClusterGraph* _graph) noexcept
{
  m_graph = _graph;
  Clear();
}

void PathPlanner::Clear() noexcept
{
  m_requests.clear();
  m_lastNodesExpanded = 0;
}

PathPlanner::Job* PathPlanner::Find(ObjectId _device) noexcept
{
  const auto at = std::find_if(m_requests.begin(), m_requests.end(), [_device](const Job& _job) { return _job.device == _device; });
  return at == m_requests.end() ? nullptr : &*at;
}

const PathPlanner::Job* PathPlanner::Find(ObjectId _device) const noexcept
{
  const auto at = std::find_if(m_requests.begin(), m_requests.end(), [_device](const Job& _job) { return _job.device == _device; });
  return at == m_requests.end() ? nullptr : &*at;
}

const Path* PathPlanner::Result(ObjectId _device) const noexcept
{
  const Job* job = Find(_device);
  return job == nullptr ? nullptr : &job->path;
}

std::vector<PathRequest> PathPlanner::Requests() const
{
  std::vector<PathRequest> requests;
  requests.reserve(m_requests.size());
  for (const Job& job : m_requests)
  {
    requests.push_back({job.device, job.fromX, job.fromY, job.toX, job.toY, job.drive, job.atX, job.atY, job.path});
  }
  return requests;
}

void PathPlanner::Restore(const PathRequest& _request)
{
  Job job;
  job.device = _request.device;
  job.fromX = _request.fromX;
  job.fromY = _request.fromY;
  job.toX = _request.toX;
  job.toY = _request.toY;
  job.drive = _request.drive;
  if (_request.path.state != PathState::Planning)
  {
    // Searched and answered: the route, what is left to refine and where the refinement stands are
    // all in the record, and there is nothing to replay.
    job.path = _request.path;
    job.atX = _request.atX;
    job.atY = _request.atY;
    job.searching = false;
    m_requests.push_back(std::move(job));
    return;
  }
  m_requests.push_back(std::move(job));
  Job& placed = m_requests.back();
  Begin(placed);
  if (placed.path.state == PathState::Planning)
  {
    // The same search over the same graph, expanded for the nodes it had already spent: the open
    // set it rebuilds is the open set it had. Step stops of its own accord if the search would
    // have finished within those nodes, which it cannot have done, because then the state written
    // would not have been Planning.
    (void)Step(placed, _request.path.nodesExpanded);
  }
  placed.path.nodesExpanded = _request.path.nodesExpanded;
}

std::uint32_t PathPlanner::PendingRequests() const noexcept
{
  return static_cast<std::uint32_t>(
    std::count_if(m_requests.begin(), m_requests.end(), [](const Job& _job) { return _job.path.state == PathState::Planning; }));
}

void PathPlanner::Cancel(ObjectId _device)
{
  const auto at = std::find_if(m_requests.begin(), m_requests.end(), [_device](const Job& _job) { return _job.device == _device; });
  if (at != m_requests.end())
  {
    m_requests.erase(at);
  }
}

void PathPlanner::Request(ObjectId _device, std::uint32_t _fromX, std::uint32_t _fromY, std::uint32_t _toX, std::uint32_t _toY,
                          DriveClass _drive)
{
  Cancel(_device);
  Job job;
  job.device = _device;
  job.fromX = _fromX;
  job.fromY = _fromY;
  job.toX = _toX;
  job.toY = _toY;
  job.drive = _drive;
  m_requests.push_back(std::move(job));
  Begin(m_requests.back());
}

void PathPlanner::Begin(Job& _job)
{
  _job.path = {};
  if (m_graph == nullptr || !m_graph->Built())
  {
    _job.path.state = PathState::Refused;
    return;
  }
  // A cell this drive cannot stand on is refused rather than searched: the answer is known without
  // expanding anything, and it is a different answer from "no route exists".
  const std::uint32_t from = m_graph->ComponentAt(_job.fromX, _job.fromY, _job.drive);
  const std::uint32_t to = m_graph->ComponentAt(_job.toX, _job.toY, _job.drive);
  if (from == NO_COMPONENT || to == NO_COMPONENT)
  {
    _job.path.state = PathState::Refused;
    return;
  }

  const std::uint32_t components = m_graph->ComponentCount(_job.drive);
  _job.gScore.assign(components, UNREACHED);
  _job.cameFrom.assign(components, UNREACHED);
  _job.openNodes.clear();
  _job.openCost.clear();
  _job.gScore[from] = 0;
  _job.openNodes.push_back(from);
  _job.openCost.push_back(Octile(_job.fromX, _job.fromY, _job.toX, _job.toY));
  _job.searching = true;
  _job.path.state = PathState::Planning;
}

std::uint32_t PathPlanner::Step(Job& _job, std::uint32_t _budget)
{
  const std::uint32_t goal = m_graph->ComponentAt(_job.toX, _job.toY, _job.drive);
  std::uint32_t expanded = 0;

  while (expanded < _budget && !_job.openNodes.empty())
  {
    // The cheapest open entry, and the lowest component of the ties. Written out rather than left
    // to a priority queue so that the tie-break is stated: two hosts must pop the same entry.
    std::size_t best = 0;
    for (std::size_t index = 1; index < _job.openNodes.size(); ++index)
    {
      if (_job.openCost[index] < _job.openCost[best] ||
          (_job.openCost[index] == _job.openCost[best] && _job.openNodes[index] < _job.openNodes[best]))
      {
        best = index;
      }
    }
    const std::uint32_t current = _job.openNodes[best];
    _job.openNodes.erase(_job.openNodes.begin() + static_cast<std::ptrdiff_t>(best));
    _job.openCost.erase(_job.openCost.begin() + static_cast<std::ptrdiff_t>(best));
    ++expanded;

    if (current == goal)
    {
      Finish(_job, goal);
      return expanded;
    }

    for (const ComponentLink& link : m_graph->LinksFrom(current, _job.drive))
    {
      // Crossing costs what it costs to walk from this component's representative to the crossing
      // and one step over. An estimate, as the header says: a component is a region and has no one
      // length. It is what makes the abstract route approximate and the refined cells exact.
      std::uint32_t nextX = 0;
      std::uint32_t nextY = 0;
      if (!m_graph->RepresentativeCell(link.to, _job.drive, nextX, nextY))
      {
        continue;
      }
      std::uint32_t currentX = 0;
      std::uint32_t currentY = 0;
      if (!m_graph->RepresentativeCell(current, _job.drive, currentX, currentY))
      {
        continue;
      }
      const std::uint64_t candidate = static_cast<std::uint64_t>(_job.gScore[current]) + Octile(currentX, currentY, nextX, nextY);
      if (candidate >= _job.gScore[link.to])
      {
        continue;
      }
      _job.gScore[link.to] = static_cast<std::uint32_t>(candidate);
      _job.cameFrom[link.to] = current;
      _job.openNodes.push_back(link.to);
      _job.openCost.push_back(static_cast<std::uint32_t>(candidate) + Octile(nextX, nextY, _job.toX, _job.toY));
    }
  }

  if (_job.openNodes.empty())
  {
    // Nothing left to expand and the goal was never reached: no route exists for this class, which
    // is exactly what a flood fill would say, because a component IS what a flood fill reaches.
    _job.searching = false;
    _job.path.state = PathState::Unreachable;
  }
  return expanded;
}

void PathPlanner::Finish(Job& _job, std::uint32_t _reachedEnd)
{
  std::vector<std::uint32_t> route;
  for (std::uint32_t at = _reachedEnd; at != UNREACHED; at = _job.cameFrom[at])
  {
    route.push_back(at);
  }
  std::reverse(route.begin(), route.end());
  // The first component is the one the device already stands in, so it is not a leg to walk.
  if (!route.empty())
  {
    route.erase(route.begin());
  }
  _job.path.nodes = std::move(route);
  _job.path.state = PathState::Partial;
  _job.searching = false;
  _job.atX = _job.fromX;
  _job.atY = _job.fromY;
  Refine(_job, REFINE_CLUSTERS);
}

void PathPlanner::Refine(Job& _job, std::uint32_t _clusters)
{
  if (m_graph == nullptr)
  {
    return;
  }
  const std::uint32_t side = m_graph->ClustersPerSide() * CLUSTER_CELLS;
  std::vector<std::uint32_t> cells;
  std::uint32_t cost = 0;

  std::uint32_t done = 0;
  while (done < _clusters && !_job.path.nodes.empty())
  {
    const std::uint32_t here = m_graph->ComponentAt(_job.atX, _job.atY, _job.drive);
    const std::uint32_t next = _job.path.nodes.front();
    // The crossing out of where we stand and into the next component of the route.
    const std::span<const ComponentLink> links = m_graph->LinksFrom(here, _job.drive);
    const auto link = std::find_if(links.begin(), links.end(), [next](const ComponentLink& _link) { return _link.to == next; });
    if (link == links.end())
    {
      // The route said these two were joined and the graph now says otherwise, which means it was
      // rebuilt under the request. Stop refining; the caller asks again.
      _job.path.state = _job.path.cells.empty() ? PathState::Unreachable : PathState::Partial;
      return;
    }
    if (!m_graph->LocalPath(_job.atX, _job.atY, link->fromCellX, link->fromCellY, _job.drive, m_graph->ClusterOf(_job.atX, _job.atY), cells,
                            cost))
    {
      _job.path.state = _job.path.cells.empty() ? PathState::Unreachable : PathState::Partial;
      return;
    }
    for (const std::uint32_t cell : cells)
    {
      _job.path.cells.push_back({cell % side, cell / side});
    }
    // And the step across the border, which is one cell because a crossing is two adjacent cells.
    _job.path.cells.push_back({link->toCellX, link->toCellY});
    _job.atX = link->toCellX;
    _job.atY = link->toCellY;
    _job.path.nodes.erase(_job.path.nodes.begin());
    ++done;
  }

  if (!_job.path.nodes.empty())
  {
    _job.path.state = PathState::Partial;
    return;
  }
  // Every leg is spent: the last one is from where we stand to the destination itself, which is in
  // the same component and therefore the same cluster.
  if (m_graph->LocalPath(_job.atX, _job.atY, _job.toX, _job.toY, _job.drive, m_graph->ClusterOf(_job.atX, _job.atY), cells, cost))
  {
    for (const std::uint32_t cell : cells)
    {
      _job.path.cells.push_back({cell % side, cell / side});
    }
    _job.path.state = PathState::Complete;
    return;
  }
  _job.path.state = _job.path.cells.empty() ? PathState::Unreachable : PathState::Partial;
}

bool PathPlanner::RefineFurther(ObjectId _device)
{
  Job* job = Find(_device);
  if (job == nullptr || job->path.state != PathState::Partial)
  {
    return false;
  }
  Refine(*job, REFINE_CLUSTERS);
  return true;
}

void PathPlanner::Advance(std::uint32_t _budgetNodes)
{
  m_lastNodesExpanded = 0;
  if (m_graph == nullptr || !m_graph->Built())
  {
    return;
  }
  std::uint32_t left = _budgetNodes;
  // Oldest first, so a request that has been waiting is not starved by a newer one, and so that
  // the order two hosts serve requests in is the order those requests arrived.
  for (Job& job : m_requests)
  {
    if (left == 0)
    {
      break;
    }
    if (!job.searching || job.path.state != PathState::Planning)
    {
      continue;
    }
    const std::uint32_t used = Step(job, left);
    job.path.nodesExpanded += used;
    left -= std::min(left, used);
    m_lastNodesExpanded += used;
  }
  m_totalNodesExpanded += m_lastNodesExpanded;
}

} // namespace Frontier
