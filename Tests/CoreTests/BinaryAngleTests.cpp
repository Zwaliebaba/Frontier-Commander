#include "pch.h"

#include "BinaryAngle.h"
#include "SinTable.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace CoreTests
{

TEST_CLASS(BinaryAngleTests)
{
public:
  TEST_METHOD(SinAtEveryTableEntryIsTheTableValue)
  {
    for (std::uint32_t index = 0; index < Neuron::SIN_TABLE_16_16.size(); ++index)
    {
      const Neuron::BinaryAngle angle = static_cast<Neuron::BinaryAngle>(index << 6);
      Assert::AreEqual(Neuron::SIN_TABLE_16_16[index], Neuron::Sin(angle));
    }
  }

  TEST_METHOD(QuarterTurnsAreExact)
  {
    Assert::AreEqual(0, Neuron::Sin(0));
    Assert::AreEqual(65536, Neuron::Sin(Neuron::QUARTER_TURN));
    Assert::AreEqual(0, Neuron::Sin(Neuron::HALF_TURN));
    Assert::AreEqual(-65536, Neuron::Sin(static_cast<Neuron::BinaryAngle>(3 * Neuron::QUARTER_TURN)));
    Assert::AreEqual(65536, Neuron::Cos(0));
    Assert::AreEqual(0, Neuron::Cos(Neuron::QUARTER_TURN));
    Assert::AreEqual(-65536, Neuron::Cos(Neuron::HALF_TURN));
  }

  TEST_METHOD(SinIsAntisymmetricAcrossAHalfTurnWithinOneUnit)
  {
    for (std::uint32_t angle = 0; angle < Neuron::FULL_TURN; ++angle)
    {
      const auto here = Neuron::Sin(static_cast<Neuron::BinaryAngle>(angle));
      const auto opposite = Neuron::Sin(static_cast<Neuron::BinaryAngle>(angle + Neuron::HALF_TURN));
      Assert::IsTrue(std::abs(here + opposite) <= 1, L"sin(a) + sin(a + half turn) is not within one unit of zero");
    }
  }

  TEST_METHOD(SinIsWithinTwoUnitsOfTheRealSineEverywhere)
  {
    // The test may use float: it is not the simulation. Linear interpolation of 1,024 entries has
    // a worst case of about 0.3 units of 65,536 plus the table's rounding.
    int worst = 0;
    for (std::uint32_t angle = 0; angle < Neuron::FULL_TURN; ++angle)
    {
      const double real = std::sin(2.0 * 3.14159265358979323846 * static_cast<double>(angle) / 65536.0) * 65536.0;
      const int error = static_cast<int>(std::abs(static_cast<double>(Neuron::Sin(static_cast<Neuron::BinaryAngle>(angle))) - real));
      if (error > worst)
      {
        worst = error;
      }
    }
    Assert::IsTrue(worst <= 2, L"the interpolated sine strays more than two units from the real one");
  }

  TEST_METHOD(TurnsWrapTheShortWay)
  {
    Assert::AreEqual(-1, static_cast<int>(Neuron::TurnBetween(0, 65535)));
    Assert::AreEqual(1, static_cast<int>(Neuron::TurnBetween(65535, 0)));
    Assert::AreEqual(-32768, static_cast<int>(Neuron::TurnBetween(0, Neuron::HALF_TURN)));
    Assert::AreEqual(100, static_cast<int>(Neuron::TurnToward(65500, 100, 200)));
    Assert::AreEqual(1000, static_cast<int>(Neuron::TurnToward(0, Neuron::HALF_TURN, 1000)));
    Assert::AreEqual(64536, static_cast<int>(Neuron::TurnToward(0, 60000, 1000)));
    Assert::AreEqual(60000, static_cast<int>(Neuron::TurnToward(59990, 60000, 1000)));
  }
};

} // namespace CoreTests
