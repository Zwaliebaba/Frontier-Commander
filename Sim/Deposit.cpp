#include "pch.h"

#include "Deposit.h"

#include <algorithm>

namespace Frontier
{

namespace
{

/// Row then column, which is the order the field is held in and the order a bisection assumes.
[[nodiscard]] constexpr bool Before(const Deposit& _left, const Deposit& _right) noexcept
{
  return _left.cellY != _right.cellY ? _left.cellY < _right.cellY : _left.cellX < _right.cellX;
}

} // namespace

void DepositField::Build(const LandscapeDefinition& _definition)
{
  m_deposits.clear();
  m_deposits.reserve(_definition.deposits.size());
  for (const CellPosition& cell : _definition.deposits)
  {
    if (cell.x < _definition.cellsPerSide && cell.y < _definition.cellsPerSide)
    {
      m_deposits.push_back({cell.x, cell.y});
    }
  }
  std::sort(m_deposits.begin(), m_deposits.end(), Before);
  m_deposits.erase(std::unique(m_deposits.begin(), m_deposits.end()), m_deposits.end());
}

void DepositField::Clear() noexcept
{
  m_deposits.clear();
}

std::uint32_t DepositField::IndexAt(std::uint32_t _cellX, std::uint32_t _cellY) const noexcept
{
  const Deposit wanted{_cellX, _cellY};
  const auto found = std::lower_bound(m_deposits.begin(), m_deposits.end(), wanted, Before);
  if (found == m_deposits.end() || *found != wanted)
  {
    return NO_DEPOSIT;
  }
  return static_cast<std::uint32_t>(found - m_deposits.begin());
}

} // namespace Frontier
