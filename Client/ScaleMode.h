#pragma once

#include <cstdint>

namespace Neuron
{

/// The authored resolution (ADR-004): every pass draws at this size, whatever the window's.
///
/// IT LIVES HERE AND NOT IN SceneTarget.h, WHERE IT WAS, because the interface's layout is authored
/// against it (Design/Interface.md §2) and SceneTarget.h cannot be included without Direct3D. Two
/// numbers that nothing but arithmetic uses should not drag a graphics API in behind them, and the
/// layout arithmetic they serve is the part of the client a test can run on any machine.
inline constexpr std::uint32_t AUTHORED_WIDTH_PIXELS = 1920;
inline constexpr std::uint32_t AUTHORED_HEIGHT_PIXELS = 1080;

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

/// Where a client pixel lands in authored pixels: the inverse of FitAuthored, and the one place the
/// conversion happens (Design/Interface.md §4). Every rectangle in the interface is authored and
/// every mouse position the window gives is client, so without this every hit test would carry its
/// own arithmetic and they would drift apart.
///
/// FALSE FOR A POINTER IN THE LETTERBOX. The bars either side of the scaled rectangle are over no
/// panel and over no world, and §4 says every hit test refuses them; returning a clamped position
/// would put the pointer on the edge of the frame instead, which reads as the player hovering the
/// outermost widget whenever the mouse leaves the picture.
struct AuthoredPosition
{
  std::int32_t x;
  std::int32_t y;

  [[nodiscard]] constexpr bool operator==(const AuthoredPosition&) const noexcept = default;
};

[[nodiscard]] bool AuthoredFromClient(const ScaledRectangle& _fit, std::int32_t _clientX, std::int32_t _clientY,
                                      std::uint32_t _authoredWidth, std::uint32_t _authoredHeight, AuthoredPosition& _outAuthored) noexcept;

} // namespace Neuron
