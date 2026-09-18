#pragma once

#include "HeightDelta.h"
#include "Landscape.h"
#include "MatchSettings.h"
#include "Order.h"
#include "OrderQueue.h"
#include "Seat.h"
#include "World.h"

#include "ContentTree.h"

#include "Assertion.h"
#include "Random.h"

#include <cstdint>
#include <span>
#include <vector>

// The simulation (TechnicalDesign.md §4): one object, advanced one tick at a time by the host and
// by nothing else, whose every input is an order and whose every output is its state. Integers
// only (§4.1, ADR-002): nothing under Sim/ names a floating-point type, and the tick is the clock.
// Advance() is the fixed stage order of §4.8, written once as fourteen member functions; a stage
// whose system does not exist yet is an empty function that keeps its place.

namespace Frontier
{

class Snapshot;

class Sim
{
public:
  /// A match at tick 0: the seats from the lobby, the simulation Random seeded from the match seed,
  /// and the tables every system reads (OpenQuestions.md Q20, owner 2026-09-18). The tree is held
  /// by reference and never written: ADR-006 has one process hold one tree that nothing writes to
  /// after loading, so the simulation reads rows and owns none. **The tree must outlive the Sim**,
  /// which is why binding a temporary is deleted rather than left to be discovered at runtime.
  Sim(const MatchSettings& _settings, const ContentTree& _content);
  Sim(const MatchSettings& _settings, ContentTree&& _content) = delete;

  /// The tables this match is played by. Never null.
  [[nodiscard]] const ContentTree& Content() const noexcept
  {
    return *m_content;
  }

  /// Generates the landscape from its definition: the heightfield the systems of M1 read. False,
  /// with no landscape, for a definition the generator refuses.
  [[nodiscard]] bool CreateLandscape(const LandscapeDefinition& _definition);

  /// Applies a height delta over the base (a flatten under a structure); false when the
  /// rectangle is refused. Until the construction system of M1 owns it, the host calls it.
  [[nodiscard]] bool FlattenTerrain(const HeightDelta& _delta);

  [[nodiscard]] const Landscape& Terrain() const noexcept
  {
    return m_landscape;
  }

  /// Enqueues an order. One for a tick already advanced is moved to the next tick, so that a late
  /// order is applied rather than lost, and the queue keeps its arrival order.
  void Submit(Order _order);

  /// One tick: the fourteen stages of TechnicalDesign.md §4.8, in order. The tick counter and the
  /// simulation Random advance here and nowhere else.
  void Advance();

  [[nodiscard]] std::uint32_t Tick() const noexcept
  {
    return m_tick;
  }

  /// The hash stage 13 computed in the last Advance; 0 before the first.
  [[nodiscard]] std::uint64_t Hash() const noexcept
  {
    return m_hash;
  }

  /// The stage-13 computation over the state as it is now: the tick, the Random state, every seat,
  /// the landscape's definition and deltas, and the match's outcome, in that order (ADR-002). The pending order queue is not state and is
  /// left out, so that a match fed its orders early hashes as one fed them on time.
  [[nodiscard]] std::uint64_t ComputeHash() const noexcept;

  [[nodiscard]] const MatchSettings& Settings() const noexcept
  {
    return m_settings;
  }

  [[nodiscard]] std::span<const Seat> Seats() const noexcept
  {
    return m_seats;
  }

  /// Every device, structure, projectile, feature and wreck of the match (TechnicalDesign.md §4.3).
  [[nodiscard]] const World& Objects() const noexcept
  {
    return m_world;
  }

  /// The mutable world and a mutable seat exist for the same reason FlattenTerrain does: the
  /// systems that will own them are S2 to S11, and until they exist the host and the tests are
  /// what put a match into a state worth hashing. Every stage of Advance reaches m_world and
  /// m_seats directly, so these two narrow to nothing once the systems arrive.
  [[nodiscard]] World& Objects() noexcept
  {
    return m_world;
  }
  [[nodiscard]] Seat& SeatAt(std::uint8_t _seat) noexcept
  {
    FRONTIER_ASSERT(_seat < m_seats.size());
    return m_seats[_seat];
  }

  [[nodiscard]] const Neuron::Random& Stream() const noexcept
  {
    return m_random;
  }

  [[nodiscard]] const OrderQueue& Orders() const noexcept
  {
    return m_orders;
  }

  [[nodiscard]] std::uint32_t AppliedOrders() const noexcept
  {
    return m_appliedOrders;
  }

  /// Orders that failed validation: a seat outside the match or empty or defeated, or a kind that
  /// names objects while no system exists to own them.
  [[nodiscard]] std::uint32_t DroppedOrders() const noexcept
  {
    return m_droppedOrders;
  }

  [[nodiscard]] bool Finished() const noexcept
  {
    return m_finished;
  }

  /// The alliance that won, or NO_ALLIANCE while the match runs or when it ended in a draw.
  [[nodiscard]] std::uint8_t WinningAlliance() const noexcept
  {
    return m_winningAlliance;
  }

  /// Stage 14: true after every second tick, which is when Net publishes (TechnicalDesign.md §4.8).
  [[nodiscard]] bool PublishDue() const noexcept
  {
    return m_publishDue;
  }

private:
  friend class Snapshot;

  // The fourteen stages of TechnicalDesign.md §4.8, in its order and under its names.
  void ApplyOrders();         // 1  this tick's orders, in seat order then arrival order
  void AdvanceEconomy();      // 2  extraction, stockpile, caps
  void AdvanceResearch();     // 3  advance, complete, apply upgrades
  void AdvanceProduction();   // 4  factories advance, spawn devices
  void AdvanceConstruction(); // 5  builders advance structures and modules
  void AdvanceMovement();     // 6  paths, steering, terrain and obstruction
  void RefreshVisibility();   // 7  the budgeted refresh
  void ResolveTargeting();    // 8  acquire, roll, spawn projectiles, direct hits
  void AdvanceProjectiles();  // 9  advance, indirect impacts, splash
  void ResolveDamage();       // 10 damage, destruction, wrecks, experience
  void AdvanceAiSeats();      // 11 observe, decide, enqueue orders for a later tick
  void CheckVictory();        // 12 the lobby's condition
  void HashState();           // 13 the digest of everything above
  void MarkPublish();         // 14 every second tick: Net publishes, reading the state

  /// Validates and applies one order; false when it is dropped.
  [[nodiscard]] bool Apply(const Order& _order);

  MatchSettings m_settings;
  const ContentTree* m_content;
  std::uint32_t m_tick = 0;
  Neuron::Random m_random;
  std::vector<Seat> m_seats;
  World m_world;
  Landscape m_landscape;
  OrderQueue m_orders;
  std::vector<Order> m_thisTick; ///< Stage 1's scratch; empty between ticks and never state.
  std::uint32_t m_lastRoll = 0;  ///< Stage 8's draw, kept so that the hash covers the stream.
  std::uint32_t m_appliedOrders = 0;
  std::uint32_t m_droppedOrders = 0;
  bool m_finished = false;
  std::uint8_t m_winningAlliance = NO_ALLIANCE;
  bool m_publishDue = false;
  std::uint64_t m_hash = 0;
};

} // namespace Frontier
