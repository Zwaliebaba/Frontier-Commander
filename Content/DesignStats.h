#pragma once

#include "ContentTree.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

// The derived statistics of GameDesign.md §6: what a chassis, a drive and modules add up to. The
// design screen shows them before the commander commits, the simulation gives them to the device
// it spawns, and both read this one implementation, so that what was promised is what was built.
//
// Integer arithmetic throughout (AGENTS.md R16): every factor is in hundredths and the division
// happens once, at the end, rounded half up, so that 80 x 1.3 is 104 and not 103.
//
// Research upgrades are percentages applied to a class, retroactively. They are passed in rather
// than read from a seat, because Content knows nothing of seats.

namespace Frontier
{

/// The class upgrades in force when the statistics are derived, each a percentage added to the
/// hundred. A zeroed value is no upgrade.
struct ClassUpgrades
{
  std::array<std::int32_t, CHASSIS_CLASS_COUNT> chassisArmorPercent{};
  std::array<std::int32_t, CHASSIS_CLASS_COUNT> chassisHitPointPercent{};
  std::array<std::int32_t, WEAPON_CLASS_COUNT> weaponDamagePercent{};
  std::array<std::int32_t, WEAPON_CLASS_COUNT> weaponRatePercent{};
  std::array<std::int32_t, WEAPON_CLASS_COUNT> weaponAccuracyPercent{};

  [[nodiscard]] bool operator==(const ClassUpgrades&) const noexcept = default;
};

/// A named combination of a chassis, a drive and modules: what the design screen commits and what
/// the derivation below reads. Ids rather than pointers, because a design outlives a content reload
/// and is saved between matches (TechnicalDesign.md §9).
///
/// It is NOT what a seat holds. Sim's DeviceDesign is the same idea in row indices, because a
/// design is hashed and sent every tick and strings are neither cheap nor fixed-layout; the two
/// were both called DeviceDesign until m1-vertical-slice/S5 needed one translation unit to see
/// both, which is a hard collision in one namespace rather than a naming preference.
struct DesignRecipe
{
  std::string id;
  std::string name;
  std::string chassis;
  std::string drive;
  std::vector<std::string> modules;

  [[nodiscard]] bool operator==(const DesignRecipe&) const noexcept = default;
};

/// What the parts add up to. Every field is in the unit its name says.
struct DesignStats
{
  std::int32_t speedSubunitsPerTick;
  std::int32_t hitPoints;
  std::int32_t kineticArmor;
  std::int32_t thermalArmor;
  std::int32_t costHundredths;
  std::uint32_t buildTimeTicks;
  std::int32_t sightSubunits;
  std::int32_t maxSlopePercent;
  bool crossesWater;

  [[nodiscard]] bool operator==(const DesignStats&) const noexcept = default;
};

/// Why a design cannot be built. The design screen shows the reason and refuses to commit, and
/// the simulation rejects the order with the same code (m1-vertical-slice/S2).
enum class DesignFault : std::uint8_t
{
  None,
  UnknownChassis,
  UnknownDrive,
  UnknownModule,
  NoModules,
  TooManyModules,
  ModuleRefusesChassis
};

/// Derives the statistics of a design. Returns the fault when the design is not buildable, in
/// which case _out is untouched.
[[nodiscard]] DesignFault DeriveDesignStats(const ContentTree& _tree, const DesignRecipe& _design, const ClassUpgrades& _upgrades,
                                            DesignStats& _out);

/// The ticks a factory takes to build a design of this cost, before the factory's modules: the
/// cost divided by the ten power a second of GameDesign.md §6.
[[nodiscard]] std::uint32_t BuildTimeTicksFor(std::int32_t _costHundredths) noexcept;

/// Subunits per tick as whole world units per second, rounded half up: what the design screen
/// prints, and the only place the simulation's unit becomes the design's.
[[nodiscard]] std::int32_t WorldUnitsPerSecond(std::int32_t _subunitsPerTick) noexcept;

} // namespace Frontier
