#include "pch.h"

#include "Construction.h"
#include "Plan.h"
#include "Sim.h"

#include "FixedPoint.h"

#include <cstdint>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// Construction (GameDesign.md §5; m1-vertical-slice/S4): a plan costs nothing, a builder that
// reaches it begins it and pays for it, the ground under it is levelled, and several builders
// shorten it. The build times here are short so that the suite runs in ticks rather than in
// minutes; what they pin is the ARITHMETIC - a builder at the reference rate takes exactly the
// row's time, and two take half of it - which is the same at any number.
namespace SimTests
{

namespace
{

constexpr std::int32_t CELL = Neuron::SUBUNITS_PER_CELL;
constexpr std::uint32_t FACTORY_TICKS = 20;
constexpr std::uint32_t MODULE_TICKS = 10;

enum class Row : std::uint32_t
{
  CommandPost = 0,
  Factory = 1
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
    post.buildTimeTicks = 40;
    Frontier::StructureDesc factory{};
    factory.id = "Factory";
    factory.role = Frontier::StructureRole::Factory;
    factory.footprintCellsX = 3;
    factory.footprintCellsY = 3;
    factory.hitPoints = 800;
    factory.costHundredths = 40000;
    factory.buildTimeTicks = FACTORY_TICKS;
    factory.moduleSlots = 2;
    factory.modules = {"FactoryModule"};
    tree.structures.structures = {post, factory};

    Frontier::StructureModuleDesc module{};
    module.id = "FactoryModule";
    module.effect = Frontier::StructureModuleEffect::ShortenBuildTime;
    module.amount = 25;
    module.costHundredths = 15000;
    module.buildTimeTicks = MODULE_TICKS;
    tree.structures.modules = {module};

    // One chassis, one drive and one builder module: the least a builder needs to exist.
    Frontier::ChassisDesc light{};
    light.id = "LightI";
    light.chassisClass = Frontier::ChassisClass::Light;
    light.hitPoints = 100;
    light.sightSubunits = 20 * CELL;
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
    builder.systemRangeSubunits = 8 * CELL;
    builder.buildPowerHundredthsPerTick = Frontier::REFERENCE_BUILD_POWER_HUNDREDTHS_PER_TICK;
    tree.components.modules = {builder};
    return tree;
  }();
  return TREE;
}

Frontier::MatchSettings TwoSeats()
{
  Frontier::MatchSettings settings{};
  settings.seed = 11;
  settings.sizeClass = Frontier::SizeClass::Small;
  settings.seatCount = 2;
  settings.baseLevel = Frontier::BaseLevel::Nothing;
  settings.powerLevel = Frontier::PowerLevel::High;
  settings.deviceCapLevel = Frontier::DeviceCapLevel::Medium;
  settings.victory = Frontier::VictoryCondition::Annihilation;
  settings.seats[0] = {Frontier::SeatKind::Human, 0};
  settings.seats[1] = {Frontier::SeatKind::Ai, 1};
  return settings;
}

Frontier::LandscapeDefinition Ground()
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

/// A device carrying the builder module, at a cell.
Frontier::ObjectId Builder(Frontier::Sim& _sim, std::uint8_t _seat, std::uint32_t _cellX, std::uint32_t _cellY)
{
  Frontier::Seat& seat = _sim.SeatAt(_seat);
  if (seat.designs.empty())
  {
    Frontier::DeviceDesign design{};
    design.chassis = 0;
    design.drive = 0;
    design.modules = {};
    design.modules[0] = 0; // the builder module
    design.moduleCount = 1;
    seat.designs.push_back(design);
  }
  Frontier::Device device{};
  device.seat = _seat;
  device.design = 0;
  device.x = static_cast<std::int32_t>(_cellX) * CELL + CELL / 2;
  device.z = static_cast<std::int32_t>(_cellY) * CELL + CELL / 2;
  device.hitPoints = 100;
  device.target = Frontier::NO_OBJECT;
  return _sim.Objects().Create(device);
}

Frontier::ObjectId Standing(Frontier::Sim& _sim, std::uint8_t _seat, Row _row, std::uint32_t _cellX, std::uint32_t _cellY)
{
  Frontier::Structure structure{};
  structure.seat = _seat;
  structure.design = static_cast<std::uint32_t>(_row);
  structure.cellX = _cellX;
  structure.cellY = _cellY;
  structure.state = Frontier::StructureState::Standing;
  structure.hitPoints = 800;
  structure.buildEffortHundredths = Frontier::RequiredEffortHundredths(FACTORY_TICKS);
  structure.working = Frontier::NO_OBJECT;
  return _sim.Objects().Create(structure);
}

/// A match with a command post, explored ground and one plan of _row at (40,40).
struct Site
{
  Frontier::Sim sim{TwoSeats(), Tables()};
  Frontier::ObjectId plan;

