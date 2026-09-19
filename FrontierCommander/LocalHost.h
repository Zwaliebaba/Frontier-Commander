#pragma once

#include "Host.h"
#include "LoopbackTransport.h"
#include "Sim.h"
#include "TickPacer.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

// The host half of a local match, on its own thread (TechnicalDesign.md §3; m1-vertical-slice/G1a).
//
// SINGLE-PLAYER IS A HOST THREAD AND NOT A SPECIAL CASE. §3: "Local play is a host thread in the
// same process talking to the client through the loopback transport, so single-player exercises the
// same code path as a match over the network; nothing is special-cased for one player." The client
// never sees this object; it sees a Neuron::Transport, exactly as it would over UDP. That is why
// the 250 ms from click to movement is the same alone as it is against another commander, and why a
// bug in replication meets a player on the first playable build rather than at M3.
//
// NO WINDOWS, NO DIRECT3D, NOTHING OF THE RENDERER. This file is Sim, Net and Core and a thread,
// which is what lets it be built and run outside CI even though it lives in the executable's
// directory - the one part of G1a that is not CI's alone.
//
// THE AI SEAT IS SIM'S, not this class's. Sim::Advance runs it at stage 11 (Sim.cpp's
// AdvanceAiSeats), so a host that ticks the simulation has already run every AI commander in the
// match. G1a's acceptance names the AI seat because the host thread is what makes it run at all.

namespace Frontier
{

/// How many ticks one pass of the loop may run before it gives the thread back, and how far the
/// match clock may fall behind wall time before it starts writing the debt off. The rule those two
/// numbers serve - the match SLOWS and never skips - is Core/TickPacer.h's, which is where it can
/// be tested; these are this match's settings for it.
inline constexpr std::uint32_t MAX_TICKS_PER_PASS = 4;
inline constexpr std::uint32_t MAX_DEBT_TICKS = 20;

/// How long a pass sleeps when it has nothing owed. Short enough that a tick is never late by more
/// than a fraction of itself, long enough that an idle host is not a spinning core.
inline constexpr std::chrono::milliseconds IDLE_PASS{2};

class LocalHost
{
public:
  /// _sim and _network outlive the host. The landscape is created by the caller before this is
  /// built, because a match with no landscape is not a match and the failure belongs where it can
  /// be reported rather than on a thread nobody is watching.
  LocalHost(Sim& _sim, Neuron::LoopbackTransport& _network, std::uint64_t _contentHash);
  ~LocalHost();
  LocalHost(const LocalHost&) = delete;
  LocalHost& operator=(const LocalHost&) = delete;

  /// Starts the thread. NOT DONE BY THE CONSTRUCTOR: a thread started there can reach a member the
  /// constructor has not finished writing, and the first symptom of that is a corrupt match on
  /// somebody else's machine.
  void Start();

  /// Stops the thread and joins it. Idempotent, and called by the destructor, so a match that
  /// throws on its way up still leaves no thread running.
  void Stop() noexcept;

  [[nodiscard]] bool Running() const noexcept
  {
    return m_thread.joinable();
  }

  /// One pass of the host loop, for a caller that drives it itself: the capture path runs the match
  /// as fast as it can rather than in real time, and a test wants a match that steps when it says
  /// so. _elapsed is the wall time since the last pass; pass a tick's worth to step exactly one.
  /// The thread calls this and nothing else.
  void Pass(std::chrono::nanoseconds _elapsed);

  /// Ticks run since the host started. Read from another thread while the match runs.
  [[nodiscard]] std::uint32_t Tick() const noexcept
  {
    return m_tick.load(std::memory_order_relaxed);
  }

  /// Ticks the match owes wall time right now: zero when it is keeping up. What a lobby would show
  /// as "the host is struggling" (§3 asks for that; M1 has no lobby to show it in, so it is counted
  /// here and read by whoever grows one).
  [[nodiscard]] std::uint32_t BehindTicks() const noexcept
  {
    return m_behindTicks.load(std::memory_order_relaxed);
  }

  /// Ticks the match clock has given up rather than run in real time, over the whole match. Nonzero
  /// means the machine could not keep up and the match ran slow - never that a tick was skipped.
  [[nodiscard]] std::uint32_t SlowedTicks() const noexcept
  {
    return m_slowedTicks.load(std::memory_order_relaxed);
  }

private:
  void Loop();

  Sim* m_sim;
  Neuron::LoopbackTransport* m_network;
  Host m_host;
  /// THE ONLY PLACE WALL TIME AND TICKS MEET (§3, and AGENTS.md R16): nothing else in the process
  /// converts one to the other, so there is one definition of how fast the match runs and one place
  /// to look when it runs wrong. The arithmetic itself is Core's, and tested there.
  Neuron::TickPacer m_pacer;
  std::thread m_thread;
  std::atomic<bool> m_stop{false};
  std::atomic<std::uint32_t> m_tick{0};
  std::atomic<std::uint32_t> m_behindTicks{0};
  std::atomic<std::uint32_t> m_slowedTicks{0};
};

} // namespace Frontier
