#include "pch.h"

#include "FogPass.h"

#include <cstdint>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// The fog pass needs a device, so what is tested here is the one part of it that does not: which
// rows of the grid a frame copies (m1-vertical-slice/K2). Getting this wrong does not crash, it
// leaves a row of the map showing last frame's fog, which nobody would notice in a capture and a
// player would read as vision they do not have.
namespace ClientTests
{

namespace
{

[[nodiscard]] std::vector<Neuron::FogRowRun> Runs(const std::vector<std::uint32_t>& _rows, std::uint32_t _cellsPerSide = 128)
{
  return Neuron::CoalesceFogRows(_rows, _cellsPerSide);
}

} // namespace

TEST_CLASS(FogPassTests)
{
public:
  TEST_METHOD(NothingChangedIsNoCopyAtAll)
  {
    Assert::AreEqual(std::size_t{0}, Runs({}).size());
  }

  TEST_METHOD(ConsecutiveRowsBecomeOneRun)
  {
    const std::vector<Neuron::FogRowRun> runs = Runs({4, 5, 6, 7});
    Assert::AreEqual(std::size_t{1}, runs.size());
    Assert::IsTrue(runs[0] == Neuron::FogRowRun{4, 4});
  }

  TEST_METHOD(AGapStartsANewRun)
  {
    const std::vector<Neuron::FogRowRun> runs = Runs({0, 1, 3, 9, 10});
    Assert::AreEqual(std::size_t{3}, runs.size());
    Assert::IsTrue(runs[0] == Neuron::FogRowRun{0, 2});
    Assert::IsTrue(runs[1] == Neuron::FogRowRun{3, 1});
    Assert::IsTrue(runs[2] == Neuron::FogRowRun{9, 2});
  }

  TEST_METHOD(EveryRowChangedIsOneRunOverTheWholeGrid)
  {
    std::vector<std::uint32_t> all(128);
    for (std::uint32_t row = 0; row < 128; ++row)
    {
      all[row] = row;
    }
    const std::vector<Neuron::FogRowRun> runs = Runs(all);
    Assert::AreEqual(std::size_t{1}, runs.size());
    Assert::IsTrue(runs[0] == Neuron::FogRowRun{0, 128});
  }

  TEST_METHOD(ARowPastTheGridIsDroppedAndDoesNotJoinTheRunBeforeIt)
  {
    // The last row of a 128-cell grid is 127; 128 and beyond are somebody else's bug and the pass
    // is not going to copy from an offset it never wrote to.
    const std::vector<Neuron::FogRowRun> runs = Runs({126, 127, 128, 129});
    Assert::AreEqual(std::size_t{1}, runs.size());
    Assert::IsTrue(runs[0] == Neuron::FogRowRun{126, 2});
  }

  TEST_METHOD(OutOfOrderRowsCostRunsAndNeverCorrectness)
  {
    // The view promises ascending without repeats. If it lies, each row still copies from its own
    // offset - the result is more regions, not a row read from the wrong place.
    const std::vector<Neuron::FogRowRun> runs = Runs({7, 3, 4, 3});
    Assert::AreEqual(std::size_t{3}, runs.size());
    Assert::IsTrue(runs[0] == Neuron::FogRowRun{7, 1});
    Assert::IsTrue(runs[1] == Neuron::FogRowRun{3, 2});
    Assert::IsTrue(runs[2] == Neuron::FogRowRun{3, 1});
    std::uint32_t rows = 0;
    for (const Neuron::FogRowRun& run : runs)
    {
      rows += run.count;
    }
    Assert::AreEqual(std::uint32_t{4}, rows, L"every row asked for is still copied");
  }
};

} // namespace ClientTests
