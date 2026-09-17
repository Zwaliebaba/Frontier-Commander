#include "pch.h"

#include "FixedPoint.h"
#include "Assertion.h"

namespace Neuron
{

std::int32_t MulDiv(std::int32_t _a, std::int32_t _b, std::int32_t _c) noexcept
{
  FRONTIER_ASSERT(_c != 0);
  const std::int64_t quotient = (static_cast<std::int64_t>(_a) * _b) / _c;
  FRONTIER_ASSERT(quotient >= INT32_MIN && quotient <= INT32_MAX);
  return static_cast<std::int32_t>(quotient);
}

std::int32_t MulShift(std::int32_t _a, std::int32_t _b, int _shift) noexcept
{
  FRONTIER_ASSERT(_shift >= 0 && _shift < 63);
  const std::int64_t shifted = (static_cast<std::int64_t>(_a) * _b) >> _shift;
  FRONTIER_ASSERT(shifted >= INT32_MIN && shifted <= INT32_MAX);
  return static_cast<std::int32_t>(shifted);
}

std::uint32_t Sqrt(std::uint64_t _value) noexcept
{
  // Digit-by-digit in base 4: exact, branch-light, and the same on every machine. The result of
  // a 64-bit radicand fits 32 bits.
  std::uint64_t remainder = _value;
  std::uint64_t result = 0;
  std::uint64_t bit = std::uint64_t{1} << 62;
  while (bit > remainder)
  {
    bit >>= 2;
  }
  while (bit != 0)
  {
    if (remainder >= result + bit)
    {
      remainder -= result + bit;
      result = (result >> 1) + bit;
    }
    else
    {
      result >>= 1;
    }
    bit >>= 2;
  }
  return static_cast<std::uint32_t>(result);
}

std::uint32_t Length(std::int32_t _x, std::int32_t _y) noexcept
{
  return Sqrt(static_cast<std::uint64_t>(LengthSquared(_x, _y)));
}

} // namespace Neuron
