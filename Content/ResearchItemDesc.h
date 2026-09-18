#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The research tree of GameDesign.md §7: one row per item, each with up to three prerequisites,
// a cost, a time, and one of two effects — it makes a component or structure available, or it
// applies a percentage to a class, retroactively, to everything of that class already standing.
// Validated as a whole by ContentValidator: every prerequisite exists, no cycle, every unlock
// names a real row.

namespace Frontier
{

inline constexpr std::size_t MAX_PREREQUISITES = 3;

/// What an item does when it completes. An Unlock names a component or structure id; every
/// upgrade names a class and carries a percentage added to the hundred.
enum class ResearchEffect : std::uint8_t
{
  Unlock,
  ChassisArmor,     ///< Per ChassisClass
  ChassisHitPoints, ///< Per ChassisClass
  WeaponDamage,     ///< Per WeaponClass
  WeaponRate,       ///< Per WeaponClass
  WeaponAccuracy,   ///< Per WeaponClass
  ExtractorRate,    ///< Every extractor
  StructureHitPoints
};

struct ResearchItemDesc
{
  std::string id;
  std::string name;
  std::string description; ///< The line the research panel shows (Design/Interface.md)
  std::vector<std::string> prerequisites;
  std::int32_t costHundredths;
  std::uint32_t timeTicks;
  ResearchEffect effect;
  std::string unlocks;         ///< Effect Unlock: a chassis, drive, module or structure id
  std::uint8_t targetClass;    ///< An upgrade: the ChassisClass or WeaponClass it applies to
  std::int32_t upgradePercent; ///< An upgrade: added to the hundred, so 15 means times 1.15

  [[nodiscard]] bool operator==(const ResearchItemDesc&) const noexcept = default;
};

} // namespace Frontier
