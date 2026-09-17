#include "pch.h"

#include "BrokenFixtures.h"
#include "ContentLoader.h"
#include "ContentValidator.h"

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

struct ScratchTree
{
  std::filesystem::path path;

  ScratchTree()
  {
    path = std::filesystem::temp_directory_path() / "FrontierValidatorTests" / std::to_string(::GetCurrentProcessId()) /
           std::to_string(++g_counter);
    std::filesystem::create_directories(path);
    WriteGoodTree(path);
  }

  ~ScratchTree()
  {
    RemoveScratch(path);
  }
};

/// Loads the scratch tree, validates it without touching the disk for assets, and returns the
/// findings. Loading itself must succeed: these are the faults loading cannot see.
[[nodiscard]] std::vector<Frontier::ContentDiagnostic> FindingsOf(const ScratchTree& _tree)
{
  Frontier::ContentTree loaded;
  std::vector<Frontier::ContentDiagnostic> diagnostics;
  Assert::IsTrue(Frontier::LoadContent(_tree.path, loaded, diagnostics), L"the tree must load before it can be validated");
  // The result is the emptiness of the findings, which the caller checks for itself.
  static_cast<void>(Frontier::ValidateContent(loaded, std::filesystem::path(), diagnostics));
  return diagnostics;
}

[[nodiscard]] bool Mentions(const std::vector<Frontier::ContentDiagnostic>& _diagnostics, const char* _text)
{
  for (const Frontier::ContentDiagnostic& diagnostic : _diagnostics)
  {
    if (diagnostic.message.find(_text) != std::string::npos)
    {
      return true;
    }
  }
  return false;
}

} // namespace

