#include "pch.h"

#include "BrokenFixtures.h"
#include "ContentLoader.h"
#include "InterfaceDesc.h"

#include <array>
#include <cstdlib>
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
    path = std::filesystem::temp_directory_path() / "FrontierInterfaceTests" / std::to_string(::GetCurrentProcessId()) /
           std::to_string(++g_counter);
    std::filesystem::create_directories(path);
    WriteGoodTree(path);
  }

  ~ScratchTree()
  {
    RemoveScratch(path);
  }
};

[[nodiscard]] Frontier::ContentDiagnostic RefusedBy(const ScratchTree& _tree)
{
  Frontier::ContentTree loaded;
  std::vector<Frontier::ContentDiagnostic> diagnostics;
  Assert::IsFalse(Frontier::LoadContent(_tree.path, loaded, diagnostics), L"this tree was expected to be refused");
  Assert::AreEqual(std::size_t{1}, diagnostics.size(), L"loading stops at the first file that fails");
  return diagnostics.front();
}

/// The SHIPPED GameData\Interface.json, not a fixture. The values below are transcribed from
/// Design/Interface.md §3 and GameDesign.md §11 by hand, so the assertions are the documents and
/// the file is what is checked against them; a colour edited in one and not the other fails here.
struct ShippedTree
{
  Frontier::ContentTree tree;

  ShippedTree()
  {
    const std::filesystem::path root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "GameData";
    std::vector<Frontier::ContentDiagnostic> diagnostics;
    if (!Frontier::LoadContent(root, tree, diagnostics))
    {
      const std::string first = diagnostics.empty() ? std::string("no diagnostic") : diagnostics.front().ToString();
      Assert::Fail((L"the shipped tables do not load: " + std::wstring(first.begin(), first.end())).c_str());
    }
  }
};

/// How far apart two colours read, as the sum over the three channels. Alpha is left out: the
/// minimap and the geometry pass both draw a commander's colour opaque.
[[nodiscard]] int Separation(const Frontier::Rgba8& _left, const Frontier::Rgba8& _right)
{
  return std::abs(static_cast<int>(_left.red) - static_cast<int>(_right.red)) +
         std::abs(static_cast<int>(_left.green) - static_cast<int>(_right.green)) +
         std::abs(static_cast<int>(_left.blue) - static_cast<int>(_right.blue));
}

void AssertColor(const Frontier::Rgba8& _actual, int _red, int _green, int _blue, int _alpha, const wchar_t* _role)
{
  Assert::AreEqual(_red, static_cast<int>(_actual.red), _role);
  Assert::AreEqual(_green, static_cast<int>(_actual.green), _role);
  Assert::AreEqual(_blue, static_cast<int>(_actual.blue), _role);
  Assert::AreEqual(_alpha, static_cast<int>(_actual.alpha), _role);
}

/// A chrome object with every role, built so that a test can drop or rename one line of it.
[[nodiscard]] std::string ChromeDocument(const std::string& _extra, const std::string& _omit)
{
  std::string text = "{\n  \"version\": 1,\n  \"chrome\": {\n";
  for (const Frontier::ChromeRole& role : Frontier::CHROME_ROLES)
  {
    if (_omit == role.name)
    {
      continue;
    }
    text += std::string("    \"") + role.name + "\": [1, 2, 3, 255],\n";
  }
  text += _extra.empty() ? "" : "    " + _extra + ",\n";
  // The last member carries no comma, so the trailing one above is trimmed here rather than tracked.
  text.erase(text.size() - 2, 1);
  text += "  },\n  \"commanders\": [\n";
  for (std::size_t seat = 0; seat < Frontier::COMMANDER_COLOR_COUNT; ++seat)
  {
    text += std::string("    [10, 20, 30, 255]") + (seat + 1 == Frontier::COMMANDER_COLOR_COUNT ? "\n" : ",\n");
  }
  text += "  ]\n}\n";
  return text;
}

} // namespace

