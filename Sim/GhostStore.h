#pragma once

#include "ObjectId.h"

#include <cstdint>
#include <span>
#include <vector>

// What an explored-but-not-visible map shows (TechnicalDesign.md §4.6): the last-seen record of
// every structure a commander has ever seen. It is what an attack on an unseen target is
// redirected to (GameDesign.md §8), what a rejoining client gets back, and the reason a commander
// who scouted a base once still sees it drawn after the scout dies.
//
// A ghost is stale by design. The structure may be long destroyed and the record stays until the
// commander sees the cell again; that is the whole point of it.
//
// Only structures. A device moves, so a record of where one was a minute ago is a lie rather than
// a memory, and GameDesign.md §3 says fog hides devices outright.

namespace Frontier
{

struct Ghost
{
  ObjectId structure; ///< Stale by design: the structure may be gone
  std::uint8_t seat;  ///< Who owned it when it was last seen
  std::uint32_t design;
  std::uint32_t cellX;
  std::uint32_t cellY;
  std::uint32_t seenTick;

  [[nodiscard]] constexpr bool operator==(const Ghost&) const noexcept = default;
};

class GhostStore
{
public:
  /// Records or updates what the seat can see now. Ascending by structure id, so that two hosts
  /// that saw the same things in a different order hold the same bytes (AGENTS.md R16).
  void Record(const Ghost& _ghost);

  /// The record for a structure, or null. The pointer is invalidated by the next Record.
  [[nodiscard]] const Ghost* Find(ObjectId _structure) const noexcept;

  /// Forgets one. Used only when a commander sees the cell and finds nothing there, which is how
  /// a ghost of a demolished structure is cleared rather than left for ever.
  bool Forget(ObjectId _structure);

  void Clear() noexcept
  {
    m_ghosts.clear();
  }

  [[nodiscard]] std::span<const Ghost> All() const noexcept
  {
    return m_ghosts;
  }

  [[nodiscard]] std::size_t Count() const noexcept
  {
    return m_ghosts.size();
  }

  /// Takes a store read from a snapshot; false when the records are not in ascending id order,
  /// because every reader and the hash depend on that order.
  [[nodiscard]] bool Restore(std::vector<Ghost> _ghosts);

  [[nodiscard]] bool operator==(const GhostStore&) const noexcept = default;

private:
  std::vector<Ghost> m_ghosts; ///< Ascending by structure id
};

} // namespace Frontier
