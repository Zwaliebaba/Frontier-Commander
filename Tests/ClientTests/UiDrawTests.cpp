#include "pch.h"

#include "UiDraw.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// What a panel looks like (Design/Interface.md §3), as quads. Every rule that document states about
// the chrome is one case here, so that a change to §3 that nobody carried into UiDraw.cpp fails at
// the build rather than on somebody's screen - which for a one-pixel border or a bar drawn over its
// own frame is a fault nobody reports because it looks deliberate.
namespace ClientTests
{

namespace
{

/// A palette whose every role is a different colour, so that "this quad is buttonFillHover" is a
/// comparison rather than a guess. Built from CHROME_ROLES rather than by hand: a role added to
/// Content/InterfaceDesc.h is then covered here without anybody remembering to come back.
[[nodiscard]] Frontier::ChromePalette DistinctPalette()
{
  Frontier::ChromePalette palette{};
  for (std::size_t index = 0; index < Frontier::CHROME_ROLES.size(); ++index)
  {
    palette.*Frontier::CHROME_ROLES[index].member = Frontier::Rgba8{static_cast<std::uint8_t>(index + 1), 0, 0, 255};
  }
  return palette;
}

[[nodiscard]] std::uint32_t Role(const Frontier::ChromePalette& _palette, Frontier::Rgba8 Frontier::ChromePalette::*_member)
{
  return Neuron::PackedColor(_palette.*_member);
}

/// The shipped icon sheet, read from the tree for the reason BitmapFontTests reads the font from it.
[[nodiscard]] std::vector<std::byte> ShippedIcons()
{
  const std::filesystem::path path =
    std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "GameData" / "Textures" / "Icons.dds";
  std::ifstream stream(path, std::ios::binary);
  const std::vector<char> raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  Assert::IsFalse(raw.empty(), L"GameData\\Textures\\Icons.dds could not be read");
  if (raw.empty())
  {
    return {};
  }
  std::vector<std::byte> bytes(raw.size());
  for (std::size_t index = 0; index < raw.size(); ++index)
  {
    bytes[index] = static_cast<std::byte>(raw[index]);
  }
  return bytes;
}

/// How many quads of _quads are filled in _color.
[[nodiscard]] std::size_t CountOf(const std::vector<Neuron::UiQuad>& _quads, std::uint32_t _color)
{
  std::size_t count = 0;
  for (const Neuron::UiQuad& quad : _quads)
  {
    if (quad.colorTop == _color && quad.colorBottom == _color)
    {
      ++count;
    }
  }
  return count;
}

/// The _index'th quad filled in _color, or a default one.
[[nodiscard]] Neuron::UiQuad NthOf(const std::vector<Neuron::UiQuad>& _quads, std::uint32_t _color, std::size_t _index)
{
  std::size_t seen = 0;
  for (const Neuron::UiQuad& quad : _quads)
  {
    if (quad.colorTop == _color && seen++ == _index)
    {
      return quad;
    }
  }
  return Neuron::UiQuad{};
}

[[nodiscard]] Neuron::UiQuad FirstOf(const std::vector<Neuron::UiQuad>& _quads, std::uint32_t _color)
{
  return NthOf(_quads, _color, 0);
}

/// Where the glyph for _character lies in the Spectrum atlas, as Design/Interface.md §3 lays it out.
[[nodiscard]] Neuron::UiRect AtlasCellOf(char _character)
{
  const std::int32_t cell = static_cast<std::int32_t>(static_cast<unsigned char>(_character)) - 32;
  return Neuron::UiRect{(cell % 16) * 16, (cell / 16) * 16, 16, 16};
}

constexpr Neuron::UiRect SELECTION_PANEL{288, 792, 512, 288};

} // namespace

TEST_CLASS(UiDrawTests)
{
public:
  /// The one packing, pinned: Core/RenderView.h's teamColor and a vertex attribute declared
  /// R8G8B8A8_UNORM both read red out of the low byte, and a swap here is eight roles drawn blue.
  TEST_METHOD(AColorIsRgba8WithRedInTheLowByte)
  {
    Assert::AreEqual(std::uint32_t{0xFF0000FF}, Neuron::PackedColor(Frontier::Rgba8{255, 0, 0, 255}), L"opaque red");
    Assert::AreEqual(std::uint32_t{0xFF00FF00}, Neuron::PackedColor(Frontier::Rgba8{0, 255, 0, 255}));
    Assert::AreEqual(std::uint32_t{0x00FFFFFF}, Neuron::PackedColor(Frontier::Rgba8{255, 255, 255, 0}), L"alpha in the high byte");
    // §3's own panelFill, as GameData\Interface.json carries it.
    Assert::AreEqual(std::uint32_t{0xF50C0A18}, Neuron::PackedColor(Frontier::Rgba8{24, 10, 12, 245}));
  }

  /// §3, clause by clause: a fill, a one-pixel border on four edges, the gradient strip, the title.
  TEST_METHOD(APanelIsTheFillTheBorderTheStripAndTheTitleInThatOrder)
  {
    const Frontier::ChromePalette palette = DistinctPalette();
    const Neuron::UiPanel panel(SELECTION_PANEL, "Selection");
    std::vector<Neuron::UiQuad> quads;
    Neuron::IconAtlas icons;
    Neuron::BuildPanelQuads(panel, palette, icons, quads);

    Assert::IsTrue(quads[0].rect == SELECTION_PANEL, L"the fill is the whole panel");
    Assert::AreEqual(Role(palette, &Frontier::ChromePalette::panelFill), quads[0].colorTop);
    Assert::AreEqual(std::size_t{4}, CountOf(quads, Role(palette, &Frontier::ChromePalette::panelBorder)), L"four edges");

    const Neuron::UiQuad strip = FirstOf(quads, Role(palette, &Frontier::ChromePalette::panelTitleFrom));
    Assert::IsTrue(strip.rect == Neuron::TitleStripOf(SELECTION_PANEL));
    Assert::AreEqual(Role(palette, &Frontier::ChromePalette::panelTitleTo), strip.colorBottom, L"top to bottom, not flat");

    // "SELECTION" is nine glyphs in titleText at x + 8, centred in the 20-pixel strip.
    Assert::AreEqual(std::size_t{9}, CountOf(quads, Role(palette, &Frontier::ChromePalette::titleText)), L"uppercased and drawn");
    const Neuron::UiQuad firstGlyph = FirstOf(quads, Role(palette, &Frontier::ChromePalette::titleText));
    Assert::IsTrue(firstGlyph.kind == Neuron::UiQuadKind::Glyph);
    Assert::AreEqual(SELECTION_PANEL.x + Neuron::PANEL_TITLE_TEXT_X_PIXELS, firstGlyph.rect.x);
    Assert::AreEqual(strip.rect.y + 2, firstGlyph.rect.y, L"16 in 20, centred");
    // 'S' is code 83, cell 51: row 3, column 3 of the atlas.
    Assert::IsTrue(firstGlyph.atlas == AtlasCellOf('S'), L"the atlas cell 'S' names");
    Assert::IsTrue(firstGlyph.atlas == Neuron::UiRect{48, 48, 16, 16}, L"which is row 3, column 3");
    // AND THE SECOND GLYPH, WHICH IS WHERE THE UPPERCASING SHOWS. "Selection" already starts with a
    // capital, so pinning the first letter proves nothing about §3's "uppercased" - mutation testing
    // found exactly that: with Uppercased() removed the case above still passed.
    const Neuron::UiQuad secondGlyph = NthOf(quads, Role(palette, &Frontier::ChromePalette::titleText), 1);
    Assert::IsTrue(secondGlyph.atlas == AtlasCellOf('E'), L"'e' is drawn as 'E'");
    Assert::IsFalse(secondGlyph.atlas == AtlasCellOf('e'));
  }

  TEST_METHOD(AnInvisibleOrEmptyPanelDrawsNothing)
  {
    const Frontier::ChromePalette palette = DistinctPalette();
    Neuron::IconAtlas icons;
    std::vector<Neuron::UiQuad> quads;
    Neuron::UiPanel hidden(SELECTION_PANEL, "Selection");
    hidden.SetVisible(false);
    Neuron::BuildPanelQuads(hidden, palette, icons, quads);
    Assert::IsTrue(quads.empty());
    const Neuron::UiPanel nothing;
    Neuron::BuildPanelQuads(nothing, palette, icons, quads);
    Assert::IsTrue(quads.empty());
  }

  /// A border is four rectangles and they MUST NOT OVERLAP: panelBorder is drawn over panelFill at
  /// (24, 10, 12, 245), and a corner blended twice is a different colour from the three edges it
  /// touches - a defect that looks like a deliberate bevel and so gets reported by nobody.
  TEST_METHOD(ABorderIsFourRectanglesThatShareNoPixel)
  {
    std::vector<Neuron::UiQuad> quads;
    Neuron::AppendBorder(Neuron::UiRect{10, 20, 100, 50}, 1, 0xFFFFFFFF, quads);
    Assert::AreEqual(std::size_t{4}, quads.size());
    std::size_t covered = 0;
    for (std::int32_t y = 20; y < 70; ++y)
    {
      for (std::int32_t x = 10; x < 110; ++x)
      {
        std::size_t hits = 0;
        for (const Neuron::UiQuad& quad : quads)
        {
          hits += quad.rect.Contains(x, y) ? 1 : 0;
        }
        Assert::IsTrue(hits <= 1, L"no pixel is painted twice");
        covered += hits;
      }
    }
    Assert::AreEqual(std::size_t{2 * 100 + 2 * 48}, covered, L"and the whole one-pixel frame is painted once");
  }

  /// The same property at the degenerate end. Thickness is clamped to half the shorter side, so a
  /// 6x6 rectangle asked for a 20-pixel border gets 3: the top and bottom bands meet in the middle,
  /// there is no inside left for the two side bands, and the result is the whole rectangle painted
  /// ONCE. The overlap is what matters, not the count - four bands here would each be painted twice.
  TEST_METHOD(ABorderThickerThanWhatItSurroundsCoversItWithoutOverlapping)
  {
    std::vector<Neuron::UiQuad> quads;
    Neuron::AppendBorder(Neuron::UiRect{0, 0, 6, 6}, 20, 0xFFFFFFFF, quads);
    std::size_t covered = 0;
    for (std::int32_t y = 0; y < 6; ++y)
    {
      for (std::int32_t x = 0; x < 6; ++x)
      {
        std::size_t hits = 0;
        for (const Neuron::UiQuad& quad : quads)
        {
          hits += quad.rect.Contains(x, y) ? 1 : 0;
        }
        Assert::IsTrue(hits <= 1, L"no pixel is painted twice");
        covered += hits;
      }
    }
    Assert::AreEqual(std::size_t{36}, covered, L"and all 36 are painted once");

    quads.clear();
    Neuron::AppendBorder(Neuron::UiRect{0, 0, 1, 1}, 20, 0xFFFFFFFF, quads);
    Assert::AreEqual(std::size_t{1}, quads.size(), L"a rectangle with no inside at all is one filled quad");
    Assert::IsTrue(quads[0].rect == Neuron::UiRect{0, 0, 1, 1});
  }

  /// §3's four button states, and the ruling that a down button's caption takes panelFill because
  /// buttonFillDown is (199, 214, 220) and bodyText at (222, 226, 230) would vanish into it.
  TEST_METHOD(AButtonWearsTheFillItsStateNames)
  {
    const Frontier::ChromePalette palette = DistinctPalette();
    Neuron::IconAtlas icons;
    const auto fillOf = [&](bool _enabled, bool _down, bool _hovered)
    {
      Neuron::UiPanel panel(SELECTION_PANEL, "");
      Neuron::UiWidget button{};
      button.kind = Neuron::UiWidgetKind::Button;
      button.rect = Neuron::UiRect{300, 900, 120, Neuron::BUTTON_HEIGHT_PIXELS};
      button.text = "GO";
      button.enabled = _enabled;
      button.on = _down;
      const std::uint32_t id = panel.Add(button);
      if (_hovered)
      {
        // A PANEL NEVER HOVERS A DISABLED WIDGET: MutableAt tests `enabled`, so the pointer resting
        // on a disabled button leaves Hovered() at 0. That is why the disabled case below is not
        // "disabled beats hovered" in the drawing code so much as in the panel's own tracking.
        panel.OnPointerMove(310, 905);
        Assert::AreEqual(_enabled ? id : 0u, panel.Hovered());
      }
      std::vector<Neuron::UiQuad> quads;
      Neuron::BuildPanelQuads(panel, palette, icons, quads);
      return quads;
    };

    Assert::AreEqual(std::size_t{1}, CountOf(fillOf(true, false, false), Role(palette, &Frontier::ChromePalette::buttonFill)));
    Assert::AreEqual(std::size_t{1}, CountOf(fillOf(true, false, true), Role(palette, &Frontier::ChromePalette::buttonFillHover)));
    Assert::AreEqual(std::size_t{1}, CountOf(fillOf(true, true, false), Role(palette, &Frontier::ChromePalette::buttonFillDown)));
    Assert::AreEqual(std::size_t{1}, CountOf(fillOf(false, false, true), Role(palette, &Frontier::ChromePalette::buttonDisabled)));

    // Two glyphs of caption, in panelFill when down and dimText when disabled.
    Assert::AreEqual(std::size_t{2}, CountOf(fillOf(true, true, false), Role(palette, &Frontier::ChromePalette::panelFill)) - 1,
                     L"the panel's own fill is the other one");
    Assert::AreEqual(std::size_t{2}, CountOf(fillOf(false, false, false), Role(palette, &Frontier::ChromePalette::dimText)));
  }

  /// A button hovered and THEN disabled - which is a frame away in a game where a button goes grey
  /// the moment its power runs out - still draws disabled. The panel's hover is a stored id and it
  /// is not cleared by the widget changing under it, so the drawing code tests `enabled` again; with
  /// that test removed this case comes back buttonFillHover and nothing else notices.
  TEST_METHOD(AButtonDisabledWhileTheCursorSitsOnItStillDrawsDisabled)
  {
    const Frontier::ChromePalette palette = DistinctPalette();
    Neuron::IconAtlas icons;
    Neuron::UiPanel panel(SELECTION_PANEL, "");
    Neuron::UiWidget button{};
    button.kind = Neuron::UiWidgetKind::Button;
    button.rect = Neuron::UiRect{300, 900, 120, Neuron::BUTTON_HEIGHT_PIXELS};
    const std::uint32_t id = panel.Add(button);
    panel.OnPointerMove(310, 905);
    Assert::AreEqual(id, panel.Hovered());
    panel.Find(id)->enabled = false;
    Assert::AreEqual(id, panel.Hovered(), L"the stored hover is not cleared by the widget changing");

    std::vector<Neuron::UiQuad> quads;
    Neuron::BuildPanelQuads(panel, palette, icons, quads);
    Assert::AreEqual(std::size_t{1}, CountOf(quads, Role(palette, &Frontier::ChromePalette::buttonDisabled)));
    Assert::AreEqual(std::size_t{0}, CountOf(quads, Role(palette, &Frontier::ChromePalette::buttonFillHover)));
  }

  /// "Inset 8 pixels from the left, or centred when the button is square" (§3).
  TEST_METHOD(ASquareButtonCentersItsCaptionAndAWideOneInsetsIt)
  {
    const Frontier::ChromePalette palette = DistinctPalette();
    Neuron::IconAtlas icons;
    Neuron::UiPanel panel(Neuron::UiRect{0, 0, 400, 200}, "");
    Neuron::UiWidget square{};
    square.kind = Neuron::UiWidgetKind::Button;
    square.rect = Neuron::UiRect{100, 100, 64, 64};
    square.text = "AB"; // 32 pixels wide, so 16 either side
    panel.Add(square);
    std::vector<Neuron::UiQuad> quads;
    Neuron::BuildPanelQuads(panel, palette, icons, quads);
    Assert::AreEqual(116, FirstOf(quads, Role(palette, &Frontier::ChromePalette::bodyText)).rect.x);
  }

  /// "A filled barEmpty rectangle with a one-pixel panelBorder, filled from the left in its value's
  /// colour" - and the fill goes INSIDE the border, so a full bar does not paint over the frame
  /// that says where it ends.
  TEST_METHOD(ABarFillsInsideItsOwnBorder)
  {
    const Frontier::ChromePalette palette = DistinctPalette();
    Neuron::IconAtlas icons;
    Neuron::UiPanel panel(Neuron::UiRect{0, 0, 400, 200}, "");
    Neuron::UiWidget bar{};
    bar.kind = Neuron::UiWidgetKind::ProgressBar;
    bar.rect = Neuron::UiRect{100, 100, 200, Neuron::BAR_HEIGHT_PIXELS};
    bar.value = 100;
    bar.maximum = 100;
    bar.barStyle = Neuron::UiBarStyle::Build;
    panel.Add(bar);
    std::vector<Neuron::UiQuad> quads;
    Neuron::BuildPanelQuads(panel, palette, icons, quads);

    const Neuron::UiQuad filled = FirstOf(quads, Role(palette, &Frontier::ChromePalette::barBuild));
    Assert::IsTrue(filled.rect == Neuron::UiRect{101, 101, 198, Neuron::BAR_HEIGHT_PIXELS - 2}, L"a full bar stops at the border");
    Assert::AreEqual(std::size_t{1}, CountOf(quads, Role(palette, &Frontier::ChromePalette::barEmpty)), L"and the track is drawn");
  }

  /// §3 names barHealthLow "under a quarter" and barHealth "at or over half" and is silent between;
  /// the quarter is the threshold, which UiDraw.cpp states as a reading rather than a rule.
  TEST_METHOD(AHealthBarTurnsRedOnlyUnderAQuarter)
  {
    const Frontier::ChromePalette palette = DistinctPalette();
    Neuron::IconAtlas icons;
    const auto colorAt = [&](std::int32_t _value)
    {
      Neuron::UiPanel panel(Neuron::UiRect{0, 0, 400, 200}, "");
      Neuron::UiWidget bar{};
      bar.kind = Neuron::UiWidgetKind::ProgressBar;
      bar.rect = Neuron::UiRect{100, 100, 200, Neuron::BAR_HEIGHT_PIXELS};
      bar.value = _value;
      bar.maximum = 100;
      bar.barStyle = Neuron::UiBarStyle::Health;
      panel.Add(bar);
      std::vector<Neuron::UiQuad> quads;
      Neuron::BuildPanelQuads(panel, palette, icons, quads);
      return CountOf(quads, Role(palette, &Frontier::ChromePalette::barHealthLow)) == 1;
    };
    Assert::IsTrue(colorAt(24), L"under a quarter");
    Assert::IsFalse(colorAt(25), L"at a quarter is not under it");
    Assert::IsFalse(colorAt(40), L"the band §3 leaves unnamed takes the ordinary colour");
    Assert::IsFalse(colorAt(100));
  }

  /// A list's selected row takes the accent, and its caption panelFill on top of it for the reason
  /// a down button's does. A row with no caption draws none rather than drawing an empty string.
  TEST_METHOD(ASelectedListRowTakesTheAccentAndItsCaptionStaysReadable)
  {
    const Frontier::ChromePalette palette = DistinctPalette();
    Neuron::IconAtlas icons;
    Neuron::UiPanel panel(Neuron::UiRect{0, 0, 400, 200}, "");
    Neuron::UiWidget list{};
    list.kind = Neuron::UiWidgetKind::List;
    list.rect = Neuron::UiRect{10, 10, 300, 90};
    list.rowHeightPixels = 20;
    list.maximum = 4;
    list.value = 1;
    list.rows = {"ONE", "TWO", "SIX"}; // The fourth row has no caption
    panel.Add(list);
    std::vector<Neuron::UiQuad> quads;
    Neuron::BuildPanelQuads(panel, palette, icons, quads);

    Assert::AreEqual(std::size_t{1}, CountOf(quads, Role(palette, &Frontier::ChromePalette::accent)), L"one row is selected");
    Assert::IsTrue(FirstOf(quads, Role(palette, &Frontier::ChromePalette::accent)).rect == Neuron::ListRowRect(list, 1));
    Assert::AreEqual(std::size_t{3}, CountOf(quads, Role(palette, &Frontier::ChromePalette::panelFill)) - 1, L"TWO, on the accent");
    Assert::AreEqual(std::size_t{6}, CountOf(quads, Role(palette, &Frontier::ChromePalette::bodyText)), L"ONE and SIX, and no fourth");
  }

  TEST_METHOD(TheShippedIconSheetLoadsAsAWholeNumberOfCells)
  {
    Neuron::IconAtlas icons;
    std::string error;
    Assert::IsTrue(icons.Load(ShippedIcons(), error), std::wstring(error.begin(), error.end()).c_str());
    Assert::AreEqual(std::uint32_t{256}, icons.Width());
    Assert::AreEqual(std::uint32_t{128}, icons.Height());
    Assert::AreEqual(std::uint32_t{8}, icons.Columns());
    Assert::AreEqual(std::uint32_t{4}, icons.Rows());
    Assert::IsTrue(icons.CellRect(0) == Neuron::UiRect{0, 0, 32, 32});
    Assert::IsTrue(icons.CellRect(9) == Neuron::UiRect{32, 32, 32, 32}, L"row major");
    Assert::IsTrue(icons.CellRect(32).Empty(), L"one past the last");
    Assert::IsTrue(icons.CellRect(-1).Empty());
  }

  /// A sheet that is not a whole number of 32-pixel cells is refused rather than loaded: every
  /// cell's position is derived from the geometry, so the wrong geometry draws the wrong icon.
  TEST_METHOD(ASheetThatIsNotAWholeNumberOfCellsIsRefused)
  {
    std::vector<std::byte> bytes = ShippedIcons();
    Assert::IsTrue(bytes.size() > 20, L"the sheet has a header to break");
    if (bytes.size() <= 20)
    {
      return;
    }
    bytes[16] = static_cast<std::byte>(100); // the width's low byte: 100 is not a multiple of 32
    Neuron::IconAtlas icons;
    std::string error;
    Assert::IsFalse(icons.Load(bytes, error));
    Assert::IsFalse(icons.Loaded());
    Assert::IsFalse(error.empty(), L"and it says why");
  }

  TEST_METHOD(AnIconCellTheSheetDoesNotHaveDrawsNothing)
  {
    Neuron::IconAtlas icons;
    std::string error;
    Assert::IsTrue(icons.Load(ShippedIcons(), error));
    std::vector<Neuron::UiQuad> quads;
    Neuron::AppendIcon(icons, 999, Neuron::UiRect{0, 0, 32, 32}, 0xFFFFFFFF, quads);
    Assert::IsTrue(quads.empty(), L"rather than a slice of whatever lies at that offset");
    Neuron::AppendIcon(icons, 0, Neuron::UiRect{0, 0, 32, 32}, 0xFFFFFFFF, quads);
    Assert::AreEqual(std::size_t{1}, quads.size());
    Assert::IsTrue(quads[0].kind == Neuron::UiQuadKind::Icon);
  }

  /// "An icon is drawn in bodyText where it is available and dimText where it is not" (§3), which
  /// is the whole of how an unavailable structure or module reads on the command panel.
  TEST_METHOD(AnIconIsTintedByWhetherItIsAvailable)
  {
    const Frontier::ChromePalette palette = DistinctPalette();
    Neuron::IconAtlas icons;
    std::string error;
    Assert::IsTrue(icons.Load(ShippedIcons(), error));
    const auto tintOf = [&](bool _enabled)
    {
      Neuron::UiPanel panel(SELECTION_PANEL, "");
      Neuron::UiWidget icon{};
      icon.kind = Neuron::UiWidgetKind::Icon;
      icon.rect = Neuron::UiRect{300, 900, Neuron::ICON_SIZE_PIXELS, Neuron::ICON_SIZE_PIXELS};
      icon.value = 3;
      icon.enabled = _enabled;
      panel.Add(icon);
      std::vector<Neuron::UiQuad> quads;
      Neuron::BuildPanelQuads(panel, palette, icons, quads);
      for (const Neuron::UiQuad& quad : quads)
      {
        if (quad.kind == Neuron::UiQuadKind::Icon)
        {
          return quad.colorTop;
        }
      }
      return std::uint32_t{0};
    };
    Assert::AreEqual(Role(palette, &Frontier::ChromePalette::bodyText), tintOf(true));
    Assert::AreEqual(Role(palette, &Frontier::ChromePalette::dimText), tintOf(false));
  }

  /// Six vertices a quad, two triangles, and the corners where the rectangle's edges are.
  TEST_METHOD(AQuadBecomesSixVerticesAtItsOwnEdges)
  {
    std::vector<Neuron::UiQuad> quads;
    Neuron::AppendGradient(Neuron::UiRect{10, 20, 100, 50}, 0x11111111, 0x22222222, quads);
    std::vector<Neuron::UiVertex> vertices;
    Neuron::BuildUiVertices(quads, 256, 224, 256, 128, vertices);
    Assert::AreEqual(std::size_t{6}, vertices.size());
    Assert::AreEqual(10.0f, vertices[0].x);
    Assert::AreEqual(20.0f, vertices[0].y);
    Assert::AreEqual(110.0f, vertices[1].x, L"the right edge, half open");
    Assert::AreEqual(70.0f, vertices[2].y, L"and the bottom");
    Assert::AreEqual(std::uint32_t{0x11111111}, vertices[0].color, L"the gradient's top");
    Assert::AreEqual(std::uint32_t{0x22222222}, vertices[2].color, L"and its bottom");
    Assert::AreEqual(std::uint32_t{0}, vertices[0].kind, L"Solid is 0, as UiPS.hlsl branches on it");
  }

  /// THE TEXEL-TO-UV DIVIDE, which is the one place a glyph can land half a texel out of its cell.
  /// 'A' is cell 33: column 1, row 2, so texels 16..32 across and 32..48 down of a 256x224 atlas.
  TEST_METHOD(AGlyphsCoordinatesAreItsCellsEdgesExactly)
  {
    std::vector<Neuron::UiQuad> quads;
    Neuron::AppendText("A", 0, 0, 0xFFFFFFFF, quads);
    std::vector<Neuron::UiVertex> vertices;
    Neuron::BuildUiVertices(quads, 256, 224, 256, 128, vertices);
    Assert::AreEqual(std::size_t{6}, vertices.size());
    Assert::AreEqual(16.0f / 256.0f, vertices[0].u);
    Assert::AreEqual(32.0f / 224.0f, vertices[0].v);
    Assert::AreEqual(32.0f / 256.0f, vertices[1].u, L"one cell across");
    Assert::AreEqual(48.0f / 224.0f, vertices[2].v, L"and one down");
    Assert::AreEqual(std::uint32_t{1}, vertices[0].kind, L"Glyph is 1");
  }

  /// An atlas that never loaded has no size to divide by. The quad is dropped rather than drawn
  /// across texel (0,0), which would be a coloured rectangle where a letter should be.
  TEST_METHOD(ATexturedQuadIsDroppedWhenItsAtlasNeverLoaded)
  {
    std::vector<Neuron::UiQuad> quads;
    Neuron::AppendText("AB", 0, 0, 0xFFFFFFFF, quads);
    Neuron::AppendRect(Neuron::UiRect{0, 0, 10, 10}, 0xFFFFFFFF, quads);
    std::vector<Neuron::UiVertex> vertices;
    Neuron::BuildUiVertices(quads, 0, 0, 256, 128, vertices);
    Assert::AreEqual(std::size_t{6}, vertices.size(), L"the solid one survives and the two glyphs do not");
  }
};

} // namespace ClientTests
