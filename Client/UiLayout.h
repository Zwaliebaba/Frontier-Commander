#pragma once

#include "ScaleMode.h"

#include <cstdint>

// The arithmetic every panel is built out of (Design/Interface.md §2, §3), at the authored
// resolution and unconditional: a rectangle, the client area inside a panel's chrome, and the rows
// and columns a panel's contents are laid out in. Integer throughout, because every rectangle in
// the interface is a whole number of authored pixels and a layout that rounded would put a
// one-pixel border half on a pixel.
//
// IT NAMES NO PANEL. `Minimap`, `Selection` and `Command` are things this GAME has, and an engine
// that knew them by name would be in the wrong layer (AGENTS.md R9). §2's table of rectangles is
// the game's, and m1-vertical-slice/K4 writes it; what is here is what any panel of any game needs
// to place a button under a label.

namespace Neuron
{

/// A rectangle in authored pixels. Half-open: a point on the right or bottom edge is outside, so
/// two rectangles that share an edge share no pixel and a click lands in exactly one of them.
struct UiRect
{
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;

  [[nodiscard]] constexpr bool Contains(std::int32_t _x, std::int32_t _y) const noexcept
  {
    return _x >= x && _y >= y && _x < x + width && _y < y + height;
  }

  [[nodiscard]] constexpr std::int32_t Right() const noexcept
  {
    return x + width;
  }

  [[nodiscard]] constexpr std::int32_t Bottom() const noexcept
  {
    return y + height;
  }

  [[nodiscard]] constexpr bool Empty() const noexcept
  {
    return width <= 0 || height <= 0;
  }

  [[nodiscard]] constexpr bool operator==(const UiRect&) const noexcept = default;
};

// ── The chrome's measurements (Design/Interface.md §3) ──────────────────────────────────────

/// Every panel, button and bar carries a one-pixel border. One, not two: §3 and GameDesign.md §11.4
/// both say so, and the pixel font is drawn 1:1, so a two-pixel border would be twice the weight of
/// every stroke in the text beside it.
inline constexpr std::int32_t PANEL_BORDER_PIXELS = 1;
inline constexpr std::int32_t PANEL_TITLE_HEIGHT_PIXELS = 20;
/// The client area is inset this far from the left, the right and the bottom, and starts
/// PANEL_CLIENT_TOP_PIXELS from the top - which is the title strip plus the same inset.
inline constexpr std::int32_t PANEL_INSET_PIXELS = 8;
inline constexpr std::int32_t PANEL_CLIENT_TOP_PIXELS = 24;
inline constexpr std::int32_t PANEL_TITLE_TEXT_X_PIXELS = 8;

inline constexpr std::int32_t BUTTON_HEIGHT_PIXELS = 24;
inline constexpr std::int32_t BUTTON_TEXT_INSET_PIXELS = 8;
inline constexpr std::int32_t BAR_HEIGHT_PIXELS = 12;
inline constexpr std::int32_t ICON_SIZE_PIXELS = 32;

/// The tooltip's offset from the pointer and its dwell, from §4.
inline constexpr std::int32_t TOOLTIP_OFFSET_PIXELS = 16;
inline constexpr std::uint32_t TOOLTIP_DWELL_MILLISECONDS = 1000;
inline constexpr std::uint32_t TOOLTIP_CHARACTERS_PER_SECOND = 50;

// ── The arithmetic ──────────────────────────────────────────────────────────────────────────

/// What is inside a panel's chrome: the title strip and the insets of §3 taken off. An area that
/// would come out negative comes out empty rather than inside out, so a panel too small to hold
/// anything holds nothing instead of a rectangle that wraps around itself.
[[nodiscard]] UiRect ClientAreaOf(const UiRect& _panel) noexcept;

/// The title strip along the top of a panel, inside its border.
[[nodiscard]] UiRect TitleStripOf(const UiRect& _panel) noexcept;

/// The _index'th row of _height pixels down _area, with _spacing between rows. Rows past the bottom
/// come back empty rather than drawn off the panel.
[[nodiscard]] UiRect RowIn(const UiRect& _area, std::int32_t _index, std::int32_t _height, std::int32_t _spacing) noexcept;

/// One of _count columns across _area with _spacing between them.
///
/// THE REMAINDER GOES TO THE LEFTMOST COLUMNS, one pixel each, rather than being dropped. Three
/// columns across a 512-pixel panel is 170 and two thirds; dropping the remainder leaves a
/// two-pixel gap at the right-hand edge that nothing else in the layout has, and a reviewer reads
/// it as a bug in the panel rather than in the division.
[[nodiscard]] UiRect ColumnIn(const UiRect& _area, std::int32_t _index, std::int32_t _count, std::int32_t _spacing) noexcept;

/// _rect inset by _pixels on every side, or empty if there is nothing left.
[[nodiscard]] UiRect Inset(const UiRect& _rect, std::int32_t _pixels) noexcept;

/// The part of a bar that is filled, for a value out of a maximum. A maximum of zero is empty
/// rather than full: nothing out of nothing is not everything, and a full bar on a structure with
/// no hit points is the one reading that would be actively wrong.
[[nodiscard]] UiRect FilledPartOf(const UiRect& _bar, std::int32_t _value, std::int32_t _maximum) noexcept;

/// Where a tooltip goes for a pointer at _x, _y: offset down and right, flipped to the other side
/// of the pointer when it would leave the frame (§4).
[[nodiscard]] UiRect TooltipRect(std::int32_t _pointerX, std::int32_t _pointerY, std::int32_t _width, std::int32_t _height,
                                 std::int32_t _frameWidth = static_cast<std::int32_t>(AUTHORED_WIDTH_PIXELS),
                                 std::int32_t _frameHeight = static_cast<std::int32_t>(AUTHORED_HEIGHT_PIXELS)) noexcept;

} // namespace Neuron
