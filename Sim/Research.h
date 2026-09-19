#pragma once

#include "ObjectId.h"
#include "Seat.h"

#include "ContentTree.h"

#include <cstdint>

// Stage 3 of the tick (TechnicalDesign.md §4.8): the research tree of GameDesign.md §7, in labs
// that work in parallel, with upgrades that reach what is already in the field
// (m1-vertical-slice/S6). Free functions, as stages 4 and 5 are: the progress is on the seat and
// the labs are structures, and a tick moves one number.
//
// AN UPGRADE IS WRITTEN ONCE, TO THE SEAT, AND NOT TO EVERY DEVICE. Seat::upgrades is what every
// derivation reads (Sim/Design.h), so the moment a percentage lands there every design of that
// class answers the new number - including designs of devices built an hour ago, because nothing
// derived is stored on a device. What completing an upgrade DOES have to walk the world for is the
// one thing that is stored: the current hit points. A device at half health must still be at half
// health when its maximum grows, which is what "hit points keeping their fraction" means, and it
// is a multiply per object on an event that happens a few dozen times a match.
//
// TWO EFFECTS HAVE NOWHERE ELSE TO GO. ExtractorRate and StructureHitPoints are upgrades to things
// that are not a chassis or a weapon class, and ClassUpgrades had no room for them; they are two
// more fields on it rather than two more places an upgrade can live, because one home for
// "what research has done to this commander" is the property that makes a derivation trustworthy.
//
// THE UNLOCK EFFECT DOES NOTHING HERE, and that is not an omission. A row says what unlocks it
// (`unlockedBy`) and a research item says what it unlocks (`unlocks`); the simulation reads the
// first, so an item completing is already the whole of the unlock the moment it joins
// researchComplete. The second direction is what the research panel lists and what
// ContentValidator checks, and it is deliberately not a second mechanism.

namespace Frontier
{

class Sim;

/// A research row index that names none; what CheapestAvailable answers with when there is nothing
/// left to research.
inline constexpr std::uint32_t NO_RESEARCH_ITEM = 0xFFFFFFFFu;

/// Whether every prerequisite of an item is complete for the seat. An item naming a prerequisite
/// no row defines is unreachable, which is a content fault ContentValidator reports.
[[nodiscard]] bool PrerequisitesComplete(const Seat& _seat, const ContentTree& _content, std::uint32_t _item);

/// Whether the seat may start this item now: the row exists, it is neither complete nor already in
/// a lab, and its prerequisites are done.
[[nodiscard]] bool Available(const Seat& _seat, const ContentTree& _content, std::uint32_t _item);

/// The cheapest item the seat may start, ties broken by the lower row index so that two hosts pick
/// the same one. NO_RESEARCH_ITEM when there is nothing.
[[nodiscard]] std::uint32_t CheapestAvailable(const Seat& _seat, const ContentTree& _content);

/// Starts an item in an idle lab, drawing its cost now (GameDesign.md §4: the cost is drawn when
/// the work begins).
[[nodiscard]] bool SetResearch(Sim& _sim, std::uint8_t _seat, ObjectId _lab, std::uint32_t _item);

/// Stops what a lab is researching. It refunds nothing: research is knowledge half-acquired and
/// there is nothing to take back out of it.
[[nodiscard]] bool CancelResearch(Sim& _sim, std::uint8_t _seat, ObjectId _lab);

/// Completes an item for a seat: it joins researchComplete, and an upgrade adds its percentage and
/// rescales the hit points of everything of that class the seat already has.
void CompleteResearch(Sim& _sim, std::uint8_t _seat, std::uint32_t _item);

/// Stage 3: the labs advance, auto-research fills the idle ones, and a lab that is gone takes its
/// progress with it.
void AdvanceResearch(Sim& _sim);

} // namespace Frontier
