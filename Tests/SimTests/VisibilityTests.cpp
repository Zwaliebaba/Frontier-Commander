#include "pch.h"

#include "Sim.h"
#include "Snapshot.h"
#include "Visibility.h"

#include "FixedPoint.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// Fog of war (TechnicalDesign.md §4.6): computed on the host, and the replication filter, so what
// it says is exactly what a commander is allowed to be told. The properties under test are the
// ones that make it safe to send: a cell is visible only while something holds it, explored is
// history and never comes back off, a ridge blocks, height reaches over, and the refresh is
// bounded so the tick's cost does not depend on how many units are on the field.
namespace SimTests
{

namespace
{

constexpr std::int32_t CELL = Neuron::SUBUNITS_PER_CELL;

enum class Row : std::uint32_t
{
  CommandPost = 0,
  Tower = 1
};

/// Two structures with sight and one chassis with sight: the least a visibility test needs.
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
    post.sightSubunits = 12 * Neuron::SUBUNITS_PER_CELL; // GameDesign.md §8: structures see 12 cells
    Frontier::StructureDesc tower{};
    tower.id = "Tower";
    tower.role = Frontier::StructureRole::Tower;
    tower.footprintCellsX = 1;
    tower.footprintCellsY = 1;
    tower.costHundredths = 20000;
    tower.sightSubunits = 20 * Neuron::SUBUNITS_PER_CELL;
    tree.structures.structures = {post, tower};

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
    Frontier::ModuleDesc gun{};
    gun.id = "MachineGun";
    gun.systemKind = Frontier::SystemKind::None;
    gun.costHundredths = 4000;
    tree.components.modules = {gun};
    return tree;
  }();
  return TREE;
}

Frontier::MatchSettings Seats(std::uint8_t _alliance0 = 0, std::uint8_t _alliance1 = 1)
{
  Frontier::MatchSettings settings{};
  settings.seed = 3;
  settings.sizeClass = Frontier::SizeClass::Small;
  settings.seatCount = 2;
  settings.baseLevel = Frontier::BaseLevel::Nothing;
  settings.powerLevel = Frontier::PowerLevel::Medium;
  settings.deviceCapLevel = Frontier::DeviceCapLevel::Medium;
  settings.victory = Frontier::VictoryCondition::Annihilation;
  settings.seats[0] = {Frontier::SeatKind::Human, _alliance0};
  settings.seats[1] = {Frontier::SeatKind::Ai, _alliance1};
  return settings;
}

/// A landscape of one flat tile. The generator's own output, so the heights are whatever it makes
/// of the seed; the tests that care about a ridge raise one themselves with a height delta.
Frontier::LandscapeDefinition Flat()
{
  Frontier::LandscapeDefinition definition{};
  definition.version = Frontier::LANDSCAPE_DEFINITION_VERSION;
  definition.sizeClass = Frontier::SizeClass::Small;
  definition.cellsPerSide = Frontier::SIZE_CLASS_CELLS[0];
  definition.seed = 1;
  definition.palette = "Default";
  definition.tiles = {{0, 0, 512, 170, 0, 0, 0, 70, 1, 0, ""}};
  definition.starts = {{16, 16}, {100, 100}};
  return definition;
}

/// Raises a rectangle of samples to _height, which is how these tests build a ridge or a hill.
[[nodiscard]] bool Raise(Frontier::Sim& _sim, std::uint32_t _sampleX0, std::uint32_t _sampleY0, std::uint32_t _sampleX1,
                         std::uint32_t _sampleY1, std::int16_t _height)
{
  Frontier::HeightDelta delta{};
  delta.x = _sampleX0;
  delta.y = _sampleY0;
  delta.width = _sampleX1 - _sampleX0 + 1;
  delta.height = _sampleY1 - _sampleY0 + 1;
  delta.heights.assign(static_cast<std::size_t>(delta.width) * delta.height, _height);
  return _sim.FlattenTerrain(delta);
}

