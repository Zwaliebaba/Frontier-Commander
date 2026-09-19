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

  /// The round trip Design/Interface.md §11 row 1 asks of AuthoredFromClient, at all three scale
  /// modes: the fit's corners are the authored corners, and every authored pixel comes back as
  /// itself through the middle of its client footprint.
  TEST_METHOD(AuthoredFromClientIsTheInverseOfFitAuthoredAtEveryScaleMode)
  {
    struct Window
    {
      std::uint32_t width;
      std::uint32_t height;
      Neuron::ScaleMode mode;
    };
    for (const Window& window : {Window{1920, 1080, Neuron::ScaleMode::Exact}, Window{3840, 2160, Neuron::ScaleMode::Integer},
                                 Window{2560, 1440, Neuron::ScaleMode::Bilinear}, Window{1920, 1200, Neuron::ScaleMode::Exact}})
    {
      const Neuron::ScaledRectangle fit = Fit(window.width, window.height);
      Assert::IsTrue(fit.mode == window.mode, L"the case is the scale mode it says it is");

      Neuron::AuthoredPosition at{};
      Assert::IsTrue(Neuron::AuthoredFromClient(fit, fit.x, fit.y, AUTHORED_WIDTH, AUTHORED_HEIGHT, at));
      Assert::AreEqual(0, at.x);
      Assert::AreEqual(0, at.y);

      Assert::IsTrue(Neuron::AuthoredFromClient(fit, fit.x + static_cast<std::int32_t>(fit.width) - 1,
                                                fit.y + static_cast<std::int32_t>(fit.height) - 1, AUTHORED_WIDTH, AUTHORED_HEIGHT, at));
      Assert::AreEqual(static_cast<std::int32_t>(AUTHORED_WIDTH) - 1, at.x);
      Assert::AreEqual(static_cast<std::int32_t>(AUTHORED_HEIGHT) - 1, at.y);

      // Every authored column, back through the middle of the client pixels it covers. Exact and
      // Integer are lossless; Bilinear is within one authored pixel, which a non-integral scale
      // cannot better and which is why this asserts a bound rather than equality.
      for (std::int32_t authored = 0; authored < static_cast<std::int32_t>(AUTHORED_WIDTH); authored += 7)
      {
        const std::int32_t clientX = fit.x + static_cast<std::int32_t>(static_cast<std::int64_t>(authored) * fit.width / AUTHORED_WIDTH);
        Neuron::AuthoredPosition back{};
        Assert::IsTrue(Neuron::AuthoredFromClient(fit, clientX, fit.y, AUTHORED_WIDTH, AUTHORED_HEIGHT, back));
        const std::int32_t error = back.x > authored ? back.x - authored : authored - back.x;
        Assert::IsTrue(error <= 1, L"the round trip is within an authored pixel");
      }
    }
  }

  /// §4: a pointer in the letterbox bars is over no panel and over no world, and every hit test
  /// refuses it. Clamping instead would read as the player hovering the outermost widget whenever
  /// the mouse left the picture.
  TEST_METHOD(APointerOutsideTheFitIsRefusedRatherThanClamped)
  {
    const Neuron::ScaledRectangle fit = Fit(1920, 1200); // 60 rows of bar, top and bottom
    Assert::AreEqual(60, fit.y);

    Neuron::AuthoredPosition at{};
    Assert::IsFalse(Neuron::AuthoredFromClient(fit, 100, 0, AUTHORED_WIDTH, AUTHORED_HEIGHT, at), L"above the picture");
    Assert::IsFalse(Neuron::AuthoredFromClient(fit, 100, 1199, AUTHORED_WIDTH, AUTHORED_HEIGHT, at), L"below it");
    Assert::IsFalse(Neuron::AuthoredFromClient(fit, -1, 100, AUTHORED_WIDTH, AUTHORED_HEIGHT, at), L"off the window");
    Assert::IsTrue(Neuron::AuthoredFromClient(fit, 0, 60, AUTHORED_WIDTH, AUTHORED_HEIGHT, at), L"and the first row inside");
    Assert::AreEqual(0, at.y);

    Assert::IsFalse(Neuron::AuthoredFromClient(Neuron::ScaledRectangle{}, 0, 0, AUTHORED_WIDTH, AUTHORED_HEIGHT, at),
                    L"an empty fit is over nothing");
  }
};

} // namespace ClientTests
