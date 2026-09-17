#include "pch.h"

#include "Liveness.h"

#include <cstdint>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace CoreTests
{

namespace
{

// Heartbeat every 20 ticks, suspect after 100 of silence, lost 60 later.
inline constexpr Neuron::LivenessSettings SETTINGS = {20, 100, 60};

} // namespace

TEST_CLASS(LivenessTests)
{
public:
  TEST_METHOD(APeerThatKeepsTalkingStaysAlive)
  {
    Neuron::Liveness liveness(SETTINGS, 0);
    Assert::IsTrue(liveness.State() == Neuron::LivenessState::Alive);
    for (std::uint32_t tick = 1; tick <= 1000; ++tick)
    {
      if (tick % 50 == 0)
      {
        liveness.Heard(tick);
      }
      Assert::IsTrue(liveness.Advance(tick) == Neuron::LivenessState::Alive);
    }
  }

  TEST_METHOD(SilenceMakesThePeerSuspectThenLost)
  {
    Neuron::Liveness liveness(SETTINGS, 10);
    Assert::IsTrue(liveness.Advance(109) == Neuron::LivenessState::Alive, L"one tick short of the timeout");
    Assert::IsTrue(liveness.Advance(110) == Neuron::LivenessState::Suspect, L"the timeout");
    Assert::IsTrue(liveness.Advance(169) == Neuron::LivenessState::Suspect, L"one tick short of the grace");
    Assert::IsTrue(liveness.Advance(170) == Neuron::LivenessState::Lost, L"the grace ran out");
  }

  TEST_METHOD(ASuspectPeerThatAnswersIsAliveAgain)
  {
    Neuron::Liveness liveness(SETTINGS, 0);
    Assert::IsTrue(liveness.Advance(120) == Neuron::LivenessState::Suspect);
    liveness.Heard(125);
    Assert::IsTrue(liveness.Advance(126) == Neuron::LivenessState::Alive);
    Assert::AreEqual(125u, liveness.LastHeard());
    Assert::IsTrue(liveness.Advance(224) == Neuron::LivenessState::Alive);
    Assert::IsTrue(liveness.Advance(225) == Neuron::LivenessState::Suspect, L"the timeout counts from the last word");
  }

  TEST_METHOD(LostIsSticky)
  {
    Neuron::Liveness liveness(SETTINGS, 0);
    Assert::IsTrue(liveness.Advance(200) == Neuron::LivenessState::Lost);
    liveness.Heard(201);
    Assert::IsTrue(liveness.Advance(202) == Neuron::LivenessState::Lost, L"a lost peer reconnects; it does not come back");
    Assert::IsTrue(liveness.State() == Neuron::LivenessState::Lost);
  }

  TEST_METHOD(AHeartbeatIsDueAfterTheIntervalOfSilenceOutward)
  {
    Neuron::Liveness liveness(SETTINGS, 0);
    Assert::IsFalse(liveness.HeartbeatDue(19));
    Assert::IsTrue(liveness.HeartbeatDue(20));
    liveness.Sent(20);
    Assert::IsFalse(liveness.HeartbeatDue(39));
    Assert::IsTrue(liveness.HeartbeatDue(40));
    liveness.Sent(15); // an older tick does not move the clock back
    Assert::AreEqual(20u, liveness.LastSent());
    Assert::IsTrue(liveness.HeartbeatDue(40));
    liveness.Heard(40);
    Assert::IsTrue(liveness.HeartbeatDue(40), L"hearing the peer says nothing about what we sent");
  }

  TEST_METHOD(TheClockNeverRunsBackward)
  {
    Neuron::Liveness liveness(SETTINGS, 500);
    liveness.Heard(400);
    Assert::AreEqual(500u, liveness.LastHeard());
    Assert::IsTrue(liveness.Advance(300) == Neuron::LivenessState::Alive, L"an earlier tick is treated as no silence");
  }
};

} // namespace CoreTests
