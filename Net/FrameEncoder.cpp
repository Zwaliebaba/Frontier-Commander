#include "pch.h"

#include "FrameEncoder.h"

#include "Construction.h"

#include <algorithm>

namespace Frontier
{

namespace
{

/// A device's cell-free position, in wire units.
[[nodiscard]] std::int32_t Wire(std::int32_t _subunits) noexcept
{
  return WireFromSubunits(_subunits);
}

[[nodiscard]] std::uint16_t Clamped(std::int32_t _hitPoints) noexcept
{
  if (_hitPoints <= 0)
  {
    return 0;
  }
  return static_cast<std::uint16_t>(std::min<std::int32_t>(_hitPoints, 0xFFFF));
}

/// Walks two lists sorted by id together, calling _both for an id in each, _created for one only in
/// the new list and _removed for one only in the old.
template <class T, class Created, class Both, class Removed>
void Diff(const std::vector<T>& _was, const std::vector<T>& _now, Created&& _created, Both&& _both, Removed&& _removed)
{
  std::size_t was = 0;
  std::size_t now = 0;
  while (was < _was.size() || now < _now.size())
  {
    if (now == _now.size() || (was < _was.size() && _was[was].id < _now[now].id))
    {
      _removed(_was[was]);
      ++was;
    }
    else if (was == _was.size() || _now[now].id < _was[was].id)
    {
      _created(_now[now]);
      ++now;
    }
    else
    {
      _both(_was[was], _now[now]);
      ++was;
      ++now;
    }
  }
}

} // namespace

DeviceState WireDevice(std::uint32_t _id, const Device& _device)
{
  DeviceState record{};
  record.id = _id;
  record.design = _device.design;
  record.seat = _device.seat;
  record.x = Wire(_device.x);
  record.y = Wire(_device.y);
  record.z = Wire(_device.z);
  record.heading = WireFromFacing(_device.facing);
  record.hitPoints = Clamped(_device.hitPoints);
  record.rank = RankOf(_device.experience);
  record.target = _device.target.value;
  record.targetKind = _device.target.kind;
  record.stances = {_device.primaryOrder, _device.fire, _device.range, _device.retreat, _device.movement};
  return record;
}

StructureState WireStructure(const ContentTree& _content, std::uint32_t _id, const Structure& _structure)
{
  StructureState record{};
  record.id = _id;
  record.design = _structure.design;
  record.seat = _structure.seat;
  record.cellX = static_cast<std::uint16_t>(_structure.cellX);
  record.cellY = static_cast<std::uint16_t>(_structure.cellY);
  record.y = Wire(_structure.y);
  record.phase = _structure.state;
  record.hitPoints = Clamped(_structure.hitPoints);
  const std::uint32_t buildTicks =
    _structure.design < _content.structures.structures.size() ? _content.structures.structures[_structure.design].buildTimeTicks : 0;
  // ProgressHundredths is hundredths of a hundred percent; a client draws a bar, so a percent is
  // all the wire carries.
  record.buildPercent =
    static_cast<std::uint8_t>(std::clamp(ProgressHundredths(_structure.buildEffortHundredths, buildTicks) / 100, 0, 100));
  record.moduleCount = _structure.moduleCount;
  for (std::size_t index = 0; index < MAX_STRUCTURE_MODULES; ++index)
  {
    record.modules[index] = static_cast<std::uint8_t>(std::min<std::uint32_t>(_structure.modules[index], 0xFF));
  }
  return record;
}

StructureState WireGhost(const Ghost& _ghost)
{
  // A ghost is what the commander last saw and nothing more: its phase is Standing because that is
  // what he saw standing there, and its hit points are not sent at all - he has no idea what it has
  // taken since. Zero says "unknown", which is what a client draws a ghost with.
  StructureState record{};
  record.id = _ghost.structure.value;
  record.design = _ghost.design;
  record.seat = _ghost.seat;
  record.cellX = static_cast<std::uint16_t>(_ghost.cellX);
  record.cellY = static_cast<std::uint16_t>(_ghost.cellY);
  record.y = 0;
  record.phase = StructurePhase::Standing;
  record.hitPoints = 0;
  record.buildPercent = 100;
  record.moduleCount = 0;
  record.modules = {};
  return record;
}

WreckState WireWreck(std::uint32_t _id, const Wreck& _wreck)
{
  WreckState record{};
  record.id = _id;
  record.design = _wreck.design;
  record.seat = _wreck.seat;
  record.origin = static_cast<std::uint8_t>(_wreck.origin.kind);
  record.x = Wire(_wreck.x);
  record.y = Wire(_wreck.y);
  record.z = Wire(_wreck.z);
  record.heading = WireFromFacing(_wreck.facing);
  return record;
}

FeatureState WireFeature(std::uint32_t _id, const Feature& _feature)
{
  FeatureState record{};
  record.id = _id;
  record.design = _feature.design;
  record.cellX = static_cast<std::uint16_t>(_feature.cellX);
  record.cellY = static_cast<std::uint16_t>(_feature.cellY);
  record.y = Wire(_feature.y);
  record.heading = WireFromFacing(_feature.facing);
  return record;
}

SeatState WireSeat(std::uint8_t _seat, const Seat& _record, const RejectionLatch& _rejection)
{
  SeatState record{};
  record.seat = _seat;
  record.rejectSequence = _rejection.sequence;
  record.rejectKind = static_cast<std::uint8_t>(_rejection.kind);
  record.rejectReason = static_cast<std::uint8_t>(_rejection.reason);
  record.powerHundredths = _record.powerHundredths;
  record.stockpileCapHundredths = _record.stockpileCapHundredths;
  record.extractedHundredths = _record.extractedHundredths;
  record.researchItem = NO_RESEARCH_ITEM;
  record.researchRemainingTicks = 0;
  if (!_record.researchActive.empty())
  {
    // The first lab's item, which is what a panel shows when there is no room for all of them. The
    // whole list is M2's panel work rather than a wire question.
    record.researchItem = _record.researchActive.front().item;
    record.researchRemainingTicks = _record.researchActive.front().remainingTicks;
  }
  record.victory = static_cast<std::uint8_t>(_record.victory);
  record.deviceCount = static_cast<std::uint16_t>(std::min<std::uint32_t>(_record.deviceCount, 0xFFFF));
  record.deviceCap = static_cast<std::uint16_t>(std::min<std::uint32_t>(_record.deviceCap, 0xFFFF));
  record.structureCount = static_cast<std::uint16_t>(std::min<std::uint32_t>(_record.structureCount, 0xFFFF));
  record.structureCap = static_cast<std::uint16_t>(std::min<std::uint32_t>(_record.structureCap, 0xFFFF));
  return record;
}

DesignState WireDesign(std::uint8_t _seat, std::uint32_t _index, const DeviceDesign& _design)
{
  DesignState record{};
  record.seat = _seat;
  record.index = _index;
  record.chassis = _design.chassis;
  record.drive = _design.drive;
  record.modules = _design.modules;
  record.moduleCount = _design.moduleCount;
  return record;
}

bool ChangeOf(const DeviceState& _baseline, const DeviceState& _now, DeviceChange& _out)
{
  _out = {};
  _out.id = _now.id;
  const std::int32_t deltaX = _now.x - _baseline.x;
  const std::int32_t deltaY = _now.y - _baseline.y;
  const std::int32_t deltaZ = _now.z - _baseline.z;
  const bool fits = deltaX >= -32768 && deltaX <= 32767 && deltaY >= -32768 && deltaY <= 32767 && deltaZ >= -32768 && deltaZ <= 32767;
  if ((deltaX != 0 || deltaY != 0 || deltaZ != 0) && fits)
  {
    _out.mask |= static_cast<std::uint8_t>(DeviceField::Position);
    _out.deltaX = static_cast<std::int16_t>(deltaX);
    _out.deltaY = static_cast<std::int16_t>(deltaY);
    _out.deltaZ = static_cast<std::int16_t>(deltaZ);
  }
  if (_now.heading != _baseline.heading)
  {
    _out.mask |= static_cast<std::uint8_t>(DeviceField::Heading);
    _out.heading = _now.heading;
  }
  if (_now.hitPoints != _baseline.hitPoints)
  {
    _out.mask |= static_cast<std::uint8_t>(DeviceField::HitPoints);
    _out.hitPoints = _now.hitPoints;
  }
  if (!(_now.stances == _baseline.stances))
  {
    _out.mask |= static_cast<std::uint8_t>(DeviceField::Stances);
    _out.stances = _now.stances;
  }
  // The fields no mask bit covers - design, seat, rank and target - are sent by re-creating the
  // device, which is what keeps the changed record at fourteen bytes (§5.7). A jump too far to fit
  // a two-byte delta takes the same path.
  const bool beyondTheMask =
    _now.rank != _baseline.rank || _now.target != _baseline.target || _now.targetKind != _baseline.targetKind || !fits;
  if (beyondTheMask)
  {
    return false;
  }
  return _out.mask != 0;
}

void EncodeFrame(const Sim& _sim, const InterestSet& _interest, ClientView& _view, const FrameRecord* _baseline,
                 std::span<const Event> _events, Frame& _outFrame, FrameRecord& _outRecord)
{
  const World& world = _sim.Objects();
  const ContentTree& content = _sim.Content();
  const Seat& seat = _sim.Seats()[_view.seat];

  _outRecord.Clear();
  _outRecord.sequence = _view.nextSequence;

  // What the client would hold: every record the interest set names, in ascending id, built from
  // the interest set alone.
  for (const std::uint32_t id : _interest.devices)
  {
    const Device* device = world.FindDevice({id, ObjectKind::Device});
    if (device != nullptr)
    {
      _outRecord.devices.push_back(WireDevice(id, *device));
    }
  }
  for (const std::uint32_t id : _interest.structures)
  {
    const Structure* structure = world.FindStructure({id, ObjectKind::Structure});
    if (structure != nullptr)
    {
      _outRecord.structures.push_back(WireStructure(content, id, *structure));
    }
  }
  for (const ObjectId& ghost : _interest.ghosts)
  {
    const Ghost* record = seat.ghosts.Find(ghost);
    if (record != nullptr)
    {
      _outRecord.structures.push_back(WireGhost(*record));
    }
  }
  std::sort(_outRecord.structures.begin(), _outRecord.structures.end(),
            [](const StructureState& _a, const StructureState& _b) { return _a.id < _b.id; });
  for (const std::uint32_t id : _interest.wrecks)
  {
    const Wreck* wreck = world.FindWreck({id, ObjectKind::Wreck});
    if (wreck != nullptr)
    {
      _outRecord.wrecks.push_back(WireWreck(id, *wreck));
    }
  }
  for (const std::uint32_t id : _interest.features)
  {
    const Feature* feature = world.FindFeature({id, ObjectKind::Feature});
    if (feature != nullptr)
    {
      _outRecord.features.push_back(WireFeature(id, *feature));
    }
  }

  _outFrame = {};
  _outFrame.sequence = _view.nextSequence;
  _outFrame.baselineSequence = _baseline != nullptr ? _baseline->sequence : NO_BASELINE;
  _outFrame.tick = _sim.Tick();
  _outFrame.seat = WireSeat(_view.seat, seat, _view.rejection);

  const FrameRecord empty;
  const FrameRecord& was = _baseline != nullptr ? *_baseline : empty;

  Diff(
    was.devices, _outRecord.devices, [&_outFrame](const DeviceState& _created) { _outFrame.createdDevices.push_back(_created); },
    [&_outFrame](const DeviceState& _before, const DeviceState& _now)
    {
      DeviceChange change{};
      if (ChangeOf(_before, _now, change))
      {
        _outFrame.changedDevices.push_back(change);
      }
      else if (!(_before == _now))
      {
        _outFrame.createdDevices.push_back(_now); // beyond the mask: sent whole again
      }
    },
    [&_outFrame](const DeviceState& _gone) { _outFrame.removed.push_back(_gone.id); });

  Diff(
    was.structures, _outRecord.structures,
    [&_outFrame](const StructureState& _created) { _outFrame.createdStructures.push_back(_created); },
    [&_outFrame](const StructureState& _before, const StructureState& _now)
    {
      if (!(_before == _now))
      {
        _outFrame.changedStructures.push_back(_now);
      }
    },
    [&_outFrame](const StructureState& _gone) { _outFrame.removed.push_back(_gone.id); });

  Diff(
    was.wrecks, _outRecord.wrecks, [&_outFrame](const WreckState& _created) { _outFrame.createdWrecks.push_back(_created); },
    [](const WreckState&, const WreckState&) {}, // a wreck lies where it fell and never changes
    [&_outFrame](const WreckState& _gone) { _outFrame.removed.push_back(_gone.id); });

  Diff(
    was.features, _outRecord.features, [&_outFrame](const FeatureState& _created) { _outFrame.createdFeatures.push_back(_created); },
    [](const FeatureState&, const FeatureState&) {}, // scenery never changes either
    [&_outFrame](const FeatureState& _gone) { _outFrame.removed.push_back(_gone.id); });

  // The designs of every device in the frame that the client has not been told about. Sent with the
  // first device of that design a client sees (§5.3), so that it can show what it is fighting.
  for (const DeviceState& device : _outFrame.createdDevices)
  {
    if (device.seat >= _sim.Seats().size())
    {
      continue;
    }
    const Seat& owner = _sim.Seats()[device.seat];
    if (device.design >= owner.designs.size())
    {
      continue;
    }
    const DesignState design = WireDesign(device.seat, device.design, owner.designs[device.design]);
    const auto already = std::find(_outFrame.designs.begin(), _outFrame.designs.end(), design);
    if (already == _outFrame.designs.end())
    {
      _outFrame.designs.push_back(design);
    }
  }

  // ── The commander's own fog ────────────────────────────────────────────────────────────────
  //
  // His own information, which leaks nothing: it is his grid and no other seat's.
  //
  // THE RUNS ARE RELATIVE TO WHAT HE HAS ACKNOWLEDGED, exactly as every other list in this frame
  // is. _view.fog is the grid as of _view.foggedThrough, and it moves only when a baseline the
  // client acknowledged is folded into it - never on an encode. That is the whole fix of
  // m1-vertical-slice/G1a: Net/Client.cpp drops any delta whose baseline is not the frame it last
  // applied, which happens whenever a publish outruns an acknowledgement, and a grid advanced on
  // encode loses those runs for good.
  const std::span<const FogState> now = seat.fog.States();
  if (_view.fog.size() != now.size())
  {
    _view.fog.assign(now.size(), FogState::Unexplored);
    _view.foggedThrough = NO_BASELINE;
  }
  if (_baseline == nullptr)
  {
    // A full frame lands on a replica that clears everything it holds first (Replica::Apply), so
    // the state it is a difference from is an empty grid rather than whatever this client had.
    std::fill(_view.fog.begin(), _view.fog.end(), FogState::Unexplored);
    _view.foggedThrough = NO_BASELINE;
  }
  else if (_view.foggedThrough != _baseline->sequence)
  {
    // The client has acknowledged a newer frame. Its runs were taken against the grid held here, so
    // folding them in once gives the grid that client now holds. Folding only the acknowledged
    // record and not the ones between is right for the same reason: each record's runs are relative
    // to the baseline it was encoded against, which is the frame acknowledged before it.
    for (const FogDelta& run : _baseline->fog)
    {
      const std::size_t first = run.firstCell;
      const std::size_t last = std::min(first + run.cells, _view.fog.size());
      for (std::size_t cell = first; cell < last; ++cell)
      {
        _view.fog[cell] = run.state;
      }
    }
    _view.foggedThrough = _baseline->sequence;
  }
  std::size_t cell = 0;
  while (cell < now.size())
  {
    if (now[cell] == _view.fog[cell])
    {
      ++cell;
      continue;
    }
    const FogState state = now[cell];
    const std::size_t first = cell;
    while (cell < now.size() && now[cell] == state && now[cell] != _view.fog[cell])
    {
      ++cell;
    }
    FogDelta run{};
    run.firstCell = static_cast<std::uint32_t>(first);
    run.cells = static_cast<std::uint16_t>(std::min<std::size_t>(cell - first, 0xFFFF));
    run.state = state;
    _outFrame.fog.push_back(run);
  }
  // The record keeps them so that the fold above can replay exactly these runs if this frame is the
  // one the client acknowledges.
  _outRecord.fog = _outFrame.fog;

  _outFrame.events.assign(_events.begin(), _events.end());
}

} // namespace Frontier