Frontier::ObjectId Watcher(Frontier::Sim& _sim, std::uint8_t _seat, Row _row, std::uint32_t _cellX, std::uint32_t _cellY)
{
  Frontier::Structure structure{};
  structure.seat = _seat;
  structure.design = static_cast<std::uint32_t>(_row);
  structure.cellX = _cellX;
  structure.cellY = _cellY;
  structure.state = Frontier::StructureState::Standing;
  structure.hitPoints = 100;
  structure.buildProgressHundredths = 10000;
  structure.working = Frontier::NO_OBJECT;
  return _sim.Objects().Create(structure);
}

Frontier::ObjectId Scout(Frontier::Sim& _sim, std::uint8_t _seat, std::uint32_t _cellX, std::uint32_t _cellY)
{
  Frontier::DeviceDesign design{};
  design.chassis = 0;
  design.drive = 0;
  design.modules = {};
  design.modules[0] = 0;
  design.moduleCount = 1;
  Frontier::Seat& seat = _sim.SeatAt(_seat);
  if (seat.designs.empty())
  {
    seat.designs.push_back(design);
  }
  Frontier::Device device{};
  device.seat = _seat;
  device.design = 0;
  device.x = static_cast<std::int32_t>(_cellX) * CELL + CELL / 2;
  device.z = static_cast<std::int32_t>(_cellY) * CELL + CELL / 2;
  device.target = Frontier::NO_OBJECT;
  return _sim.Objects().Create(device);
}

} // namespace

