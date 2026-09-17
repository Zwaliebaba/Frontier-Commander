#pragma once

#include <cstdint>

#include "FixedPoint.h"

// Integer geometry in the simulation's units (TechnicalDesign.md §4.1): a point is a pair of
// std::int32_t in 1/256 world unit, a rectangle two points. Plain aggregates (AGENTS.md R8), so
// that brace initialization reads naturally and a wire record can carry one whole.

namespace Neuron
{

struct Point2
{
  std::int32_t x;
  std::int32_t y;

  [[nodiscard]] constexpr bool operator==(const Point2&) const noexcept = default;
};

[[nodiscard]] constexpr Point2 operator+(Point2 _a, Point2 _b) noexcept
{
  return {_a.x + _b.x, _a.y + _b.y};
}

[[nodiscard]] constexpr Point2 operator-(Point2 _a, Point2 _b) noexcept
{
  return {_a.x - _b.x, _a.y - _b.y};
}

/// The squared distance between two points, widened to 64 bits.
[[nodiscard]] constexpr std::int64_t DistanceSquared(Point2 _a, Point2 _b) noexcept
{
  return LengthSquared(_a.x - _b.x, _a.y - _b.y);
}

/// The cell a point lies in, along one axis: a floor shift, so that a negative coordinate lands
/// in the cell below zero rather than in cell zero.
[[nodiscard]] constexpr std::int32_t CellOf(std::int32_t _subunits) noexcept
{
  return _subunits >> SUBUNITS_PER_CELL_SHIFT;
}

/// The subunit coordinate of a cell's lower corner.
[[nodiscard]] constexpr std::int32_t SubunitsOfCell(std::int32_t _cell) noexcept
{
  return _cell * SUBUNITS_PER_CELL;
}

/// A half-open rectangle [min, max) in the same units.
struct Rect
{
  Point2 min;
  Point2 max;

  [[nodiscard]] constexpr bool operator==(const Rect&) const noexcept = default;

  [[nodiscard]] constexpr bool Contains(Point2 _point) const noexcept
  {
    return _point.x >= min.x && _point.x < max.x && _point.y >= min.y && _point.y < max.y;
  }

  [[nodiscard]] constexpr bool Overlaps(const Rect& _other) const noexcept
  {
    return min.x < _other.max.x && _other.min.x < max.x && min.y < _other.max.y && _other.min.y < max.y;
  }

  [[nodiscard]] constexpr std::int32_t Width() const noexcept
  {
    return max.x - min.x;
  }
  [[nodiscard]] constexpr std::int32_t Height() const noexcept
  {
    return max.y - min.y;
  }
};

} // namespace Neuron
