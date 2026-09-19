#pragma once

#include "Damage.h"
#include "Device.h"
#include "ObjectId.h"
#include "Seat.h"

#include "ContentTree.h"

#include <cstdint>

// Stage 8 of the tick (TechnicalDesign.md §4.8): weapons acquire, stances decide, and shots are
// fired (GameDesign.md §8; m1-vertical-slice/S10). Direct fire is resolved here - the roll happens
// as the trigger is pulled and the damage is queued - and indirect fire leaves a Projectile that
// stage 9 lands.
//
// WHAT A COMMANDER CAN SEE IS THE WHOLE OF THE SPOTTER RULE. GameDesign.md §8 says a target must be
// visible to someone on the commander's side to be fired on, and that indirect weapons "additionally
// need a spotter... because their range exceeds their sight". The fog grid of S9 is already the
// union of every viewer of the alliance, so ONE test - is the target's cell visible to this seat -
// is both rules at once. What makes it bite differently for the two is the arithmetic of the
// tables: a cannon reaches 16 cells and the light chassis it sits on sees 20, so a direct shooter
// always sees its own target; a mortar reaches 28 and sees 20, so it cannot fire at its longest
// range unless something else of its commander's is looking. Two rules, one check, and the test
// that proves it is the one that takes the mortar's spotter away.
//
// THE TARGET PRIORITY IS THIS TASK'S, AND IT IS THE ONE THE DESIGN LEFT OUT. GameDesign.md §8 says
// "Structures with weapons take a target-priority stance only" and never says what the priorities
// are, and there is no target-priority axis in Sim/Device.h's four (the order set cannot express
// one). So the rule below is fixed for every shooter and stated here: keep the target you have
// while it is alive, visible and in range - a weapon that re-chose every tick would spray a crowd
// and never kill anything - and otherwise take the nearest, devices before structures, ties broken
// by the lower id. Devices first because a thing that shoots back is the thing that has to stop.
// OpenQuestions.md Q24 puts the priority list to the owner for M2, where the stance axis belongs.
//
// ACQUISITION IS A LINEAR SCAN, and that is a measured cost rather than an oversight: it runs only
// when a shooter has no usable target, which after the first tick of a fight is nobody. At the
// eight-seat caps of GameDesign.md §4 a scan is 4,000 objects, so a tick in which every shooter
// re-acquires at once is the one that would need a grid, and that is what m1-vertical-slice/G3
// measures before anything is built.

namespace Frontier
{

class Sim;

/// A target reduced to what a shot needs: where it is, what it is hit as, and what it is worth to
/// whoever kills it.
struct TargetPoint
{
  ObjectId id;
  std::int32_t x;
  std::int32_t z;
  std::int32_t y;
  TargetArmor armor;
  std::int32_t costHundredths; ///< What destroying it is worth (Sim/Experience.h)
  bool device;                 ///< Devices are taken before structures

  [[nodiscard]] constexpr bool operator==(const TargetPoint&) const noexcept = default;
};

/// Resolves an id to a target, whether it names a device or a structure. False for an id that
/// names neither - one that died, or was never a target at all.
[[nodiscard]] bool TargetAt(const Sim& _sim, ObjectId _id, TargetPoint& _out);

/// Whether a seat can see the point, which is the fog grid of S9 answering for the whole alliance.
[[nodiscard]] bool VisibleToSeat(const Seat& _seat, std::int32_t _x, std::int32_t _z) noexcept;

/// The target a shooter of this seat takes at this point within this range, by the priority above,
/// or NO_OBJECT. Only what the seat can see and only what it is not allied with.
[[nodiscard]] ObjectId Acquire(const Sim& _sim, std::uint8_t _seat, std::int32_t _x, std::int32_t _z, std::int32_t _rangeSubunits);

/// Stage 8: every weapon of every device and of every standing structure that carries one.
void AdvanceTargeting(Sim& _sim);

} // namespace Frontier
