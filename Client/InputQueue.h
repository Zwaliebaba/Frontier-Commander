#pragma once

#include "InputEvent.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Neuron
{

/// The events the window procedure enqueued and a frame has not consumed yet (TechnicalDesign.md
/// §6.5). Bounded, and full drops the newest: dropping the oldest would reorder edges, which the
/// whole arrangement exists to prevent. The capacity is reserved once, so Push never allocates
/// inside the window procedure.
class InputQueue
{
public:
  static constexpr std::size_t CAPACITY = 4096;

  InputQueue();

  void Push(const InputEvent& _event);
  [[nodiscard]] std::span<const InputEvent> Events() const noexcept
  {
    return m_events;
  }
  /// Forgets the first _count events, which a frame consumed.
  void Erase(std::size_t _count);
  [[nodiscard]] std::uint32_t Dropped() const noexcept
  {
    return m_dropped;
  }

private:
  std::vector<InputEvent> m_events;
  std::uint32_t m_dropped = 0;
};

} // namespace Neuron
