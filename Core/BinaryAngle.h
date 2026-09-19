#pragma once

#include <cstdint>

// A binary angle is a std::uint16_t turn: 65,536 per revolution, so that addition wraps by itself
// and no angle ever needs normalising (TechnicalDesign.md §4.1). Sin and Cos read a 1,024-entry
// integer table with linear interpolation and return 16.16 fixed point; nothing here is a float.

namespace Neuron
{

using BinaryAngle = std::uint16_t;

inline constexpr BinaryAngle QUARTER_TURN = 16384;
inline constexpr BinaryAngle HALF_TURN = 32768;
inline constexpr std::uint32_t FULL_TURN = 65536;

/// sin(_angle) in 16.16 fixed point, in [-65536, 65536].
[[nodiscard]] std::int32_t Sin(BinaryAngle _angle) noexcept;

/// cos(_angle) in 16.16 fixed point, in [-65536, 65536].
[[nodiscard]] std::int32_t Cos(BinaryAngle _angle) noexcept;

/// The binary angle that points along (_x, _y): the one whose (Sin, Cos) is that direction, so
/// that AngleOf(Sin(a), Cos(a)) gives a back. (0, 0) has no direction and answers 0.
///
/// It is found by bisecting on the sign of Sin(a) * _y - Cos(a) * _x, which is the cross product
/// of the two directions and so is |(_x, _y)| * sin(a - answer): zero at the answer and of the
/// sign of a - answer across the quarter turn the two signs of _x and _y pick out. Bisecting the
/// SAME table Sin and Cos read is the point of doing it this way rather than by a polynomial: a
/// device turns to the angle this returns and then moves along Sin and Cos of it, and an inverse
/// that disagreed with them by a unit would leave it turning back and forth for ever.
[[nodiscard]] BinaryAngle AngleOf(std::int32_t _x, std::int32_t _y) noexcept;

/// The signed shortest turn from _from to _to, in [-32768, 32767].
[[nodiscard]] constexpr std::int16_t TurnBetween(BinaryAngle _from, BinaryAngle _to) noexcept
{
  return static_cast<std::int16_t>(static_cast<std::uint16_t>(_to - _from));
}

/// _from turned toward _to by at most _rate, arriving exactly when the remaining turn is within it.
[[nodiscard]] constexpr BinaryAngle TurnToward(BinaryAngle _from, BinaryAngle _to, std::uint16_t _rate) noexcept
{
  // An exact half turn has no shorter way round; it turns the positive way so that the
  // choice is fixed rather than left to the sign of the wrapped difference.
  std::int32_t remaining = TurnBetween(_from, _to);
  if (remaining == -static_cast<std::int32_t>(HALF_TURN))
  {
    remaining = static_cast<std::int32_t>(HALF_TURN);
  }
  if (remaining >= 0)
  {
    return remaining <= static_cast<std::int32_t>(_rate) ? _to : static_cast<BinaryAngle>(_from + _rate);
  }
  return -remaining <= static_cast<std::int32_t>(_rate) ? _to : static_cast<BinaryAngle>(_from - _rate);
}

} // namespace Neuron