TEST_CLASS(InterfaceDescTests)
{
public:
  TEST_METHOD(TheShippedChromeIsTheSeventeenRolesOfTheDocument)
  {
    const ShippedTree shipped;
    const Frontier::ChromePalette& chrome = shipped.tree.ui.chrome;
    AssertColor(chrome.panelFill, 24, 10, 12, 245, L"panelFill");
    AssertColor(chrome.panelBorder, 199, 214, 220, 255, L"panelBorder");
    AssertColor(chrome.panelTitleFrom, 199, 214, 220, 255, L"panelTitleFrom");
    AssertColor(chrome.panelTitleTo, 112, 141, 168, 255, L"panelTitleTo");
    AssertColor(chrome.titleText, 255, 255, 150, 255, L"titleText");
    AssertColor(chrome.bodyText, 222, 226, 230, 255, L"bodyText");
    AssertColor(chrome.dimText, 128, 132, 136, 255, L"dimText");
    AssertColor(chrome.accent, 255, 196, 64, 255, L"accent");
    AssertColor(chrome.warning, 232, 96, 72, 255, L"warning");
    AssertColor(chrome.buttonFill, 107, 37, 39, 255, L"buttonFill");
    AssertColor(chrome.buttonFillHover, 140, 52, 54, 255, L"buttonFillHover");
    AssertColor(chrome.buttonFillDown, 199, 214, 220, 255, L"buttonFillDown");
    AssertColor(chrome.buttonDisabled, 60, 32, 34, 255, L"buttonDisabled");
    AssertColor(chrome.barEmpty, 40, 20, 22, 255, L"barEmpty");
    AssertColor(chrome.barBuild, 120, 180, 220, 255, L"barBuild");
    AssertColor(chrome.barHealth, 96, 200, 108, 255, L"barHealth");
    AssertColor(chrome.barHealthLow, 232, 96, 72, 255, L"barHealthLow");
  }

  TEST_METHOD(TheShippedCommanderColorsAreTheEightInSeatOrder)
  {
    const ShippedTree shipped;
    const std::array<Frontier::Rgba8, Frontier::COMMANDER_COLOR_COUNT>& seats = shipped.tree.ui.commanders;
    AssertColor(seats[0], 100, 255, 100, 255, L"seat 0, Species team 0");
    AssertColor(seats[1], 200, 50, 50, 255, L"seat 1, Species team 1");
    AssertColor(seats[2], 200, 200, 30, 255, L"seat 2, Species team 2");
    AssertColor(seats[3], 120, 180, 255, 255, L"seat 3, Species team 3");
    AssertColor(seats[4], 160, 80, 240, 255, L"seat 4");
    AssertColor(seats[5], 30, 210, 200, 255, L"seat 5");
    AssertColor(seats[6], 250, 90, 160, 255, L"seat 6");
    AssertColor(seats[7], 170, 170, 170, 255, L"seat 7, Species team 7 lifted from 150");
  }

  /// The reason the eight are worth asserting one by one. A commander is one pixel on the minimap
  /// (Interface.md §7), so two of them that read alike are two armies that read alike; and the
  /// selection is drawn in `accent` over whatever colour the object had, so a commander who IS the
  /// accent never looks unselected.
  TEST_METHOD(NoTwoCommanderColorsAreWithinReachOfEachOther)
  {
    const ShippedTree shipped;
    const std::array<Frontier::Rgba8, Frontier::COMMANDER_COLOR_COUNT>& seats = shipped.tree.ui.commanders;
    for (std::size_t left = 0; left < seats.size(); ++left)
    {
      for (std::size_t right = left + 1; right < seats.size(); ++right)
      {
        // 140, because the closest pair of the eight is seat 3 against seat 7 at 145 and BOTH ARE
        // SPECIES'S, taken byte for byte: the bar sits just under what the inherited colours allow,
        // so the three chosen ones cannot be the pair that fails it without someone moving them.
        Assert::IsTrue(Separation(seats[left], seats[right]) >= 140, L"two commanders are told apart at one pixel");
      }
    }
  }

  /// The one near pair, asserted as the exception it is rather than hidden in a threshold low
  /// enough to swallow it.
  TEST_METHOD(EveryCommanderButTheYellowStandsWellClearOfTheSelectionAccent)
  {
    const ShippedTree shipped;
    const Frontier::Rgba8 accent = shipped.tree.ui.chrome.accent;
    const std::array<Frontier::Rgba8, Frontier::COMMANDER_COLOR_COUNT>& seats = shipped.tree.ui.commanders;
    for (std::size_t seat = 0; seat < seats.size(); ++seat)
    {
      if (seat == 2)
      {
        continue;
      }
      Assert::IsTrue(Separation(seats[seat], accent) >= 200, L"a commander colour is not the selection accent");
    }

    // SEAT 2 IS 93 FROM THE ACCENT AND THAT IS KNOWN (GameDesign.md §11). Species's team 2 is kept
    // byte for byte and Interface.md §3's amber is the document's, so neither end of this pair was
    // this table's to move; a selected yellow commander is the weakest reading of §7's selection
    // mark, and the ruling says the accent moves if a running build shows it is too weak. The
    // number is pinned so that the day either colour changes, this test says which way it went.
    Assert::AreEqual(93, Separation(seats[2], accent), L"the known near pair, unchanged");
  }

  TEST_METHOD(AMissingRoleIsRefusedByName)
  {
    const ScratchTree tree;
    WriteFixture(tree.path / "Interface.json", ChromeDocument("", "accent"));
    const Frontier::ContentDiagnostic diagnostic = RefusedBy(tree);
    Assert::AreEqual(std::string("Interface.json"), diagnostic.file);
    Assert::IsTrue(diagnostic.message.find("'accent' is missing") != std::string::npos, L"it names the role");
  }

  /// The half a loop over the roles alone would miss: a misspelled role would otherwise be read as
  /// a member nobody asked for, and the field it was meant to fill would keep its default.
  TEST_METHOD(AnUnknownRoleIsRefusedAtItsOwnLine)
  {
    const ScratchTree tree;
    WriteFixture(tree.path / "Interface.json", ChromeDocument("\"acccent\": [9, 9, 9, 255]", ""));
    const Frontier::ContentDiagnostic diagnostic = RefusedBy(tree);
    Assert::AreEqual(std::string("Interface.json"), diagnostic.file);
    const int firstRoleLine = 4; // "{", "version", "chrome": {  — then one line a role.
    Assert::AreEqual(firstRoleLine + static_cast<int>(Frontier::CHROME_ROLE_COUNT), diagnostic.line,
                     L"the line the unknown role is written on");
    Assert::IsTrue(diagnostic.message.find("'acccent' is not a chrome role") != std::string::npos, L"it names what it found");
  }

  TEST_METHOD(AChannelOutsideAByteIsRefused)
  {
    const ScratchTree tree;
    std::string document = ChromeDocument("", "");
    const std::string role = "\"accent\": [1, 2, 3, 255]";
    const std::size_t at = document.find(role);
    Assert::AreNotEqual(std::string::npos, at, L"the fixture holds the role this test breaks");
    document.replace(at, role.size(), "\"accent\": [1, 2, 256, 255]");
    WriteFixture(tree.path / "Interface.json", document);
    const Frontier::ContentDiagnostic diagnostic = RefusedBy(tree);
    Assert::AreEqual(std::string("Interface.json"), diagnostic.file);
    Assert::IsTrue(diagnostic.message.find("between 0 and 255") != std::string::npos, L"it gives the range");
  }

  TEST_METHOD(ACommanderListThatIsNotOneColorASeatIsRefused)
  {
    const ScratchTree tree;
    WriteFixture(tree.path / "Interface.json", "{\n"
                                               "  \"version\": 1,\n"
                                               "  \"chrome\": {},\n"
                                               "  \"commanders\": []\n"
                                               "}\n");
    const Frontier::ContentDiagnostic diagnostic = RefusedBy(tree);
    Assert::AreEqual(std::string("Interface.json"), diagnostic.file);
    // The empty chrome fails first, which is the loader's rule of stopping at the first fault; the
    // list is checked on its own below, with a chrome that reads.
    Assert::IsTrue(diagnostic.message.find("is missing") != std::string::npos, L"the empty chrome is the first fault");

    std::string document = ChromeDocument("", "");
    const std::string seat = "    [10, 20, 30, 255],\n";
    const std::size_t at = document.find(seat);
    Assert::AreNotEqual(std::string::npos, at, L"the fixture holds a seat to remove");
    document.erase(at, seat.size());
    WriteFixture(tree.path / "Interface.json", document);
    const Frontier::ContentDiagnostic shortList = RefusedBy(tree);
    Assert::IsTrue(shortList.message.find("one colour a seat") != std::string::npos, L"it says what the length is for");
  }
};

} // namespace ContentTests
