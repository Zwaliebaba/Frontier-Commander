#include "pch.h"

#include "Construction.h"
#include "Production.h"
#include "Research.h"
#include "Sim.h"

#include "FixedPoint.h"

#include <cstdint>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// Research (GameDesign.md §7; m1-vertical-slice/S6): labs in parallel, prerequisites, and upgrades
// that reach what is already standing. The one that is easy to get wrong and hard to notice is the
// last: a device at half health whose maximum grows must still be at half health, and a device
// built before the upgrade must answer the new number at all.
namespace SimTests
{

namespace
{

constexpr std::int32_t CELL = Neuron::SUBUNITS_PER_CELL;

enum class Row : std::uint32_t
{
  CommandPost = 0,
  ResearchLab = 1,
  Extractor = 2,
  Generator = 3
};

enum class Item : std::uint32_t
{
  Ballistics = 0,    ///< 50 power, no prerequisite
  LightArmor = 1,    ///< 30 power, no prerequisite: the cheapest, so auto-research takes it first
  LightHull = 2,     ///< 40 power, needs LightArmor
  Extraction = 3,    ///< 60 power, no prerequisite
  Fortification = 4, ///< 70 power, needs Ballistics
  Laboratories = 5   ///< 30 power, no prerequisite: ties LightArmor, and loses on the row index
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
    Frontier::StructureDesc lab{};
    lab.id = "ResearchLab";
    lab.role = Frontier::StructureRole::ResearchLab;
    lab.footprintCellsX = 2;
    lab.footprintCellsY = 2;
    lab.hitPoints = 600;
    lab.costHundredths = 30000;
    lab.moduleSlots = 1;
    lab.modules = {"LabModule"};
    Frontier::StructureDesc extractor{};
    extractor.id = "Extractor";
    extractor.role = Frontier::StructureRole::Extractor;
    extractor.footprintCellsX = 1;
    extractor.footprintCellsY = 1;
    extractor.hitPoints = 200;
    extractor.powerHundredthsPerTick = 25; // 5 power a second at 20 ticks
    Frontier::StructureDesc generator{};
    generator.id = "Generator";
    generator.role = Frontier::StructureRole::Generator;
    generator.footprintCellsX = 2;
    generator.footprintCellsY = 2;
    generator.hitPoints = 600;
    generator.servesExtractors = 4;
    generator.serviceRangeSubunits = 48 * CELL;
    tree.structures.structures = {post, lab, extractor, generator};

    Frontier::StructureModuleDesc labModule{};
    labModule.id = "LabModule";
    labModule.effect = Frontier::StructureModuleEffect::ShortenResearchTime;
    labModule.amount = 30; // GameDesign.md §5: a lab module takes 30 per cent off
    labModule.costHundredths = 15000;
    labModule.buildTimeTicks = 400;
    tree.structures.modules = {labModule};

    Frontier::ChassisDesc light{};
    light.id = "LightI";
    light.chassisClass = Frontier::ChassisClass::Light;
    light.hitPoints = 100;
    light.kineticArmor = 10;
    light.thermalArmor = 10;
    light.baseSpeedSubunitsPerTick = 1024;
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
    Frontier::ModuleDesc gun{};
    gun.id = "MachineGun";
    gun.systemKind = Frontier::SystemKind::None;
    gun.weaponClass = Frontier::WeaponClass::AntiLight;
    gun.costHundredths = 4000;
    gun.damage = 8;
    tree.components.modules = {gun};

