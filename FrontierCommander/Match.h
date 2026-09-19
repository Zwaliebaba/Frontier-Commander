#pragma once

#include "LocalHost.h"

#include "Client.h"
#include "ContentTree.h"
#include "ModelComposer.h"
#include "Picking.h"
#include "Replica.h"
#include "RenderViewBuilder.h"

#include "Liveness.h"
#include "RenderView.h"
#include "TickPacer.h"

#include <chrono>
#include <cstdint>
#include <string_view>

// One match as the game holds it (TechnicalDesign.md §3; m1-vertical-slice/G1a): the host thread on
// one side, this commander's client and replica on the other, and the loopback between them.
//
// IT IS BOTH LOOPS' MEETING POINT AND NEITHER LOOP'S. The host loop is LocalHost's, on its own
// thread. Of the client loop's seven steps this object runs 2 to 5 - drain the network, apply
// frames, interpolate, build the render view - and App runs 1, 6 and 7, the window's messages, the
// input routing and the drawing, because those need a window and a device and these do not.
//
// WHICH MEANS IT BUILDS AND RUNS OUTSIDE CI. Sim, Net, Replica, Content and Core, and not one
// Windows or Direct3D header. G1a's notes say the executable is CI's alone and that is true of App;
// it is not true of the two files that hold the match itself, and keeping it that way is worth more
// than the convenience of putting a device in here.
//
// THE CLIENT HAS NO SIMULATION CLOCK, AND STILL NEEDS A CLOCK. §3 is explicit that the client "has
// no simulation clock; it has the replica's timeline, which is the host's tick numbers arriving
// late" - and Net's Client takes a tick, for heartbeats and timeouts (Core/Liveness.h). That is a
// LIVENESS clock and not a simulation one, and it has to come from wall time rather than from the
// replica: a host that stopped sending stops advancing the replica's tick, which is exactly the
// moment a timeout has to fire. Nothing here advances the world; the world is the host's.

namespace Frontier
{

/// The landscape a match of M1 is played on, until M2's lobby chooses one.
inline constexpr const char* SLICE_LANDSCAPE = "Slice";

/// Heartbeats and timeouts for a loopback match. Generous, because the "network" here cannot lose a
/// datagram and a client that timed out against a host in its own process would be reporting that
/// the machine had stalled - which is what LocalHost's SlowedTicks() is for saying properly.
inline constexpr Neuron::LivenessSettings LOCAL_LIVENESS{20, 200, 400};

class Match
{
public:
  /// _content outlives the match; Sim holds a reference to it. _settings is the lobby's, which in
  /// M1 is the fixed pair of seats G1a's acceptance names. _chunkCells is Client/TerrainChunk.h's
  /// CHUNK_CELLS, PASSED IN rather than included: it is a rendering decision, and reaching for it
  /// here would drag Direct3D into the one half of this executable that does not need it.
  Match(const ContentTree& _content, const MatchSettings& _settings, std::uint64_t _contentHash, std::uint32_t _chunkCells);
  ~Match();
  Match(const Match&) = delete;
  Match& operator=(const Match&) = delete;

  /// Creates the landscape, starts the host thread and sends the join. False, with the reason
  /// logged, when the landscape is refused - which is a content fault and belongs on the way up
  /// rather than on a thread nobody is watching.
  [[nodiscard]] bool Start(const LandscapeDefinition& _landscape, std::string_view _commanderName);

  /// Stops the host thread and joins it. Called by the destructor.
  void Stop() noexcept;

  /// Steps 2 to 5 of the client loop, once. _elapsed is the wall time since the last call, which
  /// drives the liveness clock and nothing else.
  void Advance(std::chrono::nanoseconds _elapsed);

  [[nodiscard]] bool Playing() const noexcept
  {
    return m_client.State() == ClientState::Playing;
  }

  /// Queues one of this commander's orders. False when the unacknowledged window is full.
  [[nodiscard]] bool Submit(const Order& _order)
  {
    return m_client.Submit(_order);
  }

  /// What to draw, rebuilt by every Advance.
  [[nodiscard]] const Neuron::RenderView& View() const noexcept
  {
    return m_view;
  }

  /// What a click can land on, rebuilt by every Advance.
  [[nodiscard]] const PickSet& Picks() const noexcept
  {
    return m_picks;
  }

  [[nodiscard]] const Replica& Commander() const noexcept
  {
    return m_replica;
  }

  /// THE LANDSCAPE THIS COMMANDER GENERATES, and not the host's. It is built from the definition
  /// the host sent with the join, so it carries no flatten deltas at all: the host's own landscape
  /// is flattened under every structure in the match, including the bases this commander has never
  /// scouted, and TechnicalDesign.md §5.2 is explicit that "the terrain under an unscouted base is
  /// not public". A local match is where that boundary is easiest to lose - the host's Landscape is
  /// one member away - and losing it here would be a wallhack that shipped.
  ///
  /// Empty until the join is accepted, because the definition arrives with it. TerrainReady() says.
  [[nodiscard]] const Landscape& Terrain() const noexcept
  {
    return m_terrain;
  }

  [[nodiscard]] bool TerrainReady() const noexcept
  {
    return m_terrain.Created();
  }

  [[nodiscard]] const LocalHost& HostSide() const noexcept
  {
    return m_host;
  }

  /// The ids the commander has selected. Held here because the render view's selection flag and
  /// picking's answer have to agree, and one owner is how they do.
  void Select(std::span<const std::uint32_t> _ids);
  [[nodiscard]] std::span<const std::uint32_t> Selected() const noexcept
  {
    return m_selected;
  }

  /// The liveness tick this client is on. Not the simulation's, and not the replica's.
  [[nodiscard]] std::uint32_t LivenessTick() const noexcept
  {
    return m_livenessTick;
  }

private:
  const ContentTree* m_content;
  Sim m_sim;
  Neuron::LoopbackTransport m_network;
  LocalHost m_host;
  Neuron::Transport& m_clientEnd;
  Client m_client;
  Replica m_replica;
  ModelComposer m_composer;
  RenderViewBuilder m_builder;
  /// The client's own, generated from the definition the join carried. Never the host's.
  Landscape m_terrain;
  Neuron::RenderView m_view;
  PickSet m_picks;
  std::vector<std::uint32_t> m_selected;
  /// The liveness clock: wall time into ticks, for heartbeats and timeouts. Core's pacer, so that
  /// the one place wall time becomes a tick count is one tested piece of arithmetic wherever it is
  /// used - here it is allowed to run every tick it owes, because a liveness counter that ran four
  /// at a time would make a timeout late by however long the frame hitched.
  Neuron::TickPacer m_liveness;
  std::uint32_t m_livenessTick = 0;
  std::uint64_t m_contentHash;
  std::uint32_t m_chunkCells;
};

} // namespace Frontier
