#pragma once

#include "ComponentDesc.h"
#include "StructureDesc.h"

#include <array>
#include <cstdint>

// The damage matrix of GameDesign.md §8: five weapon classes by ten target classes — the six
// drive classes, then the four strength classes — of integer percentages, plus, per weapon class,
// how much of the target's armour counts against it and which of the two armour values it meets.
// This table is the whole of the rock-paper-scissors, and it is data so that rebalancing is an
// edit rather than a build.

namespace Frontier
{

/// Six drives then four strengths, in the order their enumerations declare them.
inline constexpr std::uint8_t TARGET_CLASS_COUNT = DRIVE_CLASS_COUNT + STRENGTH_CLASS_COUNT;

/// The matrix column a device with this drive is hit in.
[[nodiscard]] constexpr std::uint8_t TargetColumnOf(DriveClass _drive) noexcept
{
  return static_cast<std::uint8_t>(_drive);
}

/// The matrix column a structure of this strength is hit in.
[[nodiscard]] constexpr std::uint8_t TargetColumnOf(StrengthClass _strength) noexcept
{
  return static_cast<std::uint8_t>(DRIVE_CLASS_COUNT + static_cast<std::uint8_t>(_strength));
}

struct DamageTable
{
  /// [weapon class][target class], a percentage applied to the weapon's damage.
  std::array<std::array<std::int32_t, TARGET_CLASS_COUNT>, WEAPON_CLASS_COUNT> modifierPercent;
  /// How much of the target's armour this weapon class meets: 100 for the kinetic weapons and
  /// flame, 50 for energy, 0 for artillery.
  std::array<std::int32_t, WEAPON_CLASS_COUNT> armorFactorPercent;
  /// Which armour value it meets.
  std::array<ArmorKind, WEAPON_CLASS_COUNT> armorKind;

  [[nodiscard]] bool operator==(const DamageTable&) const noexcept = default;
};

/// The damage one hit deals, by the formula of GameDesign.md §8: the weapon's damage scaled by the
/// matrix, less the armour that counts, and never under a third of the scaled damage. Integer
/// throughout (R16), so the simulation and the design screen agree to the point.
[[nodiscard]] constexpr std::int32_t DamageDealt(const DamageTable& _table, WeaponClass _weapon, std::uint8_t _targetColumn,
                                                 std::int32_t _damage, std::int32_t _armor) noexcept
{
  const std::size_t weapon = static_cast<std::size_t>(_weapon);
  const std::int32_t scaled = _damage * _table.modifierPercent[weapon][_targetColumn] / 100;
  const std::int32_t counted = _armor * _table.armorFactorPercent[weapon] / 100;
  const std::int32_t reduced = scaled - counted;
  const std::int32_t floor = scaled / 3;
  return reduced > floor ? reduced : floor;
}

} // namespace Frontier
