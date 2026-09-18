#include "pch.h"

#include "ClusterGraph.h"

#include <algorithm>
#include <queue>

namespace Frontier
{

namespace
{

constexpr std::uint32_t UNREACHED = 0xFFFFFFFFu;

/// The eight neighbours, in a fixed order. The order is every search's tie-break, so it is written
/// once here rather than rebuilt at each site: two hosts expanding in different orders would find
/// different paths of equal cost (AGENTS.md R16).
struct Step
{
  std::int32_t dx;
  std::int32_t dy;
  std::uint32_t cost;
};

constexpr std::array<Step, 8> STEPS = {{{0, -1, STEP_COST},
                                        {-1, 0, STEP_COST},
                                        {1, 0, STEP_COST},
                                        {0, 1, STEP_COST},
                                        {-1, -1, DIAGONAL_COST},
                                        {1, -1, DIAGONAL_COST},
                                        {-1, 1, DIAGONAL_COST},
                                        {1, 1, DIAGONAL_COST}}};

} // namespace

void ClusterGraph::Clear() noexcept
{
  m_landscape = nullptr;
  m_clustersPerSide = 0;
  m_rules = {};
  for (ClassGraph& graph : m_classes)
  {
    graph = {};
  }
  m_localSearches = 0;
  m_graphBuilds = 0;
}

void ClusterGraph::Invalidate() noexcept
{
  for (ClassGraph& graph : m_classes)
  {
    graph = {};
  }
}

void ClusterGraph::Build(const Landscape& _landscape, const ContentTree& _content)
{
  Clear();
  if (!_landscape.Created())
  {
    return;
  }
  m_landscape = &_landscape;
  // A landscape's side is a power of two of cells and a cluster is 16, so the division is exact at
  // every size class (128, 256, 512, 1024). Rounded up anyway, so a mod's odd size does not lose
  // its last row of cells.
  m_clustersPerSide = (_landscape.CellsPerSide() + CLUSTER_CELLS - 1) / CLUSTER_CELLS;

  // The LEAST capable row of each class, so that a route this graph finds is one every drive of
  // the class can take. Taking the most capable instead would hand a Wheels I device a path only a
  // Wheels II can walk. A class no row defines keeps -1 and is impassable everywhere.
  for (const DriveDesc& row : _content.components.drives)
  {
    const std::size_t index = static_cast<std::size_t>(row.driveClass);
    if (index >= DRIVE_CLASS_COUNT)
    {
      continue;
    }
    Rules& rules = m_rules[index];
    if (rules.maxSlopePercent < 0)
    {
      rules = {row.maxSlopePercent, row.crossesWater};
      continue;
    }
    rules.maxSlopePercent = std::min(rules.maxSlopePercent, row.maxSlopePercent);
    rules.crossesWater = rules.crossesWater && row.crossesWater;
  }
}

bool ClusterGraph::Passable(std::uint32_t _cellX, std::uint32_t _cellY, DriveClass _drive) const noexcept
{
  if (m_landscape == nullptr || _cellX >= m_landscape->CellsPerSide() || _cellY >= m_landscape->CellsPerSide())
  {
    return false;
  }
  const std::size_t index = static_cast<std::size_t>(_drive);
  if (index >= DRIVE_CLASS_COUNT || m_rules[index].maxSlopePercent < 0)
  {
    return false;
  }
  const Landscape::Cell& cell = m_landscape->CellAt(_cellX, _cellY);
  // The obstruction byte stops every class: a structure is not a slope one drive climbs better.
  if (cell.obstruction != 0)
  {
    return false;
  }
  if ((cell.flags & Landscape::CELL_WATER) != 0 && !m_rules[index].crossesWater)
  {
    return false;
  }
  return static_cast<std::int32_t>(cell.slopePercent) <= m_rules[index].maxSlopePercent;
}

void ClusterGraph::BuildClass(DriveClass _drive) const
{
  const std::size_t index = static_cast<std::size_t>(_drive);
  if (index >= DRIVE_CLASS_COUNT)
  {
    return;
  }
  ClassGraph& graph = m_classes[index];
  if (graph.built || m_landscape == nullptr)
  {
    return;
  }
  graph.built = true;
  ++m_graphBuilds;
  const std::uint32_t side = m_landscape->CellsPerSide();
  graph.componentOfCell.assign(static_cast<std::size_t>(side) * side, NO_COMPONENT);

  // Cluster by cluster, in index order, and inside a cluster cell by cell in row-major order, so
  // that the numbering depends on the landscape and on nothing else.
  for (std::uint32_t cluster = 0; cluster < m_clustersPerSide * m_clustersPerSide; ++cluster)
  {
    const std::uint32_t originX = (cluster % m_clustersPerSide) * CLUSTER_CELLS;
    const std::uint32_t originY = (cluster / m_clustersPerSide) * CLUSTER_CELLS;
    const std::uint32_t spanX = std::min(CLUSTER_CELLS, side - originX);
    const std::uint32_t spanY = std::min(CLUSTER_CELLS, side - originY);
    for (std::uint32_t y = originY; y < originY + spanY; ++y)
    {
      for (std::uint32_t x = originX; x < originX + spanX; ++x)
      {
        const std::size_t cell = static_cast<std::size_t>(y) * side + x;
        if (!Passable(x, y, _drive) || graph.componentOfCell[cell] != NO_COMPONENT)
        {
          continue;
        }
        // A new component, and a flood fill of it that never leaves the cluster.
        const auto component = static_cast<std::uint32_t>(graph.cluster.size());
        graph.cluster.push_back(cluster);
        graph.representative.push_back(static_cast<std::uint32_t>(cell));
        graph.links.emplace_back();
        std::vector<std::uint32_t> open{static_cast<std::uint32_t>(cell)};
        graph.componentOfCell[cell] = component;
        while (!open.empty())
        {
          const std::uint32_t at = open.back();
          open.pop_back();
          const std::uint32_t atX = at % side;
          const std::uint32_t atY = at / side;
          for (const Step& step : STEPS)
          {
            const std::int64_t nx = static_cast<std::int64_t>(atX) + step.dx;
            const std::int64_t ny = static_cast<std::int64_t>(atY) + step.dy;
            const std::int64_t limitX = static_cast<std::int64_t>(originX) + spanX;
            const std::int64_t limitY = static_cast<std::int64_t>(originY) + spanY;
            if (nx < originX || ny < originY || nx >= limitX || ny >= limitY)
            {
              continue;
            }
            const auto neighborX = static_cast<std::uint32_t>(nx);
            const auto neighborY = static_cast<std::uint32_t>(ny);
            if (!Passable(neighborX, neighborY, _drive))
            {
              continue;
            }
            // A diagonal is taken only when both of its orthogonal neighbours are open, so nothing
            // squeezes between two corners. The same rule the cell search uses, and the same one
            // the flood fill the tests compare against uses.
            if (step.dx != 0 && step.dy != 0 &&
                (!Passable(static_cast<std::uint32_t>(static_cast<std::int64_t>(atX) + step.dx), atY, _drive) ||
                 !Passable(atX, static_cast<std::uint32_t>(static_cast<std::int64_t>(atY) + step.dy), _drive)))
            {
              continue;
            }
            const std::size_t next = static_cast<std::size_t>(neighborY) * side + neighborX;
            if (graph.componentOfCell[next] == NO_COMPONENT)
            {
              graph.componentOfCell[next] = component;
              open.push_back(static_cast<std::uint32_t>(next));
            }
          }
        }
      }
    }
  }

  // The links. Every pair of neighbouring cells that straddles a cluster border and is passable on
  // both sides joins the two components it touches, and the first such pair found is the crossing
  // the refinement will use. Only the four orthogonal directions: a diagonal across a cluster
  // corner is a move the corner rule may forbid, and there is always an orthogonal way round.
  const auto join = [&](std::uint32_t _x, std::uint32_t _y, std::uint32_t _otherX, std::uint32_t _otherY)
  {
    const std::uint32_t here = graph.componentOfCell[static_cast<std::size_t>(_y) * side + _x];
    const std::uint32_t there = graph.componentOfCell[static_cast<std::size_t>(_otherY) * side + _otherX];
    if (here == NO_COMPONENT || there == NO_COMPONENT || here == there)
    {
      return;
    }
    const auto known = [](const std::vector<ComponentLink>& _links, std::uint32_t _to)
    { return std::any_of(_links.begin(), _links.end(), [_to](const ComponentLink& _link) { return _link.to == _to; }); };
    if (!known(graph.links[here], there))
    {
      graph.links[here].push_back({there, _x, _y, _otherX, _otherY});
    }
    if (!known(graph.links[there], here))
    {
      graph.links[there].push_back({here, _otherX, _otherY, _x, _y});
    }
  };
  for (std::uint32_t y = 0; y < side; ++y)
  {
    for (std::uint32_t x = 0; x < side; ++x)
    {
      if (x + 1 < side && ClusterOf(x, y) != ClusterOf(x + 1, y))
      {
        join(x, y, x + 1, y);
      }
      if (y + 1 < side && ClusterOf(x, y) != ClusterOf(x, y + 1))
      {
        join(x, y, x, y + 1);
      }
    }
  }
}

std::uint32_t ClusterGraph::ComponentAt(std::uint32_t _cellX, std::uint32_t _cellY, DriveClass _drive) const
{
  const std::size_t index = static_cast<std::size_t>(_drive);
  if (m_landscape == nullptr || index >= DRIVE_CLASS_COUNT || _cellX >= m_landscape->CellsPerSide() ||
      _cellY >= m_landscape->CellsPerSide())
  {
    return NO_COMPONENT;
  }
  BuildClass(_drive);
  return m_classes[index].componentOfCell[static_cast<std::size_t>(_cellY) * m_landscape->CellsPerSide() + _cellX];
}

std::span<const ComponentLink> ClusterGraph::LinksFrom(std::uint32_t _component, DriveClass _drive) const
{
  const std::size_t index = static_cast<std::size_t>(_drive);
  if (index >= DRIVE_CLASS_COUNT)
  {
    return {};
  }
  BuildClass(_drive);
  if (_component >= m_classes[index].links.size())
  {
    return {};
  }
  return m_classes[index].links[_component];
}

std::uint32_t ClusterGraph::ClusterOfComponent(std::uint32_t _component, DriveClass _drive) const
{
  const std::size_t index = static_cast<std::size_t>(_drive);
  if (index >= DRIVE_CLASS_COUNT)
  {
    return NO_COMPONENT;
  }
  BuildClass(_drive);
  return _component < m_classes[index].cluster.size() ? m_classes[index].cluster[_component] : NO_COMPONENT;
}

bool ClusterGraph::RepresentativeCell(std::uint32_t _component, DriveClass _drive, std::uint32_t& _outX, std::uint32_t& _outY) const
{
  const std::size_t index = static_cast<std::size_t>(_drive);
  if (m_landscape == nullptr || index >= DRIVE_CLASS_COUNT)
  {
    return false;
  }
  BuildClass(_drive);
  if (_component >= m_classes[index].representative.size())
  {
    return false;
  }
  const std::uint32_t cell = m_classes[index].representative[_component];
  _outX = cell % m_landscape->CellsPerSide();
  _outY = cell / m_landscape->CellsPerSide();
  return true;
}

std::uint32_t ClusterGraph::ComponentCount(DriveClass _drive) const
{
  const std::size_t index = static_cast<std::size_t>(_drive);
  if (index >= DRIVE_CLASS_COUNT)
  {
    return 0;
  }
  BuildClass(_drive);
  return static_cast<std::uint32_t>(m_classes[index].cluster.size());
}

bool ClusterGraph::LocalPath(std::uint32_t _fromX, std::uint32_t _fromY, std::uint32_t _toX, std::uint32_t _toY, DriveClass _drive,
                             std::uint32_t _cluster, std::vector<std::uint32_t>& _outCells, std::uint32_t& _outCost) const
{
  _outCells.clear();
  _outCost = 0;
  if (m_landscape == nullptr || !Passable(_fromX, _fromY, _drive) || !Passable(_toX, _toY, _drive))
  {
    return false;
  }
  const std::uint32_t side = m_landscape->CellsPerSide();
  const std::uint32_t originX = (_cluster % m_clustersPerSide) * CLUSTER_CELLS;
  const std::uint32_t originY = (_cluster / m_clustersPerSide) * CLUSTER_CELLS;
  const std::uint32_t spanX = std::min(CLUSTER_CELLS, side - originX);
  const std::uint32_t spanY = std::min(CLUSTER_CELLS, side - originY);
  const auto inside = [&](std::uint32_t _x, std::uint32_t _y)
  { return _x >= originX && _y >= originY && _x < originX + spanX && _y < originY + spanY; };
  if (!inside(_fromX, _fromY) || !inside(_toX, _toY))
  {
    return false;
  }
  if (_fromX == _toX && _fromY == _toY)
  {
    return true;
  }
  ++m_localSearches;

  // Dijkstra over at most 256 cells, which is small enough that a flat array beats any structure
  // and the whole thing fits in cache. A* would want a heuristic; over 256 cells it would not pay.
  constexpr std::size_t CLUSTER_SLOTS = static_cast<std::size_t>(CLUSTER_CELLS) * CLUSTER_CELLS;
  std::array<std::uint32_t, CLUSTER_SLOTS> cost{};
  std::array<std::uint32_t, CLUSTER_SLOTS> from{};
  cost.fill(UNREACHED);
  from.fill(UNREACHED);
  const auto slot = [&](std::uint32_t _x, std::uint32_t _y) { return (_y - originY) * CLUSTER_CELLS + (_x - originX); };

  // The queue orders by cost then by slot, so that two equal-cost frontiers pop in one order.
  using Entry = std::pair<std::uint32_t, std::uint32_t>; // cost, slot
  std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
  const std::uint32_t source = slot(_fromX, _fromY);
  const std::uint32_t target = slot(_toX, _toY);
  cost[source] = 0;
  open.emplace(0u, source);
  while (!open.empty())
  {
    const Entry entry = open.top();
    open.pop();
    if (entry.first > cost[entry.second])
    {
      continue; // A stale copy left behind by a cheaper route to the same cell
    }
    if (entry.second == target)
    {
      break;
    }
    const std::uint32_t x = originX + entry.second % CLUSTER_CELLS;
    const std::uint32_t y = originY + entry.second / CLUSTER_CELLS;
    for (const Step& step : STEPS)
    {
      const std::int64_t nx = static_cast<std::int64_t>(x) + step.dx;
      const std::int64_t ny = static_cast<std::int64_t>(y) + step.dy;
      if (nx < 0 || ny < 0 || !inside(static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny)))
      {
        continue;
      }
      const auto neighborX = static_cast<std::uint32_t>(nx);
      const auto neighborY = static_cast<std::uint32_t>(ny);
      if (!Passable(neighborX, neighborY, _drive))
      {
        continue;
      }
      if (step.dx != 0 && step.dy != 0 &&
          (!Passable(static_cast<std::uint32_t>(static_cast<std::int64_t>(x) + step.dx), y, _drive) ||
           !Passable(x, static_cast<std::uint32_t>(static_cast<std::int64_t>(y) + step.dy), _drive)))
      {
        continue;
      }
      const std::uint32_t next = slot(neighborX, neighborY);
      const std::uint32_t candidate = entry.first + step.cost;
      if (candidate < cost[next])
      {
        cost[next] = candidate;
        from[next] = entry.second;
        open.emplace(candidate, next);
      }
    }
  }
  if (cost[target] == UNREACHED)
  {
    return false;
  }
  _outCost = cost[target];
  for (std::uint32_t at = target; at != source; at = from[at])
  {
    _outCells.push_back((originY + at / CLUSTER_CELLS) * side + (originX + at % CLUSTER_CELLS));
  }
  std::reverse(_outCells.begin(), _outCells.end());
  return true;
}

} // namespace Frontier
