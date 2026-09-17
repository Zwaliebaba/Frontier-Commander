#pragma once

#include <cstdint>

namespace Neuron
{

/// How the scene target reaches the back buffer (AGENTS.md §5): unfiltered at 1:1, point sampled
/// at a whole-number multiple, bilinear otherwise; letterboxed or pillarboxed whenever the client
/// area's aspect differs from the authored one.
enum class ScaleMode : std::uint8_t
{
  Exact,
  Integer,
  Bilinear
};

/// Where the scene target lands in the client area, in client pixels.
struct ScaledRectangle
{
  ScaleMode mode;
  std::uint32_t factor; ///< The whole-number multiple for Integer, 1 for Exact, 0 for Bilinear
  std::int32_t x;
  std::int32_t y;
  std::uint32_t width;
  std::uint32_t height;

  [[nodiscard]] constexpr bool operator==(const ScaledRectangle&) const noexcept = default;
};

/// The one place that reads the window's size (AGENTS.md §5): a pure function from the client
/// area and the authored size to the mode and the destination rectangle, the largest rectangle of
/// the authored aspect that fits, centred. Integer arithmetic only, so that the rectangle is the
/// same on every machine and in every test.
[[nodiscard]] ScaledRectangle FitAuthored(std::uint32_t _clientWidth, std::uint32_t _clientHeight, std::uint32_t _authoredWidth,
                                          std::uint32_t _authoredHeight) noexcept;

} // namespace Neuron
