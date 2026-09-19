#include "pch.h"

#include "Sim.h"
#include "Victory.h"

#include "FixedPoint.h"

#include <cstdint>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// How a match ends (GameDesign.md §2; m1-vertical-slice/S11). The three conditions the lobby
// offers are one stage: annihilation takes a commander out when he holds neither a structure nor a
// builder, the last alliance standing wins whatever the condition, and the survival clock is
// settled on what each side EXTRACTED rather than on what it still has in the bank.
namespace SimTests
{

namespace
{

constexpr std::uint32_t CELL = static_cast<std::uint32_t>(Neuron::SUBUNITS_PER_CELL);

enum class Row : std::uint32_t
{
  CommandPost = 0,
  Extractor = 1,
  Generator = 2
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
    tree.structures.structures = {post, extractor, generator};

    // The least a builder needs to exist, and one design with no builder in it, so that the
    // annihilation rule has both kinds of device to tell apart.
    Frontier::ChassisDesc light{};
    light.id = "LightI";
    light.chassisClass = Frontier::ChassisClass::Light;
    light.hitPoints = 100;
    light.sightSubunits = 20 * Neuron::SUBUNITS_PER_CELL;
    light.costHundredths = 6000;
    light.mounts = 1;
    tree.components.chassis = {light};
    Frontier::DriveDesc wheels{};
    wheels.id = "Wheels";
    wheels.driveClass = Frontier::DriveClass::Wheels;
    wheels.speedFactorHundredths = 100;
    wheels.hitPointFactorHundredths = 100;
    wheels.costHundredths = 3000;
    tree.components.drives = {wheels};
    Frontier::ModuleDesc builder{};
    builder.id = "Builder";
    builder.systemKind = Frontier::SystemKind::Builder;
    builder.costHundredths = 5000;
    builder.systemRangeSubunits = 8 * Neuron::SUBUNITS_PER_CELL;
    builder.buildPowerHundredthsPerTick = 50;
    Frontier::ModuleDesc cannon{};
    cannon.id = "Cannon";
    cannon.systemKind = Frontier::SystemKind::None;
    cannon.costHundredths = 5000;
    tree.components.modules = {builder, cannon};
    return tree;
  }();
  return TREE;
}

/// Design 0 carries the builder module, design 1 carries a weapon and no builder.
constexpr std::uint32_t BUILDER_DESIGN = 0;
constexpr std::uint32_t FIGHTER_DESIGN = 1;

Frontier::MatchSettings Seats(std::uint8_t _count, std::vector<std::uint8_t> _alliances,
                              Frontier::VictoryCondition _victory = Frontier::VictoryCondition::Annihilation,
                              std::uint32_t _survivalTicks = 0)
{
  Frontier::MatchSettings settings{};
  settings.seed = 5;
  settings.sizeClass = Frontier::SizeClass::Small;
  settings.seatCount = _count;
  settings.baseLevel = Frontier::BaseLevel::Nothing;
  settings.powerLevel = Frontier::PowerLevel::Medium;
  settings.deviceCapLevel = Frontier::DeviceCapLevel::Medium;
  settings.victory = _victory;
  settings.survivalTicks = _survivalTicks;
  for (std::uint8_t index = 0; index < _count; ++index)
  {
    settings.seats[index] = {Frontier::SeatKind::Human, _alliances[index]};
  }
  return settings;
}

Frontier::LandscapeDefinition LandscapeWith(std::vector<Frontier::CellPosition> _deposits = {})
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

Frontier::ObjectId Standing(Frontier::Sim& _sim, std::uint8_t _seat, Row _row, std::uint32_t _cellX, std::uint32_t _cellY)
{
  Frontier::Structure structure{};
  structure.seat = _seat;
  structure.design = static_cast<std::uint32_t>(_row);
  structure.cellX = _cellX;
  structure.cellY = _cellY;
  structure.state = Frontier::StructurePhase::Standing;
  structure.hitPoints = 100;
  structure.buildEffortHundredths = 10000;
  structure.working = Frontier::NO_OBJECT;
  return _sim.Objects().Create(structure);
}

/// Gives the seat both designs the first time it is asked, so that a test names a design rather
/// than building one.
void Designs(Frontier::Sim& _sim, std::uint8_t _seat)
{
  Frontier::Seat& seat = _sim.SeatAt(_seat);
  if (!seat.designs.empty())
  {
    return;
  }
  Frontier::DeviceDesign withBuilder{};
  withBuilder.chassis = 0;
  withBuilder.drive = 0;
  withBuilder.modules[0] = 0; // the builder module
  withBuilder.moduleCount = 1;
  Frontier::DeviceDesign withWeapon{};
  withWeapon.chassis = 0;
  withWeapon.drive = 0;
  withWeapon.modules[0] = 1; // the cannon
  withWeapon.moduleCount = 1;
  seat.designs = {withBuilder, withWeapon};
}

Frontier::ObjectId DeviceOf(Frontier::Sim& _sim, std::uint8_t _seat, std::uint32_t _design, std::uint32_t _cellX, std::uint32_t _cellY)
{
  Designs(_sim, _seat);
  Frontier::Device device{};
  device.seat = _seat;
  device.design = _design;
  device.x = static_cast<std::int32_t>(_cellX * CELL + CELL / 2);
  device.z = static_cast<std::int32_t>(_cellY * CELL + CELL / 2);
  device.hitPoints = 100;
  device.target = Frontier::NO_OBJECT;
  return _sim.Objects().Create(device);
}

} // namespace

