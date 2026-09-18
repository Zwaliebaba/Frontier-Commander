#include "pch.h"

#include "OrderValidation.h"
#include "Sim.h"

#include "FixedPoint.h"

#include <cstdint>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// The trust model of TechnicalDesign.md §4.7: every order is judged against what the seat owns,
// sees and can afford, every reason a client may be given fires on a crafted order, and what
// stage 1 applies is the validated order rather than the submitted one.
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

constexpr std::int32_t CELL = Neuron::SUBUNITS_PER_CELL;

Frontier::MatchSettings TwoSeats()
{
  Frontier::MatchSettings settings{};
  settings.seed = 7;
  settings.sizeClass = Frontier::SizeClass::Small;
  settings.seatCount = 2;
  settings.baseLevel = Frontier::BaseLevel::Nothing;
  settings.powerLevel = Frontier::PowerLevel::Medium;
  settings.victory = Frontier::VictoryCondition::Annihilation;
  settings.seats[0] = {Frontier::SeatKind::Human, 0};
  settings.seats[1] = {Frontier::SeatKind::Ai, 1};
  return settings;
}

Frontier::LandscapeDefinition FlatLandscape()
{
  Frontier::LandscapeDefinition definition{};
  definition.version = Frontier::LANDSCAPE_DEFINITION_VERSION;
  definition.sizeClass = Frontier::SizeClass::Small;
  definition.cellsPerSide = Frontier::SIZE_CLASS_CELLS[0];
  definition.seed = 1;
  definition.palette = "Default";
  definition.tiles = {{0, 0, 512, 170, 90, 100, 48, 70, 1, 32, ""}};
  definition.starts = {{16, 16}, {100, 100}};
  return definition;
}

Frontier::Order Ordered(Frontier::OrderKind _kind, std::uint8_t _seat, std::int32_t _a = 0, std::int32_t _b = 0, std::int32_t _c = 0,
                        std::int32_t _d = 0)
{
  Frontier::Order order{};
  order.tick = 1;
  order.seat = _seat;
  order.kind = _kind;
  order.operands = {_a, _b, _c, _d};
  return order;
}

/// A match with a landscape, a device for each seat and a standing structure for seat 0.
struct Fixture
{
  Frontier::Sim sim{TwoSeats(), NoContent()};
  Frontier::ObjectId mine;
  Frontier::ObjectId theirs;
  Frontier::ObjectId myStructure;
  Frontier::ObjectId theirStructure;

  Fixture()
  {
    Assert::IsTrue(sim.CreateLandscape(FlatLandscape()));
    Frontier::Device device{};
    device.seat = 0;
    device.x = 20 * CELL;
    device.z = 20 * CELL;
    device.hitPoints = 100;
    mine = sim.Objects().Create(device);
    device.seat = 1;
    device.x = 90 * CELL;
    device.z = 90 * CELL;
    theirs = sim.Objects().Create(device);

    Frontier::Structure structure{};
    structure.seat = 0;
    structure.cellX = 18;
    structure.cellY = 18;
    structure.state = Frontier::StructureState::Standing;
    myStructure = sim.Objects().Create(structure);
    structure.seat = 1;
    structure.cellX = 92;
    structure.cellY = 92;
    theirStructure = sim.Objects().Create(structure);

    sim.SeatAt(0).structureCap = 10;
    sim.SeatAt(0).structureCount = 1;
    sim.SeatAt(0).deviceCap = 10;
    sim.SeatAt(0).deviceCount = 1;
  }

  [[nodiscard]] Frontier::OrderContext Context() const
  {
    return {&sim.Objects(), sim.Seats(), &sim.Terrain(), sim.Tick()};
  }

  [[nodiscard]] Frontier::RejectReason Judge(const Frontier::Order& _order) const
  {
    return Frontier::ValidateOrder(_order, Context()).reason;
  }

