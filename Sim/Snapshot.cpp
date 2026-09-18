#include "pch.h"

#include "Snapshot.h"

#include "Hash.h"

#include <utility>

namespace Frontier
{

namespace
{

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
  for (const SeatSettings& seat : _settings.seats)
  {
    _writer.Write(seat.kind);
    _writer.Write(seat.alliance);
  }
}

template <class Enum> [[nodiscard]] bool ReadEnum(Neuron::ByteReader& _reader, Enum& _out, std::uint8_t _count)
{
  std::uint8_t value = 0;
  if (!_reader.Read(value) || value >= _count)
  {
    return false;
  }
  _out = static_cast<Enum>(value);
  return true;
}

[[nodiscard]] bool ReadSettings(Neuron::ByteReader& _reader, MatchSettings& _out)
{
  MatchSettings settings{};
  if (!_reader.Read(settings.seed) || !ReadEnum(_reader, settings.sizeClass, 4) || !_reader.Read(settings.seatCount) ||
      !ReadEnum(_reader, settings.baseLevel, 3) || !ReadEnum(_reader, settings.powerLevel, 3) || !_reader.Read(settings.technologyTiers) ||
      !ReadEnum(_reader, settings.victory, 3) || !_reader.Read(settings.survivalTicks))
  {
    return false;
  }
  if (settings.seatCount < MIN_SEATS || settings.seatCount > MAX_SEATS)
  {
    return false;
  }
  for (SeatSettings& seat : settings.seats)
  {
    if (!ReadEnum(_reader, seat.kind, 3) || !_reader.Read(seat.alliance))
    {
      return false;
    }
  }
  _out = settings;
  return true;
}

void WriteLandscape(Neuron::ByteWriter& _writer, const Landscape& _landscape)
{
  _writer.WriteBool(_landscape.Created());
  if (!_landscape.Created())
  {
    return;
  }
  const LandscapeDefinition& definition = _landscape.Definition();
  _writer.Write(definition.version);
  _writer.Write(definition.sizeClass);
  _writer.Write(definition.cellsPerSide);
  _writer.Write(definition.seed);
  _writer.WriteSpan(std::as_bytes(std::span<const char>(definition.palette.data(), definition.palette.size())));
  _writer.Write(static_cast<std::uint32_t>(definition.tiles.size()));
  for (const LandscapeTile& tile : definition.tiles)
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
  for (const std::vector<CellPosition>* positions : {&definition.starts, &definition.deposits})
  {
    _writer.Write(static_cast<std::uint32_t>(positions->size()));
    for (const CellPosition& position : *positions)
    {
      _writer.Write(position.x);
      _writer.Write(position.y);
    }
  }
  _writer.Write(static_cast<std::uint32_t>(_landscape.Deltas().size()));
  for (const HeightDelta& delta : _landscape.Deltas())
  {
    _writer.Write(delta.x);
    _writer.Write(delta.y);
    _writer.Write(delta.width);
    _writer.Write(delta.height);
    for (const std::int16_t height : delta.heights)
    {
      _writer.Write(height);
    }
  }
}

[[nodiscard]] bool ReadPositions(Neuron::ByteReader& _reader, std::vector<CellPosition>& _out)
{
  std::uint32_t count = 0;
  if (!_reader.Read(count) || count > Snapshot::MAX_POSITIONS)
  {
    return false;
  }
  _out.resize(count);
  for (CellPosition& position : _out)
  {
    if (!_reader.Read(position.x) || !_reader.Read(position.y))
    {
      return false;
    }
  }
  return true;
}

/// False when the stream is refused; _landscape is left untouched when there is none to restore.
[[nodiscard]] bool ReadLandscape(Neuron::ByteReader& _reader, Landscape& _landscape)
{
  bool created = false;
  if (!_reader.ReadBool(created))
  {
    return false;
  }
  if (!created)
  {
    return true;
  }
  LandscapeDefinition definition{};
  std::span<const std::byte> palette;
  std::uint32_t tileCount = 0;
  if (!_reader.Read(definition.version) || !ReadEnum(_reader, definition.sizeClass, 4) || !_reader.Read(definition.cellsPerSide) ||
      !_reader.Read(definition.seed) || !_reader.ReadSpan(Snapshot::MAX_PALETTE_BYTES, palette) || !_reader.Read(tileCount) ||
      tileCount > Snapshot::MAX_TILES)
  {
    return false;
  }
  definition.palette.assign(reinterpret_cast<const char*>(palette.data()), palette.size());
  definition.tiles.resize(tileCount);
  for (LandscapeTile& tile : definition.tiles)
  {
    std::span<const std::byte> tilePalette;
    if (!_reader.Read(tile.x) || !_reader.Read(tile.y) || !_reader.Read(tile.extent) || !_reader.Read(tile.fractalDimensionHundredths) ||
        !_reader.Read(tile.amplitude) || !_reader.Read(tile.desiredHeight) || !_reader.Read(tile.heightShift) ||
        !_reader.Read(tile.lowlandExponentHundredths) || !_reader.Read(tile.method) || !_reader.Read(tile.edgeFalloff) ||
        !_reader.ReadSpan(Snapshot::MAX_PALETTE_BYTES, tilePalette))
    {
      return false;
    }
    tile.palette.assign(reinterpret_cast<const char*>(tilePalette.data()), tilePalette.size());
  }
  if (!ReadPositions(_reader, definition.starts) || !ReadPositions(_reader, definition.deposits))
  {
    return false;
  }
  std::uint32_t deltaCount = 0;
  if (!_reader.Read(deltaCount) || deltaCount > Snapshot::MAX_DELTAS)
  {
    return false;
  }
  const std::uint64_t side = SamplesPerSide(definition.cellsPerSide);
  std::vector<HeightDelta> deltas(deltaCount);
  for (HeightDelta& delta : deltas)
  {
    if (!_reader.Read(delta.x) || !_reader.Read(delta.y) || !_reader.Read(delta.width) || !_reader.Read(delta.height) ||
        delta.width > side || delta.height > side)
    {
      return false;
    }
    delta.heights.resize(static_cast<std::size_t>(delta.width) * delta.height);
    for (std::int16_t& height : delta.heights)
    {
      if (!_reader.Read(height))
      {
        return false;
      }
    }
  }
  return _landscape.Restore(definition, deltas);
}

} // namespace

