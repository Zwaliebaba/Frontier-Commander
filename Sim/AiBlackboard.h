#pragma once

#include "ObjectId.h"

#include "ContentTree.h"
#include "LandscapeDefinition.h"

#include <cstdint>
#include <vector>

// What a scripted commander knows (TechnicalDesign.md §7). An AI seat observes the simulation
// through the SAME visibility grid and ghost store as a human, into this small blackboard, and its
// behaviours read nothing else - which is what makes "the highest difficulty takes a power bonus
// and never vision" (owner, 2026-09-17) a property of the code rather than a promise.
//
// EVERYTHING HERE IS DERIVED, so none of it is state: it is rebuilt from the world and the seat
// every time the seat decides, and neither the hash nor the snapshot carries it. A blackboard that
// were state would be a second copy of the world that two hosts could disagree about.

namespace Frontier
{

class Sim;

/// The threat map is at cluster resolution (TechnicalDesign.md §4.5 and §7): one number a cluster
/// rather than one a cell, because an AI decides where to go and not which cell to stand in.
inline constexpr std::uint32_t THREAT_CLUSTER_CELLS = 16;

/// How far from its base a scripted commander will go for a deposit, in cells. A generator serves
/// within 48 (GameDesign.md §4), so a deposit past 24 cannot share one with the base's own.
inline constexpr std::uint32_t AI_EXPANSION_RANGE_CELLS = 24;

/// What an AI wants at least one of before it builds the next thing. The order is the order the
/// behaviour list builds them in, which is also what a reader checks the list against.
enum class AiNeed : std::uint8_t
{
  CommandPost,
  Extractor,
  Generator,
  Factory,
  Lab
};

inline constexpr std::uint8_t AI_NEED_COUNT = 5;

struct AiBlackboard
{
  std::uint8_t seat = 0;

  // Its own economy, counted from what it owns.
  std::int32_t powerHundredths = 0;
  std::int32_t stockpileCapHundredths = 0;
  std::array<std::uint32_t, AI_NEED_COUNT> standing{}; ///< How many of each it holds, built or building
  std::uint32_t unservedExtractors = 0;
  std::vector<ObjectId> idleFactories; ///< Standing factories with nothing queued
  /// Its own structures that are placed and not finished, ascending by id, and where they are.
  /// A commander that places faster than its builders can build spreads one truck over six sites
  /// and finishes none of them, so this is what the behaviour list gates new placements on.
  std::vector<ObjectId> unfinished;
  std::vector<CellPosition> unfinishedPlaces;

  // Its army, by what a device carries rather than by design index: a design is replaced and the
  // composition it was counted under would go with it.
  std::uint32_t builders = 0;
  std::uint32_t fighters = 0;
  std::vector<ObjectId> idleFighters; ///< Fighters standing still, which is what a group is drawn from
  std::vector<ObjectId> builderDevices;

  // What it knows of the enemy, from its own fog and its own ghost store.
  std::vector<ObjectId> knownEnemyStructures;
  std::vector<CellPosition> knownEnemyPlaces;                   ///< Where they are, in cells, for the attack behaviour
  std::array<std::uint32_t, TARGET_CLASS_COUNT> enemyColumns{}; ///< The target columns it has seen, counted
  std::uint32_t enemyDevicesSeen = 0;
  bool contact = false; ///< It has seen an enemy object at some point in the match

  /// One number a cluster: the devices of every other alliance the commander can see in it. What
  /// the attack behaviour steers by and what a defend behaviour would read in M2.
  std::vector<std::uint32_t> threatByCluster;
  std::uint32_t clustersPerSide = 0;

  /// Deposits with no extractor on them and WITHIN REACH of the base, nearest first. The range is
  /// what stops a scripted commander walking its only truck across the landscape for a deposit it
  /// will not hold: how far a commander should expand is a threat-map decision and that is M2's.
  std::vector<CellPosition> freeDeposits;
};

/// Rebuilds the blackboard from what this seat owns and can see. Deterministic: every walk is in
/// ascending object id and every list comes out in one order (AGENTS.md R16).
void Observe(const Sim& _sim, std::uint8_t _seat, AiBlackboard& _out);

} // namespace Frontier
