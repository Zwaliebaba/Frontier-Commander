#include "pch.h"

#include "TickPacer.h"

#include "FixedPoint.h"

#include <chrono>
#include <cstdint>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// Wall time into fixed-rate steps (TechnicalDesign.md §3; m1-vertical-slice/G1a). The rule this
// file exists for is that a host which falls behind SLOWS rather than skips: every step it owes is
// still run, and what is thrown away when the debt runs away is wall time. A skipped tick would be
// a different match on a simulation whose hash both ends compare, so "did it skip one" is the
// question every case below is a form of.
namespace CoreTests
{

namespace
{

constexpr std::chrono::nanoseconds TICK{std::chrono::milliseconds{Neuron::TICK_MILLISECONDS}};

[[nodiscard]] Neuron::TickPacer Pacer(std::uint32_t _maxPerPass = 4, std::uint32_t _maxDebt = 20)
{
  return Neuron::TickPacer(TICK, _maxPerPass, _maxDebt);
}

} // namespace

TEST_CLASS(TickPacerTests)
{
public:
  TEST_METHOD(NothingElapsedIsNothingToRun)
  {
    Neuron::TickPacer pacer = Pacer();
    const Neuron::TickStep step = pacer.Take(std::chrono::nanoseconds{0});
    Assert::AreEqual(std::uint32_t{0}, step.ticks);
    Assert::AreEqual(std::uint32_t{0}, step.behind);
    Assert::AreEqual(std::uint32_t{0}, step.givenUp);
  }

  /// Less than a step owes nothing YET and does not lose it: two half ticks are one tick.
  TEST_METHOD(TimeShorterThanAStepIsKeptAndNotLost)
  {
    Neuron::TickPacer pacer = Pacer();
    Assert::AreEqual(std::uint32_t{0}, pacer.Take(TICK / 2).ticks);
    Assert::AreEqual(std::uint32_t{1}, pacer.Take(TICK / 2).ticks, L"the two halves made one");
    Assert::AreEqual(std::uint32_t{0}, pacer.Take(std::chrono::nanoseconds{0}).ticks);
  }

  TEST_METHOD(AStepsWorthIsExactlyOneStep)
  {
    Neuron::TickPacer pacer = Pacer();
    const Neuron::TickStep step = pacer.Take(TICK);
    Assert::AreEqual(std::uint32_t{1}, step.ticks);
    Assert::AreEqual(std::uint32_t{0}, step.behind, L"and it owes nothing after it");
  }

  /// A pass is bounded, so that a stall does not advance the world by a second in one burst - and
  /// so that a machine which cannot keep up at all still returns to its caller.
  TEST_METHOD(APassRunsAtMostItsLimitAndKeepsTheRest)
  {
    Neuron::TickPacer pacer = Pacer(4, 20);
    const Neuron::TickStep first = pacer.Take(TICK * 10);
    Assert::AreEqual(std::uint32_t{4}, first.ticks, L"four this pass");
    Assert::AreEqual(std::uint32_t{6}, first.behind, L"and six still owed");
    Assert::AreEqual(std::uint32_t{0}, first.givenUp, L"nothing written off: six is within the ceiling");

    Assert::AreEqual(std::uint32_t{4}, pacer.Take(std::chrono::nanoseconds{0}).ticks, L"the debt runs with no new time");
    Assert::AreEqual(std::uint32_t{2}, pacer.Take(std::chrono::nanoseconds{0}).ticks);
    Assert::AreEqual(std::uint32_t{0}, pacer.Take(std::chrono::nanoseconds{0}).ticks, L"and then it is caught up");
  }

  /// THE CASE THIS FILE EXISTS FOR. A debt past the ceiling gives up WALL TIME and never a step:
  /// the ten steps inside the ceiling are all still run, one pass at a time.
  TEST_METHOD(ADebtPastTheCeilingGivesUpWallTimeAndNeverAStep)
  {
    Neuron::TickPacer pacer = Pacer(4, 10);
    const Neuron::TickStep step = pacer.Take(TICK * 100);
    Assert::AreEqual(std::uint32_t{4}, step.ticks, L"this pass runs its limit");
    Assert::AreEqual(std::uint32_t{10}, step.behind, L"and the debt is clamped to the ceiling");
    Assert::AreEqual(std::uint32_t{86}, step.givenUp, L"100 less the 4 run less the 10 kept");

    // Every step still inside the ceiling is run, none of them skipped.
    std::uint32_t ran = 0;
    for (int pass = 0; pass < 10; ++pass)
    {
      ran += pacer.Take(std::chrono::nanoseconds{0}).ticks;
    }
    Assert::AreEqual(std::uint32_t{10}, ran, L"all ten owed steps ran");
    Assert::AreEqual(std::uint32_t{0}, pacer.Take(std::chrono::nanoseconds{0}).behind);
  }

  /// A clock that went backwards is a clock fault, not a reason to un-run a step.
  TEST_METHOD(TimeThatWentBackwardsAddsNothingAndTakesNothing)
  {
    Neuron::TickPacer pacer = Pacer();
    Assert::AreEqual(std::uint32_t{1}, pacer.Take(TICK).ticks);
    Assert::AreEqual(std::uint32_t{0}, pacer.Take(-TICK * 5).ticks, L"nothing to run");
    Assert::AreEqual(std::uint32_t{0}, pacer.Take(std::chrono::nanoseconds{0}).behind, L"and nothing owed either way");
    Assert::AreEqual(std::uint32_t{1}, pacer.Take(TICK).ticks, L"and the next real tick still runs");
  }

  /// A finished match advances nothing, so its debt has to be forgettable - otherwise a host would
  /// report falling further and further behind for as long as the window stayed open.
  TEST_METHOD(AForgottenDebtIsGoneAndTheClockStartsCleanAgain)
  {
    Neuron::TickPacer pacer = Pacer();
    Assert::AreEqual(std::uint32_t{4}, pacer.Take(TICK * 50).ticks);
    Assert::IsTrue(pacer.Owed() > std::chrono::nanoseconds{0});
    pacer.Forget();
    Assert::IsTrue(pacer.Owed() == std::chrono::nanoseconds{0});
    Assert::AreEqual(std::uint32_t{0}, pacer.Take(std::chrono::nanoseconds{0}).ticks);
    Assert::AreEqual(std::uint32_t{1}, pacer.Take(TICK).ticks, L"and it paces again from there");
  }

  /// REAL TIME OVER A LONG RUN. A hundred passes of exactly one tick's wall time run exactly a
  /// hundred ticks - no drift, because the remainder is kept in nanoseconds rather than rounded.
  TEST_METHOD(AHundredPassesOfATickRunAHundredTicksAndNotNinetyNine)
  {
    Neuron::TickPacer pacer = Pacer();
    std::uint32_t ran = 0;
    for (int pass = 0; pass < 100; ++pass)
    {
      ran += pacer.Take(TICK).ticks;
    }
    Assert::AreEqual(std::uint32_t{100}, ran);
    Assert::AreEqual(std::uint32_t{0}, pacer.Take(std::chrono::nanoseconds{0}).behind);
  }

  /// A rate that does not divide the pass evenly must not drift either: 100 passes of 51 ms against
  /// a 50 ms tick is 5,100 ms, which is 102 ticks, and the fractions have to accumulate to say so.
  TEST_METHOD(AnUnevenPassAccumulatesItsRemainderRatherThanLosingIt)
  {
    Neuron::TickPacer pacer = Pacer();
    std::uint32_t ran = 0;
    for (int pass = 0; pass < 100; ++pass)
    {
      ran += pacer.Take(std::chrono::milliseconds{51}).ticks;
    }
    ran += pacer.Take(std::chrono::nanoseconds{0}).ticks;
    Assert::AreEqual(std::uint32_t{102}, ran, L"5,100 ms of wall time is 102 ticks of 50");
  }

  /// A step of zero would divide by zero; it is refused into one nanosecond rather than crashing.
  TEST_METHOD(AStepOfNothingIsRefusedRatherThanDividingByZero)
  {
    Neuron::TickPacer pacer(std::chrono::nanoseconds{0}, 4, 20);
    const Neuron::TickStep step = pacer.Take(std::chrono::nanoseconds{10});
    Assert::AreEqual(std::uint32_t{4}, step.ticks, L"its limit, and no crash");
  }
};

} // namespace CoreTests
