#include "pch.h"

#include "Messages.h"

#include <algorithm>
#include <span>
#include <string>

namespace Frontier
{

namespace
{

template <class Enum> [[nodiscard]] bool ReadEnum(Neuron::ByteReader& _reader, Enum& _out, std::uint8_t _count) noexcept
{
  std::uint8_t value = 0;
  if (!_reader.Read(value) || value >= _count)
  {
    return false;
  }
  _out = static_cast<Enum>(value);
  return true;
}

void WriteKind(Neuron::ByteWriter& _writer, MessageKind _kind)
{
  _writer.Write(_kind);
}

/// A list of records with a 32-bit count. The count is written from the vector's size, so a
/// message can never claim more than it carries.
template <class T> void WriteList(Neuron::ByteWriter& _writer, const std::vector<T>& _list)
{
  _writer.Write(static_cast<std::uint32_t>(_list.size()));
  for (const T& record : _list)
  {
    Write(_writer, record);
  }
}

/// The same, reading, with the bound the CALLER gives rather than the one the stream would like.
/// The vector is grown one record at a time: a count of a million that the datagram cannot back up
/// fails on the first read rather than after a million-record reserve.
template <class T> [[nodiscard]] bool ReadList(Neuron::ByteReader& _reader, std::vector<T>& _out, std::uint32_t _max)
{
  std::uint32_t count = 0;
  if (!_reader.Read(count) || count > _max)
  {
    return false;
  }
  _out.clear();
  for (std::uint32_t index = 0; index < count; ++index)
  {
    T record{};
    if (!Read(_reader, record))
    {
      return false;
    }
    _out.push_back(record);
  }
  return true;
}

/// The landscape's public half (§5.2): the definition, and never the flatten deltas. Written here
/// rather than shared with Sim/Snapshot.cpp because they are two streams with two versions and
/// two payloads, and a snapshot that gained a field would otherwise break every client.
void WriteLandscapeDefinition(Neuron::ByteWriter& _writer, const LandscapeDefinition& _definition)
{
  _writer.Write(_definition.version);
  _writer.Write(_definition.sizeClass);
  _writer.Write(_definition.cellsPerSide);
  _writer.Write(_definition.seed);
  _writer.WriteSpan(std::as_bytes(std::span<const char>(_definition.palette.data(), _definition.palette.size())));
  _writer.Write(static_cast<std::uint32_t>(_definition.tiles.size()));
  for (const LandscapeTile& tile : _definition.tiles)
  {
    _writer.Write(tile.x);
    _writer.Write(tile.y);
    _writer.Write(tile.extent);
    _writer.Write(tile.fractalDimensionHundredths);
    _writer.Write(tile.amplitude);
    _writer.Write(tile.desiredHeight);
    _writer.Write(tile.heightShift);
    _writer.Write(tile.lowlandExponentHundredths);
    _writer.Write(tile.method);
    _writer.Write(tile.edgeFalloff);
    _writer.WriteSpan(std::as_bytes(std::span<const char>(tile.palette.data(), tile.palette.size())));
  }
  for (const std::vector<CellPosition>* positions : {&_definition.starts, &_definition.deposits})
  {
    _writer.Write(static_cast<std::uint32_t>(positions->size()));
    for (const CellPosition& position : *positions)
    {
      _writer.Write(position.x);
      _writer.Write(position.y);
    }
  }
}

[[nodiscard]] bool ReadText(Neuron::ByteReader& _reader, std::size_t _max, std::string& _out)
{
  std::span<const std::byte> bytes;
  if (!_reader.ReadSpan(_max, bytes))
  {
    return false;
  }
  _out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  return true;
}

[[nodiscard]] bool ReadPositions(Neuron::ByteReader& _reader, std::uint32_t _max, std::vector<CellPosition>& _out)
{
  std::uint32_t count = 0;
  if (!_reader.Read(count) || count > _max)
  {
    return false;
  }
  _out.clear();
  for (std::uint32_t index = 0; index < count; ++index)
  {
    CellPosition position{};
    if (!_reader.Read(position.x) || !_reader.Read(position.y))
    {
      return false;
    }
    _out.push_back(position);
  }
  return true;
}

[[nodiscard]] bool ReadLandscapeDefinition(Neuron::ByteReader& _reader, LandscapeDefinition& _out)
{
  LandscapeDefinition definition{};
  if (!_reader.Read(definition.version) || !ReadEnum(_reader, definition.sizeClass, SIZE_CLASS_COUNT) ||
      !_reader.Read(definition.cellsPerSide) || !_reader.Read(definition.seed) ||
      !ReadText(_reader, MAX_PALETTE_NAME_BYTES, definition.palette))
  {
    return false;
  }
  std::uint32_t tiles = 0;
  if (!_reader.Read(tiles) || tiles > MAX_LANDSCAPE_TILES)
  {
    return false;
  }
  for (std::uint32_t index = 0; index < tiles; ++index)
  {
    LandscapeTile tile{};
    if (!_reader.Read(tile.x) || !_reader.Read(tile.y) || !_reader.Read(tile.extent) || !_reader.Read(tile.fractalDimensionHundredths) ||
        !_reader.Read(tile.amplitude) || !_reader.Read(tile.desiredHeight) || !_reader.Read(tile.heightShift) ||
        !_reader.Read(tile.lowlandExponentHundredths) || !_reader.Read(tile.method) || !_reader.Read(tile.edgeFalloff) ||
        !ReadText(_reader, MAX_PALETTE_NAME_BYTES, tile.palette))
    {
      return false;
    }
    definition.tiles.push_back(std::move(tile));
  }
  if (!ReadPositions(_reader, MAX_SEATS, definition.starts) || !ReadPositions(_reader, MAX_LANDSCAPE_DEPOSITS, definition.deposits))
  {
    return false;
  }
  _out = std::move(definition);
  return true;
}

void WriteSettings(Neuron::ByteWriter& _writer, const MatchSettings& _settings)
{
  _writer.Write(_settings.seed);
  _writer.Write(_settings.sizeClass);
  _writer.Write(_settings.seatCount);
  _writer.Write(_settings.baseLevel);
  _writer.Write(_settings.powerLevel);
  _writer.Write(_settings.technologyTiers);
  _writer.Write(_settings.victory);
  _writer.Write(_settings.survivalTicks);
  _writer.Write(_settings.deviceCapLevel);
  for (const SeatSettings& seat : _settings.seats)
  {
    _writer.Write(seat.kind);
    _writer.Write(seat.alliance);
    _writer.WriteBool(seat.autoResearch);
  }
}

[[nodiscard]] bool ReadSettings(Neuron::ByteReader& _reader, MatchSettings& _out)
{
  MatchSettings settings{};
  if (!_reader.Read(settings.seed) || !ReadEnum(_reader, settings.sizeClass, SIZE_CLASS_COUNT) || !_reader.Read(settings.seatCount) ||
      !ReadEnum(_reader, settings.baseLevel, 3) || !ReadEnum(_reader, settings.powerLevel, 3) || !_reader.Read(settings.technologyTiers) ||
      !ReadEnum(_reader, settings.victory, 3) || !_reader.Read(settings.survivalTicks) || !ReadEnum(_reader, settings.deviceCapLevel, 3))
  {
    return false;
  }
  if (settings.seatCount < MIN_SEATS || settings.seatCount > MAX_SEATS)
  {
    return false;
  }
  for (SeatSettings& seat : settings.seats)
  {
    if (!ReadEnum(_reader, seat.kind, 3) || !_reader.Read(seat.alliance) || !_reader.ReadBool(seat.autoResearch))
    {
      return false;
    }
  }
  _out = settings;
  return true;
}

} // namespace

bool ReadMessageKind(Neuron::ByteReader& _reader, MessageKind& _out)
{
  return ReadEnum(_reader, _out, MESSAGE_KIND_COUNT);
}

// ── Join ────────────────────────────────────────────────────────────────────────────────────

void Write(Neuron::ByteWriter& _writer, const Join& _message)
{
  WriteKind(_writer, MessageKind::Join);
  _writer.Write(_message.protocolVersion);
  _writer.Write(_message.contentHash);
  _writer.Write(_message.token);
  _writer.Write(_message.nameBytes);
  for (const char letter : _message.name)
  {
    _writer.Write(static_cast<std::uint8_t>(letter));
  }
}

bool Read(Neuron::ByteReader& _reader, Join& _out)
{
  Join message{};
  if (!_reader.Read(message.protocolVersion) || !_reader.Read(message.contentHash) || !_reader.Read(message.token) ||
      !_reader.Read(message.nameBytes) || message.nameBytes > MAX_PLAYER_NAME_BYTES)
  {
    return false;
  }
  for (char& letter : message.name)
  {
    std::uint8_t value = 0;
    if (!_reader.Read(value))
    {
      return false;
    }
    letter = static_cast<char>(value);
  }
  _out = message;
  return true;
}

// ── JoinAccepted ────────────────────────────────────────────────────────────────────────────

void Write(Neuron::ByteWriter& _writer, const JoinAccepted& _message)
{
  WriteKind(_writer, MessageKind::JoinAccepted);
  _writer.Write(_message.seat);
  _writer.Write(_message.tick);
  WriteSettings(_writer, _message.settings);
  WriteLandscapeDefinition(_writer, _message.landscape);
}

bool Read(Neuron::ByteReader& _reader, JoinAccepted& _out)
{
  JoinAccepted message{};
  if (!_reader.Read(message.seat) || message.seat >= MAX_SEATS || !_reader.Read(message.tick) || !ReadSettings(_reader, message.settings) ||
      !ReadLandscapeDefinition(_reader, message.landscape))
  {
    return false;
  }
  _out = std::move(message);
  return true;
}

// ── JoinRefused ─────────────────────────────────────────────────────────────────────────────

void Write(Neuron::ByteWriter& _writer, const JoinRefused& _message)
{
  WriteKind(_writer, MessageKind::JoinRefused);
  _writer.Write(_message.reason);
  _writer.Write(_message.hostProtocolVersion);
  _writer.Write(_message.hostContentHash);
}

bool Read(Neuron::ByteReader& _reader, JoinRefused& _out)
{
  JoinRefused message{};
  if (!ReadEnum(_reader, message.reason, REFUSAL_REASON_COUNT) || !_reader.Read(message.hostProtocolVersion) ||
      !_reader.Read(message.hostContentHash))
  {
    return false;
  }
  _out = message;
  return true;
}

// ── Frame ───────────────────────────────────────────────────────────────────────────────────

void Write(Neuron::ByteWriter& _writer, const Frame& _message)
{
  WriteKind(_writer, MessageKind::Frame);
  _writer.Write(_message.sequence);
  _writer.Write(_message.baselineSequence);
  _writer.Write(_message.tick);
  WriteList(_writer, _message.createdDevices);
  WriteList(_writer, _message.createdStructures);
  WriteList(_writer, _message.createdWrecks);
  WriteList(_writer, _message.createdFeatures);
  WriteList(_writer, _message.designs);
  WriteList(_writer, _message.changedDevices);
  WriteList(_writer, _message.changedStructures);
  _writer.Write(static_cast<std::uint32_t>(_message.removed.size()));
  for (const std::uint32_t id : _message.removed)
  {
    _writer.Write(id);
  }
  WriteList(_writer, _message.events);
  WriteList(_writer, _message.fog);
  Write(_writer, _message.seat);
}

bool Read(Neuron::ByteReader& _reader, Frame& _out)
{
  Frame message{};
  if (!_reader.Read(message.sequence) || !_reader.Read(message.baselineSequence) || !_reader.Read(message.tick))
  {
    return false;
  }
  if (!ReadList(_reader, message.createdDevices, MAX_FRAME_OBJECTS) || !ReadList(_reader, message.createdStructures, MAX_FRAME_OBJECTS) ||
      !ReadList(_reader, message.createdWrecks, MAX_FRAME_OBJECTS) || !ReadList(_reader, message.createdFeatures, MAX_FRAME_OBJECTS) ||
      !ReadList(_reader, message.designs, MAX_FRAME_DESIGNS) || !ReadList(_reader, message.changedDevices, MAX_FRAME_OBJECTS) ||
      !ReadList(_reader, message.changedStructures, MAX_FRAME_OBJECTS))
  {
    return false;
  }
  std::uint32_t removed = 0;
  if (!_reader.Read(removed) || removed > MAX_FRAME_OBJECTS)
  {
    return false;
  }
  for (std::uint32_t index = 0; index < removed; ++index)
  {
    std::uint32_t id = 0;
    if (!_reader.Read(id))
    {
      return false;
    }
    message.removed.push_back(id);
  }
  if (!ReadList(_reader, message.events, MAX_FRAME_EVENTS) || !ReadList(_reader, message.fog, MAX_FRAME_FOG_DELTAS) ||
      !Read(_reader, message.seat))
  {
    return false;
  }
  _out = std::move(message);
  return true;
}

// ── Fragment ────────────────────────────────────────────────────────────────────────────────

void Write(Neuron::ByteWriter& _writer, const Fragment& _message)
{
  WriteKind(_writer, MessageKind::Fragment);
  _writer.Write(_message.frameSequence);
  _writer.Write(_message.index);
  _writer.Write(_message.count);
  _writer.WriteSpan(_message.bytes);
}

bool Read(Neuron::ByteReader& _reader, Fragment& _out)
{
  Fragment message{};
  std::span<const std::byte> bytes;
  if (!_reader.Read(message.frameSequence) || !_reader.Read(message.index) || !_reader.Read(message.count) ||
      !_reader.ReadSpan(MAX_FRAME_PAYLOAD_BYTES, bytes))
  {
    return false;
  }
  // An index outside its own count names a piece of a frame that has no such piece, and a count of
  // zero names a frame with no pieces at all.
  if (message.count == 0 || message.count > MAX_FRAGMENTS || message.index >= message.count)
  {
    return false;
  }
  message.bytes.assign(bytes.begin(), bytes.end());
  _out = std::move(message);
  return true;
}

// ── Ack, Orders, Heartbeat ──────────────────────────────────────────────────────────────────

void Write(Neuron::ByteWriter& _writer, const Ack& _message)
{
  WriteKind(_writer, MessageKind::Ack);
  _writer.Write(_message.frameSequence);
  _writer.Write(_message.orderSequence);
}

bool Read(Neuron::ByteReader& _reader, Ack& _out)
{
  Ack message{};
  if (!_reader.Read(message.frameSequence) || !_reader.Read(message.orderSequence))
  {
    return false;
  }
  _out = message;
  return true;
}

void Write(Neuron::ByteWriter& _writer, const Orders& _message)
{
  WriteKind(_writer, MessageKind::Orders);
  _writer.Write(_message.ack.frameSequence);
  _writer.Write(_message.ack.orderSequence);
  WriteList(_writer, _message.orders);
}

bool Read(Neuron::ByteReader& _reader, Orders& _out)
{
  Orders message{};
  if (!_reader.Read(message.ack.frameSequence) || !_reader.Read(message.ack.orderSequence) ||
      !ReadList(_reader, message.orders, MAX_ORDERS_IN_ONE_MESSAGE))
  {
    return false;
  }
  _out = std::move(message);
  return true;
}

void Write(Neuron::ByteWriter& _writer, const Heartbeat& _message)
{
  WriteKind(_writer, MessageKind::Heartbeat);
  _writer.Write(_message.ack.frameSequence);
  _writer.Write(_message.ack.orderSequence);
  _writer.Write(_message.tick);
}

bool Read(Neuron::ByteReader& _reader, Heartbeat& _out)
{
  Heartbeat message{};
  if (!_reader.Read(message.ack.frameSequence) || !_reader.Read(message.ack.orderSequence) || !_reader.Read(message.tick))
  {
    return false;
  }
  _out = message;
  return true;
}

} // namespace Frontier
