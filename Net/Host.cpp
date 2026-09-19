#include "pch.h"

#include "Host.h"

#include "OrderValidation.h"

#include <algorithm>

namespace Frontier
{

namespace
{

/// A seat that has gone quiet is suspect after this long and under AI control after the grace
/// period on top (§5.4, GameDesign.md §10). In the simulation's ticks, which is the host's clock.
constexpr std::uint32_t QUIET_TICKS = 5 * static_cast<std::uint32_t>(Neuron::TICKS_PER_SECOND);

} // namespace

Host::Host(Sim& _sim, Neuron::Transport& _transport, std::uint64_t _contentHash, std::uint32_t _tick)
  : m_sim(&_sim),
    m_transport(&_transport),
    m_contentHash(_contentHash)
{
  m_lastFrameBytes.assign(MAX_SEATS, 0);
  (void)_tick;
}

SeatConnection Host::SeatState(std::uint8_t _seat) const noexcept
{
  for (const HostClient& client : m_clients)
  {
    if (client.view.seat == _seat)
    {
      return client.state;
    }
  }
  return SeatConnection::Open;
}

const HostClient* Host::ClientForSeat(std::uint8_t _seat) const noexcept
{
  for (const HostClient& client : m_clients)
  {
    if (client.view.seat == _seat)
    {
      return &client;
    }
  }
  return nullptr;
}

std::uint32_t Host::LastFrameBytes(std::uint8_t _seat) const noexcept
{
  return _seat < m_lastFrameBytes.size() ? m_lastFrameBytes[_seat] : 0;
}

HostClient* Host::Find(Neuron::ConnectionId _connection) noexcept
{
  for (HostClient& client : m_clients)
  {
    if (client.connection == _connection && client.state != SeatConnection::Open)
    {
      return &client;
    }
  }
  return nullptr;
}

std::uint8_t Host::FreeSeat() const noexcept
{
  const std::span<const Seat> seats = m_sim->Seats();
  for (std::size_t seat = 0; seat < seats.size(); ++seat)
  {
    const auto index = static_cast<std::uint8_t>(seat);
    if (seats[index].kind != SeatKind::Human)
    {
      continue;
    }
    bool taken = false;
    for (const HostClient& client : m_clients)
    {
      taken = taken || (client.view.seat == index && client.state != SeatConnection::Open);
    }
    if (!taken)
    {
      return index;
    }
  }
  return MAX_SEATS;
}

void Host::Advance(std::uint32_t _tick)
{
  Neuron::ConnectionId connection = Neuron::NO_CONNECTION;
  while (m_transport->Accept(connection))
  {
    // Nothing is held for a connection until it has joined: a connection that never sends a Join
    // costs a socket and no seat.
  }

  Receive(_tick);

  for (HostClient& client : m_clients)
  {
    if (client.state == SeatConnection::Open)
    {
      continue;
    }
    const std::uint32_t quiet = _tick - client.lastHeardTick;
    const std::uint32_t grace = m_sim->Settings().rejoinGraceTicks;
    if (quiet >= QUIET_TICKS + grace)
    {
      if (client.state != SeatConnection::UnderAi)
      {
        // The grace period of GameDesign.md §10 has run out. The seat plays on as a scripted one,
        // and the player may still rejoin with the same token and take it back.
        client.state = SeatConnection::UnderAi;
        ++m_counters.droppedToAi;
      }
    }
    else if (quiet >= QUIET_TICKS)
    {
      client.state = SeatConnection::Suspect;
    }
    else
    {
      client.state = SeatConnection::Playing;
    }
  }

  LatchRejections();

  if (m_sim->PublishDue())
  {
    Publish(_tick);
  }
}

void Host::LatchRejections() noexcept
{
  // EVERY TICK, NOT EVERY PUBLISH. Sim clears a seat's rejections at the start of every stage 1 and
  // a publish is due on even ticks only, so reading the vector from PublishTo would drop every
  // refusal an odd tick judged - which, since a commander's clicks do not land on even ticks by
  // arrangement, is about half of them.
  const std::span<const Seat> seats = m_sim->Seats();
  for (HostClient& client : m_clients)
  {
    if (client.state == SeatConnection::Open || client.view.seat >= seats.size())
    {
      continue;
    }
    for (const OrderRejection& rejection : seats[client.view.seat].rejections)
    {
      client.view.rejection.sequence = static_cast<std::uint16_t>(client.view.rejection.sequence + 1);
      if (client.view.rejection.sequence == 0)
      {
        // 0 is how a seat that has had no refusal says so, so the counter steps over it on the wrap
        // rather than claiming a commander's 65,536th refusal never happened.
        client.view.rejection.sequence = 1;
      }
      client.view.rejection.kind = rejection.kind;
      client.view.rejection.reason = rejection.reason;
    }
  }
}

void Host::Receive(std::uint32_t _tick)
{
  Neuron::ConnectionId connection = Neuron::NO_CONNECTION;
  std::span<const std::byte> datagram;
  while (m_transport->Receive(connection, datagram))
  {
    std::span<const std::byte> payload;
    if (!Neuron::UnframeDatagram(datagram, payload, m_framing))
    {
      continue;
    }
    Neuron::ByteReader reader(payload);
    MessageKind kind{};
    if (!ReadMessageKind(reader, kind))
    {
      continue;
    }
    switch (kind)
    {
    case MessageKind::Join:
    {
      Join join{};
      if (Read(reader, join))
      {
        OnJoin(connection, join, _tick);
      }
      break;
    }
    case MessageKind::Orders:
    {
      HostClient* client = Find(connection);
      Orders orders{};
      if (client != nullptr && Read(reader, orders))
      {
        OnOrders(*client, orders, _tick);
      }
      break;
    }
    case MessageKind::Heartbeat:
    {
      HostClient* client = Find(connection);
      Heartbeat heartbeat{};
      if (client != nullptr && Read(reader, heartbeat))
      {
        client->lastHeardTick = _tick;
        client->view.acknowledgedSequence = std::max(client->view.acknowledgedSequence, heartbeat.ack.frameSequence);
      }
      break;
    }
    case MessageKind::Ack:
    case MessageKind::JoinAccepted:
    case MessageKind::JoinRefused:
    case MessageKind::Frame:
    case MessageKind::Fragment:
    default:
      // Everything else travels host to client. A host that receives one is being talked to by
      // something that is not a client of this protocol.
      break;
    }
  }
}

void Host::OnJoin(Neuron::ConnectionId _connection, const Join& _join, std::uint32_t _tick)
{
  if (_join.protocolVersion != NET_PROTOCOL_VERSION)
  {
    ++m_counters.refusals;
    Refuse(_connection, RefusalReason::ProtocolVersion);
    return;
  }
  if (_join.contentHash != m_contentHash)
  {
    // ADR-009's binding, at the join rather than at a snapshot: a client whose tables differ is
    // playing a different game and would diverge in silence.
    ++m_counters.refusals;
    Refuse(_connection, RefusalReason::ContentHash);
    return;
  }

  // A rejoin is a join with the same token (§5.4): the seat comes back from AI control and the
  // client gets a full frame, because nothing it held can be trusted after an absence.
  for (HostClient& client : m_clients)
  {
    if (client.token == _join.token && client.state != SeatConnection::Open)
    {
      client.connection = _connection;
      client.state = SeatConnection::Playing;
      client.lastHeardTick = _tick;
      client.view.history.Clear();
      client.view.acknowledgedSequence = NO_BASELINE;
      client.view.everSentFog = false;
      client.orders = {};
      ++m_counters.rejoins;
      JoinAccepted accepted{};
      accepted.seat = client.view.seat;
      accepted.tick = m_sim->Tick();
      accepted.settings = m_sim->Settings();
      accepted.landscape = m_sim->Terrain().Definition();
      Neuron::ByteWriter payload;
      Write(payload, accepted);
      SendPayload(_connection, payload);
      return;
    }
  }

  const std::uint8_t seat = FreeSeat();
  if (seat >= MAX_SEATS)
  {
    ++m_counters.refusals;
    Refuse(_connection, RefusalReason::NoSeat);
    return;
  }

  HostClient client{};
  client.connection = _connection;
  client.token = _join.token;
  client.view.seat = seat;
  client.lastHeardTick = _tick;
  client.state = SeatConnection::Playing;
  m_clients.push_back(std::move(client));
  ++m_counters.joins;

  JoinAccepted accepted{};
  accepted.seat = seat;
  accepted.tick = m_sim->Tick();
  accepted.settings = m_sim->Settings();
  accepted.landscape = m_sim->Terrain().Definition();
  Neuron::ByteWriter payload;
  Write(payload, accepted);
  SendPayload(_connection, payload);
}

void Host::OnOrders(HostClient& _client, const Orders& _orders, std::uint32_t _tick)
{
  _client.lastHeardTick = _tick;
  _client.view.acknowledgedSequence = std::max(_client.view.acknowledgedSequence, _orders.ack.frameSequence);

  std::vector<Order> delivered;
  _client.orders.Receive(_orders.orders, delivered);
  for (Order order : delivered)
  {
    // The SHAPE is checked here and the rules are the simulation's: a seat a client does not sit in
    // is a client lying about who it is, which Net refuses, while "he cannot afford it" is stage
    // 1's judgement and belongs in the rejection list the seat carries.
    if (order.seat != _client.view.seat || static_cast<std::uint8_t>(order.kind) >= ORDER_KIND_COUNT)
    {
      ++m_counters.ordersRefusedShape;
      continue;
    }
    // Always for the next tick: an order for a tick already run would be moved anyway, and one for
    // a tick far ahead would let a client schedule the future.
    order.tick = m_sim->Tick() + 1;
    m_sim->Submit(order);
    ++m_counters.ordersApplied;
  }
}

void Host::Publish(std::uint32_t _tick)
{
  for (HostClient& client : m_clients)
  {
    if (client.state == SeatConnection::Open)
    {
      continue;
    }
    PublishTo(client, _tick);
  }
  // The events of the interval have now gone to everyone who was entitled to them.
  m_events.clear();
}

void Host::PublishTo(HostClient& _client, std::uint32_t _tick)
{
  (void)_tick;
  GatherInterest(*m_sim, _client.view.seat, m_interest);

  // Only the events this commander could see. An event names objects, and an object he cannot see
  // is one he is not told died - which is what makes an explosion fog-correct for free (§5.3).
  std::vector<Event> visible;
  for (const Event& event : m_events)
  {
    const bool known = std::binary_search(m_interest.devices.begin(), m_interest.devices.end(), event.source) ||
                       std::binary_search(m_interest.structures.begin(), m_interest.structures.end(), event.source);
    if (known)
    {
      visible.push_back(event);
    }
  }

  const FrameRecord* baseline =
    _client.view.acknowledgedSequence == NO_BASELINE ? nullptr : _client.view.history.Find(_client.view.acknowledgedSequence);
  if (baseline == nullptr)
  {
    ++m_counters.fullFrames;
  }
  EncodeFrame(*m_sim, m_interest, _client.view, baseline, visible, m_frame, m_record);

  Neuron::ByteWriter payload;
  Write(payload, m_frame);
  const std::size_t bytes = payload.Bytes().size();
  m_lastFrameBytes[_client.view.seat] = static_cast<std::uint32_t>(bytes);
  m_counters.largestFrameBytes = std::max(m_counters.largestFrameBytes, static_cast<std::uint32_t>(bytes));

  if (bytes <= Neuron::MAX_DATAGRAM_PAYLOAD_BYTES)
  {
    SendPayload(_client.connection, payload);
  }
  else if (SplitIntoFragments(m_frame.sequence, payload.Bytes(), m_fragments))
  {
    for (const Fragment& piece : m_fragments)
    {
      Neuron::ByteWriter one;
      Write(one, piece);
      SendPayload(_client.connection, one);
      ++m_counters.fragments;
    }
  }
  else
  {
    // Past what MAX_FRAGMENTS can carry. Nothing is sent, the client's acknowledgement does not
    // move, and the next publish tries again - which is the same recovery a lost frame has.
    return;
  }

  _client.view.history.Push(m_record);
  ++_client.view.nextSequence;
  ++m_counters.framesPublished;
}

void Host::SendPayload(Neuron::ConnectionId _connection, const Neuron::ByteWriter& _payload)
{
  Neuron::ByteWriter datagram;
  if (Neuron::FrameDatagram(_payload.Bytes(), datagram))
  {
    (void)m_transport->Send(_connection, datagram.Bytes());
  }
}

void Host::Refuse(Neuron::ConnectionId _connection, RefusalReason _reason)
{
  JoinRefused refused{};
  refused.reason = _reason;
  refused.hostProtocolVersion = NET_PROTOCOL_VERSION;
  refused.hostContentHash = m_contentHash;
  Neuron::ByteWriter payload;
  Write(payload, refused);
  SendPayload(_connection, payload);
}

} // namespace Frontier
