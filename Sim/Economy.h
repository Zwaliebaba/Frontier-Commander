#pragma once

#include "Deposit.h"
#include "Landscape.h"
#include "ObjectId.h"
#include "Seat.h"
#include "World.h"

#include "ContentTree.h"

#include <cstdint>
#include <span>
#include <vector>

// The economy of GameDesign.md §4: deposit to extractor to generator, one currency, a stockpile
// with a cap that cannot be parked around, and the two army caps. Stage 2 of the tick
// (TechnicalDesign.md §4.8) and the arithmetic every other system calls when it spends.
//
// WHAT IS STATE AND WHAT IS DERIVED. The stockpile, the cap and the two counts live on the Seat,
// so the hash and the snapshot carry them. Everything this class holds - the deposit index and the
// service assignment - is a function of the landscape's definition and of the world, both of which
// are already hashed and already in the snapshot, so it is rebuilt rather than carried. That is
// what makes it safe for a rejoining client: it computes the same assignment from the same world.
//
// THE SERVICE ASSIGNMENT IS RECOMPUTED EVERY TICK. GameDesign.md §4 says a generator serves the
// four nearest unserved extractors, and the task's acceptance asks for it to be recomputed when a
// generator or extractor is completed or destroyed. Recomputing every tick satisfies that strictly
// and removes the whole class of bug where a system that changes a structure forgets to say so.
// The cost is bounded by the structure cap: a seat may hold 300 structures, so the worst case is
// on the order of 20,000 squared-distance tests a seat a tick, which is integer work an order of
// magnitude under the tick budget of TechnicalDesign.md §3. Caching it needs a profile that says
// so, and a stale assignment that two hosts agree on is a wrong economy rather than a desync,
// which is the harder bug of the two to find.

namespace Frontier
{

/// One extractor and the generator serving it, as the last Advance worked it out.
struct ServedExtractor
{
  ObjectId extractor;
  ObjectId generator;

  [[nodiscard]] constexpr bool operator==(const ServedExtractor&) const noexcept = default;
};

class Economy
{
public:
  /// Rebuilds the deposit index. Called when the landscape is created and when a snapshot restores
  /// one, because the deposits are the definition's and the definition is what those two set.
  void SetLandscape(const Landscape& _landscape);

  [[nodiscard]] const DepositField& Deposits() const noexcept
  {
    return m_deposits;
  }

  /// Stage 2, for every seat: recount what the seat owns, work out which extractors are served,
  /// set the stockpile cap from the standing generators, and credit the tick's income.
  void Advance(const World& _world, std::span<Seat> _seats, const ContentTree& _content);

  /// The assignment the last Advance computed, ascending by extractor id. Empty before the first.
  [[nodiscard]] std::span<const ServedExtractor> Service() const noexcept
  {
    return m_service;
  }

  [[nodiscard]] bool Served(ObjectId _extractor) const noexcept;
  /// The generator serving it, or NO_OBJECT.
  [[nodiscard]] ObjectId ServingGenerator(ObjectId _extractor) const noexcept;

  // ── The transactions (GameDesign.md §4) ───────────────────────────────────────────────────
  // Free of the tick and of the world: they are the arithmetic of spending, which S4, S5 and S6
  // call at the moment they begin or cancel something. They are static because a transaction is a
  // function of the seat and the amount and of nothing this class holds.

  /// Draws the cost when construction or production BEGINS, never when a plan is placed. False,
  /// with the stockpile untouched, when it does not cover the cost.
  [[nodiscard]] static bool Draw(Seat& _seat, std::int32_t _costHundredths) noexcept;

  /// Adds to the stockpile without ever taking it past the cap, and without ever lowering it.
  /// **A stockpile already over the cap is left alone rather than confiscated**: GameDesign.md §4
  /// caps what accumulates ("income accumulates into a stockpile capped at...", "any refund that
  /// would exceed the cap is lost"), and the lobby's own PowerLevel::High starts a commander at
  /// 2,500 against a capless-base cap of 1,000, so a cap that clipped the balance would delete
  /// three fifths of a lobby setting on the first tick.
  static void Credit(Seat& _seat, std::int32_t _amountHundredths) noexcept;

  /// Cancelling refunds the share of the cost not yet built, clipped by the cap like any credit,
  /// so that power cannot be stored in unfinished builds. _buildProgressHundredths is the
  /// structure's, where 10,000 is complete.
  static void RefundCanceled(Seat& _seat, std::int32_t _costHundredths, std::int32_t _buildProgressHundredths) noexcept;

  /// Demolishing a standing structure refunds half its cost; a destroyed one refunds nothing, so
  /// there is no function for that.
  static void RefundDemolished(Seat& _seat, std::int32_t _costHundredths) noexcept;

  /// 1,000 power plus 500 for every completed generator, in hundredths.
  [[nodiscard]] static std::int32_t StockpileCapHundredths(std::uint32_t _standingGenerators) noexcept;

  /// The base cap and what a generator adds, in hundredths (GameDesign.md §4).
  static constexpr std::int32_t BASE_STOCKPILE_CAP_HUNDREDTHS = 100000;
  static constexpr std::int32_t GENERATOR_STOCKPILE_CAP_HUNDREDTHS = 50000;

private:
  /// One standing structure of one seat, reduced to what the assignment needs. Built once a tick
  /// so that the generator loop is over a compact array in ascending id rather than over the
  /// world's five maps.
  struct Site
  {
    ObjectId id;
    std::int64_t centerX; ///< The footprint's centre, in subunits
    std::int64_t centerY;
    std::int32_t powerHundredthsPerTick;
    std::uint32_t serves;      ///< Generators only: how many extractors it may take
    std::int64_t rangeSquared; ///< Generators only, in subunits squared
  };

  void CollectSites(const World& _world, const ContentTree& _content, std::uint8_t _seat);
  /// Serves the collected extractors from the collected generators and returns the tick's yield
  /// in hundredths, which the assignment already knows and a second walk would only rediscover.
  [[nodiscard]] std::int64_t AssignService();

  DepositField m_deposits;
  std::vector<ServedExtractor> m_service; ///< Ascending by extractor id
  std::vector<Site> m_generators;         ///< This seat's, ascending by id; scratch, not state
  std::vector<Site> m_extractors;         ///< This seat's, on a deposit, ascending by id; scratch
};

} // namespace Frontier
