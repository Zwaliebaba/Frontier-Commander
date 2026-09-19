#include "pch.h"

#include "OrderInput.h"

#include "Device.h"
#include "FixedPoint.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

/// The key went down this frame. An empty span is a frame with no keyboard at all.
[[nodiscard]] bool Pressed(std::span<const std::int8_t> _edges, std::uint8_t _key) noexcept
{
  return _key < _edges.size() && _edges[_key] > 0;
}

[[nodiscard]] Order Make(std::uint8_t _seat, OrderKind _kind) noexcept
{
  Order order{};
  order.seat = _seat;
  order.kind = _kind;
  // The tick is the HOST'S: Net/Host.cpp stamps an arriving order with its own next tick, because
  // an order for a tick already run would be moved anyway and one for a tick far ahead would let a
  // client schedule the future. Writing anything here would be writing a number nobody reads.
  order.tick = 0;
  return order;
}

/// The candidate under the ray, or nullptr. Its own function because both the default order and the
/// cursor want it and neither should cast the ray twice (AGENTS.md R16).
[[nodiscard]] const PickCandidate* Under(const OrderFrame& _frame, const PickRay& _ray) noexcept
{
  const PickResult found = NearestUnderRay(_frame.candidates, _ray);
  if (!found.hit)
  {
    return nullptr;
  }
  const auto at = std::find_if(_frame.candidates.begin(), _frame.candidates.end(),
                               [found](const PickCandidate& _candidate) { return _candidate.id == found.id; });
  return at == _frame.candidates.end() ? nullptr : &*at;
}

[[nodiscard]] bool AnyDeviceWithAWeapon(std::span<const SelectedObject> _selected) noexcept
{
  return std::any_of(_selected.begin(), _selected.end(),
                     [](const SelectedObject& _object) { return _object.kind == ObjectKind::Device && _object.weapon; });
}

} // namespace

bool OnTheWorld(std::span<const PickBox> _blocked, std::int32_t _x, std::int32_t _y) noexcept
{
  return std::none_of(_blocked.begin(), _blocked.end(),
                      [_x, _y](const PickBox& _box) { return _x >= _box.left && _x < _box.right && _y >= _box.top && _y < _box.bottom; });
}

bool GroundPoint(const PickRay& _ray, std::int32_t& _outX, std::int32_t& _outZ) noexcept
{
  // The ground plane is y = 0 and the order is a point on it. NOT the terrain's height: a Move
  // names x and z only (Sim/Order.h) and the simulation puts the device on whatever ground is
  // there, so intersecting the heightfield here would cost a walk to produce a number nobody sends.
  if (std::fabs(_ray.directionY) < 1e-6f)
  {
    return false; // Parallel to the ground: it never meets it.
  }
  const float along = -_ray.originY / _ray.directionY;
  if (along <= 0.0f)
  {
    return false; // Behind the camera: the ray points away from the ground, at the sky.
  }
  const float worldX = _ray.originX + _ray.directionX * along;
  const float worldZ = _ray.originZ + _ray.directionZ * along;
  const float subunits = static_cast<float>(Neuron::SUBUNITS_PER_WORLD_UNIT);
  _outX = static_cast<std::int32_t>(std::lround(worldX * subunits));
  _outZ = static_cast<std::int32_t>(std::lround(worldZ * subunits));
  return true;
}

void OrderInput::ArmStructure(std::uint32_t _row) noexcept
{
  m_armed = ArmedOrder::PlaceStructure;
  m_structureRow = _row;
  m_awaitingSecondPoint = false;
}

void OrderInput::Disarm() noexcept
{
  m_armed = ArmedOrder::None;
  m_awaitingSecondPoint = false;
}

void OrderInput::DefaultOrder(const OrderFrame& _frame, std::vector<Order>& _outOrders)
{
  const PickRay ray = RayThrough(_frame.camera, _frame.x, _frame.y);
  const PickCandidate* target = Under(_frame, ray);
  const bool enemy = target != nullptr && target->seat != _frame.ownSeat;

  // §6's table. A structure in the selection takes no primary order, so it contributes nothing and
  // is skipped rather than refused: a commander who has a factory and four trucks selected and
  // right-clicks means the trucks.
  if (enemy && AnyDeviceWithAWeapon(_frame.selected))
  {
    for (const SelectedObject& object : _frame.selected)
    {
      if (object.kind != ObjectKind::Device || !object.weapon)
      {
        continue; // A device with no weapon moves to the enemy instead, below.
      }
      Order order = Make(_frame.ownSeat, OrderKind::Attack);
      order.operands[0] = static_cast<std::int32_t>(object.id);
      order.operands[1] = static_cast<std::int32_t>(target->id);
      order.operands[2] = static_cast<std::int32_t>(target->kind);
      _outOrders.push_back(order);
    }
  }

  // Everything else in the selection moves: to the point under the cursor over open ground, and to
  // the target's position over an enemy it cannot shoot, an own damaged structure or an own plan -
  // §6 gives all three the same answer, "Move adjacent", and the simulation's steering is what
  // makes adjacent mean adjacent.
  std::int32_t x = 0;
  std::int32_t z = 0;
  const bool onGround = target != nullptr || GroundPoint(ray, x, z);
  if (!onGround)
  {
    return; // A right click at the sky orders nothing.
  }
  if (target != nullptr)
  {
    x = static_cast<std::int32_t>(std::lround(target->x * static_cast<float>(Neuron::SUBUNITS_PER_WORLD_UNIT)));
    z = static_cast<std::int32_t>(std::lround(target->z * static_cast<float>(Neuron::SUBUNITS_PER_WORLD_UNIT)));
  }
  for (const SelectedObject& object : _frame.selected)
  {
    if (object.kind != ObjectKind::Device)
    {
      continue;
    }
    if (enemy && object.weapon)
    {
      continue; // It was given an Attack above.
    }
    Order order = Make(_frame.ownSeat, OrderKind::Move);
    order.operands[0] = static_cast<std::int32_t>(object.id);
    order.operands[1] = x;
    order.operands[2] = z;
    _outOrders.push_back(order);
  }
}

