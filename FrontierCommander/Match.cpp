#include "pch.h"

#include "Match.h"

#include "FixedPoint.h"
#include "Log.h"

#include <algorithm>

namespace Frontier
{

namespace
{

constexpr std::chrono::nanoseconds TICK_DURATION{std::chrono::milliseconds{Neuron::TICK_MILLISECONDS}};

/// A liveness counter runs every tick it owes: one that ran four at a time would make a timeout
/// late by however long the frame hitched, which is the opposite of what a timeout is for. The
/// ceiling is high for the same reason - the debt is the measure, not something to write off.
inline constexpr std::uint32_t LIVENESS_MAX_PER_PASS = 1000;
inline constexpr std::uint32_t LIVENESS_MAX_DEBT = 1000;

} // namespace

Match::Match(const ContentTree& _content, const MatchSettings& _settings, std::uint64_t _contentHash, std::uint32_t _chunkCells)
  : m_content(&_content),
    m_sim(_settings, _content),
    m_network(0),
    m_host(m_sim, m_network, _contentHash),
    m_clientEnd(m_network.Connect()),
    m_client(m_clientEnd, LOCAL_LIVENESS, 0),
    m_replica(m_client),
    m_composer(_content),
    m_builder(_content, m_composer, RenderViewSettings{}),
    m_liveness(TICK_DURATION, LIVENESS_MAX_PER_PASS, LIVENESS_MAX_DEBT),
    m_contentHash(_contentHash),
    m_chunkCells(_chunkCells)
{
  // THE SEED IS ZERO AND THE FAULTS ARE OFF. A loopback with no faults set drops nothing, so the
  // seed never draws; it is named rather than left to a default so that nobody later reads a
  // dropped datagram in single-player as a network problem.
}

Match::~Match()
{
  Stop();
}

bool Match::Start(const LandscapeDefinition& _landscape, std::string_view _commanderName)
{
  if (!m_sim.CreateLandscape(_landscape))
  {
    Neuron::Log::Write(Neuron::LogLevel::Error, "match: the landscape was refused; there is no match to play");
    return false;
  }
  // The builder needs the landscape's size before it can turn a footprint into a chunk, and the
  // landscape is only known now - Sim is what validates and holds it.
  RenderViewSettings settings{};
  settings.cellsPerSide = m_sim.Terrain().CellsPerSide();
  settings.chunkCells = m_chunkCells;
  m_builder.Settings(settings);

  // THE HOST STARTS BEFORE THE JOIN, and it has to: the join is a datagram, and a datagram is
  // answered by a host that is running. Starting the thread last would leave the first join to sit
  // in a queue until the second pass, which works and is a frame of nothing for no reason.
  m_host.Start();
  m_client.SendJoin(m_contentHash, 1, _commanderName, 0);
  return true;
}

void Match::Stop() noexcept
{
  m_host.Stop();
}

void Match::Select(std::span<const std::uint32_t> _ids)
{
  m_selected.assign(_ids.begin(), _ids.end());
  // Ascending, so that the render view's flags and picking's answers are compared against one
  // order rather than whichever the caller happened to hand over.
  std::sort(m_selected.begin(), m_selected.end());
  m_selected.erase(std::unique(m_selected.begin(), m_selected.end()), m_selected.end());
}

void Match::Advance(std::chrono::nanoseconds _elapsed)
{
  // The liveness clock. Not the simulation's: nothing here advances the world.
  m_livenessTick += m_liveness.Take(_elapsed).ticks;

  // 2. Drain the network: frames in, orders and acks out. One Poll serves this end; the host thread
  //    polls its own (Net/Client.h says the transport is the caller's to poll).
  m_clientEnd.Poll();

  // 3. Apply what arrived to the replica. Client::Advance reads everything that has come in, hands
  //    each frame to the replica, and sends the orders and the acknowledgement that are due.
  m_client.Advance(m_livenessTick, m_replica);

  // 4 and 5. Interpolate a hundred milliseconds behind the newest frame, and build the render view
  //    from that moment. RenderTimeAtNewestFrame is the replica's own timeline - the host's tick
  //    numbers arriving late - and the interpolation delay is already inside it.
  m_builder.Build(m_replica, m_replica.RenderTimeAtNewestFrame(), m_selected, m_view, m_picks);
}

} // namespace Frontier