void Snapshot::Write(const Sim& _sim, Neuron::ByteWriter& _writer)
{
  const std::size_t start = _writer.Size();
  _writer.WriteHeader({SNAPSHOT_MAGIC, SNAPSHOT_VERSION});
  WriteSettings(_writer, _sim.m_settings);
  _writer.Write(_sim.m_tick);
  for (const std::uint32_t word : _sim.m_random.GetState())
  {
    _writer.Write(word);
  }
  _writer.Write(static_cast<std::uint8_t>(_sim.m_seats.size()));
  for (const Seat& seat : _sim.m_seats)
  {
    _writer.Write(seat.kind);
    _writer.Write(seat.alliance);
    _writer.Write(seat.powerHundredths);
    _writer.WriteBool(seat.defeated);
  }
  WriteLandscape(_writer, _sim.m_landscape);
  // The object maps go here, one count and its records per kind, as the systems arrive (ADR-003).
  _writer.Write(_sim.m_lastRoll);
  _writer.Write(_sim.m_appliedOrders);
  _writer.Write(_sim.m_droppedOrders);
  _writer.WriteBool(_sim.m_finished);
  _writer.Write(_sim.m_winningAlliance);
  _writer.WriteBool(_sim.m_publishDue);
  _writer.Write(_sim.m_hash);
  _writer.Write(_sim.m_orders.NextArrival());
  _writer.Write(static_cast<std::uint32_t>(_sim.m_orders.Size()));
  for (const OrderQueue::Entry& entry : _sim.m_orders.Entries())
  {
    _writer.Write(entry.arrival);
    WriteOrder(_writer, entry.order);
  }
  // The digest of everything above, so that a short or altered stream is refused rather than read.
  const std::span<const std::byte> written = _writer.Bytes().subspan(start);
  _writer.Write(Neuron::Fnv1a64(written));
}

std::vector<std::byte> Snapshot::Write(const Sim& _sim)
{
  Neuron::ByteWriter writer;
  Write(_sim, writer);
  return writer.Release();
}

std::optional<Sim> Snapshot::Read(std::span<const std::byte> _bytes)
{
  Neuron::ByteReader reader(_bytes);
  Neuron::StreamHeader header{};
  if (!reader.ReadHeader(SNAPSHOT_MAGIC, SNAPSHOT_VERSION, SNAPSHOT_VERSION, header))
  {
    return std::nullopt;
  }
  MatchSettings settings{};
  if (!ReadSettings(reader, settings))
  {
    return std::nullopt;
  }
  Sim sim(settings);
  Neuron::Random::State state{};
  if (!reader.Read(sim.m_tick))
  {
    return std::nullopt;
  }
  for (std::uint32_t& word : state)
  {
    if (!reader.Read(word))
    {
      return std::nullopt;
    }
  }
  sim.m_random.SetState(state);
  std::uint8_t seatCount = 0;
  if (!reader.Read(seatCount) || seatCount != sim.m_seats.size())
  {
    return std::nullopt;
  }
  for (Seat& seat : sim.m_seats)
  {
    if (!ReadEnum(reader, seat.kind, 3) || !reader.Read(seat.alliance) || !reader.Read(seat.powerHundredths) ||
        !reader.ReadBool(seat.defeated))
    {
      return std::nullopt;
    }
  }
  if (!ReadLandscape(reader, sim.m_landscape))
  {
    return std::nullopt;
  }
  std::uint32_t nextArrival = 0;
  std::uint32_t pending = 0;
  if (!reader.Read(sim.m_lastRoll) || !reader.Read(sim.m_appliedOrders) || !reader.Read(sim.m_droppedOrders) ||
      !reader.ReadBool(sim.m_finished) || !reader.Read(sim.m_winningAlliance) || !reader.ReadBool(sim.m_publishDue) ||
      !reader.Read(sim.m_hash) || !reader.Read(nextArrival) || !reader.Read(pending) || pending > MAX_PENDING_ORDERS)
  {
    return std::nullopt;
  }
  std::vector<OrderQueue::Entry> entries;
  entries.reserve(pending);
  for (std::uint32_t index = 0; index < pending; ++index)
  {
    OrderQueue::Entry entry{};
    if (!reader.Read(entry.arrival) || !ReadOrder(reader, entry.order))
    {
      return std::nullopt;
    }
    entries.push_back(entry);
  }
  sim.m_orders.Restore(nextArrival, std::move(entries));
  const std::size_t digestAt = reader.Position();
  std::uint64_t digest = 0;
  if (!reader.Read(digest) || !reader.AtEnd() || digest != Neuron::Fnv1a64(_bytes.subspan(0, digestAt)))
  {
    return std::nullopt;
  }
  return sim;
}

} // namespace Frontier
