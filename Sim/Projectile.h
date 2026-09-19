#pragma once

#include "ObjectId.h"

#include <cstdint>

// A shot in flight (GameDesign.md §8). Direct fire is decided at the shot and needs no record;
// what flies is indirect fire and anything whose impact is resolved later, so a projectile carries
// where it came from, where it is going and how long until it lands. Units are
// TechnicalDesign.md §4.1's: subunits and ticks.

namespace Frontier
{

struct Projectile
{
  std::uint8_t seat;    ///< Who fired it, so that a kill credits the right commander
  ObjectId shooter;     ///< The device or structure that fired, or NO_OBJECT once it is gone
  std::uint32_t module; ///< Row index in the module table: the damage, class and splash are its

  std::int32_t x; ///< Subunits
  std::int32_t y;
  std::int32_t z;

  /// Where it lands. Indirect fire is aimed at the ground, so the impact is a point rather than an
  /// object, and a target that moves out from under it is missed rather than followed.
  std::int32_t impactX;
  std::int32_t impactY;
  std::int32_t impactZ;
  std::uint32_t ticksToImpact;

  /// What the shot was fired with, captured at the muzzle rather than looked up at the impact: the
  /// chance it hits what the splash catches and the damage it carries, both already through the
  /// shooter's rank and its commander's research (Sim/Weapons.h). A shell outlives its shooter, so
  /// a shot fired by a device that dies in the two seconds it is in the air lands as it was fired.
  std::int32_t hitPercent;
  std::int32_t damage;

  [[nodiscard]] constexpr bool operator==(const Projectile&) const noexcept = default;
};

} // namespace Frontier
