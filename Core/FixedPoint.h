#pragma once

#include <cstdint>

// The simulation's numbers (TechnicalDesign.md §4.1; AGENTS.md R16). Positions and heights are
// std::int32_t in 1/256 world unit; a product of two positions overflows int32, so every such
// product is done here in std::int64_t and nobody writes the widening by hand twice. Nothing in
// this header is a float, and nothing in it depends on the platform.

namespace Neuron
{

/// The subunits of one world unit: positions are int32 in 1/256 world unit.
inline constexpr std::int32_t SUBUNITS_PER_WORLD_UNIT = 256;

/// One cell is 64 world units on a side, so cell arithmetic is a shift.
inline constexpr std::int32_t WORLD_UNITS_PER_CELL = 64;
inline constexpr std::int32_t SUBUNITS_PER_CELL = SUBUNITS_PER_WORLD_UNIT * WORLD_UNITS_PER_CELL;
inline constexpr int SUBUNITS_PER_CELL_SHIFT = 14;

static_assert(SUBUNITS_PER_CELL == (1 << SUBUNITS_PER_CELL_SHIFT));

/// The tick rate (ADR-002): the simulation's only clock, 50 ms a tick. Every duration under Sim/
/// is a tick count and every rate is per tick; this constant is what a table authored in seconds
/// is converted by, once, in Content.
inline constexpr std::int32_t TICKS_PER_SECOND = 20;
inline constexpr std::int32_t TICK_MILLISECONDS = 1000 / TICKS_PER_SECOND;

static_assert(TICKS_PER_SECOND * TICK_MILLISECONDS == 1000, "a tick is a whole number of milliseconds");

/// A 16.16 fixed-point unit: what the sine table and the fractal tables are scaled by.
inline constexpr std::int32_t FIXED_16_ONE = 65536;

/// _a * _b / _c with the product widened to 64 bits; the quotient is truncated toward zero as
/// the C++ division is, and must fit int32, which the caller guarantees (asserted in Debug).
[[nodiscard]] std::int32_t MulDiv(std::int32_t _a, std::int32_t _b, std::int32_t _c) noexcept;

/// _a * _b >> _shift with the product widened to 64 bits; the shift is arithmetic (floor), as
/// C++20 defines it for a signed value.
[[nodiscard]] std::int32_t MulShift(std::int32_t _a, std::int32_t _b, int _shift) noexcept;

/// The integer square root: the largest value whose square is at most _value. 0 for a negative.
[[nodiscard]] std::uint32_t Sqrt(std::uint64_t _value) noexcept;

/// The dot product of (_ax, _ay) and (_bx, _by), widened to 64 bits.
[[nodiscard]] constexpr std::int64_t Dot(std::int32_t _ax, std::int32_t _ay, std::int32_t _bx, std::int32_t _by) noexcept
{
  return static_cast<std::int64_t>(_ax) * _bx + static_cast<std::int64_t>(_ay) * _by;
}

/// The squared length of (_x, _y), widened to 64 bits; compare squared lengths wherever a root is
/// not needed.
[[nodiscard]] constexpr std::int64_t LengthSquared(std::int32_t _x, std::int32_t _y) noexcept
{
  return Dot(_x, _y, _x, _y);
}

/// The length of (_x, _y) as the integer square root of the squared length.
[[nodiscard]] std::uint32_t Length(std::int32_t _x, std::int32_t _y) noexcept;

/// Floor division for a possibly negative numerator and a positive denominator, so that a
/// simulation quantity divides the same way the Python landscape tool divides (toward negative
/// infinity), rather than toward zero as C++ does.
[[nodiscard]] constexpr std::int64_t FloorDiv(std::int64_t _numerator, std::int64_t _denominator) noexcept
{
  const std::int64_t quotient = _numerator / _denominator;
  const std::int64_t remainder = _numerator % _denominator;
  return (remainder != 0 && ((remainder < 0) != (_denominator < 0))) ? quotient - 1 : quotient;
}

} // namespace Neuron
