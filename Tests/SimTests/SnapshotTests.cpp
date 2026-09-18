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
/// The tables a match is played by. These suites exercise the simulation rather than the rules, so
/// an empty tree is the honest one: no row is read, and Q20's binding is still exercised, because
/// the snapshot carries this tree's hash and refuses any other.
const Frontier::ContentTree& NoContent()
{
  static const Frontier::ContentTree TREE{};
  return TREE;
}

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
  std::optional<Frontier::Sim> reloaded = Frontier::Snapshot::Read(Frontier::Snapshot::Write(_sim), NoContent());
  if (!reloaded.has_value())
  {
    Assert::Fail(L"the snapshot did not read back"); // noreturn, which is what the optional access below relies on
  }
  return *reloaded;
}

/// Fills a seat's every variable-length field, so that the snapshot's counts and bounds are
/// exercised rather than written as zero. The systems that will do this are S3, S5 and S6.
void Furnish(Frontier::Seat& _seat, std::uint32_t _salt)
{
  _seat.stockpileCapHundredths = 250000 + static_cast<std::int32_t>(_salt);
  _seat.researchComplete = {1, 4, 9, 16 + _salt};
  // The lab is part of the record now (m1-vertical-slice/S6): a destroyed lab loses its
  // progress, and a list that does not say whose progress it is cannot enforce that.
  _seat.researchActive = {{{3, Frontier::ObjectKind::Structure}, 7, 120}, {{4, Frontier::ObjectKind::Structure}, 11, 60 + _salt}};
  Frontier::DeviceDesign design{};
  design.chassis = 1 + _salt;
  design.drive = 2;
  design.modules = {3, 4, 0, 0, 0, 0, 0, 0};
  design.moduleCount = 2;
  _seat.designs.push_back(design);
  design.chassis = 9;
  design.moduleCount = 1;
  _seat.designs.push_back(design);
  _seat.deviceCount = 3 + _salt;
  _seat.deviceCap = 120;
  _seat.structureCount = 2;
  _seat.structureCap = 80;
  _seat.ghosts.Record({{40 + _salt, Frontier::ObjectKind::Structure}, 1, 5, 12, 13, 900});
  _seat.ghosts.Record({{41 + _salt, Frontier::ObjectKind::Structure}, 2, 6, 30, 31, 950});
  // A small grid rather than a landscape's: what is under test is that the counts and the states
  // survive the stream, and 64 cells exercise that as well as a million would.
  _seat.fog.Resize(8);
  const std::uint32_t lit = 3 + _salt;
  _seat.fog.AddViewer(lit % 8, lit / 8);
  _seat.fog.AddViewer(lit % 8, lit / 8);
  // Cell 9 is seen and then left: explored, which no viewer count can produce on its own.
  _seat.fog.AddViewer(1, 1);
  _seat.fog.RemoveViewer(1, 1);
}

/// One of each kind, so that every map, every record and the id counter are on the wire.
void Populate(Frontier::Sim& _sim)
{
  Frontier::Device device{};
  device.seat = 0;
  device.design = 1;
  device.x = 4096;
  device.y = 256;
  device.z = -2048;
  device.facing = 0x8000;
  device.hitPoints = 240;
  device.experience = 12;
  device.primaryOrder = Frontier::PrimaryOrder::AttackMove;
  device.destinationX = 8192;
  device.destinationZ = 1024;
  device.fire = Frontier::FireStance::HoldFire;
  device.retreat = Frontier::RetreatStance::AtQuarter;
  device.group = 5;
  device.reloadTicks = {5, 2, 0, 0, 0, 0, 0, 0};
  const Frontier::ObjectId shooter = _sim.Objects().Create(device);
  device.seat = 1;
  device.x = -4096;
  device.primaryOrder = Frontier::PrimaryOrder::Stop;
  const Frontier::ObjectId other = _sim.Objects().Create(device);

  Frontier::Structure structure{};
  structure.seat = 0;
  structure.design = 3;
  structure.cellX = 17;
  structure.cellY = 42;
  structure.y = 128;
  structure.state = Frontier::StructureState::Standing;
  structure.hitPoints = 600;
  structure.buildEffortHundredths = 10000;
  structure.modules = {1, 2, 3, 0};
  structure.moduleCount = 3;
  structure.working = other;
  structure.workRemainingTicks = 55;
  _sim.Objects().Create(structure);

  Frontier::Projectile projectile{};
  projectile.seat = 0;
  projectile.shooter = shooter;
  projectile.module = 4;
  projectile.x = 4200;
  projectile.y = 300;
  projectile.z = -2000;
  projectile.impactX = -4096;
  projectile.impactY = 256;
  projectile.impactZ = 0;
  projectile.ticksToImpact = 9;
  _sim.Objects().Create(projectile);

  Frontier::Feature feature{};
  feature.design = 2;
  feature.cellX = 60;
  feature.cellY = 61;
  feature.y = 96;
  feature.facing = 0x2000;
  _sim.Objects().Create(feature);

  Frontier::Wreck wreck{};
  wreck.seat = 1;
  wreck.origin = other;
  wreck.design = 1;
  wreck.x = -4000;
  wreck.y = 250;
  wreck.z = 100;
  wreck.facing = 0xC000;
  wreck.decayTicks = 300;
  const Frontier::ObjectId hulk = _sim.Objects().Create(wreck);
  // A removed object must not come back through the snapshot, and the counter must not rewind.
  Assert::IsTrue(_sim.Objects().Remove(hulk));
}

