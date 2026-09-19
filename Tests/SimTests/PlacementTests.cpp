#include "pch.h"

#include "Placement.h"
#include "Sim.h"

#include "FixedPoint.h"

#include <cstdint>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// Where a structure may stand (GameDesign.md §5; m1-vertical-slice/S4). Every rule the design
// states is one test, and the two that had a choice in them - which reading of "the slope across
// the footprint", and whether a plan blocks - are pinned by a case that would pass under the other
// reading, so that changing the rule fails here rather than in a capture nobody reads.
namespace SimTests
{

namespace
{

constexpr std::uint32_t SAMPLES = Frontier::SAMPLES_PER_CELL_EDGE;

enum class Row : std::uint32_t
{
  CommandPost = 0,
  Extractor = 1,
  Factory = 2
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
    post.hitPoints = 1500;
    post.costHundredths = 50000;
    post.buildTimeTicks = 1200;
    Frontier::StructureDesc extractor{};
    extractor.id = "Extractor";
    extractor.role = Frontier::StructureRole::Extractor;
    extractor.footprintCellsX = 1;
    extractor.footprintCellsY = 1;
    extractor.hitPoints = 200;
    extractor.costHundredths = 5000;
    extractor.buildTimeTicks = 300;
    Frontier::StructureDesc factory{};
    factory.id = "Factory";
    factory.role = Frontier::StructureRole::Factory;
    factory.footprintCellsX = 3;
    factory.footprintCellsY = 3;
    factory.hitPoints = 800;
    factory.costHundredths = 40000;
    factory.buildTimeTicks = 1200;
    tree.structures.structures = {post, extractor, factory};
    return tree;
  }();
  return TREE;
}

Frontier::MatchSettings TwoSeats()
{
  Frontier::MatchSettings settings{};
  settings.seed = 7;
  settings.sizeClass = Frontier::SizeClass::Small;
  settings.seatCount = 2;
  settings.baseLevel = Frontier::BaseLevel::Nothing;
  settings.powerLevel = Frontier::PowerLevel::Medium;
  settings.deviceCapLevel = Frontier::DeviceCapLevel::Medium;
  settings.victory = Frontier::VictoryCondition::Annihilation;
  settings.seats[0] = {Frontier::SeatKind::Human, 0};
  settings.seats[1] = {Frontier::SeatKind::Ai, 1};
  return settings;
}

/// The built-in Small landscape of the other suites: real ground, gentle, dry where these tests
/// build. What a rule needs to bite is sculpted with a height delta rather than hoped for.
Frontier::LandscapeDefinition Ground(std::vector<Frontier::CellPosition> _deposits = {})
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

/// Sets every sample of a rectangle of CELLS to one height, which is what makes a test's ground say
/// exactly what the test is about.
[[nodiscard]] bool Level(Frontier::Sim& _sim, std::uint32_t _cellX, std::uint32_t _cellY, std::uint32_t _cellsX, std::uint32_t _cellsY,
                         std::int16_t _height)
{
  Frontier::HeightDelta delta{};
  delta.x = _cellX * SAMPLES;
  delta.y = _cellY * SAMPLES;
  delta.width = _cellsX * SAMPLES + 1;
  delta.height = _cellsY * SAMPLES + 1;
  delta.heights.assign(static_cast<std::size_t>(delta.width) * delta.height, _height);
  return _sim.FlattenTerrain(delta);
}

/// One sample, for the cases that are about a single step between two of them.
[[nodiscard]] bool RaiseSample(Frontier::Sim& _sim, std::uint32_t _sampleX, std::uint32_t _sampleY, std::int16_t _height)
{
  Frontier::HeightDelta delta{};
  delta.x = _sampleX;
  delta.y = _sampleY;
  delta.width = 1;
  delta.height = 1;
  delta.heights = {_height};
  return _sim.FlattenTerrain(delta);
}

void Reveal(Frontier::Sim& _sim, std::uint8_t _seat, std::uint32_t _cellX, std::uint32_t _cellY, std::uint32_t _cellsX,
            std::uint32_t _cellsY)
{
  for (std::uint32_t y = _cellY; y < _cellY + _cellsY; ++y)
  {
    for (std::uint32_t x = _cellX; x < _cellX + _cellsX; ++x)
    {
      _sim.SeatAt(_seat).fog.AddViewer(x, y);
    }
  }
}

Frontier::ObjectId Placed(Frontier::Sim& _sim, std::uint8_t _seat, Row _row, std::uint32_t _cellX, std::uint32_t _cellY,
                          Frontier::StructurePhase _state)
{
  Frontier::Structure structure{};
  structure.seat = _seat;
  structure.design = static_cast<std::uint32_t>(_row);
  structure.cellX = _cellX;
  structure.cellY = _cellY;
  structure.state = _state;
  structure.hitPoints = 100;
  structure.working = Frontier::NO_OBJECT;
  return _sim.Objects().Create(structure);
}

/// The fault CheckPlacement gives a footprint of _row at a cell, for seat 0.
[[nodiscard]] Frontier::PlacementFault FaultAt(Frontier::Sim& _sim, Row _row, std::uint32_t _cellX, std::uint32_t _cellY)
{
  const Frontier::StructureDesc& row = Tables().structures.structures[static_cast<std::uint32_t>(_row)];
  const Frontier::Footprint footprint = Frontier::FootprintAt(row, _cellX, _cellY);
  const Frontier::PlacementQuery query{&_sim.Terrain(), &_sim.Objects(), &_sim.Seats()[0], &row, &Tables(), &_sim.Power().Deposits()};
  return Frontier::CheckPlacement(footprint, query);
}

} // namespace

