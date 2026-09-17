#include "pch.h"

#include "Sim.h"
#include "Snapshot.h"

#include "Random.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace SimTests
{

namespace
{

// The three tests TechnicalDesign.md §10 says exist from M0, over a Sim with no systems yet: the
// tick, the one draw stage 8 makes, the seats, the queue and the hash are what they exercise, so
// that every later system is built under them (m0-foundation/T15 notes).

inline constexpr std::uint32_t MATCH_TICKS = 10000;
inline constexpr std::uint32_t SNAPSHOT_TICK = 5000;

Frontier::MatchSettings FourSeats(std::uint64_t _seed)
{
  Frontier::MatchSettings settings{};
  settings.seed = _seed;
  settings.sizeClass = Frontier::SizeClass::Small;
  settings.seatCount = 4;
  settings.baseLevel = Frontier::BaseLevel::Nothing;
  settings.powerLevel = Frontier::PowerLevel::Medium;
  settings.technologyTiers = 0;
  settings.victory = Frontier::VictoryCondition::Annihilation;
  settings.survivalTicks = 0;
  settings.seats[0] = {Frontier::SeatKind::Human, 0};
  settings.seats[1] = {Frontier::SeatKind::Ai, 1};
  settings.seats[2] = {Frontier::SeatKind::Ai, 1};
  settings.seats[3] = {Frontier::SeatKind::Human, 0};
  return settings;
}

/// A deterministic order stream: about one order in eight ticks, from a seat that may be outside
/// the match, of any kind but Surrender, so that the match runs to the end. Every order carries the
/// tick it is generated for, which is how a replay feeds it back.
class OrderStream
{
public:
  explicit OrderStream(std::uint64_t _seed)
    : m_random(_seed)
  {
  }

  std::vector<Frontier::Order> ForTick(std::uint32_t _tick)
  {
    std::vector<Frontier::Order> orders;
    while (m_random.Below(8) == 0)
    {
      Frontier::Order order{};
      order.tick = _tick;
      order.seat = static_cast<std::uint8_t>(m_random.Below(6)); // seats 4 and 5 are outside the match
      std::uint32_t kind = m_random.Below(Frontier::ORDER_KIND_COUNT);
      if (kind == static_cast<std::uint32_t>(Frontier::OrderKind::Surrender))
      {
        kind = static_cast<std::uint32_t>(Frontier::OrderKind::Chat);
      }
      order.kind = static_cast<Frontier::OrderKind>(kind);
      for (std::int32_t& operand : order.operands)
      {
        operand = m_random.Between(-1000, 1000);
      }
      orders.push_back(order);
    }
    return orders;
  }

private:
  Neuron::Random m_random;
};

/// The Sim a snapshot of _sim reads back as; a snapshot that does not read back fails the test here.
Frontier::Sim Reload(const Frontier::Sim& _sim)
{
  std::optional<Frontier::Sim> reloaded = Frontier::Snapshot::Read(Frontier::Snapshot::Write(_sim));
  if (!reloaded.has_value())
  {
    Assert::Fail(L"the snapshot did not read back");
    return Frontier::Sim(_sim.Settings());
  }
  return *reloaded;
}

void AssertSameHash(const Frontier::Sim& _a, const Frontier::Sim& _b, std::uint32_t _tick)
{
  if (_a.Hash() != _b.Hash())
  {
    const std::wstring message = L"the hashes differ at tick " + std::to_wstring(_tick);
    Assert::Fail(message.c_str());
  }
}

} // namespace

TEST_CLASS(DeterminismTests)
{
public:
  TEST_METHOD(TwoMatchesFromOneSeedAndOneOrderStreamHashIdenticallyEveryTick)
  {
    const Frontier::MatchSettings settings = FourSeats(0x5EEDull);
    Frontier::Sim a(settings);
    Frontier::Sim b(settings);
    OrderStream stream(7);
    std::uint32_t submitted = 0;
    for (std::uint32_t tick = 1; tick <= MATCH_TICKS; ++tick)
    {
      for (const Frontier::Order& order : stream.ForTick(tick))
      {
        a.Submit(order);
        b.Submit(order);
        ++submitted;
      }
      a.Advance();
      b.Advance();
      AssertSameHash(a, b, tick);
    }
    Assert::AreEqual(MATCH_TICKS, a.Tick());
    Assert::IsTrue(submitted > MATCH_TICKS / 16, L"the stream produced too few orders to mean anything");
    Assert::AreEqual(submitted, a.AppliedOrders() + a.DroppedOrders());
    Assert::IsTrue(a.AppliedOrders() > 0 && a.DroppedOrders() > 0, L"both outcomes of validation should occur");
    Assert::IsFalse(a.Finished());
  }

  TEST_METHOD(ASnapshotReloadedContinuesToTheSameHashes)
  {
    const Frontier::MatchSettings settings = FourSeats(0xC0FFEEull);
    Frontier::Sim original(settings);
    OrderStream stream(11);
    for (std::uint32_t tick = 1; tick <= SNAPSHOT_TICK; ++tick)
    {
      for (const Frontier::Order& order : stream.ForTick(tick))
      {
        original.Submit(order);
      }
      original.Advance();
    }
    // Orders already queued for later ticks must survive the snapshot too.
    for (std::uint32_t ahead = 1; ahead <= 40; ++ahead)
    {
      Frontier::Order order{};
      order.tick = SNAPSHOT_TICK + ahead * 7;
      order.seat = static_cast<std::uint8_t>(ahead % 4);
      order.kind = Frontier::OrderKind::Chat;
      original.Submit(order);
    }
    Frontier::Sim reloaded = Reload(original);
    Assert::AreEqual(original.Tick(), reloaded.Tick());
    Assert::AreEqual(original.Hash(), reloaded.Hash());
    Assert::AreEqual(original.ComputeHash(), reloaded.ComputeHash());
    Assert::AreEqual(original.Orders().Size(), reloaded.Orders().Size());
    for (std::uint32_t tick = SNAPSHOT_TICK + 1; tick <= MATCH_TICKS; ++tick)
    {
      for (const Frontier::Order& order : stream.ForTick(tick))
      {
        original.Submit(order);
        reloaded.Submit(order);
      }
      original.Advance();
      reloaded.Advance();
      AssertSameHash(original, reloaded, tick);
    }
    Assert::AreEqual(original.AppliedOrders(), reloaded.AppliedOrders());
    Assert::AreEqual(original.DroppedOrders(), reloaded.DroppedOrders());
  }

  TEST_METHOD(ARecordedOrderStreamReplayedFromTheSeedReproducesTheHashes)
  {
    const Frontier::MatchSettings settings = FourSeats(0xABCDEFull);
    Frontier::Sim live(settings);
    OrderStream stream(23);
    std::vector<Frontier::Order> recorded;
    std::vector<std::uint64_t> hashes;
    hashes.reserve(MATCH_TICKS);
    for (std::uint32_t tick = 1; tick <= MATCH_TICKS; ++tick)
    {
      for (const Frontier::Order& order : stream.ForTick(tick))
      {
        live.Submit(order);
        recorded.push_back(order);
      }
      live.Advance();
      hashes.push_back(live.Hash());
    }
    // A replay is the settings, the seed and the order stream (TechnicalDesign.md §4.9): the whole
    // stream is known up front, so it is fed up front, and the hash must not care.
    Frontier::Sim replay(settings);
    for (const Frontier::Order& order : recorded)
    {
      replay.Submit(order);
    }
    for (std::uint32_t tick = 1; tick <= MATCH_TICKS; ++tick)
    {
      replay.Advance();
      if (replay.Hash() != hashes[tick - 1])
      {
        const std::wstring message = L"the replay diverged at tick " + std::to_wstring(tick);
        Assert::Fail(message.c_str());
      }
    }
    Assert::AreEqual(live.AppliedOrders(), replay.AppliedOrders());
    Assert::AreEqual(live.DroppedOrders(), replay.DroppedOrders());
  }

  TEST_METHOD(TheCostOfAnEmptyTickAndOfTheHashIsMeasured)
  {
    // ADR-002's measurement: an empty tick and a hash of the empty state, in nanoseconds each,
    // written to the test output for the run that records them.
    const Frontier::MatchSettings settings = FourSeats(1);
    Frontier::Sim sim(settings);
    const auto tickStart = std::chrono::steady_clock::now();
    for (std::uint32_t tick = 0; tick < MATCH_TICKS; ++tick)
    {
      sim.Advance();
    }
    const auto tickEnd = std::chrono::steady_clock::now();
    std::uint64_t folded = 0;
    for (std::uint32_t round = 0; round < MATCH_TICKS; ++round)
    {
      folded ^= sim.ComputeHash();
    }
    const auto hashEnd = std::chrono::steady_clock::now();
    const std::int64_t tickNanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(tickEnd - tickStart).count() / MATCH_TICKS;
    const std::int64_t hashNanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(hashEnd - tickEnd).count() / MATCH_TICKS;
    const std::wstring message = L"measured: an empty tick takes " + std::to_wstring(tickNanoseconds) +
                                 L" ns, the hash of the empty state " + std::to_wstring(hashNanoseconds) + L" ns (" +
                                 std::to_wstring(MATCH_TICKS) + L" of each)";
    Logger::WriteMessage(message.c_str());
    Assert::IsTrue(folded == 0, L"an even number of one hash folds to zero; the hash must be stable");
    Assert::IsTrue(tickNanoseconds < 50000000LL, L"an empty tick must be nowhere near the 50 ms budget");
  }
};

} // namespace SimTests
