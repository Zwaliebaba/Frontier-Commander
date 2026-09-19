#include "pch.h"

#include "Client.h"

#include "LoopbackTransport.h"

#include <cstdint>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// The client end over a real transport (TechnicalDesign.md §5.5; m1-vertical-slice/N3). The host
// here is the smallest thing that speaks the protocol - N2 builds the real one - because what is
// under test is what the CLIENT does with what arrives: a join it may have to repeat, a frame in
// pieces, a piece that never comes, a frame that arrives after the one that replaced it, and the
// acknowledgement that has to ride every datagram it sends back.
namespace NetTests
{

namespace
{

constexpr Neuron::LivenessSettings LIVENESS = {20, 100, 200};

/// Remembers every frame it was handed, in the order it was handed them.
class Recorder : public Frontier::FrameSink
{
public:
  void Apply(const Frontier::Frame& _frame) override
  {
    frames.push_back(_frame);
  }

  std::vector<Frontier::Frame> frames;
};

Frontier::MatchSettings TheLobby()
{
  Frontier::MatchSettings settings{};
  settings.seed = 4242;
  settings.sizeClass = Frontier::SizeClass::Small;
  settings.seatCount = 2;
  settings.baseLevel = Frontier::BaseLevel::Small;
  settings.powerLevel = Frontier::PowerLevel::Medium;
  settings.victory = Frontier::VictoryCondition::Annihilation;
  settings.deviceCapLevel = Frontier::DeviceCapLevel::Medium;
  settings.seats[0] = {Frontier::SeatKind::Human, 0, false};
  settings.seats[1] = {Frontier::SeatKind::Ai, 1, true};
  return settings;
}

Frontier::LandscapeDefinition TheGround()
{
  Frontier::LandscapeDefinition definition{};
  definition.version = Frontier::LANDSCAPE_DEFINITION_VERSION;
  definition.sizeClass = Frontier::SizeClass::Small;
  definition.cellsPerSide = Frontier::SIZE_CLASS_CELLS[0];
  definition.seed = 7;
  definition.palette = "Default";
  definition.tiles = {{0, 0, 512, 170, 90, 100, 48, 70, 1, 32, ""}};
  definition.starts = {{16, 16}, {100, 100}};
  return definition;
}

Frontier::DeviceState ADevice(std::uint32_t _id)
{
  Frontier::DeviceState record{};
  record.id = _id;
  record.design = 0;
  record.seat = 0;
  record.x = static_cast<std::int32_t>(_id) * 16;
  record.hitPoints = 100;
  record.targetKind = Frontier::ObjectKind::Device;
  record.stances = {Frontier::PrimaryOrder::Stop, Frontier::FireStance::FireAtWill, Frontier::RangeStance::Optimal,
                    Frontier::RetreatStance::AtHalf, Frontier::MovementStance::Pursue};
  return record;
}

/// The other end: it answers a Join, publishes frames, splits one that is too large, and reads the
/// orders and acknowledgements that come back. Everything N2's Host will do properly, done here in
/// as few lines as the client needs to be exercised.
class TinyHost
{
public:
  explicit TinyHost(Neuron::Transport& _transport)
    : m_transport(&_transport)
  {
  }

