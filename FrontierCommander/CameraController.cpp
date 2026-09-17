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
constexpr float TURN_RATE = 1.5f;
constexpr float AIM_RADIANS_PER_COUNT = 0.003f;
constexpr float WHEEL_HEIGHT_PER_DETENT = 50.0f;
constexpr std::uint32_t EDGE_PIXELS = 8;

// Virtual-key codes; the letters are their ASCII values.
constexpr std::uint8_t KEY_SHIFT = 0x10;
constexpr std::uint8_t KEY_PAGE_UP = 0x21;
constexpr std::uint8_t KEY_PAGE_DOWN = 0x22;
constexpr std::uint8_t KEY_LEFT = 0x25;
constexpr std::uint8_t KEY_UP = 0x26;
constexpr std::uint8_t KEY_RIGHT = 0x27;
constexpr std::uint8_t KEY_DOWN = 0x28;
constexpr std::uint8_t KEY_A = 'A';
constexpr std::uint8_t KEY_D = 'D';
constexpr std::uint8_t KEY_E = 'E';
constexpr std::uint8_t KEY_F = 'F';
constexpr std::uint8_t KEY_Q = 'Q';
constexpr std::uint8_t KEY_R = 'R';
constexpr std::uint8_t KEY_S = 'S';
constexpr std::uint8_t KEY_W = 'W';

[[nodiscard]] float Axis(const Neuron::FrameInput& _input, std::uint8_t _positive, std::uint8_t _negative) noexcept
{
  return (_input.keysHeld[_positive] ? 1.0f : 0.0f) - (_input.keysHeld[_negative] ? 1.0f : 0.0f);
}

} // namespace

void CameraController::Advance(Neuron::Camera& _camera, const Neuron::FrameInput& _input, const Neuron::HeightView& _terrain,
                               float _seconds, std::uint32_t _clientWidth, std::uint32_t _clientHeight) const
{
  float forward = Axis(_input, KEY_W, KEY_S) + Axis(_input, KEY_UP, KEY_DOWN);
  float right = Axis(_input, KEY_D, KEY_A) + Axis(_input, KEY_RIGHT, KEY_LEFT);
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

  float yaw = _camera.Yaw() + Axis(_input, KEY_E, KEY_Q) * TURN_RATE * _seconds;
  float pitch = _camera.Pitch() + Axis(_input, KEY_R, KEY_F) * TURN_RATE * _seconds;
  if (_input.buttonsHeld[Neuron::IndexOf(Neuron::MouseButton::Right)])
  {
    const Neuron::MouseTravel travel = Neuron::TravelOf(_input);
    yaw += static_cast<float>(travel.x) * AIM_RADIANS_PER_COUNT;
    pitch -= static_cast<float>(travel.y) * AIM_RADIANS_PER_COUNT;
  }
  _camera.SetOrientation(yaw, pitch);
  _camera.ClampHeight(_terrain);
}

} // namespace Frontier
