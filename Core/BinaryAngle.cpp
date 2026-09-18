#include "pch.h"

#include "BinaryAngle.h"
#include "SinTable.h"

namespace Neuron
{

namespace
{

// The table has 1,024 entries over the turn, so an angle's top ten bits pick the entry and the
// low six bits interpolate toward the next; the interpolation is linear in integers and exact
// for what it is, with a maximum error against a real sine that the test pins.
constexpr int TABLE_SHIFT = 6;
constexpr std::uint32_t TABLE_MASK = static_cast<std::uint32_t>(SIN_TABLE_16_16.size() - 1);
constexpr std::int32_t FRACTION_MASK = (1 << TABLE_SHIFT) - 1;

} // namespace

std::int32_t Sin(BinaryAngle _angle) noexcept
{
  const std::uint32_t index = static_cast<std::uint32_t>(_angle) >> TABLE_SHIFT;
  const std::int32_t fraction = static_cast<std::int32_t>(_angle) & FRACTION_MASK;
  const std::int32_t here = SIN_TABLE_16_16[index];
  const std::int32_t next = SIN_TABLE_16_16[(index + 1) & TABLE_MASK];
  return here + (((next - here) * fraction) >> TABLE_SHIFT);
}

std::int32_t Cos(BinaryAngle _angle) noexcept
{
  return Sin(static_cast<BinaryAngle>(_angle + QUARTER_TURN));
}

BinaryAngle AngleOf(std::int32_t _x, std::int32_t _y) noexcept
{
  if (_x == 0 && _y == 0)
  {
    return 0;
  }
  // Angle 0 points along +y and a quarter turn points along +x (the direction of a facing is
  // (Sin, Cos)), so the two signs name the quarter the answer lies in. Inside it the answer is
  // never more than a quarter turn away, which is what makes the cross product below monotone.
  std::uint32_t low = 0;
  if (_x >= 0 && _y >= 0)
  {
    low = 0;
  }
  else if (_x >= 0)
  {
    low = QUARTER_TURN;
  }
  else if (_y < 0)
  {
    low = HALF_TURN;
  }
  else
  {
    low = HALF_TURN + QUARTER_TURN;
  }

  const auto cross = [_x, _y](std::uint32_t _angle) noexcept
  {
    const auto angle = static_cast<BinaryAngle>(_angle);
    return static_cast<std::int64_t>(Sin(angle)) * _y - static_cast<std::int64_t>(Cos(angle)) * _x;
  };

  std::uint32_t high = low + QUARTER_TURN;
  while (high - low > 1)
  {
    const std::uint32_t middle = low + (high - low) / 2;
    if (cross(middle) < 0)
    {
      low = middle;
    }
    else
    {
      high = middle;
    }
  }
  // The two straddle the answer; the nearer is the one whose cross product is the smaller, and a
  // tie goes to the lower so that two hosts cannot part company on it.
  const std::int64_t below = cross(low) < 0 ? -cross(low) : cross(low);
  const std::int64_t above = cross(high) < 0 ? -cross(high) : cross(high);
  return static_cast<BinaryAngle>(above < below ? high : low);
}

} // namespace Neuron
