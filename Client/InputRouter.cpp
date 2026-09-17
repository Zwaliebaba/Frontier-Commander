#include "pch.h"

#include "InputRouter.h"

#include <algorithm>
#include <utility>

namespace Neuron
{

void InputRouter::AddSink(InputSink* _sink)
{
  if (_sink != nullptr)
  {
    m_sinks.push_back(_sink);
  }
}

void InputRouter::RemoveSink(InputSink* _sink)
{
  std::erase(m_sinks, _sink);
}

void InputRouter::Dispatch(std::span<const InputEvent> _events)
{
  for (const InputEvent& event : _events)
  {
    // The key a character belongs to is the last one pressed, because WM_CHAR carries none.
    if (event.kind == InputEventKind::KeyDown)
    {
      m_lastKeyDown = event.key;
    }
    InputDisposition disposition = InputDisposition::Ignored;
    for (InputSink* sink : m_sinks)
    {
      if (sink->OnInputEvent(event) == InputDisposition::Consumed)
      {
        disposition = InputDisposition::Consumed;
        break;
      }
    }
    if (event.kind == InputEventKind::KeyUp && m_maskedKeys[event.key])
    {
      // The release is still masked this frame, and the mask clears once the frame has read it.
      m_releasedKeys[event.key] = true;
    }
    if (disposition != InputDisposition::Consumed)
    {
      continue;
    }
    switch (event.kind)
    {
    case InputEventKind::KeyDown:
      m_maskedKeys[event.key] = true;
      break;
    case InputEventKind::Character:
      if (m_lastKeyDown >= 0)
      {
        m_maskedKeys[static_cast<std::size_t>(m_lastKeyDown)] = true;
      }
      break;
    case InputEventKind::MouseButtonDown:
    case InputEventKind::MouseButtonUp:
      if (IndexOf(event.button) < MOUSE_BUTTON_COUNT)
      {
        m_maskedButtonEdges[IndexOf(event.button)] = true;
      }
      break;
    case InputEventKind::KeyUp:
    case InputEventKind::MouseMove:
    case InputEventKind::MouseRawMove:
    case InputEventKind::Wheel:
    case InputEventKind::FocusLost:
      // A release, movement and the wheel are not masked: no edge of theirs is the interface's to
      // hide, and a release a sink took belongs to the press already masked.
      break;
    }
  }
}

void InputRouter::Mask(FrameInput& _view) noexcept
{
  for (std::size_t key = 0; key < KEY_COUNT; ++key)
  {
    if (m_maskedKeys[key])
    {
      _view.keysHeld[key] = false;
      _view.keyEdges[key] = 0;
    }
    if (m_releasedKeys[key])
    {
      m_maskedKeys[key] = false;
      m_releasedKeys[key] = false;
    }
  }
  for (std::size_t button = 0; button < MOUSE_BUTTON_COUNT; ++button)
  {
    if (m_maskedButtonEdges[button])
    {
      _view.buttonEdges[button] = 0;
      m_maskedButtonEdges[button] = false;
    }
  }
}

InputSubscription InputRouter::Subscribe(InputTrigger _trigger, std::function<void()> _handler)
{
  if (!_handler)
  {
    return {};
  }
  Compact();
  const std::uint32_t id = m_nextSubscriptionId;
  ++m_nextSubscriptionId;
  m_subscriptions.push_back(Subscription{id, _trigger, std::move(_handler), false});
  return {this, id};
}

void InputRouter::Unsubscribe(std::uint32_t _id) noexcept
{
  // Marked rather than erased, so that a handle's destructor never touches the vector's layout;
  // Subscribe and FireSubscriptions compact the list when they run.
  for (Subscription& subscription : m_subscriptions)
  {
    if (subscription.id == _id)
    {
      subscription.canceled = true;
      subscription.handler = nullptr;
    }
  }
}

std::size_t InputRouter::SubscriptionCount() const noexcept
{
  std::size_t count = 0;
  for (const Subscription& subscription : m_subscriptions)
  {
    if (!subscription.canceled)
    {
      ++count;
    }
  }
  return count;
}

void InputRouter::FireSubscriptions(const FrameInput& _view)
{
  Compact();
  if (m_subscriptions.empty())
  {
    return;
  }
  // Decided before any handler runs, because a handler may cancel a subscription (its own, from
  // the object it destroys) or add one, and neither may change what this frame fires.
  std::vector<std::uint32_t> firing;
  for (const Subscription& subscription : m_subscriptions)
  {
    const std::int8_t edge = _view.keyEdges[subscription.trigger.key];
    const bool fires = subscription.trigger.edge == InputEdge::Pressed ? edge > 0 : edge < 0;
    if (fires)
    {
      firing.push_back(subscription.id);
    }
  }
  for (const std::uint32_t id : firing)
  {
    const auto found = std::find_if(m_subscriptions.begin(), m_subscriptions.end(),
                                    [id](const Subscription& _subscription) { return _subscription.id == id && !_subscription.canceled; });
    if (found == m_subscriptions.end())
    {
      continue; // an earlier handler canceled it
    }
    // A copy, because the common handler closes the window that owns the handle, which cancels the
    // subscription the call is executing inside; the copy outlives that.
    const std::function<void()> handler = found->handler;
    handler();
  }
}

void InputRouter::Compact()
{
  std::erase_if(m_subscriptions, [](const Subscription& _subscription) { return _subscription.canceled; });
}

} // namespace Neuron
