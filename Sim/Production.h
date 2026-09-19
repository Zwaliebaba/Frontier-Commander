#pragma once

#include "Design.h"
#include "ObjectId.h"
#include "Placement.h"
#include "Seat.h"

#include "ContentTree.h"
#include "DesignStats.h"

#include <cstdint>

// Stage 4 of the tick (TechnicalDesign.md §4.8): factories build devices from their commander's
// designs (GameDesign.md §6; m1-vertical-slice/S5). Free functions for the same reason
// Sim/Construction.h has them - this system holds no state of its own. The queues are on the seat,
// the factory's countdown is on the structure, and a tick moves one of the two.
//
// A DEVICE IS PRICED AND TIMED BY ITS OWN STATISTICS AND NOT BY A ROW. The design's cost is the sum
// of its parts (Content/DesignStats.cpp) and its build time is that cost divided by the ten power a
// second of GameDesign.md §6, less what the factory's modules take off. Structures are the other
// way round - their rows state a time and the builders' rates meet it (Sim/Construction.h) - and
// the two rules are the tables as the design authored them rather than an inconsistency invented
// here.
//
// THE COST IS DRAWN WHEN THE FACTORY STARTS ONE, not when the queue is set, which is the same rule
// construction follows: a queue costs nothing until it reaches the front. Cancelling gives back the
// share of the one in progress that is not yet built, by the same arithmetic a cancelled structure
// uses, so power cannot be parked in a production queue any more than in an unfinished building.

namespace Frontier
{

class Sim;
class World;

/// The statistics of one of a seat's designs, with that seat's class upgrades in force. The fault
/// when the design is not buildable, in which case _out is untouched.
[[nodiscard]] DesignFault DeriveSeatDesign(const Seat& _seat, const ContentTree& _content, std::uint32_t _design, DesignStats& _out);

/// Whether a seat may build a design at all: every part exists, every part is unlocked for it, and
/// the combination is one Content/DesignStats.cpp accepts. DesignFault::None is yes.
[[nodiscard]] DesignFault CheckDesign(const Seat& _seat, const ContentTree& _content, const DeviceDesign& _design);

/// Where a factory puts what it built: the first cell around its footprint that is on the
/// landscape, dry and unobstructed, scanned anticlockwise from the cell below its lowest corner so
/// that two hosts pick the same one. False when it is walled in, and the caller then spawns on the
/// footprint itself rather than not at all.
[[nodiscard]] bool ExitCell(const Landscape& _landscape, const Footprint& _footprint, std::uint32_t& _cellX, std::uint32_t& _cellY);

// ── The three orders this system owns (Sim/Order.h's table) ──────────────────────────────────

/// Saves a design in one of the seat's slots, extending the list where the slot is the next one.
[[nodiscard]] bool SaveDesign(Sim& _sim, std::uint8_t _seat, std::uint32_t _slot, const DeviceDesign& _design);

/// Queues _repeat of a design at a factory. A second order for a factory that already has a queue
/// appends to it, because a commander who asks for two things wants both.
[[nodiscard]] bool SetProduction(Sim& _sim, std::uint8_t _seat, ObjectId _factory, std::uint32_t _design, std::uint32_t _repeat);

/// Drops the _slot-th of that factory's entries, refunding the share of an unfinished one.
[[nodiscard]] bool CancelProduction(Sim& _sim, std::uint8_t _seat, ObjectId _factory, std::uint32_t _slot);

/// Stage 4: every factory advances the front of its queue, and a finished device is spawned.
void AdvanceProduction(Sim& _sim);

} // namespace Frontier