void OrderInput::ArmedAt(const OrderFrame& _frame, std::vector<Order>& _outOrders)
{
  const PickRay ray = RayThrough(_frame.camera, _frame.x, _frame.y);
  std::int32_t x = 0;
  std::int32_t z = 0;
  if (!GroundPoint(ray, x, z))
  {
    return; // Armed at the sky: the order waits for a click that lands somewhere.
  }

  if (m_armed == ArmedOrder::PlaceStructure)
  {
    // A cell and not a point (Sim/Order.h): placement is on the grid, because the obstruction grid
    // and the pathfinder read cells. Negative ground is off the landscape and the simulation
    // refuses it with InvalidPlacement, which is the correction §12 ruling 7 relies on.
    Order order = Make(_frame.ownSeat, OrderKind::PlaceStructure);
    order.operands[0] = static_cast<std::int32_t>(m_structureRow);
    order.operands[1] = x / Neuron::SUBUNITS_PER_CELL;
    order.operands[2] = z / Neuron::SUBUNITS_PER_CELL;
    _outOrders.push_back(order);
    Disarm();
    return;
  }

  if (m_armed == ArmedOrder::Patrol && !m_awaitingSecondPoint)
  {
    // §6: "Patrol takes two clicks, the second setting the far point." The first is remembered and
    // nothing is sent, because an order with one end of a patrol in it is not a patrol.
    m_firstX = x;
    m_firstZ = z;
    m_awaitingSecondPoint = true;
    return;
  }

  const OrderKind kind = m_armed == ArmedOrder::AttackMove ? OrderKind::AttackMove
                         : m_armed == ArmedOrder::Patrol   ? OrderKind::Patrol
                                                           : OrderKind::Move;
  const std::int32_t toX = m_armed == ArmedOrder::Patrol ? m_firstX : x;
  const std::int32_t toZ = m_armed == ArmedOrder::Patrol ? m_firstZ : z;
  for (const SelectedObject& object : _frame.selected)
  {
    if (object.kind != ObjectKind::Device)
    {
      continue;
    }
    Order order = Make(_frame.ownSeat, kind);
    order.operands[0] = static_cast<std::int32_t>(object.id);
    order.operands[1] = toX;
    order.operands[2] = toZ;
    _outOrders.push_back(order);
  }
  Disarm();
}

void OrderInput::Advance(const OrderFrame& _frame, std::vector<Order>& _outOrders)
{
  // ── The hotkeys of §7 ──────────────────────────────────────────────────────────────────────
  //
  // ESCAPE IS PEELED AND NOT SWALLOWED (§7): it cancels the innermost thing that is open, one press
  // at a time. The innermost thing this object owns is an armed order, so it takes Escape only
  // while one is armed and leaves it for the modal panels of K4 and the pause menu otherwise.
  if (Pressed(_frame.keyEdges, m_keys.cancel))
  {
    if (m_armed != ArmedOrder::None)
    {
      Disarm();
      return;
    }
  }
  if (Pressed(_frame.keyEdges, m_keys.move))
  {
    m_armed = ArmedOrder::Move;
    m_awaitingSecondPoint = false;
  }
  if (Pressed(_frame.keyEdges, m_keys.patrol))
  {
    m_armed = ArmedOrder::Patrol;
    m_awaitingSecondPoint = false;
  }
  if (Pressed(_frame.keyEdges, m_keys.attackMove))
  {
    m_armed = ArmedOrder::AttackMove;
    m_awaitingSecondPoint = false;
  }
  if (Pressed(_frame.keyEdges, m_keys.stop))
  {
    // Stop is immediate and needs no click: it is the one order a commander gives when what he
    // wants is for something to stop happening, and asking him to aim it would be absurd.
    for (const SelectedObject& object : _frame.selected)
    {
      if (object.kind != ObjectKind::Device)
      {
        continue;
      }
      Order order = Make(_frame.ownSeat, OrderKind::Stop);
      order.operands[0] = static_cast<std::int32_t>(object.id);
      _outOrders.push_back(order);
    }
    Disarm();
  }
  if (Pressed(_frame.keyEdges, m_keys.holdPosition))
  {
    for (const SelectedObject& object : _frame.selected)
    {
      if (object.kind != ObjectKind::Device)
      {
        continue;
      }
      Order order = Make(_frame.ownSeat, OrderKind::SetStance);
      order.operands[0] = static_cast<std::int32_t>(object.id);
      order.operands[1] = static_cast<std::int32_t>(StanceAxis::Movement);
      order.operands[2] = static_cast<std::int32_t>(MovementStance::HoldPosition);
      _outOrders.push_back(order);
    }
  }

  if (!OnTheWorld(_frame.blocked, _frame.x, _frame.y))
  {
    return; // The click is the interface's; §5 refuses the ray before it is cast.
  }
  // A RIGHT CLICK DISARMS RATHER THAN ORDERING (§6), so that the commander who armed Build by
  // mistake is one click from the ordinary cursor and not one structure from a refund.
  if (_frame.rightPressed)
  {
    if (m_armed != ArmedOrder::None)
    {
      Disarm();
      return;
    }
    DefaultOrder(_frame, _outOrders);
    return;
  }
  if (_frame.leftPressed && m_armed != ArmedOrder::None)
  {
    ArmedAt(_frame, _outOrders);
  }
}

} // namespace Frontier
