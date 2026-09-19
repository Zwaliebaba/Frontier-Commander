#include "pch.h"

#include "UiWidgets.h"

namespace Neuron
{

std::int32_t ListRowAt(const UiWidget& _list, std::int32_t _y) noexcept
{
  if (_list.rowHeightPixels <= 0 || _list.maximum <= 0 || _y < _list.rect.y)
  {
    return -1;
  }
  const std::int32_t row = (_y - _list.rect.y) / _list.rowHeightPixels;
  // A list taller than its rows is a list with empty space under them, and a click there chooses
  // nothing rather than the last row: the row under the pointer is the one the pointer is on.
  return row < _list.maximum ? row : -1;
}

UiRect ListRowRect(const UiWidget& _list, std::int32_t _row) noexcept
{
  if (_row < 0 || _row >= _list.maximum || _list.rowHeightPixels <= 0)
  {
    return {};
  }
  const std::int32_t top = _list.rect.y + _row * _list.rowHeightPixels;
  if (top + _list.rowHeightPixels > _list.rect.Bottom())
  {
    return {}; // A row past the bottom of the list is not drawn, as RowIn does it for a panel.
  }
  return UiRect{_list.rect.x, top, _list.rect.width, _list.rowHeightPixels};
}

} // namespace Neuron
