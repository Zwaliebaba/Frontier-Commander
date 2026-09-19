#include "pch.h"

#include "Replica.h"

#include "Host.h"
#include "Interest.h"

#include "LoopbackTransport.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// The claim of TechnicalDesign.md §10, made against the real thing (m1-vertical-slice/R1):
// "applying a host's frames, with and without loss, produces a replica equal to the host's interest
// set at every acked frame".
//
// A REAL Sim, N2's Host AND N3's Client OVER THE LOOPBACK TRANSPORT. Nothing here is a stub or a
// recorded stream. The comparison is against Net's own encoders rather than against a second copy
// of what the replica did, so a bug shared by the encoder and the replica cannot cancel out: what
// is asserted is that the client holds what the HOST WOULD SEND IF IT SENT EVERYTHING NOW.
namespace ReplicaTests
{

namespace
{

constexpr std::int32_t CELL = Neuron::SUBUNITS_PER_CELL;
constexpr Neuron::LivenessSettings LIVENESS = {20, 100, 200};
constexpr std::uint64_t CONTENT = 0xFEEDFACEu;

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
    tree.structures.structures = {post};

    Frontier::ChassisDesc light{};
    light.id = "LightI";
    light.chassisClass = Frontier::ChassisClass::Light;
    light.hitPoints = 100;
    light.sightSubunits = 6 * CELL;
    light.costHundredths = 6000;
    light.mounts = 1;
    // A SPEED, unlike the fixture NetTests uses. Nothing there ever walks, so a chassis at zero
    // subunits a tick was never noticed; the walking test below needs a device that can actually go.
    light.baseSpeedSubunitsPerTick = 1024;
    tree.components.chassis = {light};

