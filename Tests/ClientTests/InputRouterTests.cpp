#include "pch.h"

#include "InputRouter.h"

#include <cstdint>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// The router owes one ordering rule and one mask, and both are silent when wrong: a sink never
// offered an event makes the panel stop responding, a chain that continues past a consuming
// sink makes a click the panel handled also reach the game, and a mask that lets a consumed key
// through fires the binding a typed letter carries.
namespace ClientTests
{

namespace
{

constexpr std::uint8_t KEY_A = 65;
constexpr std::uint8_t KEY_ESCAPE = 27;

class RecordingSink : public Neuron::InputSink
{
public:
  explicit RecordingSink(Neuron::InputDisposition _answer)
    : answer(_answer)
  {
  }

  Neuron::InputDisposition OnInputEvent(const Neuron::InputEvent& _event) override
  {
    seen.push_back(_event);
    return answer;
  }

  [[nodiscard]] std::size_t Seen() const noexcept
  {
    return seen.size();
  }

  Neuron::InputDisposition answer;
  std::vector<Neuron::InputEvent> seen;
};

/// A text field: takes the characters and nothing else.
class CharacterSink : public Neuron::InputSink
{
public:
  Neuron::InputDisposition OnInputEvent(const Neuron::InputEvent& _event) override
  {
    return _event.kind == Neuron::InputEventKind::Character ? Neuron::InputDisposition::Consumed : Neuron::InputDisposition::Ignored;
  }
};

Neuron::InputEvent Key(Neuron::InputEventKind _kind, std::uint8_t _key)
{
  Neuron::InputEvent event;
  event.kind = _kind;
  event.key = _key;
  return event;
}

Neuron::InputEvent Character(std::uint32_t _character)
{
  Neuron::InputEvent event;
  event.kind = Neuron::InputEventKind::Character;
  event.character = _character;
  return event;
}

Neuron::InputEvent Button(Neuron::InputEventKind _kind)
{
  Neuron::InputEvent event;
  event.kind = _kind;
  event.button = Neuron::MouseButton::Left;
  return event;
}

/// One frame as the game runs it: derive, dispatch what was consumed, mask the view.
Neuron::FrameInput RunFrame(Neuron::InputRouter& _router, Neuron::FrameInput& _state, std::vector<Neuron::InputEvent> _events)
{
  const std::size_t consumed = Neuron::DeriveFrameInput(_events, _state);
  _router.Dispatch(std::span<const Neuron::InputEvent>(_events).first(consumed));
  Neuron::FrameInput view = _state;
  _router.Mask(view);
  return view;
}

} // namespace

TEST_CLASS(InputRouterTests)
{
public:
  TEST_METHOD(AnEventNobodyWantsIsOfferedToEverySink)
  {
    RecordingSink first(Neuron::InputDisposition::Ignored);
    RecordingSink second(Neuron::InputDisposition::Ignored);
    Neuron::InputRouter router;
    router.AddSink(&first);
    router.AddSink(&second);
    const std::vector<Neuron::InputEvent> events{Key(Neuron::InputEventKind::KeyDown, KEY_A)};
    router.Dispatch(events);
    Assert::AreEqual(std::size_t{1}, first.Seen());
    Assert::AreEqual(std::size_t{1}, second.Seen());
  }

  TEST_METHOD(DispatchWithNoSinksIsHarmless)
  {
    Neuron::InputRouter router;
    const std::vector<Neuron::InputEvent> events{Key(Neuron::InputEventKind::KeyDown, KEY_A)};
    router.Dispatch(events);
    Assert::IsFalse(router.IsKeyMasked(KEY_A));
  }

  TEST_METHOD(SinksAreOfferedInTheOrderTheyWereAddedAndConsumptionStopsTheChain)
  {
    RecordingSink panel(Neuron::InputDisposition::Consumed);
    RecordingSink game(Neuron::InputDisposition::Ignored);
    Neuron::InputRouter router;
    router.AddSink(&panel);
    router.AddSink(&game);
    const std::vector<Neuron::InputEvent> events{Key(Neuron::InputEventKind::KeyDown, KEY_A)};
    router.Dispatch(events);
    Assert::AreEqual(std::size_t{1}, panel.Seen());
    Assert::AreEqual(std::size_t{0}, game.Seen(), L"the panel took it, so the game never saw it");
  }

  TEST_METHOD(ARemovedSinkStopsBeingOffered)
  {
    RecordingSink sink(Neuron::InputDisposition::Ignored);
    Neuron::InputRouter router;
    router.AddSink(&sink);
    router.RemoveSink(&sink);
    const std::vector<Neuron::InputEvent> events{Key(Neuron::InputEventKind::KeyDown, KEY_A)};
    router.Dispatch(events);
    Assert::AreEqual(std::size_t{0}, sink.Seen());
  }

  TEST_METHOD(ANullSinkIsRefusedRatherThanStored)
  {
    Neuron::InputRouter router;
    router.AddSink(nullptr);
    const std::vector<Neuron::InputEvent> events{Key(Neuron::InputEventKind::KeyDown, KEY_A)};
    router.Dispatch(events); // would fault on a stored null
    Assert::IsFalse(router.IsKeyMasked(KEY_A));
  }

  // A consumed press masks the key for its whole hold: the view shows it neither pressed nor held.
  TEST_METHOD(AConsumedKeyIsMaskedForItsWholeHold)
  {
    RecordingSink panel(Neuron::InputDisposition::Consumed);
    Neuron::InputRouter router;
    router.AddSink(&panel);
    Neuron::FrameInput state;

    Neuron::FrameInput view = RunFrame(router, state, {Key(Neuron::InputEventKind::KeyDown, KEY_A)});
    Assert::IsTrue(state.keysHeld[KEY_A], L"the state stays truthful underneath");
    Assert::IsFalse(view.keysHeld[KEY_A]);
    Assert::AreEqual(0, static_cast<int>(view.keyEdges[KEY_A]));

    view = RunFrame(router, state, {});
    Assert::IsFalse(view.keysHeld[KEY_A], L"still masked while held");
  }

  TEST_METHOD(TheMaskCoversTheReleaseAndClearsTheFrameAfter)
  {
    RecordingSink panel(Neuron::InputDisposition::Consumed);
    Neuron::InputRouter router;
    router.AddSink(&panel);
    Neuron::FrameInput state;
    RunFrame(router, state, {Key(Neuron::InputEventKind::KeyDown, KEY_A)});

    panel.answer = Neuron::InputDisposition::Ignored;
    Neuron::FrameInput view = RunFrame(router, state, {Key(Neuron::InputEventKind::KeyUp, KEY_A)});
    Assert::AreEqual(0, static_cast<int>(view.keyEdges[KEY_A]), L"the release of a consumed press is the panel's too");
    Assert::IsFalse(router.IsKeyMasked(KEY_A), L"and the mask is gone once the frame has read it");

    view = RunFrame(router, state, {Key(Neuron::InputEventKind::KeyDown, KEY_A)});
    Assert::AreEqual(1, static_cast<int>(view.keyEdges[KEY_A]), L"the next press reaches the game");
  }

  // A text field consumes the character, not the key, and WM_CHAR carries no key: the mask lands
  // on the key that produced it, so typing a letter never also fires the binding it carries.
  TEST_METHOD(AConsumedCharacterMasksTheKeyThatProducedIt)
  {
    CharacterSink field;
    Neuron::InputRouter router;
    router.AddSink(&field);
    Neuron::FrameInput state;

    const Neuron::FrameInput view = RunFrame(router, state, {Key(Neuron::InputEventKind::KeyDown, KEY_A), Character('a')});
    Assert::IsTrue(router.IsKeyMasked(KEY_A));
    Assert::AreEqual(0, static_cast<int>(view.keyEdges[KEY_A]));
  }

  TEST_METHOD(AKeyTheFieldRefusesStillReachesTheGame)
  {
    CharacterSink field;
    Neuron::InputRouter router;
    router.AddSink(&field);
    Neuron::FrameInput state;

    const Neuron::FrameInput view = RunFrame(router, state, {Key(Neuron::InputEventKind::KeyDown, KEY_ESCAPE)});
    Assert::IsFalse(router.IsKeyMasked(KEY_ESCAPE), L"escape produces no character the field takes");
    Assert::AreEqual(1, static_cast<int>(view.keyEdges[KEY_ESCAPE]));
  }

  // A click is an edge: the panel consumes the press, the held button still answers underneath.
  TEST_METHOD(AConsumedButtonIsMaskedForTheEdgeOnly)
  {
    RecordingSink panel(Neuron::InputDisposition::Consumed);
    Neuron::InputRouter router;
    router.AddSink(&panel);
    Neuron::FrameInput state;

    Neuron::FrameInput view = RunFrame(router, state, {Button(Neuron::InputEventKind::MouseButtonDown)});
    Assert::AreEqual(0, static_cast<int>(view.buttonEdges[0]));
    Assert::IsTrue(view.buttonsHeld[0], L"the panel polls the held button under it");

    view = RunFrame(router, state, {Button(Neuron::InputEventKind::MouseButtonUp)});
    Assert::AreEqual(0, static_cast<int>(view.buttonEdges[0]), L"the sink took the release too");
    Assert::IsFalse(view.buttonsHeld[0]);
  }
};

} // namespace ClientTests
