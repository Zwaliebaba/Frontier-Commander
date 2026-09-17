#include "pch.h"

#include "ScaleMode.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace ClientTests
{

namespace
{

inline constexpr std::uint32_t AUTHORED_WIDTH = 1920;
inline constexpr std::uint32_t AUTHORED_HEIGHT = 1080;

Neuron::ScaledRectangle Fit(std::uint32_t _clientWidth, std::uint32_t _clientHeight)
{
  return Neuron::FitAuthored(_clientWidth, _clientHeight, AUTHORED_WIDTH, AUTHORED_HEIGHT);
}

} // namespace

TEST_CLASS(ScaleModeTests)
{
public:
  TEST_METHOD(AMatchingClientAreaIsExactAndUnmoved)
  {
    const Neuron::ScaledRectangle fit = Fit(1920, 1080);
    Assert::IsTrue(fit == Neuron::ScaledRectangle{Neuron::ScaleMode::Exact, 1, 0, 0, 1920, 1080});
  }

  TEST_METHOD(AWholeNumberMultipleIsIntegerScaled)
  {
    Assert::IsTrue(Fit(3840, 2160) == Neuron::ScaledRectangle{Neuron::ScaleMode::Integer, 2, 0, 0, 3840, 2160});
    Assert::IsTrue(Fit(5760, 3240) == Neuron::ScaledRectangle{Neuron::ScaleMode::Integer, 3, 0, 0, 5760, 3240});
    // A 4K monitor taller than 16:9 still doubles, with bars above and below.
    Assert::IsTrue(Fit(3840, 2400) == Neuron::ScaledRectangle{Neuron::ScaleMode::Integer, 2, 0, 120, 3840, 2160});
  }

  TEST_METHOD(AnUltrawideMonitorPillarboxesAtOneToOne)
  {
    Assert::IsTrue(Fit(2560, 1080) == Neuron::ScaledRectangle{Neuron::ScaleMode::Exact, 1, 320, 0, 1920, 1080});
  }

  TEST_METHOD(ATallerMonitorLetterboxesAtOneToOne)
  {
    Assert::IsTrue(Fit(1920, 1200) == Neuron::ScaledRectangle{Neuron::ScaleMode::Exact, 1, 0, 60, 1920, 1080});
  }

  TEST_METHOD(AnythingElseIsBilinearAndKeepsTheAspect)
  {
    Assert::IsTrue(Fit(2560, 1440) == Neuron::ScaledRectangle{Neuron::ScaleMode::Bilinear, 0, 0, 0, 2560, 1440});
    Assert::IsTrue(Fit(1280, 720) == Neuron::ScaledRectangle{Neuron::ScaleMode::Bilinear, 0, 0, 0, 1280, 720});
    Assert::IsTrue(Fit(800, 600) == Neuron::ScaledRectangle{Neuron::ScaleMode::Bilinear, 0, 0, 75, 800, 450});
    Assert::IsTrue(Fit(1000, 1000) == Neuron::ScaledRectangle{Neuron::ScaleMode::Bilinear, 0, 0, 219, 1000, 562});
    Assert::IsTrue(Fit(3000, 1000) == Neuron::ScaledRectangle{Neuron::ScaleMode::Bilinear, 0, 611, 0, 1777, 1000});
  }

  TEST_METHOD(ADegenerateSizeGivesAnEmptyRectangle)
  {
    Assert::IsTrue(Fit(0, 1080) == Neuron::ScaledRectangle{Neuron::ScaleMode::Bilinear, 0, 0, 0, 0, 0});
    Assert::IsTrue(Neuron::FitAuthored(1920, 1080, 0, 0) == Neuron::ScaledRectangle{Neuron::ScaleMode::Bilinear, 0, 0, 0, 0, 0});
  }
};

} // namespace ClientTests
