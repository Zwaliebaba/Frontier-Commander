#include "pch.h"

#include "Seat.h"

namespace Frontier
{

void SizeFog(Seat& _seat, std::uint32_t _cellsPerSide)
{
  const std::size_t cells = static_cast<std::size_t>(_cellsPerSide) * _cellsPerSide;
  _seat.fogViewers.assign(cells, 0);
  _seat.fogState.assign(cells, FogState::Unexplored);
}

} // namespace Frontier
