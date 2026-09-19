#pragma once

#include "Records.h"

#include <cstdint>

// Where a moving thing is drawn between two frames (TechnicalDesign.md §3 step 4, §6.3).
//
// THIS IS THE FIRST FLOAT CONVERSION OF A SIMULATION NUMBER AND THE ONLY ONE. Everything upstream
// of Evaluate is an integer: the simulation's subunits, the wire's quarter-world-units, the binary
// angle's high byte, and the render time below. Evaluate is where they become the world units and
// radians the renderer wants, and nothing else in Replica or Client converts one.
//
// THE CLIENT HAS NO SIMULATION CLOCK (TechnicalDesign.md §3). It has the replica's timeline, which
// is the host's tick numbers arriving late, and a render time the caller names on that timeline.
// This file therefore holds no clock either: Evaluate is a pure function of two samples and a time,
// and the loop that draws decides what that time is.

namespace Frontier
{

/// How far behind the newest frame the client draws: 100 milliseconds, which is two ticks at
/// Neuron::TICKS_PER_SECOND (TechnicalDesign.md §3). The host publishes every second tick, so this
/// is exactly one publish interval - the delay that buys an interval's worth of loss and jitter to
/// absorb, and the one §3 names as the knob to turn if the slice says it is too much.
inline constexpr std::int32_t INTERPOLATION_DELAY_TICKS = 2;

/// A render time is a tick multiplied by this, so that the caller can name a moment BETWEEN two
/// ticks without a float. A tick is 50 milliseconds and a client draws many frames inside one, so
/// the resolution has to be finer than a tick or every drawn frame in an interval would land on the
/// same position and the interpolation would do nothing. A thousandth of a tick is 50 microseconds.
inline constexpr std::int64_t RENDER_TIME_SCALE = 1000;

[[nodiscard]] constexpr std::int64_t RenderTimeOfTick(std::uint32_t _tick) noexcept
{
  return static_cast<std::int64_t>(_tick) * RENDER_TIME_SCALE;
}

/// Where one object was at one frame's tick, in the wire's own units. A sample is what a frame
/// said, never something worked out.
struct Sample
{
  std::uint32_t tick = 0;
  std::int32_t x = 0; ///< Wire units, a quarter of a world unit (Net/Records.h)
  std::int32_t y = 0;
  std::int32_t z = 0;
  std::uint8_t heading = 0; ///< The high byte of a binary angle: 256 headings to the turn

  [[nodiscard]] constexpr bool operator==(const Sample&) const noexcept = default;
};

/// The last two samples of one object, and nothing older.
///
/// TWO, NOT A HISTORY. Interpolation draws between the two frames either side of the render time,
/// and with a render time one publish interval behind the newest frame those two are always the
/// newest and the one before it. A longer history would only be read if the client drew further
/// back than it interpolates, which is extrapolation's problem and not this milestone's.
struct Motion
{
  Sample older;
  Sample newer;
  /// 0, 1 or 2. A count rather than a flag, because a first sample for tick 0 is a real sample and
  /// a flag would have to tell it apart from a default-constructed one by its value.
  std::uint8_t samples = 0;

  [[nodiscard]] constexpr bool HasSegment() const noexcept
  {
    return samples == 2;
  }

  /// Takes a new sample as the newer one and pushes what was newer back. A sample for a tick the
  /// newer one already holds REPLACES it rather than becoming a second point at the same instant:
  /// two samples for one tick would give Evaluate a zero-length segment to divide by.
  void Push(const Sample& _sample) noexcept;
};

/// Where a thing is drawn: world units, and a heading in radians. The only floats in Replica.
struct Pose
{
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float headingRadians = 0.0f;
};

/// The pose at _renderTime, on the segment between the two samples and NEVER OUTSIDE IT. A render
/// time before the older sample gives the older sample and one after the newer gives the newer,
/// because a client that has not been sent a frame yet must draw the last thing it was told rather
/// than a guess: an extrapolated device walks through a wall and then snaps back out of it, which
/// reads worse than a device that waited (TechnicalDesign.md §12 leaves prediction to a later
/// milestone).
[[nodiscard]] Pose Evaluate(const Motion& _motion, std::int64_t _renderTime) noexcept;

} // namespace Frontier
