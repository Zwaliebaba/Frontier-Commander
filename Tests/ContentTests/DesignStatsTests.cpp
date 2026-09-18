#include "pch.h"

#include "BrokenFixtures.h"
#include "ContentLoader.h"
#include "DesignStats.h"
#include "FixedPoint.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace ContentTests
{

namespace
{

/// A scratch directory per instance, so that two tests in one process never share one.
int g_counter = 0;

/// The shipped tables, as m1-vertical-slice/C2 authored them. The two worked examples are
/// re-derived from these below, so that the numbers a reviewer diffs against GameDesign.md §6 are
/// the numbers the game plays by; the fixture tree stays for the edge cases the shipped set has no
/// row for, like a sensor module or a chassis with more mounts than modules.
struct ShippedTree
{
  Frontier::ContentTree tree;

  ShippedTree()
  {
    const std::filesystem::path root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "GameData";
    std::vector<Frontier::ContentDiagnostic> diagnostics;
    if (!Frontier::LoadContent(root, tree, diagnostics))
    {
      const std::string first = diagnostics.empty() ? std::string("no diagnostic") : diagnostics.front().message;
      Assert::Fail((L"the shipped tables do not load: " + std::wstring(first.begin(), first.end())).c_str());
    }
  }
};

/// The fixture tree's component rows are GameDesign.md §6's own numbers for the two worked
/// examples, so the derivation is checked against the design rather than against itself.
struct LoadedTree
{
  std::filesystem::path path;
  Frontier::ContentTree tree;

  LoadedTree()
  {
    path = std::filesystem::temp_directory_path() / "FrontierDesignStatsTests" / std::to_string(::GetCurrentProcessId()) /
           std::to_string(++g_counter);
    std::filesystem::create_directories(path);
    WriteGoodTree(path);
    std::vector<Frontier::ContentDiagnostic> diagnostics;
    Assert::IsTrue(Frontier::LoadContent(path, tree, diagnostics), L"the fixture tree loads");
  }

  ~LoadedTree()
  {
    RemoveScratch(path);
  }
};

} // namespace

TEST_CLASS(DesignStatsTests)
{
public:
  TEST_METHOD(ALightOnWheelsWithAMachineGunIsTheDesignsFirstWorkedExample)
  {
    const LoadedTree loaded;
    const Frontier::DeviceDesign design{"Scout", "Scout", "LightI", "Wheels", {"MachineGun"}};
    Frontier::DesignStats stats{};
    Assert::IsTrue(Frontier::DeriveDesignStats(loaded.tree, design, Frontier::ClassUpgrades{}, stats) == Frontier::DesignFault::None);
    // GameDesign.md §6: 104 world units a second for 130 power in 13 seconds.
    Assert::AreEqual(104, Frontier::WorldUnitsPerSecond(stats.speedSubunitsPerTick));
    Assert::AreEqual(13000, stats.costHundredths);
    Assert::AreEqual(std::uint32_t{13} * Neuron::TICKS_PER_SECOND, stats.buildTimeTicks);
    Assert::AreEqual(100, stats.hitPoints);
    Assert::AreEqual(5, stats.kineticArmor);
  }

  TEST_METHOD(AHeavyOnTracksWithACannonIsTheDesignsSecondWorkedExample)
  {
    const LoadedTree loaded;
    const Frontier::DeviceDesign design{"Line", "Line", "HeavyI", "Tracks", {"Cannon"}};
    Frontier::DesignStats stats{};
    Assert::IsTrue(Frontier::DeriveDesignStats(loaded.tree, design, Frontier::ClassUpgrades{}, stats) == Frontier::DesignFault::None);
    // GameDesign.md §6: 29 world units a second for 490 power in 49 seconds.
    Assert::AreEqual(29, Frontier::WorldUnitsPerSecond(stats.speedSubunitsPerTick), L"40 x 0.8 x 0.9 rounds to 29, not down to 28");
    Assert::AreEqual(49000, stats.costHundredths);
    Assert::AreEqual(std::uint32_t{49} * Neuron::TICKS_PER_SECOND, stats.buildTimeTicks);
    Assert::AreEqual(750, stats.hitPoints, L"500 hit points at the tracks' factor of 1.5");
  }

  TEST_METHOD(TheShippedTablesReproduceBothWorkedExamplesAndTheWholeResearchTree)
  {
    // The point of C2: the slice's numbers are files a reviewer diffs against GameDesign.md, and
    // the derivation run over them gives the design's own two answers.
    const ShippedTree shipped;
    Frontier::DesignStats scout{};
    Assert::IsTrue(Frontier::DeriveDesignStats(shipped.tree, {"Scout", "Scout", "LightI", "Wheels", {"MachineGun"}},
                                               Frontier::ClassUpgrades{}, scout) == Frontier::DesignFault::None);
    Assert::AreEqual(104, Frontier::WorldUnitsPerSecond(scout.speedSubunitsPerTick), L"§6: 104 world units a second");
    Assert::AreEqual(13000, scout.costHundredths, L"§6: 130 power");
    Assert::AreEqual(std::uint32_t{13} * Neuron::TICKS_PER_SECOND, scout.buildTimeTicks, L"§6: 13 seconds");

    Frontier::DesignStats line{};
    Assert::IsTrue(Frontier::DeriveDesignStats(shipped.tree, {"Line", "Line", "HeavyI", "Tracks", {"Cannon"}}, Frontier::ClassUpgrades{},
                                               line) == Frontier::DesignFault::None);
    Assert::AreEqual(29, Frontier::WorldUnitsPerSecond(line.speedSubunitsPerTick), L"§6: 29 world units a second");
    Assert::AreEqual(49000, line.costHundredths, L"§6: 490 power");
    Assert::AreEqual(std::uint32_t{49} * Neuron::TICKS_PER_SECOND, line.buildTimeTicks, L"§6: 49 seconds");
    Assert::AreEqual(750, line.hitPoints, L"500 hit points at the tracks' factor of 1.5");

    // The tree reaches every M1 component and structure once (C2): what no row unlocks is
    // available from the first tick, and what is unlocked is unlocked by exactly one item.
    Assert::AreEqual(std::size_t{30}, shipped.tree.research.size(), L"thirty items");
    std::vector<std::string> unlocked;
    for (const Frontier::ResearchItemDesc& item : shipped.tree.research)
    {
      if (item.effect == Frontier::ResearchEffect::Unlock)
      {
        unlocked.push_back(item.unlocks);
      }
    }
    Assert::AreEqual(std::size_t{10}, unlocked.size(), L"ten unlocks");
    std::sort(unlocked.begin(), unlocked.end());
    Assert::IsTrue(std::adjacent_find(unlocked.begin(), unlocked.end()) == unlocked.end(), L"no row is unlocked twice");
    for (const std::string& identifier : unlocked)
    {
      const bool exists = shipped.tree.FindChassis(identifier) != nullptr || shipped.tree.FindDrive(identifier) != nullptr ||
                          shipped.tree.FindModule(identifier) != nullptr || shipped.tree.FindStructure(identifier) != nullptr ||
                          shipped.tree.FindStructureModule(identifier) != nullptr;
      Assert::IsTrue(exists, (L"nothing named " + std::wstring(identifier.begin(), identifier.end())).c_str());
    }
  }

  TEST_METHOD(AClassUpgradeRaisesArmourAndHitPointsAndNothingElse)
  {
    const LoadedTree loaded;
    const Frontier::DeviceDesign design{"Scout", "Scout", "LightI", "Wheels", {"MachineGun"}};
    Frontier::ClassUpgrades upgrades{};
    upgrades.chassisArmorPercent[static_cast<std::size_t>(Frontier::ChassisClass::Light)] = 20;
    upgrades.chassisHitPointPercent[static_cast<std::size_t>(Frontier::ChassisClass::Light)] = 10;
    Frontier::DesignStats stats{};
    Assert::IsTrue(Frontier::DeriveDesignStats(loaded.tree, design, upgrades, stats) == Frontier::DesignFault::None);
    Assert::AreEqual(6, stats.kineticArmor, L"5 at 1.2");
    Assert::AreEqual(110, stats.hitPoints, L"100 at 1.1");
    Assert::AreEqual(13000, stats.costHundredths, L"an upgrade is free");
    Assert::AreEqual(104, Frontier::WorldUnitsPerSecond(stats.speedSubunitsPerTick));
  }

  TEST_METHOD(TheModulesWeightSlowsTheDeviceAndTheSensorRaisesItsSight)
  {
    const LoadedTree loaded;
    const Frontier::DeviceDesign light{"A", "A", "LightI", "Wheels", {"MachineGun"}};
    const Frontier::DeviceDesign heavy{"B", "B", "LightI", "Wheels", {"Cannon"}};
    Frontier::DesignStats fast{};
    Frontier::DesignStats slow{};
    Assert::IsTrue(Frontier::DeriveDesignStats(loaded.tree, light, Frontier::ClassUpgrades{}, fast) == Frontier::DesignFault::None);
    Assert::IsTrue(Frontier::DeriveDesignStats(loaded.tree, heavy, Frontier::ClassUpgrades{}, slow) == Frontier::DesignFault::None);
    Assert::IsTrue(slow.speedSubunitsPerTick < fast.speedSubunitsPerTick, L"the cannon's ten per cent of weight is felt");
    Assert::AreEqual(fast.sightSubunits, slow.sightSubunits, L"neither module is a sensor");
  }

  TEST_METHOD(ADesignWithNoPartsOrTooManyIsRefusedWithItsReason)
  {
    const LoadedTree loaded;
    Frontier::DesignStats stats{};
    const Frontier::ClassUpgrades none{};
    Assert::IsTrue(Frontier::DeriveDesignStats(loaded.tree, {"a", "a", "Nothing", "Wheels", {"MachineGun"}}, none, stats) ==
                   Frontier::DesignFault::UnknownChassis);
    Assert::IsTrue(Frontier::DeriveDesignStats(loaded.tree, {"a", "a", "LightI", "Nothing", {"MachineGun"}}, none, stats) ==
                   Frontier::DesignFault::UnknownDrive);
    Assert::IsTrue(Frontier::DeriveDesignStats(loaded.tree, {"a", "a", "LightI", "Wheels", {"Nothing"}}, none, stats) ==
                   Frontier::DesignFault::UnknownModule);
    Assert::IsTrue(Frontier::DeriveDesignStats(loaded.tree, {"a", "a", "LightI", "Wheels", {}}, none, stats) ==
                   Frontier::DesignFault::NoModules);
    Assert::IsTrue(Frontier::DeriveDesignStats(loaded.tree, {"a", "a", "LightI", "Wheels", {"MachineGun", "Cannon"}}, none, stats) ==
                     Frontier::DesignFault::TooManyModules,
                   L"the light chassis has one mount");
  }

  TEST_METHOD(TheBuildTimeIsTheCostOverTenPowerASecondAndNeverZero)
  {
    Assert::AreEqual(std::uint32_t{260}, Frontier::BuildTimeTicksFor(13000));
    Assert::AreEqual(std::uint32_t{980}, Frontier::BuildTimeTicksFor(49000));
    Assert::AreEqual(std::uint32_t{1}, Frontier::BuildTimeTicksFor(1), L"nothing is built in no time at all");
  }

  TEST_METHOD(TheSpeedConversionRoundsHalfUpAndSurvivesTheRoundTrip)
  {
    // 104 world units a second is 1,331.2 subunits a tick, which stores as 1,331 and reads back as 104.
    Assert::AreEqual(104, Frontier::WorldUnitsPerSecond(1331));
    Assert::AreEqual(29, Frontier::WorldUnitsPerSecond(369));
    Assert::AreEqual(0, Frontier::WorldUnitsPerSecond(0));
    Assert::AreEqual(80, Frontier::WorldUnitsPerSecond(1024), L"the light chassis's own base speed");
  }

  TEST_METHOD(TheDamageFormulaScalesByTheMatrixAndFloorsAtAThird)
  {
    const LoadedTree loaded;
    const Frontier::DamageTable& table = loaded.tree.damage;
    // Anti-light against wheels: 8 damage at 120 per cent is 9, less 5 armour at 100 per cent is 4;
    // the floor of a third of 9 is 3, so 4 stands.
    Assert::AreEqual(
      4, Frontier::DamageDealt(table, Frontier::WeaponClass::AntiLight, Frontier::TargetColumnOf(Frontier::DriveClass::Wheels), 8, 5));
    // The same weapon against a bunker: 8 at 20 per cent is 1, and armour would take it below the
    // floor, so a third of 1 — zero — is what is dealt.
    Assert::AreEqual(
      0, Frontier::DamageDealt(table, Frontier::WeaponClass::AntiLight, Frontier::TargetColumnOf(Frontier::StrengthClass::Bunker), 8, 25));
    // Artillery ignores armour entirely: 80 at 100 per cent against tracks, whatever the armour.
    Assert::AreEqual(
      80, Frontier::DamageDealt(table, Frontier::WeaponClass::Artillery, Frontier::TargetColumnOf(Frontier::DriveClass::Tracks), 80, 25));
  }
};

} // namespace ContentTests
