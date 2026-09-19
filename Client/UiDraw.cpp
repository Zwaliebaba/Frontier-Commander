#include "pch.h"

#include "UiDraw.h"

#include <algorithm>
#include <cctype>

namespace Neuron
{

namespace
{

/// A title is drawn uppercased (§3). Through unsigned char, because std::toupper on a negative
/// char is undefined and a high byte is exactly what a player's name can carry.
[[nodiscard]] std::string Uppercased(std::string_view _text)
{
  std::string upper(_text);
  for (char& character : upper)
  {
    character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
  }
  return upper;
}

/// Where a line of text sits so that its 16-pixel glyph box is centred in _area.
[[nodiscard]] std::int32_t CenteredTextY(const UiRect& _area) noexcept
{
  return _area.y + (_area.height - GLYPH_SIZE_PIXELS) / 2;
}

/// A button's caption is inset 8 from the left, "or centred when the button is square" (§3).
[[nodiscard]] std::int32_t CaptionX(const UiRect& _button, std::string_view _caption) noexcept
{
  if (_button.width == _button.height)
  {
    return _button.x + (_button.width - LineWidthPixels(_caption)) / 2;
  }
  return _button.x + BUTTON_TEXT_INSET_PIXELS;
}

/// The fill and caption a button or toggle wears, by §3's four states. `_down` is the toggle's own
/// state or the button's held flag; the panel sets the latter on the press and clears it on the
/// release, so a button lights while it is held and not after.
struct ButtonColors
{
  std::uint32_t fill;
  std::uint32_t caption;
};

[[nodiscard]] ButtonColors ButtonColorsOf(const Frontier::ChromePalette& _palette, bool _enabled, bool _down, bool _hovered) noexcept
{
  if (!_enabled)
  {
    // "Unavailable; its caption takes dimText" (§3).
    return {PackedColor(_palette.buttonDisabled), PackedColor(_palette.dimText)};
  }
  if (_down)
  {
    // "A down button's caption takes panelFill so that it stays readable on the light fill" (§3):
    // buttonFillDown is (199, 214, 220), which bodyText at (222, 226, 230) would vanish into.
    return {PackedColor(_palette.buttonFillDown), PackedColor(_palette.panelFill)};
  }
  return {PackedColor(_hovered ? _palette.buttonFillHover : _palette.buttonFill), PackedColor(_palette.bodyText)};
}

/// The colour a bar's filled part takes (§3).
///
/// THE QUARTER-TO-HALF BAND IS A READING, NOT A RULE. §3's table names `barHealth` "at or over
/// half" and `barHealthLow` "under a quarter" and says nothing about between; the two named
/// colours are the ordinary one and the alarm, so the band that is neither takes the ordinary one
/// and only the quarter turns it red. That also matches the same document's "damage under a
/// quarter" beside `warning`, which is the one threshold it states twice.
[[nodiscard]] std::uint32_t BarFillColor(const Frontier::ChromePalette& _palette, const UiWidget& _bar) noexcept
{
  if (_bar.barStyle == UiBarStyle::Build)
  {
    return PackedColor(_palette.barBuild);
  }
  const bool low = _bar.maximum > 0 && static_cast<std::int64_t>(_bar.value) * 4 < static_cast<std::int64_t>(_bar.maximum);
  return PackedColor(low ? _palette.barHealthLow : _palette.barHealth);
}

} // namespace

bool IconAtlas::Load(std::span<const std::byte> _ddsBytes, std::string& _error)
{
  TextureFile file;
  if (!TextureFile::Read(_ddsBytes, file, _error))
  {
    return false;
  }
  const std::uint32_t cell = static_cast<std::uint32_t>(ICON_SIZE_PIXELS);
  if (file.Width() == 0 || file.Height() == 0 || file.Width() % cell != 0 || file.Height() % cell != 0)
  {
    _error = "the icon sheet is " + std::to_string(file.Width()) + "x" + std::to_string(file.Height()) +
             ", which is not a whole number of " + std::to_string(cell) + " pixel cells (Interface.md 3)";
    return false;
  }
  std::vector<std::uint8_t> pixels;
  if (!file.DecodeRgba8(0, pixels))
  {
    _error = "the icon sheet could not be decoded to RGBA8";
    return false;
  }
  m_pixels = std::move(pixels);
  m_width = file.Width();
  m_height = file.Height();
  return true;
}

UiRect IconAtlas::CellRect(std::int32_t _index) const noexcept
{
  if (_index < 0 || static_cast<std::uint32_t>(_index) >= CellCount())
  {
    return UiRect{};
  }
  const std::int32_t columns = static_cast<std::int32_t>(Columns());
  return UiRect{(_index % columns) * ICON_SIZE_PIXELS, (_index / columns) * ICON_SIZE_PIXELS, ICON_SIZE_PIXELS, ICON_SIZE_PIXELS};
}

void AppendRect(const UiRect& _rect, std::uint32_t _color, std::vector<UiQuad>& _outQuads)
{
  AppendGradient(_rect, _color, _color, _outQuads);
}

void AppendGradient(const UiRect& _rect, std::uint32_t _top, std::uint32_t _bottom, std::vector<UiQuad>& _outQuads)
{
  if (_rect.Empty())
  {
    return;
  }
  UiQuad quad{};
  quad.rect = _rect;
  quad.colorTop = _top;
  quad.colorBottom = _bottom;
  quad.kind = UiQuadKind::Solid;
  _outQuads.push_back(quad);
}

void AppendBorder(const UiRect& _rect, std::int32_t _thickness, std::uint32_t _color, std::vector<UiQuad>& _outQuads)
{
  if (_rect.Empty() || _thickness <= 0)
  {
    return;
  }
  // Clamped, so that a border thicker than what it surrounds is a filled rectangle rather than four
  // rectangles that overlap and blend with themselves - which for a colour with alpha is a different
  // colour, and panelFill is (24, 10, 12, 245).
  const std::int32_t thickness = std::min(_thickness, std::min(_rect.width, _rect.height) / 2);
  if (thickness <= 0)
  {
    AppendRect(_rect, _color, _outQuads);
    return;
  }
  AppendRect(UiRect{_rect.x, _rect.y, _rect.width, thickness}, _color, _outQuads);
  AppendRect(UiRect{_rect.x, _rect.Bottom() - thickness, _rect.width, thickness}, _color, _outQuads);
  const std::int32_t innerHeight = _rect.height - 2 * thickness;
  AppendRect(UiRect{_rect.x, _rect.y + thickness, thickness, innerHeight}, _color, _outQuads);
  AppendRect(UiRect{_rect.Right() - thickness, _rect.y + thickness, thickness, innerHeight}, _color, _outQuads);
}

void AppendText(std::string_view _text, std::int32_t _x, std::int32_t _y, std::uint32_t _color, std::vector<UiQuad>& _outQuads)
{
  std::vector<GlyphQuad> glyphs;
  LayoutText(_text, _x, _y, glyphs);
  for (const GlyphQuad& glyph : glyphs)
  {
    UiQuad quad{};
    quad.rect = glyph.screen;
    quad.atlas = UiRect{glyph.atlasX, glyph.atlasY, GLYPH_SIZE_PIXELS, GLYPH_SIZE_PIXELS};
    quad.colorTop = _color;
    quad.colorBottom = _color;
    quad.kind = UiQuadKind::Glyph;
    _outQuads.push_back(quad);
  }
}

void AppendIcon(const IconAtlas& _icons, std::int32_t _cell, const UiRect& _rect, std::uint32_t _color, std::vector<UiQuad>& _outQuads)
{
  const UiRect source = _icons.CellRect(_cell);
  if (source.Empty() || _rect.Empty())
  {
    return;
  }
  UiQuad quad{};
  quad.rect = _rect;
  quad.atlas = source;
  quad.colorTop = _color;
  quad.colorBottom = _color;
  quad.kind = UiQuadKind::Icon;
  _outQuads.push_back(quad);
}

void BuildPanelQuads(const UiPanel& _panel, const Frontier::ChromePalette& _palette, const IconAtlas& _icons,
                     std::vector<UiQuad>& _outQuads)
{
  if (!_panel.Visible() || _panel.Rect().Empty())
  {
    return;
  }
  const UiRect panel = _panel.Rect();

  // "A filled rectangle in panelFill, a one-pixel panelBorder on all four edges, and a 20-pixel
  // title strip along the top filled with the vertical gradient panelTitleFrom -> panelTitleTo,
  // holding the panel's name in titleText, uppercased, at x + 8" (§3).
  AppendRect(panel, PackedColor(_palette.panelFill), _outQuads);
  AppendBorder(panel, PANEL_BORDER_PIXELS, PackedColor(_palette.panelBorder), _outQuads);
  const UiRect title = TitleStripOf(panel);
  AppendGradient(title, PackedColor(_palette.panelTitleFrom), PackedColor(_palette.panelTitleTo), _outQuads);
  if (!_panel.Title().empty() && !title.Empty())
  {
    AppendText(Uppercased(_panel.Title()), panel.x + PANEL_TITLE_TEXT_X_PIXELS, CenteredTextY(title), PackedColor(_palette.titleText),
               _outQuads);
  }

  for (const UiWidget& widget : _panel.Widgets())
  {
    if (widget.rect.Empty())
    {
      continue;
    }
    const bool hovered = _panel.Hovered() == widget.id;
    switch (widget.kind)
    {
    case UiWidgetKind::Label:
      AppendText(widget.text, widget.rect.x, CenteredTextY(widget.rect), PackedColor(widget.enabled ? _palette.bodyText : _palette.dimText),
                 _outQuads);
      break;
    case UiWidgetKind::Button:
    case UiWidgetKind::Toggle:
    {
      const ButtonColors colors = ButtonColorsOf(_palette, widget.enabled, widget.on, hovered && widget.enabled);
      AppendRect(widget.rect, colors.fill, _outQuads);
      AppendBorder(widget.rect, PANEL_BORDER_PIXELS, PackedColor(_palette.panelBorder), _outQuads);
      AppendText(widget.text, CaptionX(widget.rect, widget.text), CenteredTextY(widget.rect), colors.caption, _outQuads);
      break;
    }
    case UiWidgetKind::List:
    {
      for (std::int32_t row = 0; row < widget.maximum; ++row)
      {
        const UiRect rowRect = ListRowRect(widget, row);
        if (rowRect.Empty())
        {
          break; // Past the bottom of the list: every later row is too.
        }
        const bool selected = widget.value == row;
        if (selected)
        {
          // "accent: the one accent - selection, the active tab, a ready item" (§3). The caption
          // takes panelFill on it for the reason a down button's does: the accent is (255, 196, 64).
          AppendRect(rowRect, PackedColor(_palette.accent), _outQuads);
        }
        if (static_cast<std::size_t>(row) < widget.rows.size())
        {
          const std::uint32_t caption =
            selected ? PackedColor(_palette.panelFill) : PackedColor(widget.enabled ? _palette.bodyText : _palette.dimText);
          AppendText(widget.rows[static_cast<std::size_t>(row)], rowRect.x, CenteredTextY(rowRect), caption, _outQuads);
        }
      }
      break;
    }
    case UiWidgetKind::ProgressBar:
      // "A filled barEmpty rectangle with a one-pixel panelBorder, 12 pixels high, filled from the
      // left in its value's colour" (§3). The fill goes inside the border, so a full bar does not
      // paint over the frame that says where it ends.
      AppendRect(widget.rect, PackedColor(_palette.barEmpty), _outQuads);
      AppendRect(FilledPartOf(Inset(widget.rect, PANEL_BORDER_PIXELS), widget.value, widget.maximum), BarFillColor(_palette, widget),
                 _outQuads);
      AppendBorder(widget.rect, PANEL_BORDER_PIXELS, PackedColor(_palette.panelBorder), _outQuads);
      break;
    case UiWidgetKind::Icon:
      // "In bodyText where it is available and dimText where it is not" (§3).
      AppendIcon(_icons, widget.value, widget.rect, PackedColor(widget.enabled ? _palette.bodyText : _palette.dimText), _outQuads);
      break;
    case UiWidgetKind::TextField:
    {
      // §3 gives no field of its own, so it is a button that is never down: the same fill, border
      // and inset caption, with the accent border while it holds the focus so that the player can
      // see where the characters are going.
      const bool focused = _panel.Focus() == widget.id;
      AppendRect(widget.rect, PackedColor(widget.enabled ? _palette.buttonFill : _palette.buttonDisabled), _outQuads);
      AppendBorder(widget.rect, PANEL_BORDER_PIXELS, PackedColor(focused ? _palette.accent : _palette.panelBorder), _outQuads);
      AppendText(widget.text, widget.rect.x + BUTTON_TEXT_INSET_PIXELS, CenteredTextY(widget.rect),
                 PackedColor(widget.enabled ? _palette.bodyText : _palette.dimText), _outQuads);
      break;
    }
    }
  }
}

void BuildUiVertices(std::span<const UiQuad> _quads, std::uint32_t _fontWidth, std::uint32_t _fontHeight, std::uint32_t _iconWidth,
                     std::uint32_t _iconHeight, std::vector<UiVertex>& _outVertices)
{
  _outVertices.clear();
  _outVertices.reserve(_quads.size() * VERTICES_PER_QUAD);
  for (const UiQuad& quad : _quads)
  {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    if (quad.kind != UiQuadKind::Solid)
    {
      const std::uint32_t width = quad.kind == UiQuadKind::Glyph ? _fontWidth : _iconWidth;
      const std::uint32_t height = quad.kind == UiQuadKind::Glyph ? _fontHeight : _iconHeight;
      if (width == 0 || height == 0)
      {
        // An atlas that never loaded. Drawing the quad would read texel (0,0) across its whole
        // face, which is a coloured rectangle where a letter should be; it is dropped instead.
        continue;
      }
      // EDGES, NOT CENTRES. The rectangle is given by its texel edges and the rasterizer samples at
      // the pixel centre, so a 16-texel cell drawn on 16 pixels lands on its own texels exactly and
      // no half-texel nudge is needed - which is the 1:1 of Design/Interface.md §3.
      left = static_cast<float>(quad.atlas.x) / static_cast<float>(width);
      top = static_cast<float>(quad.atlas.y) / static_cast<float>(height);
      right = static_cast<float>(quad.atlas.Right()) / static_cast<float>(width);
      bottom = static_cast<float>(quad.atlas.Bottom()) / static_cast<float>(height);
    }
    const auto x0 = static_cast<float>(quad.rect.x);
    const auto y0 = static_cast<float>(quad.rect.y);
    const auto x1 = static_cast<float>(quad.rect.Right());
    const auto y1 = static_cast<float>(quad.rect.Bottom());
    const auto kind = static_cast<std::uint32_t>(quad.kind);
    const UiVertex topLeft{x0, y0, left, top, quad.colorTop, kind};
    const UiVertex topRight{x1, y0, right, top, quad.colorTop, kind};
    const UiVertex bottomLeft{x0, y1, left, bottom, quad.colorBottom, kind};
    const UiVertex bottomRight{x1, y1, right, bottom, quad.colorBottom, kind};
    _outVertices.push_back(topLeft);
    _outVertices.push_back(topRight);
    _outVertices.push_back(bottomLeft);
    _outVertices.push_back(bottomLeft);
    _outVertices.push_back(topRight);
    _outVertices.push_back(bottomRight);
  }
}

} // namespace Neuron
