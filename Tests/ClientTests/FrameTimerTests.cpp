#include "pch.h"

#include "FrameTimer.h"

#include <cstdint>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// The frame time as a number that can be read off a running build (m1-vertical-slice/K5).
// m0-foundation/T22 asked the owner for one and got the display's refresh, because nothing in the
// client measured or reported a frame. What is under test is the statistic: a percentile that
// always names a frame that happened, over a window that means the same thing at every frame rate.
namespace ClientTests
{

TEST_CLASS(FrameTimerTests)
{
public:
  TEST_METHOD(AnEmptyTimerReportsZeroRatherThanDividingByIt)
  {
    const Neuron::FrameTimer timer;
    Assert::AreEqual(std::size_t{0}, timer.Count());
    Assert::AreEqual(std::uint64_t{0}, timer.MeanMicroseconds());
    Assert::AreEqual(std::uint64_t{0}, timer.MedianMicroseconds());
    Assert::AreEqual(std::uint64_t{0}, timer.PercentileMicroseconds(99));
  }

  TEST_METHOD(ThePercentileIsTheNearestRankOverAKnownSequence)
  {
    // One hundred frames of 1 to 100 microseconds. Nearest rank puts the p-th percentile at
    // ceil(p * 100 / 100) = p, one-based, which over 1..100 is the value p itself.
    Neuron::FrameTimer timer;
    for (std::uint64_t value = 1; value <= 100; ++value)
    {
      timer.Add(value);
    }
    Assert::AreEqual(std::size_t{100}, timer.Count());
    Assert::AreEqual(std::uint64_t{50}, timer.PercentileMicroseconds(50), L"the median");
    Assert::AreEqual(std::uint64_t{50}, timer.MedianMicroseconds());
    Assert::AreEqual(std::uint64_t{99}, timer.PercentileMicroseconds(99));
    Assert::AreEqual(std::uint64_t{100}, timer.PercentileMicroseconds(100), L"the worst frame, not one past it");
    Assert::AreEqual(std::uint64_t{1}, timer.PercentileMicroseconds(0), L"the best, never an index of minus one");
    Assert::AreEqual(std::uint64_t{50}, timer.MeanMicroseconds(), L"(1 + ... + 100) / 100 = 50.5, floored");
  }

  TEST_METHOD(AWindowNotYetFullReportsOverWhatItHasAndNotOverItsZeroes)
  {
    // The case the acceptance names, and the one that would silently halve every figure: a ring of
    // 240 slots holding three frames must not average in 237 zeroes.
    Neuron::FrameTimer timer;
    timer.Add(10);
    timer.Add(20);
    timer.Add(30);
    Assert::AreEqual(std::size_t{3}, timer.Count());
    Assert::AreEqual(std::uint64_t{20}, timer.MeanMicroseconds());
    Assert::AreEqual(std::uint64_t{20}, timer.MedianMicroseconds());
    Assert::AreEqual(std::uint64_t{30}, timer.PercentileMicroseconds(99));
    Assert::AreEqual(std::uint64_t{10}, timer.PercentileMicroseconds(1), L"ceil(1 * 3 / 100) is rank 1");
  }

  TEST_METHOD(TheWindowIsACountSoTheOldestFrameFallsOutAndTheStatisticStaysComparable)
  {
    // A window of "the last second" holds 60 samples at 60 frames a second and 500 at 500, so its
    // 99th percentile means a different thing at each rate. A fixed count always reports the same
    // statistic, and that is what makes two runs comparable.
    Neuron::FrameTimer timer;
    for (std::size_t frame = 0; frame < Neuron::FrameTimer::WINDOW_FRAMES; ++frame)
    {
      timer.Add(1000);
    }
    Assert::AreEqual(std::uint64_t{1000}, timer.MeanMicroseconds());
    Assert::AreEqual(Neuron::FrameTimer::WINDOW_FRAMES, timer.Count());

    // One more window of a different value: the first is gone entirely, not blended with.
    for (std::size_t frame = 0; frame < Neuron::FrameTimer::WINDOW_FRAMES; ++frame)
    {
      timer.Add(2000);
    }
    Assert::AreEqual(Neuron::FrameTimer::WINDOW_FRAMES, timer.Count(), L"the window does not grow");
    Assert::AreEqual(std::uint64_t{2000}, timer.MeanMicroseconds(), L"and holds only the newest frames");
    Assert::AreEqual(std::uint64_t{2} * Neuron::FrameTimer::WINDOW_FRAMES, timer.TotalFrames(), L"though it counts them all");
  }

  TEST_METHOD(OneSlowFrameMovesThePercentileAndBarelyMovesTheMedian)
  {
    // Which is the whole reason three figures are reported rather than one: a hitch is invisible
    // in the mean and in the median, and it is what a player feels.
    Neuron::FrameTimer timer;
    for (std::size_t frame = 0; frame < 199; ++frame)
    {
      timer.Add(4000);
    }
    timer.Add(40000);
    Assert::AreEqual(std::uint64_t{4000}, timer.MedianMicroseconds(), L"the median does not notice");
    Assert::AreEqual(std::uint64_t{40000}, timer.PercentileMicroseconds(100), L"the worst frame does");
    Assert::IsTrue(timer.MeanMicroseconds() < 4200, L"and the mean hides it: 180 us on a 4 ms frame");
  }

  TEST_METHOD(ClearingReturnsItToEmpty)
  {
    Neuron::FrameTimer timer;
    timer.Add(5);
    timer.Clear();
    Assert::AreEqual(std::size_t{0}, timer.Count());
    Assert::AreEqual(std::uint64_t{0}, timer.TotalFrames());
    Assert::AreEqual(std::uint64_t{0}, timer.MeanMicroseconds());
  }
};

} // namespace ClientTests
