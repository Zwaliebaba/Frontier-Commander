#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <CppUnitTest.h>

// The content documents the loader and validator tests are written against: one tree that is
// correct, and one deliberate fault per rule, each as the whole file it belongs to so that a test
// can assert the line the diagnostic names. A fixture is written to a scratch directory by the
// test and never read from Content\ in place (m1-vertical-slice/C1).
//
// Every fixture is laid out so that the faulty line is where the comment beside it says, and the
// tests assert that line by number. Editing one means editing its expected line with it, which is
// the point: a diagnostic that moves without anyone noticing is a diagnostic nobody trusts.

namespace ContentTests
{

/// A minimal tree that loads and validates clean: one chassis, one drive, one weapon module, one
/// structure with a module, two research items in a chain, the damage matrix, one biome, one sound.
inline constexpr std::string_view GOOD_COMPONENTS = R"({
  "version": 1,
  "chassis": [
    {
      "id": "LightI", "name": "Light I", "class": "Light", "model": "LightI",
      "hitPoints": 100, "kineticArmor": 5, "thermalArmor": 5,
      "baseSpeedSubunitsPerTick": 1024, "sightSubunits": 327680,
      "costHundredths": 6000, "mounts": 1
    },
    {
      "id": "HeavyI", "name": "Heavy I", "class": "Heavy", "model": "HeavyI",
      "hitPoints": 500, "kineticArmor": 25, "thermalArmor": 18,
      "baseSpeedSubunitsPerTick": 512, "sightSubunits": 262144,
      "costHundredths": 32000, "mounts": 1
    }
  ],
  "drives": [
    {
      "id": "Wheels", "name": "Wheels", "class": "Wheels", "model": "Wheels",
      "speedFactorHundredths": 130, "maxSlopePercent": 25, "crossesWater": false,
      "hitPointFactorHundredths": 100, "costHundredths": 3000
    },
    {
      "id": "Tracks", "name": "Tracks", "class": "Tracks", "model": "Tracks",
      "speedFactorHundredths": 80, "maxSlopePercent": 40, "crossesWater": false,
      "hitPointFactorHundredths": 150, "costHundredths": 7000
    }
  ],
  "modules": [
    {
      "id": "MachineGun", "name": "Machine gun", "systemKind": "None", "model": "MachineGun",
      "weightPenaltyPercent": 0, "costHundredths": 4000,
      "weaponClass": "AntiLight", "damage": 8, "shotsPerSalvo": 1, "reloadTicks": 5,
      "fireKind": "Direct", "shortRangeSubunits": 131072, "longRangeSubunits": 196608,
      "shortHitPercent": 80, "longHitPercent": 50
    },
    {
      "id": "Cannon", "name": "Cannon", "systemKind": "None", "model": "Cannon",
      "weightPenaltyPercent": 10, "costHundredths": 10000,
      "weaponClass": "AntiTank", "damage": 60, "shotsPerSalvo": 1, "reloadTicks": 40,
      "fireKind": "Direct", "shortRangeSubunits": 163840, "longRangeSubunits": 262144,
      "shortHitPercent": 70, "longHitPercent": 45
    },
    {
      "id": "Builder", "name": "Builder", "systemKind": "Builder", "model": "Builder",
      "weightPenaltyPercent": 10, "costHundredths": 5000,
      "systemRangeSubunits": 32768, "buildPowerHundredthsPerTick": 50
    }
  ]
})";

inline constexpr std::string_view GOOD_STRUCTURES = R"({
  "version": 1,
  "structures": [
    {
      "id": "Factory", "name": "Factory", "role": "Factory", "strength": "Medium", "model": "Factory",
      "footprintCellsX": 3, "footprintCellsY": 3, "hitPoints": 800,
      "kineticArmor": 10, "thermalArmor": 10, "costHundredths": 40000,
      "buildTimeTicks": 1200, "sightSubunits": 196608, "moduleSlots": 2,
      "modules": ["FactoryModule"]
    },
    {
      "id": "Extractor", "name": "Extractor", "role": "Extractor", "strength": "Soft", "model": "Extractor",
      "footprintCellsX": 1, "footprintCellsY": 1, "hitPoints": 200,
      "kineticArmor": 2, "thermalArmor": 2, "costHundredths": 5000,
      "buildTimeTicks": 300, "sightSubunits": 196608, "moduleSlots": 0,
      "powerHundredthsPerTick": 25
    }
  ],
  "modules": [
    {
      "id": "FactoryModule", "name": "Factory module", "model": "FactoryModule",
      "costHundredths": 15000, "buildTimeTicks": 400,
      "effect": "ShortenBuildTime", "amount": 25
    }
  ]
})";

inline constexpr std::string_view GOOD_RESEARCH = R"({
  "version": 1,
  "items": [
    {
      "id": "Basics", "name": "Basics", "description": "The first item",
      "prerequisites": [], "costHundredths": 5000, "timeTicks": 600,
      "effect": "Unlock", "unlocks": "MachineGun"
    },
    {
      "id": "Armour", "name": "Armour", "description": "Tougher light chassis",
      "prerequisites": ["Basics"], "costHundredths": 12000, "timeTicks": 900,
      "effect": "ChassisArmor", "targetClass": 0, "upgradePercent": 15
    }
  ]
})";

