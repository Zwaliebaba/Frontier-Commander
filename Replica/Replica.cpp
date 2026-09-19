#include "pch.h"

#include "Replica.h"

#include <algorithm>

namespace Frontier
{

namespace
{

[[nodiscard]] Sample SampleOf(std::uint32_t _tick, const DeviceState& _state) noexcept
{
  return Sample{_tick, _state.x, _state.y, _state.z, _state.heading};
}

[[nodiscard]] constexpr bool Carries(std::uint8_t _mask, DeviceField _field) noexcept
{
  return (_mask & static_cast<std::uint8_t>(_field)) != 0;
}

} // namespace

void Replica::Adopt()
{
  m_seat = m_client->Seat();
  const std::uint32_t cells = m_client->Landscape().cellsPerSide;
  if (cells != m_fogCellsPerSide)
  {
    m_fogCellsPerSide = cells;
    m_fog.assign(static_cast<std::size_t>(cells) * cells, FogState::Unexplored);
  }
}

void Replica::Clear()
{
  m_devices.clear();
  m_structures.clear();
  m_wrecks.clear();
  m_features.clear();
  m_designs.Clear();
  m_own = SeatState{};
  m_events.clear();
  m_changedFogRows.clear();
  std::fill(m_fog.begin(), m_fog.end(), FogState::Unexplored);
  m_newestTick = 0;
  m_newestSequence = NO_BASELINE;
  m_previousTick = 0;
}

std::int64_t Replica::RenderTimeAtNewestFrame() const noexcept
{
  const std::int64_t newest = RenderTimeOfTick(m_newestTick);
  const std::int64_t delay = INTERPOLATION_DELAY_TICKS * RENDER_TIME_SCALE;
  // Early in a match the newest tick is smaller than the delay, and a render time before tick 0 is
  // a time nothing was ever sampled at. Evaluate would clamp it to the older sample anyway; naming
  // 0 here means the floor is a time on the timeline rather than one off the end of it.
  return newest > delay ? newest - delay : 0;
}

void Replica::Apply(const Frame& _frame)
{
  // A frame with no baseline is everything the commander can see, sent whole: a join, a rejoin, or
  // a client whose acknowledged baseline aged out of the host's history (Net/Messages.h). Whatever
  // is held now was encoded against a baseline this frame is not a delta from, so it is not stale
  // in parts - it is stale entirely, and keeping any of it would leave objects the host has since
  // removed standing on the field forever.
  if (_frame.baselineSequence == NO_BASELINE)
  {
    Clear();
    Adopt();
  }
  // Net's Client never hands over a delta against a baseline it has not applied, and its first
  // frame after a join is always full, so by here the fog is sized. The assertion says so rather
  // than leaving a silent nothing if that ever changes.
  FRONTIER_ASSERT(m_fogCellsPerSide != 0);

  for (const DesignState& design : _frame.designs)
  {
    m_designs.Remember(design);
  }

  for (const DeviceState& created : _frame.createdDevices)
  {
    ReplicaDevice& device = m_devices[created.id];
    device.state = created;
    device.motion.Push(SampleOf(_frame.tick, created));
  }
  for (const StructureState& created : _frame.createdStructures)
  {
    ReplicaStructure& structure = m_structures[created.id];
    structure.state = created;
    structure.ghost = created.hitPoints == GHOST_HIT_POINTS;
  }
  for (const WreckState& created : _frame.createdWrecks)
  {
    m_wrecks[created.id].state = created;
  }
  for (const FeatureState& created : _frame.createdFeatures)
  {
    m_features[created.id].state = created;
  }

  for (const DeviceChange& change : _frame.changedDevices)
  {
    const auto at = m_devices.find(change.id);
    if (at == m_devices.end())
    {
      // A change against a baseline this client does not hold. Net's Client refuses a frame whose
      // baseline it never applied, so this is a host and a client that disagree about history
      // rather than ordinary loss; dropping it leaves the object absent, which the next full frame
      // corrects. Inventing a device from a delta would put one on the field the host never sent.
      continue;
    }
    ReplicaDevice& device = at->second;
    DeviceState& state = device.state;
    if (Carries(change.mask, DeviceField::Position))
    {
      state.x += change.deltaX;
      state.y += change.deltaY;
      state.z += change.deltaZ;
    }
    if (Carries(change.mask, DeviceField::Heading))
    {
      state.heading = change.heading;
    }
    if (Carries(change.mask, DeviceField::HitPoints))
    {
      state.hitPoints = change.hitPoints;
    }
    if (Carries(change.mask, DeviceField::Stances))
    {
      state.stances = change.stances;
    }

    // A DEVICE THAT SENT NO CHANGE DID NOT MOVE, which the protocol says by omission: an empty
    // change is never sent (§5.3). So a device that stood still for a while and then moved would
    // otherwise hand Evaluate a segment spanning the whole still period, and be dragged across it
    // in the single frame that ends it. Stamping the position it held at the LAST FRAME THIS
    // REPLICA APPLIED closes that: it is not a guess, it is what the host asserted by saying
    // nothing, and it uses only what this replica observed rather than assuming the publish rate.
    if (device.motion.samples != 0 && device.motion.newer.tick < m_previousTick)
    {
      Sample stood = device.motion.newer;
      stood.tick = m_previousTick;
      device.motion.Push(stood);
    }
    device.motion.Push(SampleOf(_frame.tick, state));
  }

  for (const StructureState& changed : _frame.changedStructures)
  {
    ReplicaStructure& structure = m_structures[changed.id];
    structure.state = changed;
    // The one field that tells a remembered structure from a standing one, and the reason a ghost
    // needs no separate collection: the host sends the last-seen record in the same list, with no
    // hit points on it (Net/FrameEncoder.cpp's WireGhost). A structure that comes back into view
    // arrives with its hit points again and stops being a ghost here, in the same line.
    structure.ghost = changed.hitPoints == GHOST_HIT_POINTS;
  }

  for (const std::uint32_t id : _frame.removed)
  {
    // The id says nothing about which table holds it, so all four are asked. Ids are unique across
    // kinds (Sim/ObjectId.h), so at most one erases.
    m_devices.erase(id);
    m_structures.erase(id);
    m_wrecks.erase(id);
    m_features.erase(id);
  }

  ApplyFog(_frame);

  m_own = _frame.seat;
  m_events.assign(_frame.events.begin(), _frame.events.end());
  m_previousTick = m_newestTick;
  m_newestTick = _frame.tick;
  m_newestSequence = _frame.sequence;
}

void Replica::ApplyFog(const Frame& _frame)
{
  m_changedFogRows.clear();
  if (m_fog.empty())
  {
    return;
  }
  for (const FogDelta& run : _frame.fog)
  {
    const std::size_t first = run.firstCell;
    const std::size_t last = std::min(first + run.cells, m_fog.size());
    for (std::size_t cell = first; cell < last; ++cell)
    {
      m_fog[cell] = run.state;
    }
    if (last > first)
    {
      const std::uint32_t firstRow = static_cast<std::uint32_t>(first / m_fogCellsPerSide);
      const std::uint32_t lastRow = static_cast<std::uint32_t>((last - 1) / m_fogCellsPerSide);
      for (std::uint32_t row = firstRow; row <= lastRow; ++row)
      {
        m_changedFogRows.push_back(row);
      }
    }
  }
  // Ascending and without repeats, which is what Core/RenderView.h's FogView promises its reader:
  // a run may cross a row boundary and two runs in one frame may touch the same row.
  std::sort(m_changedFogRows.begin(), m_changedFogRows.end());
  m_changedFogRows.erase(std::unique(m_changedFogRows.begin(), m_changedFogRows.end()), m_changedFogRows.end());
}

} // namespace Frontier
