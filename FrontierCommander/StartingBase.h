#pragma once

#include "MatchSettings.h"
#include "Sim.h"

#include <cstdint>

// What is already standing when the match begins (GameDesign.md §2 and §5): "the base level in the
// lobby decides", and the levels are "nothing is a builder and a command post; small adds two
// served extractors, a generator and a factory; established adds a lab, a repair bay and four
// hardpoints".
//
// THIS IS THE APPLICATION'S AND NOT THE SIMULATION'S, which is a ruling the tree already carries:
// Tests/SimTests/AiTests.cpp places its own base and says why - "NOTHING IN THE TREE OWNS
// BASE-LEVEL PLACEMENT YET - it is the application's (m1-vertical-slice/G1), and until G1 lands a
// match is set up by whoever starts one". This is G1 landing. It stays out of Sim because a tick
// never does it: it happens once, before tick 0, from the lobby's settings, and a Sim that could do
// it would be a Sim with a notion of "the start" that its own loop has no use for.
//
// IT USES Sim's MUTABLE DOOR DELIBERATELY. Sim::Objects(), Sim::SeatAt, Sim::FlattenTerrain and
// Sim::SetObstruction exist for exactly this - Sim.h says the mutable world "exists for the same
// reason FlattenTerrain does" - and the alternative, an order stream, cannot express it: an order
// builds a structure over its build time with a builder that does not exist yet.
//
// AND IT FLATTENS, WHICH THE TEST FIXTURE DOES NOT. Construction.cpp levels the ground under a
// footprint the moment construction begins and writes the mean into Structure::y; a base that
// skipped that step would have every starting command post at y = 0, which is sea level, and the
// renderer would draw it buried. The test fixture never noticed because no test of it draws.

namespace Frontier
{

/// Places _seat's base level at _start, the cell the landscape names as that seat's start, which
/// becomes the command post's LOWEST cell rather than its centre - the same reading
/// Tests/SimTests/AiTests.cpp takes of GameData/Landscapes/Slice.json, so the picture the
/// application starts and the picture the AI suite tests are the same one.
///
/// False, with the reason logged, when the tables carry no command post or no builder the seat
/// could field, or when there is nowhere within sixteen cells of the start that the builder's
/// drive class can stand on. Each is a content fault and each leaves the seat with no way to play,
/// so it belongs on the way up rather than as a commander who quietly cannot build.
///
/// Only BaseLevel::Nothing is placed. Small and Established are the lobby's (M2) and are refused
/// rather than silently reduced: a commander who chose an established base and got a builder and a
/// command post would read it as the game losing his settings.
[[nodiscard]] bool PlaceStartingBase(Sim& _sim, std::uint8_t _seat, const CellPosition& _start, BaseLevel _level);

} // namespace Frontier
