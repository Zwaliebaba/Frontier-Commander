#include "pch.h"

#include "Client.h"

#include <algorithm>

namespace Frontier
{

Client::Client(Neuron::Transport& _transport, const Neuron::LivenessSettings& _liveness, std::uint32_t _tick)
  : m_transport(&_transport),
    m_liveness(_liveness, _tick)
{
}

void Client::SendJoin(std::uint64_t _contentHash, std::uint64_t _token, std::string_view _name, std::uint32_t _tick)
{
  m_join = {};
  m_join.protocolVersion = NET_PROTOCOL_VERSION;
  m_join.contentHash = _contentHash;
  m_join.token = _token;
  const std::size_t bytes = std::min(_name.size(), MAX_PLAYER_NAME_BYTES);
  m_join.nameBytes = static_cast<std::uint8_t>(bytes);
  for (std::size_t index = 0; index < bytes; ++index)
  {
    m_join.name[index] = _name[index];
  }
  m_state = ClientState::Joining;
  m_lastJoinTick = _tick;

  Neuron::ByteWriter payload;
  Write(payload, m_join);
  SendPayload(payload, _tick);
}

bool Client::Submit(const Order& _order)
{
  return m_orders.Send(_order) != NO_SEQUENCE;
}

void Client::SendPayload(const Neuron::ByteWriter& _payload, std::uint32_t _tick)
{
  Neuron::ByteWriter datagram;
  if (!Neuron::FrameDatagram(_payload.Bytes(), datagram))
  {
    return; // Over the datagram size: the caller built something that cannot travel.
  }
  if (m_transport->Send(Neuron::HOST_CONNECTION, datagram.Bytes()))
  {
    ++m_counters.datagramsSent;
    m_liveness.Sent(_tick);
  }
}

void Client::Advance(std::uint32_t _tick, FrameSink& _sink)
{
  Neuron::ConnectionId connection = Neuron::NO_CONNECTION;
  std::span<const std::byte> datagram;
  while (m_transport->Receive(connection, datagram))
  {
    ++m_counters.datagramsReceived;
    std::span<const std::byte> payload;
    if (!Neuron::UnframeDatagram(datagram, payload, m_framing))
    {
      continue;
    }
    m_liveness.Heard(_tick);
    Deliver(payload, _tick, _sink);
  }

  if (m_state == ClientState::Joining)
  {
    // A Join is the one message with no acknowledgement of its own, so it is the one that has to be
    // sent again on its own schedule. Everything after it rides the frame acknowledgement.
    if (_tick - m_lastJoinTick >= JOIN_RETRY_TICKS)
    {
      m_lastJoinTick = _tick;
      Neuron::ByteWriter payload;
      Write(payload, m_join);
      SendPayload(payload, _tick);
    }
    return;
  }
  if (m_state != ClientState::Playing)
  {
    return;
  }

  SendOrdersAndAck(_tick);
  if (m_liveness.Advance(_tick) == Neuron::LivenessState::Lost)
  {
    m_state = ClientState::Lost;
  }
}

void Client::Deliver(std::span<const std::byte> _payload, std::uint32_t _tick, FrameSink& _sink)
{
  Neuron::ByteReader reader(_payload);
  MessageKind kind{};
  if (!ReadMessageKind(reader, kind))
  {
    ++m_counters.unreadable;
    return;
  }
  switch (kind)
  {
  case MessageKind::JoinAccepted:
  {
    JoinAccepted accepted{};
    if (!Read(reader, accepted))
    {
      ++m_counters.unreadable;
      return;
    }
    // A second JoinAccepted is the host answering a Join this client repeated; taking it again is
    // harmless and taking the newer one is right, because a rejoin may land in a different seat.
    m_seat = accepted.seat;
    m_settings = accepted.settings;
    m_landscape = std::move(accepted.landscape);
    m_simulationTick = accepted.tick;
    m_appliedSequence = NO_BASELINE;
    m_reassembler.Clear();
    m_state = ClientState::Playing;
    return;
  }
  case MessageKind::JoinRefused:
  {
    JoinRefused refused{};
    if (!Read(reader, refused))
    {
      ++m_counters.unreadable;
      return;
    }
    m_refusal = refused.reason;
    m_state = ClientState::Refused;
    return;
  }
  case MessageKind::Frame:
  {
    Frame frame{};
    if (!Read(reader, frame))
    {
      ++m_counters.unreadable;
      return;
    }
    TakeFrame(frame, _sink);
    return;
  }
  case MessageKind::Fragment:
  {
    Fragment fragment{};
    if (!Read(reader, fragment))
    {
      ++m_counters.unreadable;
      return;
    }
    std::vector<std::byte> whole;
    if (!m_reassembler.Add(fragment, whole))
    {
      return; // Not the last piece, or a piece of a frame already given up on.
    }
    Neuron::ByteReader inner(whole);
    MessageKind innerKind{};
    Frame frame{};
    if (!ReadMessageKind(inner, innerKind) || innerKind != MessageKind::Frame || !Read(inner, frame))
    {
      ++m_counters.unreadable;
      return;
    }
    TakeFrame(frame, _sink);
    return;
  }
  case MessageKind::Heartbeat:
  {
    Heartbeat heartbeat{};
    if (!Read(reader, heartbeat))
    {
      ++m_counters.unreadable;
      return;
    }
    m_orders.Acknowledged(heartbeat.ack.orderSequence, _tick);
    return;
  }
  case MessageKind::Ack:
  {
    Ack ack{};
    if (!Read(reader, ack))
    {
      ++m_counters.unreadable;
      return;
    }
    m_orders.Acknowledged(ack.orderSequence, _tick);
    return;
  }
  case MessageKind::Join:
  case MessageKind::Orders:
  default:
    // Join and Orders travel client to host: a client that receives one is talking to something
    // that is not a host, and there is nothing sensible to do but count it.
    ++m_counters.unreadable;
    return;
  }
}

void Client::TakeFrame(const Frame& _frame, FrameSink& _sink)
{
  // A frame is applied when it is a full one, or when it is a delta against exactly the frame this
  // client last applied. Anything else - an old frame overtaken by a newer one, or a delta against
  // a baseline that never arrived - is dropped, and the host learns from the unmoved acknowledgement
  // that its next frame must be encoded against something this client has (§5.3).
  const bool full = _frame.baselineSequence == NO_BASELINE;
  if (!full && _frame.baselineSequence != m_appliedSequence)
  {
    ++m_counters.framesSkipped;
    m_acknowledgementDue = true;
    return;
  }
  if (_frame.sequence <= m_appliedSequence)
  {
    ++m_counters.framesSkipped;
    return;
  }
  m_appliedSequence = _frame.sequence;
  m_simulationTick = _frame.tick;
  m_acknowledgementDue = true;
  ++m_counters.framesApplied;
  _sink.Apply(_frame);
}

void Client::SendOrdersAndAck(std::uint32_t _tick)
{
  std::vector<OrderMessage> due;
  m_orders.Due(_tick, due);
  const bool heartbeat = m_liveness.HeartbeatDue(_tick);
  // A frame applied and not yet acknowledged is the third reason to send. It is the important one:
  // the host encodes against the newest frame this client has acknowledged, so a client that only
  // spoke on the heartbeat would be sent a full frame every time instead of a delta - the whole
  // saving of §5.3 - and would age its own baseline out of the history within four seconds.
  const bool acknowledgementDue = m_acknowledgementDue;
  if (due.empty() && !heartbeat && !acknowledgementDue)
  {
    return;
  }

  // The acknowledgement rides every datagram this client sends, whichever message carries it: what
  // frame it has applied, and what orders it has received. An Orders message with nothing in it
  // would be a heartbeat that cost more, so the two are one decision.
  Ack ack{};
  ack.frameSequence = m_appliedSequence;
  ack.orderSequence = m_orders.Acknowledgement();

  Neuron::ByteWriter payload;
  if (due.empty())
  {
    Heartbeat beat{};
    beat.ack = ack;
    beat.tick = m_simulationTick;
    Write(payload, beat);
  }
  else
  {
    Orders orders{};
    orders.ack = ack;
    // A datagram carries at most what it can hold; the rest are due again next pass, which the
    // resend timer takes care of without a second path.
    const std::size_t count = std::min(due.size(), static_cast<std::size_t>(MAX_ORDERS_IN_ONE_MESSAGE));
    orders.orders.assign(due.begin(), due.begin() + static_cast<std::ptrdiff_t>(count));
    Write(payload, orders);
  }
  SendPayload(payload, _tick);
  m_acknowledgementDue = false;
}

} // namespace Frontier
