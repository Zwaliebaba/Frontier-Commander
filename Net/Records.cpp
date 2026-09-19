#include "pch.h"

#include "Records.h"

namespace Frontier
{

namespace
{

/// An enumeration whose value the stream chose, refused rather than cast when it names something
/// the enumeration does not have. Every enumeration on this wire goes through here: a client that
/// can make a host hold a StructurePhase of 200 has found a way past every switch in the tree.
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

[[nodiscard]] bool ReadStances(Neuron::ByteReader& _reader, OrderAndStances& _out) noexcept
{
  std::uint8_t packed = 0;
  return _reader.Read(packed) && UnpackOrderAndStances(packed, _out);
}

} // namespace

// ── DeviceState ─────────────────────────────────────────────────────────────────────────────

void Write(Neuron::ByteWriter& _writer, const DeviceState& _record)
{
  _writer.Write(_record.id);
  _writer.Write(_record.design);
  _writer.Write(_record.seat);
  _writer.Write(_record.x);
  _writer.Write(_record.y);
  _writer.Write(_record.z);
  _writer.Write(_record.heading);
  _writer.Write(_record.hitPoints);
  _writer.Write(_record.rank);
  _writer.Write(_record.target);
  _writer.Write(_record.targetKind);
  _writer.Write(PackOrderAndStances(_record.stances));
}

bool Read(Neuron::ByteReader& _reader, DeviceState& _out)
{
  DeviceState record{};
  if (!_reader.Read(record.id) || !_reader.Read(record.design) || !_reader.Read(record.seat) || !_reader.Read(record.x) ||
      !_reader.Read(record.y) || !_reader.Read(record.z) || !_reader.Read(record.heading) || !_reader.Read(record.hitPoints) ||
      !_reader.Read(record.rank) || !_reader.Read(record.target) || !ReadEnum(_reader, record.targetKind, OBJECT_KIND_COUNT) ||
      !ReadStances(_reader, record.stances))
  {
    return false;
  }
  if (record.rank >= RANK_COUNT)
  {
    return false;
  }
  _out = record;
  return true;
}

// ── DeviceChange ────────────────────────────────────────────────────────────────────────────

void Write(Neuron::ByteWriter& _writer, const DeviceChange& _record)
{
  _writer.Write(_record.id);
  _writer.Write(_record.mask);
  if ((_record.mask & static_cast<std::uint8_t>(DeviceField::Position)) != 0)
  {
    _writer.Write(_record.deltaX);
    _writer.Write(_record.deltaY);
    _writer.Write(_record.deltaZ);
  }
  if ((_record.mask & static_cast<std::uint8_t>(DeviceField::Heading)) != 0)
  {
    _writer.Write(_record.heading);
  }
  if ((_record.mask & static_cast<std::uint8_t>(DeviceField::HitPoints)) != 0)
  {
    _writer.Write(_record.hitPoints);
  }
  if ((_record.mask & static_cast<std::uint8_t>(DeviceField::Stances)) != 0)
  {
    _writer.Write(PackOrderAndStances(_record.stances));
  }
}

bool Read(Neuron::ByteReader& _reader, DeviceChange& _out)
{
  DeviceChange record{};
  if (!_reader.Read(record.id) || !_reader.Read(record.mask))
  {
    return false;
  }
  // A bit the mask has and this version does not is a stream from a newer protocol, and the fields
  // after it cannot be found: refused here rather than read as whatever follows.
  constexpr std::uint8_t KNOWN = static_cast<std::uint8_t>(DeviceField::Position) | static_cast<std::uint8_t>(DeviceField::Heading) |
                                 static_cast<std::uint8_t>(DeviceField::HitPoints) | static_cast<std::uint8_t>(DeviceField::Stances);
  if ((record.mask & ~KNOWN) != 0)
  {
    return false;
  }
  if ((record.mask & static_cast<std::uint8_t>(DeviceField::Position)) != 0 &&
      (!_reader.Read(record.deltaX) || !_reader.Read(record.deltaY) || !_reader.Read(record.deltaZ)))
  {
    return false;
  }
  if ((record.mask & static_cast<std::uint8_t>(DeviceField::Heading)) != 0 && !_reader.Read(record.heading))
  {
    return false;
  }
  if ((record.mask & static_cast<std::uint8_t>(DeviceField::HitPoints)) != 0 && !_reader.Read(record.hitPoints))
  {
    return false;
  }
  if ((record.mask & static_cast<std::uint8_t>(DeviceField::Stances)) != 0 && !ReadStances(_reader, record.stances))
  {
    return false;
  }
  _out = record;
  return true;
}

// ── StructureState ──────────────────────────────────────────────────────────────────────────

void Write(Neuron::ByteWriter& _writer, const StructureState& _record)
{
  _writer.Write(_record.id);
  _writer.Write(_record.design);
  _writer.Write(_record.seat);
  _writer.Write(_record.cellX);
  _writer.Write(_record.cellY);
  _writer.Write(_record.y);
  _writer.Write(_record.phase);
  _writer.Write(_record.hitPoints);
  _writer.Write(_record.buildPercent);
  _writer.Write(_record.moduleCount);
  for (const std::uint8_t module : _record.modules)
  {
    _writer.Write(module);
  }
}

bool Read(Neuron::ByteReader& _reader, StructureState& _out)
{
  StructureState record{};
  if (!_reader.Read(record.id) || !_reader.Read(record.design) || !_reader.Read(record.seat) || !_reader.Read(record.cellX) ||
      !_reader.Read(record.cellY) || !_reader.Read(record.y) || !ReadEnum(_reader, record.phase, STRUCTURE_PHASE_COUNT) ||
      !_reader.Read(record.hitPoints) || !_reader.Read(record.buildPercent) || !_reader.Read(record.moduleCount))
  {
    return false;
  }
  if (record.buildPercent > 100 || record.moduleCount > MAX_STRUCTURE_MODULES)
  {
    return false;
  }
  for (std::uint8_t& module : record.modules)
  {
    if (!_reader.Read(module))
    {
      return false;
    }
  }
  _out = record;
  return true;
}

// ── WreckState ──────────────────────────────────────────────────────────────────────────────

void Write(Neuron::ByteWriter& _writer, const WreckState& _record)
{
  _writer.Write(_record.id);
  _writer.Write(_record.design);
  _writer.Write(_record.seat);
  _writer.Write(_record.origin);
  _writer.Write(_record.x);
  _writer.Write(_record.y);
  _writer.Write(_record.z);
  _writer.Write(_record.heading);
}

bool Read(Neuron::ByteReader& _reader, WreckState& _out)
{
  WreckState record{};
  if (!_reader.Read(record.id) || !_reader.Read(record.design) || !_reader.Read(record.seat) || !_reader.Read(record.origin) ||
      !_reader.Read(record.x) || !_reader.Read(record.y) || !_reader.Read(record.z) || !_reader.Read(record.heading))
  {
    return false;
  }
  if (record.origin >= OBJECT_KIND_COUNT)
  {
    return false;
  }
  _out = record;
  return true;
}

// ── FeatureState ────────────────────────────────────────────────────────────────────────────

void Write(Neuron::ByteWriter& _writer, const FeatureState& _record)
{
  _writer.Write(_record.id);
  _writer.Write(_record.design);
  _writer.Write(_record.cellX);
  _writer.Write(_record.cellY);
  _writer.Write(_record.y);
  _writer.Write(_record.heading);
}

bool Read(Neuron::ByteReader& _reader, FeatureState& _out)
{
  FeatureState record{};
  if (!_reader.Read(record.id) || !_reader.Read(record.design) || !_reader.Read(record.cellX) || !_reader.Read(record.cellY) ||
      !_reader.Read(record.y) || !_reader.Read(record.heading))
  {
    return false;
  }
  _out = record;
  return true;
}

// ── SeatState ───────────────────────────────────────────────────────────────────────────────

void Write(Neuron::ByteWriter& _writer, const SeatState& _record)
{
  _writer.Write(_record.seat);
  _writer.Write(_record.powerHundredths);
  _writer.Write(_record.stockpileCapHundredths);
  _writer.Write(_record.extractedHundredths);
  _writer.Write(_record.researchItem);
  _writer.Write(_record.researchRemainingTicks);
  _writer.Write(_record.victory);
  _writer.Write(_record.deviceCount);
  _writer.Write(_record.deviceCap);
  _writer.Write(_record.structureCount);
  _writer.Write(_record.structureCap);
}

bool Read(Neuron::ByteReader& _reader, SeatState& _out)
{
  SeatState record{};
  if (!_reader.Read(record.seat) || !_reader.Read(record.powerHundredths) || !_reader.Read(record.stockpileCapHundredths) ||
      !_reader.Read(record.extractedHundredths) || !_reader.Read(record.researchItem) || !_reader.Read(record.researchRemainingTicks) ||
      !_reader.Read(record.victory) || !_reader.Read(record.deviceCount) || !_reader.Read(record.deviceCap) ||
      !_reader.Read(record.structureCount) || !_reader.Read(record.structureCap))
  {
    return false;
  }
  if (record.seat >= MAX_SEATS || record.victory >= VICTORY_STATE_COUNT)
  {
    return false;
  }
  _out = record;
  return true;
}

// ── DesignState ─────────────────────────────────────────────────────────────────────────────

void Write(Neuron::ByteWriter& _writer, const DesignState& _record)
{
  _writer.Write(_record.seat);
  _writer.Write(_record.index);
  _writer.Write(_record.chassis);
  _writer.Write(_record.drive);
  _writer.Write(_record.moduleCount);
  for (const std::uint32_t module : _record.modules)
  {
    _writer.Write(module);
  }
}

bool Read(Neuron::ByteReader& _reader, DesignState& _out)
{
  DesignState record{};
  if (!_reader.Read(record.seat) || !_reader.Read(record.index) || !_reader.Read(record.chassis) || !_reader.Read(record.drive) ||
      !_reader.Read(record.moduleCount))
  {
    return false;
  }
  if (record.seat >= MAX_SEATS || record.moduleCount > MAX_MOUNTS || record.index >= MAX_SAVED_DESIGNS)
  {
    return false;
  }
  for (std::uint32_t& module : record.modules)
  {
    if (!_reader.Read(module))
    {
      return false;
    }
  }
  _out = record;
  return true;
}

// ── Event ───────────────────────────────────────────────────────────────────────────────────

void Write(Neuron::ByteWriter& _writer, const Event& _record)
{
  _writer.Write(_record.kind);
  _writer.Write(_record.source);
  _writer.Write(_record.sourceKind);
  _writer.Write(_record.target);
  _writer.Write(_record.targetKind);
  _writer.Write(_record.x);
  _writer.Write(_record.y);
  _writer.Write(_record.z);
  _writer.Write(_record.tick);
}

bool Read(Neuron::ByteReader& _reader, Event& _out)
{
  Event record{};
  if (!ReadEnum(_reader, record.kind, EVENT_KIND_COUNT) || !_reader.Read(record.source) ||
      !ReadEnum(_reader, record.sourceKind, OBJECT_KIND_COUNT) || !_reader.Read(record.target) ||
      !ReadEnum(_reader, record.targetKind, OBJECT_KIND_COUNT) || !_reader.Read(record.x) || !_reader.Read(record.y) ||
      !_reader.Read(record.z) || !_reader.Read(record.tick))
  {
    return false;
  }
  _out = record;
  return true;
}

// ── FogDelta ────────────────────────────────────────────────────────────────────────────────

void Write(Neuron::ByteWriter& _writer, const FogDelta& _record)
{
  _writer.Write(_record.firstCell);
  _writer.Write(_record.cells);
  _writer.Write(_record.state);
}

bool Read(Neuron::ByteReader& _reader, FogDelta& _out)
{
  FogDelta record{};
  if (!_reader.Read(record.firstCell) || !_reader.Read(record.cells) || !ReadEnum(_reader, record.state, FOG_STATE_COUNT))
  {
    return false;
  }
  if (record.cells == 0)
  {
    return false; // A run of nothing is a record that says nothing and costs seven bytes.
  }
  _out = record;
  return true;
}

// ── OrderMessage ────────────────────────────────────────────────────────────────────────────

void Write(Neuron::ByteWriter& _writer, const OrderMessage& _record)
{
  _writer.Write(_record.sequence);
  WriteOrder(_writer, _record.order);
}

bool Read(Neuron::ByteReader& _reader, OrderMessage& _out)
{
  OrderMessage record{};
  if (!_reader.Read(record.sequence) || !ReadOrder(_reader, record.order))
  {
    return false;
  }
  _out = record;
  return true;
}

} // namespace Frontier
