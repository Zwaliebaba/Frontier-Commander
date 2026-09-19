#include "pch.h"

#include "UiInputSink.h"

#include <cstdint>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// The consume rule (m1-vertical-slice/K3; Design/Interface.md §4; TechnicalDesign.md §6.5): the
// interface is the router's first sink, a widget that acts on an event consumes it, and a panel
// under the cursor consumes clicks so they never reach selection.
//
// STUB EVENTS, NO WINDOW AND NO DEVICE. Every case here is an InputEvent built by hand, which is
// what the acceptance asks for and what makes the whole rule testable without a machine that can
// draw.
namespace ClientTests
{

namespace
{

[[nodiscard]] Neuron::InputEvent Move(std::int32_t _x, std::int32_t _y)
{
  Neuron::InputEvent event{};
  event.kind = Neuron::InputEventKind::MouseMove;
  event.x = _x;
  event.y = _y;
  return event;
}

[[nodiscard]] Neuron::InputEvent Down(Neuron::MouseButton _button)
{
  Neuron::InputEvent event{};
  event.kind = Neuron::InputEventKind::MouseButtonDown;
  event.button = _button;
  return event;
}

[[nodiscard]] Neuron::InputEvent Up(Neuron::MouseButton _button)
{
  Neuron::InputEvent event{};
  event.kind = Neuron::InputEventKind::MouseButtonUp;
  event.button = _button;
  return event;
}

[[nodiscard]] Neuron::InputEvent Character(std::uint32_t _character)
{
  Neuron::InputEvent event{};
  event.kind = Neuron::InputEventKind::Character;
  event.character = _character;
  return event;
}

/// A panel laid out like the selection panel of §2, with one of each interactive kind in it.
struct Fixture
{
  Neuron::UiPanel panel{Neuron::UiRect{288, 792, 512, 288}, "SELECTION"};
  Neuron::UiInputSink sink;
  std::uint32_t button = 0;
  std::uint32_t toggle = 0;
  std::uint32_t list = 0;
  std::uint32_t field = 0;

  Fixture()
  {
    Neuron::UiWidget widget{};
    widget.kind = Neuron::UiWidgetKind::Button;
    widget.rect = Neuron::UiRect{300, 820, 120, 24};
    widget.text = "STOP";
    button = panel.Add(widget);

    widget = Neuron::UiWidget{};
    widget.kind = Neuron::UiWidgetKind::Toggle;
    widget.rect = Neuron::UiRect{300, 850, 120, 24};
    widget.text = "HOLD";
    toggle = panel.Add(widget);

    widget = Neuron::UiWidget{};
    widget.kind = Neuron::UiWidgetKind::List;
    widget.rect = Neuron::UiRect{300, 880, 200, 100};
    widget.maximum = 4;
    widget.rowHeightPixels = 20;
    list = panel.Add(widget);

    widget = Neuron::UiWidget{};
    widget.kind = Neuron::UiWidgetKind::TextField;
    widget.rect = Neuron::UiRect{300, 990, 200, 24};
    widget.textLimit = 8;
    field = panel.Add(widget);

    widget = Neuron::UiWidget{};
    widget.kind = Neuron::UiWidgetKind::Label;
    widget.rect = Neuron::UiRect{300, 800, 200, 16};
    widget.text = "READOUT";
    panel.Add(widget);

    // A 1:1 window, so that authored and client are the same and the arithmetic is out of the way;
    // ScaleModeTests is where the conversion itself is pinned.
    sink.SetFit(Neuron::FitAuthored(Neuron::AUTHORED_WIDTH_PIXELS, Neuron::AUTHORED_HEIGHT_PIXELS, Neuron::AUTHORED_WIDTH_PIXELS,
                                    Neuron::AUTHORED_HEIGHT_PIXELS));
    sink.AddPanel(&panel);
  }

