#include "pch.h"

#include "Landscape.h"
#include "LandscapeGenerator.h"

#include <algorithm>

namespace Frontier
{

bool Landscape::Create(const LandscapeDefinition& _definition)
{
  if (_definition.cellsPerSide == 0 || _definition.cellsPerSide > SIZE_CLASS_CELLS.back())
  {
    return false;
  }
  std::vector<std::int16_t> heights;
  if (!LandscapeGenerator::Generate(_definition, heights))
  {
    return false;
  }
  m_definition = _definition;
  m_cellsPerSide = _definition.cellsPerSide;
  m_samplesPerSide = Frontier::SamplesPerSide(_definition.cellsPerSide); // the free function, not the member
  m_heights = std::move(heights);
  m_cells.assign(static_cast<std::size_t>(m_cellsPerSide) * m_cellsPerSide, Cell{0, 0, 0});
  m_deltas.clear();
  DeriveCells(0, 0, m_cellsPerSide, m_cellsPerSide);
  return true;
}

bool Landscape::Restore(const LandscapeDefinition& _definition, const std::vector<HeightDelta>& _deltas)
{
  if (!Create(_definition))
  {
    return false;
  }
  for (const HeightDelta& delta : _deltas)
  {
    if (!ApplyDelta(delta))
    {
      return false;
    }
  }
  return true;
}

std::int16_t Landscape::HeightAt(std::uint32_t _sampleX, std::uint32_t _sampleY) const noexcept
{
  FRONTIER_ASSERT(_sampleX < m_samplesPerSide && _sampleY < m_samplesPerSide);
  return m_heights[static_cast<std::size_t>(_sampleY) * m_samplesPerSide + _sampleX];
}

const Landscape::Cell& Landscape::CellAt(std::uint32_t _cellX, std::uint32_t _cellY) const noexcept
{
  FRONTIER_ASSERT(_cellX < m_cellsPerSide && _cellY < m_cellsPerSide);
  return m_cells[static_cast<std::size_t>(_cellY) * m_cellsPerSide + _cellX];
}

bool Landscape::ApplyDelta(const HeightDelta& _delta)
{
  if (!Created() || _delta.width == 0 || _delta.height == 0 || _delta.x > m_samplesPerSide - _delta.width ||
      _delta.y > m_samplesPerSide - _delta.height || _delta.heights.size() != static_cast<std::size_t>(_delta.width) * _delta.height)
  {
    return false;
  }
  for (std::uint32_t j = 0; j < _delta.height; ++j)
  {
    const std::size_t destination = static_cast<std::size_t>(_delta.y + j) * m_samplesPerSide + _delta.x;
    const std::size_t source = static_cast<std::size_t>(j) * _delta.width;
    std::copy_n(_delta.heights.begin() + static_cast<std::ptrdiff_t>(source), _delta.width,
                m_heights.begin() + static_cast<std::ptrdiff_t>(destination));
  }
  // The cells whose 5x5 samples include a changed sample: a sample on a cell's edge belongs to two.
  const std::uint32_t cellX0 = _delta.x == 0 ? 0 : (_delta.x - 1) / SAMPLES_PER_CELL_EDGE;
  const std::uint32_t cellY0 = _delta.y == 0 ? 0 : (_delta.y - 1) / SAMPLES_PER_CELL_EDGE;
  const std::uint32_t cellX1 = std::min(m_cellsPerSide, (_delta.x + _delta.width - 1) / SAMPLES_PER_CELL_EDGE + 1);
  const std::uint32_t cellY1 = std::min(m_cellsPerSide, (_delta.y + _delta.height - 1) / SAMPLES_PER_CELL_EDGE + 1);
  DeriveCells(cellX0, cellY0, cellX1, cellY1);
  m_deltas.push_back(_delta);
  return true;
}

void Landscape::SetObstruction(std::uint32_t _cellX, std::uint32_t _cellY, std::uint8_t _obstruction) noexcept
{
  FRONTIER_ASSERT(_cellX < m_cellsPerSide && _cellY < m_cellsPerSide);
  m_cells[static_cast<std::size_t>(_cellY) * m_cellsPerSide + _cellX].obstruction = _obstruction;
}

void Landscape::AddToHash(StateHash& _hash) const noexcept
{
  _hash.Add(m_definition.version);
  _hash.Add(m_definition.sizeClass);
  _hash.Add(m_definition.cellsPerSide);
  _hash.Add(m_definition.seed);
  _hash.Add(static_cast<std::uint32_t>(m_definition.tiles.size()));
  for (const LandscapeTile& tile : m_definition.tiles)
  {
    _hash.Add(tile.x);
    _hash.Add(tile.y);
    _hash.Add(tile.extent);
    _hash.Add(tile.fractalDimensionHundredths);
    _hash.Add(tile.amplitude);
    _hash.Add(tile.desiredHeight);
    _hash.Add(tile.heightShift);
    _hash.Add(tile.lowlandExponentHundredths);
    _hash.Add(tile.method);
    _hash.Add(tile.edgeFalloff);
  }
  for (const std::vector<CellPosition>* positions : {&m_definition.starts, &m_definition.deposits})
  {
    _hash.Add(static_cast<std::uint32_t>(positions->size()));
    for (const CellPosition& position : *positions)
    {
      _hash.Add(position.x);
      _hash.Add(position.y);
    }
  }
  _hash.Add(static_cast<std::uint32_t>(m_deltas.size()));
  for (const HeightDelta& delta : m_deltas)
  {
    _hash.Add(delta.x);
    _hash.Add(delta.y);
    _hash.Add(delta.width);
    _hash.Add(delta.height);
    _hash.AddSpan(std::span<const std::int16_t>(delta.heights));
  }
}

void Landscape::DeriveCells(std::uint32_t _cellX0, std::uint32_t _cellY0, std::uint32_t _cellX1, std::uint32_t _cellY1) noexcept
{
  // The tool's analyse, cell by cell: the lowest sample decides water, the steepest step between
  // two adjacent samples, along a row or a column, decides the slope.
  const std::uint32_t side = m_samplesPerSide;
  for (std::uint32_t cellY = _cellY0; cellY < _cellY1; ++cellY)
  {
    for (std::uint32_t cellX = _cellX0; cellX < _cellX1; ++cellX)
    {
      std::int32_t lowest = 32767;
      std::int32_t steepest = 0;
      const std::size_t origin =
        static_cast<std::size_t>(cellY) * SAMPLES_PER_CELL_EDGE * side + static_cast<std::size_t>(cellX) * SAMPLES_PER_CELL_EDGE;
      for (std::uint32_t j = 0; j <= SAMPLES_PER_CELL_EDGE; ++j)
      {
        const std::size_t row = origin + static_cast<std::size_t>(j) * side;
        std::int32_t previous = m_heights[row];
        lowest = std::min(lowest, previous);
        for (std::uint32_t i = 1; i <= SAMPLES_PER_CELL_EDGE; ++i)
        {
          const std::int32_t current = m_heights[row + i];
          steepest = std::max(steepest, current >= previous ? current - previous : previous - current);
          lowest = std::min(lowest, current);
          previous = current;
        }
      }
      for (std::uint32_t i = 0; i <= SAMPLES_PER_CELL_EDGE; ++i)
      {
        std::int32_t previous = m_heights[origin + i];
        for (std::uint32_t j = 1; j <= SAMPLES_PER_CELL_EDGE; ++j)
        {
          const std::int32_t current = m_heights[origin + static_cast<std::size_t>(j) * side + i];
          steepest = std::max(steepest, current >= previous ? current - previous : previous - current);
          previous = current;
        }
      }
      Cell& cell = m_cells[static_cast<std::size_t>(cellY) * m_cellsPerSide + cellX];
      cell.slopePercent = static_cast<std::uint16_t>(std::min<std::int32_t>(65535, steepest * 100 / SAMPLE_SPACING_WORLD_UNITS));
      cell.flags = lowest < 0 ? CELL_WATER : 0;
    }
  }
}

} // namespace Frontier
