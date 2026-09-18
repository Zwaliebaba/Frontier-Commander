#include "pch.h"

#include "BrokenFixtures.h"
#include "ContentLoader.h"

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

/// A scratch content directory of this test's own. Nothing here touches Content\ in place: a test
/// that edited the shipped tree would pass once and fail for the next reader.
struct ScratchTree
{
  std::filesystem::path path;

  ScratchTree()
  {
    path = std::filesystem::temp_directory_path() / "FrontierContentTests" / std::to_string(::GetCurrentProcessId()) /
           std::to_string(++g_counter);
    std::filesystem::create_directories(path);
    WriteGoodTree(path);
  }

  ~ScratchTree()
  {
    RemoveScratch(path);
  }
};

/// Loads the scratch tree and returns the one diagnostic, having asserted that loading failed.
[[nodiscard]] Frontier::ContentDiagnostic RefusedBy(const ScratchTree& _tree)
{
  Frontier::ContentTree loaded;
  std::vector<Frontier::ContentDiagnostic> diagnostics;
  Assert::IsFalse(Frontier::LoadContent(_tree.path, loaded, diagnostics), L"this tree was expected to be refused");
  Assert::AreEqual(std::size_t{1}, diagnostics.size(), L"loading stops at the first file that fails");
  return diagnostics.front();
}

} // namespace

