#include "pch.h"

#include "Snapshot.h"

#include "ContentHash.h"

#include "Hash.h"

#include <type_traits>
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
  _writer.Write(_settings.deviceCapLevel);
  for (const SeatSettings& seat : _settings.seats)
  {
    _writer.Write(seat.kind);
    _writer.Write(seat.alliance);
    _writer.WriteBool(seat.autoResearch);
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

void WriteObjectId(Neuron::ByteWriter& _writer, ObjectId _id)
{
  _writer.Write(_id.value);
  _writer.Write(_id.kind);
}

[[nodiscard]] bool ReadObjectId(Neuron::ByteReader& _reader, ObjectId& _out)
{
  return _reader.Read(_out.value) && ReadEnum(_reader, _out.kind, OBJECT_KIND_COUNT);
}

/// A fog grid is three bytes per cell per seat, which is 25 MB on a Frontier landscape with eight
/// seats and would put the snapshot back in the business of carrying the map rather than its
/// definition (ADR-003). It is also almost entirely one value, because a commander explores a
/// fraction of a landscape, so it is written as runs: a length and a value, and an untouched grid
/// is one run. Deterministic, because the runs are a pure function of the values.
///
/// Templated on the value's type since S9 widened the viewer count to sixteen bits (Sim/FogGrid.h
/// says why), so the two grids of the pair no longer share one.
template <class T> void WriteRuns(Neuron::ByteWriter& _writer, std::span<const T> _values)
{
  const auto runEnd = [_values](std::size_t _from)
  {
    std::size_t end = _from + 1;
    while (end < _values.size() && _values[end] == _values[_from])
    {
      ++end;
    }
    return end;
  };
  std::uint32_t runs = 0;
  for (std::size_t index = 0; index < _values.size(); index = runEnd(index))
  {
    ++runs;
  }
  _writer.Write(static_cast<std::uint32_t>(_values.size()));
  _writer.Write(runs);
  for (std::size_t index = 0; index < _values.size();)
  {
    const std::size_t end = runEnd(index);
    _writer.Write(static_cast<std::uint32_t>(end - index));
    _writer.Write(_values[index]);
    index = end;
  }
}

/// Reads a run-length grid, checking that the runs account for exactly the cells claimed and that
/// every value is inside _valueCount, so that a hostile file cannot describe a grid that is not
/// one. The cell count is the caller's to compare against the other grid of the pair.
template <class T> [[nodiscard]] bool ReadRuns(Neuron::ByteReader& _reader, std::uint32_t _valueCount, std::vector<T>& _out)
{
  std::uint32_t cells = 0;
  std::uint32_t runs = 0;
  if (!_reader.Read(cells) || cells > Snapshot::MAX_FOG_CELLS || !_reader.Read(runs) || runs > cells)
  {
    return false;
  }
  _out.clear();
  _out.reserve(cells);
  for (std::uint32_t run = 0; run < runs; ++run)
  {
    std::uint32_t length = 0;
    T value{};
    if (!_reader.Read(length) || !_reader.Read(value) || static_cast<std::uint32_t>(value) >= _valueCount || length == 0 ||
        length > cells - static_cast<std::uint32_t>(_out.size()))
    {
      return false;
    }
    _out.insert(_out.end(), length, value);
  }
  return _out.size() == cells;
}

/// The seat in the order StateHash::AddSeat reads it, so that a reader checking one against the
/// other has one order to check.
void WriteSeat(Neuron::ByteWriter& _writer, const Seat& _seat)
{
  _writer.Write(_seat.kind);
  _writer.Write(_seat.alliance);
  _writer.Write(_seat.powerHundredths);
  _writer.Write(_seat.stockpileCapHundredths);
  _writer.Write(static_cast<std::uint32_t>(_seat.researchComplete.size()));
  for (const std::uint32_t item : _seat.researchComplete)
  {
    _writer.Write(item);
  }
  _writer.Write(static_cast<std::uint32_t>(_seat.researchActive.size()));
  for (const ResearchProgress& progress : _seat.researchActive)
  {
    WriteObjectId(_writer, progress.lab);
    _writer.Write(progress.item);
    _writer.Write(progress.remainingTicks);
  }
  _writer.Write(static_cast<std::uint32_t>(_seat.designs.size()));
  for (const DeviceDesign& design : _seat.designs)
  {
    _writer.Write(design.chassis);
    _writer.Write(design.drive);
    for (const std::uint32_t module : design.modules)
    {
      _writer.Write(module);
    }
    _writer.Write(design.moduleCount);
  }
  for (const std::int32_t percent : _seat.upgrades.chassisArmorPercent)
  {
    _writer.Write(percent);
  }
  for (const std::int32_t percent : _seat.upgrades.chassisHitPointPercent)
  {
    _writer.Write(percent);
  }
  for (const std::int32_t percent : _seat.upgrades.weaponDamagePercent)
  {
    _writer.Write(percent);
  }
  for (const std::int32_t percent : _seat.upgrades.weaponRatePercent)
  {
    _writer.Write(percent);
  }
  for (const std::int32_t percent : _seat.upgrades.weaponAccuracyPercent)
  {
    _writer.Write(percent);
  }
  _writer.Write(_seat.upgrades.extractorRatePercent);
  _writer.Write(_seat.upgrades.structureHitPointPercent);
  _writer.Write(static_cast<std::uint32_t>(_seat.production.size()));
  for (const ProductionEntry& entry : _seat.production)
  {
    WriteObjectId(_writer, entry.factory);
    _writer.Write(entry.design);
    _writer.Write(entry.remaining);
  }
  _writer.Write(_seat.deviceCount);
  _writer.Write(_seat.deviceCap);
  _writer.Write(_seat.structureCount);
  _writer.Write(_seat.structureCap);
  _writer.Write(_seat.fog.CellsPerSide());
  WriteRuns(_writer, _seat.fog.Viewers());
  static_assert(std::is_same_v<std::underlying_type_t<FogState>, std::uint8_t>, "the run encoding reads a fog state as its byte");
  WriteRuns(_writer, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(_seat.fog.States().data()), _seat.fog.Count()));
  _writer.Write(static_cast<std::uint32_t>(_seat.ghosts.Count()));
  for (const Ghost& ghost : _seat.ghosts.All())
  {
    WriteObjectId(_writer, ghost.structure);
    _writer.Write(ghost.seat);
    _writer.Write(ghost.design);
    _writer.Write(ghost.cellX);
    _writer.Write(ghost.cellY);
    _writer.Write(ghost.seenTick);
  }
  _writer.Write(static_cast<std::uint32_t>(_seat.rejections.size()));
  for (const OrderRejection& rejection : _seat.rejections)
  {
    _writer.Write(rejection.kind);
    _writer.Write(rejection.reason);
  }
  _writer.WriteBool(_seat.defeated);
  _writer.WriteBool(_seat.surrendered);
}

