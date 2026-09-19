#include "pch.h"

#include "UiInputSink.h"

#include <algorithm>
#include <utility>

namespace Neuron
{

void UiInputSink::AddPanel(UiPanel* _panel)
{
  if (_panel != nullptr && std::find(m_panels.begin(), m_panels.end(), _panel) == m_panels.end())
  {
    m_panels.push_back(_panel);
  }
}

void UiInputSink::RemovePanel(UiPanel* _panel)
{
  m_panels.erase(std::remove(m_panels.begin(), m_panels.end(), _panel), m_panels.end());
}

void UiInputSink::Clear() noexcept
{
  m_panels.clear();
}

bool UiInputSink::ToAuthored(std::int32_t _clientX, std::int32_t _clientY, AuthoredPosition& _out) const noexcept
{
  return AuthoredFromClient(m_fit, _clientX, _clientY, AUTHORED_WIDTH_PIXELS, AUTHORED_HEIGHT_PIXELS, _out);
}

bool UiInputSink::PointerAuthored(AuthoredPosition& _outPosition) const noexcept
{
  _outPosition = m_pointer;
  return m_pointerInside;
}

bool UiInputSink::PointerOverPanel() const noexcept
{
  if (!m_pointerInside)
  {
    return false;
  }
  for (const UiPanel* panel : m_panels)
  {
    if (panel->Visible() && panel->Rect().Contains(m_pointer.x, m_pointer.y))
    {
      return true;
    }
  }
  return false;
}

UiEventResult UiInputSink::Take() noexcept
{
  return std::exchange(m_pending, UiEventResult{});
}

InputDisposition UiInputSink::OnInputEvent(const InputEvent& _event)
{
  // Last added is asked first, so a modal panel over the others takes what lands on it (§2).
  const auto offer = [this](auto&& _ask) -> InputDisposition
  {
    for (auto panel = m_panels.rbegin(); panel != m_panels.rend(); ++panel)
    {
      const UiEventResult result = _ask(**panel);
      if (result.action != UiAction::None)
      {
        m_pending = result;
      }
      if (result.consumed)
      {
        return InputDisposition::Consumed;
      }
    }
    return InputDisposition::Ignored;
  };

  switch (_event.kind)
  {
  case InputEventKind::MouseMove:
  {
    // ONLY MouseMove CARRIES A POSITION (Client/InputEvent.h): a button event has no x or y, so
    // where a click landed is where the last move put the pointer. That is not a workaround - it is
    // how the window messages arrive, and a click is always preceded by the move that got there.
    m_pointerInside = ToAuthored(_event.x, _event.y, m_pointer);
    if (!m_pointerInside)
    {
      for (UiPanel* panel : m_panels)
      {
        panel->ForgetPointer();
      }
      return InputDisposition::Ignored; // The letterbox is over nothing (§4).
    }
    const AuthoredPosition at = m_pointer;
    return offer([at](UiPanel& _panel) { return _panel.OnPointerMove(at.x, at.y); });
  }
  case InputEventKind::MouseButtonDown:
  {
    if (!m_pointerInside || _event.button == MouseButton::Middle)
    {
      // The middle button aims the camera (§4, and §12's ruling 3), and the interface never wants
      // it: consuming it here would take the camera away wherever the pointer happened to rest.
      return InputDisposition::Ignored;
    }
    const AuthoredPosition at = m_pointer;
    const bool left = _event.button == MouseButton::Left;
    return offer([at, left](UiPanel& _panel) { return _panel.OnPointerDown(at.x, at.y, left); });
  }
  case InputEventKind::MouseButtonUp:
  {
    if (_event.button == MouseButton::Middle)
    {
      return InputDisposition::Ignored;
    }
    const AuthoredPosition at = m_pointer;
    const bool left = _event.button == MouseButton::Left;
    // A release is offered even with the pointer outside the frame, so that a button held down and
    // let go over the letterbox still stops being held. The panel refuses to ACT on it; what it
    // does is forget the press.
    return offer([at, left](UiPanel& _panel) { return _panel.OnPointerUp(at.x, at.y, left); });
  }
  case InputEventKind::Character:
  {
    const std::uint32_t character = _event.character;
    return offer([character](UiPanel& _panel) { return _panel.OnCharacter(character); });
  }
  case InputEventKind::FocusLost:
  {
    // The window went away with the pointer wherever it was, so nothing is hovered and nothing is
    // held. Not consumed: the game wants to know too, to stop the camera scrolling.
    for (UiPanel* panel : m_panels)
    {
      panel->ForgetPointer();
    }
    m_pointerInside = false;
    return InputDisposition::Ignored;
  }
  case InputEventKind::KeyDown:
  case InputEventKind::KeyUp:
  case InputEventKind::MouseRawMove:
  case InputEventKind::Wheel:
    // THE INTERFACE TAKES NO KEY IN M1. The hotkeys of §4 are the game's, the wheel is the camera's,
    // and a raw move is the camera aiming. A text field takes characters, and a Character event is
    // a separate message from the KeyDown that produced it - so typing into a field never eats the
    // key, which is the one place these two could have fought.
    return InputDisposition::Ignored;
  }
  return InputDisposition::Ignored;
}

} // namespace Neuron