TEST_CLASS(PlacementTests)
{
public:
  TEST_METHOD(AFootprintMustLieWhollyOnTheLandscape)
  {
    Frontier::Sim sim(TwoSeats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(Ground()));
    // The border of a generated landscape falls to the outside plain, which is under the sea
    // (Content/LandscapeDefinition.h), so the ground is levelled first: this test is about the
    // bounds and nothing else.
    Assert::IsTrue(Level(sim, 123, 123, 5, 5, 40));
    Reveal(sim, 0, 120, 120, 8, 8);
    // 125 plus three is 128, which is one past the last cell of a Small landscape.
    Assert::IsTrue(Frontier::PlacementFault::OffLandscape == FaultAt(sim, Row::Factory, 126, 120), L"the far edge");
    Assert::IsTrue(Frontier::PlacementFault::OffLandscape == FaultAt(sim, Row::Factory, 120, 126));
    Assert::IsTrue(Frontier::PlacementFault::Accepted == FaultAt(sim, Row::Factory, 125, 125), L"and 125 is the last that fits");
  }

  TEST_METHOD(AStandingStructureBlocksAndAPlanDoesNot)
  {
    // "A placed plan costs nothing and obstructs nothing until a builder begins it"
    // (GameDesign.md §5), which is the whole reason a plan is a state and not a separate record.
    Frontier::Sim sim(TwoSeats(), Tables());
    // Deposits under the two cells the one-by-one probe uses, so that what it measures is the
    // occupancy rule and not the extractor's own.
    Assert::IsTrue(sim.CreateLandscape(Ground({{32, 32}, {33, 33}})));
    Assert::IsTrue(Level(sim, 28, 28, 10, 10, 40));
    Reveal(sim, 0, 28, 28, 10, 10);
    const Frontier::ObjectId plan = Placed(sim, 0, Row::Factory, 30, 30, Frontier::StructurePhase::Plan);
    Assert::IsTrue(Frontier::PlacementFault::Accepted == FaultAt(sim, Row::Factory, 30, 30), L"two plans may share ground");

    sim.Objects().FindStructure(plan)->state = Frontier::StructurePhase::UnderConstruction;
    Assert::IsTrue(Frontier::PlacementFault::Occupied == FaultAt(sim, Row::Factory, 30, 30), L"the moment a builder begins it");
    Assert::IsTrue(Frontier::PlacementFault::Occupied == FaultAt(sim, Row::Extractor, 32, 32), L"the far corner of a three by three");
    Assert::IsTrue(Frontier::PlacementFault::Accepted == FaultAt(sim, Row::Extractor, 33, 33), L"and one cell past it is free");
  }

  TEST_METHOD(AFeatureBlocksTheGroundItStandsOn)
  {
    Frontier::Sim sim(TwoSeats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(Ground()));
    Assert::IsTrue(Level(sim, 28, 28, 10, 10, 40));
    Reveal(sim, 0, 28, 28, 10, 10);
    Frontier::Feature rock{};
    rock.cellX = 31;
    rock.cellY = 31;
    (void)sim.Objects().Create(rock);
    Assert::IsTrue(Frontier::PlacementFault::Occupied == FaultAt(sim, Row::Factory, 30, 30));
    Assert::IsTrue(Frontier::PlacementFault::Accepted == FaultAt(sim, Row::Factory, 32, 32), L"clear of it");
  }

  TEST_METHOD(WaterUnderAnyCellRefusesIt)
  {
    Frontier::Sim sim(TwoSeats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(Ground()));
    Reveal(sim, 0, 28, 28, 10, 10);
    Assert::IsTrue(Level(sim, 30, 30, 3, 3, 40));
    Assert::IsTrue(Frontier::PlacementFault::Accepted == FaultAt(sim, Row::Factory, 30, 30));
    // Sea level is zero (Content/LandscapeDefinition.h), so one cell put under it is enough.
    Assert::IsTrue(Level(sim, 32, 32, 1, 1, -10));
    Assert::IsTrue(Frontier::PlacementFault::Water == FaultAt(sim, Row::Factory, 30, 30), L"one corner in the sea");
  }

  TEST_METHOD(TheSteepestCellDecidesTheSlopeAndNotTheAverageAcrossIt)
  {
    // This is the whole of the reading ADR-less choice in Sim/Placement.h, pinned by a case the
    // other reading would accept: a three-by-three of level ground with ONE eight-unit step in the
    // middle of it. Across the footprint that is 8 world units over 192, which is 4%; under the
    // one cell that carries it, it is 8 over the 16-unit sample spacing, which is 50%.
    Frontier::Sim sim(TwoSeats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(Ground()));
    Reveal(sim, 0, 48, 48, 6, 6);
    Assert::IsTrue(Level(sim, 49, 49, 3, 3, 40));
    Assert::IsTrue(Frontier::PlacementFault::Accepted == FaultAt(sim, Row::Factory, 49, 49), L"level ground");
    Assert::AreEqual(0u, Frontier::FootprintSlopePercent(sim.Terrain(), {49, 49, 3, 3}));

    Assert::IsTrue(RaiseSample(sim, 50 * SAMPLES + 2, 50 * SAMPLES + 2, 48));
    Assert::AreEqual(50u, Frontier::FootprintSlopePercent(sim.Terrain(), {49, 49, 3, 3}), L"eight world units over the 16 between samples");
    Assert::IsTrue(Frontier::PlacementFault::TooSteep == FaultAt(sim, Row::Factory, 49, 49));
    Assert::IsTrue(Frontier::MAX_PLACEMENT_SLOPE_PERCENT == 25u, L"the same number the drive table gives wheels");
  }

  TEST_METHOD(GroundTheCommanderHasNotSeenIsRefused)
  {
    // "A structure may be placed anywhere the commander has explored" (GameDesign.md §5): explored
    // and not visible, which is what makes a forward base possible after the scout has left.
    Frontier::Sim sim(TwoSeats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(Ground()));
    Assert::IsTrue(Level(sim, 60, 60, 3, 3, 40));
    Assert::IsTrue(Frontier::PlacementFault::Unexplored == FaultAt(sim, Row::Factory, 60, 60));
    Reveal(sim, 0, 60, 60, 2, 3);
    Assert::IsTrue(Frontier::PlacementFault::Unexplored == FaultAt(sim, Row::Factory, 60, 60),
                   L"two of the three columns is not all of it");
    Reveal(sim, 0, 62, 60, 1, 3);
    Assert::IsTrue(Frontier::PlacementFault::Accepted == FaultAt(sim, Row::Factory, 60, 60));

    // The viewers go away; the history does not, and the placement is still legal.
    for (std::uint32_t y = 60; y < 63; ++y)
    {
      for (std::uint32_t x = 60; x < 63; ++x)
      {
        sim.SeatAt(0).fog.RemoveViewer(x, y);
      }
    }
    Assert::IsFalse(sim.Seats()[0].fog.Visible(61, 61));
    Assert::IsTrue(Frontier::PlacementFault::Accepted == FaultAt(sim, Row::Factory, 60, 60), L"explored, not visible");
  }

  TEST_METHOD(AnExtractorStandsOnADepositAndNowhereElse)
  {
    Frontier::Sim sim(TwoSeats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(Ground({{70, 70}})));
    Assert::IsTrue(Level(sim, 69, 69, 5, 5, 40));
    Reveal(sim, 0, 69, 69, 5, 5);
    Assert::IsTrue(Frontier::PlacementFault::Accepted == FaultAt(sim, Row::Extractor, 70, 70));
    Assert::IsTrue(Frontier::PlacementFault::NotOnDeposit == FaultAt(sim, Row::Extractor, 71, 70));
    // The rule is the extractor's alone; every other role may stand where the landscape allows.
    Assert::IsTrue(Frontier::PlacementFault::Accepted == FaultAt(sim, Row::Factory, 70, 71));
  }

  TEST_METHOD(TheFlattenLevelsEverySampleTheFootprintOwnsToTheirMean)
  {
    Frontier::Sim sim(TwoSeats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(Ground()));
    Assert::IsTrue(Level(sim, 80, 80, 3, 3, 40));
    Assert::IsTrue(RaiseSample(sim, 81 * SAMPLES, 81 * SAMPLES, 53));

    const Frontier::Footprint footprint{80, 80, 3, 3};
    // Thirteen by thirteen samples: four a cell plus the sample that closes the far side, which is
    // the same window the landscape derives a cell from.
    const Frontier::HeightDelta delta = Frontier::FlattenDelta(sim.Terrain(), footprint);
    Assert::AreEqual(80u * SAMPLES, delta.x);
    Assert::AreEqual(80u * SAMPLES, delta.y);
    Assert::AreEqual(13u, delta.width);
    Assert::AreEqual(13u, delta.height);
    Assert::AreEqual(std::size_t{169}, delta.heights.size());
    // 168 samples at 40 and one at 53 is 6,733 over 169, which is 39 and a bit: integers round
    // toward zero, and the fraction is a quarter of a subunit of drawing.
    Assert::AreEqual(40, Frontier::FootprintMeanHeightWorldUnits(sim.Terrain(), footprint));
    Assert::AreEqual(std::int16_t{40}, delta.heights.front());

    Assert::IsTrue(sim.FlattenTerrain(delta));
    Assert::AreEqual(std::int16_t{40}, sim.Terrain().HeightAt(81 * SAMPLES, 81 * SAMPLES), L"the lump is gone");
    Assert::AreEqual(0u, Frontier::FootprintSlopePercent(sim.Terrain(), footprint));
  }

  TEST_METHOD(AFootprintComesFromItsRowAndWithoutTablesItIsOneCell)
  {
    Frontier::Structure structure{};
    structure.design = static_cast<std::uint32_t>(Row::Factory);
    structure.cellX = 10;
    structure.cellY = 20;
    Assert::IsTrue(Frontier::Footprint{10, 20, 3, 3} == Frontier::FootprintOf(structure, &Tables()));
    // No tables: one cell, which is where the structure certainly is and never ground it may not
    // hold. Guessing a size would block placements that are legal.
    Assert::IsTrue(Frontier::Footprint{10, 20, 1, 1} == Frontier::FootprintOf(structure, nullptr));

    Assert::IsTrue(Frontier::Overlaps({10, 20, 3, 3}, {12, 22, 1, 1}), L"the far corner is inside");
    Assert::IsFalse(Frontier::Overlaps({10, 20, 3, 3}, {13, 20, 1, 1}), L"and one past it is not");
    Assert::IsFalse(Frontier::Overlaps({10, 20, 3, 3}, {10, 23, 3, 3}));
  }
};

} // namespace SimTests