  /// Makes seat 0 see the cell a position falls in, as S9's refresh will.
  void Reveal(std::int32_t _x, std::int32_t _z)
  {
    const std::uint32_t side = sim.Terrain().Definition().cellsPerSide;
    const std::size_t cell = static_cast<std::size_t>(_z >> Neuron::SUBUNITS_PER_CELL_SHIFT) * side +
                             static_cast<std::size_t>(_x >> Neuron::SUBUNITS_PER_CELL_SHIFT);
    sim.SeatAt(0).fogState[cell] = Frontier::FogState::Visible;
  }
};

} // namespace

TEST_CLASS(OrderValidationTests)
{
public:
  TEST_METHOD(EveryReasonFiresOnACraftedOrder)
  {
    Fixture fixture;
    using Kind = Frontier::OrderKind;
    using Reason = Frontier::RejectReason;

    Assert::IsTrue(Reason::Accepted ==
                     fixture.Judge(Ordered(Kind::Move, 0, static_cast<std::int32_t>(fixture.mine.value), 30 * CELL, 30 * CELL)),
                   L"a move on one's own device to a point on the map");

    Assert::IsTrue(Reason::NotOwned ==
                     fixture.Judge(Ordered(Kind::Move, 0, static_cast<std::int32_t>(fixture.theirs.value), 30 * CELL, 30 * CELL)),
                   L"NotOwned: the device belongs to the other seat");
    Assert::IsTrue(Reason::NotOwned == fixture.Judge(Ordered(Kind::Stop, 0, 9999)), L"NotOwned: no such device");

    Assert::IsTrue(Reason::NotVisible == fixture.Judge(Ordered(Kind::Attack, 0, static_cast<std::int32_t>(fixture.mine.value),
                                                               static_cast<std::int32_t>(fixture.theirs.value),
                                                               static_cast<std::int32_t>(Frontier::ObjectKind::Device))),
                   L"NotVisible: the target is real, unseen, and no ghost records it");

    Assert::IsTrue(Reason::InvalidTarget == fixture.Judge(Ordered(Kind::Attack, 0, static_cast<std::int32_t>(fixture.mine.value),
                                                                  static_cast<std::int32_t>(fixture.myStructure.value),
                                                                  static_cast<std::int32_t>(Frontier::ObjectKind::Structure))),
                   L"InvalidTarget: no firing on one's own");

    Assert::IsTrue(Reason::InvalidPlacement ==
                     fixture.Judge(Ordered(Kind::Move, 0, static_cast<std::int32_t>(fixture.mine.value), -1, 30 * CELL)),
                   L"InvalidPlacement: off the landscape");

    Assert::IsTrue(Reason::Malformed == fixture.Judge(Ordered(Kind::SetStance, 0, static_cast<std::int32_t>(fixture.mine.value), 9, 0)),
                   L"Malformed: no such stance axis");
    Assert::IsTrue(Reason::Malformed == fixture.Judge(Ordered(Kind::SetStance, 0, static_cast<std::int32_t>(fixture.mine.value), 1, 7)),
                   L"Malformed: a value outside that axis");
    Assert::IsTrue(Reason::Malformed == fixture.Judge(Ordered(Kind::Group, 0, static_cast<std::int32_t>(fixture.mine.value), 11)),
                   L"Malformed: there are ten control groups");

    Assert::IsTrue(
      Reason::AtCap ==
        [&fixture]
        {
          fixture.sim.SeatAt(0).structureCount = fixture.sim.SeatAt(0).structureCap;
          const Frontier::RejectReason reason = fixture.Judge(Ordered(Kind::PlaceStructure, 0, 0, 30, 30));
          fixture.sim.SeatAt(0).structureCount = 1;
          return reason;
        }(),
      L"AtCap: the structure cap is reached");

    Assert::IsTrue(
      Reason::CannotAfford ==
        [&fixture]
        {
          const std::int32_t power = fixture.sim.SeatAt(0).powerHundredths;
          fixture.sim.SeatAt(0).powerHundredths = 0;
          const Frontier::RejectReason reason = fixture.Judge(Ordered(Kind::PlaceStructure, 0, 0, 30, 30));
          fixture.sim.SeatAt(0).powerHundredths = power;
          return reason;
        }(),
      L"CannotAfford: an empty stockpile buys nothing");

    Assert::IsTrue(
      Reason::NoCommandPost ==
        [&fixture]
        {
          // Seat 1's only structure is still a plan, so it has nothing standing to build from.
          fixture.sim.Objects().FindStructure(fixture.theirStructure)->state = Frontier::StructureState::Plan;
          const Frontier::RejectReason reason = fixture.Judge(Ordered(Kind::PlaceStructure, 1, 0, 30, 30));
          fixture.sim.Objects().FindStructure(fixture.theirStructure)->state = Frontier::StructureState::Standing;
          return reason;
        }(),
      L"NoCommandPost: nothing standing to build from");

    Assert::IsTrue(Reason::NotResearched ==
                     fixture.Judge(Ordered(Kind::SetProduction, 0, static_cast<std::int32_t>(fixture.myStructure.value), 0)),
                   L"NotResearched: the seat has saved no design 0");
  }

  TEST_METHOD(AnAttackOnAnUnseenTargetBecomesAnAttackMoveToItsLastKnownPosition)
  {
    Fixture fixture;
    const Frontier::Order attack =
      Ordered(Frontier::OrderKind::Attack, 0, static_cast<std::int32_t>(fixture.mine.value),
              static_cast<std::int32_t>(fixture.theirStructure.value), static_cast<std::int32_t>(Frontier::ObjectKind::Structure));

    // With no ghost there is nowhere to send it.
    Assert::IsTrue(Frontier::RejectReason::NotVisible == fixture.Judge(attack));

    // Seen once and remembered: the order becomes an attack-move to the cell it stood in.
    fixture.sim.SeatAt(0).ghosts.push_back({fixture.theirStructure, 1, 0, 92, 92, 5});
    const Frontier::OrderCheck checked = Frontier::ValidateOrder(attack, fixture.Context());
    Assert::IsTrue(checked.Accepted());
    Assert::IsTrue(Frontier::OrderKind::AttackMove == checked.order.kind, L"rewritten, not rejected");
    Assert::AreEqual(92 * CELL + CELL / 2, checked.order.operands[1], L"the centre of the cell it was last seen in");
    Assert::AreEqual(92 * CELL + CELL / 2, checked.order.operands[2]);

    // Visible now: the Attack stands as given.
    fixture.Reveal(92 * CELL, 92 * CELL);
    const Frontier::OrderCheck seen = Frontier::ValidateOrder(attack, fixture.Context());
    Assert::IsTrue(seen.Accepted());
    Assert::IsTrue(Frontier::OrderKind::Attack == seen.order.kind, L"a target in sight is attacked, not moved to");
  }

  TEST_METHOD(AnAcceptedMoveChangesTheDevicesOrder)
  {
    Fixture fixture;
    fixture.sim.Submit(Ordered(Frontier::OrderKind::Move, 0, static_cast<std::int32_t>(fixture.mine.value), 31 * CELL, 33 * CELL));
    fixture.sim.Advance();
    const Frontier::Device* device = fixture.sim.Objects().FindDevice(fixture.mine);
    Assert::IsTrue(Frontier::PrimaryOrder::Move == device->primaryOrder);
    Assert::AreEqual(31 * CELL, device->destinationX);
    Assert::AreEqual(33 * CELL, device->destinationZ);
    Assert::AreEqual(std::uint32_t{1}, fixture.sim.AppliedOrders());
    Assert::IsTrue(fixture.sim.Seats()[0].rejections.empty());

    // And a stance and a group land on the same device without disturbing the move.
    fixture.sim.Submit(Ordered(Frontier::OrderKind::SetStance, 0, static_cast<std::int32_t>(fixture.mine.value),
                               static_cast<std::int32_t>(Frontier::StanceAxis::Retreat),
                               static_cast<std::int32_t>(Frontier::RetreatStance::Never)));
    fixture.sim.Submit(Ordered(Frontier::OrderKind::Group, 0, static_cast<std::int32_t>(fixture.mine.value), 4));
    fixture.sim.Advance();
    device = fixture.sim.Objects().FindDevice(fixture.mine);
    Assert::IsTrue(Frontier::RetreatStance::Never == device->retreat);
    Assert::IsTrue(Frontier::FireStance::FireAtWill == device->fire, L"one axis at a time: the others are untouched");
    Assert::AreEqual(static_cast<int>(4), static_cast<int>(device->group));
    Assert::IsTrue(Frontier::PrimaryOrder::Move == device->primaryOrder);
  }

  TEST_METHOD(RejectionsAreRecordedPerSeatInArrivalOrderAndClearedEachTick)
  {
    Fixture fixture;
    // Three bad orders from seat 0, submitted in this order, and one good one between them.
    fixture.sim.Submit(Ordered(Frontier::OrderKind::Stop, 0, 4242));                                        // NotOwned
    fixture.sim.Submit(Ordered(Frontier::OrderKind::Move, 0, static_cast<std::int32_t>(fixture.mine.value), // accepted
                               25 * CELL, 25 * CELL));
    fixture.sim.Submit(Ordered(Frontier::OrderKind::Group, 0, static_cast<std::int32_t>(fixture.mine.value), 99));   // Malformed
    fixture.sim.Submit(Ordered(Frontier::OrderKind::Move, 0, static_cast<std::int32_t>(fixture.mine.value), -5, 0)); // InvalidPlacement
    fixture.sim.Advance();

    const std::vector<Frontier::OrderRejection>& rejections = fixture.sim.Seats()[0].rejections;
    Assert::AreEqual(std::size_t{3}, rejections.size());
    Assert::IsTrue(Frontier::RejectReason::NotOwned == rejections[0].reason, L"arrival order within a seat is preserved");
    Assert::IsTrue(Frontier::OrderKind::Stop == rejections[0].kind);
    Assert::IsTrue(Frontier::RejectReason::Malformed == rejections[1].reason);
    Assert::IsTrue(Frontier::RejectReason::InvalidPlacement == rejections[2].reason);
    Assert::IsTrue(fixture.sim.Seats()[1].rejections.empty(), L"seat 1 sent nothing and is told nothing");
    Assert::AreEqual(std::uint32_t{3}, fixture.sim.DroppedOrders());

    const std::uint64_t withRejections = fixture.sim.ComputeHash();
    fixture.sim.Advance();
    Assert::IsTrue(fixture.sim.Seats()[0].rejections.empty(), L"a tick reports what it refused, not a running tally");
    Assert::AreNotEqual(withRejections, fixture.sim.ComputeHash(), L"and the list is in the hash, so clearing it moves it");
  }

  TEST_METHOD(TheRejectionListIsPartOfTheHash)
  {
    // Two matches identical but for one refused order must not hash alike: a host that accepts
    // what another refuses has diverged, and the hash is what says so.
    Fixture quiet;
    Fixture noisy;
    quiet.sim.Advance();
    noisy.sim.Submit(Ordered(Frontier::OrderKind::Stop, 0, 4242));
    noisy.sim.Advance();
    Assert::AreNotEqual(quiet.sim.Hash(), noisy.sim.Hash());
  }

  TEST_METHOD(AnOrderFromASeatOutsideTheMatchOrDefeatedIsDroppedWithoutAReason)
  {
    Fixture fixture;
    fixture.sim.Submit(Ordered(Frontier::OrderKind::Surrender, 0));
    fixture.sim.Advance();
    Assert::IsTrue(fixture.sim.Seats()[0].defeated);
    Assert::IsTrue(fixture.sim.Seats()[0].surrendered, L"surrender is distinguishable from annihilation");

    fixture.sim.Submit(Ordered(Frontier::OrderKind::Move, 0, static_cast<std::int32_t>(fixture.mine.value), 25 * CELL, 25 * CELL));
    fixture.sim.Submit(Ordered(Frontier::OrderKind::Move, 7, 1, 0, 0));
    fixture.sim.Advance();
    Assert::IsTrue(fixture.sim.Seats()[0].rejections.empty(), L"the fault is in who sent it, so there is no rejection to report to anyone");
  }
};

} // namespace SimTests
