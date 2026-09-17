#include "pch.h"

#include "FixedPoint.h"
#include "Point.h"

#include <cstdint>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace CoreTests
{

TEST_CLASS(FixedPointTests)
{
public:
  TEST_METHOD(SqrtIsTheFloorRootExhaustivelyUpToTwoToTheTwenty)
  {
    for (std::uint64_t value = 0; value < (std::uint64_t{1} << 20); ++value)
    {
      const std::uint64_t root = Neuron::Sqrt(value);
      Assert::IsTrue(root * root <= value, L"root squared exceeds the value");
      Assert::IsTrue((root + 1) * (root + 1) > value, L"the next root's square does not exceed the value");
    }
  }

  TEST_METHOD(SqrtAtTheLimitsAndAbove)
  {
    Assert::AreEqual(0xFFFFFFFFu, Neuron::Sqrt(UINT64_MAX));
    const std::uint64_t largest = 0xFFFFFFFFull;
    Assert::AreEqual(0xFFFFFFFFu, Neuron::Sqrt(largest * largest));
    Assert::AreEqual(0xFFFFFFFEu, Neuron::Sqrt(largest * largest - 1));
    Assert::AreEqual(1048576u, Neuron::Sqrt(std::uint64_t{1} << 40));
    Assert::AreEqual(1000000000u, Neuron::Sqrt(1000000000000000000ull));
    Assert::AreEqual(999999999u, Neuron::Sqrt(999999999999999999ull));
  }

  TEST_METHOD(MulDivWidensToSixtyFourBits)
  {
    Assert::AreEqual(INT32_MAX, Neuron::MulDiv(INT32_MAX, INT32_MAX, INT32_MAX));
    Assert::AreEqual(2048, Neuron::MulDiv(1 << 20, 1 << 11, 1 << 20));
    Assert::AreEqual(-42, Neuron::MulDiv(-100, 3, 7)); // truncated toward zero, as C++ divides
    Assert::AreEqual(42, Neuron::MulDiv(-100, -3, 7));
    Assert::AreEqual(0, Neuron::MulDiv(1, 1, 2));
  }

  TEST_METHOD(MulShiftFloorsANegativeProduct)
  {
    Assert::AreEqual(15 << 16, Neuron::MulShift(3 << 16, 5 << 16, 16));
    Assert::AreEqual(-1, Neuron::MulShift(-1, 1, 1));
    Assert::AreEqual(-1, Neuron::MulShift(-1, 1, 0));
    Assert::AreEqual(INT32_MAX, Neuron::MulShift(INT32_MAX, 1 << 30, 30));
  }

  TEST_METHOD(FloorDivRoundsTowardNegativeInfinity)
  {
    Assert::AreEqual(std::int64_t{3}, Neuron::FloorDiv(7, 2));
    Assert::AreEqual(std::int64_t{-4}, Neuron::FloorDiv(-7, 2));
    Assert::AreEqual(std::int64_t{-4}, Neuron::FloorDiv(-8, 2));
    Assert::AreEqual(std::int64_t{0}, Neuron::FloorDiv(0, 5));
    Assert::AreEqual(std::int64_t{-1}, Neuron::FloorDiv(-1, 1000000));
  }

  TEST_METHOD(DotAndLengthWiden)
  {
    Assert::AreEqual(std::int64_t{2} * INT32_MAX * INT32_MAX, Neuron::LengthSquared(INT32_MAX, INT32_MAX));
    Assert::AreEqual(std::int64_t{-1}, Neuron::Dot(1, 0, -1, 5));
    Assert::AreEqual(5u << 8, Neuron::Length(3 << 8, 4 << 8));
    Assert::AreEqual(0u, Neuron::Length(0, 0));
  }

  TEST_METHOD(CellsAreAFloorShift)
  {
    Assert::AreEqual(0, Neuron::CellOf(0));
    Assert::AreEqual(0, Neuron::CellOf(Neuron::SUBUNITS_PER_CELL - 1));
    Assert::AreEqual(1, Neuron::CellOf(Neuron::SUBUNITS_PER_CELL));
    Assert::AreEqual(-1, Neuron::CellOf(-1));
    Assert::AreEqual(-2, Neuron::CellOf(-Neuron::SUBUNITS_PER_CELL - 1));
    Assert::AreEqual(3 * Neuron::SUBUNITS_PER_CELL, Neuron::SubunitsOfCell(3));
    Assert::AreEqual(64 * 256, Neuron::SUBUNITS_PER_CELL);
  }

  TEST_METHOD(RectanglesAreHalfOpen)
  {
    const Neuron::Rect rect{{0, 0}, {10, 20}};
    Assert::IsTrue(rect.Contains({0, 0}));
    Assert::IsTrue(rect.Contains({9, 19}));
    Assert::IsFalse(rect.Contains({10, 0}));
    Assert::IsFalse(rect.Contains({0, 20}));
    Assert::IsFalse(rect.Contains({-1, 5}));
    Assert::AreEqual(10, rect.Width());
    Assert::AreEqual(20, rect.Height());
    Assert::IsTrue(rect.Overlaps({{9, 19}, {30, 30}}));
    Assert::IsFalse(rect.Overlaps({{10, 0}, {30, 30}}));
    Assert::IsFalse(rect.Overlaps({{0, 20}, {30, 30}}));
    const Neuron::Point2 sum = Neuron::Point2{1, 2} + Neuron::Point2{3, 4};
    Assert::IsTrue(sum == Neuron::Point2{4, 6});
    Assert::AreEqual(std::int64_t{25}, Neuron::DistanceSquared({0, 0}, {3, 4}));
  }
};

} // namespace CoreTests
