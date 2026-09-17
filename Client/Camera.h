#pragma once

#include "WindowsHeader.h"

#include <DirectXMath.h>

#include "HeightView.h"

#include <cstdint>

namespace Neuron
{

/// The Species camera in floats (SpeciesLook.md §7): a fly camera with a ground floor, not an RTS
/// camera with a fixed pitch. Yaw and pitch are free, height is what the wheel and the vertical
/// keys change, and the floor keeps it out of the terrain. Left-handed, y up, z forward.
inline constexpr float CAMERA_FIELD_OF_VIEW_DEGREES = 60.0f;
inline constexpr float CAMERA_NEAR = 5.0f;
inline constexpr float CAMERA_FAR = 15000.0f;
inline constexpr float CAMERA_MIN_CLEARANCE = 10.0f; ///< Above the ground under and around the camera, and above the water
inline constexpr float CAMERA_MAX_HEIGHT = 5000.0f;
inline constexpr float CAMERA_MAX_PITCH_DEGREES = 89.0f;

class Camera
{
public:
  void SetPosition(float _x, float _y, float _z) noexcept;
  /// Yaw about y from +z toward +x, pitch above the horizon; the pitch is clamped.
  void SetOrientation(float _yawRadians, float _pitchRadians) noexcept;
  /// Along the heading's ground projection, to its right, and up, in world units.
  void Move(float _forward, float _right, float _up) noexcept;
  /// Aims at a point from where the camera is.
  void LookAt(float _x, float _y, float _z) noexcept;
  /// The floor: at least CAMERA_MIN_CLEARANCE above the highest ground within one spacing of the
  /// camera and above the water, and at most CAMERA_MAX_HEIGHT.
  void ClampHeight(const HeightView& _view) noexcept;

  [[nodiscard]] DirectX::XMFLOAT3 Position() const noexcept
  {
    return m_position;
  }
  [[nodiscard]] float Yaw() const noexcept
  {
    return m_yaw;
  }
  [[nodiscard]] float Pitch() const noexcept
  {
    return m_pitch;
  }
  [[nodiscard]] DirectX::XMFLOAT3 Forward() const noexcept;
  [[nodiscard]] DirectX::XMMATRIX View() const noexcept;
  [[nodiscard]] DirectX::XMMATRIX Projection(float _aspect) const noexcept;

private:
  DirectX::XMFLOAT3 m_position{0.0f, 500.0f, 0.0f};
  float m_yaw = 0.0f;
  float m_pitch = -0.4636f; ///< The Species default: pitched 26.6 degrees down
};

/// The ground under a world position, from the four samples around it, bilinear; the plain's
/// height outside the field.
[[nodiscard]] float GroundHeightAt(const HeightView& _view, float _x, float _z) noexcept;

} // namespace Neuron
