#pragma once

#include "ObjectId.h"
#include "Placement.h"

#include "FixedPoint.h"

#include <cstdint>
#include <span>

// Stage 5 of the tick (TechnicalDesign.md §4.8): builders begin plans, advance the sites they are
// attending, and put modules onto standing structures (GameDesign.md §5). Free functions rather
// than a class, because this system holds no state at all: the plans and the sites are structures
// in the world, the builders are devices in it, and the only thing a tick changes is a field on
// one of them.
//
// A BUILDER IS NOT ORDERED TO BUILD; IT ATTENDS WHAT IT IS NEAR. There is no build order in
// TechnicalDesign.md §4.7's table, and GameDesign.md §5 says a builder "drives there and
// constructs it over the build time". So a device carrying a builder module contributes to every
// plan and site of its own seat within that module's range, and construction begins the moment one
// is in reach. That is the whole rule, and it makes moving a builder the way a commander directs
// one - which is what the order table already gives them.
//
// THE TABLES STATE A TIME PER STRUCTURE AND A RATE PER BUILDER, AND THE TWO ONLY MEET THROUGH A
// REFERENCE. GameDesign.md §5 says a structure is built "over the build time" and several builders
// shorten it, so StructureDesc::buildTimeTicks is the time one builder takes. GameDesign.md §6
// gives the builder module a rate of ten power a second, which is Components.json's
// buildPowerHundredthsPerTick of 50. Neither says what a FASTER builder does to a stated build
// time, and the only answer that keeps both numbers meaningful is a ratio: a structure needs
// buildTimeTicks times the reference rate of effort, and each builder puts in its own rate a tick.
// Today's only builder module is the reference, so a structure takes exactly the time its row
// states, two builders take half of it, and a builder module twice as fast would halve it too.
//
// WHAT IS NOT RECONCILED, AND IS STATED RATHER THAN QUIETLY AVERAGED. The rows' build times are
// about 1.5 times what cost-divided-by-ten-power-a-second would give (a 400-power factory at 1,200
// ticks against the 800 that rate implies), and four of the six rows are at exactly 1.5. Devices
// are built by the other rule - m1-vertical-slice/S5's acceptance says "cost divided by 10 per
// second" - so structures and devices genuinely build at different paces. That is the tables as
// authored; nothing here corrects it.

namespace Frontier
{

class Sim;
class World;

/// The obstruction byte a structure that occupies its cells writes into every cell of its
/// footprint; cleared to zero when it is gone. Nothing passes it (Sim/ClusterGraph.h).
inline constexpr std::uint8_t OBSTRUCTION_STRUCTURE = 255;

/// The builder rate the structure rows' build times are stated against: the Builder module of
/// GameData\Components.json, which is GameDesign.md §6's ten power a second at 20 Hz.
inline constexpr std::int32_t REFERENCE_BUILD_POWER_HUNDREDTHS_PER_TICK = 50;

/// How long a wreck lies before it is removed (GameDesign.md §5): thirty seconds, which is long
/// enough that a client sent the death has drawn it and short enough that a battlefield clears.
inline constexpr std::uint32_t WRECK_DECAY_TICKS = 30 * static_cast<std::uint32_t>(Neuron::TICKS_PER_SECOND);

/// The effort a thing of this build time needs before it is finished.
[[nodiscard]] constexpr std::int32_t RequiredEffortHundredths(std::uint32_t _buildTimeTicks) noexcept
{
  return static_cast<std::int32_t>(_buildTimeTicks) * REFERENCE_BUILD_POWER_HUNDREDTHS_PER_TICK;
}

/// How far along a build is, in hundredths of a hundred percent: 10,000 is finished. What the hit
/// points are proportional to and what a cancellation refunds against.
[[nodiscard]] std::int32_t ProgressHundredths(std::int32_t _effortHundredths, std::uint32_t _buildTimeTicks) noexcept;

/// The build power, in hundredths a tick, that _seat's builders within reach of _footprint put in.
/// A builder reaches a footprint when it is within its module's range of the nearest point of it.
[[nodiscard]] std::int64_t AttendingBuildPower(const World& _world, std::span<const Seat> _seats, const ContentTree& _content,
                                               std::uint8_t _seat, const Footprint& _footprint);

/// How many plans _seat has waiting, against MAX_PLANS_PER_SEAT.
[[nodiscard]] std::uint32_t PlanCount(const World& _world, std::uint8_t _seat);

// ── The four orders this system owns (Sim/Order.h's table) ───────────────────────────────────

/// Places a plan: no cost, no obstruction, no flatten. False when the seat is at its plan limit.
[[nodiscard]] bool PlaceStructurePlan(Sim& _sim, std::uint8_t _seat, std::uint32_t _row, std::uint32_t _cellX, std::uint32_t _cellY);

/// Withdraws a plan or an unfinished site, refunding the share not yet built. False for anything
/// standing, which is Demolish's.
[[nodiscard]] bool CancelStructure(Sim& _sim, std::uint8_t _seat, ObjectId _structure);

/// Takes a standing structure down, refunding half. It leaves no wreck: a wreck is what a thing
/// destroyed leaves, and a commander who dismantles their own has the pieces.
[[nodiscard]] bool DemolishStructure(Sim& _sim, std::uint8_t _seat, ObjectId _structure);

/// Starts a module on a standing structure. False when the row does not take it, the slots are
/// full, or one is already going up.
[[nodiscard]] bool BeginModule(Sim& _sim, std::uint8_t _seat, ObjectId _structure, std::uint32_t _module);

// ── The stages ───────────────────────────────────────────────────────────────────────────────

/// What a structure's modules take off a time of that effect, as a percentage, summed over the
/// modules it carries: a factory with two takes 50 off its build times and a lab with one takes 30
/// off its research (GameDesign.md §5). The generator's effect is a COUNT rather than a time and
/// Sim/Economy.cpp reads it where it serves; these two have no reader until m1-vertical-slice/S5
/// and S6, and the function is here so that when they arrive there is one place it is read from
/// and not two that can disagree.
[[nodiscard]] std::int32_t ModuleTimeReductionPercent(const Structure& _structure, const ContentTree& _content,
                                                      StructureModuleEffect _effect);

/// _ticks with that percentage off it, never below one tick: a time of zero would make a thing
/// appear the moment it was ordered, which no reduction the design allows should ever do.
[[nodiscard]] std::uint32_t ShortenedTicks(std::uint32_t _ticks, std::int32_t _reductionPercent) noexcept;

/// Re-marks the obstruction grid from every structure that occupies its footprint. Obstruction is
/// DERIVED from the world - a structure marks its own cells when construction begins - so it is
/// rebuilt wherever a world arrives without having been built up a structure at a time, which is
/// the snapshot's read (TechnicalDesign.md §4.9). Without it a reloaded match's buildings are
/// walked straight through, because the landscape carries its definition and its height deltas and
/// the obstruction byte is in neither.
void MarkStandingObstructions(Sim& _sim);

/// Stage 5: begin the plans a builder has reached, advance the sites and the modules.
void AdvanceConstruction(Sim& _sim);

/// Turns a structure into a wreck and frees its ground. What stage 10 calls when the hit points
/// reach zero; it lives here because this is what put the structure up.
void DestroyStructure(Sim& _sim, ObjectId _structure);

/// The tail of stage 10: a wreck counts down and is removed. Nothing else touches a wreck.
void AdvanceWrecks(World& _world);

} // namespace Frontier
