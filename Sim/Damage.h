#pragma once

#include "ObjectId.h"

#include "ContentTree.h"
#include "DamageTable.h"

#include "FixedPoint.h"

#include <cstdint>

// Stage 10 of the tick (TechnicalDesign.md §4.8): the damage stages 8 and 9 decided is applied,
// what reaches nought is destroyed and leaves a wreck, the killer gains experience, and what is
// hurt enough breaks off (GameDesign.md §8; m1-vertical-slice/S10).
//
// WHY DAMAGE IS A QUEUE AND NOT A WRITE AT THE SHOT. The tick's stages are named for what they do:
// stage 8 acquires and fires, stage 9 lands the shells, and stage 10 is "damage, destruction,
// wrecks, experience". Two shooters that both fire at one device in one tick must both be firing
// at a device that is alive, because both fired before either shot landed - and a write at the
// shot makes the second shooter's tick depend on whether the first happened to be walked first.
// A queue makes the order of the walk stop mattering: the two shots are both fired, both applied,
// and the target dies once.
//
// THE FORMULA IS CONTENT'S, NOT THIS TASK'S. Content/DamageTable.h carries DamageDealt, because
// the design screen and Tools/CheckBalance.py apply it too, and a second implementation here is a
// second thing to keep in step. What this file adds is which armour a target offers and which
// column of the matrix it is hit in.

namespace Frontier
{

class Sim;

/// One hit, decided by stage 8 or stage 9 and applied by stage 10. The shooter is carried so that
/// the kill credits the right device, and the seat so that it credits the right commander even
/// when the shooter did not survive the tick it fired in.
struct DamageEvent
{
  ObjectId target;
  ObjectId shooter;
  std::uint8_t seat;
  std::int32_t points;

  [[nodiscard]] constexpr bool operator==(const DamageEvent&) const noexcept = default;
};

/// What a target offers a shot: the column of the matrix it is hit in and its two armour values.
/// Filled for a device from its design and its commander's upgrades, for a structure from its row.
struct TargetArmor
{
  std::uint8_t column;
  std::int32_t kinetic;
  std::int32_t thermal;

  [[nodiscard]] constexpr bool operator==(const TargetArmor&) const noexcept = default;
};

/// The armour a weapon of this class meets, from the pair a target carries.
[[nodiscard]] constexpr std::int32_t ArmorAgainst(const DamageTable& _table, WeaponClass _weapon, const TargetArmor& _armor) noexcept
{
  return _table.armorKind[static_cast<std::size_t>(_weapon)] == ArmorKind::Kinetic ? _armor.kinetic : _armor.thermal;
}

/// How far a device will walk to a repair point before deciding it is not worth it
/// (GameDesign.md §8): 60 cells, so that a heavy on a large landscape holds and fights rather than
/// spending the match commuting, and the answer to distance is a forward repair bay.
inline constexpr std::int32_t RETREAT_REPAIR_RANGE_SUBUNITS = 60 * Neuron::SUBUNITS_PER_CELL;

/// Stage 10: apply the tick's damage, destroy what reaches nought, award the experience, then let
/// the hurt break off and the wrecks decay.
void ResolveDamage(Sim& _sim);

/// The part of stage 10 that decides who leaves the fight. Its own translation unit because it is
/// its own rule and a long one (Sim/Retreat.cpp); declared here because it is what the hit points
/// this stage writes are read for.
void AdvanceRetreat(Sim& _sim);

/// The nearest place a seat can be repaired - a standing repair bay, or one of its own devices
/// carrying a repair module - within RETREAT_REPAIR_RANGE_SUBUNITS of a point. False when there is
/// none, which is what M1's content always answers: it ships neither the structure nor the module,
/// so the rule is exercised against a fixture that does (m1-vertical-slice/S10's acceptance).
[[nodiscard]] bool RepairPointNear(const Sim& _sim, std::uint8_t _seat, std::int32_t _x, std::int32_t _z, std::int32_t& _outX,
                                   std::int32_t& _outZ);

} // namespace Frontier