TEST_CLASS(ContentLoaderTests)
{
public:
  TEST_METHOD(TheGoodTreeLoadsWithEveryRow)
  {
    const ScratchTree tree;
    Frontier::ContentTree loaded;
    std::vector<Frontier::ContentDiagnostic> diagnostics;
    Assert::IsTrue(Frontier::LoadContent(tree.path, loaded, diagnostics), L"the fixture tree loads");
    Assert::AreEqual(std::size_t{0}, diagnostics.size());
    Assert::AreEqual(std::size_t{2}, loaded.components.chassis.size());
    Assert::AreEqual(std::size_t{2}, loaded.components.drives.size());
    Assert::AreEqual(std::size_t{3}, loaded.components.modules.size());
    Assert::AreEqual(std::size_t{2}, loaded.structures.structures.size());
    Assert::AreEqual(std::size_t{2}, loaded.research.size());
    Assert::AreEqual(std::size_t{1}, loaded.biomes.size());
    Assert::AreEqual(std::size_t{1}, loaded.sounds.size());
    Assert::IsNotNull(loaded.FindChassis("LightI"));
    Assert::IsNotNull(loaded.FindModule("Builder"));
    Assert::IsNull(loaded.FindChassis("Nothing"));
  }

  TEST_METHOD(EveryRowCarriesTheFileAndLineItWasReadFrom)
  {
    const ScratchTree tree;
    Frontier::ContentTree loaded;
    std::vector<Frontier::ContentDiagnostic> diagnostics;
    Assert::IsTrue(Frontier::LoadContent(tree.path, loaded, diagnostics));
    const Frontier::RowOrigin* origin = loaded.FindOrigin("Cannon");
    Assert::IsNotNull(origin);
    Assert::AreEqual(std::string("Components.json"), origin->file);
    Assert::IsTrue(origin->line > 1, L"a row's line is where its object opened");
    Assert::AreEqual(loaded.RowCount(), loaded.origins.size(), L"every row has an origin");
  }

  TEST_METHOD(ADirectoryWithNoComponentsIsNotAContentDirectory)
  {
    const ScratchTree tree;
    std::filesystem::remove(tree.path / "Components.json");
    const Frontier::ContentDiagnostic diagnostic = RefusedBy(tree);
    Assert::AreEqual(0, diagnostic.line);
    Assert::IsTrue(diagnostic.message.find("not a content directory") != std::string::npos, L"it says what is missing");
  }

  TEST_METHOD(AMalformedFileIsRefusedAtItsLineAndColumn)
  {
    const ScratchTree tree;
    // The comma on line 4 has no member after it.
    WriteFixture(tree.path / "Damage.json", "{\n"
                                            "  \"version\": 1,\n"
                                            "  \"weapons\": [],\n"
                                            "  ,\n"
                                            "}\n");
    const Frontier::ContentDiagnostic diagnostic = RefusedBy(tree);
    Assert::AreEqual(std::string("Damage.json"), diagnostic.file);
    Assert::AreEqual(4, diagnostic.line, L"the line the parser stopped on");
    Assert::AreEqual(3, diagnostic.column);
  }

  TEST_METHOD(ANumberOutsideItsRangeIsRefusedAtItsOwnLine)
  {
    const ScratchTree tree;
    WriteFixture(tree.path / "Components.json", "{\n"
                                                "  \"version\": 1,\n"
                                                "  \"chassis\": [\n"
                                                "    {\n"
                                                "      \"id\": \"LightI\", \"name\": \"Light I\", \"class\": \"Light\", \"model\": \"m\",\n"
                                                "      \"hitPoints\": 100, \"kineticArmor\": 5, \"thermalArmor\": 5,\n"
                                                "      \"baseSpeedSubunitsPerTick\": 1024, \"sightSubunits\": 327680,\n"
                                                "      \"costHundredths\": 6000, \"mounts\": 99\n"
                                                "    }\n"
                                                "  ],\n"
                                                "  \"drives\": [],\n"
                                                "  \"modules\": []\n"
                                                "}\n");
    const Frontier::ContentDiagnostic diagnostic = RefusedBy(tree);
    Assert::AreEqual(std::string("Components.json"), diagnostic.file);
    Assert::AreEqual(8, diagnostic.line, L"the line the offending number is on, not the row's");
    Assert::IsTrue(diagnostic.message.find("mounts") != std::string::npos);
    Assert::IsTrue(diagnostic.message.find("between 1 and 8") != std::string::npos, L"it names the range it wanted");
  }

  TEST_METHOD(AMissingMemberIsRefusedAtTheRowsLine)
  {
    const ScratchTree tree;
    WriteFixture(tree.path / "Components.json", "{\n"
                                                "  \"version\": 1,\n"
                                                "  \"chassis\": [\n"
                                                "    {\n"
                                                "      \"id\": \"LightI\", \"name\": \"Light I\", \"class\": \"Light\", \"model\": \"m\"\n"
                                                "    }\n"
                                                "  ],\n"
                                                "  \"drives\": [],\n"
                                                "  \"modules\": []\n"
                                                "}\n");
    const Frontier::ContentDiagnostic diagnostic = RefusedBy(tree);
    Assert::AreEqual(4, diagnostic.line, L"the row's own opening brace");
    Assert::IsTrue(diagnostic.message.find("hitPoints") != std::string::npos);
  }

  TEST_METHOD(AnUnknownEnumerationNameListsWhatWasAllowed)
  {
    const ScratchTree tree;
    WriteFixture(tree.path / "Components.json", "{\n"
                                                "  \"version\": 1,\n"
                                                "  \"chassis\": [\n"
                                                "    {\n"
                                                "      \"id\": \"X\", \"name\": \"X\", \"class\": \"Enormous\", \"model\": \"m\",\n"
                                                "      \"hitPoints\": 100, \"kineticArmor\": 5, \"thermalArmor\": 5,\n"
                                                "      \"baseSpeedSubunitsPerTick\": 1024, \"sightSubunits\": 1,\n"
                                                "      \"costHundredths\": 6000, \"mounts\": 1\n"
                                                "    }\n"
                                                "  ],\n"
                                                "  \"drives\": [],\n"
                                                "  \"modules\": []\n"
                                                "}\n");
    const Frontier::ContentDiagnostic diagnostic = RefusedBy(tree);
    Assert::AreEqual(5, diagnostic.line);
    Assert::IsTrue(diagnostic.message.find("Light, Medium, Heavy") != std::string::npos, L"it lists the classes");
  }

  TEST_METHOD(AFileFromAnotherVersionIsRefusedByName)
  {
    const ScratchTree tree;
    WriteFixture(tree.path / "Research.json", "{\n"
                                              "  \"version\": 2,\n"
                                              "  \"items\": []\n"
                                              "}\n");
    const Frontier::ContentDiagnostic diagnostic = RefusedBy(tree);
    Assert::AreEqual(std::string("Research.json"), diagnostic.file);
    Assert::IsTrue(diagnostic.message.find("version 1") != std::string::npos);
  }

  TEST_METHOD(ADamageMatrixMissingAWeaponClassIsRefused)
  {
    const ScratchTree tree;
    WriteFixture(tree.path / "Damage.json", "{\n"
                                            "  \"version\": 1,\n"
                                            "  \"weapons\": [\n"
                                            "    { \"class\": \"AntiLight\", \"armorFactorPercent\": 100, \"armorKind\": \"Kinetic\",\n"
                                            "      \"modifierPercent\": [1, 1, 1, 1, 1, 1, 1, 1, 1, 1] }\n"
                                            "  ]\n"
                                            "}\n");
    const Frontier::ContentDiagnostic diagnostic = RefusedBy(tree);
    Assert::IsTrue(diagnostic.message.find("five in all") != std::string::npos);
  }

  TEST_METHOD(TheModelScaleDefaultsToNativeAndIsReadWhereItIsGiven)
  {
    const ScratchTree tree;
    Frontier::ContentTree loaded;
    std::vector<Frontier::ContentDiagnostic> diagnostics;
    Assert::IsTrue(Frontier::LoadContent(tree.path, loaded, diagnostics));
    // No fixture row carries the member, so every one of them is at native scale.
    Assert::AreEqual(100, loaded.FindChassis("LightI")->modelScaleHundredths, L"a row with no scale is native");
    Assert::AreEqual(100, loaded.FindDrive("Wheels")->modelScaleHundredths);
    Assert::AreEqual(100, loaded.FindModule("Cannon")->modelScaleHundredths);
    Assert::AreEqual(100, loaded.FindStructure("Factory")->modelScaleHundredths);
    Assert::AreEqual(100, loaded.FindStructureModule("FactoryModule")->modelScaleHundredths);

    // The Species review found shapes needing 0.45 and 0.6 to fit their footprints.
    WriteFixture(tree.path / "Structures.json", "{\n"
                                                "  \"version\": 1,\n"
                                                "  \"structures\": [\n"
                                                "    { \"id\": \"Mine\", \"name\": \"Mine\", \"role\": \"Extractor\",\n"
                                                "      \"strength\": \"Soft\", \"model\": \"m\", \"modelScaleHundredths\": 45,\n"
                                                "      \"footprintCellsX\": 1, \"footprintCellsY\": 1, \"hitPoints\": 1,\n"
                                                "      \"kineticArmor\": 0, \"thermalArmor\": 0, \"costHundredths\": 1,\n"
                                                "      \"buildTimeTicks\": 1, \"sightSubunits\": 1, \"moduleSlots\": 0 }\n"
                                                "  ],\n"
                                                "  \"modules\": []\n"
                                                "}\n");
    Frontier::ContentTree scaled;
    Assert::IsTrue(Frontier::LoadContent(tree.path, scaled, diagnostics));
    Assert::AreEqual(45, scaled.FindStructure("Mine")->modelScaleHundredths);
  }

  TEST_METHOD(AModelScaleOutsideItsRangeIsRefusedAtItsOwnLine)
  {
    const ScratchTree tree;
    WriteFixture(tree.path / "Components.json", "{\n"
                                                "  \"version\": 1,\n"
                                                "  \"chassis\": [\n"
                                                "    {\n"
                                                "      \"id\": \"LightI\", \"name\": \"Light I\", \"class\": \"Light\", \"model\": \"m\",\n"
                                                "      \"modelScaleHundredths\": 0,\n"
                                                "      \"hitPoints\": 100, \"kineticArmor\": 5, \"thermalArmor\": 5,\n"
                                                "      \"baseSpeedSubunitsPerTick\": 1024, \"sightSubunits\": 327680,\n"
                                                "      \"costHundredths\": 6000, \"mounts\": 1\n"
                                                "    }\n"
                                                "  ],\n"
                                                "  \"drives\": [],\n"
                                                "  \"modules\": []\n"
                                                "}\n");
    const Frontier::ContentDiagnostic diagnostic = RefusedBy(tree);
    Assert::AreEqual(6, diagnostic.line, L"a model drawn at nothing is a table bug, not a hidden model");
    Assert::IsTrue(diagnostic.message.find("modelScaleHundredths") != std::string::npos);
  }

  TEST_METHOD(ADiagnosticPrintsInTheFormTheBuildToolsPrint)
  {
    const Frontier::ContentDiagnostic located{"Components.json", 12, 5, "'mounts' is between 1 and 8"};
    Assert::AreEqual(std::string("Components.json(12,5): 'mounts' is between 1 and 8"), located.ToString());
    const Frontier::ContentDiagnostic whole{"Content", 0, 0, "it holds no Components.json"};
    Assert::AreEqual(std::string("Content: it holds no Components.json"), whole.ToString());
  }
};

} // namespace ContentTests
