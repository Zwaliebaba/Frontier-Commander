#include "pch.h"

#include "LocalHost.h"

#include "FixedPoint.h"
#include "Log.h"

#include <string>

namespace Frontier
{

namespace
{

/// A tick as a duration. The match's own clock, and the only rate in the process.
constexpr std::chrono::nanoseconds TICK_DURATION{std::chrono::milliseconds{Neuron::TICK_MILLISECONDS}};

} // namespace

LocalHost::LocalHost(Sim& _sim, Neuron::LoopbackTransport& _network, std::uint64_t _contentHash)
  : m_sim(&_sim),
    m_network(&_network),
    m_host(_sim, _network.Host(), _contentHash, _sim.Tick()),
    m_pacer(TICK_DURATION, MAX_TICKS_PER_PASS, MAX_DEBT_TICKS)
{
  m_tick.store(_sim.Tick(), std::memory_order_relaxed);
}

LocalHost::~LocalHost()
{
  Stop();
}

void LocalHost::Start()
{
  if (m_thread.joinable())
  {
    return;
  }
  m_stop.store(false, std::memory_order_relaxed);
  m_thread = std::thread([this] { Loop(); });
}

void LocalHost::Stop() noexcept
{
  m_stop.store(true, std::memory_order_relaxed);
  if (m_thread.joinable())
  {
    m_thread.join();
  }
}

void LocalHost::Loop()
{
  // WALL TIME IS READ HERE AND NOWHERE ELSE. steady_clock rather than system_clock: a match must not
  // run backwards because somebody's clock synchronised, and a host that ran a thousand ticks in one
  // pass because the wall clock jumped an hour is a host that hangs.
  std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
  while (!m_stop.load(std::memory_order_relaxed))
  {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const std::chrono::nanoseconds elapsed = now - last;
    last = now;
    Pass(elapsed);
    if (m_pacer.Owed() < TICK_DURATION)
    {
      // Nothing owed: give the core back rather than spin. The sleep is shorter than a tick, so the
      // next one is never late by more than a fraction of itself.
      std::this_thread::sleep_for(IDLE_PASS);
    }
  }
}

void LocalHost::Pass(std::chrono::nanoseconds _elapsed)
{
  // 1. Drain the network: orders in, acks in (§3's step 1). The host's Poll both delivers what has
  //    arrived and sends what the last pass queued, so it comes before the ticks rather than after.
  m_network->Host().Poll();

  // 2. While the match clock trails wall time by a tick, run one (§3's step 2). The arithmetic is
  //    Core's TickPacer, which is where the rule it serves can be tested: the match SLOWS when it
  //    falls behind and never skips a tick, because a skipped tick would be a different match.
  if (m_sim->Finished())
  {
    // A finished Sim advances nothing (Sim::Advance returns on m_finished), so counting its debt
    // would report a host falling further and further behind for as long as the window stayed open.
    m_pacer.Forget();
  }
  const Neuron::TickStep step = m_sim->Finished() ? Neuron::TickStep{} : m_pacer.Take(_elapsed);
  for (std::uint32_t index = 0; index < step.ticks; ++index)
  {
    m_sim->Advance();
  }
  if (step.ticks > 0)
  {
    m_tick.store(m_sim->Tick(), std::memory_order_relaxed);
  }
  if (step.givenUp > 0)
  {
    m_slowedTicks.fetch_add(step.givenUp, std::memory_order_relaxed);
    Neuron::Log::Write(Neuron::LogLevel::Warning,
                       "host: the match clock gave up " + std::to_string(step.givenUp) + " tick(s) of wall time; it is running slow");
  }
  m_behindTicks.store(step.behind, std::memory_order_relaxed);

  // 3 and 4. Publish to each client on its own schedule, and the heartbeats and timeouts with it -
  //    Host::Advance is all of that, and it is the half of the loop that knows the protocol.
  m_host.Advance(m_sim->Tick());
}

} // namespace Frontier
