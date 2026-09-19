#pragma once

#include "Picking.h"

#include <cstdint>
#include <span>
#include <vector>

// What the commander has selected, and the drag that is choosing it (Design/Interface.md §5;
// m1-vertical-slice/G1b).
//
// IN Replica AND NOT IN THE EXECUTABLE, which is where m1-vertical-slice/G1b's file list put it and
// where the repository's own rules will not have it: Build/CheckProjectFiles.py refuses a project
// that lists a source outside its own directory, and FrontierCommander is an Application, so an
// .exe exports nothing a test DLL could link. Logic that deserves tests therefore lives in a
// library. It belongs here on its own merits too - Design/Interface.md §5 calls this code "R2's
// picking functions, as pure functions over the replica and the camera matrices", and Picking.h is
// already next door. No layering edge is new: Replica is built on Core, Content, Sim and Net, and
// this needs only Picking and Sim/Order.h.
//
// NO DEVICE AND NO WINDOW. Everything here is arithmetic over the frame's input, R2's picking and
// the replica's candidates, so it is a unit test rather than something only the capture can see.
// That is the same bargain Replica/Picking.h struck and for the same reason: the rules here - which
// press is a click and which is a rectangle, what a rectangle may take, what an empty click does -
// are the ones that are wrong by a pixel or by one commander's units rather than wrong by a crash.
//
// WHAT MAY BE SELECTED IS §5's LIST AND NOT PICKING'S. NearestUnderRay answers with the nearest
// candidate of ANY seat, because inspection is a real thing a commander does: §5 says a click on a
// visible enemy "selects it as an inspection", with the selection panel showing it and the orders
// panel empty. A RECTANGLE is the opposite: it "never selects structures and never selects another
// commander's anything", which is InsideRectangle's own rule and why it takes a seat.
//
// A RAY THAT STARTS INSIDE A PANEL IS NEVER CAST (§5). M1's panels arrive with K4, so the list of
// blocked rectangles is empty here and the rule is honoured rather than remembered later: a click
// that the interface has already answered must not also reach through it into the world.

namespace Frontier
{

/// How far the mouse must travel between press and release before a drag is a rectangle rather than
/// a click, in authored pixels. A hand resting on a mouse moves a pixel or two while it clicks, and
/// a rectangle of three pixels would select whatever happened to be under the cursor by a different
/// rule than the click the commander meant.
inline constexpr std::int32_t DRAG_THRESHOLD_PIXELS = 4;

/// One frame as Selection reads it. Authored pixels throughout (Client/ScaleMode.h's
/// AuthoredFromClient is what turns a client pixel into one), because that is the frame R2's
/// picking projects into and the frame Design/Interface.md lays its panels out in.
struct SelectionFrame
{
  PickCamera camera;
  std::span<const PickCandidate> candidates;
  /// The panel rectangles of §2; a press inside one is the interface's and never reaches the world.
  std::span<const PickBox> blocked;
  std::uint8_t ownSeat = 0;
  std::int32_t x = 0;
  std::int32_t y = 0;
  bool pressed = false;  ///< The left button went down this frame
  bool released = false; ///< And came up this frame
  bool additive = false; ///< Shift: add to the selection rather than replace it
};

class Selection
{
public:
  /// One frame. The selection changes only on release, because until the button comes up nobody -
  /// the commander included - knows whether the gesture was a click or a rectangle.
  void Advance(const SelectionFrame& _frame);

  /// Ascending and without repeats, which is the order Match::Select puts them in and the order the
  /// render view's flags and picking's answers are compared against.
  [[nodiscard]] std::span<const std::uint32_t> Ids() const noexcept
  {
    return m_ids;
  }

  /// Whether a rubber band is being dragged now, and the rectangle to draw for it. Empty until the
  /// press has travelled DRAG_THRESHOLD_PIXELS, so that an ordinary click never flashes one.
  [[nodiscard]] bool Dragging() const noexcept
  {
    return m_dragging;
  }
  [[nodiscard]] PickBox Band() const noexcept;

  /// Drops everything, for a rejoin or a match that has ended.
  void Clear() noexcept;

  /// Forgets ids the replica no longer holds. A selected device that died stays selected otherwise,
  /// and every order the commander gives it afterwards is refused with NotOwned for an object that
  /// is not there - which reads as the game ignoring him.
  void Retain(std::span<const PickCandidate> _candidates);

private:
  void Take(std::vector<std::uint32_t>& _ids, bool _additive);

  std::vector<std::uint32_t> m_ids;
  std::vector<std::uint32_t> m_taken; ///< Scratch, so that a frame allocates nothing
  std::int32_t m_anchorX = 0;
  std::int32_t m_anchorY = 0;
  std::int32_t m_currentX = 0;
  std::int32_t m_currentY = 0;
  bool m_pressing = false; ///< A press landed on the world and has not come up
  bool m_dragging = false; ///< And it has travelled far enough to be a rectangle
};

} // namespace Frontier
