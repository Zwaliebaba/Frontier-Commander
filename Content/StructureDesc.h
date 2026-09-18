#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The structure catalogue of GameDesign.md §5 and the modules built onto a standing structure,
// as plain rows. Footprints are in cells, because placement is a cell rule; every other distance
// is in subunits, as the simulation holds it.

namespace Frontier
{

/// The four strength classes the damage matrix reads as its last four target columns
/// (GameDesign.md §8). Their order is the matrix's column order after the six drives.
enum class StrengthClass : std::uint8_t
{
  Soft,
  Medium,
  Hard,
  Bunker
};

inline constexpr std::uint8_t STRENGTH_CLASS_COUNT = 4;

/// What a structure is for. The simulation branches on this rather than on an id, so that a mod
/// may add a second factory without adding a rule.
enum class StructureRole : std::uint8_t
{
  CommandPost,
  Extractor,
  Generator,
  Factory,
  ResearchLab,
  RepairBay,
  SensorTower,
  Wall,
  Hardpoint,
  Tower,
  Bunker,
  Uplink
};

/// What a module built onto a structure does (GameDesign.md §5).
enum class StructureModuleEffect : std::uint8_t
{
  ShortenBuildTime,    ///< Factory: a percentage off its build times
  ShortenResearchTime, ///< Lab: a percentage off its research
  ServeMoreExtractors, ///< Generator: how many more it serves
  AddWeapon            ///< Hardpoint: the weapon module is chosen from the researched set
};

struct StructureModuleDesc
{
  std::string id;
  std::string name;
  std::string unlockedBy;
  std::string model;
  /// The factor the model is drawn at, in hundredths of its authored size; 100 is native. It sits
  /// on the row rather than on the model so that one model serves two rows at two sizes, which is
  /// what a placeholder primitive does before the authored models exist (SpeciesLineage.md §5).
  std::int32_t modelScaleHundredths = 100;
  std::int32_t costHundredths;
  std::uint32_t buildTimeTicks;
  StructureModuleEffect effect;
  std::int32_t amount; ///< A percentage for the two times, a count for the generator

  [[nodiscard]] bool operator==(const StructureModuleDesc&) const noexcept = default;
};

struct StructureDesc
{
  std::string id;
  std::string name;
  StructureRole role;
  StrengthClass strength;
  std::string unlockedBy;
  std::string model;
  /// The factor the model is drawn at, in hundredths of its authored size; 100 is native. It sits
  /// on the row rather than on the model so that one model serves two rows at two sizes, which is
  /// what a placeholder primitive does before the authored models exist (SpeciesLineage.md §5).
  std::int32_t modelScaleHundredths = 100;
  std::uint32_t footprintCellsX;
  std::uint32_t footprintCellsY;
  std::int32_t hitPoints;
  std::int32_t kineticArmor;
  std::int32_t thermalArmor;
  std::int32_t costHundredths;
  std::uint32_t buildTimeTicks;
  std::int32_t sightSubunits;
  std::uint8_t moduleSlots;
  std::vector<std::string> modules; ///< The StructureModuleDesc ids this structure takes

  /// Role-specific numbers, read only by the role that names them.
  std::int32_t powerHundredthsPerTick;           ///< CommandPost's trickle, Extractor's yield on a served deposit
  std::uint32_t servesExtractors;                ///< Generator: how many extractors one serves before its modules
  std::int32_t serviceRangeSubunits;             ///< Generator: how far it serves; RepairBay: how far it reaches
  std::int32_t repairHitPointsHundredthsPerTick; ///< RepairBay
  std::string weapon;                            ///< Tower, Bunker: the module id it carries

  [[nodiscard]] bool operator==(const StructureDesc&) const noexcept = default;
};

struct StructureTables
{
  std::vector<StructureDesc> structures;
  std::vector<StructureModuleDesc> modules;

  [[nodiscard]] bool operator==(const StructureTables&) const noexcept = default;
};

} // namespace Frontier
