#include "pch.h"

#include "Picking.h"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// Turning a click into an object (Design/Interface.md §5; m1-vertical-slice/R2). The two cases that
// task names - "a ray picks the nearer of two overlapping devices" and "a rectangle selects only
// own devices" - are here by name, with the arithmetic around them pinned so that a sign the wrong
// way round fails rather than picks the thing behind the camera.
namespace ReplicaTests
{

namespace
{

/// A camera looking down -z from (0, 0, 100) at the origin, with an orthographic projection two
/// hundred world units wide and tall and a REVERSED depth (ADR-005: near at 1, far at 0). Written
/// out as sixteen floats rather than built with a matrix library, because Replica has none and
/// because a fixture a reader can check by eye is worth more here than one that is convenient.
///
/// Row major, row vector times matrix. x and y pass through at a fiftieth; z maps [0, 200] of
/// distance in front of the camera onto depth [1, 0].
[[nodiscard]] Frontier::PickCamera OrthographicCamera()
{
  Frontier::PickCamera camera{};
  camera.frameWidth = 200;
  camera.frameHeight = 200;
  // world (x, y, z) -> clip (x/100, y/100, z/200 + 1/2), w = 1. The camera stands at z = 100 and looks toward -z,
  // and the depth is REVERSED (ADR-005), so the near plane at z = 100 maps to 1 and the far plane
  // at z = -100 to 0. Written this way round on purpose: the first draft of this fixture had the z
  // row negated, which is self-consistent between the matrix and its inverse and describes a camera
  // looking the other way - four cases failed and not one of them said so.
  camera.viewProjection.m = {0.01f, 0.0f,  0.0f,   0.0f, //
                             0.0f,  0.01f, 0.0f,   0.0f, //
                             0.0f,  0.0f,  0.005f, 0.0f, //
                             0.0f,  0.0f,  0.5f,   1.0f};
  // And its inverse: clip (a, b, c) -> world (100a, 100b, 200c - 100), so depth 1 is z = 100.
  camera.inverseViewProjection.m = {100.0f, 0.0f,   0.0f,    0.0f, //
                                    0.0f,   100.0f, 0.0f,    0.0f, //
                                    0.0f,   0.0f,   200.0f,  0.0f, //
                                    0.0f,   0.0f,   -100.0f, 1.0f};
  return camera;
}

[[nodiscard]] Frontier::PickCandidate Device(std::uint32_t _id, std::uint8_t _seat, float _x, float _y, float _z, float _radius = 5.0f)
{
  Frontier::PickCandidate candidate{};
  candidate.id = _id;
  candidate.seat = _seat;
  candidate.x = _x;
  candidate.y = _y;
  candidate.z = _z;
  candidate.radius = _radius;
  candidate.kind = Frontier::ObjectKind::Device;
  return candidate;
}

void AssertNear(float _expected, float _actual, const wchar_t* _message)
{
  Assert::IsTrue(std::fabs(_expected - _actual) < 0.01f, _message);
}

} // namespace

TEST_CLASS(PickingTests)
{
public:
  /// The convention, pinned against a matrix written by hand: row vector times a row-major matrix,
  /// which is what XMStoreFloat4x4 gives and what GeometryVS.hlsl's mul(float4(world, 1), m)
  /// computes. Transposed by mistake, every one of the cases below would still pass on a symmetric
  /// camera and fail on a real one, which is why this is a case of its own.
  TEST_METHOD(APointIsARowVectorTimesARowMajorMatrix)
  {
    Frontier::CameraMatrix translate{};
    translate.m = {1.0f, 0.0f, 0.0f, 0.0f, //
                   0.0f, 1.0f, 0.0f, 0.0f, //
                   0.0f, 0.0f, 1.0f, 0.0f, //
                   7.0f, 8.0f, 9.0f, 1.0f};
    const std::array<float, 4> moved = Frontier::Transformed(translate, 1.0f, 2.0f, 3.0f, 1.0f);
    AssertNear(8.0f, moved[0], L"the translation is in the LAST ROW, which is the row-vector convention");
    AssertNear(10.0f, moved[1], L"transposed, this would be 2");
    AssertNear(12.0f, moved[2], L"");
    AssertNear(1.0f, moved[3], L"");

    const std::array<float, 4> direction = Frontier::Transformed(translate, 1.0f, 2.0f, 3.0f, 0.0f);
    AssertNear(1.0f, direction[0], L"a direction has w of zero and does not translate");
  }

  /// THE DEPTH IS REVERSED (ADR-005). The near point is at normalized depth 1 and the far point at
  /// 0; taking them the other way round gives a ray pointing backwards out of the camera, which
  /// picks nothing at all and reads as a dead mouse button rather than as a sign error.
  TEST_METHOD(ARayThroughTheMiddleGoesForwardAndNotBackwards)
  {
    const Frontier::PickCamera camera = OrthographicCamera();
    const Frontier::PickRay ray = Frontier::RayThrough(camera, 100, 100);
    AssertNear(0.0f, ray.directionX, L"straight down the middle");
    AssertNear(0.0f, ray.directionY, L"");
    AssertNear(-1.0f, ray.directionZ, L"AWAY from the camera at z = 100, which is -z");
    AssertNear(100.0f, ray.originZ, L"and it starts at the near plane");
    AssertNear(1.0f, std::sqrt(ray.directionX * ray.directionX + ray.directionY * ray.directionY + ray.directionZ * ray.directionZ),
               L"normalized, because the distance it reports is in world units");
  }

  /// A click on pixel 0 is half a pixel across - where the rasterizer sampled what was drawn there.
  TEST_METHOD(ARayIsCastThroughThePixelsCenterAndNotItsCorner)
  {
    const Frontier::PickCamera camera = OrthographicCamera();
    const Frontier::PickRay ray = Frontier::RayThrough(camera, 0, 0);
    AssertNear(-99.5f, ray.originX, L"half a pixel in from the left edge at -100");
    AssertNear(99.5f, ray.originY, L"and half a pixel down from the top at +100");
  }

  TEST_METHOD(ARayFromAFrameOfNoSizeIsNotCast)
  {
    Frontier::PickCamera camera = OrthographicCamera();
    camera.frameWidth = 0;
    const Frontier::PickRay ray = Frontier::RayThrough(camera, 10, 10);
    AssertNear(0.0f, ray.directionZ, L"no frame, no ray");
  }

  /// R2's own acceptance line: a ray picks the NEARER of two overlapping devices. Both are under
  /// the same pixel and their spheres overlap; the answer is the one the camera reaches first.
  TEST_METHOD(ARayPicksTheNearerOfTwoOverlappingDevices)
  {
    const Frontier::PickCamera camera = OrthographicCamera();
    const Frontier::PickRay ray = Frontier::RayThrough(camera, 100, 100);
    const std::vector<Frontier::PickCandidate> candidates = {
      Device(1, 0, 0.0f, 0.0f, -10.0f, 20.0f), // further from the camera at z = 100
      Device(2, 0, 0.0f, 0.0f, 20.0f, 20.0f),  // nearer
    };
    const Frontier::PickResult result = Frontier::NearestUnderRay(candidates, ray);
    Assert::IsTrue(result.hit);
    Assert::AreEqual(std::uint32_t{2}, result.id, L"the nearer one, listed second");
    // 80 along the ray to the sphere's centre, less sqrt(400 - 1/4 - 1/4) to its surface: the ray
    // goes through the PIXEL'S CENTRE, which on a 200-wide frame is half a pixel off each axis, so
    // it enters the sphere a hundredth further out than a ray straight down the middle would.
    AssertNear(60.0125f, result.distance, L"from the near plane at z = 100 to the sphere's front");

    // And the same two in the other order, so that the answer is the geometry's and not the list's.
    const std::vector<Frontier::PickCandidate> reversed = {candidates[1], candidates[0]};
    Assert::AreEqual(std::uint32_t{2}, Frontier::NearestUnderRay(reversed, ray).id);
  }

  /// A tie goes to the FIRST candidate listed. The replica holds its objects in id order, so the
  /// answer is the lower id and is the same on every machine - which a capture compared frame by
  /// frame depends on.
  TEST_METHOD(TwoAtTheSameDistanceResolveToTheLowerId)
  {
    const Frontier::PickCamera camera = OrthographicCamera();
    const Frontier::PickRay ray = Frontier::RayThrough(camera, 100, 100);
    const std::vector<Frontier::PickCandidate> candidates = {Device(3, 0, 0.0f, 0.0f, 0.0f, 10.0f), Device(9, 0, 0.0f, 0.0f, 0.0f, 10.0f)};
    Assert::AreEqual(std::uint32_t{3}, Frontier::NearestUnderRay(candidates, ray).id);
  }

  TEST_METHOD(ARayThatMissesEverythingHitsNothing)
  {
    const Frontier::PickCamera camera = OrthographicCamera();
    const Frontier::PickRay ray = Frontier::RayThrough(camera, 100, 100);
    const std::vector<Frontier::PickCandidate> candidates = {Device(1, 0, 60.0f, 0.0f, 0.0f, 5.0f)};
    const Frontier::PickResult result = Frontier::NearestUnderRay(candidates, ray);
    Assert::IsFalse(result.hit);
    Assert::AreEqual(std::uint32_t{0}, result.id, L"and it names nothing rather than the last thing it looked at");
  }

  /// Behind the ray's origin is a miss, not a hit at a negative distance.
  TEST_METHOD(SomethingBehindTheCameraIsNotPicked)
  {
    const Frontier::PickCamera camera = OrthographicCamera();
    const Frontier::PickRay ray = Frontier::RayThrough(camera, 100, 100);
    const std::vector<Frontier::PickCandidate> candidates = {Device(1, 0, 0.0f, 0.0f, 500.0f, 10.0f)};
    Assert::IsFalse(Frontier::NearestUnderRay(candidates, ray).hit, L"500 is behind a camera at 100 looking at -z");
  }

  /// The camera can get close enough to a structure to be inside its radius, and a click that did
  /// nothing there would look like a broken mouse rather than like a near miss.
  TEST_METHOD(ARayThatStartsInsideSomethingHitsItAtZero)
  {
    const Frontier::PickCamera camera = OrthographicCamera();
    const Frontier::PickRay ray = Frontier::RayThrough(camera, 100, 100);
    const std::vector<Frontier::PickCandidate> candidates = {Device(1, 0, 0.0f, 0.0f, 100.0f, 50.0f)};
    const Frontier::PickResult result = Frontier::NearestUnderRay(candidates, ray);
    Assert::IsTrue(result.hit);
    AssertNear(0.0f, result.distance, L"");
  }

  /// §5 says what may be selected, and a wreck, a feature and a projectile are not in it.
  TEST_METHOD(AWreckAFeatureAndAProjectileArePickedByNothing)
  {
    const Frontier::PickCamera camera = OrthographicCamera();
    const Frontier::PickRay ray = Frontier::RayThrough(camera, 100, 100);
    for (const Frontier::ObjectKind kind : {Frontier::ObjectKind::Wreck, Frontier::ObjectKind::Feature, Frontier::ObjectKind::Projectile})
    {
      Frontier::PickCandidate candidate = Device(1, 0, 0.0f, 0.0f, 0.0f, 20.0f);
      candidate.kind = kind;
      Assert::IsFalse(Frontier::NearestUnderRay(std::vector{candidate}, ray).hit);
    }
    Frontier::PickCandidate structure = Device(1, 0, 0.0f, 0.0f, 0.0f, 20.0f);
    structure.kind = Frontier::ObjectKind::Structure;
    Assert::IsTrue(Frontier::NearestUnderRay(std::vector{structure}, ray).hit, L"but a structure is, by click");
  }

  /// R2's other acceptance line, and §5's rule in full: "a rectangle never selects structures and
  /// never selects another commander's anything".
  TEST_METHOD(ARectangleSelectsOnlyOwnDevices)
  {
    const Frontier::PickCamera camera = OrthographicCamera();
    Frontier::PickCandidate enemyStructure = Device(4, 1, 10.0f, 0.0f, 0.0f);
    enemyStructure.kind = Frontier::ObjectKind::Structure;
    Frontier::PickCandidate ownStructure = Device(5, 0, -10.0f, 0.0f, 0.0f);
    ownStructure.kind = Frontier::ObjectKind::Structure;
    const std::vector<Frontier::PickCandidate> candidates = {
      Device(1, 0, 0.0f, 0.0f, 0.0f),  // own device, inside
      Device(2, 1, 5.0f, 0.0f, 0.0f),  // another commander's device, inside
      Device(3, 0, 90.0f, 0.0f, 0.0f), // own device, outside the box
      enemyStructure,
      ownStructure,
    };
    std::vector<std::uint32_t> selected;
    Frontier::InsideRectangle(candidates, camera, Frontier::PickBox{50, 50, 150, 150}, 0, selected);
    Assert::AreEqual(std::size_t{1}, selected.size(), L"one own device, and neither structure nor the enemy's");
    Assert::AreEqual(std::uint32_t{1}, selected[0]);
  }

  /// Half open, as every rectangle in this tree is: the world origin projects to pixel 100, so a box
  /// whose right edge is 100 does not contain it and one whose right edge is 101 does.
  TEST_METHOD(ARectangleIsHalfOpenLikeEveryOtherOne)
  {
    const Frontier::PickCamera camera = OrthographicCamera();
    const std::vector<Frontier::PickCandidate> candidates = {Device(1, 0, 0.0f, 0.0f, 0.0f)};
    std::vector<std::uint32_t> selected;
    Frontier::InsideRectangle(candidates, camera, Frontier::PickBox{0, 0, 100, 100}, 0, selected);
    Assert::IsTrue(selected.empty(), L"the right and bottom edges are outside");
    Frontier::InsideRectangle(candidates, camera, Frontier::PickBox{100, 100, 101, 101}, 0, selected);
    Assert::AreEqual(std::size_t{1}, selected.size(), L"and the left and top edges are inside");
  }

  /// EACH EDGE ON ITS OWN. The case above moves both axes at once, so a box that had lost its
  /// right-edge test entirely would still exclude the device by its bottom edge and nothing would
  /// say so - which is exactly what mutation testing found. One device, four boxes, each tight on
  /// one side and generous on the other three.
  TEST_METHOD(EveryEdgeOfARectangleIsTestedOnItsOwn)
  {
    const Frontier::PickCamera camera = OrthographicCamera();
    const std::vector<Frontier::PickCandidate> candidates = {Device(1, 0, 0.0f, 0.0f, 0.0f)}; // pixel (100, 100)
    const auto selects = [&](const Frontier::PickBox& _box)
    {
      std::vector<std::uint32_t> selected;
      Frontier::InsideRectangle(candidates, camera, _box, 0, selected);
      return !selected.empty();
    };
    Assert::IsTrue(selects(Frontier::PickBox{0, 0, 200, 200}), L"well inside");
    Assert::IsFalse(selects(Frontier::PickBox{101, 0, 200, 200}), L"the left edge alone excludes it");
    Assert::IsFalse(selects(Frontier::PickBox{0, 0, 100, 200}), L"the right edge alone, and it is half open");
    Assert::IsFalse(selects(Frontier::PickBox{0, 101, 200, 200}), L"the top edge alone");
    Assert::IsFalse(selects(Frontier::PickBox{0, 0, 200, 100}), L"the bottom edge alone, and it is half open");
    Assert::IsTrue(selects(Frontier::PickBox{100, 100, 101, 101}), L"and the left and top edges are inside");
  }

  TEST_METHOD(AnEmptyRectangleSelectsNothing)
  {
    const Frontier::PickCamera camera = OrthographicCamera();
    const std::vector<Frontier::PickCandidate> candidates = {Device(1, 0, 0.0f, 0.0f, 0.0f)};
    std::vector<std::uint32_t> selected;
    Frontier::InsideRectangle(candidates, camera, Frontier::PickBox{50, 50, 50, 150}, 0, selected);
    Frontier::InsideRectangle(candidates, camera, Frontier::PickBox{150, 50, 50, 150}, 0, selected);
    Assert::IsTrue(selected.empty(), L"a drag of no width, and one dragged backwards");
  }

  /// A device the camera has passed projects to a point that is on screen by arithmetic and behind
  /// the player by geometry. Dividing by a negative w folds it back into the frame, so the sign is
  /// tested before the divide rather than after.
  TEST_METHOD(ADeviceBehindTheCameraIsOutsideEveryRectangle)
  {
    Frontier::PickCamera camera = OrthographicCamera();
    // A perspective-shaped matrix, so that w carries the depth and can go negative.
    camera.viewProjection.m = {1.0f, 0.0f, 0.0f, 0.0f,  //
                               0.0f, 1.0f, 0.0f, 0.0f,  //
                               0.0f, 0.0f, 1.0f, -1.0f, //
                               0.0f, 0.0f, 0.0f, 0.0f};
    const std::vector<Frontier::PickCandidate> candidates = {Device(1, 0, 0.0f, 0.0f, 10.0f)};
    std::vector<std::uint32_t> selected;
    Frontier::InsideRectangle(candidates, camera, Frontier::PickBox{0, 0, 200, 200}, 0, selected);
    Assert::IsTrue(selected.empty(), L"w is -10, so it is behind the camera whatever x and y say");
  }
};

} // namespace ReplicaTests