  void Poll(std::uint32_t _tick)
  {
    Neuron::ConnectionId connection = Neuron::NO_CONNECTION;
    while (m_transport->Accept(connection))
    {
      m_connection = connection;
    }
    std::span<const std::byte> datagram;
    while (m_transport->Receive(connection, datagram))
    {
      m_connection = connection;
      std::span<const std::byte> payload;
      Neuron::FramingCounters framing{};
      if (!Neuron::UnframeDatagram(datagram, payload, framing))
      {
        continue;
      }
      Neuron::ByteReader reader(payload);
      Frontier::MessageKind kind{};
      if (!Frontier::ReadMessageKind(reader, kind))
      {
        continue;
      }
      if (kind == Frontier::MessageKind::Join)
      {
        Frontier::Join join{};
        if (Frontier::Read(reader, join))
        {
          ++joins;
          if (answerJoins)
          {
            Accept(_tick);
          }
        }
      }
      else if (kind == Frontier::MessageKind::Orders)
      {
        Frontier::Orders orders{};
        if (Frontier::Read(reader, orders))
        {
          acknowledgedFrame = orders.ack.frameSequence;
          m_stream.Receive(orders.orders, received);
          // A real host puts this on its next frame; here it is a bare Ack, because what the client
          // needs from it is the order sequence and nothing else.
          Frontier::Ack ack{};
          ack.frameSequence = 0;
          ack.orderSequence = m_stream.Acknowledgement();
          Neuron::ByteWriter answer;
          Frontier::Write(answer, ack);
          Send(answer);
        }
      }
      else if (kind == Frontier::MessageKind::Heartbeat)
      {
        Frontier::Heartbeat beat{};
        if (Frontier::Read(reader, beat))
        {
          acknowledgedFrame = beat.ack.frameSequence;
        }
      }
    }
  }

  void Accept(std::uint32_t _tick)
  {
    Frontier::JoinAccepted accepted{};
    accepted.seat = 0;
    accepted.tick = _tick;
    accepted.settings = TheLobby();
    accepted.landscape = TheGround();
    Neuron::ByteWriter payload;
    Frontier::Write(payload, accepted);
    Send(payload);
  }

  void Refuse(Frontier::RefusalReason _reason)
  {
    Frontier::JoinRefused refused{};
    refused.reason = _reason;
    refused.hostProtocolVersion = Frontier::NET_PROTOCOL_VERSION;
    Neuron::ByteWriter payload;
    Frontier::Write(payload, refused);
    Send(payload);
  }

  /// A frame, whole, in one datagram.
  void Publish(const Frontier::Frame& _frame)
  {
    Neuron::ByteWriter payload;
    Frontier::Write(payload, _frame);
    Send(payload);
  }

  /// A frame in _pieces fragments. _skip, if given, is the one piece that never leaves.
  void PublishInPieces(const Frontier::Frame& _frame, std::uint8_t _pieces, int _skip = -1)
  {
    Neuron::ByteWriter payload;
    Frontier::Write(payload, _frame);
    const std::span<const std::byte> whole = payload.Bytes();
    const std::size_t each = (whole.size() + _pieces - 1) / _pieces;
    for (std::uint8_t index = 0; index < _pieces; ++index)
    {
      if (static_cast<int>(index) == _skip)
      {
        continue;
      }
      const std::size_t from = std::min(whole.size(), static_cast<std::size_t>(index) * each);
      const std::size_t to = std::min(whole.size(), from + each);
      Frontier::Fragment fragment{};
      fragment.frameSequence = _frame.sequence;
      fragment.index = index;
      fragment.count = _pieces;
      fragment.bytes.assign(whole.begin() + static_cast<std::ptrdiff_t>(from), whole.begin() + static_cast<std::ptrdiff_t>(to));
      Neuron::ByteWriter one;
      Frontier::Write(one, fragment);
      Send(one);
    }
  }

  bool answerJoins = true;
  std::uint32_t joins = 0;
  std::uint32_t acknowledgedFrame = 0;
  std::vector<Frontier::Order> received;

private:
  void Send(const Neuron::ByteWriter& _payload)
  {
    Neuron::ByteWriter datagram;
    if (Neuron::FrameDatagram(_payload.Bytes(), datagram))
    {
      (void)m_transport->Send(m_connection, datagram.Bytes());
    }
  }

