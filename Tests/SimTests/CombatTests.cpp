#include "pch.h"

#include "ClusterGraph.h"
#include "Construction.h"
#include "Damage.h"
#include "Design.h"
#include "Experience.h"
#include "Movement.h"
#include "Production.h"
#include "Sim.h"
#include "Targeting.h"
#include "Weapons.h"

#include "FixedPoint.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// Combat as GameDesign.md §8 writes it (m1-vertical-slice/S10): what a weapon may shoot at, what
// the stances change, what a shell does between the muzzle and the ground, what a kill is worth,
// and who leaves the fight. The formula itself is DamageTests'; what is measured here is the
// simulation applying it - the rules that need a world, a fog of war and a tick to be true or false.
namespace SimTests
{

namespace
{

constexpr std::int32_t CELL = Neuron::SUBUNITS_PER_CELL;
constexpr std::uint32_t SAMPLES = Frontier::SAMPLES_PER_CELL_EDGE;

enum class Row : std::uint32_t
{
  CommandPost = 0,
  Tower = 1,
  RepairBay = 2
};

enum class Chassis : std::uint32_t
{
  Light = 0
};

enum class Drive : std::uint32_t
{
  Wheels = 0,
  Tracks = 1
};

enum class Module : std::uint32_t
{
  MachineGun = 0,
  Mortar = 1,
  Repair = 2
};

/// The shipped rows the design works through, plus the two M1's content does not ship and this
/// suite needs: a tower, which carries a weapon and no stances, and a repair bay, which is the
/// stub the acceptance asks the retreat rule to be tested against.
const Frontier::ContentTree& Tables()
{
  static const Frontier::ContentTree TREE = []
  {
    Frontier::ContentTree tree;

    Frontier::StructureDesc post{};
    post.id = "CommandPost";
    post.role = Frontier::StructureRole::CommandPost;
    post.strength = Frontier::StrengthClass::Hard;
    post.footprintCellsX = 3;
    post.footprintCellsY = 3;
    post.hitPoints = 1500;
    post.kineticArmor = 20;
    post.thermalArmor = 18;
    post.costHundredths = 50000;
    post.buildTimeTicks = 1200;
    post.sightSubunits = 12 * CELL;
    Frontier::StructureDesc tower{};
    tower.id = "Tower";
    tower.role = Frontier::StructureRole::Tower;
    tower.strength = Frontier::StrengthClass::Medium;
    tower.footprintCellsX = 1;
    tower.footprintCellsY = 1;
    tower.hitPoints = 400;
    tower.kineticArmor = 10;
    tower.thermalArmor = 8;
    tower.costHundredths = 20000;
    tower.buildTimeTicks = 600;
    tower.sightSubunits = 20 * CELL;
    tower.weapon = "MachineGun";
    Frontier::StructureDesc bay{};
    bay.id = "RepairBay";
    bay.role = Frontier::StructureRole::RepairBay;
    bay.strength = Frontier::StrengthClass::Medium;
    bay.footprintCellsX = 2;
    bay.footprintCellsY = 2;
    bay.hitPoints = 500;
    bay.costHundredths = 25000;
    bay.buildTimeTicks = 600;
    bay.sightSubunits = 12 * CELL;
    bay.repairHitPointsHundredthsPerTick = 75;
    tree.structures.structures = {post, tower, bay};

    Frontier::ChassisDesc light{};
    light.id = "LightI";
    light.chassisClass = Frontier::ChassisClass::Light;
    light.hitPoints = 100;
    light.kineticArmor = 5;
    light.thermalArmor = 5;
    light.baseSpeedSubunitsPerTick = 1024;
    light.sightSubunits = 20 * CELL;
    light.costHundredths = 6000;
    light.mounts = 1;
    tree.components.chassis = {light};

    Frontier::DriveDesc wheels{};
    wheels.id = "Wheels";
    wheels.driveClass = Frontier::DriveClass::Wheels;
    wheels.speedFactorHundredths = 130;
    wheels.maxSlopePercent = 25;
    wheels.hitPointFactorHundredths = 100;
    wheels.costHundredths = 3000;
    Frontier::DriveDesc tracks{};
    tracks.id = "Tracks";
    tracks.driveClass = Frontier::DriveClass::Tracks;
    tracks.speedFactorHundredths = 80;
    tracks.maxSlopePercent = 40;
    tracks.hitPointFactorHundredths = 150;
    tracks.costHundredths = 7000;
    tree.components.drives = {wheels, tracks};

    Frontier::ModuleDesc gun{};
    gun.id = "MachineGun";
    gun.systemKind = Frontier::SystemKind::None;
    gun.weaponClass = Frontier::WeaponClass::AntiLight;
    gun.costHundredths = 4000;
    gun.damage = 8;
    gun.shotsPerSalvo = 1;
    gun.reloadTicks = 5;
    gun.fireKind = Frontier::FireKind::Direct;
    gun.shortRangeSubunits = 8 * CELL;
    gun.longRangeSubunits = 12 * CELL;
    gun.shortHitPercent = 80;
    gun.longHitPercent = 50;
    Frontier::ModuleDesc mortar{};
    mortar.id = "Mortar";
    mortar.systemKind = Frontier::SystemKind::None;
    mortar.weaponClass = Frontier::WeaponClass::Artillery;
    mortar.costHundredths = 11000;
    mortar.damage = 80;
    mortar.shotsPerSalvo = 1;
    mortar.reloadTicks = 80;
    mortar.fireKind = Frontier::FireKind::Indirect;
    mortar.minimumRangeSubunits = 6 * CELL;
    mortar.shortRangeSubunits = 6 * CELL;
    mortar.longRangeSubunits = 28 * CELL;
    mortar.shortHitPercent = 100;
    mortar.longHitPercent = 100;
    mortar.splashSubunits = 2 * CELL;
    Frontier::ModuleDesc repair{};
    repair.id = "Repair";
    repair.systemKind = Frontier::SystemKind::Repair;
    repair.costHundredths = 5000;
    repair.systemRangeSubunits = 4 * CELL;
    repair.repairHitPointsHundredthsPerTick = 75;
    tree.components.modules = {gun, mortar, repair};

    // The version-1 matrix of GameDesign.md §8; DamageTests pins the arithmetic against it.
    tree.damage.modifierPercent[static_cast<std::size_t>(Frontier::WeaponClass::AntiLight)] = {120, 100, 50, 110, 130,
                                                                                               100, 120, 60, 30,  20};
    tree.damage.modifierPercent[static_cast<std::size_t>(Frontier::WeaponClass::AntiTank)] = {90, 100, 120, 90, 70, 60, 80, 100, 110, 60};
    tree.damage.modifierPercent[static_cast<std::size_t>(Frontier::WeaponClass::Flame)] = {130, 110, 70, 120, 140, 40, 150, 80, 40, 10};
    tree.damage.modifierPercent[static_cast<std::size_t>(Frontier::WeaponClass::Artillery)] = {100, 100, 100, 100, 100,
                                                                                               20,  130, 120, 100, 60};
    tree.damage.modifierPercent[static_cast<std::size_t>(Frontier::WeaponClass::Energy)] = {100, 100, 100, 100, 100,
                                                                                            100, 100, 100, 100, 80};
    tree.damage.armorFactorPercent = {100, 100, 100, 0, 50};
    tree.damage.armorKind = {Frontier::ArmorKind::Kinetic, Frontier::ArmorKind::Kinetic, Frontier::ArmorKind::Thermal,
                             Frontier::ArmorKind::Kinetic, Frontier::ArmorKind::Kinetic};
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
  settings.powerLevel = Frontier::PowerLevel::Medium;
  settings.deviceCapLevel = Frontier::DeviceCapLevel::Medium;
  settings.victory = Frontier::VictoryCondition::Annihilation;
  settings.seats[0] = {Frontier::SeatKind::Human, 0};
  settings.seats[1] = {Frontier::SeatKind::Human, 1};
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

/// Level ground where the fight happens, so that a ridge between two devices is never the reason
/// one of them did not fire: what is under test is the rules and not the heightfield.
[[nodiscard]] bool Level(Frontier::Sim& _sim, std::uint32_t _cellX, std::uint32_t _cellY, std::uint32_t _cells, std::int16_t _height)
{
  Frontier::HeightDelta delta{};
  delta.x = _cellX * SAMPLES;
  delta.y = _cellY * SAMPLES;
  delta.width = _cells * SAMPLES + 1;
  delta.height = _cells * SAMPLES + 1;
  delta.heights.assign(static_cast<std::size_t>(delta.width) * delta.height, _height);
  return _sim.FlattenTerrain(delta);
}

[[nodiscard]] std::int32_t Middle(std::uint32_t _cell)
{
  return static_cast<std::int32_t>(_cell) * CELL + CELL / 2;
}

[[nodiscard]] Frontier::DeviceDesign Designed(Chassis _chassis, Drive _drive, Module _module)
{
  Frontier::DeviceDesign design{};
  design.chassis = static_cast<std::uint32_t>(_chassis);
  design.drive = static_cast<std::uint32_t>(_drive);
  design.modules = {};
  design.modules[0] = static_cast<std::uint32_t>(_module);
  design.moduleCount = 1;
  return design;
}

Frontier::ObjectId Spawn(Frontier::Sim& _sim, std::uint8_t _seat, std::uint32_t _design, std::int32_t _x, std::int32_t _z)
{
  Frontier::DesignStats stats{};
  Assert::IsTrue(Frontier::DeriveSeatDesign(_sim.Seats()[_seat], Tables(), _design, stats) == Frontier::DesignFault::None);
  Frontier::Device device{};
  device.seat = _seat;
  device.design = _design;
  device.x = _x;
  device.z = _z;
  device.y = Frontier::GroundHeightSubunits(_sim.Terrain(), _x, _z);
  device.hitPoints = stats.hitPoints;
  device.primaryOrder = Frontier::PrimaryOrder::Stop;
  device.target = Frontier::NO_OBJECT;
  device.destinationX = _x;
  device.destinationZ = _z;
  device.anchorX = _x;
  device.anchorZ = _z;
  device.fire = Frontier::FireStance::FireAtWill;
  device.range = Frontier::RangeStance::Optimal;
  device.retreat = Frontier::RetreatStance::Never;
  device.movement = Frontier::MovementStance::HoldPosition;
  return _sim.Objects().Create(device);
}

Frontier::ObjectId Standing(Frontier::Sim& _sim, std::uint8_t _seat, Row _row, std::uint32_t _cellX, std::uint32_t _cellY)
{
  Frontier::Structure structure{};
  structure.seat = _seat;
  structure.design = static_cast<std::uint32_t>(_row);
  structure.cellX = _cellX;
  structure.cellY = _cellY;
  structure.state = Frontier::StructurePhase::Standing;
  structure.hitPoints = Tables().structures.structures[static_cast<std::size_t>(_row)].hitPoints;
  structure.buildEffortHundredths = Frontier::RequiredEffortHundredths(600);
  structure.moduleUnderConstruction = Frontier::NO_STRUCTURE_MODULE;
  structure.working = Frontier::NO_OBJECT;
  return _sim.Objects().Create(structure);
}

const Frontier::Device* DeviceAt(const Frontier::Sim& _sim, Frontier::ObjectId _id)
{
  return _sim.Objects().FindDevice(_id);
}

/// A match on level ground with the three designs saved for both commanders.
struct Field
{
  Frontier::Sim sim{TwoSeats(), Tables()};

  Field()
  {
    Assert::IsTrue(sim.CreateLandscape(Ground()));
    Assert::IsTrue(Level(sim, 50, 50, 40, 40));
    for (std::uint8_t seat = 0; seat < 2; ++seat)
    {
      Assert::IsTrue(Frontier::SaveDesign(sim, seat, 0, Designed(Chassis::Light, Drive::Wheels, Module::MachineGun)));
      Assert::IsTrue(Frontier::SaveDesign(sim, seat, 1, Designed(Chassis::Light, Drive::Wheels, Module::Mortar)));
      Assert::IsTrue(Frontier::SaveDesign(sim, seat, 2, Designed(Chassis::Light, Drive::Wheels, Module::Repair)));
    }
  }
};

} // namespace

TEST_CLASS(CombatTests)
{
public:
  TEST_METHOD(AMachineGunAcquiresWhatItCanSeeFiresAtItAndKillsIt)
  {
    Field field;
    const Frontier::ObjectId mine = Spawn(field.sim, 0, 0, Middle(60), Middle(60));
    const Frontier::ObjectId theirs = Spawn(field.sim, 1, 0, Middle(64), Middle(60));

    std::uint32_t died = 0;
    for (std::uint32_t tick = 0; tick < 600 && died == 0; ++tick)
    {
      field.sim.Advance();
      if (DeviceAt(field.sim, theirs) == nullptr || DeviceAt(field.sim, mine) == nullptr)
      {
        died = field.sim.Tick();
      }
    }
    Logger::WriteMessage((L"measured: two scouts four cells apart settled it on tick " + std::to_wstring(died)).c_str());
    Assert::IsTrue(died > 0, L"one of them died");
    // A scout is 100 hit points and a machine gun deals 4 through 5 armour at 80 percent every
    // five ticks, so a one-sided kill is about 160 ticks and a mutual one rather less.
    Assert::IsTrue(died > 100 && died < 400, L"in about the time the tables allow");
    Assert::IsTrue(field.sim.Objects().Count(Frontier::ObjectKind::Wreck) > 0, L"and left a wreck");
  }

  TEST_METHOD(ARankIsEarnedByKillsWeightedByWhatTheyCost)
  {
    // The weighting of Sim/Experience.h, against the reference device it is defined by.
    Assert::AreEqual(1u, Frontier::ExperienceForKill(13000), L"the scout GameDesign.md §6 sizes by");
    Assert::AreEqual(1u, Frontier::ExperienceForKill(6000), L"and anything cheaper still counts");
    Assert::AreEqual(3u, Frontier::ExperienceForKill(50000), L"a command post is worth three");
    Assert::AreEqual(0u, static_cast<std::uint32_t>(Frontier::RankOf(1)));
    Assert::AreEqual(1u, static_cast<std::uint32_t>(Frontier::RankOf(2)));

    // And the killer is the one that gets it: a lone gunner against a target that cannot answer.
    Field field;
    const Frontier::ObjectId gunner = Spawn(field.sim, 0, 0, Middle(60), Middle(60));
    (void)Spawn(field.sim, 1, 2, Middle(64), Middle(60)); // A repair device carries no weapon
    for (std::uint32_t tick = 0; tick < 600; ++tick)
    {
      field.sim.Advance();
      if (field.sim.Objects().Count(Frontier::ObjectKind::Device) == 1)
      {
        break;
      }
    }
    Assert::IsTrue(DeviceAt(field.sim, gunner) != nullptr, L"the unarmed one was the one that died");
    Assert::AreEqual(1u, DeviceAt(field.sim, gunner)->experience, L"and the kill was worth one");
  }

  TEST_METHOD(HoldFireNeverShootsAndReturnFireAnswersWhatShotAtIt)
  {
    Field field;
    const Frontier::ObjectId quiet = Spawn(field.sim, 0, 0, Middle(60), Middle(60));
    const Frontier::ObjectId other = Spawn(field.sim, 1, 0, Middle(64), Middle(60));
    field.sim.Objects().FindDevice(quiet)->fire = Frontier::FireStance::HoldFire;
    field.sim.Objects().FindDevice(other)->fire = Frontier::FireStance::HoldFire;
    for (std::uint32_t tick = 0; tick < 200; ++tick)
    {
      field.sim.Advance();
    }
    Assert::IsTrue(DeviceAt(field.sim, quiet) != nullptr && DeviceAt(field.sim, other) != nullptr, L"neither fired a shot");
    Assert::IsTrue(DeviceAt(field.sim, quiet)->target == Frontier::NO_OBJECT, L"and hold fire acquires nothing");

    // Return fire takes no target of its own, and takes one the moment it is shot at.
    field.sim.Objects().FindDevice(quiet)->fire = Frontier::FireStance::ReturnFire;
    field.sim.Advance();
    Assert::IsTrue(DeviceAt(field.sim, quiet)->target == Frontier::NO_OBJECT, L"return fire goes looking for nobody");
    field.sim.Objects().FindDevice(other)->fire = Frontier::FireStance::FireAtWill;
    for (std::uint32_t tick = 0; tick < 60; ++tick)
    {
      field.sim.Advance();
    }
    Assert::IsTrue(DeviceAt(field.sim, quiet) == nullptr || DeviceAt(field.sim, quiet)->target == other,
                   L"and answers the one that shot at it");
  }

  TEST_METHOD(AMortarNeedsASpotterAndMissesWhatWalksOutFromUnderIt)
  {
    {
      // Twenty-four cells away: inside the mortar's twenty-eight and well outside the twenty its
      // chassis sees, so nothing of its commander's is looking and it must hold (GameDesign.md §8).
      Field field;
      Assert::IsTrue(Level(field.sim, 20, 55, 60, 40));
      const Frontier::ObjectId mortar = Spawn(field.sim, 0, 1, Middle(40), Middle(60));
      const Frontier::ObjectId distant = Spawn(field.sim, 1, 2, Middle(64), Middle(60));
      for (std::uint32_t tick = 0; tick < 200; ++tick)
      {
        field.sim.Advance();
      }
      Assert::IsTrue(DeviceAt(field.sim, mortar)->target == Frontier::NO_OBJECT, L"no spotter, no target");
      Assert::AreEqual(std::size_t{0}, field.sim.Objects().Count(Frontier::ObjectKind::Projectile), L"and no shell");
      Assert::IsTrue(DeviceAt(field.sim, distant) != nullptr);

      // Give it a spotter - anything of its commander's with the target in its own sight - and the
      // same shot becomes legal.
      (void)Spawn(field.sim, 0, 2, Middle(56), Middle(60));
      bool fired = false;
      for (std::uint32_t tick = 0; tick < 200 && !fired; ++tick)
      {
        field.sim.Advance();
        fired = field.sim.Objects().Count(Frontier::ObjectKind::Projectile) > 0 || DeviceAt(field.sim, distant) == nullptr;
      }
      Assert::IsTrue(fired, L"with a spotter it fires");
    }
    {
      // A shell is aimed at the ground the target stood on, and the arithmetic of the miss is the
      // flight against the splash: at twenty-four cells a shell is thirty-two ticks in the air, a
      // scout covers two and a half cells in that time, and the splash is two. So it misses - but
      // only at range. A mortar firing at ten cells leads its target by less than the splash and
      // connects, which is correct and is why this fires from near its maximum.
      Field field;
      Assert::IsTrue(Level(field.sim, 36, 40, 70, 40));
      const Frontier::ObjectId mortar = Spawn(field.sim, 0, 1, Middle(40), Middle(60));
      // The runner starts on the first COLUMN of a cluster, which is where the graph puts the
      // doorways between one cluster and the one above it (Sim/ClusterGraph.cpp), so its route
      // north is a straight line. Half a cluster to the side it would detour ten cells west to
      // reach a doorway - toward the mortar - and a shell fired over fourteen cells leads its
      // target by less than the splash and connects. That is correct behaviour and it is not what
      // this test is about, so the run is straight and the assertion below says so.
      const std::uint32_t column = 4 * Frontier::CLUSTER_CELLS;
      const Frontier::ObjectId runner = Spawn(field.sim, 1, 0, Middle(column), Middle(60));
      field.sim.Objects().FindDevice(runner)->fire = Frontier::FireStance::HoldFire;

      const std::int32_t started = DeviceAt(field.sim, runner)->hitPoints;
      // Across the line of fire rather than along it, so that it stays about twenty-four cells out
      // and goes on being shot at instead of walking out of range after one shell.
      Frontier::Order order{};
      order.seat = 1;
      order.kind = Frontier::OrderKind::Move;
      order.operands[0] = static_cast<std::int32_t>(runner.value);
      order.operands[1] = Middle(column);
      order.operands[2] = Middle(100);
      field.sim.Submit(order);
      // Up to speed before anything can see it: a scout still turning is a stationary target, and
      // what is under test is the shell that leads a moving one.
      for (std::uint32_t tick = 0; tick < 30; ++tick)
      {
        field.sim.Advance();
      }
      Assert::AreEqual(started, DeviceAt(field.sim, runner)->hitPoints, L"nothing has seen it yet");
      (void)Spawn(field.sim, 0, 2, Middle(column - 4), Middle(72)); // The spotter, beside its path

      bool fired = false;
      for (std::uint32_t tick = 0; tick < 300; ++tick)
      {
        field.sim.Advance();
        fired = fired || field.sim.Objects().Count(Frontier::ObjectKind::Projectile) > 0;
      }
      Assert::IsTrue(fired, L"shells were in the air");
      Assert::IsTrue(DeviceAt(field.sim, runner) != nullptr, L"it is still alive");
      Assert::AreEqual(started, DeviceAt(field.sim, runner)->hitPoints, L"and untouched: it walked out from under every shell");
      Assert::IsTrue(std::abs(DeviceAt(field.sim, runner)->x - Middle(column)) < CELL,
                     L"and it really did run straight, which is what the arithmetic above assumes");
      Assert::IsTrue(DeviceAt(field.sim, runner)->z > Middle(70), L"and covered the ground");
      Assert::IsTrue(DeviceAt(field.sim, mortar) != nullptr);
    }
  }

  TEST_METHOD(ATowerFiresWithNoStancesAndTakesTheNearestDeviceFirst)
  {
    Field field;
    const Frontier::ObjectId tower = Standing(field.sim, 0, Row::Tower, 60, 60);
    // A structure of theirs nearer than a device of theirs: devices come first all the same,
    // because a thing that shoots back is the thing that has to stop (Sim/Targeting.h).
    (void)Standing(field.sim, 1, Row::CommandPost, 62, 60);
    const Frontier::ObjectId device = Spawn(field.sim, 1, 2, Middle(66), Middle(60));
    field.sim.Advance(); // Stage 7 stamps the tower's sight before stage 8 reads it
    field.sim.Advance();

    const std::int32_t started = DeviceAt(field.sim, device)->hitPoints;
    for (std::uint32_t tick = 0; tick < 120; ++tick)
    {
      field.sim.Advance();
    }
    Assert::IsTrue(field.sim.Objects().FindStructure(tower) != nullptr, L"the tower stands");
    Assert::IsTrue(DeviceAt(field.sim, device) == nullptr || DeviceAt(field.sim, device)->hitPoints < started,
                   L"and shot the device rather than the nearer building");
  }

  TEST_METHOD(RetreatBreaksOffOnlyWhenARepairPointIsWithinSixtyCells)
  {
    Field field;
    const Frontier::ObjectId hurt = Spawn(field.sim, 0, 0, Middle(60), Middle(60));
    field.sim.Objects().FindDevice(hurt)->retreat = Frontier::RetreatStance::AtHalf;
    field.sim.Objects().FindDevice(hurt)->hitPoints = 40; // Under half of a scout's hundred

    // Nothing to go to: it holds and fights, which GameDesign.md §8 asks for by name.
    field.sim.Advance();
    Assert::IsTrue(DeviceAt(field.sim, hurt)->primaryOrder != Frontier::PrimaryOrder::ReturnToRepair, L"nowhere to go, so it stays");

    // A bay beyond sixty cells is still nowhere to go.
    (void)Standing(field.sim, 0, Row::RepairBay, 125, 125);
    field.sim.Objects().FindDevice(hurt)->hitPoints = 40;
    field.sim.Advance();
    Assert::IsTrue(DeviceAt(field.sim, hurt)->primaryOrder != Frontier::PrimaryOrder::ReturnToRepair, L"sixty cells is the rule");

    // One within sixty is, and the device breaks off toward it.
    const Frontier::ObjectId bay = Standing(field.sim, 0, Row::RepairBay, 70, 60);
    field.sim.Objects().FindDevice(hurt)->hitPoints = 40;
    field.sim.Advance();
    Assert::IsTrue(DeviceAt(field.sim, hurt)->primaryOrder == Frontier::PrimaryOrder::ReturnToRepair, L"and now it breaks off");
    Assert::IsTrue(DeviceAt(field.sim, hurt)->destinationX > Middle(60), L"toward the bay");
    Assert::IsTrue(field.sim.Objects().FindStructure(bay) != nullptr);

    // A device whose stance says never does not, however badly hurt.
    const Frontier::ObjectId stubborn = Spawn(field.sim, 0, 0, Middle(62), Middle(62));
    field.sim.Objects().FindDevice(stubborn)->hitPoints = 5;
    field.sim.Advance();
    Assert::IsTrue(DeviceAt(field.sim, stubborn) == nullptr ||
                     DeviceAt(field.sim, stubborn)->primaryOrder != Frontier::PrimaryOrder::ReturnToRepair,
                   L"never is never");
  }

  TEST_METHOD(TwoSimsGivenTheSameFightResolveItIdentically)
  {
    Field one;
    Field other;
    for (std::uint32_t index = 0; index < 4; ++index)
    {
      const Frontier::ObjectId mine = Spawn(one.sim, 0, 0, Middle(58 + index), Middle(60));
      Assert::IsTrue(Spawn(other.sim, 0, 0, Middle(58 + index), Middle(60)) == mine);
      const Frontier::ObjectId theirs = Spawn(one.sim, 1, 0, Middle(66 + index), Middle(61));
      Assert::IsTrue(Spawn(other.sim, 1, 0, Middle(66 + index), Middle(61)) == theirs);
    }
    for (std::uint32_t tick = 0; tick < 400; ++tick)
    {
      one.sim.Advance();
      other.sim.Advance();
      Assert::AreEqual(one.sim.Hash(), other.sim.Hash());
    }
    Logger::WriteMessage((L"measured: of eight scouts, " + std::to_wstring(one.sim.Objects().Count(Frontier::ObjectKind::Device)) +
                          L" were left after 400 ticks")
                           .c_str());
    Assert::IsTrue(one.sim.Objects().Count(Frontier::ObjectKind::Device) < 8, L"and they actually fought");
  }
};

} // namespace SimTests
