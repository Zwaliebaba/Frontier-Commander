#include "pch.h"

#include "Camera.h"

#include <algorithm>
#include <cmath>

namespace Neuron
{

namespace
{

constexpr float PI = 3.14159265358979323846f;

[[nodiscard]] float Radians(float _degrees) noexcept
{
  return _degrees * PI / 180.0f;
}

[[nodiscard]] float SampleOrPlain(const HeightView& _view, std::int64_t _x, std::int64_t _y) noexcept
{
  const std::int64_t side = _view.samplesPerSide;
  if (_x < 0 || _y < 0 || _x >= side || _y >= side)
  {
    return static_cast<float>(_view.waterLevel) - 26.0f;
  }
  return static_cast<float>(_view.samples[static_cast<std::size_t>(_y * side + _x)]);
}

} // namespace

void Camera::SetPosition(float _x, float _y, float _z) noexcept
{
  m_position = {_x, _y, _z};
}

void Camera::SetOrientation(float _yawRadians, float _pitchRadians) noexcept
{
  m_yaw = std::remainder(_yawRadians, 2.0f * PI);
  m_pitch = std::clamp(_pitchRadians, -Radians(CAMERA_MAX_PITCH_DEGREES), Radians(CAMERA_MAX_PITCH_DEGREES));
}

void Camera::Move(float _forward, float _right, float _up) noexcept
{
  const float sinYaw = std::sin(m_yaw);
  const float cosYaw = std::cos(m_yaw);
  m_position.x += sinYaw * _forward + cosYaw * _right;
  m_position.z += cosYaw * _forward - sinYaw * _right;
  m_position.y += _up;
}

void Camera::LookAt(float _x, float _y, float _z) noexcept
{
  const float dx = _x - m_position.x;
  const float dy = _y - m_position.y;
  const float dz = _z - m_position.z;
  const float flat = std::sqrt(dx * dx + dz * dz);
  SetOrientation(std::atan2(dx, dz), std::atan2(dy, flat));
}

void Camera::ClampHeight(const HeightView& _view) noexcept
{
  const float spacing = static_cast<float>(_view.spacingWorldUnits);
  float ground = static_cast<float>(_view.waterLevel);
  for (const float offsetX : {-spacing, 0.0f, spacing})
  {
    for (const float offsetZ : {-spacing, 0.0f, spacing})
    {
      ground = std::max(ground, GroundHeightAt(_view, m_position.x + offsetX, m_position.z + offsetZ));
    }
  }
  m_position.y = std::clamp(m_position.y, ground + CAMERA_MIN_CLEARANCE, CAMERA_MAX_HEIGHT);
}

DirectX::XMFLOAT3 Camera::Forward() const noexcept
{
  const float cosPitch = std::cos(m_pitch);
  return {std::sin(m_yaw) * cosPitch, std::sin(m_pitch), std::cos(m_yaw) * cosPitch};
}

DirectX::XMMATRIX Camera::View() const noexcept
{
  const DirectX::XMFLOAT3 forward = Forward();
  return DirectX::XMMatrixLookToLH(DirectX::XMLoadFloat3(&m_position), DirectX::XMLoadFloat3(&forward),
                                   DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
}

DirectX::XMMATRIX Camera::Projection(float _aspect) const noexcept
{
  return DirectX::XMMatrixPerspectiveFovLH(Radians(CAMERA_FIELD_OF_VIEW_DEGREES), _aspect, CAMERA_NEAR, CAMERA_FAR);
}

float GroundHeightAt(const HeightView& _view, float _x, float _z) noexcept
{
  const float spacing = static_cast<float>(_view.spacingWorldUnits);
  const float sampleX = _x / spacing;
  const float sampleZ = _z / spacing;
  const float floorX = std::floor(sampleX);
  const float floorZ = std::floor(sampleZ);
  const auto x0 = static_cast<std::int64_t>(floorX);
  const auto z0 = static_cast<std::int64_t>(floorZ);
  const float tx = sampleX - floorX;
  const float tz = sampleZ - floorZ;
  const float h00 = SampleOrPlain(_view, x0, z0);
  const float h10 = SampleOrPlain(_view, x0 + 1, z0);
  const float h01 = SampleOrPlain(_view, x0, z0 + 1);
  const float h11 = SampleOrPlain(_view, x0 + 1, z0 + 1);
  const float top = h00 + (h10 - h00) * tx;
  const float bottom = h01 + (h11 - h01) * tx;
  return top + (bottom - top) * tz;
}

} // namespace Neuron
