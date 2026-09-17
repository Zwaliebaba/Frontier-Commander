#include "pch.h"

#include "Camera.h"

#include <cmath>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace ClientTests
{

namespace
{

constexpr float TOLERANCE = 0.001f;

} // namespace

TEST_CLASS(CameraTests)
{
public:
  TEST_METHOD(TheProjectionIsSixtyDegreesBetweenFiveAndFifteenThousand)
  {
    const Neuron::Camera camera;
    DirectX::XMFLOAT4X4 projection;
    DirectX::XMStoreFloat4x4(&projection, camera.Projection(16.0f / 9.0f));
    Assert::AreEqual(1.0f / std::tan(30.0f * 3.14159265f / 180.0f), projection._22, TOLERANCE, L"the vertical field of view is 60 degrees");
    Assert::AreEqual(projection._22 * 9.0f / 16.0f, projection._11, TOLERANCE);
    // Reversed depth (ADR-005): a point on the near plane lands at depth 1 and one on the far plane at depth 0.
    const DirectX::XMVECTOR nearPoint =
      DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(0.0f, 0.0f, Neuron::CAMERA_NEAR, 1.0f), camera.Projection(1.0f));
    const DirectX::XMVECTOR farPoint =
      DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(0.0f, 0.0f, Neuron::CAMERA_FAR, 1.0f), camera.Projection(1.0f));
    Assert::AreEqual(1.0f, DirectX::XMVectorGetZ(nearPoint), TOLERANCE);
    Assert::AreEqual(0.0f, DirectX::XMVectorGetZ(farPoint), TOLERANCE);
    const DirectX::XMVECTOR between =
      DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(0.0f, 0.0f, 1000.0f, 1.0f), camera.Projection(1.0f));
    Assert::IsTrue(DirectX::XMVectorGetZ(between) > 0.0f && DirectX::XMVectorGetZ(between) < 1.0f, L"depth falls with distance");
  }

  TEST_METHOD(TheFarPlaneIsPushedOutByALandscapeAndNeverPulledIn)
  {
    Neuron::Camera camera;
    camera.SetFarPlane(8192.0f * Neuron::FAR_PLANE_EXTENT_FACTOR);
    Assert::AreEqual(Neuron::CAMERA_FAR, camera.FarPlane(), L"a Small landscape's far corner is inside the Species far plane");
    camera.SetFarPlane(30000.0f);
    Assert::AreEqual(30000.0f, camera.FarPlane());
    const DirectX::XMVECTOR farPoint =
      DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(0.0f, 0.0f, 30000.0f, 1.0f), camera.Projection(1.0f));
    Assert::AreEqual(0.0f, DirectX::XMVectorGetZ(farPoint), TOLERANCE, L"the far plane moved with it");
  }

  TEST_METHOD(TheHeightIsClampedAboveTheGroundAndBelowTheCeiling)
  {
    const std::vector<std::int16_t> samples(static_cast<std::size_t>(9) * 9, 100);
    const Neuron::HeightView view{samples.data(), 9, 16, 0, 100};
    Neuron::Camera camera;
    camera.SetPosition(64.0f, 20.0f, 64.0f);
    camera.ClampHeight(view);
    Assert::AreEqual(100.0f + Neuron::CAMERA_MIN_CLEARANCE, camera.Position().y, TOLERANCE, L"lifted onto the floor");
    camera.SetPosition(64.0f, 9000.0f, 64.0f);
    camera.ClampHeight(view);
    Assert::AreEqual(Neuron::CAMERA_MAX_HEIGHT, camera.Position().y, TOLERANCE, L"held under the ceiling");
    camera.SetPosition(64.0f, 300.0f, 64.0f);
    camera.ClampHeight(view);
    Assert::AreEqual(300.0f, camera.Position().y, TOLERANCE, L"left alone in between");
  }

  TEST_METHOD(TheFloorIsAtLeastTheWaterAndFollowsTheNearbyGround)
  {
    std::vector<std::int16_t> samples(static_cast<std::size_t>(9) * 9, -26);
    samples[static_cast<std::size_t>(4) * 9 + 5] = 40; // one sample away from the camera
    const Neuron::HeightView view{samples.data(), 9, 16, 0, 40};
    Neuron::Camera camera;
    camera.SetPosition(64.0f, -100.0f, 64.0f);
    camera.ClampHeight(view);
    Assert::AreEqual(40.0f + Neuron::CAMERA_MIN_CLEARANCE, camera.Position().y, TOLERANCE, L"the ground a spacing away counts");
    camera.SetPosition(0.0f, -100.0f, 0.0f);
    camera.ClampHeight(view);
    Assert::AreEqual(Neuron::CAMERA_MIN_CLEARANCE, camera.Position().y, TOLERANCE, L"over the plain, the water is the floor");
  }

  TEST_METHOD(ThePitchIsClampedAndTheYawWraps)
  {
    Neuron::Camera camera;
    camera.SetOrientation(0.0f, 3.0f);
    Assert::AreEqual(89.0f * 3.14159265f / 180.0f, camera.Pitch(), TOLERANCE);
    camera.SetOrientation(7.0f, -3.0f);
    Assert::AreEqual(-89.0f * 3.14159265f / 180.0f, camera.Pitch(), TOLERANCE);
    Assert::AreEqual(7.0f - 2.0f * 3.14159265f, camera.Yaw(), TOLERANCE);
  }

  TEST_METHOD(LookAtAimsAndMoveFollowsTheHeading)
  {
    Neuron::Camera camera;
    camera.SetPosition(0.0f, 100.0f, 0.0f);
    camera.LookAt(100.0f, 0.0f, 0.0f);
    Assert::AreEqual(3.14159265f / 2.0f, camera.Yaw(), TOLERANCE, L"toward +x is a quarter turn from +z");
    Assert::AreEqual(-3.14159265f / 4.0f, camera.Pitch(), TOLERANCE, L"as far down as along");
    camera.SetOrientation(0.0f, -0.5f);
    camera.Move(10.0f, 5.0f, 2.0f);
    Assert::AreEqual(5.0f, camera.Position().x, TOLERANCE);
    Assert::AreEqual(102.0f, camera.Position().y, TOLERANCE);
    Assert::AreEqual(10.0f, camera.Position().z, TOLERANCE, L"forward is along the ground, whatever the pitch");
  }

  TEST_METHOD(GroundHeightInterpolatesAndTheOutsideIsThePlain)
  {
    std::vector<std::int16_t> samples(static_cast<std::size_t>(3) * 3, 0);
    samples[1] = 100; // sample (1, 0)
    const Neuron::HeightView view{samples.data(), 3, 16, 0, 100};
    Assert::AreEqual(50.0f, Neuron::GroundHeightAt(view, 8.0f, 0.0f), TOLERANCE, L"halfway between samples 0 and 1 of the first row");
    Assert::AreEqual(25.0f, Neuron::GroundHeightAt(view, 8.0f, 8.0f), TOLERANCE, L"and halfway down toward the zero row");
    Assert::AreEqual(-26.0f, Neuron::GroundHeightAt(view, -100.0f, 0.0f), TOLERANCE, L"the plain outside");
  }
};

} // namespace ClientTests