TEST_CLASS(ContentValidatorTests)
{
public:
  TEST_METHOD(TheGoodTreeHasNoFindings)
  {
    const ScratchTree tree;
    const std::vector<Frontier::ContentDiagnostic> findings = FindingsOf(tree);
    for (const Frontier::ContentDiagnostic& finding : findings)
    {
      Logger::WriteMessage(finding.ToString().c_str());
    }
    Assert::AreEqual(std::size_t{0}, findings.size());
  }

  TEST_METHOD(APrerequisiteNothingDefinesIsFoundAtItsRowsLine)
  {
    const ScratchTree tree;
    WriteFixture(tree.path / "Research.json", "{\n"
                                              "  \"version\": 1,\n"
                                              "  \"items\": [\n"
                                              "    {\n"
                                              "      \"id\": \"Armour\", \"name\": \"Armour\",\n"
                                              "      \"prerequisites\": [\"Nothing\"], \"costHundredths\": 1, \"timeTicks\": 1,\n"
                                              "      \"effect\": \"ChassisArmor\", \"targetClass\": 0, \"upgradePercent\": 5\n"
                                              "    }\n"
                                              "  ]\n"
                                              "}\n");
    const std::vector<Frontier::ContentDiagnostic> findings = FindingsOf(tree);
    Assert::AreEqual(std::size_t{1}, findings.size());
    Assert::AreEqual(std::string("Research.json"), findings.front().file);
    Assert::AreEqual(4, findings.front().line, L"the row that names the missing prerequisite");
    Assert::IsTrue(Mentions(findings, "'Nothing' is not a research item"));
  }

  TEST_METHOD(ACycleInTheResearchTreeIsFound)
  {
    const ScratchTree tree;
    WriteFixture(tree.path / "Research.json", "{\n"
                                              "  \"version\": 1,\n"
                                              "  \"items\": [\n"
                                              "    { \"id\": \"A\", \"name\": \"A\", \"prerequisites\": [\"B\"],\n"
                                              "      \"costHundredths\": 1, \"timeTicks\": 1,\n"
                                              "      \"effect\": \"Unlock\", \"unlocks\": \"MachineGun\" },\n"
                                              "    { \"id\": \"B\", \"name\": \"B\", \"prerequisites\": [\"A\"],\n"
                                              "      \"costHundredths\": 1, \"timeTicks\": 1,\n"
                                              "      \"effect\": \"Unlock\", \"unlocks\": \"Cannon\" }\n"
                                              "  ]\n"
                                              "}\n");
    const std::vector<Frontier::ContentDiagnostic> findings = FindingsOf(tree);
    Assert::IsTrue(Mentions(findings, "the research tree has a cycle here"));
    Assert::AreEqual(std::string("Research.json"), findings.front().file);
    Assert::IsTrue(findings.front().line > 0, L"a cycle names a line like every other finding");
  }

  TEST_METHOD(AnUnlockNamingNothingUnlockableIsFound)
  {
    const ScratchTree tree;
    WriteFixture(tree.path / "Research.json", "{\n"
                                              "  \"version\": 1,\n"
                                              "  \"items\": [\n"
                                              "    { \"id\": \"A\", \"name\": \"A\", \"prerequisites\": [],\n"
                                              "      \"costHundredths\": 1, \"timeTicks\": 1,\n"
                                              "      \"effect\": \"Unlock\", \"unlocks\": \"Teleporter\" }\n"
                                              "  ]\n"
                                              "}\n");
    const std::vector<Frontier::ContentDiagnostic> findings = FindingsOf(tree);
    Assert::AreEqual(std::size_t{1}, findings.size());
    Assert::IsTrue(Mentions(findings, "'Teleporter'"));
    Assert::AreEqual(4, findings.front().line);
  }

  TEST_METHOD(ADuplicateIdNamesBothLines)
  {
    const ScratchTree tree;
    WriteFixture(tree.path / "Structures.json", "{\n"
                                                "  \"version\": 1,\n"
                                                "  \"structures\": [\n"
                                                "    { \"id\": \"Cannon\", \"name\": \"Not a cannon\", \"role\": \"Tower\",\n"
                                                "      \"strength\": \"Medium\", \"model\": \"m\",\n"
                                                "      \"footprintCellsX\": 1, \"footprintCellsY\": 1, \"hitPoints\": 1,\n"
                                                "      \"kineticArmor\": 0, \"thermalArmor\": 0, \"costHundredths\": 1,\n"
                                                "      \"buildTimeTicks\": 1, \"sightSubunits\": 1, \"moduleSlots\": 0 }\n"
                                                "  ],\n"
                                                "  \"modules\": []\n"
                                                "}\n");
    const std::vector<Frontier::ContentDiagnostic> findings = FindingsOf(tree);
    Assert::AreEqual(std::size_t{1}, findings.size());
    Assert::AreEqual(std::string("Structures.json"), findings.front().file, L"the second use is the one to delete");
    Assert::AreEqual(4, findings.front().line);
    Assert::IsTrue(Mentions(findings, "already used at Components.json("), L"and it points at the first");
  }

  TEST_METHOD(AStructureNamingAModuleNothingDefinesIsFound)
  {
    const ScratchTree tree;
    WriteFixture(tree.path / "Structures.json", "{\n"
                                                "  \"version\": 1,\n"
                                                "  \"structures\": [\n"
                                                "    { \"id\": \"Factory\", \"name\": \"Factory\", \"role\": \"Factory\",\n"
                                                "      \"strength\": \"Medium\", \"model\": \"m\",\n"
                                                "      \"footprintCellsX\": 3, \"footprintCellsY\": 3, \"hitPoints\": 1,\n"
                                                "      \"kineticArmor\": 0, \"thermalArmor\": 0, \"costHundredths\": 1,\n"
                                                "      \"buildTimeTicks\": 1, \"sightSubunits\": 1, \"moduleSlots\": 2,\n"
                                                "      \"modules\": [\"Nothing\"] }\n"
                                                "  ],\n"
                                                "  \"modules\": []\n"
                                                "}\n");
    const std::vector<Frontier::ContentDiagnostic> findings = FindingsOf(tree);
    Assert::AreEqual(std::size_t{1}, findings.size());
    Assert::IsTrue(Mentions(findings, "'Nothing', which is not a structure module"));
  }

  TEST_METHOD(AWeaponWhoseLongRangeIsUnderItsShortRangeIsFound)
  {
    const ScratchTree tree;
    Frontier::ContentTree loaded;
    std::vector<Frontier::ContentDiagnostic> diagnostics;
    Assert::IsTrue(Frontier::LoadContent(tree.path, loaded, diagnostics));
    for (Frontier::ModuleDesc& module : loaded.components.modules)
    {
      if (module.id == "Cannon")
      {
        module.longRangeSubunits = 1;
      }
    }
    Assert::IsFalse(Frontier::ValidateContent(loaded, std::filesystem::path(), diagnostics));
    Assert::IsTrue(Mentions(diagnostics, "its long range is under its short range"));
  }

  TEST_METHOD(AModelNoFileDefinesIsFoundWhenTheDirectoryIsGiven)
  {
    const ScratchTree tree;
    Frontier::ContentTree loaded;
    std::vector<Frontier::ContentDiagnostic> diagnostics;
    Assert::IsTrue(Frontier::LoadContent(tree.path, loaded, diagnostics));
    // Nothing under Models defines a model at all, so every row that names one is a finding.
    Assert::IsFalse(Frontier::ValidateContent(loaded, tree.path, diagnostics));
    Assert::IsTrue(Mentions(diagnostics, "which no file under Models defines"));
    Assert::IsTrue(Mentions(diagnostics, "the texture 'LandscapeDefault.dds' is not under Textures"));
    Assert::IsTrue(Mentions(diagnostics, "the wave 'Cannon.wav' is not under Sounds"));
  }

  TEST_METHOD(TheValidatorReportsEveryFaultRatherThanTheFirst)
  {
    const ScratchTree tree;
    WriteFixture(tree.path / "Research.json", "{\n"
                                              "  \"version\": 1,\n"
                                              "  \"items\": [\n"
                                              "    { \"id\": \"A\", \"name\": \"A\", \"prerequisites\": [\"X\"],\n"
                                              "      \"costHundredths\": 1, \"timeTicks\": 1,\n"
                                              "      \"effect\": \"Unlock\", \"unlocks\": \"Y\" },\n"
                                              "    { \"id\": \"B\", \"name\": \"B\", \"prerequisites\": [\"Z\"],\n"
                                              "      \"costHundredths\": 1, \"timeTicks\": 1,\n"
                                              "      \"effect\": \"Unlock\", \"unlocks\": \"W\" }\n"
                                              "  ]\n"
                                              "}\n");
    const std::vector<Frontier::ContentDiagnostic> findings = FindingsOf(tree);
    Assert::AreEqual(std::size_t{4}, findings.size(), L"two missing prerequisites and two unknown unlocks");
  }
};

} // namespace ContentTests
