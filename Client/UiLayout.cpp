#include "pch.h"

#include "UiLayout.h"

#include <algorithm>

namespace Neuron
{

namespace
{

/// A rectangle whose width or height came out at or below zero is empty, and empty is {0,0,0,0}
/// rather than a negative extent. Every caller below goes through this, so there is one answer to
/// "what does a panel too small to hold this give back" instead of one per function.
[[nodiscard]] UiRect OrEmpty(std::int32_t _x, std::int32_t _y, std::int32_t _width, std::int32_t _height) noexcept
{
  return (_width <= 0 || _height <= 0) ? UiRect{} : UiRect{_x, _y, _width, _height};
}

} // namespace

UiRect ClientAreaOf(const UiRect& _panel) noexcept
{
  return OrEmpty(_panel.x + PANEL_INSET_PIXELS, _panel.y + PANEL_CLIENT_TOP_PIXELS, _panel.width - 2 * PANEL_INSET_PIXELS,
                 _panel.height - PANEL_CLIENT_TOP_PIXELS - PANEL_INSET_PIXELS);
}

UiRect TitleStripOf(const UiRect& _panel) noexcept
{
  return OrEmpty(_panel.x + PANEL_BORDER_PIXELS, _panel.y + PANEL_BORDER_PIXELS, _panel.width - 2 * PANEL_BORDER_PIXELS,
                 std::min(PANEL_TITLE_HEIGHT_PIXELS, _panel.height - 2 * PANEL_BORDER_PIXELS));
}

UiRect RowIn(const UiRect& _area, std::int32_t _index, std::int32_t _height, std::int32_t _spacing) noexcept
{
  if (_index < 0 || _height <= 0)
  {
    return {};
  }
  const std::int32_t top = _area.y + _index * (_height + _spacing);
  if (top + _height > _area.Bottom())
  {
    return {}; // Past the bottom of the area: empty, rather than drawn outside the panel.
  }
  return OrEmpty(_area.x, top, _area.width, _height);
}

UiRect ColumnIn(const UiRect& _area, std::int32_t _index, std::int32_t _count, std::int32_t _spacing) noexcept
{
  if (_index < 0 || _count <= 0 || _index >= _count)
  {
    return {};
  }
  const std::int32_t usable = _area.width - (_count - 1) * _spacing;
  if (usable <= 0)
  {
    return {};
  }
  const std::int32_t each = usable / _count;
  const std::int32_t remainder = usable % _count;
  // The leftmost `remainder` columns are one pixel wider, and every column after them is shifted by
  // how many of those came before it. That is what spends the remainder instead of leaving it as a
  // gap at the right-hand edge.
  const std::int32_t widthHere = each + (_index < remainder ? 1 : 0);
  const std::int32_t before = _index * each + std::min(_index, remainder);
  return OrEmpty(_area.x + before + _index * _spacing, _area.y, widthHere, _area.height);
}

UiRect Inset(const UiRect& _rect, std::int32_t _pixels) noexcept
{
  return OrEmpty(_rect.x + _pixels, _rect.y + _pixels, _rect.width - 2 * _pixels, _rect.height - 2 * _pixels);
}

UiRect FilledPartOf(const UiRect& _bar, std::int32_t _value, std::int32_t _maximum) noexcept
{
  if (_maximum <= 0 || _value <= 0)
  {
    return {};
  }
  const std::int32_t clamped = std::min(_value, _maximum);
  // int64 because a bar of a thousand pixels times a value in hundredths of power leaves int32 on a
  // stockpile any real match reaches.
  const std::int32_t filled = static_cast<std::int32_t>(static_cast<std::int64_t>(_bar.width) * clamped / _maximum);
  return OrEmpty(_bar.x, _bar.y, filled, _bar.height);
}

UiRect TooltipRect(std::int32_t _pointerX, std::int32_t _pointerY, std::int32_t _width, std::int32_t _height, std::int32_t _frameWidth,
                   std::int32_t _frameHeight) noexcept
{
  if (_width <= 0 || _height <= 0)
  {
    return {};
  }
  std::int32_t x = _pointerX + TOOLTIP_OFFSET_PIXELS;
  std::int32_t y = _pointerY + TOOLTIP_OFFSET_PIXELS;
  if (x + _width > _frameWidth)
  {
    x = _pointerX - TOOLTIP_OFFSET_PIXELS - _width; // Flipped to the other side of the pointer (§4)
  }
  if (y + _height > _frameHeight)
  {
    y = _pointerY - TOOLTIP_OFFSET_PIXELS - _height;
  }
  // A tooltip wider than the frame has nowhere to flip to, so it is pinned to the edge rather than
  // drawn off it. Clamping after the flip and not instead of it: the flip is what §4 asks for and
  // this is only the case where the flip does not help.
  x = std::clamp(x, 0, std::max(0, _frameWidth - _width));
  y = std::clamp(y, 0, std::max(0, _frameHeight - _height));
  return UiRect{x, y, _width, _height};
}

} // namespace Neuron
