#include "pch.h"

#include "Sim.h"

#include <cstdint>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace SimTests
{

namespace
{
/// The tables a match is played by. These suites exercise the simulation rather than the rules, so
/// an empty tree is the honest one: no row is read, and Q20's binding is still exercised, because
/// the snapshot carries this tree's hash and refuses any other.
const Frontier::ContentTree& NoContent()
{
  static const Frontier::ContentTree TREE{};
  return TREE;
}

Frontier::MatchSettings TwoSides(Frontier::VictoryCondition _victory, std::uint32_t _survivalTicks)
{
  Frontier::MatchSettings settings{};
  settings.seed = 99;
  settings.sizeClass = Frontier::SizeClass::Small;
  settings.seatCount = 3;
  settings.baseLevel = Frontier::BaseLevel::Nothing;
  settings.powerLevel = Frontier::PowerLevel::Low;
  settings.technologyTiers = 0;
  settings.victory = _victory;
  settings.survivalTicks = _survivalTicks;
  settings.seats[0] = {Frontier::SeatKind::Human, 0};
  settings.seats[1] = {Frontier::SeatKind::Ai, 1};
  settings.seats[2] = {Frontier::SeatKind::Empty, 2};
  return settings;
}

Frontier::Order Of(Frontier::OrderKind _kind, std::uint8_t _seat, std::uint32_t _tick)
{
  Frontier::Order order{};
  order.tick = _tick;
  order.seat = _seat;
  order.kind = _kind;
  return order;
}

} // namespace

TEST_CLASS(SimTests)
{
public:
  TEST_METHOD(TheSeatsComeFromTheLobby)
  {
    const Frontier::Sim sim(TwoSides(Frontier::VictoryCondition::Annihilation, 0), NoContent());
    Assert::AreEqual(static_cast<std::size_t>(3), sim.Seats().size());
    Assert::IsTrue(sim.Seats()[0].kind == Frontier::SeatKind::Human);
    Assert::AreEqual(1, static_cast<int>(sim.Seats()[1].alliance));
    Assert::AreEqual(40000, sim.Seats()[0].powerHundredths);
    Assert::IsFalse(sim.Seats()[0].Defeated());
    Assert::IsTrue(sim.Seats()[2].Defeated(), L"an empty seat takes no part");
    Assert::AreEqual(static_cast<std::uint32_t>(0), sim.Tick());
    Assert::IsFalse(sim.Finished());
  }

  TEST_METHOD(TheTickAndTheRandomAdvanceOnlyInAdvance)
  {
    Frontier::Sim sim(TwoSides(Frontier::VictoryCondition::Annihilation, 0), NoContent());
    const Neuron::Random::State before = sim.Stream().GetState();
    sim.Submit(Of(Frontier::OrderKind::Chat, 0, 1));
    Assert::IsTrue(before == sim.Stream().GetState(), L"Submit must not draw");
    Assert::AreEqual(static_cast<std::uint32_t>(0), sim.Tick());
    sim.Advance();
    Assert::AreEqual(static_cast<std::uint32_t>(1), sim.Tick());
    // The stream advances when the simulation has something to roll for and not otherwise. Stage 8
    // drew unconditionally until m1-vertical-slice/S10 gave it real hit rolls to make, which is
    // what that draw was a placeholder for; a match with nothing shooting now leaves the stream
    // where it was, and a draw on a tick that rolled for nothing would be a draw two hosts could
    // fall out of step over for no reason at all.
    Assert::IsTrue(before == sim.Stream().GetState(), L"an empty tick rolls for nothing");
    for (std::uint32_t tick = 0; tick < 20; ++tick)
    {
      sim.Advance();
    }
    Assert::IsTrue(before == sim.Stream().GetState(), L"and goes on rolling for nothing");
    // And when there IS a roll, it goes through Sim::Roll, which is the one door to the stream.
    const std::uint32_t rolled = sim.Roll(100);
    Assert::IsTrue(rolled < 100);
    Assert::IsFalse(before == sim.Stream().GetState(), L"a roll advances it");
  }

  TEST_METHOD(AnOrderForAPastTickAppliesOnTheNextOne)
  {
    Frontier::Sim sim(TwoSides(Frontier::VictoryCondition::Annihilation, 0), NoContent());
    for (int tick = 0; tick < 5; ++tick)
    {
      sim.Advance();
    }
    sim.Submit(Of(Frontier::OrderKind::Chat, 0, 2));
    Assert::AreEqual(static_cast<std::uint32_t>(6), sim.Orders().Entries()[0].order.tick);
    sim.Advance();
    Assert::AreEqual(static_cast<std::uint32_t>(1), sim.AppliedOrders());
    Assert::IsTrue(sim.Orders().Empty());
  }

  TEST_METHOD(OrdersThatFailValidationAreDroppedAndCounted)
  {
    Frontier::Sim sim(TwoSides(Frontier::VictoryCondition::Annihilation, 0), NoContent());
    sim.Submit(Of(Frontier::OrderKind::Chat, 7, 1)); // no such seat
    sim.Submit(Of(Frontier::OrderKind::Chat, 2, 1)); // an empty seat
    sim.Submit(Of(Frontier::OrderKind::Move, 0, 1)); // names an object nobody owns
    sim.Submit(Of(Frontier::OrderKind::Chat, 0, 1)); // fine
    sim.Advance();
    Assert::AreEqual(static_cast<std::uint32_t>(1), sim.AppliedOrders());
    Assert::AreEqual(static_cast<std::uint32_t>(3), sim.DroppedOrders());
  }

  TEST_METHOD(SurrenderDefeatsTheSeatAndTheLastAllianceWins)
  {
    Frontier::Sim sim(TwoSides(Frontier::VictoryCondition::Annihilation, 0), NoContent());
    sim.Advance();
    Assert::IsFalse(sim.Finished());
    sim.Submit(Of(Frontier::OrderKind::Surrender, 1, 2));
    sim.Advance();
    Assert::IsTrue(sim.Seats()[1].Defeated());
    Assert::IsTrue(sim.Finished());
    Assert::AreEqual(0, static_cast<int>(sim.WinningAlliance()));
    Assert::IsTrue(sim.Seats()[0].victory == Frontier::VictoryState::Won);
    Assert::IsTrue(sim.Seats()[1].victory == Frontier::VictoryState::Eliminated, L"he left rather than lost");
    // And a decided match does not advance: the order is neither applied nor refused, because
    // nothing runs at all (m1-vertical-slice/S11).
    const std::uint32_t decidedAt = sim.Tick();
    const std::uint64_t decided = sim.Hash();
    sim.Submit(Of(Frontier::OrderKind::Chat, 1, 3));
    sim.Advance();
    Assert::AreEqual(decidedAt, sim.Tick(), L"a finished match does not tick");
    Assert::AreEqual(decided, sim.Hash());
    Assert::AreEqual(static_cast<std::uint32_t>(0), sim.DroppedOrders());
    Assert::AreEqual(0, static_cast<int>(sim.WinningAlliance()), L"a finished match stays finished");
  }

  TEST_METHOD(SurvivalEndsWhenTheClockRunsOutAndEqualExtractionIsADraw)
  {
    Frontier::Sim sim(TwoSides(Frontier::VictoryCondition::Survival, 10), NoContent());
    for (int tick = 0; tick < 9; ++tick)
    {
      sim.Advance();
      Assert::IsFalse(sim.Finished());
    }
    sim.Advance();
    Assert::IsTrue(sim.Finished());
    Assert::AreEqual(static_cast<int>(Frontier::NO_ALLIANCE), static_cast<int>(sim.WinningAlliance()));
  }

  TEST_METHOD(PublishIsDueOnEverySecondTick)
  {
    Frontier::Sim sim(TwoSides(Frontier::VictoryCondition::Annihilation, 0), NoContent());
    Assert::IsFalse(sim.PublishDue());
    sim.Advance();
    Assert::IsFalse(sim.PublishDue());
    sim.Advance();
    Assert::IsTrue(sim.PublishDue());
    sim.Advance();
    Assert::IsFalse(sim.PublishDue());
  }

  TEST_METHOD(TheHashCoversTheSeatsTheTickAndTheStream)
  {
    Frontier::Sim a(TwoSides(Frontier::VictoryCondition::Annihilation, 0), NoContent());
    Frontier::Sim b(TwoSides(Frontier::VictoryCondition::Annihilation, 0), NoContent());
    Assert::AreEqual(a.ComputeHash(), b.ComputeHash());
    a.Advance();
    Assert::AreNotEqual(a.ComputeHash(), b.ComputeHash(), L"the tick and the draw change the hash");
    b.Advance();
    Assert::AreEqual(a.ComputeHash(), b.ComputeHash());
    a.Submit(Of(Frontier::OrderKind::Surrender, 1, 0)); // for a past tick: applied on the next one
    Assert::AreEqual(a.ComputeHash(), b.ComputeHash(), L"a pending order is not state");
    a.Advance();
    b.Advance();
    Assert::AreNotEqual(a.ComputeHash(), b.ComputeHash(), L"a defeated seat changes the hash");
    Frontier::MatchSettings other = TwoSides(Frontier::VictoryCondition::Annihilation, 0);
    other.seed = 100;
    const Frontier::Sim c(other, NoContent());
    Assert::AreNotEqual(b.ComputeHash(), c.ComputeHash(), L"the seed reaches the hash through the Random state");
  }
};

} // namespace SimTests
