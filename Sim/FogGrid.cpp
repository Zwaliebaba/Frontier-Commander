#include "pch.h"

#include "FogGrid.h"

#include "Assertion.h"

namespace Frontier
{

void FogGrid::Resize(std::uint32_t _cellsPerSide)
{
  m_cellsPerSide = _cellsPerSide;
  const std::size_t cells = static_cast<std::size_t>(_cellsPerSide) * _cellsPerSide;
  m_viewers.assign(cells, 0);
  m_state.assign(cells, FogState::Unexplored);
}

bool FogGrid::Restore(std::uint32_t _cellsPerSide, std::vector<std::uint16_t> _viewers, std::vector<FogState> _state)
{
  const std::size_t cells = static_cast<std::size_t>(_cellsPerSide) * _cellsPerSide;
  if (_viewers.size() != cells || _state.size() != cells)
  {
    return false;
  }
  m_cellsPerSide = _cellsPerSide;
  m_viewers = std::move(_viewers);
  m_state = std::move(_state);
  return true;
}

FogState FogGrid::StateAt(std::uint32_t _cellX, std::uint32_t _cellY) const noexcept
{
  if (!Inside(_cellX, _cellY))
  {
    return FogState::Unexplored;
  }
  return m_state[static_cast<std::size_t>(_cellY) * m_cellsPerSide + _cellX];
}

std::uint16_t FogGrid::ViewersAt(std::uint32_t _cellX, std::uint32_t _cellY) const noexcept
{
  if (!Inside(_cellX, _cellY))
  {
    return 0;
  }
  return m_viewers[static_cast<std::size_t>(_cellY) * m_cellsPerSide + _cellX];
}

void FogGrid::AddViewer(std::uint32_t _cellX, std::uint32_t _cellY) noexcept
{
  if (!Inside(_cellX, _cellY))
  {
    return;
  }
  const std::size_t index = static_cast<std::size_t>(_cellY) * m_cellsPerSide + _cellX;
  // The header says why this cannot overflow: 65,535 is beyond every viewer an alliance may field.
  FRONTIER_ASSERT(m_viewers[index] != 0xFFFFu);
  ++m_viewers[index];
  m_state[index] = FogState::Visible;
}

void FogGrid::RemoveViewer(std::uint32_t _cellX, std::uint32_t _cellY) noexcept
{
  if (!Inside(_cellX, _cellY))
  {
    return;
  }
  const std::size_t index = static_cast<std::size_t>(_cellY) * m_cellsPerSide + _cellX;
  // A decrement with no matching increment is a bug in the caller, not a state to fall into: the
  // stamps are what keep the two in step, and a count that went negative would wrap to 65,535.
  FRONTIER_ASSERT(m_viewers[index] != 0);
  if (m_viewers[index] == 0)
  {
    return;
  }
  --m_viewers[index];
  if (m_viewers[index] == 0)
  {
    m_state[index] = FogState::Explored;
  }
}

} // namespace Frontier