  explicit Site(bool _withBuilder = true)
  {
    Assert::IsTrue(sim.CreateLandscape(Ground()));
    Standing(sim, 0, Row::CommandPost, 20, 20);
    Reveal(sim, 0, 36, 36, 12, 12);
    Assert::IsTrue(Frontier::PlaceStructurePlan(sim, 0, static_cast<std::uint32_t>(Row::Factory), 40, 40));
    sim.Objects().ForEachStructure(
      [this](Frontier::ObjectId _id, const Frontier::Structure& _structure)
      {
        if (_structure.state == Frontier::StructureState::Plan)
        {
          plan = _id;
        }
      });
    if (_withBuilder)
    {
      (void)Builder(sim, 0, 43, 43);
    }
  }

  [[nodiscard]] const Frontier::Structure& Structure() const
  {
    const Frontier::Structure* found = sim.Objects().FindStructure(plan);
    if (found == nullptr)
    {
      Assert::Fail(L"the site is gone"); // noreturn, which is what the dereference below relies on
    }
    return *found;
  }
};

} // namespace

TEST_CLASS(ConstructionTests)
{
public:
  TEST_METHOD(APlanCostsNothingAndObstructsNothingUntilABuilderReachesIt)
  {
    Site site(false);
    const std::int32_t before = site.sim.SeatAt(0).powerHundredths;
    site.sim.Advance();
    Assert::IsTrue(Frontier::StructureState::Plan == site.Structure().state, L"nobody is there");
    Assert::AreEqual(before, site.sim.SeatAt(0).powerHundredths, L"and nothing has been drawn");
    Assert::AreEqual(std::uint8_t{0}, site.sim.Terrain().CellAt(41, 41).obstruction);
    Assert::IsFalse(Frontier::Occupies(Frontier::StructureState::Plan));
  }

  TEST_METHOD(ABuilderInRangeBeginsItAndTheCostIsDrawnAtThatMoment)
  {
    // GameDesign.md §4: the cost is drawn when construction begins, never when the plan is placed.
    Site site;
    Frontier::Seat& seat = site.sim.SeatAt(0);
    seat.powerHundredths = 40000;
    site.sim.Advance();
    Assert::IsTrue(Frontier::StructureState::UnderConstruction == site.Structure().state);
    Assert::AreEqual(0, seat.powerHundredths, L"the factory's 400 power, exactly");
    Assert::AreEqual(Frontier::OBSTRUCTION_STRUCTURE, site.sim.Terrain().CellAt(41, 41).obstruction, L"and the ground is taken");
    Assert::AreEqual(1, site.Structure().hitPoints, L"proportional to progress, and never nothing");
  }

  TEST_METHOD(ACommanderWhoCannotPayKeepsThePlanAndNotTheStructure)
  {
    Site site;
    site.sim.SeatAt(0).powerHundredths = 39900;
    site.sim.Advance();
    Assert::IsTrue(Frontier::StructureState::Plan == site.Structure().state);
    Assert::AreEqual(std::uint8_t{0}, site.sim.Terrain().CellAt(41, 41).obstruction);
  }

  TEST_METHOD(OneBuilderTakesTheRowsBuildTimeAndTwoTakeHalfOfIt)
  {
    // The whole of the rate arithmetic (Sim/Construction.h): a builder at the reference rate puts
    // in exactly the row's time, and the effort is summed, so a second builder halves it. The tick
    // that BEGINS the site puts in nothing, which is why the counts below start at one.
    Site one;
    one.sim.SeatAt(0).powerHundredths = 40000;
    std::uint32_t ticks = 0;
    while (one.sim.Objects().FindStructure(one.plan) != nullptr &&
           one.sim.Objects().FindStructure(one.plan)->state != Frontier::StructureState::Standing && ticks < 200)
    {
      one.sim.Advance();
      ++ticks;
    }
    Assert::AreEqual(FACTORY_TICKS + 1, ticks, L"the row's twenty ticks, after the one that began it");
    Assert::AreEqual(800, one.Structure().hitPoints, L"and it stands at the row's full hit points");

    Site two;
    two.sim.SeatAt(0).powerHundredths = 40000;
    (void)Builder(two.sim, 0, 38, 38);
    ticks = 0;
    while (two.sim.Objects().FindStructure(two.plan) != nullptr &&
           two.sim.Objects().FindStructure(two.plan)->state != Frontier::StructureState::Standing && ticks < 200)
    {
      two.sim.Advance();
      ++ticks;
    }
    Assert::AreEqual(FACTORY_TICKS / 2 + 1, ticks, L"two builders, half the time");
  }

  TEST_METHOD(ABuilderOutOfRangeAttendsNothing)
  {
    Site site(false);
    // The builder module reaches eight cells; twenty away is twenty away.
    (void)Builder(site.sim, 0, 60, 60);
    site.sim.Advance();
    Assert::IsTrue(Frontier::StructureState::Plan == site.Structure().state);
  }

  TEST_METHOD(TheTerrainUnderTheFootprintIsLevelledToItsMeanWhenItBegins)
  {
    Site site;
    site.sim.SeatAt(0).powerHundredths = 40000;
    const std::size_t deltasBefore = site.sim.Terrain().Deltas().size();
    const std::int32_t mean = Frontier::FootprintMeanHeightWorldUnits(site.sim.Terrain(), {40, 40, 3, 3});
    site.sim.Advance();
    Assert::AreEqual(deltasBefore + 1, site.sim.Terrain().Deltas().size(), L"the flatten is a height delta, which the snapshot carries");
    Assert::AreEqual(0u, Frontier::FootprintSlopePercent(site.sim.Terrain(), {40, 40, 3, 3}), L"level, to its own edges");
    Assert::AreEqual(mean, static_cast<std::int32_t>(
                             site.sim.Terrain().HeightAt(41 * Frontier::SAMPLES_PER_CELL_EDGE, 41 * Frontier::SAMPLES_PER_CELL_EDGE)));
    Assert::AreEqual(mean * Neuron::SUBUNITS_PER_WORLD_UNIT, site.Structure().y, L"and the structure stands at it, in subunits");
  }

  TEST_METHOD(HitPointsFollowTheProgressAndTheSiteCanBeDestroyedMidway)
  {
    Site site;
    site.sim.SeatAt(0).powerHundredths = 40000;
    for (std::uint32_t tick = 0; tick < 1 + FACTORY_TICKS / 2; ++tick)
    {
      site.sim.Advance();
    }
    Assert::AreEqual(5000, Frontier::ProgressHundredths(site.Structure().buildEffortHundredths, FACTORY_TICKS), L"halfway");
    Assert::AreEqual(400, site.Structure().hitPoints, L"half of the row's 800");

    Frontier::DestroyStructure(site.sim, site.plan);
    Assert::IsNull(site.sim.Objects().FindStructure(site.plan));
    Assert::AreEqual(std::size_t{1}, site.sim.Objects().Count(Frontier::ObjectKind::Wreck), L"a site that dies leaves a wreck");
    Assert::AreEqual(std::uint8_t{0}, site.sim.Terrain().CellAt(41, 41).obstruction, L"and gives its ground back");
  }

  TEST_METHOD(CancelingRefundsTheShareNotYetBuiltAndDemolishingRefundsHalf)
  {
    Site site;
    Frontier::Seat& seat = site.sim.SeatAt(0);
    seat.powerHundredths = 40000;
    for (std::uint32_t tick = 0; tick < 1 + FACTORY_TICKS / 4; ++tick)
    {
      site.sim.Advance();
    }
    Assert::AreEqual(2500, Frontier::ProgressHundredths(site.Structure().buildEffortHundredths, FACTORY_TICKS));
    seat.powerHundredths = 0;
    Assert::IsTrue(Frontier::CancelStructure(site.sim, 0, site.plan));
    Assert::AreEqual(30000, seat.powerHundredths, L"three quarters of 400 power back");
    Assert::AreEqual(std::uint8_t{0}, site.sim.Terrain().CellAt(41, 41).obstruction);
    Assert::AreEqual(std::size_t{0}, site.sim.Objects().Count(Frontier::ObjectKind::Wreck), L"a cancellation is not a death");

    // A standing structure is demolished instead, for half.
    Site standing;
    const Frontier::ObjectId built = Standing(standing.sim, 0, Row::Factory, 60, 60);
    standing.sim.SeatAt(0).powerHundredths = 0;
    Assert::IsFalse(Frontier::CancelStructure(standing.sim, 0, built), L"cancel is for what is not finished");
    Assert::IsTrue(Frontier::DemolishStructure(standing.sim, 0, built));
    Assert::AreEqual(20000, standing.sim.SeatAt(0).powerHundredths, L"half of 400 power");
    Assert::AreEqual(std::size_t{0}, standing.sim.Objects().Count(Frontier::ObjectKind::Wreck), L"and no wreck: it was dismantled");
  }

  TEST_METHOD(AModuleGoesOntoAStandingStructureAndThenCounts)
  {
    Site site;
    const Frontier::ObjectId factory = Standing(site.sim, 0, Row::Factory, 60, 60);
    (void)Builder(site.sim, 0, 62, 62);
    Frontier::Seat& seat = site.sim.SeatAt(0);
    seat.powerHundredths = 15000;
    Assert::IsTrue(Frontier::BeginModule(site.sim, 0, factory, 0));
    Assert::IsFalse(Frontier::BeginModule(site.sim, 0, factory, 0), L"one at a time");

    for (std::uint32_t tick = 0; tick < MODULE_TICKS; ++tick)
    {
      site.sim.Advance();
    }
    const Frontier::Structure* built = site.sim.Objects().FindStructure(factory);
    Assert::IsNotNull(built);
    Assert::AreEqual(std::uint8_t{1}, built->moduleCount, L"the module is on it");
    Assert::AreEqual(0u, built->modules[0]);
    Assert::IsTrue(Frontier::NO_STRUCTURE_MODULE == built->moduleUnderConstruction);
    Assert::AreEqual(0, seat.powerHundredths, L"and its 150 power was drawn when the builder started it");
  }

  TEST_METHOD(ModulesShortenTheTimesTheDesignSaysTheyDo)
  {
    // GameDesign.md §5's numbers, read from the rows: a factory module takes 25 off and two take
    // 50, and a lab module takes 30. Nothing consumes either yet - m1-vertical-slice/S5 and S6 do -
    // so this is the arithmetic pinned where it is written, before it has a second home.
    Frontier::ContentTree tree;
    Frontier::StructureModuleDesc factoryModule{};
    factoryModule.id = "FactoryModule";
    factoryModule.effect = Frontier::StructureModuleEffect::ShortenBuildTime;
    factoryModule.amount = 25;
    Frontier::StructureModuleDesc labModule{};
    labModule.id = "LabModule";
    labModule.effect = Frontier::StructureModuleEffect::ShortenResearchTime;
    labModule.amount = 30;
    tree.structures.modules = {factoryModule, labModule};

    Frontier::Structure factory{};
    factory.modules[0] = 0;
    factory.moduleCount = 1;
    Assert::AreEqual(25, Frontier::ModuleTimeReductionPercent(factory, tree, Frontier::StructureModuleEffect::ShortenBuildTime));
    factory.modules[1] = 0;
    factory.moduleCount = 2;
    Assert::AreEqual(50, Frontier::ModuleTimeReductionPercent(factory, tree, Frontier::StructureModuleEffect::ShortenBuildTime));
    Assert::AreEqual(0, Frontier::ModuleTimeReductionPercent(factory, tree, Frontier::StructureModuleEffect::ShortenResearchTime),
                     L"a factory module shortens no research");

    Frontier::Structure lab{};
    lab.modules[0] = 1;
    lab.moduleCount = 1;
    Assert::AreEqual(30, Frontier::ModuleTimeReductionPercent(lab, tree, Frontier::StructureModuleEffect::ShortenResearchTime));

    Assert::AreEqual(600u, Frontier::ShortenedTicks(1200, 50));
    Assert::AreEqual(630u, Frontier::ShortenedTicks(900, 30));
    Assert::AreEqual(1u, Frontier::ShortenedTicks(1, 99), L"never nothing, whatever the reduction");
  }

  TEST_METHOD(APlanIsRefusedPastTheSixtyFourthAndAWreckDecaysAway)
  {
    Frontier::Sim sim(TwoSeats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(Ground()));
    for (std::uint32_t index = 0; index < Frontier::MAX_PLANS_PER_SEAT; ++index)
    {
      Assert::IsTrue(Frontier::PlaceStructurePlan(sim, 0, static_cast<std::uint32_t>(Row::Factory), 4 + index, 4));
    }
    Assert::AreEqual(Frontier::MAX_PLANS_PER_SEAT, Frontier::PlanCount(sim.Objects(), 0));
    Assert::IsFalse(Frontier::PlaceStructurePlan(sim, 0, static_cast<std::uint32_t>(Row::Factory), 80, 80), L"sixty-four is the limit");
    Assert::IsTrue(Frontier::PlaceStructurePlan(sim, 1, static_cast<std::uint32_t>(Row::Factory), 80, 80), L"and it is per commander");

    Frontier::Wreck wreck{};
    wreck.origin = Frontier::NO_OBJECT;
    wreck.decayTicks = 3;
    (void)sim.Objects().Create(wreck);
    for (std::uint32_t tick = 0; tick < 2; ++tick)
    {
      sim.Advance();
    }
    Assert::AreEqual(std::size_t{1}, sim.Objects().Count(Frontier::ObjectKind::Wreck), L"still lying there");
    sim.Advance();
    Assert::AreEqual(std::size_t{0}, sim.Objects().Count(Frontier::ObjectKind::Wreck), L"and gone on the third");
  }
};

} // namespace SimTests
