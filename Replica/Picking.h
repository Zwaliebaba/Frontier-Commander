#pragma once

#include "ObjectId.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

// Turning a click into an object (Design/Interface.md §5; m1-vertical-slice/R2). Pure functions
// over a list of candidates and the camera's matrices, so that the executable holds no logic worth
// a test - which is this task's whole intent - and so that "a ray picks the nearer of two
// overlapping devices" is a case rather than something somebody checks by clicking.
//
// REPLICA HAS NO MATRIX TYPE AND MAY NOT HAVE ONE. It includes no Windows, Direct3D or WinRT header
// anywhere and Build/CheckProjectFiles.py refuses one (Replica/pch.h, ADR-001), so DirectXMath is
// out. Sixteen floats is the whole of what this needs, and the executable stores its XMMATRIX into
// them with XMStoreFloat4x4, which is where the row-major convention below comes from.
//
// THE UNPROJECTION IS HERE AND NOT IN THE EXECUTABLE. Turning a screen point into a world ray is
// exactly the kind of arithmetic that is wrong by a sign and right-looking on a symmetric camera;
// leaving it in the executable would put it where it cannot be tested, which is the thing this task
// exists to stop.
//
// WHAT MAY BE PICKED IS §5's RULE AND NOT THIS FILE'S OPINION. A click takes the nearest object of
// any seat - an enemy one is selected as an inspection - and the caller decides what to do with it.
// A rectangle takes DEVICES of one seat only: "a rectangle never selects structures and never
// selects another commander's anything". A wreck, a feature and a projectile are picked by nothing.
// A ray whose origin lands on a panel is refused before it is cast, and that is the caller's: the
// interface already knows where its panels are (Client/UiInputSink.h's PointerOverPanel).

namespace Frontier
{

/// A camera matrix as sixteen floats, ROW MAJOR, multiplied as a row vector times the matrix. That
/// is DirectXMath's convention - XMStoreFloat4x4 of View * Projection - and the one
/// Client/Shaders/GeometryVS.hlsl computes with mul(float4(world, 1), g_viewProjection).
struct CameraMatrix
{
  std::array<float, 16> m{};
};

/// The camera as picking reads it: the matrix that projects, its inverse, and the frame the screen
/// coordinates are in. The frame is passed rather than named because AUTHORED_WIDTH_PIXELS is
/// Client's (Client/ScaleMode.h) and Replica may not include it - and because a capture at another
/// size would otherwise pick at the wrong place with no symptom but a miss.
struct PickCamera
{
  CameraMatrix viewProjection;
  CameraMatrix inverseViewProjection;
  std::int32_t frameWidth = 0;
  std::int32_t frameHeight = 0;
};

/// A half-open screen rectangle, in the same authored pixels a click arrives in. Its own type
/// rather than Client's UiRect, which Replica may not include, and named for what it is so that
/// nobody wires the two together by accident.
struct PickBox
{
  std::int32_t left = 0;
  std::int32_t top = 0;
  std::int32_t right = 0;
  std::int32_t bottom = 0;

  [[nodiscard]] constexpr bool Empty() const noexcept
  {
    return right <= left || bottom <= top;
  }
};

/// One thing a click could land on: where it is, how big it is, whose it is and what it is. A
/// SPHERE AND NOT THE MODEL'S TRIANGLES, because a click is a gesture with a pixel of slop in it
/// and a per-triangle test would make a unit harder to select the more detailed its model became -
/// which is the wrong way round. The radius is the model's own furthest vertex, which
/// Client/ModelBuffers.h already computes for the same reason a cull would.
struct PickCandidate
{
  std::uint32_t id = 0;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float radius = 0.0f;
  std::uint8_t seat = 0;
  ObjectKind kind = ObjectKind::Device;
};

/// A ray in world units.
struct PickRay
{
  float originX = 0.0f;
  float originY = 0.0f;
  float originZ = 0.0f;
  float directionX = 0.0f;
  float directionY = 0.0f;
  float directionZ = 0.0f;
};

/// What a click found. `distance` is along the ray, from its origin, in world units.
struct PickResult
{
  std::uint32_t id = 0;
  float distance = 0.0f;
  bool hit = false;
};

/// _point times _matrix as a row vector, with w. The projection and the unprojection both need it
/// and neither owns it; free so that a test can check the convention against a matrix it wrote.
[[nodiscard]] std::array<float, 4> Transformed(const CameraMatrix& _matrix, float _x, float _y, float _z, float _w) noexcept;

/// The world ray through an authored screen point.
///
/// THE DEPTH IS REVERSED (ADR-005): the target is cleared to 0 and the near plane is at 1, so the
/// near point is at normalized depth 1 and the far point at 0. Taking them the other way round
/// gives a ray pointing backwards out of the camera, which picks nothing and looks like a dead
/// mouse button.
[[nodiscard]] PickRay RayThrough(const PickCamera& _camera, std::int32_t _screenX, std::int32_t _screenY) noexcept;

/// The nearest candidate the ray enters, of any seat. A wreck, a feature and a projectile are
/// skipped: §5 says what may be selected and none of those is in it.
[[nodiscard]] PickResult NearestUnderRay(std::span<const PickCandidate> _candidates, const PickRay& _ray) noexcept;

/// Every DEVICE of _seat whose centre projects inside _box, appended to _outIds in the order the
/// candidates were given - which is id order, because the replica holds them in a map. Behind the
/// camera is outside the box: a device the camera has passed projects to a point that is on screen
/// by arithmetic and behind the player by geometry.
void InsideRectangle(std::span<const PickCandidate> _candidates, const PickCamera& _camera, const PickBox& _box, std::uint8_t _seat,
                     std::vector<std::uint32_t>& _outIds);

} // namespace Frontier
