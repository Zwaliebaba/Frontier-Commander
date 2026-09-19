#include "pch.h"

#include "RenderViewBuilder.h"

#include "FixedPoint.h"

#include <algorithm>

namespace Frontier
{

namespace
{

/// A wire position as world units. Net/Records.h quantises to a quarter of a world unit.
[[nodiscard]] float WorldFromWire(std::int32_t _wireUnits) noexcept
{
  return Neuron::WorldUnitsOfSubunits(_wireUnits * SUBUNITS_PER_WIRE_UNIT);
}

/// Where a thing placed on the grid stands: the CENTRE of its footprint, because a model is
/// authored about its own origin and a structure whose origin sat on a corner would be drawn a
/// footprint's worth away from where the simulation says it is.
[[nodiscard]] float WorldFromCells(std::uint32_t _cell, std::uint32_t _footprintCells) noexcept
{
  return static_cast<float>(_cell * Neuron::WORLD_UNITS_PER_CELL) +
         static_cast<float>(_footprintCells * Neuron::WORLD_UNITS_PER_CELL) * 0.5f;
}

[[nodiscard]] bool Contains(std::span<const std::uint32_t> _ids, std::uint32_t _id) noexcept
{
  return std::find(_ids.begin(), _ids.end(), _id) != _ids.end();
}

} // namespace

RenderViewBuilder::RenderViewBuilder(const ContentTree& _content, const ModelComposer& _composer, const RenderViewSettings& _settings)
  : m_content(&_content),
    m_composer(&_composer),
    m_settings(_settings)
{
}

void RenderViewBuilder::Settings(const RenderViewSettings& _settings)
{
  m_settings = _settings;
  ForgetTerrain();
}

void RenderViewBuilder::ForgetTerrain()
{
  m_flattened.clear();
}

void ChunksOfFootprint(std::uint32_t _cellX, std::uint32_t _cellY, std::uint32_t _footprintCellsX, std::uint32_t _footprintCellsY,
                       std::uint32_t _cellsPerSide, std::uint32_t _chunkCells, std::vector<std::uint32_t>& _outChunks)
{
  // THE FIRST TWO ARE LOAD-BEARING and the last two are not, which is worth stating because
  // mutation testing removed the off-landscape test and failed nothing: a cell past the edge gives
  // a first chunk beyond the clamped last one, so the loops below simply do not run. It stays
  // because "a cell that is not on the landscape names no chunk" is the rule, and leaving it to
  // fall out of two clamps and a loop bound is how a rule gets lost the next time one of them
  // moves. The zero tests ARE load-bearing: a chunk size of zero divides by zero, and a landscape
  // of zero cells underflows `_cellsPerSide - 1` into a very large clamp.
  if (_chunkCells == 0 || _cellsPerSide == 0 || _cellX >= _cellsPerSide || _cellY >= _cellsPerSide)
  {
    return;
  }
  const std::uint32_t chunksPerSide = (_cellsPerSide + _chunkCells - 1) / _chunkCells;
  // The footprint's cells INCLUSIVE OF THE LAST. A footprint of zero cells is treated as one,
  // because a structure standing on no ground is a content fault and drawing nothing for it here
  // would hide it behind a chunk list that was merely short.
  const std::uint32_t firstX = _cellX / _chunkCells;
  const std::uint32_t firstY = _cellY / _chunkCells;
  const std::uint32_t lastX = std::min(_cellX + std::max(_footprintCellsX, 1u) - 1, _cellsPerSide - 1) / _chunkCells;
  const std::uint32_t lastY = std::min(_cellY + std::max(_footprintCellsY, 1u) - 1, _cellsPerSide - 1) / _chunkCells;
  for (std::uint32_t chunkY = firstY; chunkY <= lastY && chunkY < chunksPerSide; ++chunkY)
  {
    for (std::uint32_t chunkX = firstX; chunkX <= lastX && chunkX < chunksPerSide; ++chunkX)
    {
      _outChunks.push_back(chunkY * chunksPerSide + chunkX);
    }
  }
}

void RenderViewBuilder::MarkChunks(const Flattened& _footprint, std::vector<std::uint32_t>& _outChunks) const
{
  ChunksOfFootprint(_footprint.cellX, _footprint.cellY, _footprint.footprintCellsX, _footprint.footprintCellsY, m_settings.cellsPerSide,
                    m_settings.chunkCells, _outChunks);
}

void RenderViewBuilder::Build(const Replica& _replica, std::int64_t _renderTime, std::span<const std::uint32_t> _selectedIds,
                              Neuron::RenderView& _outView, PickSet& _outPicks)
{
  _outView.instances.clear();
  _outView.changedChunks.clear();
  _outPicks.candidates.clear();
  m_lastUnresolved = 0;

  // ── Devices ────────────────────────────────────────────────────────────────────────────────
  for (const auto& [id, device] : _replica.Devices())
  {
    const DesignState* design = _replica.Designs().Find(device.state.seat, device.state.design);
    if (design == nullptr)
    {
      // The host sends a design with the first device of it a commander sees, so this is a frame
      // arriving before the one that carried it - rare, and a device drawn as nothing for a frame
      // is better than one drawn as whatever row zero happens to be.
      ++m_lastUnresolved;
      continue;
    }
    const Pose pose = Evaluate(device.motion, _renderTime);
    ObjectAppearance appearance{};
    appearance.colorIndex = device.state.seat;
    appearance.rankBadge = device.state.rank;
    appearance.selected = Contains(_selectedIds, id);
    m_composer->ComposeDevice(*design, pose, appearance, _outView.instances);

    PickCandidate candidate{};
    candidate.id = id;
    candidate.x = pose.x;
    candidate.y = pose.y;
    candidate.z = pose.z;
    candidate.radius = m_composer->ChassisRadius(design->chassis);
    candidate.seat = device.state.seat;
    candidate.kind = ObjectKind::Device;
    _outPicks.candidates.push_back(candidate);
  }

  // ── Structures, and the terrain they flattened ─────────────────────────────────────────────
  std::map<std::uint32_t, Flattened> standing;
  for (const auto& [id, structure] : _replica.Structures())
  {
    const StructureState& state = structure.state;
    const StructureDesc* row =
      state.design < m_content->structures.structures.size() ? &m_content->structures.structures[state.design] : nullptr;
    const std::uint32_t footprintX = row != nullptr ? row->footprintCellsX : 1;
    const std::uint32_t footprintY = row != nullptr ? row->footprintCellsY : 1;
    const Pose pose{WorldFromCells(state.cellX, footprintX), WorldFromWire(state.y), WorldFromCells(state.cellY, footprintY), 0.0f};

    ObjectAppearance appearance{};
    appearance.colorIndex = state.seat;
    appearance.selected = Contains(_selectedIds, id);
    m_composer->ComposeStructure(state, pose, appearance, _outView.instances);
    if (row == nullptr)
    {
      ++m_lastUnresolved;
    }

    // A GHOST IS NOT PICKED. It is what the commander remembers of a building he cannot see now,
    // so a click on one would select something that may not be there - and §5's inspection is for
    // what is visible. It is still drawn, and the fog pass darkens it like everything else.
    if (!structure.ghost)
    {
      PickCandidate candidate{};
      candidate.id = id;
      candidate.x = pose.x;
      candidate.y = pose.y;
      candidate.z = pose.z;
      candidate.radius = m_composer->StructureRadius(state.design);
      candidate.seat = state.seat;
      candidate.kind = ObjectKind::Structure;
      _outPicks.candidates.push_back(candidate);
    }

    standing.emplace(id, Flattened{state.cellX, state.cellY, footprintX, footprintY, state.y});
  }

  // What changed since the last frame: a structure that arrived, one whose flattened height moved,
  // and one that is gone - a razed building leaves its ground as it left it, and the chunk has to
  // be rebuilt either way.
  for (const auto& [id, flattened] : standing)
  {
    const auto was = m_flattened.find(id);
    if (was == m_flattened.end() || was->second != flattened)
    {
      MarkChunks(flattened, _outView.changedChunks);
    }
  }
  for (const auto& [id, flattened] : m_flattened)
  {
    if (!standing.contains(id))
    {
      MarkChunks(flattened, _outView.changedChunks);
    }
  }
  m_flattened = std::move(standing);
  // Ascending and without repeats, which is what a consumer of a change list expects and what two
  // structures on one chunk would otherwise break.
  std::sort(_outView.changedChunks.begin(), _outView.changedChunks.end());
  _outView.changedChunks.erase(std::unique(_outView.changedChunks.begin(), _outView.changedChunks.end()), _outView.changedChunks.end());

  // ── Wrecks ─────────────────────────────────────────────────────────────────────────────────
  for (const auto& [id, wreck] : _replica.Wrecks())
  {
    const WreckState& state = wreck.state;
    std::uint32_t model = ModelComposer::NO_MODEL;
    float scale = 1.0f;
    if (static_cast<ObjectKind>(state.origin) == ObjectKind::Structure)
    {
      model = m_composer->StructureModel(state.design);
      scale = m_composer->StructureScale(state.design);
    }
    else if (const DesignState* design = _replica.Designs().Find(state.seat, state.design); design != nullptr)
    {
      // A DEVICE'S WRECK IS ITS CHASSIS AND NOT ITS WHOLE TREE. What is left after §7's explosion
      // is the hull; the drives and the modules came apart with everything else.
      model = m_composer->ChassisModel(design->chassis);
      scale = m_composer->ChassisScale(design->chassis);
    }
    if (model == ModelComposer::NO_MODEL)
    {
      ++m_lastUnresolved;
      continue;
    }
    ObjectAppearance appearance{};
    appearance.colorIndex = state.seat;
    const Pose pose{WorldFromWire(state.x), WorldFromWire(state.y), WorldFromWire(state.z), RadiansOfWireHeading(state.heading)};
    m_composer->ComposeSingle(model, scale, pose, appearance, Neuron::RenderInstanceKind::Wreck, _outView.instances);
  }

  // ── The fog, straight through ──────────────────────────────────────────────────────────────
  // The replica's own bytes without a table: Core/RenderView.h's FogShade and Sim/FogGrid.h's
  // FogState carry the same three values in the same order, and that header says so.
  const std::span<const FogState> fog = _replica.Fog();
  _outView.fog.cells.resize(fog.size());
  for (std::size_t index = 0; index < fog.size(); ++index)
  {
    _outView.fog.cells[index] = static_cast<std::uint8_t>(fog[index]);
  }
  const std::span<const std::uint32_t> changedRows = _replica.ChangedFogRows();
  _outView.fog.changedRows.assign(changedRows.begin(), changedRows.end());
  _outView.fog.cellsPerSide = _replica.FogCellsPerSide();
}

} // namespace Frontier