  /// A press and a release at one place, which is what a click is.
  Neuron::UiEventResult Click(std::int32_t _x, std::int32_t _y)
  {
    (void)sink.OnInputEvent(Move(_x, _y));
    (void)sink.OnInputEvent(Down(Neuron::MouseButton::Left));
    (void)sink.OnInputEvent(Up(Neuron::MouseButton::Left));
    return sink.Take();
  }
};

} // namespace

TEST_CLASS(UiInputSinkTests)
{
public:
  /// The rule the whole file is for: a click on the world reaches the game, a click on a panel does
  /// not. Without it the player orders a unit to walk to whatever is behind the readout he clicked.
  TEST_METHOD(AClickOnThePanelIsConsumedAndAClickOnTheWorldIsNot)
  {
    Fixture fixture;
    Assert::IsTrue(fixture.sink.OnInputEvent(Move(960, 400)) == Neuron::InputDisposition::Ignored, L"a move over the world");
    Assert::IsFalse(fixture.sink.PointerOverPanel());
    Assert::IsTrue(fixture.sink.OnInputEvent(Down(Neuron::MouseButton::Left)) == Neuron::InputDisposition::Ignored);
    Assert::IsTrue(fixture.sink.OnInputEvent(Up(Neuron::MouseButton::Left)) == Neuron::InputDisposition::Ignored);

    (void)fixture.sink.OnInputEvent(Move(310, 830));
    Assert::IsTrue(fixture.sink.PointerOverPanel(), L"which is what §5 refuses a picking ray for");
    Assert::IsTrue(fixture.sink.OnInputEvent(Down(Neuron::MouseButton::Left)) == Neuron::InputDisposition::Consumed);
    Assert::IsTrue(fixture.sink.OnInputEvent(Up(Neuron::MouseButton::Left)) == Neuron::InputDisposition::Consumed);
  }

  TEST_METHOD(AButtonActsOnTheReleaseAndIsHeldInBetween)
  {
    Fixture fixture;
    (void)fixture.sink.OnInputEvent(Move(310, 830));
    (void)fixture.sink.OnInputEvent(Down(Neuron::MouseButton::Left));
    Assert::IsTrue(fixture.panel.Find(fixture.button)->on, L"held, for the down fill of §3");
    Assert::IsTrue(fixture.sink.Take().action == Neuron::UiAction::None, L"and nothing has happened yet");

    (void)fixture.sink.OnInputEvent(Up(Neuron::MouseButton::Left));
    const Neuron::UiEventResult acted = fixture.sink.Take();
    Assert::IsTrue(acted.action == Neuron::UiAction::Pressed);
    Assert::AreEqual(fixture.button, acted.widget);
    Assert::IsFalse(fixture.panel.Find(fixture.button)->on);
  }

  /// Pressing a button and sliding off it before letting go is how anyone changes their mind, and a
  /// button that fired anyway takes that away.
  TEST_METHOD(SlidingOffAButtonBeforeTheReleaseCancelsIt)
  {
    Fixture fixture;
    (void)fixture.sink.OnInputEvent(Move(310, 830));
    (void)fixture.sink.OnInputEvent(Down(Neuron::MouseButton::Left));
    (void)fixture.sink.OnInputEvent(Move(310, 950)); // off the button, still on the panel
    Assert::IsTrue(fixture.sink.OnInputEvent(Up(Neuron::MouseButton::Left)) == Neuron::InputDisposition::Consumed, L"still the panel's");
    Assert::IsTrue(fixture.sink.Take().action == Neuron::UiAction::None, L"but the button did not fire");
    Assert::IsFalse(fixture.panel.Find(fixture.button)->on, L"and it is let go");
  }

  TEST_METHOD(ARightClickOnAPanelIsConsumedAndDoesNothing)
  {
    Fixture fixture;
    (void)fixture.sink.OnInputEvent(Move(310, 830));
    Assert::IsTrue(fixture.sink.OnInputEvent(Down(Neuron::MouseButton::Right)) == Neuron::InputDisposition::Consumed);
    Assert::IsTrue(fixture.sink.Take().action == Neuron::UiAction::None);
    Assert::IsFalse(fixture.panel.Find(fixture.button)->on, L"and it does not arm the button under it");
  }

  /// The middle button aims the camera (§12, ruling 3). Consuming it would take the camera away
  /// wherever the pointer happened to be resting.
  TEST_METHOD(TheMiddleButtonIsNeverTheInterfaces)
  {
    Fixture fixture;
    (void)fixture.sink.OnInputEvent(Move(310, 830));
    Assert::IsTrue(fixture.sink.OnInputEvent(Down(Neuron::MouseButton::Middle)) == Neuron::InputDisposition::Ignored);
    Assert::IsTrue(fixture.sink.OnInputEvent(Up(Neuron::MouseButton::Middle)) == Neuron::InputDisposition::Ignored);
  }

  TEST_METHOD(AToggleFlipsAndAListSelectsTheRowUnderThePointer)
  {
    Fixture fixture;
    Neuron::UiEventResult acted = fixture.Click(310, 860);
    Assert::IsTrue(acted.action == Neuron::UiAction::Toggled);
    Assert::AreEqual(1, acted.value);
    Assert::IsTrue(fixture.panel.Find(fixture.toggle)->on);

    acted = fixture.Click(310, 860);
    Assert::AreEqual(0, acted.value, L"and back off again");

    acted = fixture.Click(310, 925); // 880 + two rows of 20, then five into the third
    Assert::IsTrue(acted.action == Neuron::UiAction::Selected);
    Assert::AreEqual(2, acted.value);
    Assert::AreEqual(2, fixture.panel.Find(fixture.list)->value);
  }

  /// Four rows of twenty fill eighty of the list's hundred pixels; the rest is empty space, and the
  /// row under the pointer there is no row at all.
  TEST_METHOD(TheEmptySpaceUnderAListsRowsSelectsNothing)
  {
    Fixture fixture;
    (void)fixture.Click(310, 925);
    const Neuron::UiEventResult acted = fixture.Click(310, 970);
    Assert::IsTrue(acted.action == Neuron::UiAction::None);
    Assert::AreEqual(2, fixture.panel.Find(fixture.list)->value, L"and leaves the selection alone");
  }

  TEST_METHOD(AReadoutTakesNoClickAndShieldsNothing)
  {
    Fixture fixture;
    Assert::IsNull(fixture.panel.WidgetAt(310, 805), L"a label is not interactive");
  }

  /// A character with nobody typing is a key the game may have a binding for; consuming it would
  /// delete that input.
  TEST_METHOD(ACharacterReachesTheFocusedFieldAndOnlyThen)
  {
    Fixture fixture;
    Assert::IsTrue(fixture.sink.OnInputEvent(Character('A')) == Neuron::InputDisposition::Ignored, L"with no focus");

    (void)fixture.Click(310, 1000);
    Assert::AreEqual(fixture.field, fixture.panel.Focus());
    Assert::IsTrue(fixture.sink.OnInputEvent(Character('H')) == Neuron::InputDisposition::Consumed);
    (void)fixture.sink.OnInputEvent(Character('I'));
    Assert::AreEqual(std::string("HI"), fixture.panel.Find(fixture.field)->text);

    (void)fixture.sink.OnInputEvent(Character(8)); // backspace
    Assert::AreEqual(std::string("H"), fixture.panel.Find(fixture.field)->text);

    for (int typed = 0; typed < 20; ++typed)
    {
      (void)fixture.sink.OnInputEvent(Character('X'));
    }
    Assert::AreEqual(std::size_t{8}, fixture.panel.Find(fixture.field)->text.size(), L"the limit holds");

    (void)fixture.sink.OnInputEvent(Character(13)); // Return
    Assert::AreEqual(std::uint32_t{0}, fixture.panel.Focus(), L"submitting drops the focus");
  }

  TEST_METHOD(ClickingAwayFromAFieldClearsItsFocus)
  {
    Fixture fixture;
    (void)fixture.Click(310, 1000);
    Assert::AreEqual(fixture.field, fixture.panel.Focus());
    (void)fixture.sink.OnInputEvent(Move(310, 830));
    (void)fixture.sink.OnInputEvent(Down(Neuron::MouseButton::Left));
    Assert::AreEqual(std::uint32_t{0}, fixture.panel.Focus());
  }

  /// A modal panel - the pause menu, the match-end overlay (§2) - is added over the others and is
  /// asked first, without anything having to reorder the list.
  TEST_METHOD(TheLastPanelAddedIsAskedFirst)
  {
    Fixture fixture;
    Neuron::UiPanel pause{Neuron::UiRect{760, 420, 400, 240}, "PAUSED"};
    Neuron::UiWidget quitButton{};
    quitButton.kind = Neuron::UiWidgetKind::Button;
    quitButton.rect = Neuron::UiRect{780, 500, 360, 24};
    quitButton.text = "QUIT";
    const std::uint32_t quit = pause.Add(quitButton);
    fixture.sink.AddPanel(&pause);

    const Neuron::UiEventResult acted = fixture.Click(800, 510);
    Assert::IsTrue(acted.action == Neuron::UiAction::Pressed);
    Assert::AreEqual(quit, acted.widget);

    pause.SetVisible(false);
    (void)fixture.sink.OnInputEvent(Move(800, 510));
    Assert::IsTrue(fixture.sink.OnInputEvent(Down(Neuron::MouseButton::Left)) == Neuron::InputDisposition::Ignored,
                   L"a hidden panel takes nothing, and there is no other panel there");
  }

  /// §4: a pointer in the letterbox is over no panel and over no world, and every hit test refuses
  /// it. A clamped position would put it on the outermost widget instead.
  TEST_METHOD(ThePointerInTheLetterboxIsOverNothingAtAll)
  {
    Fixture fixture;
    fixture.sink.SetFit(Neuron::FitAuthored(1920, 1200, Neuron::AUTHORED_WIDTH_PIXELS, Neuron::AUTHORED_HEIGHT_PIXELS));
    Assert::IsTrue(fixture.sink.OnInputEvent(Move(310, 10)) == Neuron::InputDisposition::Ignored, L"into the bar");

    Neuron::AuthoredPosition where{};
    Assert::IsFalse(fixture.sink.PointerAuthored(where), L"the pointer is not over the frame");
    Assert::IsFalse(fixture.sink.PointerOverPanel());
    Assert::IsTrue(fixture.sink.OnInputEvent(Down(Neuron::MouseButton::Left)) == Neuron::InputDisposition::Ignored);
  }

  /// The window going away with a button held must not leave it lit under a pointer that has gone.
  TEST_METHOD(LosingTheWindowsFocusForgetsTheHeldButton)
  {
    Fixture fixture;
    (void)fixture.sink.OnInputEvent(Move(310, 830));
    (void)fixture.sink.OnInputEvent(Down(Neuron::MouseButton::Left));
    Assert::IsTrue(fixture.panel.Find(fixture.button)->on);

    Neuron::InputEvent lost{};
    lost.kind = Neuron::InputEventKind::FocusLost;
    Assert::IsTrue(fixture.sink.OnInputEvent(lost) == Neuron::InputDisposition::Ignored, L"the game wants to know too");
    Assert::IsFalse(fixture.panel.Find(fixture.button)->on);
    Assert::AreEqual(std::uint32_t{0}, fixture.panel.Hovered());
  }
};

} // namespace ClientTests
