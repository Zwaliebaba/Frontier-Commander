#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The component tables of GameDesign.md §6: chassis, drives and modules, as plain aggregates
// (AGENTS.md R8) one row apiece, filled by ContentLoader from Content\Components.json. Every
// number is an integer in the unit its name says (AGENTS.md R6; ADR-002): hundredths for factors
// and percentages, ticks for times, subunits for distances, power in hundredths. Nothing here is
// a float, because Sim reads these rows and R16 forbids one.
//
// Hit points and damage are whole points, as the design tables write them; a rate that does not
// divide into the tick is held in hundredths per tick, so that the simulation accumulates it
// without a remainder it has to carry by hand.

namespace Frontier
{

/// The five weapon classes of the damage matrix (GameDesign.md §8). The order is the matrix's
/// row order and the wire's.
enum class WeaponClass : std::uint8_t
{
  AntiLight,
  AntiTank,
  Flame,
  Artillery,
  Energy
};

inline constexpr std::uint8_t WEAPON_CLASS_COUNT = 5;

/// Which of a target's two armour values a weapon class meets (GameDesign.md §8).
enum class ArmorKind : std::uint8_t
{
  Kinetic,
  Thermal
};

/// The six drive classes. They are also the first six target columns of the damage matrix, which
/// is why their order is fixed here and read there.
enum class DriveClass : std::uint8_t
{
  Wheels,
  HalfTrack,
  Tracks,
  Hover,
  Legs,
  Lift
};

inline constexpr std::uint8_t DRIVE_CLASS_COUNT = 6;

/// The three chassis classes. A mark is a row of its own (Light II is not Light I upgraded), and
/// a class upgrade from research is a percentage applied to every row of the class.
enum class ChassisClass : std::uint8_t
{
  Light,
  Medium,
  Heavy
};

inline constexpr std::uint8_t CHASSIS_CLASS_COUNT = 3;

/// What a module does other than shoot (GameDesign.md §6). A module is a weapon when its system
/// kind is None, and exactly one of the two halves of ModuleDesc is then read.
enum class SystemKind : std::uint8_t
{
  None,
  Builder,
  Sensor,
  Repair,
  Command
};

/// How a weapon reaches its target: a direct weapon decides the hit as it fires, an indirect one
/// at impact against whatever stands in the splash then, and needs a spotter (GameDesign.md §8).
enum class FireKind : std::uint8_t
{
  Direct,
  Indirect
};

struct ChassisDesc
{
  std::string id;
  std::string name;
  ChassisClass chassisClass;
  std::string unlockedBy; ///< A research item's id, or empty for a row available from the first tick
  std::string model;      ///< A model id; its MarkerMount* and MarkerDrive* carry the other parts
  /// The factor the model is drawn at, in hundredths of its authored size; 100 is native. It sits
  /// on the row rather than on the model so that one model serves two rows at two sizes, which is
  /// what a placeholder primitive does before the authored models exist (SpeciesLineage.md §5).
  std::int32_t modelScaleHundredths = 100;
  std::int32_t hitPoints;
  std::int32_t kineticArmor;
  std::int32_t thermalArmor;
  std::int32_t baseSpeedSubunitsPerTick; ///< 80 world units a second is 1,024 at 20 Hz
  std::int32_t sightSubunits;
  std::int32_t costHundredths;
  std::uint8_t mounts; ///< How many module mounts, and how many MarkerMount* the model carries

  [[nodiscard]] bool operator==(const ChassisDesc&) const noexcept = default;
};

struct DriveDesc
{
  std::string id;
  std::string name;
  DriveClass driveClass;
  std::string unlockedBy;
  std::string model;
  /// The factor the model is drawn at, in hundredths of its authored size; 100 is native. It sits
  /// on the row rather than on the model so that one model serves two rows at two sizes, which is
  /// what a placeholder primitive does before the authored models exist (SpeciesLineage.md §5).
  std::int32_t modelScaleHundredths = 100;
  std::int32_t speedFactorHundredths;
  std::int32_t maxSlopePercent;
  bool crossesWater;
  std::int32_t hitPointFactorHundredths;
  std::int32_t costHundredths;

  [[nodiscard]] bool operator==(const DriveDesc&) const noexcept = default;
};

struct ModuleDesc
{
  std::string id;
  std::string name;
  SystemKind systemKind; ///< None for a weapon; the weapon half is read only then
  std::string unlockedBy;
  std::string model; ///< Its MarkerMuzzle is where a shot leaves
  /// The factor the model is drawn at, in hundredths of its authored size; 100 is native. It sits
  /// on the row rather than on the model so that one model serves two rows at two sizes, which is
  /// what a placeholder primitive does before the authored models exist (SpeciesLineage.md §5).
  std::int32_t modelScaleHundredths = 100;
  std::int32_t weightPenaltyPercent;
  std::int32_t costHundredths;
  std::uint8_t chassisClassMask; ///< Bit per ChassisClass; 0 means every chassis takes it

  // The weapon half (systemKind == None).
  WeaponClass weaponClass;
  std::int32_t damage;
  std::uint8_t shotsPerSalvo; ///< 1 but for a burst weapon
  std::uint32_t reloadTicks;
  FireKind fireKind;
  std::int32_t minimumRangeSubunits; ///< Indirect weapons alone; 0 otherwise
  std::int32_t shortRangeSubunits;
  std::int32_t longRangeSubunits;
  std::int32_t shortHitPercent;
  std::int32_t longHitPercent;
  std::int32_t splashSubunits;

  // The system half (systemKind != None).
  std::int32_t systemRangeSubunits;              ///< Builder, Repair: how far it reaches
  std::int32_t buildPowerHundredthsPerTick;      ///< Builder: 10 power a second is 50 at 20 Hz
  std::int32_t repairHitPointsHundredthsPerTick; ///< Repair: 15 points a second is 75 at 20 Hz
  std::int32_t sightSubunits;                    ///< Sensor: replaces the chassis's when larger

  [[nodiscard]] bool operator==(const ModuleDesc&) const noexcept = default;
};

/// Bit per ChassisClass for ModuleDesc::chassisClassMask.
[[nodiscard]] constexpr std::uint8_t ChassisClassBit(ChassisClass _class) noexcept
{
  return static_cast<std::uint8_t>(1U << static_cast<std::uint8_t>(_class));
}

struct ComponentTables
{
  std::vector<ChassisDesc> chassis;
  std::vector<DriveDesc> drives;
  std::vector<ModuleDesc> modules;

  [[nodiscard]] bool operator==(const ComponentTables&) const noexcept = default;
};

} // namespace Frontier
