#pragma once

#include <chrono>
#include <cstdint>

// Turning elapsed wall time into a number of fixed-rate steps (TechnicalDesign.md §3;
// m1-vertical-slice/G1a). The host loop's step 2 and the only place in the process where a duration
// becomes a tick number (AGENTS.md R16).
//
// IT SLOWS, IT DOES NOT SKIP. §3: a host that falls behind "slows the match clock rather than
// skipping: it reduces ticks per wall second until it catches up". So when the debt runs past what
// this will hold, what is thrown away is WALL TIME and never a step. Every tick the simulation owes
// is still run, later than real time asked for; the match takes longer in real seconds than it says
// and is otherwise identical. A skipped tick would be a DIFFERENT MATCH, and on a simulation whose
// hash both ends of a connection compare, that is not something anybody recovers from.
//
// IN Core AND NOT IN THE EXECUTABLE, and that is TechnicalDesign.md §2's rule rather than a
// preference: "code in an executable cannot be linked into a test DLL, so anything in one that
// deserves a test is code that belongs in a library". Pacing is exactly the kind of arithmetic that
// is wrong only on a machine that stutters, months later, in a way nobody can reproduce - so it is
// here, where Tests/CoreTests can drive it with a clock it makes up. What stays in the executable is
// the thread and the wiring, which have nothing to get wrong.
//
// IT HOLDS NO CLOCK OF ITS OWN. The caller reads wall time and hands over the difference, so a test
// steps it by an exact duration and the capture path (which runs a match as fast as it can rather
// than in real time) hands it a tick's worth per pass and gets exactly one step.

namespace Neuron
{

/// What one pass owes.
struct TickStep
{
  /// How many steps to run now. The caller runs exactly this many.
  std::uint32_t ticks = 0;
  /// Steps of WALL TIME written off this pass because the debt ran past the ceiling. Never steps
  /// skipped: nonzero means the match is running slower than real time, not that it lost anything.
  std::uint32_t givenUp = 0;
  /// What the clock still owes after this pass, in whole steps. Zero means it is keeping up.
  std::uint32_t behind = 0;
};

class TickPacer
{
public:
  /// _step is how long one step represents. _maxPerPass bounds a single pass, so that a long stall
  /// does not advance the world by a second in one burst and publish a frame that jumped - and so
  /// that a machine that cannot keep up at all still returns to its caller. _maxDebt is how far
  /// behind the clock may fall before it starts writing wall time off.
  constexpr TickPacer(std::chrono::nanoseconds _step, std::uint32_t _maxPerPass, std::uint32_t _maxDebt) noexcept
    : m_step(_step.count() > 0 ? _step : std::chrono::nanoseconds{1}),
      m_maxPerPass(_maxPerPass),
      m_maxDebt(_maxDebt)
  {
  }

  /// Adds _elapsed to the debt and says what to run. A negative or zero _elapsed adds nothing,
  /// because a clock that went backwards is a clock fault and not a reason to un-run a tick.
  [[nodiscard]] constexpr TickStep Take(std::chrono::nanoseconds _elapsed) noexcept
  {
    if (_elapsed.count() > 0)
    {
      m_owed += _elapsed;
    }
    TickStep step;
    while (m_owed >= m_step && step.ticks < m_maxPerPass)
    {
      m_owed -= m_step;
      ++step.ticks;
    }
    const std::chrono::nanoseconds ceiling = m_step * m_maxDebt;
    if (m_owed > ceiling)
    {
      step.givenUp = static_cast<std::uint32_t>((m_owed - ceiling) / m_step);
      m_owed = ceiling;
    }
    step.behind = static_cast<std::uint32_t>(m_owed / m_step);
    return step;
  }

  /// Forgets the debt. What a caller does when the thing being stepped has stopped - a finished
  /// match advances nothing, so counting its debt would report a host falling further and further
  /// behind for as long as the window stayed open.
  constexpr void Forget() noexcept
  {
    m_owed = std::chrono::nanoseconds{0};
  }

  [[nodiscard]] constexpr std::chrono::nanoseconds Owed() const noexcept
  {
    return m_owed;
  }

private:
  std::chrono::nanoseconds m_step;
  std::chrono::nanoseconds m_owed{0};
  std::uint32_t m_maxPerPass;
  std::uint32_t m_maxDebt;
};

} // namespace Neuron
