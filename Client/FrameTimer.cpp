#include "pch.h"

#include "FrameTimer.h"

#include <algorithm>

namespace Neuron
{

void FrameTimer::Add(std::uint64_t _microseconds) noexcept
{
  m_frames[m_count % WINDOW_FRAMES] = _microseconds;
  ++m_count;
}

void FrameTimer::Clear() noexcept
{
  m_frames.fill(0);
  m_count = 0;
}

std::uint64_t FrameTimer::MeanMicroseconds() const noexcept
{
  const std::size_t count = Count();
  if (count == 0)
  {
    return 0;
  }
  std::uint64_t total = 0;
  for (std::size_t index = 0; index < count; ++index)
  {
    total += m_frames[index];
  }
  return total / count;
}

std::uint64_t FrameTimer::PercentileMicroseconds(std::uint32_t _percent) const noexcept
{
  const std::size_t count = Count();
  if (count == 0)
  {
    return 0;
  }
  // Sorted on a copy: the ring has to keep its order, because the order is what makes the oldest
  // frame the one the next Add overwrites.
  std::array<std::uint64_t, WINDOW_FRAMES> sorted{};
  std::copy_n(m_frames.begin(), count, sorted.begin());
  std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(count));
  const std::uint32_t percent = std::min<std::uint32_t>(_percent, 100);
  // Nearest rank, one-based: ceil(percent * n / 100), and at least the first.
  const std::uint64_t rank = (static_cast<std::uint64_t>(percent) * count + 99) / 100;
  const std::size_t index = rank == 0 ? 0 : static_cast<std::size_t>(rank - 1);
  return sorted[std::min(index, count - 1)];
}

} // namespace Neuron
