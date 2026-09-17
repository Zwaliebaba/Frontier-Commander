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

/// The signed shortest turn from _from to _to, in [-32768, 32767].
[[nodiscard]] constexpr std::int16_t TurnBetween(BinaryAngle _from, BinaryAngle _to) noexcept
{
  return static_cast<std::int16_t>(static_cast<std::uint16_t>(_to - _from));
}

/// _from turned toward _to by at most _rate, arriving exactly when the remaining turn is within it.
[[nodiscard]] constexpr BinaryAngle TurnToward(BinaryAngle _from, BinaryAngle _to, std::uint16_t _rate) noexcept
{
  const std::int16_t remaining = TurnBetween(_from, _to);
  if (remaining >= 0)
  {
    return remaining <= static_cast<std::int32_t>(_rate) ? _to : static_cast<BinaryAngle>(_from + _rate);
  }
  return -remaining <= static_cast<std::int32_t>(_rate) ? _to : static_cast<BinaryAngle>(_from - _rate);
}

} // namespace Neuron
