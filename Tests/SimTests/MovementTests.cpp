#include "pch.h"

#include "ClusterGraph.h"
#include "Movement.h"
#include "Production.h"
#include "Sim.h"
#include "Snapshot.h"
#include "Steering.h"

#include "FixedPoint.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// Movement and steering (GameDesign.md §2 and §6; m1-vertical-slice/S8). The crossing time is the
// number the whole design is sized by - "a light on wheels with a machine gun is 104 world units a
// second" is what makes a Small landscape a two-minute drive and a Frontier one a quarter of an
// hour - so it is measured here against the shipped tables rather than assumed. The rest is the
// rules the acceptance names: what slope costs, what refuses it, what two devices meeting do, and
// that two hosts do all of it identically.
namespace SimTests
{

namespace
{

constexpr std::int32_t CELL = Neuron::SUBUNITS_PER_CELL;

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
  MachineGun = 0
};

/// The shipped rows the design works through: a light chassis, wheels and tracks, a machine gun.
/// Written out rather than loaded so that the suite needs no GameData beside it; ContentTests pins
/// the shipped tables against the same numbers.
const Frontier::ContentTree& Tables()
{
  static const Frontier::ContentTree TREE = []
  {
    Frontier::ContentTree tree;
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
    tree.components.modules = {gun};
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

/// Replaces every sample of the landscape with what _height says for its column, which is how a
/// test says exactly what ground its rule is about. A profile of one height is flat ground.
[[nodiscard]] bool Sculpt(Frontier::Sim& _sim, const std::vector<std::int16_t>& _profile)
{
  const std::uint32_t side = _sim.Terrain().SamplesPerSide();
  Frontier::HeightDelta delta{};
  delta.x = 0;
  delta.y = 0;
  delta.width = side;
  delta.height = side;
  delta.heights.resize(static_cast<std::size_t>(side) * side);
  for (std::uint32_t y = 0; y < side; ++y)
  {
    for (std::uint32_t x = 0; x < side; ++x)
    {
      delta.heights[static_cast<std::size_t>(y) * side + x] = _profile[std::min<std::size_t>(x, _profile.size() - 1)];
    }
  }
  return _sim.FlattenTerrain(delta);
}

[[nodiscard]] bool Flatten(Frontier::Sim& _sim, std::int16_t _height)
{
  return Sculpt(_sim, {_height});
}

/// Flat ground, then a ridge of _stepPerSample running the whole height of the landscape: the
/// cells it crosses are at _stepPerSample * 100 / 16 percent, which is what a drive's maximum
/// slope is measured against (Sim/Landscape.cpp derives a cell's slope that way).
[[nodiscard]] bool Ridge(Frontier::Sim& _sim, std::uint32_t _atSample, std::int16_t _stepPerSample, std::uint32_t _samples)
{
  std::vector<std::int16_t> profile(_sim.Terrain().SamplesPerSide(), 10);
  for (std::uint32_t x = _atSample; x < profile.size(); ++x)
  {
    const auto climbed = static_cast<std::int16_t>(std::min<std::uint32_t>(x - _atSample, _samples) * _stepPerSample);
    profile[x] = static_cast<std::int16_t>(10 + climbed);
  }
  return Sculpt(_sim, profile);
}

[[nodiscard]] std::int32_t Middle(std::uint32_t _cell)
{
  return static_cast<std::int32_t>(_cell) * CELL + CELL / 2;
}

Frontier::ObjectId Spawn(Frontier::Sim& _sim, std::uint8_t _seat, std::uint32_t _design, std::int32_t _x, std::int32_t _z)
{
  Frontier::Device device{};
  device.seat = _seat;
  device.design = _design;
  device.x = _x;
  device.z = _z;
  device.y = Frontier::GroundHeightSubunits(_sim.Terrain(), _x, _z);
  device.hitPoints = 100;
  device.primaryOrder = Frontier::PrimaryOrder::Stop;
  device.target = Frontier::NO_OBJECT;
  device.destinationX = _x;
  device.destinationZ = _z;
  device.anchorX = _x;
  device.anchorZ = _z;
  return _sim.Objects().Create(device);
}

Frontier::Order Ordered(Frontier::OrderKind _kind, std::uint8_t _seat, Frontier::ObjectId _device, std::int32_t _x, std::int32_t _z)
{
  Frontier::Order order{};
  order.tick = 0; // Submit gives an order for a tick already advanced the next one.
  order.seat = _seat;
  order.kind = _kind;
  order.operands[0] = static_cast<std::int32_t>(_device.value);
  order.operands[1] = _x;
  order.operands[2] = _z;
  return order;
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

const Frontier::Device& DeviceAt(const Frontier::Sim& _sim, Frontier::ObjectId _id)
{
  const Frontier::Device* device = _sim.Objects().FindDevice(_id);
  if (device == nullptr)
  {
    Assert::Fail(L"the device is gone"); // noreturn, which is what the reference below relies on
  }
  return *device;
}

/// A match on a Small landscape with a wheeled and a tracked design saved for seat 0.
struct Field
{
  Frontier::Sim sim{TwoSeats(), Tables()};

  explicit Field(std::int16_t _height = 10)
  {
    Assert::IsTrue(sim.CreateLandscape(Ground()));
    Assert::IsTrue(Flatten(sim, _height));
    Assert::IsTrue(Frontier::SaveDesign(sim, 0, 0, Designed(Chassis::Light, Drive::Wheels, Module::MachineGun)));
    Assert::IsTrue(Frontier::SaveDesign(sim, 0, 1, Designed(Chassis::Light, Drive::Tracks, Module::MachineGun)));
    Assert::IsTrue(Frontier::SaveDesign(sim, 1, 0, Designed(Chassis::Light, Drive::Wheels, Module::MachineGun)));
  }
};

/// Advances until the device stops carrying a movement order, or until _limit; returns the tick it
/// finished on, or _limit when it never did.
std::uint32_t RunUntilStopped(Frontier::Sim& _sim, Frontier::ObjectId _device, std::uint32_t _limit)
{
  for (std::uint32_t tick = 0; tick < _limit; ++tick)
  {
    _sim.Advance();
    if (DeviceAt(_sim, _device).primaryOrder == Frontier::PrimaryOrder::Stop)
    {
      return _sim.Tick();
    }
  }
  return _limit;
}

Frontier::Sim Reload(const Frontier::Sim& _sim)
{
  std::optional<Frontier::Sim> reloaded = Frontier::Snapshot::Read(Frontier::Snapshot::Write(_sim), Tables());
  if (!reloaded.has_value())
  {
    Assert::Fail(L"the snapshot did not read back"); // noreturn
  }
  return *reloaded;
}

} // namespace

TEST_CLASS(MovementTests)
{
public:
  TEST_METHOD(TheTerrainFactorFallsToHalfAtTheDrivesOwnLimitAndStopsBeyondIt)
  {
    // The owner's answer to OpenQuestions.md Q23 (2026-09-18), which is the whole of the rule.
    Assert::AreEqual(100, Frontier::TerrainFactorPercent(0, 25));
    Assert::AreEqual(50, Frontier::TerrainFactorPercent(25, 25));
    Assert::AreEqual(0, Frontier::TerrainFactorPercent(26, 25));
    Assert::AreEqual(50, Frontier::TerrainFactorPercent(40, 40));
    Assert::AreEqual(0, Frontier::TerrainFactorPercent(41, 40));
    // And what scaling to the drive's own limit buys: on ground both can cross, the drive that
    // could climb more keeps more. This is the difference between a max-slope column that buys
    // reach only and one that buys handling.
    Assert::AreEqual(60, Frontier::TerrainFactorPercent(20, 25), L"wheels on a fifth");
    Assert::AreEqual(75, Frontier::TerrainFactorPercent(20, 40), L"tracks on the same fifth");
    // A drive row that states no maximum climbs the flat and nothing else, rather than dividing.
    Assert::AreEqual(100, Frontier::TerrainFactorPercent(0, 0));
    Assert::AreEqual(0, Frontier::TerrainFactorPercent(1, 0));
  }

  TEST_METHOD(ALightOnWheelsCrossesAFlatSmallLandscapeInAboutFifteenHundredTicks)
  {
    Field field;
    const std::uint32_t last = field.sim.Terrain().CellsPerSide() - 1;
    // Along the first row of a cluster row. The crossing between two clusters is the first pair of
    // passable cells the graph finds across their border and that is the lowest row of the two
    // (Sim/ClusterGraph.cpp), so a device walking along that row walks a straight line and what is
    // being measured here is the speed rather than where the abstraction puts its doorways. Half a
    // cluster higher the same drive takes 1,804 ticks, all of the difference being two diagonals
    // to the doorway and back, which is S7's abstraction and is measured by S7's own suite.
    const std::uint32_t row = 4 * Frontier::CLUSTER_CELLS;
    const Frontier::ObjectId scout = Spawn(field.sim, 0, 0, Middle(0), Middle(row));
    field.sim.Submit(Ordered(Frontier::OrderKind::Move, 0, scout, Middle(last), Middle(row)));

    const std::uint32_t arrived = RunUntilStopped(field.sim, scout, 4000);
    const std::int64_t crossed = static_cast<std::int64_t>(Middle(last)) - Middle(0);
    Logger::WriteMessage((L"measured: a light on wheels crossed " + std::to_wstring(crossed / Neuron::SUBUNITS_PER_WORLD_UNIT) +
                          L" world units in " + std::to_wstring(arrived) + L" ticks")
                           .c_str());
    // GameDesign.md §6 sizes the scout at 1,331 subunits a tick, so 8,192 world units is 1,575
    // ticks driven flat out. What the route, the turn at the start and the arrival radius add is
    // what this window allows; a change that made it a fifth slower is the one worth catching.
    Assert::IsTrue(arrived > 1540, L"the crossing cannot be faster than the speed allows");
    Assert::IsTrue(arrived < 1620, L"the crossing is the 1,575 ticks the design is sized by");
    Assert::IsTrue(DeviceAt(field.sim, scout).x > Middle(last) - Frontier::ARRIVAL_SUBUNITS);
  }

  TEST_METHOD(ADriveRefusesGroundSteeperThanItsOwnLimit)
  {
    // A ridge of five units a sample is 31 percent and one of seven is 43: the two nearest the 30
    // and 45 the acceptance names that a heightfield spaced every 16 world units can express.
    const std::uint32_t at = 200; // Sample 200 is cell 50, well clear of both ends
    const std::int32_t start = Middle(10);
    const std::int32_t beyond = Middle(80);
    const std::int32_t row = Middle(64);

    {
      Field field;
      Assert::IsTrue(Ridge(field.sim, at, 5, 8));
      Assert::AreEqual(31, static_cast<int>(field.sim.Terrain().CellAt(51, 64).slopePercent), L"the ridge is 31 percent");
      const Frontier::ObjectId wheeled = Spawn(field.sim, 0, 0, start, row);
      const Frontier::ObjectId tracked = Spawn(field.sim, 0, 1, start, Middle(70));
      field.sim.Submit(Ordered(Frontier::OrderKind::Move, 0, wheeled, beyond, row));
      field.sim.Submit(Ordered(Frontier::OrderKind::Move, 0, tracked, beyond, Middle(70)));
      for (std::uint32_t tick = 0; tick < 2500; ++tick)
      {
        field.sim.Advance();
      }
      Assert::IsTrue(DeviceAt(field.sim, wheeled).x < static_cast<std::int32_t>(at) * 16 * Neuron::SUBUNITS_PER_WORLD_UNIT,
                     L"wheels stop at the foot of a 31 percent ridge");
      Assert::IsTrue(DeviceAt(field.sim, tracked).x > beyond - Frontier::ARRIVAL_SUBUNITS, L"tracks climb it");
    }
    {
      Field field;
      Assert::IsTrue(Ridge(field.sim, at, 7, 8));
      Assert::AreEqual(43, static_cast<int>(field.sim.Terrain().CellAt(51, 64).slopePercent), L"the ridge is 43 percent");
      const Frontier::ObjectId tracked = Spawn(field.sim, 0, 1, start, row);
      field.sim.Submit(Ordered(Frontier::OrderKind::Move, 0, tracked, beyond, row));
      for (std::uint32_t tick = 0; tick < 2500; ++tick)
      {
        field.sim.Advance();
      }
      Assert::IsTrue(DeviceAt(field.sim, tracked).x < static_cast<std::int32_t>(at) * 16 * Neuron::SUBUNITS_PER_WORLD_UNIT,
                     L"tracks stop at the foot of a 43 percent ridge");
      Assert::IsTrue(DeviceAt(field.sim, tracked).primaryOrder == Frontier::PrimaryOrder::Stop, L"and give up rather than grind");
    }
  }

  TEST_METHOD(TwoDevicesMeetingHeadOnPassInsteadOfBlocking)
  {
    Field field;
    const std::int32_t row = Middle(64);
    const Frontier::ObjectId west = Spawn(field.sim, 0, 0, Middle(40), row);
    const Frontier::ObjectId east = Spawn(field.sim, 1, 0, Middle(60), row);
    field.sim.Submit(Ordered(Frontier::OrderKind::Move, 0, west, Middle(60), row));
    field.sim.Submit(Ordered(Frontier::OrderKind::Move, 1, east, Middle(40), row));

    std::int32_t closest = row * 4;
    std::int32_t westOffset = 0;
    std::int32_t eastOffset = 0;
    for (std::uint32_t tick = 0; tick < 1200; ++tick)
    {
      field.sim.Advance();
      const Frontier::Device& one = DeviceAt(field.sim, west);
      const Frontier::Device& other = DeviceAt(field.sim, east);
      const auto apart = static_cast<std::int32_t>(Neuron::Length(one.x - other.x, one.z - other.z));
      if (apart < closest)
      {
        closest = apart;
        westOffset = one.z - row;
        eastOffset = other.z - row;
      }
    }
    Logger::WriteMessage((L"measured: they closed to " + std::to_wstring(closest) + L" subunits, one " + std::to_wstring(westOffset) +
                          L" off the line and the other " + std::to_wstring(eastOffset))
                           .c_str());
    Assert::IsTrue(westOffset != 0 || eastOffset != 0, L"steering moved them off the line they met on");
    Assert::IsTrue(static_cast<std::int64_t>(westOffset) * eastOffset <= 0, L"and to opposite sides of it");
    // Both walked past each other and arrived, which is what not blocking means.
    Assert::IsTrue(DeviceAt(field.sim, west).x > Middle(60) - Frontier::ARRIVAL_SUBUNITS, L"the westerly one arrived");
    Assert::IsTrue(DeviceAt(field.sim, east).x < Middle(40) + Frontier::ARRIVAL_SUBUNITS, L"and so did the easterly one");
  }

  TEST_METHOD(SeparationPushesApartAndBreaksATieByIdOnlyWhenTwoDevicesCoincide)
  {
    const Frontier::ObjectId first{1, Frontier::ObjectKind::Device};
    const Frontier::ObjectId second{2, Frontier::ObjectKind::Device};
    {
      // Apart along one axis: the two pushes are exactly opposite, so neither host's answer
      // depends on which of the two it looked at first.
      const std::vector<Frontier::SteeredDevice> devices = {{first, Middle(10), Middle(10)}, {second, Middle(10) + 1000, Middle(10)}};
      std::vector<Frontier::SeparationPush> pushes(devices.size());
      Frontier::Separate(devices, pushes);
      Assert::AreEqual(pushes[0].x, -pushes[1].x);
      Assert::AreEqual(pushes[0].z, -pushes[1].z);
      Assert::IsTrue(pushes[0].x < 0, L"the westerly one is pushed west");
      Assert::IsTrue(pushes[0].z != 0, L"and a quarter turn round, which is what lets them pass");
    }
    {
      // Exactly coincident: there is no direction between them, so the higher id steps and the
      // lower stands.
      const std::vector<Frontier::SteeredDevice> devices = {{first, Middle(10), Middle(10)}, {second, Middle(10), Middle(10)}};
      std::vector<Frontier::SeparationPush> pushes(devices.size());
      Frontier::Separate(devices, pushes);
      Assert::AreEqual(0, pushes[0].x);
      Assert::AreEqual(0, pushes[0].z);
      Assert::IsTrue(pushes[1].x != 0 || pushes[1].z != 0, L"the higher id moves");
    }
    {
      // Further apart than the radius: neither is touched.
      const std::vector<Frontier::SteeredDevice> devices = {{first, Middle(10), Middle(10)},
                                                            {second, Middle(10) + Frontier::SEPARATION_RADIUS_SUBUNITS, Middle(10)}};
      std::vector<Frontier::SeparationPush> pushes(devices.size());
      Frontier::Separate(devices, pushes);
      Assert::AreEqual(0, pushes[0].x);
      Assert::AreEqual(0, pushes[1].x);
    }
  }

  TEST_METHOD(MoveArrivesAndStopsAndStopClearsTheRoute)
  {
    Field field;
    const std::int32_t row = Middle(64);
    const Frontier::ObjectId scout = Spawn(field.sim, 0, 0, Middle(20), row);
    field.sim.Submit(Ordered(Frontier::OrderKind::Move, 0, scout, Middle(40), row));
    for (std::uint32_t tick = 0; tick < 20; ++tick)
    {
      field.sim.Advance();
    }
    Assert::IsTrue(DeviceAt(field.sim, scout).x > Middle(20), L"it set off");
    Assert::IsTrue(DeviceAt(field.sim, scout).pathIndex != Frontier::NO_PATH_INDEX, L"walking a route");

    field.sim.Submit(Ordered(Frontier::OrderKind::Stop, 0, scout, 0, 0));
    field.sim.Advance();
    Assert::IsTrue(DeviceAt(field.sim, scout).primaryOrder == Frontier::PrimaryOrder::Stop);
    Assert::IsTrue(DeviceAt(field.sim, scout).pathIndex == Frontier::NO_PATH_INDEX, L"Stop clears the route");
    Assert::IsTrue(field.sim.Planner().Result(scout) == nullptr, L"and withdraws the request");
    const std::int32_t stoppedAt = DeviceAt(field.sim, scout).x;
    field.sim.Advance();
    Assert::AreEqual(stoppedAt, DeviceAt(field.sim, scout).x, L"and it stays where it stopped");

    // And a Move that reaches its destination stops of its own accord.
    field.sim.Submit(Ordered(Frontier::OrderKind::Move, 0, scout, Middle(24), row));
    const std::uint32_t arrived = RunUntilStopped(field.sim, scout, 400);
    Assert::IsTrue(arrived < 400, L"the Move arrived");
    Assert::IsTrue(Neuron::Length(DeviceAt(field.sim, scout).x - Middle(24), DeviceAt(field.sim, scout).z - row) <=
                     static_cast<std::uint32_t>(Frontier::ARRIVAL_SUBUNITS),
                   L"within the arrival radius of where it was sent");
  }

  TEST_METHOD(APatrolTurnsRoundAtEachEndAndAGuardReturnsToItsPost)
  {
    {
      Field field;
      const std::int32_t row = Middle(64);
      const Frontier::ObjectId scout = Spawn(field.sim, 0, 0, Middle(20), row);
      field.sim.Submit(Ordered(Frontier::OrderKind::Patrol, 0, scout, Middle(28), row));
      bool turned = false;
      for (std::uint32_t tick = 0; tick < 1200 && !turned; ++tick)
      {
        field.sim.Advance();
        turned = DeviceAt(field.sim, scout).destinationX == Middle(20);
      }
      Assert::IsTrue(turned, L"it reached the far end and turned for the near one");
      Assert::IsTrue(DeviceAt(field.sim, scout).primaryOrder == Frontier::PrimaryOrder::Patrol, L"and is still patrolling");
      Assert::AreEqual(Middle(28), DeviceAt(field.sim, scout).anchorX, L"with the far end now the anchor");
    }
    {
      Field field;
      const std::int32_t row = Middle(64);
      const Frontier::ObjectId sentry = Spawn(field.sim, 0, 0, Middle(20), row);
      field.sim.Submit(Ordered(Frontier::OrderKind::Guard, 0, sentry, Middle(20), row));
      field.sim.Advance();
      Assert::IsTrue(DeviceAt(field.sim, sentry).primaryOrder == Frontier::PrimaryOrder::Guard);
      // Shoved off its post, it walks back and keeps the order.
      field.sim.Objects().FindDevice(sentry)->x = Middle(26);
      for (std::uint32_t tick = 0; tick < 600; ++tick)
      {
        field.sim.Advance();
      }
      Assert::IsTrue(DeviceAt(field.sim, sentry).primaryOrder == Frontier::PrimaryOrder::Guard, L"a Guard never becomes a Stop");
      Assert::IsTrue(Neuron::Length(DeviceAt(field.sim, sentry).x - Middle(20), DeviceAt(field.sim, sentry).z - row) <=
                       static_cast<std::uint32_t>(Frontier::ARRIVAL_SUBUNITS),
                     L"and it is back at its post");
    }
  }

  TEST_METHOD(ADeviceThatCannotMakeHeadwayAsksForANewRoute)
  {
    Field field;
    const std::int32_t row = Middle(64);
    const Frontier::ObjectId scout = Spawn(field.sim, 0, 0, Middle(20), row);
    field.sim.Submit(Ordered(Frontier::OrderKind::Move, 0, scout, Middle(60), row));
    for (std::uint32_t tick = 0; tick < 10; ++tick)
    {
      field.sim.Advance();
    }
    const std::uint32_t walking = DeviceAt(field.sim, scout).pathIndex;
    Assert::IsTrue(walking != Frontier::NO_PATH_INDEX);

    // A wall it cannot get round, put up in front of it after it set off: it presses against the
    // obstruction, makes no headway, and after STUCK_TICKS throws the route away.
    for (std::uint32_t y = 0; y < field.sim.Terrain().CellsPerSide(); ++y)
    {
      field.sim.SetObstruction(30, y, 255);
    }
    bool replanned = false;
    for (std::uint32_t tick = 0; tick < 4 * Frontier::STUCK_TICKS && !replanned; ++tick)
    {
      field.sim.Advance();
      replanned = DeviceAt(field.sim, scout).primaryOrder == Frontier::PrimaryOrder::Stop;
    }
    Assert::IsTrue(replanned, L"it asked again, was told there is no way through, and stopped");
    Assert::IsTrue(DeviceAt(field.sim, scout).x < Middle(30), L"on its own side of the wall");
  }

  TEST_METHOD(TwoSimsGivenTheSameOrdersMoveIdentically)
  {
    Field one;
    Field other;
    const std::int32_t row = Middle(64);
    std::vector<Frontier::ObjectId> devices;
    for (std::uint32_t index = 0; index < 8; ++index)
    {
      const std::int32_t x = Middle(20 + index);
      devices.push_back(Spawn(one.sim, 0, 0, x, row));
      Assert::IsTrue(Spawn(other.sim, 0, 0, x, row) == devices.back());
    }
    for (const Frontier::ObjectId device : devices)
    {
      // One destination for all eight, so that they crowd it and the separation pass is what two
      // hosts have to agree about rather than eight independent walks.
      one.sim.Submit(Ordered(Frontier::OrderKind::Move, 0, device, Middle(40), row));
      other.sim.Submit(Ordered(Frontier::OrderKind::Move, 0, device, Middle(40), row));
    }
    for (std::uint32_t tick = 0; tick < 500; ++tick)
    {
      one.sim.Advance();
      other.sim.Advance();
      Assert::AreEqual(one.sim.Hash(), other.sim.Hash());
    }
    Assert::IsTrue(DeviceAt(one.sim, devices[0]).x != Middle(20), L"and they actually moved");
  }

  TEST_METHOD(AReloadedMatchGoesOnWalkingTheSameRouteFromTheSameTick)
  {
    Field field;
    const std::int32_t row = Middle(64);
    const Frontier::ObjectId scout = Spawn(field.sim, 0, 0, Middle(20), row);
    field.sim.Submit(Ordered(Frontier::OrderKind::Move, 0, scout, Middle(60), row));
    // Reloaded on the tick the request was made and before it was answered, which is the case the
    // planner's own header calls out: the search is half done and must finish when it would have.
    field.sim.Advance();
    Frontier::Sim reloaded = Reload(field.sim);
    Assert::AreEqual(field.sim.ComputeHash(), reloaded.ComputeHash());
    for (std::uint32_t tick = 0; tick < 600; ++tick)
    {
      field.sim.Advance();
      reloaded.Advance();
      Assert::AreEqual(field.sim.Hash(), reloaded.Hash());
    }
    Assert::IsTrue(DeviceAt(field.sim, scout).x > Middle(20), L"and the walk is what was compared");

    // And again from the middle of the walk, with the route already found.
    Frontier::Sim again = Reload(field.sim);
    for (std::uint32_t tick = 0; tick < 600; ++tick)
    {
      field.sim.Advance();
      again.Advance();
      Assert::AreEqual(field.sim.Hash(), again.Hash());
    }
  }

  TEST_METHOD(TheHeightUnderADeviceIsBlendedBetweenTheSamples)
  {
    Field field;
    // A ramp of one unit a sample: halfway between two samples the ground is half a unit up, which
    // a per-cell height could not say and a device crossing it would step down instead of rolling.
    std::vector<std::int16_t> profile(field.sim.Terrain().SamplesPerSide());
    for (std::size_t x = 0; x < profile.size(); ++x)
    {
      profile[x] = static_cast<std::int16_t>(10 + x);
    }
    Assert::IsTrue(Sculpt(field.sim, profile));
    constexpr std::int32_t SPACING = 16 * Neuron::SUBUNITS_PER_WORLD_UNIT;
    Assert::AreEqual(10 * Neuron::SUBUNITS_PER_WORLD_UNIT, Frontier::GroundHeightSubunits(field.sim.Terrain(), 0, 0));
    Assert::AreEqual(11 * Neuron::SUBUNITS_PER_WORLD_UNIT, Frontier::GroundHeightSubunits(field.sim.Terrain(), SPACING, 0));
    Assert::AreEqual(10 * Neuron::SUBUNITS_PER_WORLD_UNIT + Neuron::SUBUNITS_PER_WORLD_UNIT / 2,
                     Frontier::GroundHeightSubunits(field.sim.Terrain(), SPACING / 2, 0), L"halfway up the ramp");
    // Off the landscape reads the nearest edge rather than falling off it.
    Assert::AreEqual(10 * Neuron::SUBUNITS_PER_WORLD_UNIT, Frontier::GroundHeightSubunits(field.sim.Terrain(), -CELL, -CELL));
  }
};

} // namespace SimTests
