#include "pch.h"

#include "Interest.h"

#include <algorithm>

namespace Frontier
{

namespace
{

/// The cell a position in subunits falls in, clamped into the grid.
[[nodiscard]] std::uint32_t CellOf(std::int32_t _subunits, std::uint32_t _cellsPerSide) noexcept
{
  if (_subunits < 0)
  {
    return 0;
  }
  const std::uint32_t cell = static_cast<std::uint32_t>(_subunits / Neuron::SUBUNITS_PER_CELL);
  return std::min(cell, _cellsPerSide == 0 ? 0u : _cellsPerSide - 1);
}

/// True when any cell of a footprint is visible to the alliance. A structure is several cells and a
/// commander who can see one corner of a factory can see the factory.
[[nodiscard]] bool FootprintVisible(const Sim& _sim, std::uint8_t _seat, std::uint32_t _cellX, std::uint32_t _cellY, std::uint32_t _cellsX,
                                    std::uint32_t _cellsY) noexcept
{
  for (std::uint32_t y = _cellY; y < _cellY + _cellsY; ++y)
  {
    for (std::uint32_t x = _cellX; x < _cellX + _cellsX; ++x)
    {
      if (VisibleToAlliance(_sim, _seat, x, y))
      {
        return true;
      }
    }
  }
  return false;
}

[[nodiscard]] const StructureDesc* RowOf(const ContentTree& _content, std::uint32_t _design) noexcept
{
  return _design < _content.structures.structures.size() ? &_content.structures.structures[_design] : nullptr;
}

} // namespace

bool VisibleToAlliance(const Sim& _sim, std::uint8_t _seat, std::uint32_t _cellX, std::uint32_t _cellY) noexcept
{
  const std::span<const Seat> seats = _sim.Seats();
  if (_seat >= seats.size())
  {
    return false;
  }
  const std::uint8_t alliance = seats[_seat].alliance;
  for (const Seat& seat : seats)
  {
    if (seat.alliance == alliance && seat.kind != SeatKind::Empty && !seat.fog.Empty() && seat.fog.Inside(_cellX, _cellY) &&
        seat.fog.Visible(_cellX, _cellY))
    {
      return true;
    }
  }
  return false;
}

void GatherInterest(const Sim& _sim, std::uint8_t _seat, InterestSet& _out)
{
  _out.Clear();
  if (_seat >= _sim.Seats().size())
  {
    return;
  }
  const World& world = _sim.Objects();
  const ContentTree& content = _sim.Content();
  const std::uint32_t cellsPerSide = _sim.Seats()[_seat].fog.CellsPerSide();

  world.ForEachDevice(
    [&_sim, &_out, _seat, cellsPerSide](ObjectId _id, const Device& _device)
    {
      // Owned, wherever it is: a commander is never in the dark about his own army, and a device
      // outside his own vision is one he ordered somewhere he cannot see.
      if (_device.seat == _seat || VisibleToAlliance(_sim, _seat, CellOf(_device.x, cellsPerSide), CellOf(_device.z, cellsPerSide)))
      {
        _out.devices.push_back(_id.value);
      }
    });

  world.ForEachStructure(
    [&_sim, &_out, _seat, &content](ObjectId _id, const Structure& _structure)
    {
      if (_structure.seat == _seat)
      {
        _out.structures.push_back(_id.value);
        return;
      }
      const StructureDesc* row = RowOf(content, _structure.design);
      const std::uint32_t cellsX = row != nullptr ? row->footprintCellsX : 1;
      const std::uint32_t cellsY = row != nullptr ? row->footprintCellsY : 1;
      if (FootprintVisible(_sim, _seat, _structure.cellX, _structure.cellY, cellsX, cellsY))
      {
        _out.structures.push_back(_id.value);
      }
    });

  world.ForEachWreck(
    [&_sim, &_out, _seat, cellsPerSide](ObjectId _id, const Wreck& _wreck)
    {
      if (_wreck.seat == _seat || VisibleToAlliance(_sim, _seat, CellOf(_wreck.x, cellsPerSide), CellOf(_wreck.z, cellsPerSide)))
      {
        _out.wrecks.push_back(_id.value);
      }
    });

  world.ForEachFeature(
    [&_sim, &_out, _seat](ObjectId _id, const Feature& _feature)
    {
      // A feature is scenery and never moves, but it is still fog-gated: a rock in an unexplored
      // corner is not something a commander has been told about, and a client that drew every rock
      // on the map would be drawing a map nobody scouted.
      if (VisibleToAlliance(_sim, _seat, _feature.cellX, _feature.cellY))
      {
        _out.features.push_back(_id.value);
      }
    });

  // A ghost for every structure this commander has seen and cannot see now. A structure he CAN see
  // is in `structures` at its true state, so it is never also a ghost: the live record wins.
  for (const Ghost& ghost : _sim.Seats()[_seat].ghosts.All())
  {
    const auto at = std::lower_bound(_out.structures.begin(), _out.structures.end(), ghost.structure.value);
    if (at != _out.structures.end() && *at == ghost.structure.value)
    {
      continue;
    }
    _out.ghosts.push_back(ghost.structure);
  }
}

} // namespace Frontier
