#include "pch.h"

#include "Interpolation.h"

#include "FixedPoint.h"

#include <algorithm>
#include <numbers>

namespace Frontier
{

namespace
{

/// A wire unit is a quarter of a world unit (Net/Records.h), so this is 0.25 and multiplying by it
/// is the float conversion, done once per axis. Derived from the two constants rather than written
/// as a quarter, so that a change to either is a change here and not a silent disagreement.
constexpr float WORLD_UNITS_PER_WIRE_UNIT =
  static_cast<float>(SUBUNITS_PER_WIRE_UNIT) / static_cast<float>(Neuron::SUBUNITS_PER_WORLD_UNIT);

/// A heading is interpolated at 256 substeps to each of the wire's 256 steps, which multiplies back
/// out to the 65,536 of a full binary angle (Core/BinaryAngle.h) - the resolution the simulation
/// turns at before the wire coarsened it. Without the substeps the intermediate would round to one
/// of the two endpoints and a device would snap through its turn in 1.4-degree jumps however many
/// frames were drawn inside the interval.
constexpr std::int64_t HEADING_SUBSTEPS = 256;
constexpr float RADIANS_PER_HEADING_SUBSTEP = 2.0f * std::numbers::pi_v<float> / 65536.0f;

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
              static_cast<float>(static_cast<std::int64_t>(_sample.heading) * HEADING_SUBSTEPS) * RADIANS_PER_HEADING_SUBSTEP};
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

  // The shortest way round, as a signed step: 250 to 10 is +16 and not -240, and the cast through
  // std::int8_t is what makes the wrap arithmetic rather than a special case.
  const std::int64_t step = static_cast<std::int8_t>(static_cast<std::uint8_t>(_motion.newer.heading - _motion.older.heading));
  const std::int64_t headingFrom = static_cast<std::int64_t>(_motion.older.heading) * HEADING_SUBSTEPS;
  const std::int64_t headingTo = headingFrom + step * HEADING_SUBSTEPS;
  // NOT REDUCED BACK UNDER A TURN. A heading that crossed zero comes out just over one turn, which
  // a sine and a cosine do not care about, and reducing it would put a discontinuity at exactly the
  // place this shortest-way arithmetic exists to make smooth. It cannot run away either: both ends
  // are rebuilt from a byte every frame, so the value never leaves a turn and a half.

  return Pose{WorldFromWire(Between(_motion.older.x, _motion.newer.x, offset, span)),
              WorldFromWire(Between(_motion.older.y, _motion.newer.y, offset, span)),
              WorldFromWire(Between(_motion.older.z, _motion.newer.z, offset, span)),
              static_cast<float>(Between(headingFrom, headingTo, offset, span)) * RADIANS_PER_HEADING_SUBSTEP};
}

} // namespace Frontier
