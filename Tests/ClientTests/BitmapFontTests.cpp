#include "pch.h"

#include "BitmapFont.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// The Spectrum font as Design/Interface.md §3 rules it: monospaced, 16 authored pixels a glyph box,
// a 16-pixel advance, and a line of n characters exactly 16n pixels wide. Every layout in that
// document is built on that arithmetic, so it is worth pinning by number.
namespace ClientTests
{

namespace
{

/// The atlas the game ships, read from the tree rather than from a fixture: what is checked here is
/// that GameData\Textures\SpectrumFont.dds IS the atlas SpeciesCanvas.md §4 describes, and a
/// fixture of the right shape would prove only that the test knew the shape.
[[nodiscard]] std::vector<std::byte> ShippedAtlas()
{
  const std::filesystem::path path =
    std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "GameData" / "Textures" / "SpectrumFont.dds";
  std::ifstream stream(path, std::ios::binary);
  const std::vector<char> raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  // Fail HERE and RETURN, rather than three lines into a test that indexes the header of a file it
  // never read. The first draft asserted and carried on, and the case that pokes the width field
  // walked off the end of an empty vector - a crash instead of a failure, which is a worse thing for
  // a suite to do. The return is not belt and braces either: Assert::Fail throws under the framework
  // and a harness that only counted would carry straight on into the same read.
  if (raw.empty())
  {
    Assert::Fail(L"GameData\\Textures\\SpectrumFont.dds could not be read");
    return {};
  }
  std::vector<std::byte> bytes(raw.size());
  for (std::size_t index = 0; index < raw.size(); ++index)
  {
    bytes[index] = static_cast<std::byte>(raw[index]);
  }
  return bytes;
}

} // namespace

TEST_CLASS(BitmapFontTests)
{
public:
  TEST_METHOD(TheShippedAtlasIsTheOneTheLayoutAssumes)
  {
    Neuron::BitmapFont font;
    std::string error;
    const std::vector<std::byte> bytes = ShippedAtlas();
    Assert::IsTrue(font.Load(bytes, error), std::wstring(error.begin(), error.end()).c_str());
    Assert::IsTrue(font.Loaded());
    Assert::AreEqual(std::uint32_t{256}, font.AtlasWidth(), L"16 columns of 16");
    Assert::AreEqual(std::uint32_t{224}, font.AtlasHeight(), L"by 14 rows of 16");
    Assert::AreEqual(std::size_t{256} * 224 * 4, font.Pixels().size(), L"decoded to RGBA8");
  }

  /// Every cell's position is derived from the atlas's geometry, so an atlas of another size does
  /// not fail to load - it loads and draws the wrong glyph for every letter in the game, which is a
  /// fault somebody has to notice by reading the screen. It is refused instead.
  TEST_METHOD(AnAtlasOfAnotherSizeIsRefusedRatherThanDrawnWrong)
  {
    std::vector<std::byte> bytes = ShippedAtlas();
    if (bytes.size() <= 20)
    {
      Assert::Fail(L"the atlas has a header to break");
      return;
    }
    bytes[16] = static_cast<std::byte>(64); // the DDS header's width, low byte
    Neuron::BitmapFont font;
    std::string error;
    Assert::IsFalse(font.Load(bytes, error), L"a differently sized atlas does not load");
    Assert::IsFalse(font.Loaded());
    Assert::IsFalse(error.empty(), L"and it says why");
  }

  TEST_METHOD(ANonsenseFileIsRefused)
  {
    const std::vector<std::byte> notADds(64, std::byte{0});
    Neuron::BitmapFont font;
    std::string error;
    Assert::IsFalse(font.Load(notADds, error));
  }

  /// §3's arithmetic, stated as the document states it: a line of n characters is exactly 16n.
  TEST_METHOD(ALineOfNCharactersIsExactlySixteenNPixelsWide)
  {
    Assert::AreEqual(0, Neuron::LineWidthPixels(""));
    Assert::AreEqual(16, Neuron::LineWidthPixels("A"));
    Assert::AreEqual(80, Neuron::LineWidthPixels("POWER"));
    Assert::AreEqual(16 * 24, Neuron::LineWidthPixels(std::string(24, 'X')), L"a design name at its limit");
  }

  TEST_METHOD(MeasuringCountsTheWidestLineAndHowManyThereAre)
  {
    const Neuron::UiRect one = Neuron::MeasuredSize("ABC");
    Assert::AreEqual(48, one.width);
    Assert::AreEqual(Neuron::LINE_HEIGHT_PIXELS, one.height);

    const Neuron::UiRect two = Neuron::MeasuredSize("AB\nCDE");
    Assert::AreEqual(48, two.width, L"the widest line");
    Assert::AreEqual(2 * Neuron::LINE_HEIGHT_PIXELS, two.height);

    const Neuron::UiRect empty = Neuron::MeasuredSize("");
    Assert::AreEqual(0, empty.width);
    Assert::AreEqual(Neuron::LINE_HEIGHT_PIXELS, empty.height, L"an empty string is still one line tall");
  }

  TEST_METHOD(TextLaysOutOneQuadAGlyphAtTheAdvance)
  {
    std::vector<Neuron::GlyphQuad> quads;
    Neuron::LayoutText("A B", 100, 200, quads);
    Assert::AreEqual(std::size_t{3}, quads.size(), L"the space is a cell like any other");
    Assert::AreEqual(100, quads[0].screen.x);
    Assert::AreEqual(116, quads[1].screen.x);
    Assert::AreEqual(132, quads[2].screen.x);
    Assert::AreEqual(200, quads[0].screen.y);
    Assert::AreEqual(Neuron::GLYPH_SIZE_PIXELS, quads[0].screen.width);
  }

  /// The atlas's first cell is ASCII 32, so 'A' at 65 is cell 33: row 2, column 1.
  TEST_METHOD(AGlyphTakesTheAtlasCellItsCodeNames)
  {
    std::vector<Neuron::GlyphQuad> quads;
    Neuron::LayoutText(" A", 0, 0, quads);
    Assert::AreEqual(0, quads[0].atlasX, L"the space is the first cell");
    Assert::AreEqual(0, quads[0].atlasY);
    Assert::AreEqual(16, quads[1].atlasX, L"'A' is cell 33");
    Assert::AreEqual(32, quads[1].atlasY);
  }

  /// A byte the atlas has no cell for is drawn as nothing but STILL costs its column, so one stray
  /// character does not slide every character after it one place left.
  TEST_METHOD(AnUndrawableByteMakesNoQuadButStillAdvances)
  {
    std::vector<Neuron::GlyphQuad> quads;
    Neuron::LayoutText("A\tB", 0, 0, quads);
    Assert::AreEqual(std::size_t{2}, quads.size());
    Assert::AreEqual(0, quads[0].screen.x);
    Assert::AreEqual(32, quads[1].screen.x, L"the tab cost its column and drew nothing");
  }

  TEST_METHOD(ANewlineReturnsToTheStartAndDropsALine)
  {
    std::vector<Neuron::GlyphQuad> quads;
    Neuron::LayoutText("AB\nC", 10, 20, quads);
    Assert::AreEqual(std::size_t{3}, quads.size());
    Assert::AreEqual(10, quads[2].screen.x);
    Assert::AreEqual(20 + Neuron::LINE_HEIGHT_PIXELS, quads[2].screen.y);
  }

  /// The ruling of §3 against SpeciesCanvas.md §4, pinned so that a return to Species's 0.6 is a
  /// failing test rather than a resampled screen: 0.6 of 16 is 9.6, and a glyph on a fraction of a
  /// pixel is the one thing the authored resolution exists to prevent.
  TEST_METHOD(TheAdvanceIsTheAtlasCellAndNotSpeciesSixTenths)
  {
    Assert::AreEqual(16, Neuron::GLYPH_ADVANCE_PIXELS);
    Assert::AreEqual(Neuron::GLYPH_SIZE_PIXELS, Neuron::GLYPH_ADVANCE_PIXELS, L"monospaced at the cell");
    Assert::AreEqual(20, Neuron::LINE_HEIGHT_PIXELS);
    Assert::AreEqual(std::uint32_t{224}, Neuron::GLYPH_COUNT, L"16 by 14 cells");
  }
};

} // namespace ClientTests
