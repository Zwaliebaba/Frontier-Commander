#include "pch.h"

#include "OrderInput.h"

#include "Device.h"
#include "FixedPoint.h"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// Design/Interface.md §6's table as cases, and §7's hotkeys beside it.

namespace ReplicaTests
{

namespace
{

/// A camera looking STRAIGHT DOWN from y = 100, which is what an order needs and what the picking
/// suite's side-on fixture cannot give: a ray with no vertical component never meets the ground, so
/// every Move would come back with nowhere to go.
///
/// world (x, y, z) -> clip (x/100, z/100, y/200 + 1/2), depth reversed (ADR-005). Screen x is world
/// x plus 100; screen y is 100 MINUS world z, because the screen counts downward and +z is away.
[[nodiscard]] Frontier::PickCamera TopDownCamera()
{
  Frontier::PickCamera camera{};
  camera.frameWidth = 200;
  camera.frameHeight = 200;
  camera.viewProjection.m = {0.01f, 0.0f,  0.0f,   0.0f, //
                             0.0f,  0.0f,  0.005f, 0.0f, //
                             0.0f,  0.01f, 0.0f,   0.0f, //
                             0.0f,  0.0f,  0.5f,   1.0f};
  camera.inverseViewProjection.m = {100.0f, 0.0f,    0.0f,   0.0f, //
                                    0.0f,   0.0f,    100.0f, 0.0f, //
                                    0.0f,   200.0f,  0.0f,   0.0f, //
                                    0.0f,   -100.0f, 0.0f,   1.0f};
  return camera;
}

[[nodiscard]] Frontier::PickCandidate Thing(std::uint32_t _id, std::uint8_t _seat, Frontier::ObjectKind _kind, float _x, float _z)
{
  Frontier::PickCandidate candidate{};
  candidate.id = _id;
  candidate.seat = _seat;
  candidate.kind = _kind;
  candidate.x = _x;
  candidate.y = 0.0f;
  candidate.z = _z;
  candidate.radius = 6.0f;
  return candidate;
}

[[nodiscard]] Frontier::SelectedObject Armed(std::uint32_t _id)
{
  return {_id, Frontier::ObjectKind::Device, true, false};
}

[[nodiscard]] Frontier::SelectedObject Unarmed(std::uint32_t _id)
{
  return {_id, Frontier::ObjectKind::Device, false, false};
}

[[nodiscard]] Frontier::SelectedObject Building(std::uint32_t _id)
{
  return {_id, Frontier::ObjectKind::Structure, false, false};
}

/// Subunits from world units, which is what every positional operand of Sim/Order.h is in.
[[nodiscard]] std::int32_t Sub(float _worldUnits)
{
  return static_cast<std::int32_t>(std::lround(_worldUnits * static_cast<float>(Neuron::SUBUNITS_PER_WORLD_UNIT)));
}

/// What a screen column names on the ground, in subunits, under this camera.
///
/// THE HALF PIXEL IS REAL AND IS NOT ABSORBED INTO A TOLERANCE. Replica/Picking.cpp casts through
/// the pixel's CENTRE, because that is where the rasterizer sampled what was drawn there, so screen
/// column 150 is world x 50.5 and not 50. The first writing of these cases expected the round
/// number and four of them failed; a tolerance would have hidden it, and half a world unit is a
/// real distance - a test loose enough to swallow it is loose enough to swallow a whole one.
[[nodiscard]] std::int32_t SubAtColumn(std::int32_t _screenX)
{
  return Sub(static_cast<float>(_screenX) + 0.5f - 100.0f);
}

/// And what a screen ROW names: the screen counts downward and +z is away, so row 100 is z = 0 and
/// a row above it is a larger z, again half a pixel over.
[[nodiscard]] std::int32_t SubAtRow(std::int32_t _screenY)
{
  return Sub(100.0f - (static_cast<float>(_screenY) + 0.5f));
}

struct Session
{
  Frontier::OrderInput input;
  std::vector<Frontier::PickCandidate> candidates;
  std::vector<Frontier::PickBox> blocked;
  std::vector<Frontier::SelectedObject> selected;
  std::vector<std::int8_t> keyEdges = std::vector<std::int8_t>(256, 0);
  std::uint8_t seat = 0;

