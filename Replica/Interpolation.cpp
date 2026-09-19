#include "pch.h"

#include "Interpolation.h"

#include "FixedPoint.h"
#include "RenderView.h"

#include <algorithm>

namespace Frontier
{

namespace
{

/// A wire unit is a quarter of a world unit (Net/Records.h), so this is 0.25 and multiplying by it
/// is the float conversion, done once per axis. Derived from the two constants rather than written
/// as a quarter, so that a change to either is a change here and not a silent disagreement.
constexpr float WORLD_UNITS_PER_WIRE_UNIT =
  static_cast<float>(SUBUNITS_PER_WIRE_UNIT) / static_cast<float>(Neuron::SUBUNITS_PER_WORLD_UNIT);

// HEADING_SUBSTEPS is Interpolation.h's. Without the substeps the intermediate would round to one
// of the two endpoints and a device would snap through its turn in 1.4-degree jumps however many
// frames were drawn inside the interval.
/// The wire's headings to a full turn: Net/Records.h carries the high byte of a binary angle.
constexpr std::int32_t HEADINGS_PER_TURN = 256;

[[nodiscard]] constexpr float WorldFromWire(std::int64_t _wireUnits) noexcept
{
  return static_cast<float>(_wireUnits) * WORLD_UNITS_PER_WIRE_UNIT;
}

/// _older plus the fraction _offset/_span of the way to _newer, in integers throughout. int64
/// because the product is a full int32 range times a span of ticks scaled by a thousand, which
/// leaves int32 behind at the first frame a device crosses the landscape in.
[[nodiscard]] constexpr std::int64_t Between(std::int64_t _older, std::int64_t _newer, std::int64_t _offset, std::int64_t _span) noexcept
{
  return _older + ((_newer - _older) * _offset) / _span;
}

/// One sample as a pose: where a thing with no segment to move along is drawn.
[[nodiscard]] Pose AtRest(const Sample& _sample) noexcept
{
  return Pose{WorldFromWire(_sample.x), WorldFromWire(_sample.y), WorldFromWire(_sample.z),
              Neuron::RadiansOfBinaryAngle(static_cast<std::int64_t>(_sample.heading) * HEADING_SUBSTEPS)};
}

} // namespace

void Motion::Push(const Sample& _sample) noexcept
{
  if (samples != 0 && _sample.tick == newer.tick)
  {
    newer = _sample;
    return;
  }
  if (samples != 0)
  {
    older = newer;
  }
  newer = _sample;
  samples = static_cast<std::uint8_t>(std::min(samples + 1, 2));
}

Pose Evaluate(const Motion& _motion, std::int64_t _renderTime) noexcept
{
  // One sample, or two that a host published for the same tick: there is no segment, so the newest
  // thing the client was told is the answer. This is the first frame of a match and the first frame
  // an object is seen on, and drawing it at where it is beats not drawing it at all.
  const std::int64_t span = _motion.HasSegment() ? (RenderTimeOfTick(_motion.newer.tick) - RenderTimeOfTick(_motion.older.tick)) : 0;
  if (span <= 0)
  {
    return AtRest(_motion.newer);
  }

  // THE CLAMP IS THE WHOLE GUARANTEE. Without it a render time past the newer sample would
  // extrapolate, and a device that lost a frame would be drawn somewhere the host never put it.
  const std::int64_t offset = std::clamp(_renderTime - RenderTimeOfTick(_motion.older.tick), std::int64_t{0}, span);

  // The shortest way round: 250 to 10 is +16 and not -240. Stated as arithmetic on the unsigned
  // difference rather than as a cast through std::int8_t, which is what it was and which
  // clang-tidy's bugprone-signed-char-misuse rightly refused: a `signed char` widening to a 64-bit
  // integer is a classic way to turn a byte into a negative number by accident, and a reader should
  // not have to know that this one was on purpose.
  const std::int32_t forward = static_cast<std::uint8_t>(_motion.newer.heading - _motion.older.heading);
  const std::int64_t step = forward >= HEADINGS_PER_TURN / 2 ? forward - HEADINGS_PER_TURN : forward;
  const std::int64_t headingFrom = static_cast<std::int64_t>(_motion.older.heading) * HEADING_SUBSTEPS;
  const std::int64_t headingTo = headingFrom + step * HEADING_SUBSTEPS;
  // NOT REDUCED BACK UNDER A TURN. A heading that crossed zero comes out just over one turn, which
  // a sine and a cosine do not care about, and reducing it would put a discontinuity at exactly the
  // place this shortest-way arithmetic exists to make smooth. It cannot run away either: both ends
  // are rebuilt from a byte every frame, so the value never leaves a turn and a half.

  return Pose{WorldFromWire(Between(_motion.older.x, _motion.newer.x, offset, span)),
              WorldFromWire(Between(_motion.older.y, _motion.newer.y, offset, span)),
              WorldFromWire(Between(_motion.older.z, _motion.newer.z, offset, span)),
              Neuron::RadiansOfBinaryAngle(Between(headingFrom, headingTo, offset, span))};
}

} // namespace Frontier
