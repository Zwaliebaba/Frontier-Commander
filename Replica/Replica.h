#pragma once

#include "DesignStore.h"
#include "Interpolation.h"
#include "ReplicaObject.h"

#include "Client.h"
#include "Messages.h"
#include "Records.h"

#include "FogGrid.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

// The client's world (TechnicalDesign.md §2, §5.3): what this commander can see, built by applying
// the frames Net's Client hands over, and nothing else. It is a FAITHFUL READING OF THE HOST'S
// INTEREST SET and not a second simulation - it holds no rules, advances no state between frames,
// and answers no question the host did not send it the answer to.
//
// That is what makes the convergence test of m1-vertical-slice/R1 possible at all: at every frame
// the client has acknowledged, this object equals what the host encoded for that seat, object for
// object. A replica that guessed anything - a device's next position, a structure's hit points
// while it is out of sight - would drift, and the test would have to say by how much rather than
// that it does not.
//
// IT IS A FrameSink. Net's Client owns the conversation and Replica owns the world, and the seam
// between them is one virtual call with a Frame in it.

namespace Frontier
{

class Replica : public FrameSink
{
public:
  /// The client whose frames this replica is built from. It must outlive the replica.
  ///
  /// THE REPLICA STARTS ITSELF, and that is a deliberate seam rather than a convenience. It needs
  /// the seat and the landscape's size before it can apply anything, and both arrive in the
  /// JoinAccepted that Net's Client reads inside the very same Advance that then delivers the first
  /// frame (Net/Client.cpp) - so there is no moment between the two for a caller to act in. A
  /// replica started by hand after that Advance would therefore be started AFTER its first full
  /// frame had already been applied, and would clear it: an ordering trap with no signal, which the
  /// convergence test of this task fell into before this constructor existed.
  /// NOT noexcept, although it does nothing but take a pointer: the members it default-constructs
  /// are four maps, three vectors and a DesignStore, and every one of those can allocate. A
  /// constructor that promised otherwise would be a promise the compiler turns into std::terminate
  /// the first time a client runs out of memory (clang-tidy's bugprone-exception-escape, which is
  /// how this was found).
  explicit Replica(const Client& _client)
    : m_client(&_client)
  {
  }

  /// Applies one frame. A FULL FRAME CLEARS FIRST and re-reads the seat and the landscape: a frame
  /// with no baseline is everything the commander can see sent whole (Net/Messages.h), which is
  /// what a joining or rejoining client gets, so anything held from before it is by definition
  /// stale - and a rejoin may land in a different seat. A delta is applied on top of what is there.
  void Apply(const Frame& _frame) override;

  [[nodiscard]] std::uint8_t Seat() const noexcept
  {
    return m_seat;
  }

  /// The simulation tick of the newest frame applied, which is the far end of the timeline
  /// Interpolation.h draws behind.
  [[nodiscard]] std::uint32_t NewestTick() const noexcept
  {
    return m_newestTick;
  }

  [[nodiscard]] std::uint32_t NewestSequence() const noexcept
  {
    return m_newestSequence;
  }

  /// The render time INTERPOLATION_DELAY_TICKS behind the newest frame, for a caller that draws
  /// exactly when a frame lands. A loop with its own wall clock drives the delay itself and passes
  /// its own time to Evaluate; this is the floor of that, and what the tests draw at.
  [[nodiscard]] std::int64_t RenderTimeAtNewestFrame() const noexcept;

  [[nodiscard]] const std::map<std::uint32_t, ReplicaDevice>& Devices() const noexcept
  {
    return m_devices;
  }

  [[nodiscard]] const std::map<std::uint32_t, ReplicaStructure>& Structures() const noexcept
  {
    return m_structures;
  }

  [[nodiscard]] const std::map<std::uint32_t, ReplicaWreck>& Wrecks() const noexcept
  {
    return m_wrecks;
  }

  [[nodiscard]] const std::map<std::uint32_t, ReplicaFeature>& Features() const noexcept
  {
    return m_features;
  }

  [[nodiscard]] const DesignStore& Designs() const noexcept
  {
    return m_designs;
  }

  /// This commander's own state as the newest frame carried it: power, research, caps, where he
  /// stands, and the last order refused (m1-vertical-slice/N4).
  [[nodiscard]] const SeatState& Own() const noexcept
  {
    return m_own;
  }

  /// The commander's fog, cell by cell, row major. The fog pass and the minimap read this same
  /// grid, so the two can never disagree (TechnicalDesign.md §6.3).
  [[nodiscard]] std::span<const FogState> Fog() const noexcept
  {
    return m_fog;
  }

  [[nodiscard]] std::uint32_t FogCellsPerSide() const noexcept
  {
    return m_fogCellsPerSide;
  }

  /// The rows of the fog that the newest frame changed, ascending and without repeats. Emptied and
  /// refilled by every Apply, because the render view carries "which rows" rather than making the
  /// client diff a megabyte to recover it (Core/RenderView.h).
  [[nodiscard]] std::span<const std::uint32_t> ChangedFogRows() const noexcept
  {
    return m_changedFogRows;
  }

  /// What happened in the interval the newest frame covered: shots, impacts, deaths. They are the
  /// frame's own and are replaced by every Apply, because an event is a thing that happened and not
  /// a thing that is - a client that missed a frame missed its events, which is the bargain
  /// TechnicalDesign.md §5.3 makes for them.
  [[nodiscard]] std::span<const Event> Events() const noexcept
  {
    return m_events;
  }

  /// Every object gone, for a caller that wants to count what it holds.
  [[nodiscard]] std::size_t ObjectCount() const noexcept
  {
    return m_devices.size() + m_structures.size() + m_wrecks.size() + m_features.size();
  }

private:
  void Clear();
  void Adopt();
  void ApplyFog(const Frame& _frame);

  const Client* m_client;
  std::uint8_t m_seat = 0;
  std::uint32_t m_fogCellsPerSide = 0;
  std::uint32_t m_newestTick = 0;
  /// The tick of the frame applied BEFORE the newest one. It is what says where a device that sent
  /// no change was standing: see the note in Apply, which is the whole reason this is kept.
  std::uint32_t m_previousTick = 0;
  std::uint32_t m_newestSequence = NO_BASELINE;

  // Keyed by id and ordered by it. ORDERED ON PURPOSE: the host builds its interest set as ids in
  // ascending order so that two hosts hold it one way (Net/Interest.h), and a replica that walks
  // its objects in the same order is one a test can compare against it without sorting either side.
  std::map<std::uint32_t, ReplicaDevice> m_devices;
  std::map<std::uint32_t, ReplicaStructure> m_structures;
  std::map<std::uint32_t, ReplicaWreck> m_wrecks;
  std::map<std::uint32_t, ReplicaFeature> m_features;

  DesignStore m_designs;
  SeatState m_own{};

  std::vector<FogState> m_fog;
  std::vector<std::uint32_t> m_changedFogRows;
  std::vector<Event> m_events;
};

} // namespace Frontier
