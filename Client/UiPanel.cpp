#include "pch.h"

#include "UiPanel.h"

#include <algorithm>
#include <utility>

namespace Neuron
{

namespace
{

/// The backspace WM_CHAR delivers. Spelled here rather than as '\b' so that the one control
/// character this field acts on is named where a reader meets it.
constexpr std::uint32_t CHARACTER_BACKSPACE = 8;
constexpr std::uint32_t CHARACTER_RETURN = 13;
constexpr std::uint32_t CHARACTER_ESCAPE = 27;
/// Anything below this is a control code the field ignores, the three above excepted.
constexpr std::uint32_t FIRST_PRINTABLE_CHARACTER = 32;
/// WM_CHAR is a UTF-16 code unit; this field takes the ASCII range the Spectrum atlas has cells
/// for and drops the rest, rather than storing bytes it could never draw.
constexpr std::uint32_t LAST_DRAWABLE_CHARACTER = 255;

} // namespace

UiPanel::UiPanel(const UiRect& _rect, std::string _title)
  : m_rect(_rect),
    m_title(std::move(_title))
{
}

std::uint32_t UiPanel::Add(const UiWidget& _widget)
{
  UiWidget widget = _widget;
  if (widget.id == 0)
  {
    widget.id = m_nextId++;
  }
  else
  {
    m_nextId = std::max(m_nextId, widget.id + 1);
  }
  const std::uint32_t id = widget.id;
  m_widgets.push_back(std::move(widget));
  return id;
}

UiWidget* UiPanel::Find(std::uint32_t _id) noexcept
{
  for (UiWidget& widget : m_widgets)
  {
    if (widget.id == _id)
    {
      return &widget;
    }
  }
  return nullptr;
}

const UiWidget* UiPanel::Find(std::uint32_t _id) const noexcept
{
  for (const UiWidget& widget : m_widgets)
  {
    if (widget.id == _id)
    {
      return &widget;
    }
  }
  return nullptr;
}

UiWidget* UiPanel::MutableAt(std::int32_t _x, std::int32_t _y) noexcept
{
  // Backwards, so that the last widget added wins where two overlap: a panel builds its background
  // first and its controls after, which is the order everything else about a panel reads in.
  for (auto widget = m_widgets.rbegin(); widget != m_widgets.rend(); ++widget)
  {
    if (Interactive(widget->kind) && widget->enabled && widget->rect.Contains(_x, _y))
    {
      return &*widget;
    }
  }
  return nullptr;
}

const UiWidget* UiPanel::WidgetAt(std::int32_t _x, std::int32_t _y) const noexcept
{
  return const_cast<UiPanel*>(this)->MutableAt(_x, _y);
}

UiEventResult UiPanel::OnPointerMove(std::int32_t _x, std::int32_t _y) noexcept
{
  UiEventResult result{};
  if (!m_visible || !m_rect.Contains(_x, _y))
  {
    m_hovered = 0;
    return result; // NOT consumed: a move over the world is the camera's and the cursor's.
  }
  const UiWidget* under = WidgetAt(_x, _y);
  m_hovered = under != nullptr ? under->id : 0;
  // A MOVE OVER A PANEL IS NOT CONSUMED. It is not an edge: the game still wants to know where the
  // pointer is, to draw its own cursor (§4) and to keep edge scrolling honest. What a panel
  // consumes is the click, which is the thing that would otherwise reach the world.
  return result;
}

UiEventResult UiPanel::OnPointerDown(std::int32_t _x, std::int32_t _y, bool _leftButton)
{
  UiEventResult result{};
  if (!m_visible || !m_rect.Contains(_x, _y))
  {
    return result;
  }
  result.consumed = true; // Everything that lands on a panel stops here (§4).

  UiWidget* under = MutableAt(_x, _y);
  if (!_leftButton)
  {
    return result; // A right click on a panel does nothing, and the panel consumes it (§4).
  }

  // The focus follows the press, so clicking away from a field stops typing into it.
  m_focus = (under != nullptr && under->kind == UiWidgetKind::TextField) ? under->id : 0;
  if (under == nullptr)
  {
    m_pressed = 0;
    return result;
  }
  m_pressed = under->id;
  if (under->kind == UiWidgetKind::Button)
  {
    under->on = true; // Held, for the buttonFillDown of §3. The release is what acts.
  }
  return result;
}

UiEventResult UiPanel::OnPointerUp(std::int32_t _x, std::int32_t _y, bool _leftButton)
{
  UiEventResult result{};
  const std::uint32_t pressed = std::exchange(m_pressed, 0u);
  if (!m_visible)
  {
    return result;
  }
  // A button that was held is let go whether or not the pointer is still on it.
  if (pressed != 0)
  {
    UiWidget* held = Find(pressed);
    if (held != nullptr && held->kind == UiWidgetKind::Button)
    {
      held->on = false;
    }
  }
  if (!m_rect.Contains(_x, _y))
  {
    return result;
  }
  result.consumed = true;
  if (!_leftButton)
  {
    return result;
  }

  UiWidget* under = MutableAt(_x, _y);
  // THE RELEASE ONLY ACTS INSIDE THE WIDGET THE PRESS ARMED. Pressing a button and sliding off it
  // is how anyone changes their mind, and a release over a different widget must not fire that one
  // either - the press it would be acting on never happened.
  if (under == nullptr || under->id != pressed)
  {
    return result;
  }

  result.widget = under->id;
  switch (under->kind)
  {
  case UiWidgetKind::Button:
    result.action = UiAction::Pressed;
    break;
  case UiWidgetKind::Toggle:
    under->on = !under->on;
    result.action = UiAction::Toggled;
    result.value = under->on ? 1 : 0;
    break;
  case UiWidgetKind::List:
  {
    const std::int32_t row = ListRowAt(*under, _y);
    if (row < 0)
    {
      result.widget = 0;
      break; // The empty space under the last row selects nothing.
    }
    under->value = row;
    result.action = UiAction::Selected;
    result.value = row;
    break;
  }
  case UiWidgetKind::TextField:
    break; // Taking the focus is all a click does to a field; the characters do the rest.
  case UiWidgetKind::Label:
  case UiWidgetKind::ProgressBar:
  case UiWidgetKind::Icon:
    break; // Not interactive, so MutableAt never returns one.
  }
  return result;
}

UiEventResult UiPanel::OnCharacter(std::uint32_t _character)
{
  UiEventResult result{};
  if (!m_visible || m_focus == 0)
  {
    // NOT CONSUMED WITH NO FOCUS. A character nobody is typing into is a key the game may have a
    // binding for, and consuming it here would delete that input (Client/InputRouter.h says so of
    // answering Consumed for an event a sink merely looked at).
    return result;
  }
  UiWidget* field = Find(m_focus);
  if (field == nullptr || field->kind != UiWidgetKind::TextField || !field->enabled)
  {
    m_focus = 0;
    return result;
  }

  result.consumed = true;
  result.widget = field->id;
  if (_character == CHARACTER_ESCAPE)
  {
    m_focus = 0;
    return result;
  }
  if (_character == CHARACTER_RETURN)
  {
    result.action = UiAction::TextSubmitted;
    m_focus = 0;
    return result;
  }
  if (_character == CHARACTER_BACKSPACE)
  {
    if (!field->text.empty())
    {
      field->text.pop_back();
      result.action = UiAction::TextEdited;
    }
    return result;
  }
  if (_character < FIRST_PRINTABLE_CHARACTER || _character > LAST_DRAWABLE_CHARACTER)
  {
    return result; // Consumed, because the field has the focus, but not stored: it cannot be drawn.
  }
  if (field->textLimit != 0 && field->text.size() >= field->textLimit)
  {
    return result;
  }
  field->text.push_back(static_cast<char>(_character));
  result.action = UiAction::TextEdited;
  return result;
}

void UiPanel::ForgetPointer() noexcept
{
  if (m_pressed != 0)
  {
    UiWidget* held = Find(m_pressed);
    if (held != nullptr && held->kind == UiWidgetKind::Button)
    {
      held->on = false;
    }
  }
  m_pressed = 0;
  m_hovered = 0;
}

} // namespace Neuron
