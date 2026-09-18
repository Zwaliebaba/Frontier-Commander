#include "pch.h"

#include "Economy.h"
#include "OrderValidation.h"
#include "Sim.h"
#include "Snapshot.h"

#include "FixedPoint.h"

#include <cstdint>
#include <optional>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// The economy of GameDesign.md §4, in the numbers that section states: a served extractor at 5
// power a second and a command post at 1, a generator that serves the four nearest unserved
// extractors within 48 cells, a stockpile capped at 1,000 plus 500 a generator that cannot be
// parked around, and the two army caps.
//
// Every number here is read from the tables rather than written into the test, so that rebalancing
// GameData\Structures.json moves the assertions with it. What the test pins is the RULE - four,
// nearest, unserved, 48 cells, clipped - and the arithmetic the design's own worked numbers give.
namespace SimTests
{

namespace
{

constexpr std::uint32_t TICKS = static_cast<std::uint32_t>(Neuron::TICKS_PER_SECOND);

/// The tables of GameDesign.md §4 and §5, as this task needs them: the three roles the economy
/// reads, with the numbers GameData\Structures.json ships. Built here rather than loaded so that
/// the suite does not need a GameData directory beside the test binary, and asserted against the
/// design's per-second figures so that a table that drifts from the design fails here.
enum class Row : std::uint32_t
{
  CommandPost = 0,
  Extractor = 1,
  Generator = 2,
  Factory = 3
};

const Frontier::ContentTree& Tables()
{
  static const Frontier::ContentTree TREE = []
  {
    Frontier::ContentTree tree;
    Frontier::StructureDesc post{};
    post.id = "CommandPost";
    post.role = Frontier::StructureRole::CommandPost;
    post.footprintCellsX = 3;
    post.footprintCellsY = 3;
    post.costHundredths = 50000;
    post.buildTimeTicks = 1200;
    post.powerHundredthsPerTick = 5; // 1 power a second at 20 ticks
    Frontier::StructureDesc extractor{};
    extractor.id = "Extractor";
    extractor.role = Frontier::StructureRole::Extractor;
    extractor.footprintCellsX = 1;
    extractor.footprintCellsY = 1;
    extractor.costHundredths = 5000;
    extractor.buildTimeTicks = 300;
    extractor.powerHundredthsPerTick = 25; // 5 power a second at 20 ticks
    Frontier::StructureDesc generator{};
    generator.id = "Generator";
    generator.role = Frontier::StructureRole::Generator;
    generator.footprintCellsX = 2;
    generator.footprintCellsY = 2;
    generator.costHundredths = 25000;
    generator.buildTimeTicks = 800;
    generator.servesExtractors = 4;
    generator.serviceRangeSubunits = 48 * Neuron::SUBUNITS_PER_CELL;
    Frontier::StructureDesc factory{};
    factory.id = "Factory";
    factory.role = Frontier::StructureRole::Factory;
    factory.footprintCellsX = 3;
    factory.footprintCellsY = 3;
    factory.costHundredths = 40000;
    factory.buildTimeTicks = 1200;
    tree.structures.structures = {post, extractor, generator, factory};
    return tree;
  }();
  return TREE;
}

Frontier::MatchSettings TwoSeats(Frontier::PowerLevel _power = Frontier::PowerLevel::Medium,
                                 Frontier::DeviceCapLevel _devices = Frontier::DeviceCapLevel::Medium)
{
  Frontier::MatchSettings settings{};
  settings.seed = 7;
  settings.sizeClass = Frontier::SizeClass::Small;
  settings.seatCount = 2;
  settings.baseLevel = Frontier::BaseLevel::Nothing;
  settings.powerLevel = _power;
  settings.deviceCapLevel = _devices;
  settings.victory = Frontier::VictoryCondition::Annihilation;
  settings.seats[0] = {Frontier::SeatKind::Human, 0};
  settings.seats[1] = {Frontier::SeatKind::Ai, 1};
  return settings;
}

/// A landscape whose deposits are wherever the test wants them. The heights do not matter to the
/// economy: what a deposit is, is a cell.
Frontier::LandscapeDefinition LandscapeWith(std::vector<Frontier::CellPosition> _deposits)
{
  Frontier::LandscapeDefinition definition{};
  definition.version = Frontier::LANDSCAPE_DEFINITION_VERSION;
  definition.sizeClass = Frontier::SizeClass::Small;
  definition.cellsPerSide = Frontier::SIZE_CLASS_CELLS[0];
  definition.seed = 1;
  definition.palette = "Default";
  definition.tiles = {{0, 0, 512, 170, 90, 100, 48, 70, 1, 32, ""}};
  definition.starts = {{16, 16}, {100, 100}};
  definition.deposits = std::move(_deposits);
  return definition;
}

/// The Sim a snapshot of _sim reads back as; a snapshot that does not read back fails the test here.
Frontier::Sim Reload(const Frontier::Sim& _sim)
{
  std::optional<Frontier::Sim> reloaded = Frontier::Snapshot::Read(Frontier::Snapshot::Write(_sim), Tables());
  if (!reloaded.has_value())
  {
    Assert::Fail(L"the snapshot did not read back"); // noreturn, which is what the access below relies on
  }
  return *reloaded;
}

Frontier::ObjectId Standing(Frontier::Sim& _sim, std::uint8_t _seat, Row _row, std::uint32_t _cellX, std::uint32_t _cellY)
{
  Frontier::Structure structure{};
  structure.seat = _seat;
  structure.design = static_cast<std::uint32_t>(_row);
  structure.cellX = _cellX;
  structure.cellY = _cellY;
  structure.state = Frontier::StructureState::Standing;
  structure.hitPoints = 100;
  structure.buildEffortHundredths = 10000;
  structure.working = Frontier::NO_OBJECT;
  return _sim.Objects().Create(structure);
}

/// Makes a seat's fog say it has explored a rectangle of cells, which S4's placement rule now
/// requires (GameDesign.md §5: "anywhere the commander has explored").
void Reveal(Frontier::Sim& _sim, std::uint8_t _seat, std::uint32_t _cellX, std::uint32_t _cellY, std::uint32_t _cellsX = 1,
            std::uint32_t _cellsY = 1)
{
  for (std::uint32_t y = _cellY; y < _cellY + _cellsY; ++y)
  {
    for (std::uint32_t x = _cellX; x < _cellX + _cellsX; ++x)
    {
      _sim.SeatAt(_seat).fog.AddViewer(x, y);
    }
  }
}

} // namespace

TEST_CLASS(EconomyTests)
{
public:
  TEST_METHOD(AServedExtractorAndACommandPostPayTheDesignsPerSecondRate)
  {
    // GameDesign.md §4's table, read over exactly one second: a served extractor is 5 power and a
    // command post 1, so a base with one of each earns 600 hundredths in twenty ticks.
    Frontier::Sim sim(TwoSeats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(LandscapeWith({{20, 20}})));
    Standing(sim, 0, Row::CommandPost, 40, 40);
    Standing(sim, 0, Row::Extractor, 20, 20);
    Standing(sim, 0, Row::Generator, 22, 20);
    Frontier::Seat& seat = sim.SeatAt(0);
    seat.powerHundredths = 0;
    for (std::uint32_t tick = 0; tick < TICKS; ++tick)
    {
      sim.Advance();
    }
    Assert::AreEqual(600, seat.powerHundredths);
  }

  TEST_METHOD(AnExtractorEarnsNothingUntilAGeneratorReachesIt)
  {
    // The chain of GameDesign.md §4: the extractor is the bait and the generator is the target, so
    // an extractor on its own is worth nothing at all rather than worth less.
    Frontier::Sim sim(TwoSeats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(LandscapeWith({{20, 20}})));
    const Frontier::ObjectId extractor = Standing(sim, 0, Row::Extractor, 20, 20);
    Frontier::Seat& seat = sim.SeatAt(0);
    seat.powerHundredths = 0;
    sim.Advance();
    Assert::IsFalse(sim.Power().Served(extractor));
    Assert::AreEqual(0, seat.powerHundredths);

    Standing(sim, 0, Row::Generator, 24, 20);
    sim.Advance();
    Assert::IsTrue(sim.Power().Served(extractor));
    Assert::AreEqual(25, seat.powerHundredths);
  }

  TEST_METHOD(AnExtractorOffEveryDepositEarnsNothingHoweverClosePowerIs)
  {
    // The deposit is what an extractor draws from, so one standing on bare ground is not served
    // and is not income. Placement refuses to put one there; this is the same rule holding for a
    // structure that arrived some other way, such as out of a crafted snapshot.
    Frontier::Sim sim(TwoSeats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(LandscapeWith({{20, 20}})));
    const Frontier::ObjectId onDeposit = Standing(sim, 0, Row::Extractor, 20, 20);
    const Frontier::ObjectId onBareGround = Standing(sim, 0, Row::Extractor, 21, 20);
    Standing(sim, 0, Row::Generator, 22, 20);
    Frontier::Seat& seat = sim.SeatAt(0);
    seat.powerHundredths = 0;
    sim.Advance();
    Assert::IsTrue(sim.Power().Served(onDeposit));
    Assert::IsFalse(sim.Power().Served(onBareGround));
    Assert::AreEqual(25, seat.powerHundredths);
  }

  TEST_METHOD(AGeneratorServesFourAndTheFifthExtractorWaitsForAnother)
  {
    // "A generator serves the four nearest unserved extractors" (GameDesign.md §4). Five in reach
    // of one generator is four served and one idle; the fifth is the reason a second generator is
    // worth building rather than a fifth extractor.
    Frontier::Sim sim(TwoSeats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(LandscapeWith({{20, 20}, {21, 20}, {22, 20}, {23, 20}, {24, 20}})));
    std::vector<Frontier::ObjectId> extractors;
    for (std::uint32_t cell = 20; cell <= 24; ++cell)
    {
      extractors.push_back(Standing(sim, 0, Row::Extractor, cell, 20));
    }
    Standing(sim, 0, Row::Generator, 20, 21);
    Frontier::Seat& seat = sim.SeatAt(0);
    seat.powerHundredths = 0;
    sim.Advance();

    std::uint32_t served = 0;
    for (const Frontier::ObjectId& extractor : extractors)
    {
      served += sim.Power().Served(extractor) ? 1u : 0u;
    }
    Assert::AreEqual(4u, served);
    // The one left out is the farthest: the generator sits by the first, so the fifth column is
    // the one beyond the four nearest.
    Assert::IsFalse(sim.Power().Served(extractors[4]));
    Assert::AreEqual(100, seat.powerHundredths);
  }

  TEST_METHOD(AGeneratorAtFortyNineCellsServesNothingAndAtFortyEightServesEverything)
  {
    // The 48-cell radius is what keeps generators at the front (GameDesign.md §4), so the boundary
    // is a rule rather than a tolerance: the same pair one cell apart is all or nothing.
    Frontier::Sim outOfReach(TwoSeats(), Tables());
    Assert::IsTrue(outOfReach.CreateLandscape(LandscapeWith({{10, 10}})));
    const Frontier::ObjectId beyond = Standing(outOfReach, 0, Row::Extractor, 10, 10);
    // The generator's footprint is two cells, so its centre is one cell past its origin: an origin
    // 48 further along puts the centres 49 apart.
    Standing(outOfReach, 0, Row::Generator, 10 + 48, 10);
    outOfReach.SeatAt(0).powerHundredths = 0;
    outOfReach.Advance();
    Assert::IsFalse(outOfReach.Power().Served(beyond));
    Assert::AreEqual(0, outOfReach.SeatAt(0).powerHundredths);

    Frontier::Sim inReach(TwoSeats(), Tables());
    Assert::IsTrue(inReach.CreateLandscape(LandscapeWith({{10, 10}})));
    const Frontier::ObjectId within = Standing(inReach, 0, Row::Extractor, 10, 10);
    Standing(inReach, 0, Row::Generator, 10 + 47, 10);
    inReach.SeatAt(0).powerHundredths = 0;
    inReach.Advance();
    Assert::IsTrue(inReach.Power().Served(within));
    Assert::AreEqual(25, inReach.SeatAt(0).powerHundredths);
  }

  TEST_METHOD(TheStockpileCapIsAThousandPlusFiveHundredForEveryStandingGenerator)
  {
    Frontier::Sim sim(TwoSeats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(LandscapeWith({})));
    Frontier::Seat& seat = sim.SeatAt(0);
    sim.Advance();
    Assert::AreEqual(100000, seat.stockpileCapHundredths);

    Standing(sim, 0, Row::Generator, 20, 20);
    Standing(sim, 0, Row::Generator, 30, 30);
    sim.Advance();
    Assert::AreEqual(200000, seat.stockpileCapHundredths);

    // A generator still under construction is not a completed one, so it lifts nothing.
    Frontier::Structure building{};
    building.seat = 0;
    building.design = static_cast<std::uint32_t>(Row::Generator);
    building.cellX = 40;
    building.cellY = 40;
    building.state = Frontier::StructureState::UnderConstruction;
    building.working = Frontier::NO_OBJECT;
    static_cast<void>(sim.Objects().Create(building));
    sim.Advance();
    Assert::AreEqual(200000, seat.stockpileCapHundredths);
  }

  TEST_METHOD(IncomeStopsAtTheCapAndARefundOverItIsLost)
  {
    // "Any refund that would exceed the cap is lost - so power cannot be stored in unfinished
    // builds" (GameDesign.md §4).
    Frontier::Sim sim(TwoSeats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(LandscapeWith({{20, 20}})));
    Standing(sim, 0, Row::Extractor, 20, 20);
    Standing(sim, 0, Row::Generator, 22, 20);
    Frontier::Seat& seat = sim.SeatAt(0);
    // The generator that serves the extractor also lifts the cap, so the cap here is 1,500.
    seat.powerHundredths = 149990;
    sim.Advance();
    // The tick earned 25 and only 10 fitted.
    Assert::AreEqual(150000, seat.stockpileCapHundredths);
    Assert::AreEqual(150000, seat.powerHundredths);
    sim.Advance();
    Assert::AreEqual(150000, seat.powerHundredths);

    // A refund at the cap is lost whole, and one that half fits is clipped rather than refused.
    Frontier::Economy::RefundDemolished(seat, 50000);
    Assert::AreEqual(150000, seat.powerHundredths);
    seat.powerHundredths = 140000;
    Frontier::Economy::RefundCanceled(seat, 50000, 0);
    Assert::AreEqual(150000, seat.powerHundredths);
  }

  TEST_METHOD(ACancelRefundsTheUnbuiltShareAndADemolitionHalf)
  {
    Frontier::Sim sim(TwoSeats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(LandscapeWith({})));
    Standing(sim, 0, Row::Generator, 20, 20);
    Standing(sim, 0, Row::Generator, 30, 30);
    sim.Advance(); // Lifts the cap to 2,000 so that nothing below is clipped by it.
    Frontier::Seat& seat = sim.SeatAt(0);

    seat.powerHundredths = 0;
    Frontier::Economy::RefundCanceled(seat, 40000, 0); // Nothing built: the whole cost comes back
    Assert::AreEqual(40000, seat.powerHundredths);
    seat.powerHundredths = 0;
    Frontier::Economy::RefundCanceled(seat, 40000, 2500); // A quarter built: three quarters back
    Assert::AreEqual(30000, seat.powerHundredths);
    seat.powerHundredths = 0;
    Frontier::Economy::RefundCanceled(seat, 40000, 10000); // Complete: nothing to cancel back
    Assert::AreEqual(0, seat.powerHundredths);
    seat.powerHundredths = 0;
    Frontier::Economy::RefundDemolished(seat, 40000);
    Assert::AreEqual(20000, seat.powerHundredths);

    // Drawing takes the cost when the work begins, and refuses rather than going negative.
    seat.powerHundredths = 25000;
    Assert::IsFalse(Frontier::Economy::Draw(seat, 40000));
    Assert::AreEqual(25000, seat.powerHundredths);
    Assert::IsTrue(Frontier::Economy::Draw(seat, 25000));
    Assert::AreEqual(0, seat.powerHundredths);
  }

  TEST_METHOD(AStockpileAboveItsCapIsLeftAloneRatherThanConfiscated)
  {
    // PowerLevel::High starts a commander at 2,500 against a base cap of 1,000, so a cap that
    // clipped the balance would delete three fifths of a lobby setting on the first tick. The cap
    // is on what accumulates: income stops, the balance stands.
    Frontier::Sim sim(TwoSeats(Frontier::PowerLevel::High), Tables());
    Assert::IsTrue(sim.CreateLandscape(LandscapeWith({{20, 20}})));
    Standing(sim, 0, Row::Extractor, 20, 20);
    Standing(sim, 0, Row::Generator, 22, 20);
    Frontier::Seat& seat = sim.SeatAt(0);
    Assert::AreEqual(250000, seat.powerHundredths);
    sim.Advance();
    Assert::AreEqual(150000, seat.stockpileCapHundredths);
    Assert::AreEqual(250000, seat.powerHundredths);
  }

  TEST_METHOD(TheArmyCapsComeFromTheLobbyAndTheCountsAreWhatTheWorldHolds)
  {
    // "A commander may field at most 200 devices and 300 structures at once (a lobby setting: 100,
    // 200 or 300 devices)" (GameDesign.md §4). Stage 2 fills the counts, so no other system has to
    // remember to, and validation refuses at the cap - which is the factory's pause, expressed
    // where S3 can express it and turned into a paused factory by S5.
    Frontier::Sim sim(TwoSeats(Frontier::PowerLevel::Medium, Frontier::DeviceCapLevel::Low), Tables());
    Assert::IsTrue(sim.CreateLandscape(LandscapeWith({})));
    Frontier::Seat& seat = sim.SeatAt(0);
    Assert::AreEqual(100u, seat.deviceCap);
    Assert::AreEqual(300u, seat.structureCap);

    Standing(sim, 0, Row::Factory, 20, 20);
    Frontier::Device device{};
    device.seat = 0;
    device.target = Frontier::NO_OBJECT;
    const Frontier::ObjectId first = sim.Objects().Create(device);
    static_cast<void>(sim.Objects().Create(device));
    sim.Advance();
    Assert::AreEqual(2u, seat.deviceCount);
    Assert::AreEqual(1u, seat.structureCount);

    // A plan counts too: a cap that ignored them could be walked past by placing them.
    Frontier::Structure plan{};
    plan.seat = 0;
    plan.design = static_cast<std::uint32_t>(Row::Factory);
    plan.cellX = 30;
    plan.cellY = 30;
    plan.state = Frontier::StructureState::Plan;
    plan.working = Frontier::NO_OBJECT;
    static_cast<void>(sim.Objects().Create(plan));
    sim.Advance();
    Assert::AreEqual(2u, seat.structureCount);

    // Removing a device is seen on the next tick, because the count is what the world holds.
    Assert::IsTrue(sim.Objects().Remove(first));
    sim.Advance();
    Assert::AreEqual(1u, seat.deviceCount);
  }

  TEST_METHOD(AnExtractorIsPlacedOnADepositAndNowhereElse)
  {
    Frontier::Sim sim(TwoSeats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(LandscapeWith({{20, 20}})));
    Standing(sim, 0, Row::CommandPost, 40, 40); // PlaceStructure needs a standing command post
    sim.Advance();
    Reveal(sim, 0, 20, 20);       // the deposit cell
    Reveal(sim, 0, 21, 20, 3, 3); // and the ground the other two placements want

    const Frontier::OrderContext context{&sim.Objects(), sim.Seats(), &sim.Terrain(), sim.Tick(), &Tables(), &sim.Power().Deposits()};
    const auto place = [](Row _row, std::int32_t _cellX, std::int32_t _cellY)
    {
      Frontier::Order order{};
      order.tick = 1;
      order.seat = 0;
      order.kind = Frontier::OrderKind::PlaceStructure;
      order.operands = {static_cast<std::int32_t>(_row), _cellX, _cellY, 0};
      return order;
    };
    Assert::IsTrue(Frontier::ValidateOrder(place(Row::Extractor, 20, 20), context).Accepted());
    const Frontier::OrderCheck offDeposit = Frontier::ValidateOrder(place(Row::Extractor, 21, 20), context);
    Assert::IsFalse(offDeposit.Accepted());
    Assert::IsTrue(offDeposit.reason == Frontier::RejectReason::InvalidPlacement);
    // Every other role may stand anywhere the landscape allows; the rule is the extractor's alone.
    Assert::IsTrue(Frontier::ValidateOrder(place(Row::Factory, 21, 20), context).Accepted());
  }

  TEST_METHOD(APlacementIsPricedByItsRowRatherThanByAnEmptyStockpile)
  {
    // S2 could only ask whether the stockpile was empty, because nothing priced a row. It is the
    // row's cost now: 400 power for a factory is refused at 399 and accepted at 400.
    Frontier::Sim sim(TwoSeats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(LandscapeWith({})));
    Standing(sim, 0, Row::CommandPost, 40, 40);
    sim.Advance();
    Reveal(sim, 0, 30, 30, 3, 3);
    Frontier::Seat& seat = sim.SeatAt(0);

    Frontier::Order order{};
    order.tick = 1;
    order.seat = 0;
    order.kind = Frontier::OrderKind::PlaceStructure;
    order.operands = {static_cast<std::int32_t>(Row::Factory), 30, 30, 0};
    const Frontier::OrderContext context{&sim.Objects(), sim.Seats(), &sim.Terrain(), sim.Tick(), &Tables(), &sim.Power().Deposits()};

    seat.powerHundredths = 39900;
    const Frontier::OrderCheck poor = Frontier::ValidateOrder(order, context);
    Assert::IsFalse(poor.Accepted());
    Assert::IsTrue(poor.reason == Frontier::RejectReason::CannotAfford);

    seat.powerHundredths = 40000;
    Assert::IsTrue(Frontier::ValidateOrder(order, context).Accepted());
  }

  TEST_METHOD(TheServiceAssignmentSurvivesASnapshotBecauseItIsRebuiltRatherThanCarried)
  {
    // The deposits are the definition's and the assignment is the world's, so neither is in the
    // stream; what the snapshot must prove is that a restored match computes the same thing and
    // goes on earning, rather than losing its deposits and stopping.
    Frontier::Sim sim(TwoSeats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(LandscapeWith({{20, 20}})));
    const Frontier::ObjectId extractor = Standing(sim, 0, Row::Extractor, 20, 20);
    Standing(sim, 0, Row::Generator, 22, 20);
    sim.SeatAt(0).powerHundredths = 0;
    sim.Advance();
    Assert::AreEqual(25, sim.SeatAt(0).powerHundredths);

    Frontier::Sim restored = Reload(sim);
    Assert::AreEqual(sim.Hash(), restored.Hash());
    Assert::AreEqual(std::size_t{1}, restored.Power().Deposits().Count());

    sim.Advance();
    restored.Advance();
    Assert::IsTrue(restored.Power().Served(extractor));
    Assert::AreEqual(sim.SeatAt(0).powerHundredths, restored.SeatAt(0).powerHundredths);
    Assert::AreEqual(sim.Hash(), restored.Hash());
  }
};

} // namespace SimTests
