#include "pch.h"

#include "InputRouter.h"
#include "InputSubscription.h"

#include <utility>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// The handle is the lifetime half of subscribing, and the half that is invisible when it goes
// wrong: a handle that fails to cancel leaves a handler holding a pointer to a destroyed window,
// and the game keeps running until the day that key is pressed. The bookkeeping needs no input at
// all; the firing rules at the end need a frame's view built by hand.
namespace ClientTests
{

namespace
{

constexpr std::uint8_t KEY_P = 80;
constexpr Neuron::InputTrigger PAUSE = {KEY_P, Neuron::InputEdge::Pressed};

Neuron::FrameInput Pressed(std::uint8_t _key)
{
  Neuron::FrameInput view;
  view.keysHeld[_key] = true;
  view.keyEdges[_key] = 1;
  return view;
}

} // namespace

TEST_CLASS(InputSubscriptionTests)
{
public:
  TEST_METHOD(SubscribingRegistersAndTheHandleIsActive)
  {
    Neuron::InputRouter router;
    const Neuron::InputSubscription handle = router.Subscribe(PAUSE, [] {});
    Assert::AreEqual(std::size_t{1}, router.SubscriptionCount());
    Assert::IsTrue(handle.IsActive());
  }

  TEST_METHOD(DestroyingTheHandleUnsubscribes)
  {
    Neuron::InputRouter router;
    {
      const Neuron::InputSubscription handle = router.Subscribe(PAUSE, [] {});
      Assert::AreEqual(std::size_t{1}, router.SubscriptionCount());
    }
    Assert::AreEqual(std::size_t{0}, router.SubscriptionCount(), L"the subscription went with the handle");
  }

  TEST_METHOD(SubscriptionsAreIndependentOfEachOther)
  {
    Neuron::InputRouter router;
    const Neuron::InputSubscription kept = router.Subscribe(PAUSE, [] {});
    {
      const Neuron::InputSubscription temporary = router.Subscribe(PAUSE, [] {});
      Assert::AreEqual(std::size_t{2}, router.SubscriptionCount());
    }
    Assert::AreEqual(std::size_t{1}, router.SubscriptionCount());
  }

  TEST_METHOD(MovingTransfersOwnershipWithoutCanceling)
  {
    Neuron::InputRouter router;
    Neuron::InputSubscription original = router.Subscribe(PAUSE, [] {});
    const Neuron::InputSubscription moved = std::move(original);
    Assert::IsTrue(moved.IsActive());
    Assert::IsFalse(original.IsActive()); // NOLINT(bugprone-use-after-move): the moved-from state is what is under test
    Assert::AreEqual(std::size_t{1}, router.SubscriptionCount());
  }

  TEST_METHOD(AMovedFromHandleGoingOutOfScopeCancelsNothing)
  {
    Neuron::InputRouter router;
    Neuron::InputSubscription outer;
    {
      Neuron::InputSubscription inner = router.Subscribe(PAUSE, [] {});
      outer = std::move(inner);
    }
    Assert::AreEqual(std::size_t{1}, router.SubscriptionCount());
    Assert::IsTrue(outer.IsActive());
  }

  TEST_METHOD(MoveAssigningOverALiveHandleCancelsTheOldSubscription)
  {
    Neuron::InputRouter router;
    Neuron::InputSubscription first = router.Subscribe(PAUSE, [] {});
    Neuron::InputSubscription second = router.Subscribe(PAUSE, [] {});
    Assert::AreEqual(std::size_t{2}, router.SubscriptionCount());
    first = std::move(second);
    Assert::AreEqual(std::size_t{1}, router.SubscriptionCount(), L"what first held was cancelled, what second held lives on in first");
    Assert::IsTrue(first.IsActive());
  }

  TEST_METHOD(ResetIsIdempotent)
  {
    Neuron::InputRouter router;
    Neuron::InputSubscription handle = router.Subscribe(PAUSE, [] {});
    handle.Reset();
    handle.Reset();
    Assert::IsFalse(handle.IsActive());
    Assert::AreEqual(std::size_t{0}, router.SubscriptionCount());
  }

  TEST_METHOD(ADefaultHandleOwnsNothingAndIsSafeToDestroy)
  {
    {
      Neuron::InputSubscription handle;
      Assert::IsFalse(handle.IsActive());
    }
    Neuron::InputRouter router;
    Assert::AreEqual(std::size_t{0}, router.SubscriptionCount());
  }

  TEST_METHOD(ANullHandlerIsRefusedRatherThanStored)
  {
    Neuron::InputRouter router;
    const Neuron::InputSubscription handle = router.Subscribe(PAUSE, nullptr);
    Assert::IsFalse(handle.IsActive());
    Assert::AreEqual(std::size_t{0}, router.SubscriptionCount());
  }

  TEST_METHOD(AHandlerFiresOncePerFrameItsEdgeAppearsIn)
  {
    Neuron::InputRouter router;
    int fired = 0;
    const Neuron::InputSubscription handle = router.Subscribe(PAUSE, [&fired] { ++fired; });
    router.FireSubscriptions(Pressed(KEY_P));
    router.FireSubscriptions(Pressed(KEY_P));
    router.FireSubscriptions(Neuron::FrameInput{});
    Assert::AreEqual(2, fired);
  }

  // The common handler closes the window that owns the handle, which cancels the subscription the
  // call is executing inside.
  TEST_METHOD(AHandlerMayCancelItsOwnSubscription)
  {
    Neuron::InputRouter router;
    Neuron::InputSubscription handle;
    int fired = 0;
    handle = router.Subscribe(PAUSE,
                              [&handle, &fired]
                              {
                                ++fired;
                                handle.Reset();
                              });
    router.FireSubscriptions(Pressed(KEY_P));
    router.FireSubscriptions(Pressed(KEY_P));
    Assert::AreEqual(1, fired);
    Assert::AreEqual(std::size_t{0}, router.SubscriptionCount());
  }

  TEST_METHOD(ASubscriptionMadeByAHandlerDoesNotFireThatFrame)
  {
    Neuron::InputRouter router;
    std::vector<Neuron::InputSubscription> handles;
    int lateFired = 0;
    handles.push_back(router.Subscribe(PAUSE, [&router, &handles, &lateFired]
                                       { handles.push_back(router.Subscribe(PAUSE, [&lateFired] { ++lateFired; })); }));
    router.FireSubscriptions(Pressed(KEY_P));
    Assert::AreEqual(0, lateFired, L"a window opened by a keystroke does not act on the same keystroke");
    router.FireSubscriptions(Pressed(KEY_P));
    Assert::AreEqual(1, lateFired);
  }

  TEST_METHOD(AHandlerThatCancelsAnotherDueToFireStopsIt)
  {
    Neuron::InputRouter router;
    int secondFired = 0;
    Neuron::InputSubscription second;
    const Neuron::InputSubscription first = router.Subscribe(PAUSE, [&second] { second.Reset(); });
    second = router.Subscribe(PAUSE, [&secondFired] { ++secondFired; });
    router.FireSubscriptions(Pressed(KEY_P));
    Assert::AreEqual(0, secondFired);
  }

  // The mask and the subscriptions answer to one rule: a key the panel consumed reaches no
  // subscriber either.
  TEST_METHOD(AMaskedKeyReachesNoSubscriber)
  {
    class ConsumingSink : public Neuron::InputSink
    {
    public:
      Neuron::InputDisposition OnInputEvent(const Neuron::InputEvent&) override
      {
        return Neuron::InputDisposition::Consumed;
      }
    } panel;
    Neuron::InputRouter router;
    router.AddSink(&panel);
    int fired = 0;
    const Neuron::InputSubscription handle = router.Subscribe(PAUSE, [&fired] { ++fired; });

    Neuron::InputEvent press;
    press.kind = Neuron::InputEventKind::KeyDown;
    press.key = KEY_P;
    const std::vector<Neuron::InputEvent> events{press};
    Neuron::FrameInput state;
    static_cast<void>(Neuron::DeriveFrameInput(events, state));
    router.Dispatch(events);
    router.Mask(state);
    router.FireSubscriptions(state);
    Assert::AreEqual(0, fired);
  }
};

} // namespace ClientTests
