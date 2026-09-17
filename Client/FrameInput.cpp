#include "pch.h"

#include "FrameInput.h"

namespace Neuron
{

std::size_t DeriveFrameInput(std::span<const InputEvent> _events, FrameInput& _state) noexcept
{
  // Everything per-frame resets; everything held persists.
  _state.keyEdges.fill(0);
  _state.buttonEdges.fill(0);
  _state.relativeX = 0;
  _state.relativeY = 0;
  _state.wheelDetents = 0;
  const std::int32_t startX = _state.mouseX;
  const std::int32_t startY = _state.mouseY;

  std::size_t consumed = 0;
  for (; consumed < _events.size(); ++consumed)
  {
    const InputEvent& event = _events[consumed];
    switch (event.kind)
    {
    case InputEventKind::KeyDown:
      // A repeat is not an edge: the key is already down, and Windows sends one every few tens of
      // milliseconds while it stays down.
      if (event.repeat)
      {
        break;
      }
      // Already edged this frame: stop here and leave this event, and everything behind it, for
      // the next frame, so that nothing is reordered.
      if (_state.keyEdges[event.key] != 0)
      {
        return consumed;
      }
      if (!_state.keysHeld[event.key])
      {
        _state.keysHeld[event.key] = true;
        _state.keyEdges[event.key] = 1;
      }
      break;
    case InputEventKind::KeyUp:
      if (_state.keyEdges[event.key] != 0)
      {
        return consumed;
      }
      if (_state.keysHeld[event.key])
      {
        _state.keysHeld[event.key] = false;
        _state.keyEdges[event.key] = -1;
      }
      break;
    case InputEventKind::Character:
      // Consumed and changes nothing: a character is not a control, and it reaches the focused
      // sink through the router, in order with the key events either side of it.
      break;
    case InputEventKind::MouseButtonDown:
    {
      const std::size_t button = IndexOf(event.button);
      if (button >= MOUSE_BUTTON_COUNT)
      {
        break;
      }
      if (_state.buttonEdges[button] != 0)
      {
        return consumed;
      }
      if (!_state.buttonsHeld[button])
      {
        _state.buttonsHeld[button] = true;
        _state.buttonEdges[button] = 1;
      }
      break;
    }
    case InputEventKind::MouseButtonUp:
    {
      const std::size_t button = IndexOf(event.button);
      if (button >= MOUSE_BUTTON_COUNT)
      {
        break;
      }
      if (_state.buttonEdges[button] != 0)
      {
        return consumed;
      }
      if (_state.buttonsHeld[button])
      {
        _state.buttonsHeld[button] = false;
        _state.buttonEdges[button] = -1;
      }
      break;
    }
    case InputEventKind::MouseMove:
      // Coalesced by overwriting: the last position is where the mouse is, and the velocity is
      // measured against where it was when the frame began.
      _state.mouseX = event.x;
      _state.mouseY = event.y;
      break;
    case InputEventKind::MouseRawMove:
      // Summed, the opposite of MouseMove: two relative steps mean the mouse travelled both.
      _state.relativeX += event.x;
      _state.relativeY += event.y;
      _state.relativeSeen = true;
      break;
    case InputEventKind::Wheel:
    {
      // Truncation toward zero in both directions: a partial detent stays in the remainder rather
      // than rounding into a scroll the user did not make.
      _state.wheelRemainder += event.wheelDelta;
      const std::int32_t detents = _state.wheelRemainder / WHEEL_DELTA_PER_DETENT;
      if (detents != 0)
      {
        _state.wheelRemainder -= detents * WHEEL_DELTA_PER_DETENT;
        _state.wheelDetents += detents;
      }
      break;
    }
    case InputEventKind::FocusLost:
    {
      // Windows sends no release for anything held when focus goes, so the release edges are made
      // here, one per control, and a control that already edged this frame defers the whole event.
      for (std::size_t key = 0; key < KEY_COUNT; ++key)
      {
        if (_state.keysHeld[key] && _state.keyEdges[key] != 0)
        {
          return consumed;
        }
      }
      for (std::size_t button = 0; button < MOUSE_BUTTON_COUNT; ++button)
      {
        if (_state.buttonsHeld[button] && _state.buttonEdges[button] != 0)
        {
          return consumed;
        }
      }
      for (std::size_t key = 0; key < KEY_COUNT; ++key)
      {
        if (_state.keysHeld[key])
        {
          _state.keysHeld[key] = false;
          _state.keyEdges[key] = -1;
        }
      }
      for (std::size_t button = 0; button < MOUSE_BUTTON_COUNT; ++button)
      {
        if (_state.buttonsHeld[button])
        {
          _state.buttonsHeld[button] = false;
          _state.buttonEdges[button] = -1;
        }
      }
      // A partial scroll is owed to nobody after focus goes.
      _state.wheelRemainder = 0;
      break;
    }
    }
  }

  _state.mouseVelocityX = _state.mouseX - startX;
  _state.mouseVelocityY = _state.mouseY - startY;
  return consumed;
}

MouseTravel TravelOf(const FrameInput& _state) noexcept
{
  if (_state.relativeSeen)
  {
    return {_state.relativeX, _state.relativeY};
  }
  return {_state.mouseVelocityX, _state.mouseVelocityY};
}

} // namespace Neuron
