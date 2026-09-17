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
constexpr std::uint32_t TABLE_MASK = SIN_TABLE_16_16.size() - 1;
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

} // namespace Neuron
