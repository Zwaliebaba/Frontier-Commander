#include "pch.h"

#include "Picking.h"

#include <cmath>

namespace Frontier
{

namespace
{

/// Whether §5 lets this kind be picked at all. A wreck "blocks nothing" and is drawn for a while
/// (GameDesign.md §7); a projectile is in flight; a feature is part of the landscape.
[[nodiscard]] bool Selectable(ObjectKind _kind) noexcept
{
  return _kind == ObjectKind::Device || _kind == ObjectKind::Structure;
}

/// The nearer of the two points where a ray meets a sphere, or a negative number for a miss. The
/// ray's direction is normalized by the caller, so the quadratic's leading term is one and this is
/// the usual reduction of it.
///
/// A RAY THAT STARTS INSIDE THE SPHERE HITS AT ZERO rather than missing. The camera can be close
/// enough to a structure to be inside its radius, and a click that did nothing there would look
/// like a broken mouse rather than like a near miss.
[[nodiscard]] float RaySphereDistance(const PickRay& _ray, float _x, float _y, float _z, float _radius) noexcept
{
  const float toCenterX = _x - _ray.originX;
  const float toCenterY = _y - _ray.originY;
  const float toCenterZ = _z - _ray.originZ;
  const float along = toCenterX * _ray.directionX + toCenterY * _ray.directionY + toCenterZ * _ray.directionZ;
  const float centerSquared = toCenterX * toCenterX + toCenterY * toCenterY + toCenterZ * toCenterZ;
  const float radiusSquared = _radius * _radius;
  if (centerSquared <= radiusSquared)
  {
    return 0.0f; // The origin is inside it.
  }
  if (along <= 0.0f)
  {
    // Wholly behind the ray. AN EARLY-OUT AND NOT THE GUARD: with `along` negative the distance
    // below comes out negative too, and NearestUnderRay drops a negative one, so removing this
    // changes no answer - which mutation testing showed by removing it and failing nothing. It
    // stays because it saves a square root and says what the case is; it is not load-bearing.
    return -1.0f;
  }
  const float perpendicularSquared = centerSquared - along * along;
  if (perpendicularSquared > radiusSquared)
  {
    return -1.0f;
  }
  return along - std::sqrt(radiusSquared - perpendicularSquared);
}

} // namespace

std::array<float, 4> Transformed(const CameraMatrix& _matrix, float _x, float _y, float _z, float _w) noexcept
{
  const std::array<float, 16>& m = _matrix.m;
  return {_x * m[0] + _y * m[4] + _z * m[8] + _w * m[12], _x * m[1] + _y * m[5] + _z * m[9] + _w * m[13],
          _x * m[2] + _y * m[6] + _z * m[10] + _w * m[14], _x * m[3] + _y * m[7] + _z * m[11] + _w * m[15]};
}

PickRay RayThrough(const PickCamera& _camera, std::int32_t _screenX, std::int32_t _screenY) noexcept
{
  PickRay ray{};
  if (_camera.frameWidth <= 0 || _camera.frameHeight <= 0)
  {
    return ray;
  }
  // The pixel's CENTRE, not its corner: a click on pixel 0 is at half a pixel across, which is
  // where the rasterizer samples it and therefore where what was drawn there actually is.
  const float normalizedX = (static_cast<float>(_screenX) + 0.5f) / static_cast<float>(_camera.frameWidth) * 2.0f - 1.0f;
  const float normalizedY = 1.0f - (static_cast<float>(_screenY) + 0.5f) / static_cast<float>(_camera.frameHeight) * 2.0f;

  // NOT `near` AND `far`: both are macros of the Windows SDK's minwindef.h, and an identifier
  // spelled that way loses its name wherever <windows.h> is in scope - which for this file is every
  // test suite that includes it. AGENTS.md §3 records the day it happened and CheckProjectFiles
  // refuses the family; it refused these two before this comment was written.
  const std::array<float, 4> atNear = Transformed(_camera.inverseViewProjection, normalizedX, normalizedY, 1.0f, 1.0f);
  const std::array<float, 4> atFar = Transformed(_camera.inverseViewProjection, normalizedX, normalizedY, 0.0f, 1.0f);
  if (atNear[3] == 0.0f || atFar[3] == 0.0f)
  {
    return ray;
  }
  const float nearX = atNear[0] / atNear[3];
  const float nearY = atNear[1] / atNear[3];
  const float nearZ = atNear[2] / atNear[3];
  float directionX = atFar[0] / atFar[3] - nearX;
  float directionY = atFar[1] / atFar[3] - nearY;
  float directionZ = atFar[2] / atFar[3] - nearZ;
  const float length = std::sqrt(directionX * directionX + directionY * directionY + directionZ * directionZ);
  if (length <= 0.0f)
  {
    return ray;
  }
  directionX /= length;
  directionY /= length;
  directionZ /= length;
  return PickRay{nearX, nearY, nearZ, directionX, directionY, directionZ};
}

PickResult NearestUnderRay(std::span<const PickCandidate> _candidates, const PickRay& _ray) noexcept
{
  PickResult result{};
  for (const PickCandidate& candidate : _candidates)
  {
    if (!Selectable(candidate.kind) || candidate.radius <= 0.0f)
    {
      continue;
    }
    const float distance = RaySphereDistance(_ray, candidate.x, candidate.y, candidate.z, candidate.radius);
    if (distance < 0.0f)
    {
      continue;
    }
    // STRICTLY NEARER, so that two candidates at the same distance resolve to the first the caller
    // listed rather than to the last. The replica holds its objects in id order, so "the first" is
    // the lower id and the answer is the same on every machine - which matters because a capture
    // compares frames and a tie broken by iteration order would differ between runs.
    if (!result.hit || distance < result.distance)
    {
      result.hit = true;
      result.id = candidate.id;
      result.distance = distance;
    }
  }
  return result;
}

void InsideRectangle(std::span<const PickCandidate> _candidates, const PickCamera& _camera, const PickBox& _box, std::uint8_t _seat,
                     std::vector<std::uint32_t>& _outIds)
{
  if (_box.Empty() || _camera.frameWidth <= 0 || _camera.frameHeight <= 0)
  {
    return;
  }
  for (const PickCandidate& candidate : _candidates)
  {
    // §5: "A rectangle never selects structures and never selects another commander's anything."
    if (candidate.kind != ObjectKind::Device || candidate.seat != _seat)
    {
      continue;
    }
    const std::array<float, 4> clip = Transformed(_camera.viewProjection, candidate.x, candidate.y, candidate.z, 1.0f);
    if (clip[3] <= 0.0f)
    {
      continue; // Behind the camera, where the divide would fold it back onto the screen.
    }
    const float normalizedX = clip[0] / clip[3];
    const float normalizedY = clip[1] / clip[3];
    const float screenX = (normalizedX * 0.5f + 0.5f) * static_cast<float>(_camera.frameWidth);
    const float screenY = (0.5f - normalizedY * 0.5f) * static_cast<float>(_camera.frameHeight);
    const auto pixelX = static_cast<std::int32_t>(std::floor(screenX));
    const auto pixelY = static_cast<std::int32_t>(std::floor(screenY));
    if (pixelX < _box.left || pixelX >= _box.right || pixelY < _box.top || pixelY >= _box.bottom)
    {
      continue; // Half open, as every rectangle in this tree is.
    }
    _outIds.push_back(candidate.id);
  }
}

} // namespace Frontier
