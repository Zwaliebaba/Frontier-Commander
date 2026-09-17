#include "pch.h"

#include "FrameInput.h"

#include <cstddef>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// DeriveFrameInput is the rule that decides what a frame saw, and it touches no window, no Windows
// API and no global, so it is pinned here rather than by playing the game. The case that named the
// design is the first one: a key pressed and released between two frames used to report neither
// edge in the tree this design comes from (the Species input-native-events plan, T5).
namespace ClientTests
{

namespace
{

constexpr std::uint8_t KEY_A = 65;
constexpr std::uint8_t KEY_B = 66;

Neuron::InputEvent Key(Neuron::InputEventKind _kind, std::uint8_t _key, bool _repeat = false)
{
  Neuron::InputEvent event;
  event.kind = _kind;
  event.key = _key;
  event.repeat = _repeat;
  return event;
}

Neuron::InputEvent Button(Neuron::InputEventKind _kind, Neuron::MouseButton _button)
{
  Neuron::InputEvent event;
  event.kind = _kind;
  event.button = _button;
  return event;
}

Neuron::InputEvent Move(std::int32_t _x, std::int32_t _y)
{
  Neuron::InputEvent event;
  event.kind = Neuron::InputEventKind::MouseMove;
  event.x = _x;
  event.y = _y;
  return event;
}

Neuron::InputEvent RawMove(std::int32_t _deltaX, std::int32_t _deltaY)
{
  Neuron::InputEvent event;
  event.kind = Neuron::InputEventKind::MouseRawMove;
  event.x = _deltaX;
  event.y = _deltaY;
  return event;
}

Neuron::InputEvent Wheel(std::int32_t _rawDelta)
{
  Neuron::InputEvent event;
  event.kind = Neuron::InputEventKind::Wheel;
  event.wheelDelta = _rawDelta;
  return event;
}

Neuron::InputEvent Character(std::uint32_t _character)
{
  Neuron::InputEvent event;
  event.kind = Neuron::InputEventKind::Character;
  event.character = _character;
  return event;
}

Neuron::InputEvent FocusLost()
{
  Neuron::InputEvent event;
  event.kind = Neuron::InputEventKind::FocusLost;
  return event;
}

/// One frame: consumes what the derivation accepted and keeps the rest, as the game does.
std::size_t Frame(std::vector<Neuron::InputEvent>& _queue, Neuron::FrameInput& _state)
{
  const std::size_t consumed = Neuron::DeriveFrameInput(_queue, _state);
  _queue.erase(_queue.begin(), _queue.begin() + static_cast<std::ptrdiff_t>(consumed));
  return consumed;
}

} // namespace

TEST_CLASS(FrameInputTests)
{
public:
  TEST_METHOD(AFastPressAndReleaseReportsBothEdgesOnConsecutiveFrames)
  {
    Neuron::FrameInput state;
    std::vector<Neuron::InputEvent> queue{Key(Neuron::InputEventKind::KeyDown, KEY_A), Key(Neuron::InputEventKind::KeyUp, KEY_A)};

    Assert::AreEqual(std::size_t{1}, Frame(queue, state), L"the release waits for the next frame");
    Assert::AreEqual(1, static_cast<int>(state.keyEdges[KEY_A]));
    Assert::IsTrue(state.keysHeld[KEY_A]);

    Assert::AreEqual(std::size_t{1}, Frame(queue, state));
    Assert::AreEqual(-1, static_cast<int>(state.keyEdges[KEY_A]));
    Assert::IsFalse(state.keysHeld[KEY_A]);
  }

  TEST_METHOD(AHeldKeyEdgesOnceAndThenStaysDown)
  {
    Neuron::FrameInput state;
    std::vector<Neuron::InputEvent> queue{Key(Neuron::InputEventKind::KeyDown, KEY_A)};
    Frame(queue, state);
    Assert::AreEqual(1, static_cast<int>(state.keyEdges[KEY_A]));

    Frame(queue, state);
    Assert::AreEqual(0, static_cast<int>(state.keyEdges[KEY_A]), L"no edge on the frame after");
    Assert::IsTrue(state.keysHeld[KEY_A]);
  }

  TEST_METHOD(AutoRepeatIsNotAnEdge)
  {
    Neuron::FrameInput state;
    std::vector<Neuron::InputEvent> queue{Key(Neuron::InputEventKind::KeyDown, KEY_A)};
    Frame(queue, state);
    queue = {Key(Neuron::InputEventKind::KeyDown, KEY_A, true), Key(Neuron::InputEventKind::KeyDown, KEY_A, true)};

    Assert::AreEqual(std::size_t{2}, Frame(queue, state), L"repeats are consumed, not deferred");
    Assert::AreEqual(0, static_cast<int>(state.keyEdges[KEY_A]));
    Assert::IsTrue(state.keysHeld[KEY_A]);
  }

  // Stopping early must not reorder: an unrelated key behind the deferred event waits with it.
  TEST_METHOD(DeferringAnEdgeDefersEverythingBehindIt)
  {
    Neuron::FrameInput state;
    std::vector<Neuron::InputEvent> queue{Key(Neuron::InputEventKind::KeyDown, KEY_A), Key(Neuron::InputEventKind::KeyUp, KEY_A),
                                          Key(Neuron::InputEventKind::KeyDown, KEY_B)};

    Assert::AreEqual(std::size_t{1}, Frame(queue, state));
    Assert::AreEqual(0, static_cast<int>(state.keyEdges[KEY_B]), L"B has not been seen yet");

    Assert::AreEqual(std::size_t{2}, Frame(queue, state));
    Assert::AreEqual(-1, static_cast<int>(state.keyEdges[KEY_A]));
    Assert::AreEqual(1, static_cast<int>(state.keyEdges[KEY_B]));
  }

  TEST_METHOD(AClickInsideOneFrameReportsBothEdges)
  {
    Neuron::FrameInput state;
    std::vector<Neuron::InputEvent> queue{Button(Neuron::InputEventKind::MouseButtonDown, Neuron::MouseButton::Left),
                                          Button(Neuron::InputEventKind::MouseButtonUp, Neuron::MouseButton::Left)};

    Frame(queue, state);
    Assert::AreEqual(1, static_cast<int>(state.buttonEdges[0]));
    Assert::IsTrue(state.buttonsHeld[0]);

    Frame(queue, state);
    Assert::AreEqual(-1, static_cast<int>(state.buttonEdges[0]));
    Assert::IsFalse(state.buttonsHeld[0]);
  }

  TEST_METHOD(MouseMovesCoalesceAndVelocityIsMeasuredAcrossTheFrame)
  {
    Neuron::FrameInput state;
    std::vector<Neuron::InputEvent> queue{Move(10, 10)};
    Frame(queue, state);

    queue = {Move(12, 15), Move(30, 20), Move(25, 40)};
    Assert::AreEqual(std::size_t{3}, Frame(queue, state));
    Assert::AreEqual(25, state.mouseX);
    Assert::AreEqual(40, state.mouseY);
    Assert::AreEqual(15, state.mouseVelocityX, L"against where it was when the frame began");
    Assert::AreEqual(30, state.mouseVelocityY);
  }

  TEST_METHOD(RawMouseDeltasSumAcrossTheFrameAndResetOnTheNext)
  {
    Neuron::FrameInput state;
    std::vector<Neuron::InputEvent> queue{RawMove(3, -2), RawMove(4, 1), RawMove(-1, 0)};

    Assert::AreEqual(std::size_t{3}, Frame(queue, state));
    Assert::AreEqual(6, state.relativeX);
    Assert::AreEqual(-1, state.relativeY);
    Assert::IsTrue(state.relativeSeen);

    Frame(queue, state);
    Assert::AreEqual(0, state.relativeX, L"travel is per-frame");
    Assert::AreEqual(0, state.relativeY);
    Assert::IsTrue(state.relativeSeen, L"but the raw path stays live");
  }

  // The screen-edge case at frame granularity: the absolute position cannot move any further, the
  // raw axis still reports the hand's movement, and the aim follows the raw axis.
  TEST_METHOD(RawTravelSurvivesAPositionThatCannotMoveAnyFurther)
  {
    Neuron::FrameInput state;
    std::vector<Neuron::InputEvent> queue{Move(1919, 500)};
    Frame(queue, state);

    queue = {Move(1919, 500), RawMove(12, 0), Move(1919, 500)};
    Frame(queue, state);
    Assert::AreEqual(0, state.mouseVelocityX);
    Assert::AreEqual(12, state.relativeX);
    Assert::AreEqual(12, Neuron::TravelOf(state).x);
  }

  TEST_METHOD(UntilARelativePacketArrivesTheAimFollowsTheAbsolutePosition)
  {
    Neuron::FrameInput state;
    std::vector<Neuron::InputEvent> queue{Move(100, 100)};
    Frame(queue, state);
    queue = {Move(107, 96)};
    Frame(queue, state);

    Assert::IsFalse(state.relativeSeen);
    Assert::AreEqual(7, Neuron::TravelOf(state).x);
    Assert::AreEqual(-4, Neuron::TravelOf(state).y);
  }

  TEST_METHOD(RawMovesNeverDeferWhatIsBehindThem)
  {
    Neuron::FrameInput state;
    std::vector<Neuron::InputEvent> queue{RawMove(1, 1), RawMove(2, 2), Key(Neuron::InputEventKind::KeyDown, KEY_A)};

    Assert::AreEqual(std::size_t{3}, Frame(queue, state));
    Assert::AreEqual(1, static_cast<int>(state.keyEdges[KEY_A]));
  }

  TEST_METHOD(PartialWheelDeltasAccumulateIntoOneDetent)
  {
    Neuron::FrameInput state;
    std::vector<Neuron::InputEvent> queue{Wheel(40), Wheel(40)};
    Frame(queue, state);
    Assert::AreEqual(0, state.wheelDetents, L"two thirds of a detent is no detent");
    Assert::AreEqual(80, state.wheelRemainder);

    queue = {Wheel(40)};
    Frame(queue, state);
    Assert::AreEqual(1, state.wheelDetents);
    Assert::AreEqual(0, state.wheelRemainder);
  }

  TEST_METHOD(AWholeWheelDetentBackwardsScrollsOneBackwards)
  {
    Neuron::FrameInput state;
    std::vector<Neuron::InputEvent> queue{Wheel(-Neuron::WHEEL_DELTA_PER_DETENT)};
    Frame(queue, state);
    Assert::AreEqual(-1, state.wheelDetents);
    Assert::AreEqual(0, state.wheelRemainder);
  }

  TEST_METHOD(FocusLossReleasesEverythingHeldWithAnEdgeForEach)
  {
    Neuron::FrameInput state;
    std::vector<Neuron::InputEvent> queue{Key(Neuron::InputEventKind::KeyDown, KEY_A),
                                          Button(Neuron::InputEventKind::MouseButtonDown, Neuron::MouseButton::Right), Wheel(50)};
    Frame(queue, state);

    queue.push_back(FocusLost());
    Frame(queue, state);
    Assert::IsFalse(state.keysHeld[KEY_A]);
    Assert::AreEqual(-1, static_cast<int>(state.keyEdges[KEY_A]));
    Assert::IsFalse(state.buttonsHeld[1]);
    Assert::AreEqual(-1, static_cast<int>(state.buttonEdges[1]));
    Assert::AreEqual(0, state.wheelRemainder, L"a partial scroll is owed to nobody after focus goes");
  }

  // Focus loss obeys the one-edge rule like everything else, or it would erase the press the frame
  // had just reported.
  TEST_METHOD(FocusLossWaitsForAPressMadeInTheSameFrame)
  {
    Neuron::FrameInput state;
    std::vector<Neuron::InputEvent> queue{Key(Neuron::InputEventKind::KeyDown, KEY_A), FocusLost()};

    Assert::AreEqual(std::size_t{1}, Frame(queue, state));
    Assert::AreEqual(1, static_cast<int>(state.keyEdges[KEY_A]), L"the press is reported");

    Frame(queue, state);
    Assert::AreEqual(-1, static_cast<int>(state.keyEdges[KEY_A]), L"the release lands the frame after");
    Assert::IsFalse(state.keysHeld[KEY_A]);
  }

  // A character edges no control, so it can never be the second edge that stops a frame, and a
  // burst of typing must not hold the keys behind it back.
  TEST_METHOD(CharactersAreConsumedWithoutEdgingAnythingOrDeferring)
  {
    Neuron::FrameInput state;
    std::vector<Neuron::InputEvent> queue{Key(Neuron::InputEventKind::KeyDown, KEY_A), Character('a'), Character('b'),
                                          Key(Neuron::InputEventKind::KeyDown, KEY_B)};

    Assert::AreEqual(std::size_t{4}, Frame(queue, state));
    Assert::AreEqual(1, static_cast<int>(state.keyEdges[KEY_A]));
    Assert::AreEqual(1, static_cast<int>(state.keyEdges[KEY_B]));
  }

  TEST_METHOD(AnEmptyFrameClearsTheDeltasAndKeepsTheState)
  {
    Neuron::FrameInput state;
    std::vector<Neuron::InputEvent> queue{Key(Neuron::InputEventKind::KeyDown, KEY_A), Move(7, 8)};
    Frame(queue, state);

    Assert::AreEqual(std::size_t{0}, Neuron::DeriveFrameInput({}, state));
    Assert::AreEqual(0, static_cast<int>(state.keyEdges[KEY_A]), L"edges are per-frame");
    Assert::IsTrue(state.keysHeld[KEY_A], L"held state is not");
    Assert::AreEqual(7, state.mouseX, L"position is not");
    Assert::AreEqual(0, state.mouseVelocityX, L"velocity is");
  }
};

} // namespace ClientTests
