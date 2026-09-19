#include "pch.h"

#include "Visibility.h"

#include "FixedPoint.h"
#include "Point.h"

#include <algorithm>

namespace Frontier
{

namespace
{

/// The cell a subunit position falls in, clamped onto the landscape.
[[nodiscard]] std::uint32_t CellOf(std::int32_t _subunits, std::uint32_t _cellsPerSide) noexcept
{
  if (_subunits < 0)
  {
    return 0;
  }
  const std::int64_t cell = static_cast<std::int64_t>(_subunits) >> Neuron::SUBUNITS_PER_CELL_SHIFT;
  return static_cast<std::uint32_t>(std::min<std::int64_t>(cell, _cellsPerSide == 0 ? 0 : _cellsPerSide - 1));
}

/// Subunits as whole cells, rounded down: a sight radius as the grid counts it.
[[nodiscard]] std::uint32_t CellsOf(std::int32_t _subunits) noexcept
{
  return _subunits <= 0 ? 0u : static_cast<std::uint32_t>(_subunits >> Neuron::SUBUNITS_PER_CELL_SHIFT);
}

[[nodiscard]] const StructureDesc* StructureRow(const ContentTree& _content, std::uint32_t _design) noexcept
{
  const std::vector<StructureDesc>& rows = _content.structures.structures;
  return _design < rows.size() ? &rows[_design] : nullptr;
}

/// A device's sight, in subunits: the chassis's, or the sensor module's if larger
/// (GameDesign.md §6). Derived here rather than through DeriveDesignStats because that wants a
/// DeviceDesign of ids and this has one of row indices.
[[nodiscard]] std::int32_t DeviceSightSubunits(const ContentTree& _content, const Seat& _seat, const Device& _device) noexcept
{
  if (_device.design >= _seat.designs.size())
  {
    return 0;
  }
  const DeviceDesign& design = _seat.designs[_device.design];
  const std::vector<ChassisDesc>& chassis = _content.components.chassis;
  std::int32_t sight = design.chassis < chassis.size() ? chassis[design.chassis].sightSubunits : 0;
  const std::vector<ModuleDesc>& modules = _content.components.modules;
  for (std::uint8_t mount = 0; mount < design.moduleCount && mount < MAX_MOUNTS; ++mount)
  {
    const std::uint32_t row = design.modules[mount];
    if (row < modules.size() && modules[row].systemKind == SystemKind::Sensor)
    {
      sight = std::max(sight, modules[row].sightSubunits);
    }
  }
  return sight;
}

} // namespace

std::int32_t Visibility::CellHeight(const Landscape& _landscape, std::uint32_t _cellX, std::uint32_t _cellY) noexcept
{
  // The tallest of the cell's samples, so a ridge that runs between two cell centres blocks rather
  // than being seen straight through. Landscape derives it when it derives the cell: scanning the
  // 25 samples here instead cost 140 ms for one refresh against a 50 ms tick, because a refresh
  // asks this about twenty thousand cells per viewer.
  if (_cellX >= _landscape.CellsPerSide() || _cellY >= _landscape.CellsPerSide())
  {
    return OUTSIDE_HEIGHT;
  }
  return _landscape.CellAt(_cellX, _cellY).highestSample;
}

bool Visibility::LineOfSight(const Landscape& _landscape, std::uint32_t _fromX, std::uint32_t _fromY, std::uint32_t _toX,
                             std::uint32_t _toY, std::uint64_t& _heightReads)
{
  if (_fromX == _toX && _fromY == _toY)
  {
    return true;
  }
  const std::int32_t fromHeight = CellHeight(_landscape, _fromX, _fromY);
  const std::int32_t toHeight = CellHeight(_landscape, _toX, _toY);
  _heightReads += 2;

  // Bresenham over cells, with the line of sight evaluated exactly at each step: at step i of n,
  // the sight line stands at fromHeight + (toHeight - fromHeight) * i / n. Multiplied out rather
  // than divided, so there is no rounding for two hosts to disagree about (AGENTS.md R16).
  const std::int64_t dx = static_cast<std::int64_t>(_toX) - _fromX;
  const std::int64_t dy = static_cast<std::int64_t>(_toY) - _fromY;
  const std::int64_t stepX = dx > 0 ? 1 : -1;
  const std::int64_t stepY = dy > 0 ? 1 : -1;
  const std::int64_t spanX = dx > 0 ? dx : -dx;
  const std::int64_t spanY = dy > 0 ? dy : -dy;
  const std::int64_t steps = std::max(spanX, spanY);

  std::int64_t x = _fromX;
  std::int64_t y = _fromY;
  std::int64_t errorX = spanX;
  std::int64_t errorY = spanY;
  for (std::int64_t step = 1; step < steps; ++step)
  {
    // One axis advances every step; the other advances when its error passes the major span.
    if (spanX >= spanY)
    {
      x += stepX;
      errorY += spanY;
      if (errorY >= spanX)
      {
        errorY -= spanX;
        y += stepY;
      }
    }
    else
    {
      y += stepY;
      errorX += spanX;
      if (errorX >= spanY)
      {
        errorX -= spanY;
        x += stepX;
      }
    }
    const std::int32_t ground = CellHeight(_landscape, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    ++_heightReads;
    // ground > fromHeight + (toHeight - fromHeight) * step / steps, without the division.
    const std::int64_t groundAlongLine = static_cast<std::int64_t>(ground - fromHeight) * steps;
    const std::int64_t lineAlongLine = static_cast<std::int64_t>(toHeight - fromHeight) * step;
    if (groundAlongLine > lineAlongLine)
    {
      return false;
    }
  }
  return true;
}

void Visibility::Reset(std::span<Seat> _seats, const Landscape& _landscape)
{
  m_stamps.clear();
  const std::uint32_t side = _landscape.Created() ? _landscape.CellsPerSide() : 0;
  for (Seat& seat : _seats)
  {
    seat.fog.Resize(side);
  }
}

void Visibility::InvalidateRegion(std::span<Seat> _seats, const Landscape& _landscape, std::uint32_t _cellX0, std::uint32_t _cellY0,
                                  std::uint32_t _cellX1, std::uint32_t _cellY1)
{
  if (!_landscape.Created() || m_stamps.empty() || _cellX1 < _cellX0 || _cellY1 < _cellY0)
  {
    return;
  }
  std::vector<ViewerStamp> kept;
  kept.reserve(m_stamps.size());
  for (const ViewerStamp& stamp : m_stamps)
  {
    // The disc as it was counted, clamped at nothing: a radius bigger than the cell index simply
    // reaches the edge. A disc that does not reach the rectangle cannot have had any of its cells'
    // visibility changed by what happens inside it, so its stamp is still exact.
    const std::uint32_t low = stamp.cellX > stamp.radiusCells ? stamp.cellX - stamp.radiusCells : 0;
    const std::uint32_t high = stamp.cellX + stamp.radiusCells;
    const std::uint32_t bottom = stamp.cellY > stamp.radiusCells ? stamp.cellY - stamp.radiusCells : 0;
    const std::uint32_t top = stamp.cellY + stamp.radiusCells;
    if (high < _cellX0 || low > _cellX1 || top < _cellY0 || bottom > _cellY1)
    {
      kept.push_back(stamp);
      continue;
    }
    UnstampDisc(_seats, _landscape, stamp);
  }
  m_stamps = std::move(kept);
}

bool Visibility::Restore(std::vector<ViewerStamp> _stamps)
{
  for (std::size_t index = 1; index < _stamps.size(); ++index)
  {
    if (_stamps[index - 1].viewer.value >= _stamps[index].viewer.value)
    {
      return false;
    }
  }
  m_stamps = std::move(_stamps);
  return true;
}

ViewerStamp* Visibility::FindStamp(ObjectId _viewer) noexcept
{
  const auto at = std::lower_bound(m_stamps.begin(), m_stamps.end(), _viewer.value,
                                   [](const ViewerStamp& _stamp, std::uint32_t _id) { return _stamp.viewer.value < _id; });
  if (at == m_stamps.end() || at->viewer != _viewer)
  {
    return nullptr;
  }
  return &*at;
}

void Visibility::CollectViewers(const World& _world, std::span<const Seat> _seats, const Landscape& _landscape, const ContentTree& _content)
{
  m_viewers.clear();
  const std::uint32_t side = _landscape.CellsPerSide();
  _world.ForEachDevice(
    [this, &_seats, &_content, side](ObjectId _id, const Device& _device)
    {
      if (_device.seat >= _seats.size())
      {
        return;
      }
      const std::int32_t sight = DeviceSightSubunits(_content, _seats[_device.seat], _device);
      if (sight <= 0)
      {
        return;
      }
      m_viewers.push_back({_id, _device.seat, CellOf(_device.x, side), CellOf(_device.z, side), CellsOf(sight)});
    });
  _world.ForEachStructure(
    [this, &_seats, &_content, side](ObjectId _id, const Structure& _structure)
    {
      // A structure sees only once it stands: a plan and a building site are not eyes.
      if (_structure.seat >= _seats.size() || _structure.state != StructurePhase::Standing)
      {
        return;
      }
      const StructureDesc* row = StructureRow(_content, _structure.design);
      if (row == nullptr || row->sightSubunits <= 0)
      {
        return;
      }
      m_viewers.push_back({_id, _structure.seat, std::min(_structure.cellX, side == 0 ? 0 : side - 1),
                           std::min(_structure.cellY, side == 0 ? 0 : side - 1), CellsOf(row->sightSubunits)});
    });
  // Devices walk in ascending id and structures likewise, but an id is unique across kinds
  // (Sim/ObjectId.h), so the two runs interleave and the whole has to be put back in id order for
  // the stamp lookups and for the tie-break below to mean anything.
  std::sort(m_viewers.begin(), m_viewers.end(), [](const Viewer& _left, const Viewer& _right) { return _left.id.value < _right.id.value; });
}

void Visibility::StampDisc(std::span<Seat> _seats, const Landscape& _landscape, const Viewer& _viewer, std::uint32_t _tick)
{
  const std::uint32_t side = _landscape.CellsPerSide();
  const std::int32_t viewerHeight = CellHeight(_landscape, _viewer.cellX, _viewer.cellY);
  const std::uint32_t scan = _viewer.radiusCells * MAX_HEIGHT_SIGHT_BONUS_FACTOR;
  const std::uint32_t minX = _viewer.cellX > scan ? _viewer.cellX - scan : 0;
  const std::uint32_t minY = _viewer.cellY > scan ? _viewer.cellY - scan : 0;
  const std::uint32_t maxX = std::min(side == 0 ? 0 : side - 1, _viewer.cellX + scan);
  const std::uint32_t maxY = std::min(side == 0 ? 0 : side - 1, _viewer.cellY + scan);
  const std::uint8_t alliance = _seats[_viewer.seat].alliance;

  for (std::uint32_t cellY = minY; cellY <= maxY; ++cellY)
  {
    for (std::uint32_t cellX = minX; cellX <= maxX; ++cellX)
    {
      const std::int64_t dx = static_cast<std::int64_t>(cellX) - _viewer.cellX;
      const std::int64_t dy = static_cast<std::int64_t>(cellY) - _viewer.cellY;
      const std::int64_t distanceSquared = dx * dx + dy * dy;
      // Height extends sight, and by how much depends on the cell being looked at, so the radius
      // is computed per cell rather than once: a viewer on a hill sees further into the valley
      // than across the hill (GameDesign.md §8).
      const std::int32_t targetHeight = CellHeight(_landscape, cellX, cellY);
      ++m_lastHeightReads;
      const std::int32_t above = viewerHeight - targetHeight;
      const std::uint32_t bonus = above <= 0 ? 0u
                                             : std::min(_viewer.radiusCells * (MAX_HEIGHT_SIGHT_BONUS_FACTOR - 1),
                                                        static_cast<std::uint32_t>(above / WORLD_UNITS_PER_SIGHT_CELL));
      const std::int64_t radius = static_cast<std::int64_t>(_viewer.radiusCells) + bonus;
      if (distanceSquared > radius * radius)
      {
        continue;
      }
      if (!LineOfSight(_landscape, _viewer.cellX, _viewer.cellY, cellX, cellY, m_lastHeightReads))
      {
        continue;
      }
      for (Seat& seat : _seats)
      {
        if (seat.alliance == alliance && !seat.fog.Empty())
        {
          seat.fog.AddViewer(cellX, cellY);
        }
      }
    }
  }

  const ViewerStamp stamp{_viewer.id, _viewer.seat, _viewer.cellX, _viewer.cellY, _viewer.radiusCells, _tick};
  const auto at = std::lower_bound(m_stamps.begin(), m_stamps.end(), _viewer.id.value,
                                   [](const ViewerStamp& _left, std::uint32_t _id) { return _left.viewer.value < _id; });
  if (at != m_stamps.end() && at->viewer == _viewer.id)
  {
    *at = stamp;
  }
  else
  {
    m_stamps.insert(at, stamp);
  }
}

void Visibility::UnstampDisc(std::span<Seat> _seats, const Landscape& _landscape, const ViewerStamp& _stamp)
{
  // The same walk as StampDisc, with the same radius and the same line of sight, so that exactly
  // the cells that were counted are un-counted. Any difference between the two - a changed
  // radius, a changed heightfield - would leave cells lit that nobody can see, which is why the
  // radius is on the stamp and why a height delta drops every stamp (Sim::FlattenTerrain).
  const std::uint32_t side = _landscape.CellsPerSide();
  const std::int32_t viewerHeight = CellHeight(_landscape, _stamp.cellX, _stamp.cellY);
  const std::uint32_t scan = _stamp.radiusCells * MAX_HEIGHT_SIGHT_BONUS_FACTOR;
  const std::uint32_t minX = _stamp.cellX > scan ? _stamp.cellX - scan : 0;
  const std::uint32_t minY = _stamp.cellY > scan ? _stamp.cellY - scan : 0;
  const std::uint32_t maxX = std::min(side == 0 ? 0 : side - 1, _stamp.cellX + scan);
  const std::uint32_t maxY = std::min(side == 0 ? 0 : side - 1, _stamp.cellY + scan);
  // The alliance the disc went on to, which is the stamp's seat's and not necessarily the seat's
  // the loop is standing in: taking it off every seat would un-count an enemy's grid.
  const std::uint8_t alliance = _stamp.seat < _seats.size() ? _seats[_stamp.seat].alliance : 0xFFu;

  for (std::uint32_t cellY = minY; cellY <= maxY; ++cellY)
  {
    for (std::uint32_t cellX = minX; cellX <= maxX; ++cellX)
    {
      const std::int64_t dx = static_cast<std::int64_t>(cellX) - _stamp.cellX;
      const std::int64_t dy = static_cast<std::int64_t>(cellY) - _stamp.cellY;
      const std::int32_t targetHeight = CellHeight(_landscape, cellX, cellY);
      ++m_lastHeightReads;
      const std::int32_t above = viewerHeight - targetHeight;
      const std::uint32_t bonus = above <= 0 ? 0u
                                             : std::min(_stamp.radiusCells * (MAX_HEIGHT_SIGHT_BONUS_FACTOR - 1),
                                                        static_cast<std::uint32_t>(above / WORLD_UNITS_PER_SIGHT_CELL));
      const std::int64_t radius = static_cast<std::int64_t>(_stamp.radiusCells) + bonus;
      if (dx * dx + dy * dy > radius * radius)
      {
        continue;
      }
      if (!LineOfSight(_landscape, _stamp.cellX, _stamp.cellY, cellX, cellY, m_lastHeightReads))
      {
        continue;
      }
      for (Seat& seat : _seats)
      {
        if (seat.alliance == alliance && !seat.fog.Empty())
        {
          seat.fog.RemoveViewer(cellX, cellY);
        }
      }
    }
  }
}

void Visibility::RefreshGhosts(const World& _world, std::span<Seat> _seats, const ContentTree& _content, std::uint32_t _tick)
{
  // A ghost is written while the structure is visible, so that the record a commander keeps after
  // it leaves sight is what was there at the moment it was last seen. Recording only on the tick
  // it leaves would need a "was visible last tick" bit per seat per structure, which is more state
  // than the record it would save.
  _world.ForEachStructure(
    [&_seats, &_content, _tick](ObjectId _id, const Structure& _structure)
    {
      if (StructureRow(_content, _structure.design) == nullptr || _structure.state == StructurePhase::Plan)
      {
        return;
      }
      for (Seat& seat : _seats)
      {
        if (seat.fog.Empty() || !seat.fog.Visible(_structure.cellX, _structure.cellY))
        {
          continue;
        }
        seat.ghosts.Record({_id, _structure.seat, _structure.design, _structure.cellX, _structure.cellY, _tick});
      }
    });

  // A ghost of something that is no longer there is cleared the moment the commander looks at the
  // cell and finds it empty. Without this a demolished base would be drawn for the rest of the
  // match by a commander standing in its ruins.
  for (Seat& seat : _seats)
  {
    if (seat.fog.Empty())
    {
      continue;
    }
    std::vector<ObjectId> gone;
    for (const Ghost& ghost : seat.ghosts.All())
    {
      if (!seat.fog.Visible(ghost.cellX, ghost.cellY))
      {
        continue;
      }
      const Structure* standing = _world.FindStructure(ghost.structure);
      if (standing == nullptr || standing->cellX != ghost.cellX || standing->cellY != ghost.cellY)
      {
        gone.push_back(ghost.structure);
      }
    }
    for (const ObjectId& id : gone)
    {
      static_cast<void>(seat.ghosts.Forget(id));
    }
  }
}

void Visibility::Advance(const World& _world, std::span<Seat> _seats, const Landscape& _landscape, const ContentTree& _content,
                         std::uint32_t _tick)
{
  m_lastRefreshedViewers = 0;
  m_lastHeightReads = 0;
  if (!_landscape.Created() || _seats.empty())
  {
    return;
  }

  CollectViewers(_world, _seats, _landscape, _content);

  // A stamp whose viewer is gone is un-counted first and without a budget: leaving it would light
  // cells no one can see, and a destroyed viewer is not a refresh that can wait its turn.
  for (std::size_t index = m_stamps.size(); index > 0; --index)
  {
    const ViewerStamp& stamp = m_stamps[index - 1];
    const auto found = std::lower_bound(m_viewers.begin(), m_viewers.end(), stamp.viewer.value,
                                        [](const Viewer& _viewer, std::uint32_t _id) { return _viewer.id.value < _id; });
    const bool alive = found != m_viewers.end() && found->id == stamp.viewer;
    if (!alive)
    {
      UnstampDisc(_seats, _landscape, stamp);
      m_stamps.erase(m_stamps.begin() + static_cast<std::ptrdiff_t>(index - 1));
    }
  }

  // Moved first, then the least recently refreshed, then by id. A viewer with no stamp at all has
  // never been counted, which is the most urgent case of "moved" there is.
  m_due.clear();
  m_due.reserve(m_viewers.size());
  for (std::uint32_t index = 0; index < m_viewers.size(); ++index)
  {
    m_due.push_back(index);
  }
  std::sort(m_due.begin(), m_due.end(),
            [this](std::uint32_t _left, std::uint32_t _right)
            {
              const ViewerStamp* leftStamp = FindStamp(m_viewers[_left].id);
              const ViewerStamp* rightStamp = FindStamp(m_viewers[_right].id);
              const auto moved = [](const Viewer& _viewer, const ViewerStamp* _stamp)
              {
                return _stamp == nullptr || _stamp->cellX != _viewer.cellX || _stamp->cellY != _viewer.cellY ||
                       _stamp->radiusCells != _viewer.radiusCells;
              };
              const bool leftMoved = moved(m_viewers[_left], leftStamp);
              const bool rightMoved = moved(m_viewers[_right], rightStamp);
              if (leftMoved != rightMoved)
              {
                return leftMoved;
              }
              const std::uint32_t leftTick = leftStamp == nullptr ? 0 : leftStamp->refreshedTick;
              const std::uint32_t rightTick = rightStamp == nullptr ? 0 : rightStamp->refreshedTick;
              if (leftTick != rightTick)
              {
                return leftTick < rightTick;
              }
              return m_viewers[_left].id.value < m_viewers[_right].id.value;
            });

  const std::size_t budget = std::min<std::size_t>(REFRESH_BUDGET_VIEWERS, m_due.size());
  for (std::size_t slot = 0; slot < budget; ++slot)
  {
    const Viewer& viewer = m_viewers[m_due[slot]];
    if (const ViewerStamp* stamp = FindStamp(viewer.id); stamp != nullptr)
    {
      if (stamp->cellX == viewer.cellX && stamp->cellY == viewer.cellY && stamp->radiusCells == viewer.radiusCells)
      {
        // Nothing moved, so the disc on the grids is already the right one. The stamp's tick still
        // advances, or an unmoving viewer would be first in the queue for ever and a moving one
        // would never get its turn.
        FindStamp(viewer.id)->refreshedTick = _tick;
        ++m_lastRefreshedViewers;
        continue;
      }
      const ViewerStamp previous = *stamp;
      UnstampDisc(_seats, _landscape, previous);
    }
    StampDisc(_seats, _landscape, viewer, _tick);
    ++m_lastRefreshedViewers;
  }

  m_totalHeightReads += m_lastHeightReads;
  RefreshGhosts(_world, _seats, _content, _tick);
}

} // namespace Frontier