TEST_CLASS(VictoryTests)
{
public:
  TEST_METHOD(ACommanderWithABuilderLeftIsNotAnnihilated)
  {
    // GameDesign.md §2: "every enemy structure and every enemy builder is destroyed". A commander
    // reduced to one truck is still in the match, and a commander reduced to tanks is not.
    Frontier::Sim sim(Seats(2, {0, 1}), Tables());
    Assert::IsTrue(sim.CreateLandscape(LandscapeWith()));
    Standing(sim, 0, Row::CommandPost, 16, 16);
    const Frontier::ObjectId post = Standing(sim, 1, Row::CommandPost, 100, 100);
    const Frontier::ObjectId truck = DeviceOf(sim, 1, BUILDER_DESIGN, 104, 100);
    DeviceOf(sim, 1, FIGHTER_DESIGN, 105, 100);
    sim.Advance();
    Assert::IsTrue(sim.Seats()[1].victory == Frontier::VictoryState::Playing);
    Assert::IsTrue(sim.Seats()[1].everHeldBase);

    // The base goes; the truck is what keeps him in.
    Assert::IsTrue(sim.Objects().Remove(post));
    sim.Advance();
    Assert::IsFalse(sim.Finished(), L"a builder is a base");
    Assert::IsTrue(sim.Seats()[1].victory == Frontier::VictoryState::Playing);

    // The truck goes; the tank is not a builder and does not keep him in.
    Assert::IsTrue(sim.Objects().Remove(truck));
    Assert::IsTrue(Frontier::Annihilated(sim, 1));
    sim.Advance();
    Assert::IsTrue(sim.Seats()[1].victory == Frontier::VictoryState::Eliminated);
    Assert::IsFalse(sim.Seats()[1].surrendered, L"annihilated, not conceded");
    Assert::IsTrue(sim.Finished());
    Assert::AreEqual(0, static_cast<int>(sim.WinningAlliance()));
    Assert::IsTrue(sim.Seats()[0].victory == Frontier::VictoryState::Won);
  }

  TEST_METHOD(AStructureUnderConstructionCountsAndAPlanDoesNot)
  {
    Frontier::Sim sim(Seats(2, {0, 1}), Tables());
    Assert::IsTrue(sim.CreateLandscape(LandscapeWith()));
    Standing(sim, 0, Row::CommandPost, 16, 16);
    const Frontier::ObjectId building = Standing(sim, 1, Row::CommandPost, 100, 100);
    Frontier::Structure* raising = sim.Objects().FindStructure(building);
    raising->state = Frontier::StructurePhase::UnderConstruction;
    sim.Advance();
    Assert::IsTrue(sim.Seats()[1].victory == Frontier::VictoryState::Playing, L"a half-built base is a base");

    // A plan occupies nothing and nothing has been built: a commander left with one has nothing.
    sim.Objects().FindStructure(building)->state = Frontier::StructurePhase::Plan;
    Assert::IsTrue(Frontier::Annihilated(sim, 1));
    sim.Advance();
    Assert::IsTrue(sim.Seats()[1].victory == Frontier::VictoryState::Eliminated);
  }

  TEST_METHOD(TheLastAllianceStandingWinsAndAlliesShareIt)
  {
    // Two commanders allied against one. Taking the lone commander out ends it for both allies at
    // once, which is what "allied commanders share vision and victory" means (GameDesign.md §2).
    Frontier::Sim sim(Seats(3, {0, 0, 1}), Tables());
    Assert::IsTrue(sim.CreateLandscape(LandscapeWith()));
    Standing(sim, 0, Row::CommandPost, 16, 16);
    Standing(sim, 1, Row::CommandPost, 40, 40);
    const Frontier::ObjectId lone = Standing(sim, 2, Row::CommandPost, 100, 100);
    sim.Advance();
    Assert::IsFalse(sim.Finished(), L"two alliances are standing");

    Assert::IsTrue(sim.Objects().Remove(lone));
    sim.Advance();
    Assert::IsTrue(sim.Finished());
    Assert::AreEqual(0, static_cast<int>(sim.WinningAlliance()));
    Assert::IsTrue(sim.Seats()[0].victory == Frontier::VictoryState::Won);
    Assert::IsTrue(sim.Seats()[1].victory == Frontier::VictoryState::Won, L"an ally who fired no shot still won");
    Assert::IsTrue(sim.Seats()[2].victory == Frontier::VictoryState::Eliminated);
  }

  TEST_METHOD(ASurrenderedAllyStaysEliminatedWhenHisSideWins)
  {
    // Won is for commanders who were there at the end. A seat that conceded is out from that tick
    // and the match's outcome is not his, however his alliance finishes.
    Frontier::Sim sim(Seats(3, {0, 0, 1}), Tables());
    Assert::IsTrue(sim.CreateLandscape(LandscapeWith()));
    Standing(sim, 0, Row::CommandPost, 16, 16);
    Standing(sim, 1, Row::CommandPost, 40, 40);
    const Frontier::ObjectId lone = Standing(sim, 2, Row::CommandPost, 100, 100);
    sim.Advance();

    Frontier::Order surrender{};
    surrender.tick = sim.Tick() + 1;
    surrender.seat = 1;
    surrender.kind = Frontier::OrderKind::Surrender;
    sim.Submit(surrender);
    sim.Advance();
    Assert::IsTrue(sim.Seats()[1].victory == Frontier::VictoryState::Eliminated);
    Assert::IsTrue(sim.Seats()[1].surrendered);
    Assert::IsFalse(sim.Finished(), L"his ally is still playing");

    Assert::IsTrue(sim.Objects().Remove(lone));
    sim.Advance();
    Assert::IsTrue(sim.Finished());
    Assert::IsTrue(sim.Seats()[0].victory == Frontier::VictoryState::Won);
    Assert::IsTrue(sim.Seats()[1].victory == Frontier::VictoryState::Eliminated, L"he was not there for it");
  }

  TEST_METHOD(TheSurvivalClockGoesToTheSideThatExtractedTheMost)
  {
    // Not the side holding the most: what a commander spent, he still dug up. Both sides here end
    // with the same stockpile and the match is decided on the difference in what they extracted.
    Frontier::Sim sim(Seats(2, {0, 1}, Frontier::VictoryCondition::Survival, 3), Tables());
    Assert::IsTrue(sim.CreateLandscape(LandscapeWith()));
    Standing(sim, 0, Row::CommandPost, 16, 16);
    Standing(sim, 1, Row::CommandPost, 100, 100);
    sim.SeatAt(0).extractedHundredths = 10000;
    sim.SeatAt(1).extractedHundredths = 25000;
    sim.SeatAt(0).powerHundredths = 90000;
    sim.SeatAt(1).powerHundredths = 90000;
    for (std::uint32_t tick = 0; tick < 3; ++tick)
    {
      sim.Advance();
    }
    Assert::IsTrue(sim.Finished());
    Assert::AreEqual(1, static_cast<int>(sim.WinningAlliance()), L"the poorer commander dug more");
    Assert::IsTrue(sim.Seats()[0].victory == Frontier::VictoryState::Lost);
    Assert::IsTrue(sim.Seats()[1].victory == Frontier::VictoryState::Won);
    Assert::AreEqual(sim.Seats()[0].powerHundredths, sim.Seats()[1].powerHundredths, L"the banks were level");
  }

  TEST_METHOD(TheSurvivalClockIsADrawWhenNeitherSideDugMore)
  {
    Frontier::Sim sim(Seats(2, {0, 1}, Frontier::VictoryCondition::Survival, 4), Tables());
    Assert::IsTrue(sim.CreateLandscape(LandscapeWith()));
    Standing(sim, 0, Row::CommandPost, 16, 16);
    Standing(sim, 1, Row::CommandPost, 100, 100);
    sim.SeatAt(0).extractedHundredths = 7777;
    sim.SeatAt(1).extractedHundredths = 7777;
    for (std::uint32_t tick = 0; tick < 4; ++tick)
    {
      sim.Advance();
    }
    Assert::IsTrue(sim.Finished());
    Assert::AreEqual(static_cast<int>(Frontier::NO_ALLIANCE), static_cast<int>(sim.WinningAlliance()));
    Assert::IsTrue(sim.Seats()[0].victory == Frontier::VictoryState::Lost, L"nobody won, so nobody is Won");
    Assert::IsTrue(sim.Seats()[1].victory == Frontier::VictoryState::Lost);
  }

  TEST_METHOD(PowerLostToAFullStockpileWasStillExtracted)
  {
    // Stage 2 banks what it can and drops the rest at the cap. The Survival total is taken before
    // that, because the alternative punishes the commander whose economy outran his spending.
    Frontier::Sim sim(Seats(2, {0, 1}), Tables());
    Assert::IsTrue(sim.CreateLandscape(LandscapeWith({{20, 20}})));
    Standing(sim, 0, Row::CommandPost, 16, 16);
    Standing(sim, 0, Row::Extractor, 20, 20);
    Standing(sim, 0, Row::Generator, 22, 20);
    Standing(sim, 1, Row::CommandPost, 100, 100);
    sim.SeatAt(0).powerHundredths = 1000000; // far over any cap the generators give
    const std::int32_t banked = sim.Seats()[0].powerHundredths;
    sim.Advance();
    Assert::AreEqual(static_cast<std::int64_t>(25), sim.Seats()[0].extractedHundredths, L"one tick of one served extractor");
    Assert::IsTrue(sim.Seats()[0].powerHundredths <= banked, L"the stockpile was already full");
    sim.Advance();
    Assert::AreEqual(static_cast<std::int64_t>(50), sim.Seats()[0].extractedHundredths);
    Assert::AreEqual(static_cast<std::int64_t>(0), sim.Seats()[1].extractedHundredths, L"a command post does not extract");
  }

  TEST_METHOD(AMatchNobodyHasBeenPlacedInDoesNotEndOnItsFirstTick)
  {
    // The annihilation rule cannot fire before a commander has held anything: a lobby hands the
    // Sim its seats and the base level is placed over the ticks that follow, and a rule that read
    // "holds nothing" alone would end the match in between.
    Frontier::Sim sim(Seats(2, {0, 1}), Tables());
    Assert::IsTrue(sim.CreateLandscape(LandscapeWith()));
    for (std::uint32_t tick = 0; tick < 10; ++tick)
    {
      sim.Advance();
      Assert::IsFalse(sim.Finished());
    }
    Assert::IsFalse(sim.Seats()[0].everHeldBase);

    Standing(sim, 0, Row::CommandPost, 16, 16);
    Standing(sim, 1, Row::CommandPost, 100, 100);
    sim.Advance();
    Assert::IsTrue(sim.Seats()[0].everHeldBase, L"and now he has");
    Assert::IsTrue(sim.Seats()[1].everHeldBase);
    Assert::IsFalse(sim.Finished());
  }

  TEST_METHOD(AnEliminatedSeatIsOutOfTheTickThatEliminatedIt)
  {
    // A seat that goes out at stage 12 must not be counted standing by the same stage, and its
    // orders must be refused from the next tick on rather than judged.
    Frontier::Sim sim(Seats(3, {0, 1, 2}), Tables());
    Assert::IsTrue(sim.CreateLandscape(LandscapeWith()));
    Standing(sim, 0, Row::CommandPost, 16, 16);
    const Frontier::ObjectId second = Standing(sim, 1, Row::CommandPost, 60, 60);
    Standing(sim, 2, Row::CommandPost, 100, 100);
    sim.Advance();
    Assert::IsTrue(sim.Objects().Remove(second));
    sim.Advance();
    Assert::IsTrue(sim.Seats()[1].victory == Frontier::VictoryState::Eliminated);
    Assert::IsFalse(sim.Finished(), L"two alliances are still playing");

    Frontier::Order chat{};
    chat.tick = sim.Tick() + 1;
    chat.seat = 1;
    chat.kind = Frontier::OrderKind::Chat;
    sim.Submit(chat);
    const std::uint32_t dropped = sim.DroppedOrders();
    sim.Advance();
    Assert::AreEqual(dropped + 1, sim.DroppedOrders(), L"an eliminated commander's orders are dropped");
  }
};

} // namespace SimTests