  [[nodiscard]] Frontier::OrderFrame Frame(std::int32_t _x, std::int32_t _y, bool _left, bool _right) const
  {
    Frontier::OrderFrame frame{};
    frame.camera = TopDownCamera();
    frame.candidates = candidates;
    frame.blocked = blocked;
    frame.selected = selected;
    frame.ownSeat = seat;
    frame.x = _x;
    frame.y = _y;
    frame.leftPressed = _left;
    frame.rightPressed = _right;
    frame.keyEdges = keyEdges;
    return frame;
  }

  [[nodiscard]] std::vector<Frontier::Order> RightClick(std::int32_t _x, std::int32_t _y)
  {
    std::vector<Frontier::Order> orders;
    input.Advance(Frame(_x, _y, false, true), orders);
    return orders;
  }

  [[nodiscard]] std::vector<Frontier::Order> LeftClick(std::int32_t _x, std::int32_t _y)
  {
    std::vector<Frontier::Order> orders;
    input.Advance(Frame(_x, _y, true, false), orders);
    return orders;
  }

  [[nodiscard]] std::vector<Frontier::Order> Press(std::uint8_t _key)
  {
    std::vector<Frontier::Order> orders;
    keyEdges[_key] = 1;
    input.Advance(Frame(100, 100, false, false), orders);
    keyEdges[_key] = 0;
    return orders;
  }
};

} // namespace

TEST_CLASS(OrderInputTests)
{
public:
  /// §6 row 1: open ground, devices, Move to that point.
  TEST_METHOD(ARightClickOnOpenGroundMovesEverySelectedDevice)
  {
    Session session;
    session.selected = {Unarmed(1), Unarmed(2)};
    const std::vector<Frontier::Order> orders = session.RightClick(120, 100);
    Assert::AreEqual(std::size_t{2}, orders.size(), L"one order an object, which is the wire's shape");
    for (const Frontier::Order& order : orders)
    {
      Assert::IsTrue(order.kind == Frontier::OrderKind::Move, L"a Move");
      Assert::AreEqual(SubAtColumn(120), order.operands[1], L"x in subunits");
      Assert::AreEqual(SubAtRow(100), order.operands[2], L"z in subunits");
      Assert::AreEqual(std::uint8_t{0}, order.seat, L"and it is this commander's");
    }
  }

  /// §6 rows 2 and 3: an armed device attacks a visible enemy and an unarmed one moves to it.
  TEST_METHOD(ARightClickOnAnEnemySplitsTheSelectionByWhetherItCanShoot)
  {
    Session session;
    session.candidates = {Thing(9, 1, Frontier::ObjectKind::Device, 0.0f, 0.0f)};
    session.selected = {Armed(1), Unarmed(2)};
    const std::vector<Frontier::Order> orders = session.RightClick(100, 100);
    Assert::AreEqual(std::size_t{2}, orders.size(), L"both are ordered, differently");

    Assert::IsTrue(orders[0].kind == Frontier::OrderKind::Attack, L"the armed one attacks");
    Assert::AreEqual(std::int32_t{1}, orders[0].operands[0], L"it is the attacker");
    Assert::AreEqual(std::int32_t{9}, orders[0].operands[1], L"and the enemy is the target");
    Assert::AreEqual(static_cast<std::int32_t>(Frontier::ObjectKind::Device), orders[0].operands[2], L"named by kind too");

    Assert::IsTrue(orders[1].kind == Frontier::OrderKind::Move, L"the unarmed one moves to it");
    Assert::AreEqual(std::int32_t{2}, orders[1].operands[0], L"it is the mover");
  }

  /// §6 row 6: "a structure is selected - nothing; a structure takes no primary order". It is
  /// skipped rather than refusing the whole click, because a commander with a factory and four
  /// trucks selected who right-clicks means the trucks.
  TEST_METHOD(AStructureInTheSelectionTakesNoPrimaryOrder)
  {
    Session session;
    session.selected = {Building(5), Unarmed(6)};
    const std::vector<Frontier::Order> orders = session.RightClick(120, 100);
    Assert::AreEqual(std::size_t{1}, orders.size(), L"only the device is ordered");
    Assert::AreEqual(std::int32_t{6}, orders[0].operands[0], L"and it is the device");
  }

  /// A camera aimed above the horizon has no ground under the cursor, and an order with nowhere to
  /// go is not an order.
  TEST_METHOD(ARightClickThatMeetsNoGroundOrdersNothing)
  {
    Session session;
    session.selected = {Unarmed(1)};
    // A ray parallel to the ground: the side-on camera of the picking suite, which never meets y=0.
    Frontier::OrderFrame frame = session.Frame(100, 100, false, true);
    frame.camera.inverseViewProjection.m = {100.0f, 0.0f, 0.0f,   0.0f, 0.0f, 100.0f, 0.0f,    0.0f,
                                            0.0f,   0.0f, 200.0f, 0.0f, 0.0f, 50.0f,  -100.0f, 1.0f};
    std::vector<Frontier::Order> orders;
    session.input.Advance(frame, orders);
    Assert::AreEqual(std::size_t{0}, orders.size(), L"nothing is ordered at the sky");
  }

  /// A ray aimed ABOVE the horizon meets the ground behind the camera, at a negative distance along
  /// itself, and an order issued there sends the commander's devices to a point he is looking away
  /// from. Mutation testing found this untested: removing the guard changed no result, because the
  /// case above only covers a ray PARALLEL to the ground, which a different guard catches.
  TEST_METHOD(ARightClickAboveTheHorizonOrdersNothing)
  {
    Session session;
    session.selected = {Unarmed(1)};
    Frontier::OrderFrame frame = session.Frame(100, 100, false, true);
    // A camera above the ground looking UP: near at y = 10, far at y = 210, so the ray climbs and
    // the plane y = 0 is behind it.
    frame.camera.inverseViewProjection.m = {100.0f, 0.0f,    0.0f,   0.0f, //
                                            0.0f,   0.0f,    100.0f, 0.0f, //
                                            0.0f,   -200.0f, 0.0f,   0.0f, //
                                            0.0f,   210.0f,  0.0f,   1.0f};
    const Frontier::PickRay ray = Frontier::RayThrough(frame.camera, 100, 100);
    Assert::IsTrue(ray.directionY > 0.0f, L"the ray climbs, or this case tests the parallel guard again");
    std::int32_t x = 0;
    std::int32_t z = 0;
    Assert::IsFalse(Frontier::GroundPoint(ray, x, z), L"and it never meets the ground in front of the camera");

    std::vector<Frontier::Order> orders;
    session.input.Advance(frame, orders);
    Assert::AreEqual(std::size_t{0}, orders.size(), L"so nothing is ordered");
  }

  /// §7: S is Stop, and it needs no click - it is the one order a commander gives when what he
  /// wants is for something to stop happening, and asking him to aim it would be absurd.
  TEST_METHOD(StopIsImmediateAndNeedsNoClick)
  {
    Session session;
    session.selected = {Unarmed(1), Unarmed(2), Building(3)};
    const std::vector<Frontier::Order> orders = session.Press('S');
    Assert::AreEqual(std::size_t{2}, orders.size(), L"both devices, not the structure");
    Assert::IsTrue(orders[0].kind == Frontier::OrderKind::Stop, L"a Stop");
  }

  /// §7: H is the hold-position stance, which is SetStance on the movement axis.
  TEST_METHOD(HoldPositionIsSetStanceOnTheMovementAxis)
  {
    Session session;
    session.selected = {Unarmed(1)};
    const std::vector<Frontier::Order> orders = session.Press('H');
    Assert::AreEqual(std::size_t{1}, orders.size(), L"one order");
    Assert::IsTrue(orders[0].kind == Frontier::OrderKind::SetStance, L"a SetStance");
    Assert::AreEqual(static_cast<std::int32_t>(Frontier::StanceAxis::Movement), orders[0].operands[1], L"on the movement axis");
    Assert::AreEqual(static_cast<std::int32_t>(Frontier::MovementStance::HoldPosition), orders[0].operands[2], L"holding position");
  }

  /// §6: an armed order "changes the cursor and makes the next left click on the world issue that
  /// order instead of selecting".
  TEST_METHOD(AttackMoveIsArmedByItsKeyAndIssuedByTheNextLeftClick)
  {
    Session session;
    session.selected = {Armed(1)};
    Assert::AreEqual(std::size_t{0}, session.Press('R').size(), L"arming sends nothing");
    Assert::IsTrue(session.input.Armed() == Frontier::ArmedOrder::AttackMove, L"and it is armed");

    const std::vector<Frontier::Order> orders = session.LeftClick(140, 100);
    Assert::AreEqual(std::size_t{1}, orders.size(), L"the click issues it");
    Assert::IsTrue(orders[0].kind == Frontier::OrderKind::AttackMove, L"an AttackMove");
    Assert::AreEqual(SubAtColumn(140), orders[0].operands[1], L"at the point clicked");
    Assert::IsTrue(session.input.Armed() == Frontier::ArmedOrder::None, L"and it disarms afterwards");
  }

  /// §6: "Patrol takes two clicks, the second setting the far point." An order with one end of a
  /// patrol in it is not a patrol, so the first click sends nothing.
  TEST_METHOD(PatrolTakesTwoClicksAndTheFirstSendsNothing)
  {
    Session session;
    session.selected = {Armed(1)};
    (void)session.Press('P');
    Assert::AreEqual(std::size_t{0}, session.LeftClick(120, 100).size(), L"the first click is remembered");
    Assert::IsTrue(session.input.AwaitingSecondPoint(), L"and it says so");

    const std::vector<Frontier::Order> orders = session.LeftClick(100, 80);
    Assert::AreEqual(std::size_t{1}, orders.size(), L"the second issues it");
    Assert::IsTrue(orders[0].kind == Frontier::OrderKind::Patrol, L"a Patrol");
    Assert::AreEqual(SubAtColumn(120), orders[0].operands[1], L"anchored at the FIRST point");
  }

  /// §6, until K4's construction panel: a hotkey plus a click places a structure. The operand is a
  /// CELL and not a point, because placement is on the grid (Sim/Order.h).
  TEST_METHOD(PlacingAStructureNamesACellAndNotAPoint)
  {
    Session session;
    session.selected = {Unarmed(1)};
    session.input.ArmStructure(3);
    const std::vector<Frontier::Order> orders = session.LeftClick(164, 100);
    Assert::AreEqual(std::size_t{1}, orders.size(), L"one placement, whatever is selected");
    Assert::IsTrue(orders[0].kind == Frontier::OrderKind::PlaceStructure, L"a PlaceStructure");
    Assert::AreEqual(std::int32_t{3}, orders[0].operands[0], L"of the row armed");
    Assert::AreEqual(std::int32_t{1}, orders[0].operands[1], L"at cell x: 64 world units is one cell");
  }

  /// §7: "Escape cancels an armed order, then a modal panel, then the pause menu". It is peeled,
  /// so this object takes it only while something of its own is open.
  TEST_METHOD(EscapePeelsTheArmedOrderAndNothingElse)
  {
    Session session;
    session.selected = {Armed(1)};
    (void)session.Press('R');
    Assert::AreEqual(std::size_t{0}, session.Press(0x1B).size(), L"Escape sends no order");
    Assert::IsTrue(session.input.Armed() == Frontier::ArmedOrder::None, L"and the armed order is gone");

    const std::vector<Frontier::Order> orders = session.LeftClick(140, 100);
    Assert::AreEqual(std::size_t{0}, orders.size(), L"the next left click selects instead of ordering");
  }

  /// §6: "Escape or a right click disarms it." One click back to the ordinary cursor, rather than
  /// one structure to a refund.
  TEST_METHOD(ARightClickWhileArmedDisarmsAndOrdersNothing)
  {
    Session session;
    session.selected = {Unarmed(1)};
    session.input.ArmStructure(2);
    const std::vector<Frontier::Order> orders = session.RightClick(120, 100);
    Assert::AreEqual(std::size_t{0}, orders.size(), L"no Move and no placement");
    Assert::IsTrue(session.input.Armed() == Frontier::ArmedOrder::None, L"only a disarm");
  }

  /// §5's rule, which orders obey as much as selection does: a click the interface has already
  /// answered must not reach through the panel into the world.
  TEST_METHOD(AClickInsideAPanelOrdersNothing)
  {
    Session session;
    session.selected = {Unarmed(1)};
    session.blocked = {Frontier::PickBox{90, 90, 130, 130}};
    Assert::AreEqual(std::size_t{0}, session.RightClick(100, 100).size(), L"the panel took it");
  }

  /// The one conversion in this file, pinned: a ground point is in SUBUNITS, 256 to the world unit,
  /// because that is the unit every positional operand of Sim/Order.h is in. Getting it wrong sends
  /// a Move 256 times too far and the simulation obeys it.
  TEST_METHOD(AGroundPointIsInSubunits)
  {
    const Frontier::PickRay ray = Frontier::RayThrough(TopDownCamera(), 150, 100);
    std::int32_t x = 0;
    std::int32_t z = 0;
    Assert::IsTrue(Frontier::GroundPoint(ray, x, z), L"the ray meets the ground");
    Assert::AreEqual(SubAtColumn(150), x, L"fifty and a half world units, through the pixel's centre");
    Assert::AreEqual(SubAtRow(100), z, L"and the click was all but on the z axis");
  }
};

} // namespace ReplicaTests
