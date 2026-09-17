#pragma once

#include <cstdint>
#include <vector>

namespace Frontier
{

/// A rectangle of replacement heights applied over the generated base (TechnicalDesign.md §4.4):
/// the flatten under a structure, and nothing else through M3. A snapshot carries the definition
/// and the list of these rather than the samples, and applying the list to a fresh base
/// reproduces every sample.
struct HeightDelta
{
  std::uint32_t x; ///< In samples
  std::uint32_t y;
  std::uint32_t width;
  std::uint32_t height;
  std::vector<std::int16_t> heights; ///< width * height samples, row-major, the new values

  [[nodiscard]] bool operator==(const HeightDelta&) const noexcept = default;
};

} // namespace Frontier
