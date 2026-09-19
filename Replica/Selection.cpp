#include "pch.h"

#include "Selection.h"

#include <algorithm>
#include <cstdlib>

namespace Frontier
{

namespace
{

[[nodiscard]] bool Inside(const PickBox& _box, std::int32_t _x, std::int32_t _y) noexcept
{
  return _x >= _box.left && _x < _box.right && _y >= _box.top && _y < _box.bottom;
}

/// The rectangle between two points, whichever corner the drag started at. Half open on the right
/// and the bottom, which is what PickBox promises and what InsideRectangle tests.
[[nodiscard]] PickBox Between(std::int32_t _x0, std::int32_t _y0, std::int32_t _x1, std::int32_t _y1) noexcept
{
  PickBox box{};
  box.left = std::min(_x0, _x1);
  box.top = std::min(_y0, _y1);
  box.right = std::max(_x0, _x1) + 1;
  box.bottom = std::max(_y0, _y1) + 1;
  return box;
}

} // namespace

PickBox Selection::Band() const noexcept
{
  return m_dragging ? Between(m_anchorX, m_anchorY, m_currentX, m_currentY) : PickBox{};
}

void Selection::Clear() noexcept
{
  m_ids.clear();
  m_pressing = false;
  m_dragging = false;
}

void Selection::Retain(std::span<const PickCandidate> _candidates)
{
  const auto gone = [_candidates](std::uint32_t _id)
  { return std::none_of(_candidates.begin(), _candidates.end(), [_id](const PickCandidate& _candidate) { return _candidate.id == _id; }); };
  m_ids.erase(std::remove_if(m_ids.begin(), m_ids.end(), gone), m_ids.end());
}

void Selection::Take(std::vector<std::uint32_t>& _ids, bool _additive)
{
  if (_additive)
  {
    m_ids.insert(m_ids.end(), _ids.begin(), _ids.end());
  }
  else
  {
    m_ids.assign(_ids.begin(), _ids.end());
  }
  std::sort(m_ids.begin(), m_ids.end());
  m_ids.erase(std::unique(m_ids.begin(), m_ids.end()), m_ids.end());
}

void Selection::Advance(const SelectionFrame& _frame)
{
  if (_frame.pressed)
  {
    // A press inside a panel is the interface's answer to it, and §5 refuses the ray before it is
    // cast rather than casting it and discarding the result: a click that lands on a button must
    // not also select whatever stands behind the panel.
    const bool onPanel = std::any_of(_frame.blocked.begin(), _frame.blocked.end(),
                                     [&_frame](const PickBox& _box) { return Inside(_box, _frame.x, _frame.y); });
    m_pressing = !onPanel;
    m_dragging = false;
    m_anchorX = _frame.x;
    m_anchorY = _frame.y;
  }
  m_currentX = _frame.x;
  m_currentY = _frame.y;
  if (m_pressing && !m_dragging)
  {
    m_dragging = std::abs(_frame.x - m_anchorX) >= DRAG_THRESHOLD_PIXELS || std::abs(_frame.y - m_anchorY) >= DRAG_THRESHOLD_PIXELS;
  }

  if (!_frame.released || !m_pressing)
  {
    // A release without a press of ours is a button that went down on a panel, or one that was
    // already down when this object started. Neither selects anything.
    m_pressing = _frame.released ? false : m_pressing;
    m_dragging = _frame.released ? false : m_dragging;
    return;
  }

  m_taken.clear();
  if (m_dragging)
  {
    // A RECTANGLE TAKES THIS COMMANDER'S DEVICES AND NOTHING ELSE (§5): never a structure, never
    // another commander's anything. InsideRectangle carries both rules, which is why the seat is
    // one of its arguments rather than a filter applied afterwards.
    InsideRectangle(_frame.candidates, _frame.camera, Between(m_anchorX, m_anchorY, _frame.x, _frame.y), _frame.ownSeat, m_taken);
  }
  else
  {
    // A CLICK TAKES ONE THING OF ANY SEAT. An enemy is selected as an inspection (§5): the
    // selection panel shows it and the orders panel is empty, because there is nothing it can be
    // told to do. Refusing the pick here would make a commander unable to look at what is shooting
    // at him.
    const PickResult found = NearestUnderRay(_frame.candidates, RayThrough(_frame.camera, _frame.x, _frame.y));
    if (found.hit)
    {
      m_taken.push_back(found.id);
    }
  }
  // An empty click clears, unless Shift is held: a commander who missed while adding to a group
  // meant to add nothing, not to lose the group. That falls out of Take rather than needing a
  // guard - adding an empty list changes nothing, assigning one clears - and the guard that used to
  // stand here was redundant, which mutation testing showed by surviving its removal.
  Take(m_taken, _frame.additive);
  m_pressing = false;
  m_dragging = false;
}

} // namespace Frontier
