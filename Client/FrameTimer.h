#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// What a frame cost, as a number a person can read off a running build (m1-vertical-slice/K5).
// m0-foundation/T22 asked the owner for the frame time and got 8 to 16 ms, which was the display's
// refresh and not the renderer's cost, because the client presented with vertical synchronisation
// and reported no frame time anywhere at all. A milestone whose acceptance is a measured frame time
// needs the client able to measure one.
//
// THE WINDOW IS A COUNT AND NOT A DURATION. A window of "the last second" holds 60 samples at 60
// frames a second and 500 at 500, so its 99th percentile means a different thing at each rate and
// two runs cannot be compared. A fixed count of frames always reports the same statistic.
//
// MICROSECONDS, AS AN INTEGER. A frame time in float would be fine here - this is the client and
// AGENTS.md R16 covers Sim - but a percentile over integers has no rounding for two readings of
// the same run to differ by, and a microsecond is finer than any frame time worth reading.

namespace Neuron
{

class FrameTimer
{
public:
  /// Frames in the window. A second at 240 Hz, which is long enough to smooth a hitch and short
  /// enough that the numbers move while a person watches them.
  static constexpr std::size_t WINDOW_FRAMES = 240;

  /// Records one frame. A sample of zero is kept as zero rather than dropped: a frame that took
  /// under a microsecond is a real reading, and dropping it would flatter the mean.
  void Add(std::uint64_t _microseconds) noexcept;

  void Clear() noexcept;

  /// Frames in the window so far, up to WINDOW_FRAMES.
  [[nodiscard]] std::size_t Count() const noexcept
  {
    return m_count < WINDOW_FRAMES ? m_count : WINDOW_FRAMES;
  }

  /// Every frame recorded since the last Clear, which the window itself does not remember.
  [[nodiscard]] std::uint64_t TotalFrames() const noexcept
  {
    return m_count;
  }

  [[nodiscard]] std::uint64_t MeanMicroseconds() const noexcept;

  /// The _percent-th percentile of the window, by the nearest-rank method: the value at
  /// ceil(percent * n / 100), one-based, over the window sorted ascending. Nearest rank rather
  /// than an interpolated percentile because it always returns a frame time that actually
  /// happened, which is what a person reading "99th percentile" wants to be told.
  [[nodiscard]] std::uint64_t PercentileMicroseconds(std::uint32_t _percent) const noexcept;

  [[nodiscard]] std::uint64_t MedianMicroseconds() const noexcept
  {
    return PercentileMicroseconds(50);
  }

private:
  /// A ring: the newest frame overwrites the oldest once the window is full.
  std::array<std::uint64_t, WINDOW_FRAMES> m_frames{};
  std::uint64_t m_count = 0; ///< Frames ever added, so Count() knows whether the window has filled
};

} // namespace Neuron