    const auto item = [](const char* _id, std::int32_t _cost, std::uint32_t _ticks, Frontier::ResearchEffect _effect, std::uint8_t _target,
                         std::int32_t _percent, std::vector<std::string> _prerequisites)
    {
      Frontier::ResearchItemDesc row{};
      row.id = _id;
      row.costHundredths = _cost;
      row.timeTicks = _ticks;
      row.effect = _effect;
      row.targetClass = _target;
      row.upgradePercent = _percent;
      row.prerequisites = std::move(_prerequisites);
      return row;
    };
    tree.research = {
      item("Ballistics", 5000, 100, Frontier::ResearchEffect::Unlock, 0, 0, {}),
      item("LightArmor", 3000, 100, Frontier::ResearchEffect::ChassisArmor, static_cast<std::uint8_t>(Frontier::ChassisClass::Light), 20,
           {}),
      item("LightHull", 4000, 100, Frontier::ResearchEffect::ChassisHitPoints, static_cast<std::uint8_t>(Frontier::ChassisClass::Light), 50,
           {"LightArmor"}),
      item("Extraction", 6000, 100, Frontier::ResearchEffect::ExtractorRate, 0, 40, {}),
      item("Fortification", 7000, 100, Frontier::ResearchEffect::StructureHitPoints, 0, 100, {"Ballistics"}),
      item("Laboratories", 3000, 100, Frontier::ResearchEffect::Unlock, 0, 0, {}),
    };
    return tree;
  }();
  return TREE;
}

Frontier::MatchSettings TwoSeats(bool _autoResearch = false)
{
  Frontier::MatchSettings settings{};
  settings.seed = 17;
  settings.sizeClass = Frontier::SizeClass::Small;
  settings.seatCount = 2;
  settings.baseLevel = Frontier::BaseLevel::Nothing;
  settings.powerLevel = Frontier::PowerLevel::High;
  settings.deviceCapLevel = Frontier::DeviceCapLevel::Medium;
  settings.victory = Frontier::VictoryCondition::Annihilation;
  settings.seats[0] = {Frontier::SeatKind::Human, 0, _autoResearch};
  settings.seats[1] = {Frontier::SeatKind::Ai, 1, false};
  return settings;
}

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

Frontier::ObjectId Standing(Frontier::Sim& _sim, std::uint8_t _seat, Row _row, std::uint32_t _cellX, std::uint32_t _cellY)
{
  Frontier::Structure structure{};
  structure.seat = _seat;
  structure.design = static_cast<std::uint32_t>(_row);
  structure.cellX = _cellX;
  structure.cellY = _cellY;
  structure.state = Frontier::StructurePhase::Standing;
  structure.hitPoints = Tables().structures.structures[static_cast<std::uint32_t>(_row)].hitPoints;
  structure.buildEffortHundredths = Frontier::RequiredEffortHundredths(100);
  structure.working = Frontier::NO_OBJECT;
  return _sim.Objects().Create(structure);
}

Frontier::ObjectId Fielded(Frontier::Sim& _sim, std::uint8_t _seat, std::uint32_t _cellX, std::uint32_t _cellY)
{
  Frontier::Seat& seat = _sim.SeatAt(_seat);
  if (seat.designs.empty())
  {
    Frontier::DeviceDesign design{};
    design.chassis = 0;
    design.drive = 0;
    design.modules = {};
    design.modules[0] = 0;
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

} // namespace

TEST_CLASS(ResearchTests)
{
public:
  TEST_METHOD(AnItemWaitsForItsPrerequisitesAndForSomethingToPayForIt)
  {
    Frontier::Sim sim(TwoSeats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(Ground()));
    const Frontier::ObjectId lab = Standing(sim, 0, Row::ResearchLab, 30, 30);
    Frontier::Seat& seat = sim.SeatAt(0);
    seat.powerHundredths = 100000;

    Assert::IsFalse(Frontier::Available(seat, Tables(), static_cast<std::uint32_t>(Item::LightHull)), L"LightArmor comes first");
    Assert::IsFalse(Frontier::SetResearch(sim, 0, lab, static_cast<std::uint32_t>(Item::LightHull)));
    Assert::IsTrue(Frontier::SetResearch(sim, 0, lab, static_cast<std::uint32_t>(Item::LightArmor)));
    Assert::AreEqual(97000, seat.powerHundredths, L"the cost is drawn at the start");
    // One item a lab, whatever else is available.
    Assert::IsFalse(Frontier::SetResearch(sim, 0, lab, static_cast<std::uint32_t>(Item::Ballistics)));

    for (std::uint32_t tick = 0; tick < 100; ++tick)
    {
      sim.Advance();
    }
    Assert::AreEqual(std::size_t{1}, seat.researchComplete.size());
    Assert::IsTrue(Frontier::Available(seat, Tables(), static_cast<std::uint32_t>(Item::LightHull)), L"and now the hull is reachable");

    // Not a penny in the stockpile buys nothing, however available the item is.
    seat.powerHundredths = 3999;
    Assert::IsFalse(Frontier::SetResearch(sim, 0, lab, static_cast<std::uint32_t>(Item::LightHull)));
    seat.powerHundredths = 4000;
    Assert::IsTrue(Frontier::SetResearch(sim, 0, lab, static_cast<std::uint32_t>(Item::LightHull)));
  }

  TEST_METHOD(ThreeLabsResearchThreeItemsAtOnceAndAModuleShortensOne)
  {
    Frontier::Sim sim(TwoSeats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(Ground()));
    const Frontier::ObjectId first = Standing(sim, 0, Row::ResearchLab, 30, 30);
    const Frontier::ObjectId second = Standing(sim, 0, Row::ResearchLab, 40, 40);
    const Frontier::ObjectId third = Standing(sim, 0, Row::ResearchLab, 50, 50);
    Frontier::Seat& seat = sim.SeatAt(0);
    seat.powerHundredths = 100000;
    // The third lab carries a module: 30 per cent off a hundred ticks is seventy.
    sim.Objects().FindStructure(third)->modules[0] = 0;
    sim.Objects().FindStructure(third)->moduleCount = 1;

    Assert::IsTrue(Frontier::SetResearch(sim, 0, first, static_cast<std::uint32_t>(Item::Ballistics)));
    Assert::IsTrue(Frontier::SetResearch(sim, 0, second, static_cast<std::uint32_t>(Item::LightArmor)));
    Assert::IsTrue(Frontier::SetResearch(sim, 0, third, static_cast<std::uint32_t>(Item::Extraction)));
    Assert::AreEqual(std::size_t{3}, seat.researchActive.size(), L"three at once, which is what three labs are for");

    for (std::uint32_t tick = 0; tick < 70; ++tick)
    {
      sim.Advance();
    }
    Assert::AreEqual(std::size_t{1}, seat.researchComplete.size(), L"the one with the module, at seventy ticks");
    Assert::AreEqual(static_cast<std::uint32_t>(Item::Extraction), seat.researchComplete.front());
    for (std::uint32_t tick = 0; tick < 30; ++tick)
    {
      sim.Advance();
    }
    Assert::AreEqual(std::size_t{3}, seat.researchComplete.size(), L"and the other two at a hundred");
    Assert::AreEqual(std::size_t{0}, seat.researchActive.size());
  }

  TEST_METHOD(AnArmourUpgradeReachesADeviceAlreadyInTheFieldAndItKeepsItsFraction)
  {
    Frontier::Sim sim(TwoSeats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(Ground()));
    const Frontier::ObjectId lab = Standing(sim, 0, Row::ResearchLab, 30, 30);
    const Frontier::ObjectId device = Fielded(sim, 0, 60, 60);
    Frontier::Seat& seat = sim.SeatAt(0);
    seat.powerHundredths = 100000;

    Frontier::DesignStats before{};
    Assert::IsTrue(Frontier::DeriveSeatDesign(seat, Tables(), 0, before) == Frontier::DesignFault::None);
    Assert::AreEqual(100, before.hitPoints);
    Assert::AreEqual(10, before.kineticArmor);
    // Half health, which is the state the retroactive rule is actually about.
    sim.Objects().FindDevice(device)->hitPoints = 50;

    Assert::IsTrue(Frontier::SetResearch(sim, 0, lab, static_cast<std::uint32_t>(Item::LightArmor)));
    for (std::uint32_t tick = 0; tick < 100; ++tick)
    {
      sim.Advance();
    }
    Frontier::DesignStats armored{};
    Assert::IsTrue(Frontier::DeriveSeatDesign(seat, Tables(), 0, armored) == Frontier::DesignFault::None);
    Assert::AreEqual(12, armored.kineticArmor, L"a fifth more armour, on a device built before the research");
    Assert::AreEqual(50, sim.Objects().FindDevice(device)->hitPoints, L"an armour upgrade is not a hit-point one");

    // And the hit-point upgrade, which is the one that has to rescale.
    Assert::IsTrue(Frontier::SetResearch(sim, 0, lab, static_cast<std::uint32_t>(Item::LightHull)));
    for (std::uint32_t tick = 0; tick < 100; ++tick)
    {
      sim.Advance();
    }
    Frontier::DesignStats hulled{};
    Assert::IsTrue(Frontier::DeriveSeatDesign(seat, Tables(), 0, hulled) == Frontier::DesignFault::None);
    Assert::AreEqual(150, hulled.hitPoints, L"half again");
    Assert::AreEqual(75, sim.Objects().FindDevice(device)->hitPoints, L"and still half of it, which is the fraction rule");
  }

  TEST_METHOD(AStructureUpgradeAndAnExtractorUpgradeReachTheirOwnKind)
  {
    Frontier::Sim sim(TwoSeats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(Ground({{20, 20}})));
    const Frontier::ObjectId lab = Standing(sim, 0, Row::ResearchLab, 30, 30);
    const Frontier::ObjectId post = Standing(sim, 0, Row::CommandPost, 40, 40);
    Standing(sim, 0, Row::Extractor, 20, 20);
    Standing(sim, 0, Row::Generator, 22, 20);
    Frontier::Seat& seat = sim.SeatAt(0);
    seat.powerHundredths = 100000;
    sim.Objects().FindStructure(post)->hitPoints = 750; // half of 1,500

    seat.researchComplete = {static_cast<std::uint32_t>(Item::Ballistics)};
    Assert::IsTrue(Frontier::SetResearch(sim, 0, lab, static_cast<std::uint32_t>(Item::Fortification)));
    for (std::uint32_t tick = 0; tick < 100; ++tick)
    {
      sim.Advance();
    }
    Assert::AreEqual(100, seat.upgrades.structureHitPointPercent);
    Assert::AreEqual(1500, sim.Objects().FindStructure(post)->hitPoints, L"twice the maximum, and still half of it");

    // The extractor's yield is 5 power a second; 40 per cent more is 7.
    Assert::IsTrue(Frontier::SetResearch(sim, 0, lab, static_cast<std::uint32_t>(Item::Extraction)));
    for (std::uint32_t tick = 0; tick < 100; ++tick)
    {
      sim.Advance();
    }
    Assert::AreEqual(40, seat.upgrades.extractorRatePercent);
    seat.powerHundredths = 0;
    for (std::uint32_t tick = 0; tick < Neuron::TICKS_PER_SECOND; ++tick)
    {
      sim.Advance();
    }
    Assert::AreEqual(700, seat.powerHundredths, L"seven power a second where it was five");
  }

  TEST_METHOD(ADestroyedLabLosesItsProgressAndCancelingRefundsNothing)
  {
    Frontier::Sim sim(TwoSeats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(Ground()));
    const Frontier::ObjectId lab = Standing(sim, 0, Row::ResearchLab, 30, 30);
    Frontier::Seat& seat = sim.SeatAt(0);
    seat.powerHundredths = 100000;
    Assert::IsTrue(Frontier::SetResearch(sim, 0, lab, static_cast<std::uint32_t>(Item::Ballistics)));
    Assert::AreEqual(95000, seat.powerHundredths);
    for (std::uint32_t tick = 0; tick < 50; ++tick)
    {
      sim.Advance();
    }
    Assert::AreEqual(50u, seat.researchActive.front().remainingTicks, L"half way");

    Frontier::DestroyStructure(sim, lab);
    sim.Advance();
    Assert::AreEqual(std::size_t{0}, seat.researchActive.size(), L"the lab took the work with it");
    Assert::AreEqual(95000, seat.powerHundredths, L"and the power with that: a destroyed lab refunds nothing");
    Assert::AreEqual(std::size_t{0}, seat.researchComplete.size());

    // Cancelling is the same on the money and different on the lab: it survives.
    const Frontier::ObjectId second = Standing(sim, 0, Row::ResearchLab, 40, 40);
    Assert::IsTrue(Frontier::SetResearch(sim, 0, second, static_cast<std::uint32_t>(Item::Ballistics)));
    Assert::AreEqual(90000, seat.powerHundredths);
    Assert::IsTrue(Frontier::CancelResearch(sim, 0, second));
    Assert::AreEqual(90000, seat.powerHundredths, L"nothing back: what was bought was the work");
    Assert::IsTrue(Frontier::SetResearch(sim, 0, second, static_cast<std::uint32_t>(Item::Ballistics)), L"and the lab is free again");
  }

  TEST_METHOD(AutoResearchFillsEveryIdleLabWithTheCheapestItLeftAlone)
  {
    Frontier::Sim sim(TwoSeats(true), Tables());
    Assert::IsTrue(sim.CreateLandscape(Ground()));
    Standing(sim, 0, Row::ResearchLab, 30, 30);
    Standing(sim, 0, Row::ResearchLab, 40, 40);
    Frontier::Seat& seat = sim.SeatAt(0);
    seat.powerHundredths = 100000;

    sim.Advance();
    Assert::AreEqual(std::size_t{2}, seat.researchActive.size(), L"both labs, on the first tick");
    // LightArmor and Laboratories both cost 30 power; the tie goes to the lower row index, so the
    // first lab takes LightArmor and the second takes Laboratories. Two hosts pick the same pair.
    Assert::AreEqual(static_cast<std::uint32_t>(Item::LightArmor), seat.researchActive[0].item);
    Assert::AreEqual(static_cast<std::uint32_t>(Item::Laboratories), seat.researchActive[1].item);
    Assert::AreEqual(94000, seat.powerHundredths, L"and both were paid for");

    // Seat 1 has the option off and a lab, and researches nothing at all.
    Standing(sim, 1, Row::ResearchLab, 90, 90);
    sim.SeatAt(1).powerHundredths = 100000;
    sim.Advance();
    Assert::AreEqual(std::size_t{0}, sim.Seats()[1].researchActive.size(), L"a commander who did not ask for it");
  }

  TEST_METHOD(AnUnlockIsNothingButTheItemBeingComplete)
  {
    // A row says what unlocks it and an item says what it unlocks; the simulation reads the first,
    // so there is no second mechanism to keep in step (Sim/Research.h).
    Frontier::Sim sim(TwoSeats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(Ground()));
    const Frontier::ObjectId lab = Standing(sim, 0, Row::ResearchLab, 30, 30);
    Frontier::Seat& seat = sim.SeatAt(0);
    seat.powerHundredths = 100000;
    const Frontier::ClassUpgrades before = seat.upgrades;

    Assert::IsTrue(Frontier::SetResearch(sim, 0, lab, static_cast<std::uint32_t>(Item::Ballistics)));
    for (std::uint32_t tick = 0; tick < 100; ++tick)
    {
      sim.Advance();
    }
    Assert::IsTrue(before == seat.upgrades, L"an unlock changes no percentage");
    Assert::AreEqual(std::size_t{1}, seat.researchComplete.size());
    Assert::IsTrue(Frontier::UnlockedFor(seat, Tables(), "Ballistics"));
    Assert::IsFalse(Frontier::UnlockedFor(seat, Tables(), "Fortification"), L"and unlocks nothing else");
    Assert::IsTrue(Frontier::UnlockedFor(seat, Tables(), ""), L"an empty prerequisite is a row available from the start");
    Assert::IsFalse(Frontier::UnlockedFor(seat, Tables(), "NoSuchItem"), L"and one nothing defines is unreachable, not free");
  }
};

} // namespace SimTests
