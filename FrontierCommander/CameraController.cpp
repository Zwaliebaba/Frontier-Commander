#include "pch.h"

#include "CameraController.h"

#include <cstdint>

namespace Frontier
{

namespace
{

// The Species free camera's rates (SpeciesLook.md §7): 250 units a second, four times that with
// the speed-up key.
constexpr float MOVE_RATE = 250.0f;
constexpr float FAST_MULTIPLIER = 4.0f;
constexpr float AIM_RADIANS_PER_COUNT = 0.003f;
constexpr float WHEEL_HEIGHT_PER_DETENT = 50.0f;
constexpr std::uint32_t EDGE_PIXELS = 8;

// Virtual-key codes. NO LETTERS: Design/Interface.md §7's hotkey table gives the letters to orders,
// and two of the camera's collided with it - S was Stop as well as backward, R was Attack-move as
// well as pitch down. The owner ruled on 2026-09-19 that the orders win, because §7 is the document
// that specifies the game's controls and the camera's letters were M0 placeholders from
// SpeciesLook.md §7 that predate any order existing. Nothing is lost but a second way to do the
// same thing: the camera still has the arrow keys, the screen's edges, the wheel, PageUp and
// PageDown, and the middle button's drag. Turning it by key goes with them, which the drag covers.
constexpr std::uint8_t KEY_SHIFT = 0x10;
constexpr std::uint8_t KEY_PAGE_UP = 0x21;
constexpr std::uint8_t KEY_PAGE_DOWN = 0x22;
constexpr std::uint8_t KEY_LEFT = 0x25;
constexpr std::uint8_t KEY_UP = 0x26;
constexpr std::uint8_t KEY_RIGHT = 0x27;
constexpr std::uint8_t KEY_DOWN = 0x28;

[[nodiscard]] float Axis(const Neuron::FrameInput& _input, std::uint8_t _positive, std::uint8_t _negative) noexcept
{
  return (_input.keysHeld[_positive] ? 1.0f : 0.0f) - (_input.keysHeld[_negative] ? 1.0f : 0.0f);
}

} // namespace

void CameraController::Advance(Neuron::Camera& _camera, const Neuron::FrameInput& _input, const Neuron::HeightView& _terrain,
                               float _seconds, std::uint32_t _clientWidth, std::uint32_t _clientHeight) const
{
  float forward = Axis(_input, KEY_UP, KEY_DOWN);
  float right = Axis(_input, KEY_RIGHT, KEY_LEFT);
  // The screen's edges, when the mouse is inside the client area at all.
  if (_clientWidth > 0 && _clientHeight > 0 && _input.mouseX >= 0 && _input.mouseY >= 0 &&
      static_cast<std::uint32_t>(_input.mouseX) < _clientWidth && static_cast<std::uint32_t>(_input.mouseY) < _clientHeight)
  {
    if (static_cast<std::uint32_t>(_input.mouseX) < EDGE_PIXELS)
    {
      right -= 1.0f;
    }
    if (static_cast<std::uint32_t>(_input.mouseX) >= _clientWidth - EDGE_PIXELS)
    {
      right += 1.0f;
    }
    if (static_cast<std::uint32_t>(_input.mouseY) < EDGE_PIXELS)
    {
      forward += 1.0f;
    }
    if (static_cast<std::uint32_t>(_input.mouseY) >= _clientHeight - EDGE_PIXELS)
    {
      forward -= 1.0f;
    }
  }
  const float rate = MOVE_RATE * (_input.keysHeld[KEY_SHIFT] ? FAST_MULTIPLIER : 1.0f) * _seconds;
  const float up = (Axis(_input, KEY_PAGE_UP, KEY_PAGE_DOWN) * rate) + static_cast<float>(_input.wheelDetents) * WHEEL_HEIGHT_PER_DETENT;
  _camera.Move(forward * rate, right * rate, up);

  float yaw = _camera.Yaw();
  float pitch = _camera.Pitch();
  // THE MIDDLE BUTTON AIMS AND THE RIGHT BUTTON GIVES ORDERS (Design/Interface.md §12 ruling 3).
  // M0 bound aiming to the right button because no orders existed yet. The alternative the document
  // considered and refused was a drag threshold on the right button, which delays every order by
  // however long the threshold takes to fail.
  if (_input.buttonsHeld[Neuron::IndexOf(Neuron::MouseButton::Middle)])
  {
    const Neuron::MouseTravel travel = Neuron::TravelOf(_input);
    yaw += static_cast<float>(travel.x) * AIM_RADIANS_PER_COUNT;
    pitch -= static_cast<float>(travel.y) * AIM_RADIANS_PER_COUNT;
  }
  _camera.SetOrientation(yaw, pitch);
  _camera.ClampHeight(_terrain);
}

} // namespace Frontier