inline constexpr std::string_view GOOD_DAMAGE = R"({
  "version": 1,
  "weapons": [
    { "class": "AntiLight", "armorFactorPercent": 100, "armorKind": "Kinetic",
      "modifierPercent": [120, 100, 50, 110, 130, 100, 120, 60, 30, 20] },
    { "class": "AntiTank", "armorFactorPercent": 100, "armorKind": "Kinetic",
      "modifierPercent": [90, 100, 120, 90, 70, 60, 80, 100, 110, 60] },
    { "class": "Flame", "armorFactorPercent": 100, "armorKind": "Thermal",
      "modifierPercent": [130, 110, 70, 120, 140, 40, 150, 80, 40, 10] },
    { "class": "Artillery", "armorFactorPercent": 0, "armorKind": "Kinetic",
      "modifierPercent": [100, 100, 100, 100, 100, 20, 130, 120, 100, 60] },
    { "class": "Energy", "armorFactorPercent": 50, "armorKind": "Kinetic",
      "modifierPercent": [100, 100, 100, 100, 100, 100, 100, 100, 100, 80] }
  ]
})";

inline constexpr std::string_view GOOD_BIOMES = R"({
  "version": 1,
  "biomes": [
    {
      "id": "Default", "name": "The Garden", "paletteTexture": "LandscapeDefault.dds",
      "key": { "directionHundredths": [4, 39, -92], "colorHundredths": [106, 96, 72] },
      "sun": { "directionHundredths": [57, 0, -82], "colorHundredths": [358, 79, 14] },
      "fogMode": "Desaturation",
      "fogStartExtentHundredths": 19, "fogEndExtentHundredths": 74,
      "fogColorHundredths": [0, 0, 0], "skyColorHundredths": [0, 0, 0]
    }
  ]
})";

inline constexpr std::string_view GOOD_SOUNDS = R"({
  "version": 1,
  "events": [
    { "id": "CannonFire", "space": "World", "waves": ["Cannon.wav"],
      "volumeHundredths": 80, "rangeSubunits": 500000, "cooldownTicks": 2, "loops": false }
  ]
})";

/// A Small landscape of one tile, in the schema Tools/LandscapeTool.py --define writes. Its own
/// palette is the Default biome, and its tile names none, so the tree is clean until a test gives
/// the tile a palette of its own (OpenQuestions.md Q18).
inline constexpr std::string_view GOOD_LANDSCAPE = R"({
  "version": 1,
  "sizeClass": "Small",
  "cellsPerSide": 128,
  "samplesPerSide": 513,
  "sampleSpacingWorldUnits": 16,
  "outsideHeight": -26,
  "seed": 1,
  "palette": "Default",
  "tiles": [
    {
      "x": 0, "y": 0, "extent": 512,
      "fractalDimensionHundredths": 170, "amplitude": 90, "desiredHeight": 100,
      "heightShift": 48, "lowlandExponentHundredths": 70, "method": 1, "edgeFalloff": 32
    }
  ],
  "starts": [ { "cellX": 36, "cellY": 92 }, { "cellX": 108, "cellY": 20 } ],
  "deposits": [ { "cellX": 30, "cellY": 86 } ]
})";

/// Writes _text to _path, creating the directories above it.
inline void WriteFixture(const std::filesystem::path& _path, std::string_view _text)
{
  std::filesystem::create_directories(_path.parent_path());
  std::ofstream stream(_path, std::ios::binary | std::ios::trunc);
  stream << _text;
}

/// Removes a scratch directory from a destructor. Every path out of std::filesystem can still
/// throw on allocation even with an error code, and a destructor that throws ends the process, so
/// the catch is not defensive padding: clang-tidy's bugprone-exception-escape refuses the
/// destructor without it, and it is right.
inline void RemoveScratch(const std::filesystem::path& _path) noexcept
{
  try
  {
    std::error_code ignored;
    std::filesystem::remove_all(_path, ignored);
  }
  catch (...)
  {
    // A scratch directory left behind is not a test failure, and a destructor must not throw.
    Microsoft::VisualStudio::CppUnitTestFramework::Logger::WriteMessage("the scratch directory could not be removed");
  }
}

/// Writes the clean tree into _directory. A test then overwrites the one file it wants broken.
inline void WriteGoodTree(const std::filesystem::path& _directory)
{
  WriteFixture(_directory / "Components.json", GOOD_COMPONENTS);
  WriteFixture(_directory / "Structures.json", GOOD_STRUCTURES);
  WriteFixture(_directory / "Research.json", GOOD_RESEARCH);
  WriteFixture(_directory / "Damage.json", GOOD_DAMAGE);
  WriteFixture(_directory / "Biomes.json", GOOD_BIOMES);
  WriteFixture(_directory / "Sounds.json", GOOD_SOUNDS);
  WriteFixture(_directory / "Landscapes" / "Slice.json", GOOD_LANDSCAPE);
}

} // namespace ContentTests
