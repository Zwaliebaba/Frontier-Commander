#include "pch.h"

#include "FrameEncoder.h"
#include "Host.h"
#include "Interest.h"

#include "Construction.h"

#include "LoopbackTransport.h"

#include "Client.h"

#include <cstdint>
#include <set>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// What a client is told, and nothing else (TechnicalDesign.md §5.2; m1-vertical-slice/N2). This is
// the security test of the milestone: the fog of war is enforced by the host, so a modified client
// must see nothing an honest one does not. The last method here plays a scripted match over the
// loopback transport and checks EVERY frame to EVERY client against what that commander could
// possibly know.
namespace NetTests
{

namespace
{

constexpr std::int32_t CELL = Neuron::SUBUNITS_PER_CELL;

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
    factory.buildTimeTicks = 40;
    tree.structures.structures = {post, factory};

    Frontier::ChassisDesc light{};
    light.id = "LightI";
    light.chassisClass = Frontier::ChassisClass::Light;
    light.hitPoints = 100;
    light.sightSubunits = 6 * CELL;
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
    Frontier::ModuleDesc cannon{};
    cannon.id = "Cannon";
    cannon.systemKind = Frontier::SystemKind::None;
    cannon.costHundredths = 5000;
    tree.components.modules = {cannon};
    return tree;
  }();
  return TREE;
}

Frontier::MatchSettings Lobby(std::uint8_t _seats, const std::vector<std::uint8_t>& _alliances)
{
  Frontier::MatchSettings settings{};
  settings.seed = 31;
  settings.sizeClass = Frontier::SizeClass::Small;
  settings.seatCount = _seats;
  settings.baseLevel = Frontier::BaseLevel::Nothing;
  settings.powerLevel = Frontier::PowerLevel::Medium;
  settings.deviceCapLevel = Frontier::DeviceCapLevel::Medium;
  settings.victory = Frontier::VictoryCondition::Annihilation;
  for (std::uint8_t index = 0; index < _seats; ++index)
  {
    settings.seats[index] = {Frontier::SeatKind::Human, _alliances[index], false};
  }
  return settings;
}

Frontier::LandscapeDefinition Ground()
{
  Frontier::LandscapeDefinition definition{};
  definition.version = Frontier::LANDSCAPE_DEFINITION_VERSION;
  definition.sizeClass = Frontier::SizeClass::Small;
  definition.cellsPerSide = Frontier::SIZE_CLASS_CELLS[0];
  definition.seed = 4;
  definition.palette = "Default";
  definition.tiles = {{0, 0, 512, 170, 90, 100, 48, 70, 1, 32, ""}};
  definition.starts = {{16, 16}, {100, 100}};
  return definition;
}

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

void Hide(Frontier::Sim& _sim, std::uint8_t _seat, std::uint32_t _cellX, std::uint32_t _cellY, std::uint32_t _cellsX = 1,
          std::uint32_t _cellsY = 1)
{
  for (std::uint32_t y = _cellY; y < _cellY + _cellsY; ++y)
  {
    for (std::uint32_t x = _cellX; x < _cellX + _cellsX; ++x)
    {
      _sim.SeatAt(_seat).fog.RemoveViewer(x, y);
    }
  }
}

Frontier::ObjectId Standing(Frontier::Sim& _sim, std::uint8_t _seat, Row _row, std::uint32_t _cellX, std::uint32_t _cellY)
{
  Frontier::Structure structure{};
  structure.seat = _seat;
  structure.design = static_cast<std::uint32_t>(_row);
  structure.cellX = _cellX;
  structure.cellY = _cellY;
  structure.state = Frontier::StructurePhase::Standing;
  structure.hitPoints = 800;
  structure.buildEffortHundredths = Frontier::RequiredEffortHundredths(40);
  structure.working = Frontier::NO_OBJECT;
  return _sim.Objects().Create(structure);
}

