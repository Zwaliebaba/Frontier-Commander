#include "pch.h"

#include "ModelComposer.h"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// Model composition at markers (m1-vertical-slice/R2; Content/ModelDesc.h): a device is a chassis
// with a drive at each MarkerDrive and a module at each MarkerMount, so the model count is a SUM.
// The arithmetic that puts a part where its marker says is the thing worth pinning: it is wrong by
// a reflection rather than by a crash, and a mirrored drive looks like a different model rather
// than like a defect.
namespace ReplicaTests
{

namespace
{

constexpr std::int32_t ONE_WORLD_UNIT = Neuron::SUBUNITS_PER_WORLD_UNIT;

[[nodiscard]] Frontier::ModelMarker Marker(std::string _name, std::int32_t _x, std::int32_t _y, std::int32_t _z, std::uint16_t _heading = 0)
{
  Frontier::ModelMarker marker{};
  marker.name = std::move(_name);
  marker.position = Frontier::ModelVertex{_x, _y, _z};
  marker.headingBinaryAngle = _heading;
  return marker;
}

[[nodiscard]] Frontier::ModelDesc Model(std::string _id, std::vector<Frontier::ModelMarker> _markers = {})
{
  Frontier::ModelDesc model{};
  model.version = Frontier::MODEL_DESC_VERSION;
  model.id = std::move(_id);
  model.markers = std::move(_markers);
  return model;
}

/// A heavy chassis with four drive markers and two mounts, a wheel, two modules one of which has a
/// muzzle, and a structure with one mount. Model 0 is the chassis, so an index is easy to read.
[[nodiscard]] Frontier::ContentTree Tables()
{
  Frontier::ContentTree tree;
  tree.models = {
    Model("HeavyHull",
          {Marker("MarkerDrive1", ONE_WORLD_UNIT, 0, ONE_WORLD_UNIT), Marker("MarkerDrive2", -ONE_WORLD_UNIT, 0, ONE_WORLD_UNIT),
           Marker("MarkerDrive3", ONE_WORLD_UNIT, 0, -ONE_WORLD_UNIT), Marker("MarkerDrive4", -ONE_WORLD_UNIT, 0, -ONE_WORLD_UNIT),
           Marker("MarkerMount1", 0, ONE_WORLD_UNIT, 0), Marker("MarkerMount2", 0, 2 * ONE_WORLD_UNIT, 0),
           Marker("MarkerFragmentA", 0, 0, 0)}),
    Model("Wheel"),
    // TWO MARKERS, ONE OF THEM NOT A MUZZLE, and that is deliberate: with a single marker on this
    // model the muzzle test passes whether the composer checks the name or takes every marker it
    // finds, which is exactly what mutation testing showed before this second one was added.
    Model("Cannon", {Marker("MarkerMuzzle", 0, 0, 2 * ONE_WORLD_UNIT), Marker("MarkerFragmentBarrel", 0, 0, ONE_WORLD_UNIT)}),
    Model("Radar"),
    Model("PostHull", {Marker("MarkerMount1", 0, ONE_WORLD_UNIT, 0)}),
    Model("Turret"),
  };

  Frontier::ChassisDesc heavy{};
  heavy.id = "HeavyI";
  heavy.model = "HeavyHull";
  heavy.mounts = 2;
  Frontier::ChassisDesc missing{};
  missing.id = "Phantom";
  missing.model = "NoSuchModel";
  tree.components.chassis = {heavy, missing};

  Frontier::DriveDesc wheels{};
  wheels.id = "Wheels";
  wheels.model = "Wheel";
  wheels.modelScaleHundredths = 50;
  tree.components.drives = {wheels};

  Frontier::ModuleDesc cannon{};
  cannon.id = "Cannon";
  cannon.model = "Cannon";
  Frontier::ModuleDesc radar{};
  radar.id = "Radar";
  radar.model = "Radar";
  tree.components.modules = {cannon, radar};

  Frontier::StructureDesc post{};
  post.id = "CommandPost";
  post.model = "PostHull";
  post.moduleSlots = 1;
  tree.structures.structures = {post};

  Frontier::StructureModuleDesc turret{};
  turret.id = "Turret";
  turret.model = "Turret";
  tree.structures.modules = {turret};
  return tree;
}

[[nodiscard]] Frontier::DesignState Heavy(std::uint8_t _moduleCount)
{
  Frontier::DesignState design{};
  design.seat = 0;
  design.index = 0;
  design.chassis = 0;
  design.drive = 0;
  design.modules[0] = 0; // the cannon
  design.modules[1] = 1; // the radar
  design.moduleCount = _moduleCount;
  return design;
}

/// How many instances name _modelIndex.
[[nodiscard]] std::size_t CountOf(const std::vector<Neuron::RenderInstance>& _instances, std::uint32_t _modelIndex)
{
  std::size_t count = 0;
  for (const Neuron::RenderInstance& instance : _instances)
  {
    count += instance.modelId == _modelIndex ? 1 : 0;
  }
  return count;
}

void AssertNear(float _expected, float _actual, const wchar_t* _message)
{
  Assert::IsTrue(std::fabs(_expected - _actual) < 0.001f, _message);
}

} // namespace

TEST_CLASS(RenderViewTests)
{
public:
  /// The whole reason the markers exist: a design with two modules is FOUR model kinds drawn seven
  /// times, not a model authored for every combination of chassis, drive and modules.
  TEST_METHOD(AHeavyWithTwoModulesIsASumOfModelsAndNotAProduct)
  {
    const Frontier::ContentTree tree = Tables();
    const Frontier::ModelComposer composer(tree);
    std::vector<Neuron::RenderInstance> instances;
    composer.ComposeDevice(Heavy(2), Frontier::Pose{}, Frontier::ObjectAppearance{}, instances);

    Assert::AreEqual(std::size_t{7}, instances.size(), L"one chassis, four drives, two modules");
    Assert::AreEqual(std::size_t{1}, CountOf(instances, 0), L"the hull");
    Assert::AreEqual(std::size_t{4}, CountOf(instances, 1), L"a wheel at every MarkerDrive");
    Assert::AreEqual(std::size_t{1}, CountOf(instances, 2), L"the cannon on mount 1");
    Assert::AreEqual(std::size_t{1}, CountOf(instances, 3), L"the radar on mount 2");
    Assert::AreEqual(std::uint32_t{1}, composer.UnresolvedRows(), L"the Phantom row, resolved at construction whether or not it is used");
  }

  /// A marker named MarkerFragmentA is neither a drive nor a mount and hangs nothing.
  TEST_METHOD(AMarkerThatIsNeitherADriveNorAMountHangsNothing)
  {
    const Frontier::ContentTree tree = Tables();
    const Frontier::ModelComposer composer(tree);
    std::vector<Neuron::RenderInstance> instances;
    composer.ComposeDevice(Heavy(0), Frontier::Pose{}, Frontier::ObjectAppearance{}, instances);
    Assert::AreEqual(std::size_t{5}, instances.size(), L"the hull and its four wheels, and nothing for the rest");
  }

  TEST_METHOD(ADesignWithFewerModulesThanMountsLeavesTheRestEmpty)
  {
    const Frontier::ContentTree tree = Tables();
    const Frontier::ModelComposer composer(tree);
    std::vector<Neuron::RenderInstance> instances;
    composer.ComposeDevice(Heavy(1), Frontier::Pose{}, Frontier::ObjectAppearance{}, instances);
    Assert::AreEqual(std::size_t{6}, instances.size());
    Assert::AreEqual(std::size_t{1}, CountOf(instances, 2), L"the cannon took the first mount");
    Assert::AreEqual(std::size_t{0}, CountOf(instances, 3), L"and the second is empty");
  }

  /// THE ROTATION IS Client/Shaders/GeometryVS.hlsl's. Heading zero looks along +z and grows toward
  /// +x, so at a quarter turn a marker one unit along +x is one unit along -z. A sign the other way
  /// round mirrors every drive about the device and reads as a different model, not as a defect.
  TEST_METHOD(AMarkerTurnsWithTheDeviceTheWayTheVertexShaderTurnsItsVertices)
  {
    const Frontier::ContentTree tree = Tables();
    const Frontier::ModelComposer composer(tree);
    const Frontier::Pose quarterTurn{10.0f, 0.0f, 20.0f, std::numbers::pi_v<float> / 2.0f};
    const Frontier::Pose placed = Frontier::PlacedAt(quarterTurn, Frontier::ModelVertex{ONE_WORLD_UNIT, 0, 0}, 0);
    // At a quarter turn cos is 0 and sin is 1, so x' = x*0 + z*1 = 0 and z' = z*0 - x*1 = -1: a
    // marker one unit along +x lands one unit along -z of the device. Worked out by hand and not
    // from the code, because a test that recomputes the implementation proves only that it is
    // consistent with itself.
    AssertNear(10.0f, placed.x, L"x is unchanged: the marker's +x became -z");
    AssertNear(19.0f, placed.z, L"one unit behind the device on the z axis");
    AssertNear(0.0f, placed.y, L"and a heading never touches the vertical");
    AssertNear(quarterTurn.headingRadians, placed.headingRadians, L"a marker of heading zero keeps the device's");

    // A marker of its own heading adds it, which is what lets a mount face out rather than forward.
    const Frontier::Pose turned = Frontier::PlacedAt(Frontier::Pose{}, Frontier::ModelVertex{}, Neuron::QUARTER_TURN);
    AssertNear(std::numbers::pi_v<float> / 2.0f, turned.headingRadians, L"a quarter of a binary turn is a quarter of 2 pi");
  }

  /// A muzzle is TWO frames deep - through the module's transform and then the device's - which is
  /// the whole reason it is carried forward rather than worked out from the device alone.
  TEST_METHOD(AMuzzleIsCarriedThroughTheModuleAndThenTheDevice)
  {
    const Frontier::ContentTree tree = Tables();
    const Frontier::ModelComposer composer(tree);
    std::vector<Neuron::RenderInstance> instances;
    std::vector<Frontier::ComposedMuzzle> muzzles;
    composer.ComposeDevice(Heavy(2), Frontier::Pose{100.0f, 0.0f, 200.0f, 0.0f}, Frontier::ObjectAppearance{}, instances, &muzzles);

    Assert::AreEqual(std::size_t{1}, muzzles.size(), L"the cannon has one and the radar none");
    Assert::AreEqual(std::uint32_t{0}, muzzles[0].moduleRow, L"and it says which module it belongs to");
    // The mount is one unit up; the muzzle is two units along +z of the module; the device is at
    // (100, 0, 200) facing +z. So the muzzle is at (100, 1, 202).
    AssertNear(100.0f, muzzles[0].x, L"straight ahead");
    AssertNear(1.0f, muzzles[0].y, L"up at the mount");
    AssertNear(202.0f, muzzles[0].z, L"and two units past it");
  }

  TEST_METHOD(AMuzzleTurnsWithTheDeviceThatCarriesIt)
  {
    const Frontier::ContentTree tree = Tables();
    const Frontier::ModelComposer composer(tree);
    std::vector<Neuron::RenderInstance> instances;
    std::vector<Frontier::ComposedMuzzle> muzzles;
    const float halfTurn = std::numbers::pi_v<float>;
    composer.ComposeDevice(Heavy(1), Frontier::Pose{0.0f, 0.0f, 0.0f, halfTurn}, Frontier::ObjectAppearance{}, instances, &muzzles);
    Assert::AreEqual(std::size_t{1}, muzzles.size());
    AssertNear(-2.0f, muzzles[0].z, L"turned about, the barrel points at -z");
  }

  TEST_METHOD(EveryPartWearsTheCommandersColorAndTheDevicesSelection)
  {
    const Frontier::ContentTree tree = Tables();
    const Frontier::ModelComposer composer(tree);
    std::vector<Neuron::RenderInstance> instances;
    Frontier::ObjectAppearance appearance{};
    appearance.colorIndex = 5;
    appearance.rankBadge = 3;
    appearance.selected = true;
    composer.ComposeDevice(Heavy(2), Frontier::Pose{}, appearance, instances);
    for (const Neuron::RenderInstance& instance : instances)
    {
      Assert::AreEqual(std::uint8_t{5}, instance.colorIndex);
      Assert::AreEqual(std::uint8_t{3}, instance.rankBadge);
      Assert::IsTrue(instance.selected, L"a selected device is selected all over");
      Assert::IsTrue(instance.kind == Neuron::RenderInstanceKind::Object);
      Assert::AreEqual(std::uint8_t{100}, instance.buildPercent, L"a device is not under construction");
    }
  }

  /// The row's own draw factor, not the model's: "one model serves two rows at two sizes".
  TEST_METHOD(EachRowIsDrawnAtItsOwnAuthoredScale)
  {
    const Frontier::ContentTree tree = Tables();
    const Frontier::ModelComposer composer(tree);
    std::vector<Neuron::RenderInstance> instances;
    composer.ComposeDevice(Heavy(2), Frontier::Pose{}, Frontier::ObjectAppearance{}, instances);
    for (const Neuron::RenderInstance& instance : instances)
    {
      AssertNear(instance.modelId == 1 ? 0.5f : 1.0f, instance.scale, L"the wheels are drawn at 50 hundredths");
    }
  }

  /// A mistyped model id is a content fault ContentValidator reports with a file and a line. Here it
  /// is counted and draws nothing - and a chassis with no model hangs nothing either, rather than
  /// piling its drives and modules in a heap at the device's origin.
  TEST_METHOD(AChassisWhoseModelTheTreeDoesNotHoldIsCountedAndHangsNothing)
  {
    const Frontier::ContentTree tree = Tables();
    const Frontier::ModelComposer composer(tree);
    Assert::AreEqual(std::uint32_t{1}, composer.UnresolvedRows(), L"the Phantom row");
    Assert::AreEqual(Frontier::ModelComposer::NO_MODEL, composer.ChassisModel(1));

    Frontier::DesignState phantom = Heavy(2);
    phantom.chassis = 1;
    std::vector<Neuron::RenderInstance> instances;
    composer.ComposeDevice(phantom, Frontier::Pose{}, Frontier::ObjectAppearance{}, instances);
    Assert::IsTrue(instances.empty(), L"not four wheels and two modules stacked at the origin");
  }

  TEST_METHOD(ADesignNamingARowTheContentDoesNotHaveDrawsNothing)
  {
    const Frontier::ContentTree tree = Tables();
    const Frontier::ModelComposer composer(tree);
    Frontier::DesignState nonsense = Heavy(2);
    nonsense.chassis = 99;
    std::vector<Neuron::RenderInstance> instances;
    composer.ComposeDevice(nonsense, Frontier::Pose{}, Frontier::ObjectAppearance{}, instances);
    Assert::IsTrue(instances.empty());
  }

  /// A structure half built is half built INCLUDING its modules, which is why the progress is
  /// written over every instance the structure produced rather than only the hull's.
  TEST_METHOD(AStructuresBuildProgressReachesEveryPartOfIt)
  {
    const Frontier::ContentTree tree = Tables();
    const Frontier::ModelComposer composer(tree);
    Frontier::StructureState post{};
    post.id = 1;
    post.design = 0;
    post.seat = 1;
    post.buildPercent = 40;
    post.modules[0] = 0;
    post.moduleCount = 1;

    std::vector<Neuron::RenderInstance> instances;
    Frontier::ObjectAppearance appearance{};
    appearance.colorIndex = 1;
    composer.ComposeStructure(post, Frontier::Pose{4.0f, 0.0f, 8.0f, 0.0f}, appearance, instances);
    Assert::AreEqual(std::size_t{2}, instances.size(), L"the hull and its turret");
    for (const Neuron::RenderInstance& instance : instances)
    {
      Assert::AreEqual(std::uint8_t{40}, instance.buildPercent);
      Assert::AreEqual(std::uint8_t{1}, instance.colorIndex);
    }
    AssertNear(1.0f, instances[1].y, L"the turret sits on the mount, a unit up");
  }

  TEST_METHOD(AFinishedStructureIsAtAHundredPercent)
  {
    const Frontier::ContentTree tree = Tables();
    const Frontier::ModelComposer composer(tree);
    Frontier::StructureState post{};
    post.design = 0;
    post.buildPercent = 100;
    std::vector<Neuron::RenderInstance> instances;
    composer.ComposeStructure(post, Frontier::Pose{}, Frontier::ObjectAppearance{}, instances);
    Assert::AreEqual(std::size_t{1}, instances.size(), L"no modules mounted");
    Assert::AreEqual(std::uint8_t{100}, instances[0].buildPercent);
  }

  /// A wreck and a projectile are one model each and carry their kind, which is what picking and
  /// the minimap read - nothing selects a wreck (GameDesign.md §7).
  TEST_METHOD(ASingleModelCarriesTheKindItWasComposedAs)
  {
    const Frontier::ContentTree tree = Tables();
    const Frontier::ModelComposer composer(tree);
    std::vector<Neuron::RenderInstance> instances;
    composer.ComposeSingle(0, 1.0f, Frontier::Pose{}, Frontier::ObjectAppearance{}, Neuron::RenderInstanceKind::Wreck, instances);
    composer.ComposeSingle(Frontier::ModelComposer::NO_MODEL, 1.0f, Frontier::Pose{}, Frontier::ObjectAppearance{},
                           Neuron::RenderInstanceKind::Projectile, instances);
    Assert::AreEqual(std::size_t{1}, instances.size(), L"the one with no model drew nothing");
    Assert::IsTrue(instances[0].kind == Neuron::RenderInstanceKind::Wreck);
  }
};

} // namespace ReplicaTests