[[nodiscard]] bool ReadSeat(Neuron::ByteReader& _reader, Seat& _out)
{
  Seat seat{};
  std::uint32_t count = 0;
  if (!ReadEnum(_reader, seat.kind, 3) || !_reader.Read(seat.alliance) || !_reader.Read(seat.powerHundredths) ||
      !_reader.Read(seat.stockpileCapHundredths) || !_reader.Read(count) || count > Snapshot::MAX_RESEARCH)
  {
    return false;
  }
  seat.researchComplete.resize(count);
  for (std::uint32_t& item : seat.researchComplete)
  {
    if (!_reader.Read(item))
    {
      return false;
    }
  }
  if (!_reader.Read(count) || count > Snapshot::MAX_RESEARCH)
  {
    return false;
  }
  seat.researchActive.resize(count);
  for (ResearchProgress& progress : seat.researchActive)
  {
    if (!ReadObjectId(_reader, progress.lab) || !_reader.Read(progress.item) || !_reader.Read(progress.remainingTicks))
    {
      return false;
    }
  }
  if (!_reader.Read(count) || count > Snapshot::MAX_DESIGNS)
  {
    return false;
  }
  seat.designs.resize(count);
  for (DeviceDesign& design : seat.designs)
  {
    if (!_reader.Read(design.chassis) || !_reader.Read(design.drive))
    {
      return false;
    }
    for (std::uint32_t& module : design.modules)
    {
      if (!_reader.Read(module))
      {
        return false;
      }
    }
    if (!_reader.Read(design.moduleCount) || design.moduleCount > MAX_MOUNTS)
    {
      return false;
    }
  }
  const auto readPercents = [&_reader](auto& _percents)
  {
    for (std::int32_t& percent : _percents)
    {
      if (!_reader.Read(percent))
      {
        return false;
      }
    }
    return true;
  };
  if (!readPercents(seat.upgrades.chassisArmorPercent) || !readPercents(seat.upgrades.chassisHitPointPercent) ||
      !readPercents(seat.upgrades.weaponDamagePercent) || !readPercents(seat.upgrades.weaponRatePercent) ||
      !readPercents(seat.upgrades.weaponAccuracyPercent) || !_reader.Read(seat.upgrades.extractorRatePercent) ||
      !_reader.Read(seat.upgrades.structureHitPointPercent))
  {
    return false;
  }
  if (!_reader.Read(count) || count > MAX_PRODUCTION_ENTRIES)
  {
    return false;
  }
  seat.production.resize(count);
  for (ProductionEntry& entry : seat.production)
  {
    if (!ReadObjectId(_reader, entry.factory) || !_reader.Read(entry.design) || !_reader.Read(entry.remaining) ||
        entry.remaining > MAX_PRODUCTION_REPEAT)
    {
      return false;
    }
  }
  std::uint32_t cellsPerSide = 0;
  std::vector<std::uint16_t> viewers;
  std::vector<std::uint8_t> states;
  if (!_reader.Read(seat.deviceCount) || !_reader.Read(seat.deviceCap) || !_reader.Read(seat.structureCount) ||
      !_reader.Read(seat.structureCap) || !_reader.Read(cellsPerSide) || !ReadRuns(_reader, 0x10000u, viewers) ||
      !ReadRuns(_reader, FOG_STATE_COUNT, states) || states.size() != viewers.size())
  {
    return false;
  }
  std::vector<FogState> fogStates(states.size());
  for (std::size_t cell = 0; cell < states.size(); ++cell)
  {
    fogStates[cell] = static_cast<FogState>(states[cell]);
  }
  if (!seat.fog.Restore(cellsPerSide, std::move(viewers), std::move(fogStates)))
  {
    return false;
  }
  if (!_reader.Read(count) || count > Snapshot::MAX_GHOSTS)
  {
    return false;
  }
  std::vector<Ghost> ghosts(count);
  for (Ghost& ghost : ghosts)
  {
    if (!ReadObjectId(_reader, ghost.structure) || !_reader.Read(ghost.seat) || !_reader.Read(ghost.design) || !_reader.Read(ghost.cellX) ||
        !_reader.Read(ghost.cellY) || !_reader.Read(ghost.seenTick))
    {
      return false;
    }
  }
  // Ascending by structure id is what every reader and the hash depend on, so a stream that says
  // otherwise is refused here rather than sorted into shape behind the caller's back.
  if (!seat.ghosts.Restore(std::move(ghosts)))
  {
    return false;
  }
  if (!_reader.Read(count) || count > Snapshot::MAX_REJECTIONS)
  {
    return false;
  }
  seat.rejections.resize(count);
  for (OrderRejection& rejection : seat.rejections)
  {
    if (!ReadEnum(_reader, rejection.kind, ORDER_KIND_COUNT) || !ReadEnum(_reader, rejection.reason, REJECT_REASON_COUNT))
    {
      return false;
    }
  }
  if (!_reader.ReadBool(seat.defeated) || !_reader.ReadBool(seat.surrendered))
  {
    return false;
  }
  _out = std::move(seat);
  return true;
}

