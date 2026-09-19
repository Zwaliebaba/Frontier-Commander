#pragma once

#include "Order.h"
#include "Picking.h"

#include <cstdint>
#include <span>
#include <vector>

// Turning what the commander did into orders (Design/Interface.md §6; m1-vertical-slice/G1b).
//
// IN Replica AND NOT IN THE EXECUTABLE, for the reason Selection.h records: a test project may not
// list a source outside its own directory (Build/CheckProjectFiles.py), and an .exe exports nothing
// to link against, so logic that deserves tests lives in a library. This one holds interface state
// - what is armed, whether a patrol awaits its far point - which is the one thing that reads oddly
// here; RenderViewBuilder next door holds state of its own for the same reason, that the thing
// reading the replica has to remember what it last saw.
//
// NOTHING HERE TOUCHES Sim. Every gesture becomes a Frontier::Order that Match::Submit hands to
// Net, which is G1b's acceptance line and the thing that makes K4's panels a matter of emitting the
// same records. The rule is easy to keep here and easy to lose later, so it is worth saying where
// the temptation is: this object can see what is under the cursor and it CANNOT see whether the
// order will be accepted. "He cannot afford it" is stage 1's judgement and comes back as a
// rejection (§6); guessing at it here would put a second copy of the rules in the client.
//
// NO DEVICE AND NO WINDOW, for the same reason Selection.h has none: §6's default-order table is
// seven rows of "what is under the cursor, crossed with what is selected", and a table is a unit
// test. What it needs of the replica is reduced to SelectedObject before it gets here, because
// "does this selection have a weapon" is a question about a design and a content row, and asking it
// once a frame beats asking it once an order.
//
// AN ORDER NAMES ONE OBJECT (Sim/Order.h), so a selection of twelve devices is twelve orders. That
// is the simulation's shape and not a convenience: it keeps the record fixed-layout and the
// validation per object, and it is why Advance returns a vector rather than one Order.

namespace Frontier
{

/// One selected object as the order rules read it: what it is and what it can do. Built from the
/// replica's device and its seat's design once a frame (AbilitiesOf below).
struct SelectedObject
{
  std::uint32_t id = 0;
  ObjectKind kind = ObjectKind::Device;
  bool weapon = false;  ///< Carries a module that can shoot
  bool builder = false; ///< Carries build power, so it can construct and repair
};

/// The order armed for the next left click on the world (§6). None is the ordinary state, in which
/// a left click selects instead.
enum class ArmedOrder : std::uint8_t
{
  None,
  Move,
  Patrol,
  AttackMove,
  PlaceStructure
};

/// One frame as OrderInput reads it. Authored pixels, as Selection uses.
struct OrderFrame
{
  PickCamera camera;
  std::span<const PickCandidate> candidates;
  std::span<const PickBox> blocked;
  std::span<const SelectedObject> selected;
  std::uint8_t ownSeat = 0;
  std::int32_t x = 0;
  std::int32_t y = 0;
  bool leftPressed = false;  ///< The left button went down this frame
  bool rightPressed = false; ///< The right button went down this frame
  /// The key edges of the frame, indexed by virtual-key code: +1 pressed, -1 released, 0 neither
  /// (Client/FrameInput.h). Empty is a frame with no keyboard, which every test but the hotkey ones
  /// passes.
  std::span<const std::int8_t> keyEdges;
};

/// The hotkeys of Design/Interface.md §7, as virtual-key codes. A struct rather than constants
/// because §7 says the bindings live in Preferences.json and that the binding layer is M1's: when
/// it lands it fills one of these, and nothing else here changes.
struct OrderKeys
{
  std::uint8_t move = 'M';
  std::uint8_t patrol = 'P';
  std::uint8_t attackMove = 'R';
  std::uint8_t build = 'B';
  std::uint8_t stop = 'S';
  std::uint8_t holdPosition = 'H';
  std::uint8_t cancel = 0x1B; ///< Escape, which §7 peels: an armed order first
};

class OrderInput
{
public:
  /// One frame. Appends the orders it made to _outOrders, which the caller submits; nothing is
  /// appended on a frame that gave none.
  void Advance(const OrderFrame& _frame, std::vector<Order>& _outOrders);

  /// What is armed now, for the cursor (§7) and the footprint ghost.
  [[nodiscard]] ArmedOrder Armed() const noexcept
  {
    return m_armed;
  }
  /// The structure row PlaceStructure will place, meaningless unless PlaceStructure is armed.
  [[nodiscard]] std::uint32_t ArmedStructure() const noexcept
  {
    return m_structureRow;
  }
  /// Arms the placement of a structure row, which until K4's construction panel is a hotkey's job
  /// and afterwards a button's.
  void ArmStructure(std::uint32_t _row) noexcept;

  /// The far point of a Patrol is a second click; this says the first has been taken.
  [[nodiscard]] bool AwaitingSecondPoint() const noexcept
  {
    return m_awaitingSecondPoint;
  }

  void Disarm() noexcept;

  /// The bindings this object reads. The default is §7's table.
  [[nodiscard]] OrderKeys& Keys() noexcept
  {
    return m_keys;
  }

private:
  void DefaultOrder(const OrderFrame& _frame, std::vector<Order>& _outOrders);
  void ArmedAt(const OrderFrame& _frame, std::vector<Order>& _outOrders);

  OrderKeys m_keys;
  ArmedOrder m_armed = ArmedOrder::None;
  std::uint32_t m_structureRow = 0;
  bool m_awaitingSecondPoint = false;
  std::int32_t m_firstX = 0;
  std::int32_t m_firstZ = 0;
};

/// Whether a ray through this screen point lands on the world at all, or inside a panel §5 refuses
/// to cast through. Shared with Selection's rule by being the same arithmetic written once.
[[nodiscard]] bool OnTheWorld(std::span<const PickBox> _blocked, std::int32_t _x, std::int32_t _y) noexcept;

/// Where a ray meets the ground plane at y = 0, in SUBUNITS, which is the unit every positional
/// operand of Sim/Order.h is in. False when the ray is parallel to the ground or points away from
/// it, which is a camera aimed at the sky and an order with nowhere to go.
[[nodiscard]] bool GroundPoint(const PickRay& _ray, std::int32_t& _outX, std::int32_t& _outZ) noexcept;

} // namespace Frontier
