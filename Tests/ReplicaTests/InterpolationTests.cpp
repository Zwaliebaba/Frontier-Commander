#include "pch.h"

#include "Interpolation.h"

#include <cstdint>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// Where a moving thing is drawn between two frames (m1-vertical-slice/R1; TechnicalDesign.md §3).
// The guarantee this file exists for is the one §10 names: "interpolation never places an object
// outside the segment between two frames".
namespace ReplicaTests
{

namespace
{

constexpr float TOLERANCE = 0.001f;

[[nodiscard]] Frontier::Motion Segment(std::uint32_t _olderTick, std::int32_t _olderX, std::uint32_t _newerTick, std::int32_t _newerX,
                                       std::uint8_t _olderHeading = 0, std::uint8_t _newerHeading = 0)
{
  Frontier::Motion motion;
  motion.Push(Frontier::Sample{_olderTick, _olderX, 0, 0, _olderHeading});
  motion.Push(Frontier::Sample{_newerTick, _newerX, 0, 0, _newerHeading});
  return motion;
}

} // namespace

TEST_CLASS(InterpolationTests)
{
public:
  TEST_METHOD(AMotionKeepsTheLastTwoSamplesAndNoMore)
  {
    Frontier::Motion motion;
    Assert::AreEqual(std::uint8_t{0}, motion.samples);
    Assert::IsFalse(motion.HasSegment());

    motion.Push(Frontier::Sample{10, 100, 0, 0, 0});
    Assert::AreEqual(std::uint8_t{1}, motion.samples, L"one sample is not a segment");
    Assert::IsFalse(motion.HasSegment());

    motion.Push(Frontier::Sample{12, 200, 0, 0, 0});
    Assert::IsTrue(motion.HasSegment());
    Assert::AreEqual(std::uint32_t{10}, motion.older.tick);
    Assert::AreEqual(std::uint32_t{12}, motion.newer.tick);

    motion.Push(Frontier::Sample{14, 300, 0, 0, 0});
    Assert::AreEqual(std::uint32_t{12}, motion.older.tick, L"the third pushes the first out");
    Assert::AreEqual(std::uint32_t{14}, motion.newer.tick);
  }

  /// A zero-length segment is a division by zero waiting to happen, so a second sample for a tick
  /// the newer one already holds replaces it instead of becoming a point beside it.
  TEST_METHOD(ASecondSampleForTheSameTickReplacesRatherThanDuplicates)
  {
    Frontier::Motion motion;
    motion.Push(Frontier::Sample{10, 100, 0, 0, 0});
    motion.Push(Frontier::Sample{12, 200, 0, 0, 0});
    motion.Push(Frontier::Sample{12, 250, 0, 0, 0});

    Assert::AreEqual(std::uint32_t{10}, motion.older.tick);
    Assert::AreEqual(std::uint32_t{12}, motion.newer.tick);
    Assert::AreEqual(250, motion.newer.x, L"the newer sample took the new value");
  }

  TEST_METHOD(APositionIsTheOlderSampleAtTheStartAndTheNewerAtTheEnd)
  {
    const Frontier::Motion motion = Segment(10, 400, 12, 800);
    // 400 wire units is 100 world units: a wire unit is a quarter of one.
    Assert::AreEqual(100.0f, Frontier::Evaluate(motion, Frontier::RenderTimeOfTick(10)).x, TOLERANCE);
    Assert::AreEqual(200.0f, Frontier::Evaluate(motion, Frontier::RenderTimeOfTick(12)).x, TOLERANCE);
  }

  TEST_METHOD(APositionHalfWayAlongIsHalfWayBetween)
  {
    const Frontier::Motion motion = Segment(10, 400, 12, 800);
    const std::int64_t half = Frontier::RenderTimeOfTick(10) + Frontier::RENDER_TIME_SCALE;
    Assert::AreEqual(150.0f, Frontier::Evaluate(motion, half).x, TOLERANCE);
  }

  /// THE GUARANTEE (TechnicalDesign.md §10). A render time off either end of the segment gives the
  /// end, and never a point beyond it: a client that has not been sent the next frame draws the
  /// last thing it was told rather than a guess that would have to be taken back.
  TEST_METHOD(ARenderTimeOutsideTheSegmentIsClampedToIt)
  {
    const Frontier::Motion motion = Segment(10, 400, 12, 800);
    const float atOlder = 100.0f;
    const float atNewer = 200.0f;

    for (const std::int64_t before : {Frontier::RenderTimeOfTick(0), Frontier::RenderTimeOfTick(9), Frontier::RenderTimeOfTick(10) - 1})
    {
      Assert::AreEqual(atOlder, Frontier::Evaluate(motion, before).x, TOLERANCE, L"never before the older sample");
    }
    for (const std::int64_t after :
         {Frontier::RenderTimeOfTick(12) + 1, Frontier::RenderTimeOfTick(40), Frontier::RenderTimeOfTick(100000)})
    {
      Assert::AreEqual(atNewer, Frontier::Evaluate(motion, after).x, TOLERANCE, L"never past the newer sample");
    }
  }

  /// The same guarantee stated as a sweep rather than at its two ends: over the whole timeline, and
  /// well outside it either way, every value stays inside the box the two samples make.
  TEST_METHOD(NoRenderTimeAnywhereEverLeavesTheSegment)
  {
    const Frontier::Motion motion = Segment(20, -1200, 22, 3600);
    const float low = -300.0f;
    const float high = 900.0f;
    for (std::int64_t time = Frontier::RenderTimeOfTick(0); time <= Frontier::RenderTimeOfTick(60); time += Frontier::RENDER_TIME_SCALE / 8)
    {
      const Frontier::Pose pose = Frontier::Evaluate(motion, time);
      Assert::IsTrue(pose.x >= low - TOLERANCE && pose.x <= high + TOLERANCE, L"inside the segment");
    }
  }

  /// A heading is a binary angle that wraps, so 250 to 10 is sixteen steps forward and not two
  /// hundred and forty back. The result is NOT reduced back under a turn - half way along reads as
  /// 258 steps and the end as 266, rather than 2 and 10 - because the only thing that consumes it
  /// is a sine and a cosine, which are periodic, and reducing it would put a discontinuity in the
  /// middle of the one place a turn crosses zero.
  TEST_METHOD(AHeadingTakesTheShortestWayRoundTheWrap)
  {
    const Frontier::Motion motion = Segment(10, 0, 12, 0, 250, 10);
    const float turn = 2.0f * 3.14159265358979323846f;
    const std::int64_t half = Frontier::RenderTimeOfTick(10) + Frontier::RENDER_TIME_SCALE;

    const float atHalf = Frontier::Evaluate(motion, half).headingRadians;
    Assert::AreEqual(258.0f / 256.0f * turn, atHalf, 0.01f, L"forward through the wrap, not back the long way");

    const float atEnd = Frontier::Evaluate(motion, Frontier::RenderTimeOfTick(12)).headingRadians;
    Assert::AreEqual(266.0f / 256.0f * turn, atEnd, 0.01f, L"and it arrives at the heading it was sent");
  }

  TEST_METHOD(AMotionWithOneSampleIsDrawnWhereThatSampleIs)
  {
    Frontier::Motion motion;
    motion.Push(Frontier::Sample{7, 1024, 512, -256, 64});
    for (const std::int64_t time : {std::int64_t{0}, Frontier::RenderTimeOfTick(7), Frontier::RenderTimeOfTick(900)})
    {
      const Frontier::Pose pose = Frontier::Evaluate(motion, time);
      Assert::AreEqual(256.0f, pose.x, TOLERANCE);
      Assert::AreEqual(128.0f, pose.y, TOLERANCE);
      Assert::AreEqual(-64.0f, pose.z, TOLERANCE);
    }
  }

  /// A hundred milliseconds is two ticks at twenty a second, which is exactly the host's publish
  /// interval. If either number moved without the other, the client would be interpolating between
  /// frames it does not have yet or ones it threw away.
  TEST_METHOD(TheDelayIsOnePublishIntervalAndAHundredMilliseconds)
  {
    Assert::AreEqual(2, Frontier::INTERPOLATION_DELAY_TICKS);
    Assert::AreEqual(100, Frontier::INTERPOLATION_DELAY_TICKS * 1000 / Neuron::TICKS_PER_SECOND, L"milliseconds");
  }
};

} // namespace ReplicaTests