/// The five maps in kind order, each in ascending id, as the hash reads them. The kind is the
/// section rather than a field, so only the id's value is on the wire.
void WriteWorld(Neuron::ByteWriter& _writer, const World& _world)
{
  _writer.Write(static_cast<std::uint32_t>(_world.Count(ObjectKind::Device)));
  _world.ForEachDevice(
    [&_writer](ObjectId _id, const Device& _device)
    {
      _writer.Write(_id.value);
      _writer.Write(_device.seat);
      _writer.Write(_device.design);
      _writer.Write(_device.x);
      _writer.Write(_device.y);
      _writer.Write(_device.z);
      _writer.Write(_device.facing);
      _writer.Write(_device.hitPoints);
      _writer.Write(_device.experience);
      _writer.Write(_device.primaryOrder);
      WriteObjectId(_writer, _device.target);
      _writer.Write(_device.destinationX);
      _writer.Write(_device.destinationZ);
      _writer.Write(_device.fire);
      _writer.Write(_device.range);
      _writer.Write(_device.retreat);
      _writer.Write(_device.movement);
      _writer.Write(_device.group);
      for (const std::uint32_t reload : _device.reloadTicks)
      {
        _writer.Write(reload);
      }
    });
  _writer.Write(static_cast<std::uint32_t>(_world.Count(ObjectKind::Structure)));
  _world.ForEachStructure(
    [&_writer](ObjectId _id, const Structure& _structure)
    {
      _writer.Write(_id.value);
      _writer.Write(_structure.seat);
      _writer.Write(_structure.design);
      _writer.Write(_structure.cellX);
      _writer.Write(_structure.cellY);
      _writer.Write(_structure.y);
      _writer.Write(_structure.state);
      _writer.Write(_structure.hitPoints);
      _writer.Write(_structure.buildEffortHundredths);
      for (const std::uint32_t module : _structure.modules)
      {
        _writer.Write(module);
      }
      _writer.Write(_structure.moduleCount);
      _writer.Write(_structure.moduleUnderConstruction);
      _writer.Write(_structure.moduleEffortHundredths);
      WriteObjectId(_writer, _structure.working);
      _writer.Write(_structure.workRemainingTicks);
    });
  _writer.Write(static_cast<std::uint32_t>(_world.Count(ObjectKind::Projectile)));
  _world.ForEachProjectile(
    [&_writer](ObjectId _id, const Projectile& _projectile)
    {
      _writer.Write(_id.value);
      _writer.Write(_projectile.seat);
      WriteObjectId(_writer, _projectile.shooter);
      _writer.Write(_projectile.module);
      _writer.Write(_projectile.x);
      _writer.Write(_projectile.y);
      _writer.Write(_projectile.z);
      _writer.Write(_projectile.impactX);
      _writer.Write(_projectile.impactY);
      _writer.Write(_projectile.impactZ);
      _writer.Write(_projectile.ticksToImpact);
    });
  _writer.Write(static_cast<std::uint32_t>(_world.Count(ObjectKind::Feature)));
  _world.ForEachFeature(
    [&_writer](ObjectId _id, const Feature& _feature)
    {
      _writer.Write(_id.value);
      _writer.Write(_feature.design);
      _writer.Write(_feature.cellX);
      _writer.Write(_feature.cellY);
      _writer.Write(_feature.y);
      _writer.Write(_feature.facing);
    });
  _writer.Write(static_cast<std::uint32_t>(_world.Count(ObjectKind::Wreck)));
  _world.ForEachWreck(
    [&_writer](ObjectId _id, const Wreck& _wreck)
    {
      _writer.Write(_id.value);
      _writer.Write(_wreck.seat);
      WriteObjectId(_writer, _wreck.origin);
      _writer.Write(_wreck.design);
      _writer.Write(_wreck.x);
      _writer.Write(_wreck.y);
      _writer.Write(_wreck.z);
      _writer.Write(_wreck.facing);
      _writer.Write(_wreck.decayTicks);
    });
  _writer.Write(_world.NextId());
}