    Frontier::DriveDesc wheels{};
    wheels.id = "Wheels";
    wheels.driveClass = Frontier::DriveClass::Wheels;
    wheels.speedFactorHundredths = 100;
    wheels.maxSlopePercent = 40;
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

Frontier::MatchSettings Lobby()
{
  Frontier::MatchSettings settings{};
  settings.seed = 17;
  settings.sizeClass = Frontier::SizeClass::Small;
  settings.seatCount = 2;
  settings.baseLevel = Frontier::BaseLevel::Nothing;
  settings.powerLevel = Frontier::PowerLevel::Medium;
  settings.deviceCapLevel = Frontier::DeviceCapLevel::Medium;
  settings.victory = Frontier::VictoryCondition::Annihilation;
  settings.rejoinGraceTicks = 400; // long: nothing here is testing the drop to AI
  settings.seats[0] = {Frontier::SeatKind::Human, 0, false};
  settings.seats[1] = {Frontier::SeatKind::Human, 1, false};
  return settings;
}

Frontier::LandscapeDefinition Ground()
{
  Frontier::LandscapeDefinition definition{};
  definition.version = Frontier::LANDSCAPE_DEFINITION_VERSION;
  definition.sizeClass = Frontier::SizeClass::Small;
  definition.cellsPerSide = Frontier::SIZE_CLASS_CELLS[0];
  definition.seed = 5;
  definition.palette = "Default";
  definition.tiles = {{0, 0, 512, 170, 90, 100, 48, 70, 1, 32, ""}};
  definition.starts = {{16, 16}, {100, 100}};
  return definition;
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

Frontier::ObjectId StructureAt(Frontier::Sim& _sim, std::uint8_t _seat, std::uint32_t _cellX, std::uint32_t _cellY)
{
  Frontier::Structure structure{};
  structure.seat = _seat;
  structure.design = 0;
  structure.cellX = _cellX;
  structure.cellY = _cellY;
  structure.state = Frontier::StructurePhase::Standing;
  structure.hitPoints = 1500;
  return _sim.Objects().Create(structure);
}

void Reveal(Frontier::Sim& _sim, std::uint8_t _seat, std::uint32_t _cellX, std::uint32_t _cellY, std::uint32_t _cells)
{
  for (std::uint32_t y = _cellY; y < _cellY + _cells; ++y)
  {
    for (std::uint32_t x = _cellX; x < _cellX + _cells; ++x)
    {
      _sim.SeatAt(_seat).fog.AddViewer(x, y);
    }
  }
}

void Conceal(Frontier::Sim& _sim, std::uint8_t _seat, std::uint32_t _cellX, std::uint32_t _cellY, std::uint32_t _cells)
{
  for (std::uint32_t y = _cellY; y < _cellY + _cells; ++y)
  {
    for (std::uint32_t x = _cellX; x < _cellX + _cells; ++x)
    {
      _sim.SeatAt(_seat).fog.RemoveViewer(x, y);
    }
  }
}

/// The whole loop in one object: the simulation, the host, the network, the client and the replica
/// the client fills.
struct Match
{
  explicit Match(std::uint64_t _faultSeed = 21)
    : sim(Lobby(), Tables()),
      network(_faultSeed),
      clientEnd(network.Connect()),
      host(sim, network.Host(), CONTENT, 0),
      client(clientEnd, LIVENESS, 0),
      replica(client)
  {
    Assert::IsTrue(sim.CreateLandscape(Ground()));
  }

  void Join(std::uint8_t _seat, std::uint64_t _token = 1)
  {
    client.SendJoin(CONTENT, _token, "owner", 0);
    Pump(6);
    Assert::IsTrue(client.State() == Frontier::ClientState::Playing, L"the join was accepted");
    Assert::AreEqual(_seat, client.Seat(), L"and it put this client in the seat the test expects");
  }

  void Pump(std::uint32_t _passes, bool _advanceSim = true)
  {
    for (std::uint32_t index = 0; index < _passes; ++index)
    {
      if (_advanceSim)
      {
        sim.Advance();
      }
      network.Host().Poll();
      host.Advance(sim.Tick());
      clientEnd.Poll();
      client.Advance(sim.Tick(), replica);
    }
  }

  /// Runs the match for a while and then keeps it running with nothing happening, so that the
  /// client catches up to a state that is no longer moving.
  ///
  /// THE SETTLING TICKS ARE ORDINARY TICKS. An earlier version of this stopped the simulation and
  /// pumped the network on its own, on the reasoning that the host would republish until it was
  /// acknowledged - it does - but that is a state the game is never in, and it took the sequence
  /// numbers twenty publishes past a tick that never changed. What convergence has to be measured
  /// against is the loop the game actually runs; nothing in this match gives an order, so once the
  /// opening frames are through, the state stands still while the ticks keep coming, and a client
  /// that is one publish behind a state that is not moving is a client that agrees with it.
  void RunAndSettle(std::uint32_t _ticks)
  {
    Pump(_ticks + 24);
  }

  Frontier::Sim sim;
  Neuron::LoopbackTransport network;
  Neuron::Transport& clientEnd;
  Frontier::Host host;
  Frontier::Client client;
  Frontier::Replica replica;
};

/// The assertion this whole file is for: the replica holds exactly what the host would send this
/// seat if it sent everything now, object for object and field for field.
void AssertConverged(const Frontier::Sim& _sim, std::uint8_t _seat, const Frontier::Replica& _replica, const wchar_t* _where)
{
  Frontier::InterestSet interest;
  Frontier::GatherInterest(_sim, _seat, interest);
  const Frontier::World& world = _sim.Objects();
  const Frontier::Replica& replica = _replica;

  Assert::AreEqual(interest.devices.size(), replica.Devices().size(), _where);
  for (const std::uint32_t id : interest.devices)
  {
    const Frontier::Device* device = world.FindDevice({id, Frontier::ObjectKind::Device});
    Assert::IsNotNull(device, _where);
    const auto held = replica.Devices().find(id);
    Assert::IsTrue(held != replica.Devices().end(), _where);
    Assert::IsTrue(held->second.state == Frontier::WireDevice(id, *device), _where);
  }

  Assert::AreEqual(interest.structures.size() + interest.ghosts.size(), replica.Structures().size(), _where);
  for (const std::uint32_t id : interest.structures)
  {
    const Frontier::Structure* structure = world.FindStructure({id, Frontier::ObjectKind::Structure});
    Assert::IsNotNull(structure, _where);
    const auto held = replica.Structures().find(id);
    Assert::IsTrue(held != replica.Structures().end(), _where);
    Assert::IsTrue(held->second.state == Frontier::WireStructure(_sim.Content(), id, *structure), _where);
    Assert::IsFalse(held->second.ghost, L"a structure in sight is not a ghost");
  }
  for (const Frontier::ObjectId& id : interest.ghosts)
  {
    const Frontier::Ghost* ghost = _sim.Seats()[_seat].ghosts.Find(id);
    Assert::IsNotNull(ghost, _where);
    const auto held = replica.Structures().find(id.value);
    Assert::IsTrue(held != replica.Structures().end(), _where);
    Assert::IsTrue(held->second.state == Frontier::WireGhost(*ghost), _where);
    Assert::IsTrue(held->second.ghost, L"a structure out of sight is a ghost");
  }

  Assert::AreEqual(interest.wrecks.size(), replica.Wrecks().size(), _where);
  for (const std::uint32_t id : interest.wrecks)
  {
    const Frontier::Wreck* wreck = world.FindWreck({id, Frontier::ObjectKind::Wreck});
    Assert::IsNotNull(wreck, _where);
    const auto held = replica.Wrecks().find(id);
    Assert::IsTrue(held != replica.Wrecks().end(), _where);
    Assert::IsTrue(held->second.state == Frontier::WireWreck(id, *wreck), _where);
  }

  Assert::AreEqual(interest.features.size(), replica.Features().size(), _where);
  for (const std::uint32_t id : interest.features)
  {
    const Frontier::Feature* feature = world.FindFeature({id, Frontier::ObjectKind::Feature});
    Assert::IsNotNull(feature, _where);
    const auto held = replica.Features().find(id);
    Assert::IsTrue(held != replica.Features().end(), _where);
    Assert::IsTrue(held->second.state == Frontier::WireFeature(id, *feature), _where);
  }

  // And the commander's own state, which is the one record that is his rather than an object's.
  // The latch is empty because nothing in this file sends an order, so nothing is refused; if that
  // ever stops being true this line fails rather than quietly comparing less than it says it does.
  const Frontier::RejectionLatch nothingRefused;
  Assert::IsTrue(replica.Own() == Frontier::WireSeat(_seat, _sim.Seats()[_seat], nothingRefused), _where);
}

} // namespace

TEST_CLASS(ConvergenceTests)
{
public:
  TEST_METHOD(TheReplicaEqualsTheHostsInterestSetOverAWholeMatch)
  {
    Match match;
    DeviceAt(match.sim, 0, 16, 16);
    DeviceAt(match.sim, 0, 18, 16);
    DeviceAt(match.sim, 1, 24, 24);
    StructureAt(match.sim, 0, 14, 14);
    StructureAt(match.sim, 1, 26, 26);
    Reveal(match.sim, 0, 10, 10, 20);
    match.Join(0);

    for (std::uint32_t round = 0; round < 8; ++round)
    {
      match.RunAndSettle(6);
      AssertConverged(match.sim, 0, match.replica, L"round after round, with no loss");
    }
    Assert::AreEqual(std::uint32_t{0}, match.network.Statistics().dropped, L"this run lost nothing");
    Assert::IsTrue(match.replica.NewestSequence() != Frontier::NO_BASELINE, L"frames were applied at all");
    // AGAINST A VACUOUS PASS. Every assertion above compares the replica with the interest set, and
    // two empty things are equal: without this line a replica that applied nothing at all would
    // sail through the whole test.
    Assert::IsTrue(match.replica.ObjectCount() >= 5, L"and there was something to compare");
  }

  /// The same claim with a quarter of the datagrams thrown away, which is what makes it worth
  /// making: a replica that converged only on a clean network would be a replica that cannot be
  /// used on one that is not.
  TEST_METHOD(TheReplicaConvergesThroughSeededLoss)
  {
    Match match(90210);
    match.network.SetFaults(Neuron::LoopbackFaults{250, 0, 0, 0, 0});
    DeviceAt(match.sim, 0, 16, 16);
    DeviceAt(match.sim, 0, 18, 16);
    DeviceAt(match.sim, 1, 24, 24);
    StructureAt(match.sim, 0, 14, 14);
    Reveal(match.sim, 0, 10, 10, 20);
    match.Join(0);

    for (std::uint32_t round = 0; round < 8; ++round)
    {
      match.RunAndSettle(6);
      AssertConverged(match.sim, 0, match.replica, L"round after round, through loss");
    }
    Assert::IsTrue(match.network.Statistics().dropped > 0, L"the network actually lost something");
    Assert::IsTrue(match.replica.ObjectCount() >= 4, L"and the replica holds what it converged on");
  }

  /// A ghost is the last thing a commander saw, kept after the building leaves his sight and
  /// corrected when he sees it again (GameDesign.md §5). Nothing in the replica decides this: the
  /// host sends the remembered record in the same list, and the replica's job is to not lose it.
  TEST_METHOD(AGhostPersistsOutOfSightAndIsCorrectedWhenSeenAgain)
  {
    Match match;
    DeviceAt(match.sim, 0, 16, 16);
    const Frontier::ObjectId watched = StructureAt(match.sim, 1, 30, 30);
    Reveal(match.sim, 0, 10, 10, 30);
    match.Join(0);

    match.RunAndSettle(6);
    AssertConverged(match.sim, 0, match.replica, L"seen");
    const auto seen = match.replica.Structures().find(watched.value);
    Assert::IsTrue(seen != match.replica.Structures().end(), L"the enemy structure is held");
    Assert::IsFalse(seen->second.ghost, L"and it is not a ghost while it is in sight");
    const std::uint16_t hitPointsWhenSeen = seen->second.state.hitPoints;
    Assert::IsTrue(hitPointsWhenSeen > 0);

    // The commander looks away, and the building takes damage he cannot see.
    Conceal(match.sim, 0, 10, 10, 30);
    Frontier::Structure* structure = match.sim.Objects().FindStructure(watched);
    Assert::IsNotNull(structure);
    structure->hitPoints = 400;
    match.RunAndSettle(6);
    AssertConverged(match.sim, 0, match.replica, L"out of sight");

    const auto remembered = match.replica.Structures().find(watched.value);
    Assert::IsTrue(remembered != match.replica.Structures().end(), L"the ghost is still there");
    Assert::IsTrue(remembered->second.ghost, L"and it is a ghost now");
    Assert::AreEqual(Frontier::GHOST_HIT_POINTS, remembered->second.state.hitPoints,
                     L"a ghost carries no hit points: he has no idea what it has taken");

    // He looks back, and the damage he never saw is there when he arrives.
    Reveal(match.sim, 0, 10, 10, 30);
    match.RunAndSettle(6);
    AssertConverged(match.sim, 0, match.replica, L"seen again");

    const auto again = match.replica.Structures().find(watched.value);
    Assert::IsTrue(again != match.replica.Structures().end());
    Assert::IsFalse(again->second.ghost, L"in sight again, so not a ghost");
    Assert::AreEqual(std::uint16_t{400}, again->second.state.hitPoints, L"and the damage is there");
  }

  /// The second of the three things a frame carries, and the one every other test here leaves
  /// alone: a CHANGE. Nothing else in this file gives an order, so nothing else moves, so a replica
  /// that threw every position delta away would pass all of them. This one walks a device across
  /// the landscape and holds the replica to it.
  ///
  /// WHAT IS ASSERTED WHILE IT WALKS IS NOT FULL CONVERGENCE, and that is not a weakening. A
  /// replica is one publish behind a world that is moving - that is what interpolating 100
  /// milliseconds back is FOR (TechnicalDesign.md §3) - so a replica that equalled the simulation
  /// mid-stride would mean the client had guessed ahead, which is the one thing Interpolation.h
  /// refuses to do. The claim while it moves is that the replica holds the same objects and its
  /// copy of the device follows; the claim once it stops is the full one, and it is made below.
  TEST_METHOD(ADeviceThatWalksIsFollowedDeltaByDelta)
  {
    Match match;
    const Frontier::ObjectId walker = DeviceAt(match.sim, 0, 16, 16);
    StructureAt(match.sim, 0, 14, 14);
    Reveal(match.sim, 0, 8, 8, 30);
    match.Join(0);
    match.RunAndSettle(4);
    AssertConverged(match.sim, 0, match.replica, L"before it is told to go");

    const Frontier::Device* device = match.sim.Objects().FindDevice(walker);
    Assert::IsNotNull(device);
    const std::int32_t startX = device->x;
    const auto atStart = match.replica.Devices().find(walker.value);
    Assert::IsTrue(atStart != match.replica.Devices().end());
    const std::int32_t replicaStartX = atStart->second.state.x;

    Frontier::Order walk{};
    walk.tick = match.sim.Tick() + 1;
    walk.seat = 0;
    walk.kind = Frontier::OrderKind::Move;
    walk.operands = {static_cast<std::int32_t>(walker.value), 26 * CELL + CELL / 2, 16 * CELL + CELL / 2, 0};
    match.sim.Submit(walk);

    for (std::uint32_t round = 0; round < 10; ++round)
    {
      match.Pump(8);
      Frontier::InterestSet interest;
      Frontier::GatherInterest(match.sim, 0, interest);
      Assert::AreEqual(interest.devices.size(), match.replica.Devices().size(), L"the same devices all the way");
      Assert::AreEqual(interest.structures.size() + interest.ghosts.size(), match.replica.Structures().size(), L"and structures");
    }

    const Frontier::Device* arrived = match.sim.Objects().FindDevice(walker);
    Assert::IsNotNull(arrived);
    Assert::IsTrue(arrived->x != startX, L"the order took and the device actually walked");

    const auto walked = match.replica.Devices().find(walker.value);
    Assert::IsTrue(walked != match.replica.Devices().end());
    // THE ASSERTION THAT A REPLICA IGNORING POSITION DELTAS FAILS. Every other test in this file
    // passes with the deltas thrown away, because nothing else in them ever moves.
    Assert::IsTrue(walked->second.state.x != replicaStartX, L"and the replica followed it");
    Assert::IsTrue(walked->second.motion.HasSegment(), L"with two samples to draw between");

    const Frontier::Pose pose = Frontier::Evaluate(walked->second.motion, match.replica.RenderTimeAtNewestFrame());
    const float olderX = static_cast<float>(walked->second.motion.older.x) / 4.0f;
    const float newerX = static_cast<float>(walked->second.motion.newer.x) / 4.0f;
    Assert::IsTrue(pose.x >= std::min(olderX, newerX) - 0.001f && pose.x <= std::max(olderX, newerX) + 0.001f,
                   L"and what it draws is on the segment between them");

    // And now the full claim, once the world stops moving and the client has caught up with it.
    match.Pump(200);
    AssertConverged(match.sim, 0, match.replica, L"after the walk, standing still");
  }

  /// The third of the three things a frame carries, and the one a test is most likely to leave
  /// uncovered: a removal. A creation is obvious when it is missing and a change shows up as a
  /// position that drifts, but an object that is never removed just sits there, and every other
  /// assertion in this file would still pass around it.
  TEST_METHOD(AnObjectRemovedWhileTheCommanderIsWatchingLeavesTheReplica)
  {
    Match match;
    const Frontier::ObjectId doomed = DeviceAt(match.sim, 0, 16, 16);
    const Frontier::ObjectId spared = DeviceAt(match.sim, 0, 18, 16);
    const Frontier::ObjectId razed = StructureAt(match.sim, 0, 14, 14);
    // THE SECOND STRUCTURE IS NOT SCENERY. Razing a commander's last one annihilates him, S11 ends
    // the match, and Sim::Advance stops on m_finished - so the frames this test is waiting for
    // never come and it fails somewhere else entirely. The first draft of this test did exactly
    // that, and the simulation was right both times.
    StructureAt(match.sim, 0, 20, 20);
    Reveal(match.sim, 0, 10, 10, 20);
    match.Join(0);
    match.RunAndSettle(6);
    AssertConverged(match.sim, 0, match.replica, L"before anything is removed");
    Assert::IsTrue(match.replica.Devices().find(doomed.value) != match.replica.Devices().end());
    Assert::IsTrue(match.replica.Structures().find(razed.value) != match.replica.Structures().end());

    // No rejoin and no full frame: the client stays connected, so the only way either of these can
    // leave the replica is through the removed list of an ordinary delta.
    Assert::IsTrue(match.sim.Objects().Remove(doomed));
    Assert::IsTrue(match.sim.Objects().Remove(razed));
    match.RunAndSettle(6);

    Assert::IsTrue(match.replica.Devices().find(doomed.value) == match.replica.Devices().end(),
                   L"the device went with the frame that said so");
    Assert::IsTrue(match.replica.Structures().find(razed.value) == match.replica.Structures().end(), L"and so did the structure");
    Assert::IsTrue(match.replica.Devices().find(spared.value) != match.replica.Devices().end(), L"and nothing else went with them");
    AssertConverged(match.sim, 0, match.replica, L"after the removals");
  }

  /// A rejoin is a full frame, and a full frame is everything: whatever the replica held before it
  /// was encoded against a baseline the new frame is not a delta from, so keeping any of it would
  /// leave objects the host has since removed standing on the field forever.
  TEST_METHOD(ARejoinRebuildsTheReplicaFromAFullFrame)
  {
    Match match;
    const Frontier::ObjectId doomed = DeviceAt(match.sim, 0, 16, 16);
    DeviceAt(match.sim, 0, 18, 16);
    StructureAt(match.sim, 0, 14, 14);
    Reveal(match.sim, 0, 10, 10, 20);
    match.Join(0);
    match.RunAndSettle(6);
    AssertConverged(match.sim, 0, match.replica, L"before the rejoin");
    const std::size_t before = match.replica.ObjectCount();
    Assert::IsTrue(before >= 3, L"there is something to lose");

    // The device goes while the client is not listening, so a replica that kept what it held would
    // still be drawing it after the rejoin.
    Assert::IsTrue(match.sim.Objects().Remove(doomed));
    match.Pump(6);

    Frontier::Client rejoined(match.clientEnd, LIVENESS, match.sim.Tick());
    Frontier::Replica rebuilt(rejoined);
    rejoined.SendJoin(CONTENT, 1, "owner", match.sim.Tick());
    for (std::uint32_t index = 0; index < 40; ++index)
    {
      match.network.Host().Poll();
      match.host.Advance(match.sim.Tick());
      match.clientEnd.Poll();
      rejoined.Advance(match.sim.Tick(), rebuilt);
    }

    Assert::IsTrue(rejoined.State() == Frontier::ClientState::Playing, L"the rejoin was accepted");
    Assert::IsTrue(rebuilt.Devices().find(doomed.value) == rebuilt.Devices().end(), L"what was destroyed while away is not still held");
    AssertConverged(match.sim, 0, rebuilt, L"after the rejoin");
  }

  /// The replica is one publish interval behind by design, and its own timeline says so. This is
  /// the number the executable draws at (TechnicalDesign.md §3 step 4).
  TEST_METHOD(TheReplicasRenderTimeIsOnePublishIntervalBehindItsNewestFrame)
  {
    Match match;
    DeviceAt(match.sim, 0, 16, 16);
    Reveal(match.sim, 0, 10, 10, 20);
    match.Join(0);
    match.RunAndSettle(10);

    Assert::IsTrue(match.replica.NewestTick() > 0);
    const std::int64_t expected =
      Frontier::RenderTimeOfTick(match.replica.NewestTick()) - Frontier::INTERPOLATION_DELAY_TICKS * Frontier::RENDER_TIME_SCALE;
    Assert::AreEqual(expected, match.replica.RenderTimeAtNewestFrame());
  }
};

} // namespace ReplicaTests
