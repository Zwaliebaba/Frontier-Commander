#include "pch.h"

#include "UiLayout.h"

#include <cstdint>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// The arithmetic every panel is built out of (m1-vertical-slice/K3; Design/Interface.md §2, §3).
// The rectangles below are §2's own, so a change to that table that nobody carried into the code
// fails here rather than at 1:1 on somebody's screen.
namespace ClientTests
{

namespace
{

/// The selection panel of Design/Interface.md §2, which is the one every case here is worked on.
constexpr Neuron::UiRect SELECTION_PANEL{288, 792, 512, 288};

} // namespace

TEST_CLASS(UiLayoutTests)
{
public:
  TEST_METHOD(ARectangleIsHalfOpenSoTwoThatTouchShareNoPixel)
  {
    constexpr Neuron::UiRect LEFT{0, 0, 10, 10};
    constexpr Neuron::UiRect RIGHT{10, 0, 10, 10};
    Assert::IsTrue(LEFT.Contains(9, 0), L"the last pixel inside");
    Assert::IsFalse(LEFT.Contains(10, 0), L"and the first outside");
    Assert::IsTrue(RIGHT.Contains(10, 0), L"which is the next rectangle's first");
    Assert::AreEqual(10, LEFT.Right());
    Assert::IsTrue(Neuron::UiRect{}.Empty());
  }

  TEST_METHOD(TheClientAreaIsInsideTheChromeOfTheDocument)
  {
    // §3: inset 8 left, right and bottom, and 24 from the top, which is the title strip plus 8.
    const Neuron::UiRect client = Neuron::ClientAreaOf(SELECTION_PANEL);
    Assert::IsTrue(client == Neuron::UiRect{296, 816, 496, 256});
    Assert::AreEqual(SELECTION_PANEL.Right() - Neuron::PANEL_INSET_PIXELS, client.Right());
    Assert::AreEqual(SELECTION_PANEL.Bottom() - Neuron::PANEL_INSET_PIXELS, client.Bottom());
  }

  TEST_METHOD(TheTitleStripSitsInsideTheBorder)
  {
    const Neuron::UiRect title = Neuron::TitleStripOf(SELECTION_PANEL);
    Assert::IsTrue(title == Neuron::UiRect{289, 793, 510, 20});
  }

  /// A panel too small to hold its own chrome gives nothing back, and nothing is {0,0,0,0} rather
  /// than a rectangle that wraps around itself and draws across the whole frame.
  TEST_METHOD(APanelTooSmallForItsChromeIsEmptyAndNotInsideOut)
  {
    Assert::IsTrue(Neuron::ClientAreaOf(Neuron::UiRect{0, 0, 8, 8}).Empty());
    Assert::IsTrue(Neuron::ClientAreaOf(Neuron::UiRect{0, 0, 4, 200}).Empty());
    Assert::IsTrue(Neuron::Inset(Neuron::UiRect{0, 0, 10, 10}, 6).Empty());
  }

  TEST_METHOD(RowsStackAndStopAtTheBottomOfTheArea)
  {
    const Neuron::UiRect client = Neuron::ClientAreaOf(SELECTION_PANEL);
    Assert::IsTrue(Neuron::RowIn(client, 0, 24, 4) == Neuron::UiRect{296, 816, 496, 24});
    Assert::IsTrue(Neuron::RowIn(client, 1, 24, 4) == Neuron::UiRect{296, 844, 496, 24}, L"the spacing is cleared");
    Assert::IsTrue(Neuron::RowIn(client, 99, 24, 4).Empty(), L"a row past the bottom is not drawn outside");
    Assert::IsTrue(Neuron::RowIn(client, -1, 24, 4).Empty());
  }

  /// The remainder of a division that does not come out even goes to the leftmost columns, one
  /// pixel each. The reason is visible in the last assertion: the columns end exactly on the
  /// client area's edge, where dropping the remainder would leave a gap nothing else has.
  TEST_METHOD(ColumnsSpendTheirRemainderAndEndOnTheEdge)
  {
    const Neuron::UiRect client = Neuron::ClientAreaOf(SELECTION_PANEL); // 496 wide
    const Neuron::UiRect first = Neuron::ColumnIn(client, 0, 3, 4);
    const Neuron::UiRect second = Neuron::ColumnIn(client, 1, 3, 4);
    const Neuron::UiRect third = Neuron::ColumnIn(client, 2, 3, 4);

    // 496 less two gaps of 4 is 488; 488 over 3 is 162 with 2 left, so two columns are 163.
    Assert::AreEqual(163, first.width);
    Assert::AreEqual(163, second.width);
    Assert::AreEqual(162, third.width);
    Assert::AreEqual(first.Right() + 4, second.x, L"the spacing sits between them");
    Assert::AreEqual(second.Right() + 4, third.x);
    Assert::AreEqual(client.Right(), third.Right(), L"and the last ends on the edge");

    Assert::IsTrue(Neuron::ColumnIn(client, 3, 3, 4).Empty(), L"a column the row does not have");
    Assert::IsTrue(Neuron::ColumnIn(client, 0, 0, 4).Empty());
  }

  TEST_METHOD(AColumnThatDividesEvenlyHasNoRemainderToSpend)
  {
    constexpr Neuron::UiRect AREA{0, 0, 400, 20};
    for (std::int32_t index = 0; index < 4; ++index)
    {
      Assert::AreEqual(100, Neuron::ColumnIn(AREA, index, 4, 0).width);
      Assert::AreEqual(index * 100, Neuron::ColumnIn(AREA, index, 4, 0).x);
    }
  }

  /// A bar is drawn at the value the frame has and never animates toward it (§3), so the only
  /// thing worth pinning is the arithmetic - and the one reading that would be actively wrong.
  TEST_METHOD(ABarFillsFromTheLeftAndNothingOutOfNothingIsEmpty)
  {
    constexpr Neuron::UiRect BAR{100, 50, 200, Neuron::BAR_HEIGHT_PIXELS};
    Assert::AreEqual(0, Neuron::FilledPartOf(BAR, 0, 100).width);
    Assert::AreEqual(100, Neuron::FilledPartOf(BAR, 50, 100).width);
    Assert::AreEqual(200, Neuron::FilledPartOf(BAR, 100, 100).width);
    Assert::AreEqual(200, Neuron::FilledPartOf(BAR, 500, 100).width, L"over full is full");
    Assert::AreEqual(100, Neuron::FilledPartOf(BAR, 50, 100).x, L"and it fills from the left");
    Assert::IsTrue(Neuron::FilledPartOf(BAR, 5, 0).Empty(), L"nothing out of nothing is not everything");
    Assert::IsTrue(Neuron::FilledPartOf(BAR, -5, 100).Empty());
  }

  /// A bar of a thousand pixels against a stockpile in hundredths of power leaves 32 bits behind,
  /// which is why the multiply is done in 64.
  TEST_METHOD(ABarDoesNotOverflowOnASimulationSizedValue)
  {
    constexpr Neuron::UiRect BAR{0, 0, 1000, 12};
    Assert::AreEqual(500, Neuron::FilledPartOf(BAR, 1000000, 2000000).width);
    Assert::AreEqual(250, Neuron::FilledPartOf(BAR, 500000000, 2000000000).width);
  }

  TEST_METHOD(ATooltipGoesDownAndRightAndFlipsRatherThanLeavingTheFrame)
  {
    Assert::IsTrue(Neuron::TooltipRect(100, 100, 200, 40) == Neuron::UiRect{116, 116, 200, 40});

    const Neuron::UiRect nearTheCorner = Neuron::TooltipRect(1900, 1070, 200, 40);
    Assert::IsTrue(nearTheCorner.Right() <= static_cast<std::int32_t>(Neuron::AUTHORED_WIDTH_PIXELS));
    Assert::IsTrue(nearTheCorner.Bottom() <= static_cast<std::int32_t>(Neuron::AUTHORED_HEIGHT_PIXELS));
    Assert::IsTrue(nearTheCorner.x < 1900, L"flipped to the other side of the pointer");

    // Wider than the frame: there is nowhere to flip to, so it is pinned rather than drawn off.
    const Neuron::UiRect huge = Neuron::TooltipRect(900, 500, 4000, 40);
    Assert::AreEqual(0, huge.x);
  }
};

} // namespace ClientTests