/// Reads a section's count, bounded; false when the stream is refused.
[[nodiscard]] bool ReadCount(Neuron::ByteReader& _reader, std::uint32_t& _out)
{
  return _reader.Read(_out) && _out <= Snapshot::MAX_OBJECTS;
}

[[nodiscard]] bool ReadWorld(Neuron::ByteReader& _reader, World& _world)
{
  std::uint32_t count = 0;
  if (!ReadCount(_reader, count))
  {
    return false;
  }
  for (std::uint32_t index = 0; index < count; ++index)
  {
    ObjectId id{0, ObjectKind::Device};
    Device device{};
    if (!_reader.Read(id.value) || !_reader.Read(device.seat) || !_reader.Read(device.design) || !_reader.Read(device.x) ||
        !_reader.Read(device.y) || !_reader.Read(device.z) || !_reader.Read(device.facing) || !_reader.Read(device.hitPoints) ||
        !_reader.Read(device.experience) || !ReadEnum(_reader, device.primaryOrder, PRIMARY_ORDER_COUNT) ||
        !ReadObjectId(_reader, device.target) || !_reader.Read(device.destinationX) || !_reader.Read(device.destinationZ) ||
        !ReadEnum(_reader, device.fire, STANCE_VALUE_COUNTS[0]) || !ReadEnum(_reader, device.range, STANCE_VALUE_COUNTS[1]) ||
        !ReadEnum(_reader, device.retreat, STANCE_VALUE_COUNTS[2]) || !ReadEnum(_reader, device.movement, STANCE_VALUE_COUNTS[3]) ||
        !_reader.Read(device.group) || device.group > MAX_CONTROL_GROUP)
    {
      return false;
    }
    for (std::uint32_t& reload : device.reloadTicks)
    {
      if (!_reader.Read(reload))
      {
        return false;
      }
    }
    if (!_world.Restore(id, device))
    {
      return false;
    }
  }
  if (!ReadCount(_reader, count))
  {
    return false;
  }
  for (std::uint32_t index = 0; index < count; ++index)
  {
    ObjectId id{0, ObjectKind::Structure};
    Structure structure{};
    if (!_reader.Read(id.value) || !_reader.Read(structure.seat) || !_reader.Read(structure.design) || !_reader.Read(structure.cellX) ||
        !_reader.Read(structure.cellY) || !_reader.Read(structure.y) || !ReadEnum(_reader, structure.state, 4) ||
        !_reader.Read(structure.hitPoints) || !_reader.Read(structure.buildEffortHundredths))
    {
      return false;
    }
    for (std::uint32_t& module : structure.modules)
    {
      if (!_reader.Read(module))
      {
        return false;
      }
    }
    if (!_reader.Read(structure.moduleCount) || structure.moduleCount > MAX_STRUCTURE_MODULES ||
        !_reader.Read(structure.moduleUnderConstruction) || !_reader.Read(structure.moduleEffortHundredths) ||
        !ReadObjectId(_reader, structure.working) || !_reader.Read(structure.workRemainingTicks) || !_world.Restore(id, structure))
    {
      return false;
    }
  }
  if (!ReadCount(_reader, count))
  {
    return false;
  }
  for (std::uint32_t index = 0; index < count; ++index)
  {
    ObjectId id{0, ObjectKind::Projectile};
    Projectile projectile{};
    if (!_reader.Read(id.value) || !_reader.Read(projectile.seat) || !ReadObjectId(_reader, projectile.shooter) ||
        !_reader.Read(projectile.module) || !_reader.Read(projectile.x) || !_reader.Read(projectile.y) || !_reader.Read(projectile.z) ||
        !_reader.Read(projectile.impactX) || !_reader.Read(projectile.impactY) || !_reader.Read(projectile.impactZ) ||
        !_reader.Read(projectile.ticksToImpact) || !_world.Restore(id, projectile))
    {
      return false;
    }
  }
  if (!ReadCount(_reader, count))
  {
    return false;
  }
  for (std::uint32_t index = 0; index < count; ++index)
  {
    ObjectId id{0, ObjectKind::Feature};
    Feature feature{};
    if (!_reader.Read(id.value) || !_reader.Read(feature.design) || !_reader.Read(feature.cellX) || !_reader.Read(feature.cellY) ||
        !_reader.Read(feature.y) || !_reader.Read(feature.facing) || !_world.Restore(id, feature))
    {
      return false;
    }
  }
  if (!ReadCount(_reader, count))
  {
    return false;
  }
  for (std::uint32_t index = 0; index < count; ++index)
  {
    ObjectId id{0, ObjectKind::Wreck};
    Wreck wreck{};
    if (!_reader.Read(id.value) || !_reader.Read(wreck.seat) || !ReadObjectId(_reader, wreck.origin) || !_reader.Read(wreck.design) ||
        !_reader.Read(wreck.x) || !_reader.Read(wreck.y) || !_reader.Read(wreck.z) || !_reader.Read(wreck.facing) ||
        !_reader.Read(wreck.decayTicks) || !_world.Restore(id, wreck))
    {
      return false;
    }
  }
  std::uint32_t nextId = 0;
  if (!_reader.Read(nextId))
  {
    return false;
  }
  _world.SetNextId(nextId);
  return true;
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
  // The rules the match is played by, so that reloading against others is refused rather than
  // silently played out differently (OpenQuestions.md Q20).
  _writer.Write(ContentHash(_sim.Content()));
  WriteSettings(_writer, _sim.m_settings);
  _writer.Write(_sim.m_tick);
  for (const std::uint32_t word : _sim.m_random.GetState())
  {
    _writer.Write(word);
  }
  _writer.Write(static_cast<std::uint8_t>(_sim.m_seats.size()));
  for (const Seat& seat : _sim.m_seats)
  {
    WriteSeat(_writer, seat);
  }
  WriteLandscape(_writer, _sim.m_landscape);
  WriteWorld(_writer, _sim.m_world);
  // The visibility stamps: what disc each viewer currently has counted into the grids. Derived
  // from nothing - the counts depend on them and no walk of the world reproduces them - so they
  // travel with the grids or a restored match un-counts the wrong cells on its first refresh.
  _writer.Write(static_cast<std::uint32_t>(_sim.m_visibility.Stamps().size()));
  for (const ViewerStamp& stamp : _sim.m_visibility.Stamps())
  {
    WriteObjectId(_writer, stamp.viewer);
    _writer.Write(stamp.seat);
    _writer.Write(stamp.cellX);
    _writer.Write(stamp.cellY);
    _writer.Write(stamp.radiusCells);
    _writer.Write(stamp.refreshedTick);
  }
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

std::optional<Sim> Snapshot::Read(std::span<const std::byte> _bytes, const ContentTree& _content)
{
  Neuron::ByteReader reader(_bytes);
  Neuron::StreamHeader header{};
  if (!reader.ReadHeader(SNAPSHOT_MAGIC, SNAPSHOT_VERSION, SNAPSHOT_VERSION, header))
  {
    return std::nullopt;
  }
  std::uint64_t content = 0;
  if (!reader.Read(content) || content != ContentHash(_content))
  {
    return std::nullopt;
  }
  MatchSettings settings{};
  if (!ReadSettings(reader, settings))
  {
    return std::nullopt;
  }
  Sim sim(settings, _content);
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
    if (!ReadSeat(reader, seat))
    {
      return std::nullopt;
    }
  }
  if (!ReadLandscape(reader, sim.m_landscape) || !ReadWorld(reader, sim.m_world))
  {
    return std::nullopt;
  }
  // The deposit index is the definition's, and this is the second of the two places a definition
  // arrives (Sim::CreateLandscape is the first). Without it a restored match would find no
  // deposits and every extractor would stop producing on the tick after the load.
  sim.m_economy.SetLandscape(sim.m_landscape);
  std::uint32_t stampCount = 0;
  if (!reader.Read(stampCount) || stampCount > Snapshot::MAX_STAMPS)
  {
    return std::nullopt;
  }
  std::vector<ViewerStamp> stamps(stampCount);
  for (ViewerStamp& stamp : stamps)
  {
    if (!ReadObjectId(reader, stamp.viewer) || !reader.Read(stamp.seat) || !reader.Read(stamp.cellX) || !reader.Read(stamp.cellY) ||
        !reader.Read(stamp.radiusCells) || !reader.Read(stamp.refreshedTick))
    {
      return std::nullopt;
    }
  }
  // Ascending by viewer id, which the stamp lookups bisect on: refused rather than sorted.
  if (!sim.m_visibility.Restore(std::move(stamps)))
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
