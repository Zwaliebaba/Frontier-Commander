#include "pch.h"

#include "Client.h"
#include "Host.h"

#include "LoopbackTransport.h"

#include <cstdint>
#include <map>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// The conversation (TechnicalDesign.md §5.3 and §5.4; m1-vertical-slice/N2): a join answered with a
// full frame, deltas against what the client acknowledged, a lost frame answered by the next
// delta, a full frame when the history has run out, a frame too large for a datagram split and put
// back together, and a client that stops answering handed to the AI.
namespace NetTests
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
  settings.rejoinGraceTicks = 40; // short, so that the drop-to-AI case runs in ticks not minutes
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

class Recorder : public Frontier::FrameSink
{
public:
  void Apply(const Frontier::Frame& _frame) override
  {
    frames.push_back(_frame);
    for (const Frontier::DeviceState& device : _frame.createdDevices)
    {
      held[device.id] = device;
    }
    for (const Frontier::DeviceChange& change : _frame.changedDevices)
    {
      auto at = held.find(change.id);
      if (at == held.end())
      {
        continue;
      }
      if ((change.mask & static_cast<std::uint8_t>(Frontier::DeviceField::Position)) != 0)
      {
        at->second.x += change.deltaX;
        at->second.y += change.deltaY;
        at->second.z += change.deltaZ;
      }
      if ((change.mask & static_cast<std::uint8_t>(Frontier::DeviceField::HitPoints)) != 0)
      {
        at->second.hitPoints = change.hitPoints;
      }
    }
    for (const std::uint32_t id : _frame.removed)
    {
      held.erase(id);
    }
  }

  std::vector<Frontier::Frame> frames;
  std::map<std::uint32_t, Frontier::DeviceState> held;
};

/// One pass of the whole loop: the simulation, the host, the transport and the client.
struct Match
{
  Match()
    : sim(Lobby(), Tables()),
      network(21),
      clientEnd(network.Connect()),
      host(sim, network.Host(), CONTENT, 0),
      client(clientEnd, LIVENESS, 0)
  {
    Assert::IsTrue(sim.CreateLandscape(Ground()));
  }

  void Pump(std::uint32_t _ticks, bool _advanceSim = true)
  {
    for (std::uint32_t index = 0; index < _ticks; ++index)
    {
      if (_advanceSim)
      {
        sim.Advance();
      }
      network.Host().Poll();
      host.Advance(sim.Tick());
      clientEnd.Poll();
      client.Advance(sim.Tick(), sink);
    }
  }

  Frontier::Sim sim;
  Neuron::LoopbackTransport network;
  Neuron::Transport& clientEnd;
  Frontier::Host host;
  Frontier::Client client;
  Recorder sink;
};

} // namespace

