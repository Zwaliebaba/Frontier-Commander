#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

// FNV-1a, 64 bits: the state hash of TechnicalDesign.md §4.9 and the content hash of §8. A few
// lines, and the same on every machine because every integer is fed in a defined byte order
// (little-endian) rather than as its object representation.

namespace Neuron
{

inline constexpr std::uint64_t FNV1A64_OFFSET = 0xCBF29CE484222325ull;
inline constexpr std::uint64_t FNV1A64_PRIME = 0x100000001B3ull;

/// Folds the bytes into _hash; start from FNV1A64_OFFSET for a fresh hash.
[[nodiscard]] std::uint64_t Fnv1a64(std::span<const std::byte> _bytes, std::uint64_t _hash = FNV1A64_OFFSET) noexcept;

namespace Detail
{

/// The integer behind a value: the type itself, or an enum's underlying type.
template <class T> struct IntegerOf
{
  using Type = T;
};

template <class T>
  requires std::is_enum_v<T>
struct IntegerOf<T>
{
  using Type = std::underlying_type_t<T>;
};

} // namespace Detail

/// Folds an integer into _hash as its little-endian bytes, whatever the platform's order.
template <class T>
  requires (std::is_integral_v<T> && !std::is_same_v<T, bool>) || std::is_enum_v<T>
[[nodiscard]] constexpr std::uint64_t HashInteger(std::uint64_t _hash, T _value) noexcept
{
  using Unsigned = std::make_unsigned_t<typename Detail::IntegerOf<T>::Type>;
  auto bits = static_cast<Unsigned>(_value);
  for (std::size_t index = 0; index < sizeof(Unsigned); ++index)
  {
    _hash ^= static_cast<std::uint64_t>(bits & 0xFF);
    _hash *= FNV1A64_PRIME;
    bits = static_cast<Unsigned>(bits >> 8);
  }
  return _hash;
}

/// Folds every integer of a span into _hash, in order.
template <class T>
  requires (std::is_integral_v<T> && !std::is_same_v<T, bool>) || std::is_enum_v<T>
[[nodiscard]] constexpr std::uint64_t HashIntegers(std::uint64_t _hash, std::span<const T> _values) noexcept
{
  for (const T& value : _values)
  {
    _hash = HashInteger(_hash, value);
  }
  return _hash;
}

} // namespace Neuron