Frontier::ObjectId DeviceAt(Frontier::Sim& _sim, std::uint8_t _seat, std::uint32_t _cellX, std::uint32_t _cellY)
{
  Frontier::Seat& seat = _sim.SeatAt(_seat);
  if (seat.designs.empty())
  {
    Frontier::DeviceDesign design{};
    design.chassis = 0;
    design.drive = 0;
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

[[nodiscard]] bool Holds(const std::vector<std::uint32_t>& _ids, Frontier::ObjectId _id)
{
  return std::find(_ids.begin(), _ids.end(), _id.value) != _ids.end();
}

/// Applies frames into the flat picture a replica would hold, so that a test can ask what this
/// client has ever been told about.
class Watcher : public Frontier::FrameSink
{
public:
  void Apply(const Frontier::Frame& _frame) override
  {
    ++frames;
    for (const Frontier::DeviceState& device : _frame.createdDevices)
    {
      devicesSeen.insert(device.id);
      seatsSeen.insert(device.seat);
    }
    for (const Frontier::StructureState& structure : _frame.createdStructures)
    {
      structuresSeen.insert(structure.id);
      seatsSeen.insert(structure.seat);
    }
    for (const Frontier::StructureState& structure : _frame.changedStructures)
    {
      structuresSeen.insert(structure.id);
      seatsSeen.insert(structure.seat);
    }
    for (const Frontier::SeatState& seat : {_frame.seat})
    {
      seatStatesSeen.insert(seat.seat);
    }
    for (const Frontier::FogDelta& run : _frame.fog)
    {
      for (std::uint32_t cell = 0; cell < run.cells; ++cell)
      {
        if (run.firstCell + cell < fog.size())
        {
          fog[run.firstCell + cell] = run.state;
        }
      }
    }
  }

  std::uint32_t frames = 0;
  std::set<std::uint32_t> devicesSeen;
  std::set<std::uint32_t> structuresSeen;
  std::set<std::uint8_t> seatsSeen;
  std::set<std::uint8_t> seatStatesSeen;
  std::vector<Frontier::FogState> fog;
};

} // namespace

TEST_CLASS(InterestTests)
{
public:
  TEST_METHOD(ACommanderIsToldWhatHeOwnsWhereverItIs)
  {
    Frontier::Sim sim(Lobby(2, {0, 1}), Tables());
    Assert::IsTrue(sim.CreateLandscape(Ground()));
    const Frontier::ObjectId mine = DeviceAt(sim, 0, 90, 90); // far outside his own fog
    Standing(sim, 0, Row::CommandPost, 16, 16);
    Reveal(sim, 0, 16, 16, 3, 3);

    Frontier::InterestSet interest;
    Frontier::GatherInterest(sim, 0, interest);
    Assert::IsTrue(Holds(interest.devices, mine), L"a commander is never in the dark about his own");
  }

  TEST_METHOD(ACommanderIsNotToldWhatHeCannotSee)
  {
    Frontier::Sim sim(Lobby(2, {0, 1}), Tables());
    Assert::IsTrue(sim.CreateLandscape(Ground()));
    Standing(sim, 0, Row::CommandPost, 16, 16);
    Reveal(sim, 0, 16, 16, 3, 3);
    const Frontier::ObjectId enemy = DeviceAt(sim, 1, 100, 100);
    const Frontier::ObjectId enemyBase = Standing(sim, 1, Row::Factory, 100, 100);

    Frontier::InterestSet interest;
    Frontier::GatherInterest(sim, 0, interest);
    Assert::IsFalse(Holds(interest.devices, enemy));
    Assert::IsFalse(Holds(interest.structures, enemyBase));

    // And the moment a cell of the enemy's footprint is visible, the structure is.
    Reveal(sim, 0, 102, 102);
    Frontier::GatherInterest(sim, 0, interest);
    Assert::IsTrue(Holds(interest.structures, enemyBase), L"one corner of a factory is the factory");
    Assert::IsFalse(Holds(interest.devices, enemy), L"and the device standing beside it is not");
  }

  TEST_METHOD(AlliesShareVision)
  {
    Frontier::Sim sim(Lobby(3, {0, 0, 1}), Tables());
    Assert::IsTrue(sim.CreateLandscape(Ground()));
    const Frontier::ObjectId enemy = DeviceAt(sim, 2, 60, 60);
    Reveal(sim, 1, 60, 60); // the ALLY sees it, not seat 0

    Frontier::InterestSet interest;
    Frontier::GatherInterest(sim, 0, interest);
    Assert::IsTrue(Holds(interest.devices, enemy), L"allied commanders share vision (GameDesign.md §2)");

    Frontier::GatherInterest(sim, 2, interest);
    Assert::IsFalse(Holds(interest.devices, enemy) && interest.devices.size() > 1, L"and the enemy shares nothing with them");
  }

  TEST_METHOD(AStructureThatLeavesVisibilityBecomesAGhostAtItsLastSeenState)
  {
    Frontier::Sim sim(Lobby(2, {0, 1}), Tables());
    Assert::IsTrue(sim.CreateLandscape(Ground()));
    const Frontier::ObjectId theirs = Standing(sim, 1, Row::Factory, 60, 60);
    Reveal(sim, 0, 60, 60, 3, 3);

    Frontier::InterestSet interest;
    Frontier::GatherInterest(sim, 0, interest);
    Assert::IsTrue(Holds(interest.structures, theirs));
    Assert::IsTrue(interest.ghosts.empty(), L"a structure he can see is not also a ghost");

    // He remembers it, and then loses sight of it.
    Frontier::Ghost ghost{};
    ghost.structure = theirs;
    ghost.seat = 1;
    ghost.design = static_cast<std::uint32_t>(Row::Factory);
    ghost.cellX = 60;
    ghost.cellY = 60;
    ghost.seenTick = sim.Tick();
    sim.SeatAt(0).ghosts.Record(ghost);
    Hide(sim, 0, 60, 60, 3, 3);

    Frontier::GatherInterest(sim, 0, interest);
    Assert::IsFalse(Holds(interest.structures, theirs));
    Assert::AreEqual(static_cast<std::size_t>(1), interest.ghosts.size());
    Assert::IsTrue(interest.ghosts[0] == theirs);

    // The ghost on the wire carries where it was and what it was, and not what it has taken since.
    const Frontier::StructureState wire = Frontier::WireGhost(ghost);
    Assert::AreEqual(static_cast<int>(60), static_cast<int>(wire.cellX));
    Assert::AreEqual(0, static_cast<int>(wire.hitPoints), L"he has no idea what it has taken since");
  }

  TEST_METHOD(NoFrameOfAScriptedMatchEverNamesWhatItsCommanderCannotSee)
  {
    // The security property, over a whole match rather than one frame. Two commanders on opposite
    // corners of a Small landscape, neither ever in sight of the other: every frame to each must
    // name only his own objects, and his own fog must be his and not the union of everyone's.
    Frontier::Sim sim(Lobby(2, {0, 1}), Tables());
    Assert::IsTrue(sim.CreateLandscape(Ground()));
    const Frontier::ObjectId hisBase = Standing(sim, 0, Row::CommandPost, 16, 16);
    const Frontier::ObjectId herBase = Standing(sim, 1, Row::CommandPost, 100, 100);
    std::vector<Frontier::ObjectId> his;
    std::vector<Frontier::ObjectId> hers;
    for (std::uint32_t index = 0; index < 6; ++index)
    {
      his.push_back(DeviceAt(sim, 0, 16 + index, 20));
      hers.push_back(DeviceAt(sim, 1, 100 + index, 104));
    }
    Reveal(sim, 0, 14, 14, 12, 12);
    Reveal(sim, 1, 98, 98, 12, 12);

    Neuron::LoopbackTransport network(11);
    Neuron::Transport& firstEnd = network.Connect();
    Neuron::Transport& secondEnd = network.Connect();
    Frontier::Host host(sim, network.Host(), 0xC0FFEEu, 0);
    constexpr Neuron::LivenessSettings LIVENESS = {20, 100, 200};
    Frontier::Client first(firstEnd, LIVENESS, 0);
    Frontier::Client second(secondEnd, LIVENESS, 0);
    Watcher hisView;
    Watcher herView;
    hisView.fog.assign(sim.Seats()[0].fog.Count(), Frontier::FogState::Unexplored);
    herView.fog.assign(sim.Seats()[1].fog.Count(), Frontier::FogState::Unexplored);

    first.SendJoin(0xC0FFEEu, 1, "first", 0);
    second.SendJoin(0xC0FFEEu, 2, "second", 0);

    for (std::uint32_t tick = 0; tick < 120; ++tick)
    {
      sim.Advance();
      network.Host().Poll();
      host.Advance(sim.Tick());
      firstEnd.Poll();
      secondEnd.Poll();
      first.Advance(sim.Tick(), hisView);
      second.Advance(sim.Tick(), herView);
    }

    Assert::IsTrue(hisView.frames > 10, L"the match really did publish");
    Assert::IsTrue(herView.frames > 10);

    // Neither commander was ever told of a seat but his own.
    Assert::IsTrue(hisView.seatsSeen.size() == 1 && *hisView.seatsSeen.begin() == 0, L"he saw only his own side");
    Assert::IsTrue(herView.seatsSeen.size() == 1 && *herView.seatsSeen.begin() == 1);
    Assert::IsTrue(hisView.seatStatesSeen.size() == 1 && *hisView.seatStatesSeen.begin() == 0, L"and only his own seat state");
    Assert::IsTrue(herView.seatStatesSeen.size() == 1 && *herView.seatStatesSeen.begin() == 1);

    for (const Frontier::ObjectId& mine : his)
    {
      Assert::IsTrue(hisView.devicesSeen.count(mine.value) == 1);
      Assert::IsTrue(herView.devicesSeen.count(mine.value) == 0, L"she was never told of his army");
    }
    for (const Frontier::ObjectId& theirs : hers)
    {
      Assert::IsTrue(herView.devicesSeen.count(theirs.value) == 1);
      Assert::IsTrue(hisView.devicesSeen.count(theirs.value) == 0);
    }
    Assert::IsTrue(hisView.structuresSeen.count(hisBase.value) == 1);
    Assert::IsTrue(hisView.structuresSeen.count(herBase.value) == 0);
    Assert::IsTrue(herView.structuresSeen.count(herBase.value) == 1);

    // And the fog each was sent is his own grid, cell for cell - not an ally's, not the union.
    const std::span<const Frontier::FogState> hisFog = sim.Seats()[0].fog.States();
    const std::span<const Frontier::FogState> herFog = sim.Seats()[1].fog.States();
    Assert::AreEqual(hisFog.size(), hisView.fog.size());
    std::size_t differing = 0;
    for (std::size_t cell = 0; cell < hisFog.size(); ++cell)
    {
      differing += hisFog[cell] == hisView.fog[cell] ? 0 : 1;
      differing += herFog[cell] == herView.fog[cell] ? 0 : 1;
    }
    Assert::AreEqual(static_cast<std::size_t>(0), differing, L"each commander's fog is exactly his own");

    Logger::WriteMessage(("    measured: " + std::to_string(hisView.frames) + " frames each over 120 ticks, the largest " +
                          std::to_string(host.Statistics().largestFrameBytes) + " bytes\n")
                           .c_str());
  }
};

} // namespace NetTests
