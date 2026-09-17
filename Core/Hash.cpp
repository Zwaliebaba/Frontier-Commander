#include "pch.h"

#include "Hash.h"

namespace Neuron
{

std::uint64_t Fnv1a64(std::span<const std::byte> _bytes, std::uint64_t _hash) noexcept
{
  for (const std::byte byte : _bytes)
  {
    _hash ^= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(byte));
    _hash *= FNV1A64_PRIME;
  }
  return _hash;
}

} // namespace Neuron