TEST_CLASS(HostTests)
{
public:
  TEST_METHOD(AJoinIsAnsweredAndTheFirstFrameIsAFullOne)
  {
    Match match;
    DeviceAt(match.sim, 0, 16, 16);
    Reveal(match.sim, 0, 14, 14, 8);
    match.client.SendJoin(CONTENT, 1, "owner", 0);
    match.Pump(8);

    Assert::IsTrue(match.client.State() == Frontier::ClientState::Playing);
    Assert::AreEqual(static_cast<int>(0), static_cast<int>(match.client.Seat()));
    Assert::AreEqual(static_cast<std::uint64_t>(5), match.client.Landscape().seed, L"the ground came with the seat");
    Assert::IsFalse(match.sink.frames.empty());
    Assert::AreEqual(Frontier::NO_BASELINE, match.sink.frames[0].baselineSequence, L"the first frame has no baseline");
    Assert::AreEqual(static_cast<std::size_t>(1), match.sink.frames[0].createdDevices.size());
    Assert::AreEqual(static_cast<std::uint32_t>(1), match.host.Statistics().joins);
  }

  TEST_METHOD(AJoinWithTheWrongContentOrProtocolIsRefused)
  {
    Match match;
    match.client.SendJoin(CONTENT + 1, 1, "stranger", 0);
    match.Pump(6);
    Assert::IsTrue(match.client.State() == Frontier::ClientState::Refused);
    Assert::IsTrue(match.client.Refusal() == Frontier::RefusalReason::ContentHash);
    Assert::AreEqual(static_cast<std::uint32_t>(1), match.host.Statistics().refusals);
  }

  TEST_METHOD(AFrameAfterTheFirstIsADeltaAgainstWhatTheClientAcknowledged)
  {
    Match match;
    const Frontier::ObjectId walker = DeviceAt(match.sim, 0, 16, 16);
    Reveal(match.sim, 0, 14, 14, 8);
    match.client.SendJoin(CONTENT, 1, "owner", 0);
    match.Pump(10);
    const std::size_t afterJoin = match.sink.frames.size();
    Assert::IsTrue(afterJoin >= 2);

    // Move the device a little and publish again: what goes out is a change and not a creation.
    match.sim.Objects().FindDevice(walker)->x += 4 * Frontier::SUBUNITS_PER_WIRE_UNIT;
    match.Pump(4);
    const Frontier::Frame& latest = match.sink.frames.back();
    Assert::AreNotEqual(Frontier::NO_BASELINE, latest.baselineSequence, L"a delta against the acked baseline");
    Assert::IsTrue(latest.createdDevices.empty(), L"nothing was created");
    Assert::IsTrue(match.host.Statistics().fullFrames <= 2, L"only the frames sent before the first acknowledgement came back were full");

    // The client's picture followed the simulation, which is what the delta is for.
    Assert::AreEqual(static_cast<std::size_t>(1), match.sink.held.size());
    Assert::AreEqual(Frontier::WireFromSubunits(match.sim.Objects().FindDevice(walker)->x), match.sink.held[walker.value].x);
  }

  TEST_METHOD(ADeviceThatDidNotMoveCostsNothing)
  {
    Match match;
    DeviceAt(match.sim, 0, 16, 16);
    Reveal(match.sim, 0, 14, 14, 8);
    match.client.SendJoin(CONTENT, 1, "owner", 0);
    match.Pump(16);
    const Frontier::Frame& quiet = match.sink.frames.back();
    Assert::IsTrue(quiet.createdDevices.empty());
    Assert::IsTrue(quiet.changedDevices.empty(), L"a device that did not move sends nothing (§5.3)");
    Assert::IsTrue(quiet.removed.empty());
  }

  TEST_METHOD(ALostFrameIsRecoveredByTheNextDelta)
  {
    // §5.3's whole recovery model: no retransmission, no ordering, just the next delta from an
    // older baseline. The link drops a third of everything for a while and the client's picture
    // still catches up with the simulation's.
    Match match;
    const Frontier::ObjectId walker = DeviceAt(match.sim, 0, 16, 16);
    Reveal(match.sim, 0, 14, 14, 8);
    match.client.SendJoin(CONTENT, 1, "owner", 0);
    match.Pump(8);
    Assert::IsTrue(match.client.State() == Frontier::ClientState::Playing);

    match.network.SetFaults({330, 0, 0, 0, 0});
    for (std::uint32_t step = 0; step < 30; ++step)
    {
      match.sim.Objects().FindDevice(walker)->x += Frontier::SUBUNITS_PER_WIRE_UNIT;
      match.Pump(2);
    }
    match.network.SetFaults({});
    match.Pump(12);

    Assert::IsTrue(match.network.Statistics().dropped > 0, L"the link really did lose frames");
    Assert::AreEqual(Frontier::WireFromSubunits(match.sim.Objects().FindDevice(walker)->x), match.sink.held[walker.value].x,
                     L"and the picture caught up regardless");
  }

  TEST_METHOD(AClientWhoseBaselineHasAgedOutGetsAFullFrame)
  {
    // The history is 32 frames. A client that acknowledges nothing for longer than that cannot be
    // sent a delta, and the answer is the same path a join takes.
    Match match;
    DeviceAt(match.sim, 0, 16, 16);
    Reveal(match.sim, 0, 14, 14, 8);
    match.client.SendJoin(CONTENT, 1, "owner", 0);
    match.Pump(8);
    const std::uint32_t fullAfterJoin = match.host.Statistics().fullFrames;

    // Everything the client would send back is lost, so its acknowledgement never moves.
    match.network.SetFaults({1000, 0, 0, 0, 0});
    match.Pump(2 * static_cast<std::uint32_t>(Frontier::FRAME_HISTORY) + 20);
    match.network.SetFaults({});
    match.Pump(8);

    Assert::IsTrue(match.host.Statistics().fullFrames > fullAfterJoin, L"the baseline aged out and a full frame followed");
  }

  TEST_METHOD(AFrameTooLargeForADatagramIsSplitAndPutBackTogether)
  {
    Match match;
    for (std::uint32_t index = 0; index < 120; ++index)
    {
      DeviceAt(match.sim, 0, 16 + index % 20, 16 + index / 20);
    }
    Reveal(match.sim, 0, 10, 10, 30);
    match.client.SendJoin(CONTENT, 1, "owner", 0);
    match.Pump(10);

    Assert::IsTrue(match.host.Statistics().largestFrameBytes > Neuron::MAX_DATAGRAM_PAYLOAD_BYTES, L"the frame needed splitting");
    Assert::IsTrue(match.host.Statistics().fragments > 0);
    Assert::AreEqual(static_cast<std::size_t>(120), match.sink.held.size(), L"and every device arrived");
    Logger::WriteMessage(("    measured: a full frame of 120 devices is " + std::to_string(match.host.Statistics().largestFrameBytes) +
                          " bytes in " + std::to_string(match.host.Statistics().fragments) + " fragments\n")
                           .c_str());
  }

  TEST_METHOD(AnOrderReachesTheSimulationForTheNextTick)
  {
    Match match;
    const Frontier::ObjectId walker = DeviceAt(match.sim, 0, 16, 16);
    Reveal(match.sim, 0, 14, 14, 8);
    match.client.SendJoin(CONTENT, 1, "owner", 0);
    match.Pump(8);

    Frontier::Order stop{};
    stop.tick = match.sim.Tick() + 1;
    stop.seat = 0;
    stop.kind = Frontier::OrderKind::Stop;
    stop.operands = {static_cast<std::int32_t>(walker.value), 0, 0, 0};
    Assert::IsTrue(match.client.Submit(stop));
    match.Pump(10);
    Assert::AreEqual(static_cast<std::uint32_t>(1), match.host.Statistics().ordersApplied);
  }

  TEST_METHOD(AnOrderClaimingAnotherSeatIsRefusedByTheHost)
  {
    // Shape, not rules: "he does not own that device" is stage 1's judgement, but "he is not that
    // commander" is a client lying about who it is, and it never reaches the simulation.
    Match match;
    DeviceAt(match.sim, 0, 16, 16);
    Reveal(match.sim, 0, 14, 14, 8);
    match.client.SendJoin(CONTENT, 1, "owner", 0);
    match.Pump(8);

    Frontier::Order chat{};
    chat.tick = match.sim.Tick() + 1;
    chat.seat = 1; // not his seat
    chat.kind = Frontier::OrderKind::Chat;
    Assert::IsTrue(match.client.Submit(chat));
    match.Pump(10);
    Assert::AreEqual(static_cast<std::uint32_t>(0), match.host.Statistics().ordersApplied);
    Assert::AreEqual(static_cast<std::uint32_t>(1), match.host.Statistics().ordersRefusedShape);
  }

  TEST_METHOD(TheBytesAFrameTakesAtAHundredAndAtSixHundredObjects)
  {
    // What ADR-012 records, measured rather than estimated. A full frame is everything the
    // commander can see; a delta is what moved since he acknowledged the last one, which is the
    // number that decides whether this protocol fits a broadband link.
    for (const std::uint32_t objects : {std::uint32_t{100}, std::uint32_t{600}})
    {
      Match match;
      std::vector<Frontier::ObjectId> devices;
      devices.reserve(objects);
      for (std::uint32_t index = 0; index < objects; ++index)
      {
        devices.push_back(DeviceAt(match.sim, 0, 10 + index % 40, 10 + index / 40));
      }
      Reveal(match.sim, 0, 5, 5, 60);
      match.client.SendJoin(CONTENT, 1, "owner", 0);
      match.Pump(12);
      Assert::AreEqual(static_cast<std::size_t>(objects), match.sink.held.size(), L"every one of them arrived");
      const std::uint32_t full = match.host.Statistics().largestFrameBytes;

      // Half of them move a step, which is the shape of a battle rather than of a parade.
      for (std::uint32_t index = 0; index < objects; index += 2)
      {
        match.sim.Objects().FindDevice(devices[index])->x += 2 * Frontier::SUBUNITS_PER_WIRE_UNIT;
      }
      match.Pump(4);
      const std::uint32_t delta = match.host.LastFrameBytes(0);

      Logger::WriteMessage(("    measured: at " + std::to_string(objects) + " visible objects a full frame is " + std::to_string(full) +
                            " bytes and a delta with half of them moving is " + std::to_string(delta) + " bytes (" +
                            std::to_string(delta * 10 / 1024) + " KB/s at 10 Hz)\n")
                             .c_str());
      Assert::IsTrue(delta < full, L"a delta is smaller than the frame it is a delta from");
    }
  }

  TEST_METHOD(ASilentClientGoesUnderAiControlAndComesBackOnARejoin)
  {
    Match match;
    DeviceAt(match.sim, 0, 16, 16);
    Reveal(match.sim, 0, 14, 14, 8);
    match.client.SendJoin(CONTENT, 1, "owner", 0);
    match.Pump(8);
    Assert::IsTrue(match.host.SeatState(0) == Frontier::SeatConnection::Playing);

    // He stops answering: everything he sends is lost from here on.
    match.network.SetFaults({1000, 0, 0, 0, 0});
    match.Pump(200);
    Assert::IsTrue(match.host.SeatState(0) == Frontier::SeatConnection::UnderAi, L"the grace period ran out");
    Assert::AreEqual(static_cast<std::uint32_t>(1), match.host.Statistics().droppedToAi);

    // He comes back with the same token and takes the seat again, and gets a full frame with it.
    match.network.SetFaults({});
    const std::uint32_t fullBefore = match.host.Statistics().fullFrames;
    match.client.SendJoin(CONTENT, 1, "owner", match.sim.Tick());
    match.Pump(10);
    Assert::IsTrue(match.host.SeatState(0) == Frontier::SeatConnection::Playing, L"the same token is the same seat (§5.4)");
    Assert::AreEqual(static_cast<std::uint32_t>(1), match.host.Statistics().rejoins);
    Assert::IsTrue(match.host.Statistics().fullFrames > fullBefore, L"and nothing he held was trusted");
  }
};

} // namespace NetTests