/// A match a little way in, with a surrendered seat, orders applied and dropped, and orders pending.
Frontier::Sim Busy()
{
  Frontier::Sim sim(ThreeSeats(), NoContent());
  Populate(sim);
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
  // Furnished after the ticks rather than before them. Stages 2, 3 and 4 OWN several of these
  // fields now - the counts, the caps, and a lab's progress, which m1-vertical-slice/S6 drops when
  // the lab is not a standing lab - so a fixture that filled them first would be measuring what
  // the tick left rather than what the stream carries.
  for (std::uint8_t seat = 0; seat < 3; ++seat)
  {
    Furnish(sim.SeatAt(seat), seat);
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

  TEST_METHOD(APopulatedWorldRoundTripsWithItsIdsAndItsCounter)
  {
    const Frontier::Sim original = Busy();
    const Frontier::Sim reloaded = Reload(original);
    const Frontier::World& before = original.Objects();
    const Frontier::World& after = reloaded.Objects();
    for (const Frontier::ObjectKind kind : {Frontier::ObjectKind::Device, Frontier::ObjectKind::Structure, Frontier::ObjectKind::Projectile,
                                            Frontier::ObjectKind::Feature, Frontier::ObjectKind::Wreck})
    {
      Assert::AreEqual(before.Count(kind), after.Count(kind));
    }
    Assert::AreEqual(std::size_t{0}, after.Count(Frontier::ObjectKind::Wreck), L"the removed wreck did not come back");
    Assert::AreEqual(before.NextId(), after.NextId(), L"the counter is state: the next id must be the same one");

    std::vector<std::uint32_t> ids;
    before.ForEachDevice(
      [&ids, &after](Frontier::ObjectId _id, const Frontier::Device& _device)
      {
        ids.push_back(_id.value);
        const Frontier::Device* reloadedDevice = after.FindDevice(_id);
        Assert::IsNotNull(reloadedDevice, L"every id resolves in the reloaded world");
        Assert::IsTrue(_device == *reloadedDevice, L"and to a record equal field for field");
      });
    Assert::AreEqual(std::size_t{2}, ids.size());
    before.ForEachStructure([&after](Frontier::ObjectId _id, const Frontier::Structure& _structure)
                            { Assert::IsTrue(_structure == *after.FindStructure(_id)); });
    before.ForEachProjectile([&after](Frontier::ObjectId _id, const Frontier::Projectile& _projectile)
                             { Assert::IsTrue(_projectile == *after.FindProjectile(_id)); });
    before.ForEachFeature([&after](Frontier::ObjectId _id, const Frontier::Feature& _feature)
                          { Assert::IsTrue(_feature == *after.FindFeature(_id)); });
  }

  TEST_METHOD(TheHashMovesForASingleFieldOfEveryRecordAndEverySeat)
  {
    // The hash is what a determinism test compares, so a field the hash does not read is a field
    // two machines may disagree about in silence. One mutation apiece, each one must move it.
    const Frontier::Sim original = Busy();
    const std::uint64_t baseline = original.ComputeHash();

    const auto moved = [baseline](Frontier::Sim& _sim, const wchar_t* _what) { Assert::AreNotEqual(baseline, _sim.ComputeHash(), _what); };

    Frontier::Sim device = Busy();
    device.Objects().FindDevice({1, Frontier::ObjectKind::Device})->hitPoints += 1;
    moved(device, L"a device's hit points");

    Frontier::Sim reload = Busy();
    reload.Objects().FindDevice({1, Frontier::ObjectKind::Device})->reloadTicks[7] = 1;
    moved(reload, L"a device's last reload slot");

    Frontier::Sim structure = Busy();
    structure.Objects().FindStructure({3, Frontier::ObjectKind::Structure})->buildEffortHundredths -= 1;
    moved(structure, L"a structure's build progress");

    Frontier::Sim projectile = Busy();
    projectile.Objects().FindProjectile({4, Frontier::ObjectKind::Projectile})->impactZ += 1;
    moved(projectile, L"a projectile's impact");

    Frontier::Sim feature = Busy();
    feature.Objects().FindFeature({5, Frontier::ObjectKind::Feature})->facing += 1;
    moved(feature, L"a feature's facing");

    Frontier::Sim counter = Busy();
    counter.Objects().SetNextId(counter.Objects().NextId() + 1);
    moved(counter, L"the id counter, which decides every id still to be issued");

    Frontier::Sim removed = Busy();
    Assert::IsTrue(removed.Objects().Remove({1, Frontier::ObjectKind::Device}));
    moved(removed, L"a device removed");

    Frontier::Sim power = Busy();
    power.SeatAt(1).powerHundredths += 1;
    moved(power, L"a seat's power");

    Frontier::Sim research = Busy();
    research.SeatAt(1).researchActive[0].remainingTicks -= 1;
    moved(research, L"a seat's research in progress");

    Frontier::Sim design = Busy();
    design.SeatAt(2).designs[1].moduleCount = 2;
    moved(design, L"a seat's design");

    Frontier::Sim cap = Busy();
    cap.SeatAt(0).structureCap += 1;
    moved(cap, L"a seat's structure cap");

    Frontier::Sim ghost = Busy();
    {
      Frontier::Ghost later = ghost.SeatAt(0).ghosts.All()[1];
      later.seenTick += 1;
      ghost.SeatAt(0).ghosts.Record(later);
    }
    moved(ghost, L"a seat's ghost store");

    Frontier::Sim surrender = Busy();
    surrender.SeatAt(0).surrendered = true;
    moved(surrender, L"a seat's surrender, which defeated alone does not say");

    Frontier::Sim fog = Busy();
    fog.SeatAt(2).fog.AddViewer(1, 1);
    moved(fog, L"one cell of a seat's fog");
  }

  TEST_METHOD(ASnapshotIsRefusedAgainstTablesItWasNotWrittenAgainst)
  {
    // The whole of Q20's answer: a match reloaded against different rules is a different match,
    // and the digest turns that from a divergence nobody notices into a refusal here.
    Frontier::ContentTree tables{};
    Frontier::ChassisDesc chassis{};
    chassis.id = "ChassisLight";
    chassis.hitPoints = 100;
    tables.components.chassis.push_back(chassis);

    Frontier::Sim sim(ThreeSeats(), tables);
    sim.Advance();
    const std::vector<std::byte> bytes = Frontier::Snapshot::Write(sim);

    Assert::IsTrue(Frontier::Snapshot::Read(bytes, tables).has_value(), L"the tables it was written against");
    Assert::IsFalse(Frontier::Snapshot::Read(bytes, NoContent()).has_value(), L"no tables at all");

    Frontier::ContentTree edited = tables;
    edited.components.chassis[0].hitPoints += 1;
    Assert::IsFalse(Frontier::Snapshot::Read(bytes, edited).has_value(), L"one number of one row changed");

    Frontier::ContentTree same = tables;
    Assert::IsTrue(Frontier::Snapshot::Read(bytes, same).has_value(), L"an equal tree, not the same object");
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
      if (Frontier::Snapshot::Read(std::span<const std::byte>(bytes.data(), length), NoContent()).has_value())
      {
        Assert::Fail((L"a snapshot cut to " + std::to_wstring(length) + L" bytes read back").c_str());
      }
    }
    std::vector<std::byte> longer = bytes;
    longer.push_back(std::byte{0});
    Assert::IsFalse(Frontier::Snapshot::Read(longer, NoContent()).has_value(), L"trailing bytes are refused");
  }

  TEST_METHOD(AnAlteredByteIsRefused)
  {
    const std::vector<std::byte> bytes = Frontier::Snapshot::Write(Busy());
    for (std::size_t index = 0; index < bytes.size(); index += 7)
    {
      std::vector<std::byte> altered = bytes;
      altered[index] ^= std::byte{0x5A};
      if (Frontier::Snapshot::Read(altered, NoContent()).has_value())
      {
        Assert::Fail((L"a snapshot with byte " + std::to_wstring(index) + L" altered read back").c_str());
      }
    }
  }

  TEST_METHOD(TheHeaderIsCheckedFirst)
  {
    Neuron::ByteWriter writer;
    writer.WriteHeader({Frontier::SNAPSHOT_MAGIC, static_cast<std::uint16_t>(Frontier::SNAPSHOT_VERSION + 1)});
    Assert::IsFalse(Frontier::Snapshot::Read(writer.Bytes(), NoContent()).has_value(), L"a later version is refused");
    writer.Clear();
    writer.WriteHeader({Frontier::SNAPSHOT_MAGIC ^ 1u, Frontier::SNAPSHOT_VERSION});
    Assert::IsFalse(Frontier::Snapshot::Read(writer.Bytes(), NoContent()).has_value(), L"another magic is refused");
  }

  TEST_METHOD(ASnapshotBeforeTheFirstTickReadsBack)
  {
    const Frontier::Sim fresh(ThreeSeats(), NoContent());
    Assert::AreEqual(static_cast<std::uint32_t>(0), fresh.Tick());
    Assert::AreEqual(static_cast<std::uint64_t>(0), fresh.Hash());
    const Frontier::Sim reloaded = Reload(fresh);
    Assert::AreEqual(fresh.ComputeHash(), reloaded.ComputeHash());
  }
};

} // namespace SimTests
