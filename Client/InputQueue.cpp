#include "pch.h"

#include "InputQueue.h"

#include <algorithm>

namespace Neuron
{

InputQueue::InputQueue()
{
  m_events.reserve(CAPACITY);
}

void InputQueue::Push(const InputEvent& _event)
{
  if (m_events.size() >= CAPACITY)
  {
    ++m_dropped;
    return;
  }
  m_events.push_back(_event);
}

void InputQueue::Erase(std::size_t _count)
{
  const std::size_t count = std::min(_count, m_events.size());
  m_events.erase(m_events.begin(), m_events.begin() + static_cast<std::ptrdiff_t>(count));
}

} // namespace Neuron