  Neuron::Transport* m_transport;
  Neuron::ConnectionId m_connection = Neuron::NO_CONNECTION;
  Frontier::ReliableStream m_stream;
};

Frontier::Frame AFrame(std::uint32_t _sequence, std::uint32_t _baseline, std::uint32_t _tick, std::uint32_t _devices)
{
  Frontier::Frame frame{};
  frame.sequence = _sequence;
  frame.baselineSequence = _baseline;
  frame.tick = _tick;
  for (std::uint32_t index = 0; index < _devices; ++index)
  {
    frame.createdDevices.push_back(ADevice(index + 1));
  }
  frame.seat.researchItem = Frontier::NO_RESEARCH_ITEM;
  return frame;
}

} // namespace

TEST_CLASS(ClientTests)
{
public:
  TEST_METHOD(AJoinIsAnsweredWithTheSeatTheSettingsAndTheGround)
  {
    Neuron::LoopbackTransport network(1);
    Neuron::Transport& clientEnd = network.Connect();
    TinyHost host(network.Host());
    Frontier::Client client(clientEnd, LIVENESS, 0);
    Recorder sink;

    client.SendJoin(0xABCDu, 77, "Zwalie", 0);
    for (std::uint32_t tick = 0; tick < 5; ++tick)
    {
      network.Host().Poll();
      host.Poll(tick);
      clientEnd.Poll();
      client.Advance(tick, sink);
    }
    Assert::IsTrue(client.State() == Frontier::ClientState::Playing);
    Assert::AreEqual(static_cast<int>(0), static_cast<int>(client.Seat()));
    Assert::AreEqual(static_cast<std::uint64_t>(4242), client.Settings().seed);
    Assert::AreEqual(static_cast<std::uint64_t>(7), client.Landscape().seed, L"the ground came with the seat");
    Assert::AreEqual(static_cast<std::size_t>(2), client.Landscape().starts.size());
  }

  TEST_METHOD(AJoinNobodyAnsweredIsSentAgain)
  {
    Neuron::LoopbackTransport network(2);
    Neuron::Transport& clientEnd = network.Connect();
    TinyHost host(network.Host());
    host.answerJoins = false;
    Frontier::Client client(clientEnd, LIVENESS, 0);
    Recorder sink;

    client.SendJoin(0xABCDu, 77, "Zwalie", 0);
    for (std::uint32_t tick = 0; tick < 3 * Frontier::JOIN_RETRY_TICKS; ++tick)
    {
      network.Host().Poll();
      host.Poll(tick);
      clientEnd.Poll();
      client.Advance(tick, sink);
    }
    Assert::IsTrue(client.State() == Frontier::ClientState::Joining);
    Assert::AreEqual(static_cast<std::uint32_t>(3), host.joins, L"the first, and one at twenty and at forty");
  }

  TEST_METHOD(ARefusalIsRememberedWithItsReason)
  {
    Neuron::LoopbackTransport network(3);
    Neuron::Transport& clientEnd = network.Connect();
    TinyHost host(network.Host());
    host.answerJoins = false;
    Frontier::Client client(clientEnd, LIVENESS, 0);
    Recorder sink;

    client.SendJoin(0xDEADu, 77, "Zwalie", 0);
    network.Host().Poll();
    host.Poll(0);
    host.Refuse(Frontier::RefusalReason::ContentHash);
    network.Host().Poll();
    clientEnd.Poll();
    client.Advance(1, sink);
    Assert::IsTrue(client.State() == Frontier::ClientState::Refused);
    Assert::IsTrue(client.Refusal() == Frontier::RefusalReason::ContentHash);
  }

  TEST_METHOD(AFragmentedFrameIsAppliedWhenTheLastPieceArrives)
  {
    Neuron::LoopbackTransport network(4);
    Neuron::Transport& clientEnd = network.Connect();
    TinyHost host(network.Host());
    Frontier::Client client(clientEnd, LIVENESS, 0);
    Recorder sink;
    client.SendJoin(0, 1, "a", 0);
    Pump(network, clientEnd, host, client, sink, 0, 2);

    host.PublishInPieces(AFrame(1, Frontier::NO_BASELINE, 20, 30), 4);
    Pump(network, clientEnd, host, client, sink, 2, 4);
    Assert::AreEqual(static_cast<std::size_t>(1), sink.frames.size());
    Assert::AreEqual(static_cast<std::size_t>(30), sink.frames[0].createdDevices.size());
    Assert::AreEqual(static_cast<std::uint32_t>(1), client.Fragments().Statistics().completed);
    Assert::AreEqual(static_cast<std::uint32_t>(1), client.AppliedSequence());
  }

  TEST_METHOD(AFrameWithAPieceMissingIsSkippedAndTheNextFullFrameRecoversIt)
  {
    // §5.3's rule: a frame with a fragment missing is discarded rather than waited for or asked
    // for again, and the client's unmoved acknowledgement is what tells the host to send a frame
    // it can apply. Nothing is lost but one interval.
    Neuron::LoopbackTransport network(5);
    Neuron::Transport& clientEnd = network.Connect();
    TinyHost host(network.Host());
    Frontier::Client client(clientEnd, LIVENESS, 0);
    Recorder sink;
    client.SendJoin(0, 1, "a", 0);
    Pump(network, clientEnd, host, client, sink, 0, 2);

    host.PublishInPieces(AFrame(1, Frontier::NO_BASELINE, 20, 30), 4, 2); // the third piece never leaves
    Pump(network, clientEnd, host, client, sink, 2, 6);
    Assert::IsTrue(sink.frames.empty(), L"three quarters of a frame is not a frame");
    Assert::AreEqual(Frontier::NO_BASELINE, client.AppliedSequence(), L"and it acknowledges nothing new");
    Assert::AreEqual(static_cast<std::uint32_t>(0), host.acknowledgedFrame);

    // The host, seeing the acknowledgement has not moved, sends a frame this client can apply.
    host.PublishInPieces(AFrame(2, Frontier::NO_BASELINE, 22, 30), 4);
    Pump(network, clientEnd, host, client, sink, 6, 12);
    Assert::AreEqual(static_cast<std::size_t>(1), sink.frames.size());
    Assert::AreEqual(static_cast<std::uint32_t>(2), sink.frames[0].sequence);
    Assert::AreEqual(static_cast<std::uint32_t>(2), client.AppliedSequence());
    Assert::AreEqual(static_cast<std::uint32_t>(1), client.Fragments().Statistics().abandoned);
  }

  TEST_METHOD(ADeltaAgainstABaselineTheClientDoesNotHaveIsSkipped)
  {
    Neuron::LoopbackTransport network(6);
    Neuron::Transport& clientEnd = network.Connect();
    TinyHost host(network.Host());
    Frontier::Client client(clientEnd, LIVENESS, 0);
    Recorder sink;
    client.SendJoin(0, 1, "a", 0);
    Pump(network, clientEnd, host, client, sink, 0, 2);

    host.Publish(AFrame(1, Frontier::NO_BASELINE, 20, 2));
    Pump(network, clientEnd, host, client, sink, 2, 4);
    Assert::AreEqual(static_cast<std::size_t>(1), sink.frames.size());

    // Frame 2 was lost on the way; frame 3 is a delta against it and cannot be applied.
    host.Publish(AFrame(3, 2, 26, 2));
    Pump(network, clientEnd, host, client, sink, 4, 6);
    Assert::AreEqual(static_cast<std::size_t>(1), sink.frames.size(), L"a delta needs its baseline");
    Assert::AreEqual(static_cast<std::uint32_t>(1), client.AppliedSequence());

    // The host encodes the next one against what the client actually acknowledged.
    host.Publish(AFrame(4, 1, 28, 2));
    Pump(network, clientEnd, host, client, sink, 6, 8);
    Assert::AreEqual(static_cast<std::size_t>(2), sink.frames.size());
    Assert::AreEqual(static_cast<std::uint32_t>(4), client.AppliedSequence());

    // And a frame that arrives after one that overtook it is dropped rather than applied backwards.
    host.Publish(AFrame(2, 1, 22, 2));
    Pump(network, clientEnd, host, client, sink, 8, 10);
    Assert::AreEqual(static_cast<std::size_t>(2), sink.frames.size(), L"an overtaken frame is not applied");
    Assert::IsTrue(client.Statistics().framesSkipped >= 2);
  }

  TEST_METHOD(EveryDatagramTheClientSendsCarriesWhatItHasApplied)
  {
    Neuron::LoopbackTransport network(7);
    Neuron::Transport& clientEnd = network.Connect();
    TinyHost host(network.Host());
    Frontier::Client client(clientEnd, LIVENESS, 0);
    Recorder sink;
    client.SendJoin(0, 1, "a", 0);
    Pump(network, clientEnd, host, client, sink, 0, 2);

    host.Publish(AFrame(5, Frontier::NO_BASELINE, 30, 1));
    Pump(network, clientEnd, host, client, sink, 2, 4);

    Frontier::Order move{};
    move.tick = 31;
    move.seat = 0;
    move.kind = Frontier::OrderKind::Move;
    move.operands = {1, 500, 600, 0};
    Assert::IsTrue(client.Submit(move));
    Pump(network, clientEnd, host, client, sink, 4, 12);

    Assert::AreEqual(static_cast<std::size_t>(1), host.received.size(), L"the order arrived");
    Assert::AreEqual(500, host.received[0].operands[1]);
    Assert::AreEqual(static_cast<std::uint32_t>(5), host.acknowledgedFrame, L"and said what it had applied");
  }

  TEST_METHOD(ACommanderWhoseOrdersGetThroughALossyLinkLosesNone)
  {
    // The whole point of the reliable stream, over the real transport with a fifth of the
    // datagrams dropped and a twentieth duplicated.
    Neuron::LoopbackTransport network(8);
    network.SetFaults({200, 50, 100, 0, 0});
    Neuron::Transport& clientEnd = network.Connect();
    TinyHost host(network.Host());
    Frontier::Client client(clientEnd, LIVENESS, 0);
    Recorder sink;

    client.SendJoin(0, 1, "a", 0);
    for (std::uint32_t tick = 0; tick < 40 && client.State() != Frontier::ClientState::Playing; ++tick)
    {
      network.Host().Poll();
      host.Poll(tick);
      clientEnd.Poll();
      client.Advance(tick, sink);
    }
    Assert::IsTrue(client.State() == Frontier::ClientState::Playing, L"the join got through in the end");

    constexpr std::int32_t ORDERS = 20;
    std::int32_t given = 0;
    for (std::uint32_t tick = 40; tick < 800; ++tick)
    {
      if (given < ORDERS && tick % 6 == 0)
      {
        Frontier::Order order{};
        order.tick = tick;
        order.seat = 0;
        order.kind = Frontier::OrderKind::Move;
        order.operands = {++given, 0, 0, 0};
        Assert::IsTrue(client.Submit(order));
      }
      network.Host().Poll();
      host.Poll(tick);
      clientEnd.Poll();
      client.Advance(tick, sink);
    }
    Assert::AreEqual(static_cast<std::size_t>(ORDERS), host.received.size(), L"every order, exactly once");
    for (std::int32_t index = 0; index < ORDERS; ++index)
    {
      Assert::AreEqual(index + 1, host.received[static_cast<std::size_t>(index)].operands[0], L"and in order");
    }
    Logger::WriteMessage(("    measured: " + std::to_string(ORDERS) + " orders through a link dropping a fifth took " +
                          std::to_string(client.Statistics().datagramsSent) + " datagrams, " +
                          std::to_string(client.OrderStream().Statistics().resent) + " resends\n")
                           .c_str());
  }

private:
  /// Runs the loop from _from to _to: the host end, the tiny host, the client end, the client.
  static void Pump(Neuron::LoopbackTransport& _network, Neuron::Transport& _clientEnd, TinyHost& _host, Frontier::Client& _client,
                   Recorder& _sink, std::uint32_t _from, std::uint32_t _to)
  {
    for (std::uint32_t tick = _from; tick < _to; ++tick)
    {
      _network.Host().Poll();
      _host.Poll(tick);
      _clientEnd.Poll();
      _client.Advance(tick, _sink);
    }
  }
};

} // namespace NetTests
