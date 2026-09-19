#include "pch.h"

#include "Placement.h"

#include "Plan.h"

#include "FixedPoint.h"

#include <algorithm>
#include <limits>

namespace Frontier
{

namespace
{

/// The samples one footprint owns: four a cell plus the boundary sample on the far side, which is
/// the same 5-by-5-per-cell window Landscape::DeriveCells reads.
struct SampleWindow
{
  std::uint32_t x0;
  std::uint32_t y0;
  std::uint32_t x1; ///< Inclusive
  std::uint32_t y1;
  bool valid;
};

[[nodiscard]] SampleWindow WindowOf(const Landscape& _landscape, const Footprint& _footprint) noexcept
{
  if (!_landscape.Created() || _footprint.cellsX == 0 || _footprint.cellsY == 0)
  {
    return {0, 0, 0, 0, false};
  }
  const std::uint32_t cells = _landscape.CellsPerSide();
  if (_footprint.cellX >= cells || _footprint.cellY >= cells || _footprint.cellsX > cells - _footprint.cellX ||
      _footprint.cellsY > cells - _footprint.cellY)
  {
    return {0, 0, 0, 0, false};
  }
  return {_footprint.cellX * SAMPLES_PER_CELL_EDGE, _footprint.cellY * SAMPLES_PER_CELL_EDGE,
          (_footprint.cellX + _footprint.cellsX) * SAMPLES_PER_CELL_EDGE, (_footprint.cellY + _footprint.cellsY) * SAMPLES_PER_CELL_EDGE,
          true};
}

} // namespace

Footprint FootprintOf(const Structure& _structure, const ContentTree* _content) noexcept
{
  if (_content != nullptr && _structure.design < _content->structures.structures.size())
  {
    return FootprintAt(_content->structures.structures[_structure.design], _structure.cellX, _structure.cellY);
  }
  return {_structure.cellX, _structure.cellY, 1, 1};
}

bool Overlaps(const Footprint& _a, const Footprint& _b) noexcept
{
  return _a.cellX < _b.cellX + _b.cellsX && _b.cellX < _a.cellX + _a.cellsX && _a.cellY < _b.cellY + _b.cellsY &&
         _b.cellY < _a.cellY + _a.cellsY;
}

std::uint32_t FootprintSlopePercent(const Landscape& _landscape, const Footprint& _footprint) noexcept
{
  if (!WindowOf(_landscape, _footprint).valid)
  {
    return std::numeric_limits<std::uint32_t>::max();
  }
  // The steepest of the cells under it, which the landscape derived once when it was made
  // (Placement.h says why this reading and not the average gradient).
  std::uint32_t steepest = 0;
  for (std::uint32_t y = _footprint.cellY; y < _footprint.cellY + _footprint.cellsY; ++y)
  {
    for (std::uint32_t x = _footprint.cellX; x < _footprint.cellX + _footprint.cellsX; ++x)
    {
      steepest = std::max<std::uint32_t>(steepest, _landscape.CellAt(x, y).slopePercent);
    }
  }
  return steepest;
}

std::int32_t FootprintMeanHeightWorldUnits(const Landscape& _landscape, const Footprint& _footprint) noexcept
{
  const SampleWindow window = WindowOf(_landscape, _footprint);
  if (!window.valid)
  {
    return 0;
  }
  std::int64_t total = 0;
  std::int64_t count = 0;
  for (std::uint32_t y = window.y0; y <= window.y1; ++y)
  {
    for (std::uint32_t x = window.x0; x <= window.x1; ++x)
    {
      total += _landscape.HeightAt(x, y);
      ++count;
    }
  }
  // Integer division toward zero, which is the same on every machine (AGENTS.md R16); the half
  // world unit it can lose is a quarter of a subunit of drawing and nothing the simulation reads.
  return static_cast<std::int32_t>(total / count);
}

HeightDelta FlattenDelta(const Landscape& _landscape, const Footprint& _footprint)
{
  HeightDelta delta{};
  const SampleWindow window = WindowOf(_landscape, _footprint);
  if (!window.valid)
  {
    return delta;
  }
  const std::int32_t mean = FootprintMeanHeightWorldUnits(_landscape, _footprint);
  delta.x = window.x0;
  delta.y = window.y0;
  delta.width = window.x1 - window.x0 + 1;
  delta.height = window.y1 - window.y0 + 1;
  delta.heights.assign(static_cast<std::size_t>(delta.width) * delta.height, static_cast<std::int16_t>(mean));
  return delta;
}

PlacementFault CheckPlacement(const Footprint& _footprint, const PlacementQuery& _query)
{
  const Landscape& landscape = *_query.landscape;
  if (!WindowOf(landscape, _footprint).valid)
  {
    return PlacementFault::OffLandscape;
  }

  // Something that occupies its cells, or a feature. A plan is neither: GameDesign.md §5 says a
  // plan obstructs nothing until a builder begins it, so two commanders may plan the same ground
  // and the first to start it gets it.
  PlacementFault occupancy = PlacementFault::Accepted;
  _query.world->ForEachStructure(
    [&occupancy, &_footprint, &_query](ObjectId, const Structure& _structure)
    {
      if (occupancy != PlacementFault::Accepted || !Occupies(_structure.state))
      {
        return;
      }
      if (Overlaps(_footprint, FootprintOf(_structure, _query.content)))
      {
        occupancy = PlacementFault::Occupied;
      }
    });
  _query.world->ForEachFeature(
    [&occupancy, &_footprint](ObjectId, const Feature& _feature)
    {
      if (occupancy == PlacementFault::Accepted && Overlaps(_footprint, {_feature.cellX, _feature.cellY, 1, 1}))
      {
        occupancy = PlacementFault::Occupied;
      }
    });
  if (occupancy != PlacementFault::Accepted)
  {
    return occupancy;
  }

  for (std::uint32_t y = _footprint.cellY; y < _footprint.cellY + _footprint.cellsY; ++y)
  {
    for (std::uint32_t x = _footprint.cellX; x < _footprint.cellX + _footprint.cellsX; ++x)
    {
      if ((landscape.CellAt(x, y).flags & Landscape::CELL_WATER) != 0)
      {
        return PlacementFault::Water;
      }
    }
  }

  if (FootprintSlopePercent(landscape, _footprint) >= MAX_PLACEMENT_SLOPE_PERCENT)
  {
    return PlacementFault::TooSteep;
  }

  // "A structure may be placed anywhere the commander has explored" (GameDesign.md §5). Explored,
  // not visible: a base built where a scout once walked is the whole point of the rule.
  if (_query.seat != nullptr && !_query.seat->fog.Empty())
  {
    for (std::uint32_t y = _footprint.cellY; y < _footprint.cellY + _footprint.cellsY; ++y)
    {
      for (std::uint32_t x = _footprint.cellX; x < _footprint.cellX + _footprint.cellsX; ++x)
      {
        if (!_query.seat->fog.Explored(x, y))
        {
          return PlacementFault::Unexplored;
        }
      }
    }
  }

  // An extractor stands on a deposit and nowhere else (GameDesign.md §4). Its footprint is one
  // cell, so the deposit is under its origin.
  if (_query.row != nullptr && _query.row->role == StructureRole::Extractor && _query.deposits != nullptr &&
      !_query.deposits->Has(_footprint.cellX, _footprint.cellY))
  {
    return PlacementFault::NotOnDeposit;
  }
  return PlacementFault::Accepted;
}

} // namespace Frontier
