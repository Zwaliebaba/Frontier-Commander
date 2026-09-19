#pragma once

#include <cstdint>

// Identity in the simulation (TechnicalDesign.md §4.3): a creation counter paired with a kind tag,
// never a container index. Species' owner recorded the same decision for Species' own future
// protocol on 2026-08-02: a slot index is reused, so a stale reference silently aliases whatever
// occupies the slot later, while a counter incremented in creation order can never alias. The
// counter is World's and is shared by every kind, so no two objects of a match ever carry one
// value; the kind says which map resolves it.

namespace Frontier
{

enum class ObjectKind : std::uint8_t
{
  Device,
  Structure,
  Projectile,
  Feature,
  Wreck
};

inline constexpr std::uint8_t OBJECT_KIND_COUNT = 5;

struct ObjectId
{
  std::uint32_t value; ///< World's creation counter; 0 names no object, whatever the kind says
  ObjectKind kind;

  [[nodiscard]] constexpr bool Valid() const noexcept
  {
    return value != 0;
  }

  [[nodiscard]] constexpr bool operator==(const ObjectId&) const noexcept = default;
};

/// What a record carries where it refers to nothing: no target, no carrier, no killer.
inline constexpr ObjectId NO_OBJECT = {0, ObjectKind::Device};

} // namespace Frontier
