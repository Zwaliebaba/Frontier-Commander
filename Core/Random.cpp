#include "pch.h"

#include "Random.h"
#include "Assertion.h"

namespace Neuron
{

namespace
{

constexpr std::uint64_t GOLDEN_GAMMA = 0x9E3779B97F4A7C15ull;

[[nodiscard]] constexpr std::uint32_t RotateLeft(std::uint32_t _value, int _count) noexcept
{
  return (_value << _count) | (_value >> (32 - _count));
}

} // namespace

std::uint64_t SplitMix64Next(std::uint64_t& _state) noexcept
{
  _state += GOLDEN_GAMMA;
  std::uint64_t z = _state;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

std::uint64_t DeriveSeed(std::uint64_t _seed, std::uint64_t _index) noexcept
{
  std::uint64_t state = _seed + _index * GOLDEN_GAMMA;
  return SplitMix64Next(state);
}

Random::Random(std::uint64_t _seed) noexcept
{
  std::uint64_t state = _seed;
  const std::uint64_t first = SplitMix64Next(state);
  const std::uint64_t second = SplitMix64Next(state);
  m_state = {static_cast<std::uint32_t>(first), static_cast<std::uint32_t>(first >> 32), static_cast<std::uint32_t>(second),
             static_cast<std::uint32_t>(second >> 32)};
}

Random::Random(const State& _state) noexcept
  : m_state(_state)
{
}

std::uint32_t Random::Next() noexcept
{
  const std::uint32_t result = RotateLeft(m_state[1] * 5u, 7) * 9u;
  const std::uint32_t t = m_state[1] << 9;
  m_state[2] ^= m_state[0];
  m_state[3] ^= m_state[1];
  m_state[1] ^= m_state[2];
  m_state[0] ^= m_state[3];
  m_state[2] ^= t;
  m_state[3] = RotateLeft(m_state[3], 11);
  return result;
}

std::uint32_t Random::Below(std::uint32_t _bound) noexcept
{
  FRONTIER_ASSERT(_bound >= 1);
  // Values below the threshold would favour the low results; draw again instead.
  const std::uint32_t threshold = (0u - _bound) % _bound;
  for (;;)
  {
    const std::uint32_t value = Next();
    if (value >= threshold)
    {
      return value % _bound;
    }
  }
}

std::int32_t Random::Between(std::int32_t _min, std::int32_t _max) noexcept
{
  FRONTIER_ASSERT(_min <= _max);
  const std::uint32_t span = static_cast<std::uint32_t>(static_cast<std::int64_t>(_max) - _min + 1);
  return static_cast<std::int32_t>(_min + static_cast<std::int64_t>(span == 0 ? Next() : Below(span)));
}

} // namespace Neuron
