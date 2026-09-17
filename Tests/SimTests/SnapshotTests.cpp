#include "pch.h"

#include "Sim.h"
#include "Snapshot.h"

#include "ByteReader.h"
#include "ByteWriter.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace SimTests
{

namespace
{

Frontier::MatchSettings ThreeSeats()
{
  Frontier::MatchSettings settings{};
  settings.seed = 0x1234567890ull;
  settings.sizeClass = Frontier::SizeClass::Medium;
  settings.seatCount = 3;
  settings.baseLevel = Frontier::BaseLevel::Small;
  settings.powerLevel = Frontier::PowerLevel::High;
  settings.technologyTiers = 1;
  settings.victory = Frontier::VictoryCondition::Survival;
  settings.survivalTicks = 100000;
  settings.seats[0] = {Frontier::SeatKind::Human, 0};
  settings.seats[1] = {Frontier::SeatKind::Ai, 1};
  settings.seats[2] = {Frontier::SeatKind::Ai, 2};
  return settings;
}

Frontier::Order Chat(std::uint32_t _tick, std::uint8_t _seat)
{
  Frontier::Order order{};
  order.tick = _tick;
  order.seat = _seat;
  order.kind = Frontier::OrderKind::Chat;
  order.operands = {1, 2, 3, 4};
  return order;
}

/// The Sim a snapshot of _sim reads back as; a snapshot that does not read back fails the test here.
Frontier::Sim Reload(const Frontier::Sim& _sim)
{
  std::optional<Frontier::Sim> reloaded = Frontier::Snapshot::Read(Frontier::Snapshot::Write(_sim));
  if (!reloaded.has_value())
  {
    Assert::Fail(L"the snapshot did not read back"); // noreturn, which is what the optional access below relies on
  }
  return *reloaded;
}

/// A match a little way in, with a surrendered seat, orders applied and dropped, and orders pending.
Frontier::Sim Busy()
{
  Frontier::Sim sim(ThreeSeats());
  for (std::uint32_t tick = 1; tick <= 50; ++tick)
  {
    sim.Submit(Chat(tick, static_cast<std::uint8_t>(tick % 4)));
    if (tick == 20)
    {
      Frontier::Order surrender = Chat(tick, 2);
      surrender.kind = Frontier::OrderKind::Surrender;
      sim.Submit(surrender);
    }
    sim.Advance();
  }
  sim.Submit(Chat(60, 0));
  sim.Submit(Chat(55, 1));
  sim.Submit(Chat(55, 0));
  return sim;
}

} // namespace

TEST_CLASS(SnapshotTests)
{
public:
  TEST_METHOD(AReloadedSimIsIndistinguishableFromTheOriginal)
  {
    Frontier::Sim original = Busy();
    Logger::WriteMessage(
      (L"measured: the snapshot of the three-seat match is " + std::to_wstring(Frontier::Snapshot::Write(original).size()) + L" bytes")
        .c_str());
    Frontier::Sim reloaded = Reload(original);
    Assert::IsTrue(original.Settings() == reloaded.Settings());
    Assert::AreEqual(original.Tick(), reloaded.Tick());
    Assert::AreEqual(original.Hash(), reloaded.Hash());
    Assert::AreEqual(original.ComputeHash(), reloaded.ComputeHash());
    Assert::IsTrue(original.Stream().GetState() == reloaded.Stream().GetState());
    Assert::IsTrue(original.Seats().size() == reloaded.Seats().size());
    for (std::size_t seat = 0; seat < original.Seats().size(); ++seat)
    {
      Assert::IsTrue(original.Seats()[seat] == reloaded.Seats()[seat]);
    }
    Assert::IsTrue(original.Seats()[2].defeated, L"seat 2 surrendered");
    Assert::AreEqual(original.AppliedOrders(), reloaded.AppliedOrders());
    Assert::AreEqual(original.DroppedOrders(), reloaded.DroppedOrders());
    Assert::AreEqual(original.Finished(), reloaded.Finished());
    Assert::AreEqual(static_cast<int>(original.WinningAlliance()), static_cast<int>(reloaded.WinningAlliance()));
    Assert::AreEqual(original.PublishDue(), reloaded.PublishDue());
    Assert::IsTrue(original.Orders().Entries() == reloaded.Orders().Entries());
    Assert::AreEqual(original.Orders().NextArrival(), reloaded.Orders().NextArrival());
    // The pending orders apply in both, in the same order, on the same ticks.
    for (std::uint32_t tick = 0; tick < 100; ++tick)
    {
      original.Advance();
      reloaded.Advance();
      Assert::AreEqual(original.Hash(), reloaded.Hash());
    }
    Assert::IsTrue(original.Orders().Empty());
    Assert::IsTrue(reloaded.Orders().Empty());
  }

  TEST_METHOD(WritingTheSameSimTwiceGivesTheSameBytes)
  {
    const Frontier::Sim sim = Busy();
    Assert::IsTrue(Frontier::Snapshot::Write(sim) == Frontier::Snapshot::Write(sim));
    const Frontier::Sim again = Busy();
    Assert::IsTrue(Frontier::Snapshot::Write(sim) == Frontier::Snapshot::Write(again), L"the same history must give the same snapshot");
  }

  TEST_METHOD(ATruncatedSnapshotIsRefusedAtEveryLength)
  {
    const std::vector<std::byte> bytes = Frontier::Snapshot::Write(Busy());
    for (std::size_t length = 0; length < bytes.size(); ++length)
    {
      if (Frontier::Snapshot::Read(std::span<const std::byte>(bytes.data(), length)).has_value())
      {
        Assert::Fail((L"a snapshot cut to " + std::to_wstring(length) + L" bytes read back").c_str());
      }
    }
    std::vector<std::byte> longer = bytes;
    longer.push_back(std::byte{0});
    Assert::IsFalse(Frontier::Snapshot::Read(longer).has_value(), L"trailing bytes are refused");
  }

  TEST_METHOD(AnAlteredByteIsRefused)
  {
    const std::vector<std::byte> bytes = Frontier::Snapshot::Write(Busy());
    for (std::size_t index = 0; index < bytes.size(); index += 7)
    {
      std::vector<std::byte> altered = bytes;
      altered[index] ^= std::byte{0x5A};
      if (Frontier::Snapshot::Read(altered).has_value())
      {
        Assert::Fail((L"a snapshot with byte " + std::to_wstring(index) + L" altered read back").c_str());
      }
    }
  }

  TEST_METHOD(TheHeaderIsCheckedFirst)
  {
    Neuron::ByteWriter writer;
    writer.WriteHeader({Frontier::SNAPSHOT_MAGIC, static_cast<std::uint16_t>(Frontier::SNAPSHOT_VERSION + 1)});
    Assert::IsFalse(Frontier::Snapshot::Read(writer.Bytes()).has_value(), L"a later version is refused");
    writer.Clear();
    writer.WriteHeader({Frontier::SNAPSHOT_MAGIC ^ 1u, Frontier::SNAPSHOT_VERSION});
    Assert::IsFalse(Frontier::Snapshot::Read(writer.Bytes()).has_value(), L"another magic is refused");
  }

  TEST_METHOD(ASnapshotBeforeTheFirstTickReadsBack)
  {
    const Frontier::Sim fresh(ThreeSeats());
    Assert::AreEqual(static_cast<std::uint32_t>(0), fresh.Tick());
    Assert::AreEqual(static_cast<std::uint64_t>(0), fresh.Hash());
    const Frontier::Sim reloaded = Reload(fresh);
    Assert::AreEqual(fresh.ComputeHash(), reloaded.ComputeHash());
  }
};

} // namespace SimTests
