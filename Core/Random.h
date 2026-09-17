#pragma once

#include <array>
#include <cstdint>

// The simulation's random stream (TechnicalDesign.md §4.2): xoshiro128**, written from its
// public-domain specification, 16 bytes of state, seeded from a 64-bit seed through splitmix64.
// One generator per match, advanced only by the simulation in a fixed order, and its state read
// and restored whole by a snapshot. Tools/Xoshiro.py is the same algorithm in Python; the tests
// pin the two to each other.

namespace Neuron
{

/// One step of splitmix64: advances the state and returns the output.
[[nodiscard]] std::uint64_t SplitMix64Next(std::uint64_t& _state) noexcept;

/// A 64-bit seed for stream _index of match seed _seed: one splitmix64 step from
/// _seed + _index * the golden gamma, so that a tile, a seat or a system gets its own stream
/// from the one match seed without sharing draws.
[[nodiscard]] std::uint64_t DeriveSeed(std::uint64_t _seed, std::uint64_t _index) noexcept;

class Random
{
public:
  using State = std::array<std::uint32_t, 4>;

  /// Seeds through two splitmix64 steps: the first output's low and high words, then the second's.
  explicit Random(std::uint64_t _seed) noexcept;

  /// Resumes from a state a snapshot carried.
  explicit Random(const State& _state) noexcept;

  /// The next 32 bits.
  [[nodiscard]] std::uint32_t Next() noexcept;

  /// Uniform in [0, _bound) by rejection, so that no value is favoured; _bound is at least 1.
  [[nodiscard]] std::uint32_t Below(std::uint32_t _bound) noexcept;

  /// Uniform in [_min, _max], both inclusive, with _min at most _max.
  [[nodiscard]] std::int32_t Between(std::int32_t _min, std::int32_t _max) noexcept;

  [[nodiscard]] const State& GetState() const noexcept
  {
    return m_state;
  }
  void SetState(const State& _state) noexcept
  {
    m_state = _state;
  }

private:
  State m_state;
};

} // namespace Neuron
