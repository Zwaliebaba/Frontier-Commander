#include "pch.h"

#include "GhostStore.h"

#include <algorithm>

namespace Frontier
{

namespace
{

[[nodiscard]] bool Before(const Ghost& _ghost, std::uint32_t _id) noexcept
{
  return _ghost.structure.value < _id;
}

} // namespace

void GhostStore::Record(const Ghost& _ghost)
{
  const auto at = std::lower_bound(m_ghosts.begin(), m_ghosts.end(), _ghost.structure.value, Before);
  if (at != m_ghosts.end() && at->structure == _ghost.structure)
  {
    *at = _ghost;
    return;
  }
  m_ghosts.insert(at, _ghost);
}

const Ghost* GhostStore::Find(ObjectId _structure) const noexcept
{
  const auto at = std::lower_bound(m_ghosts.begin(), m_ghosts.end(), _structure.value, Before);
  if (at == m_ghosts.end() || at->structure != _structure)
  {
    return nullptr;
  }
  return &*at;
}

bool GhostStore::Forget(ObjectId _structure)
{
  const auto at = std::lower_bound(m_ghosts.begin(), m_ghosts.end(), _structure.value, Before);
  if (at == m_ghosts.end() || at->structure != _structure)
  {
    return false;
  }
  m_ghosts.erase(at);
  return true;
}

bool GhostStore::Restore(std::vector<Ghost> _ghosts)
{
  for (std::size_t index = 1; index < _ghosts.size(); ++index)
  {
    if (_ghosts[index - 1].structure.value >= _ghosts[index].structure.value)
    {
      return false;
    }
  }
  m_ghosts = std::move(_ghosts);
  return true;
}

} // namespace Frontier
