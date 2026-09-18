#include "pch.h"

#include "BinaryAngle.h"
#include "SinTable.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>

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

  TEST_METHOD(TheAngleOfADirectionIsTheInverseOfSinAndCos)
  {
    // Angle nought points along +y and a quarter turn along +x, which is the convention a device's
    // facing is read by (its forward is (Sin, Cos)). The four of them are exact.
    Assert::AreEqual(0, static_cast<int>(Neuron::AngleOf(0, 1000)));
    Assert::AreEqual(static_cast<int>(Neuron::QUARTER_TURN), static_cast<int>(Neuron::AngleOf(1000, 0)));
    Assert::AreEqual(static_cast<int>(Neuron::HALF_TURN), static_cast<int>(Neuron::AngleOf(0, -1000)));
    Assert::AreEqual(static_cast<int>(Neuron::HALF_TURN + Neuron::QUARTER_TURN), static_cast<int>(Neuron::AngleOf(-1000, 0)));
    // A direction with no direction in it.
    Assert::AreEqual(0, static_cast<int>(Neuron::AngleOf(0, 0)));

    // And everywhere: the angle it gives back for a direction taken from the table is the angle
    // that direction was made from. Exactly, not nearly, because a device turns to what this says
    // and then drives along Sin and Cos of it, and a unit of disagreement is a device that turns
    // back and forth for ever.
    int worst = 0;
    for (std::uint32_t angle = 0; angle < Neuron::FULL_TURN; angle += 7)
    {
      const auto turn = static_cast<Neuron::BinaryAngle>(angle);
      const std::int32_t found = Neuron::AngleOf(Neuron::Sin(turn), Neuron::Cos(turn));
      const int strayed = std::abs(static_cast<int>(Neuron::TurnBetween(turn, static_cast<Neuron::BinaryAngle>(found))));
      worst = std::max(worst, strayed);
    }
    Logger::WriteMessage((L"measured: the worst round trip through AngleOf is " + std::to_wstring(worst) + L" units").c_str());
    Assert::IsTrue(worst <= 1, L"the angle of a direction from the table is the angle it came from");

    // The scale of the direction does not change its angle, which is what lets a caller pass a
    // subunit difference of any size.
    Assert::AreEqual(static_cast<int>(Neuron::AngleOf(3, 7)), static_cast<int>(Neuron::AngleOf(3000000, 7000000)));
  }
};

} // namespace CoreTests