TEST_CLASS(VisibilityTests)
{
public:
  TEST_METHOD(ACellIsVisibleWhileAViewerHoldsItAndExploredForEverAfter)
  {
    Frontier::Sim sim(Seats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(Flat()));
    const Frontier::ObjectId scout = Scout(sim, 0, 40, 40);
    sim.Advance();

    const Frontier::FogGrid& fog = sim.Seats()[0].fog;
    Assert::IsTrue(fog.Visible(40, 40), L"the cell it stands in");
    Assert::IsTrue(fog.Visible(45, 40), L"five cells away, inside a twenty-cell radius");
    Assert::IsFalse(fog.Explored(80, 40), L"forty cells away, outside it");

    // The viewer goes away: what it held falls back to explored, never to unexplored.
    Assert::IsTrue(sim.Objects().Remove(scout));
    sim.Advance();
    Assert::IsFalse(fog.Visible(40, 40));
    Assert::IsTrue(fog.Explored(40, 40), L"explored is history and does not come off");
    Assert::AreEqual(std::uint16_t{0}, fog.ViewersAt(40, 40));
  }

  TEST_METHOD(AMovedViewerUnseesWhatItLeftBeforeItSeesWhatItReached)
  {
    // The count is what makes a cell visible, so a move that counted the new disc without
    // un-counting the old one would leave a trail of lit cells behind every unit in the game.
    Frontier::Sim sim(Seats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(Flat()));
    const Frontier::ObjectId scout = Scout(sim, 0, 20, 20);
    sim.Advance();
    const Frontier::FogGrid& fog = sim.Seats()[0].fog;
    Assert::IsTrue(fog.Visible(20, 20));

    sim.Objects().FindDevice(scout)->x = 60 * CELL + CELL / 2;
    sim.Objects().FindDevice(scout)->z = 60 * CELL + CELL / 2;
    sim.Advance();
    Assert::IsFalse(fog.Visible(20, 20), L"the cell it left");
    Assert::IsTrue(fog.Explored(20, 20));
    Assert::IsTrue(fog.Visible(60, 60), L"the cell it reached");

    // Two viewers on one cell, one of which leaves: the cell stays visible, which is the whole
    // reason the grid counts rather than flags.
    const Frontier::ObjectId first = Scout(sim, 0, 80, 80);
    const Frontier::ObjectId second = Scout(sim, 0, 80, 80);
    sim.Advance();
    Assert::AreEqual(std::uint16_t{2}, fog.ViewersAt(80, 80));
    Assert::IsTrue(sim.Objects().Remove(first));
    sim.Advance();
    Assert::IsTrue(fog.Visible(80, 80), L"the second viewer still holds it");
    Assert::IsTrue(sim.Objects().Remove(second));
    sim.Advance();
    Assert::IsFalse(fog.Visible(80, 80));
  }

  TEST_METHOD(ARidgeBetweenTheViewerAndTheTargetBlocks)
  {
    Frontier::Sim sim(Seats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(Flat()));
    // A wall four cells wide across the whole line, tall enough that no line of sight clears it.
    Assert::IsTrue(Raise(sim, 0, 40 * Frontier::SAMPLES_PER_CELL_EDGE, 512, 41 * Frontier::SAMPLES_PER_CELL_EDGE, 400));
    Scout(sim, 0, 35, 35);
    sim.Advance();

    const Frontier::FogGrid& fog = sim.Seats()[0].fog;
    Assert::IsTrue(fog.Visible(35, 38), L"this side of the ridge");
    Assert::IsFalse(fog.Visible(35, 45), L"beyond it, and inside the radius");
    std::uint64_t reads = 0;
    Assert::IsFalse(Frontier::Visibility::LineOfSight(sim.Terrain(), 35, 35, 35, 45, reads));
    Assert::IsTrue(Frontier::Visibility::LineOfSight(sim.Terrain(), 35, 35, 35, 38, reads));
  }

  TEST_METHOD(AHillExtendsTheRadiusByACellForEveryThirtyTwoUnitsOfHeight)
  {
    // GameDesign.md §8: height adds a cell of sight for every 32 world units a viewer stands
    // above its target. A tower on flat ground sees 20 cells; the same tower on a hill sees past
    // that, and the ground it sees is the ground below it.
    Frontier::Sim flat(Seats(), Tables());
    Assert::IsTrue(flat.CreateLandscape(Flat()));
    Watcher(flat, 0, Row::Tower, 60, 60);
    flat.Advance();
    Assert::IsFalse(flat.Seats()[0].fog.Visible(60, 85), L"25 cells from a 20-cell tower");

    Frontier::Sim hill(Seats(), Tables());
    Assert::IsTrue(hill.CreateLandscape(Flat()));
    // A pillar under the tower, 320 units up: ten cells of bonus, which reaches 25 comfortably.
    // Only the samples STRICTLY INSIDE cell 60 are raised. A cell's height is the tallest of its
    // five-by-five samples and neighbouring cells share their edge samples, so raising the whole
    // cell would raise its neighbours too - and a plateau's own edge blocks the view out of its
    // middle, which is correct geometry and not what this test is about.
    Assert::IsTrue(Raise(hill, 60 * Frontier::SAMPLES_PER_CELL_EDGE + 1, 60 * Frontier::SAMPLES_PER_CELL_EDGE + 1,
                         60 * Frontier::SAMPLES_PER_CELL_EDGE + 3, 60 * Frontier::SAMPLES_PER_CELL_EDGE + 3, 320));
    Watcher(hill, 0, Row::Tower, 60, 60);
    hill.Advance();
    Assert::IsTrue(hill.Seats()[0].fog.Visible(60, 85), L"the same 25 cells, from 320 units up");
  }

  TEST_METHOD(TheRefreshIsBoundedByTheBudgetInViewersAndNotByTheFieldSize)
  {
    // The point of a budget in viewers is that the tick's cost is a property of the design and not
    // of how many units a commander has built (TechnicalDesign.md §4.6).
    Frontier::Sim sim(Seats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(Flat()));
    const std::uint32_t many = Frontier::REFRESH_BUDGET_VIEWERS + 57;
    for (std::uint32_t index = 0; index < many; ++index)
    {
      Scout(sim, 0, 10 + (index % 100), 10 + (index / 100));
    }
    sim.Advance();
    Assert::AreEqual(Frontier::REFRESH_BUDGET_VIEWERS, sim.Sight().LastRefreshedViewers(), L"never more than the budget");
    Assert::AreEqual(std::size_t{Frontier::REFRESH_BUDGET_VIEWERS}, sim.Sight().Stamps().size());

    // The rest arrive on the next tick, because the ones already stamped are no longer "moved" and
    // sort behind those that have never been counted.
    sim.Advance();
    Assert::AreEqual(std::size_t{many}, sim.Sight().Stamps().size(), L"every viewer counted within two ticks");
    Assert::IsTrue(sim.Sight().LastHeightReads() > 0, L"the reads are counted for the tick-cost measurement");
  }

  TEST_METHOD(AlliedSeatsShareVisionAndEnemiesDoNot)
  {
    Frontier::Sim allied(Seats(0, 0), Tables());
    Assert::IsTrue(allied.CreateLandscape(Flat()));
    Scout(allied, 0, 50, 50);
    allied.Advance();
    Assert::IsTrue(allied.Seats()[0].fog.Visible(50, 50));
    Assert::IsTrue(allied.Seats()[1].fog.Visible(50, 50), L"an ally sees what its partner sees");

    Frontier::Sim enemies(Seats(0, 1), Tables());
    Assert::IsTrue(enemies.CreateLandscape(Flat()));
    Scout(enemies, 0, 50, 50);
    enemies.Advance();
    Assert::IsTrue(enemies.Seats()[0].fog.Visible(50, 50));
    Assert::IsFalse(enemies.Seats()[1].fog.Explored(50, 50), L"an enemy learns nothing");
  }

  TEST_METHOD(AGhostIsRecordedWhileSeenAndKeptAfterTheViewerLeaves)
  {
    Frontier::Sim sim(Seats(0, 1), Tables());
    Assert::IsTrue(sim.CreateLandscape(Flat()));
    const Frontier::ObjectId theirs = Watcher(sim, 1, Row::CommandPost, 50, 50);
    const Frontier::ObjectId scout = Scout(sim, 0, 50, 52);
    sim.Advance();

    const Frontier::Ghost* ghost = sim.Seats()[0].ghosts.Find(theirs);
    Assert::IsNotNull(ghost);
    Assert::AreEqual(std::uint32_t{50}, ghost->cellX);
    Assert::AreEqual(std::uint8_t{1}, ghost->seat, L"who owned it when it was seen");
    const std::uint32_t seenAt = ghost->seenTick;

    // The scout dies. The record stays, which is what an explored map shows and what an attack on
    // an unseen target is redirected to (GameDesign.md §8).
    Assert::IsTrue(sim.Objects().Remove(scout));
    sim.Advance();
    sim.Advance();
    Assert::IsFalse(sim.Seats()[0].fog.Visible(50, 50));
    const Frontier::Ghost* remembered = sim.Seats()[0].ghosts.Find(theirs);
    Assert::IsNotNull(remembered);
    Assert::AreEqual(seenAt, remembered->seenTick, L"the record is of when it was last seen, not of now");
  }

  TEST_METHOD(AGhostOfSomethingGoneIsClearedWhenTheCommanderLooksAgain)
  {
    Frontier::Sim sim(Seats(0, 1), Tables());
    Assert::IsTrue(sim.CreateLandscape(Flat()));
    const Frontier::ObjectId theirs = Watcher(sim, 1, Row::CommandPost, 50, 50);
    Scout(sim, 0, 50, 52);
    sim.Advance();
    Assert::IsNotNull(sim.Seats()[0].ghosts.Find(theirs));

    // Demolished while the commander is still watching: a base that is gone stops being drawn.
    Assert::IsTrue(sim.Objects().Remove(theirs));
    sim.Advance();
    Assert::IsNull(sim.Seats()[0].ghosts.Find(theirs));
  }

  TEST_METHOD(TwoSimsFedTheSameThingAgreeCellForCellAndAcrossASnapshot)
  {
    // The fog is the replication filter, so a divergence here is a divergence in what two hosts
    // would tell their clients. The stamps are state and travel with the grids.
    Frontier::Sim left(Seats(), Tables());
    Frontier::Sim right(Seats(), Tables());
    Assert::IsTrue(left.CreateLandscape(Flat()));
    Assert::IsTrue(right.CreateLandscape(Flat()));
    for (std::uint32_t index = 0; index < 5; ++index)
    {
      Scout(left, 0, 30 + index * 7, 30);
      Scout(right, 0, 30 + index * 7, 30);
      Watcher(left, 1, Row::Tower, 70, 30 + index);
      Watcher(right, 1, Row::Tower, 70, 30 + index);
    }
    for (std::uint32_t tick = 0; tick < 4; ++tick)
    {
      left.Advance();
      right.Advance();
    }
    Assert::AreEqual(left.Hash(), right.Hash());

    std::optional<Frontier::Sim> read = Frontier::Snapshot::Read(Frontier::Snapshot::Write(left), Tables());
    if (!read.has_value())
    {
      Assert::Fail(L"the snapshot did not read back"); // noreturn, which is what the move below relies on
    }
    Frontier::Sim reloaded = *read;
    Assert::AreEqual(left.Hash(), reloaded.Hash());
    Assert::AreEqual(left.Sight().Stamps().size(), reloaded.Sight().Stamps().size(), L"the stamps are on the wire");
    left.Advance();
    reloaded.Advance();
    Assert::AreEqual(left.Hash(), reloaded.Hash(), L"and the next refresh agrees, which is what they are for");
  }

  TEST_METHOD(TheRefreshsCostIsMeasuredForTheTickAdr)
  {
    // m1-vertical-slice/G3 adds a measurement section to the tick ADR, and the visibility
    // refresh's share of a tick is the first number it asks for. Logged rather than asserted on a
    // figure, because a figure measured on this machine is not one measured on the owner's; what
    // IS asserted is the shape - the reads are bounded by the budget and not by the field.
    Frontier::Sim sim(Seats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(Flat()));
    for (std::uint32_t index = 0; index < Frontier::REFRESH_BUDGET_VIEWERS; ++index)
    {
      Scout(sim, 0, 20 + (index % 80), 20 + (index / 80));
    }
    const auto start = std::chrono::steady_clock::now();
    sim.Advance();
    const auto end = std::chrono::steady_clock::now();
    const std::int64_t microseconds = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    const std::uint64_t reads = sim.Sight().LastHeightReads();
    const std::wstring message = L"measured: a full visibility refresh of " + std::to_wstring(Frontier::REFRESH_BUDGET_VIEWERS) +
                                 L" viewers at a 20-cell radius took " + std::to_wstring(microseconds) + L" us and " +
                                 std::to_wstring(reads) + L" cell-height reads (" +
                                 std::to_wstring(reads / Frontier::REFRESH_BUDGET_VIEWERS) + L" a viewer)";
    Logger::WriteMessage(message.c_str());
    Assert::AreEqual(Frontier::REFRESH_BUDGET_VIEWERS, sim.Sight().LastRefreshedViewers());

    // Twice the viewers is the same work, because the budget is what bounds it. This is the
    // property the design asks for and the one a later optimisation must not lose.
    Frontier::Sim crowded(Seats(), Tables());
    Assert::IsTrue(crowded.CreateLandscape(Flat()));
    for (std::uint32_t index = 0; index < Frontier::REFRESH_BUDGET_VIEWERS * 2; ++index)
    {
      Scout(crowded, 0, 20 + (index % 80), 20 + (index / 80));
    }
    crowded.Advance();
    Assert::AreEqual(Frontier::REFRESH_BUDGET_VIEWERS, crowded.Sight().LastRefreshedViewers(), L"twice the viewers, the same budget");
  }

  TEST_METHOD(AFlattenDropsEveryStampBecauseADiscWasWorkedOutAgainstTheOldHeights)
  {
    // Un-counting a disc against heights that have changed would take viewers off cells that were
    // never counted, so the whole fog is dropped and the budget refills it.
    Frontier::Sim sim(Seats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(Flat()));
    Scout(sim, 0, 40, 40);
    sim.Advance();
    Assert::AreEqual(std::size_t{1}, sim.Sight().Stamps().size());
    Assert::IsTrue(sim.Seats()[0].fog.Explored(40, 40));

    Assert::IsTrue(Raise(sim, 100 * Frontier::SAMPLES_PER_CELL_EDGE, 100 * Frontier::SAMPLES_PER_CELL_EDGE,
                         102 * Frontier::SAMPLES_PER_CELL_EDGE, 102 * Frontier::SAMPLES_PER_CELL_EDGE, 200));
    Assert::AreEqual(std::size_t{0}, sim.Sight().Stamps().size(), L"the stamps go with the heights");
    Assert::IsFalse(sim.Seats()[0].fog.Explored(40, 40), L"and so does the history, which is the cost of the reset");
    sim.Advance();
    Assert::IsTrue(sim.Seats()[0].fog.Visible(40, 40), L"the next tick puts it back");
  }
};

} // namespace SimTests
