#pragma once

#include "Interpolation.h"

#include "Records.h"

#include <cstdint>

// What the client holds about one object it can see (TechnicalDesign.md §2, §5.3): the wire record
// exactly as the host sent it, and whatever the client has to remember beside it.
//
// THE WIRE RECORD IS KEPT WHOLE AND UNCHANGED. It is not unpacked into a client-side struct, and
// nothing here is derived from it, because the convergence test of this task compares the replica
// with the host's own encoding field for field: a client type would put a translation between the
// two and the test would be proving that translation rather than the protocol. What the client adds
// - a motion, a ghost flag - sits beside the record and never inside it.

namespace Frontier
{

/// A device the commander can see, and where it has been. Only devices carry a Motion: a structure
/// is placed on the grid and never moves, a wreck lies where the thing died, and a deposit is part
/// of the landscape.
struct ReplicaDevice
{
  DeviceState state;
  Motion motion;
};

/// A structure, standing or remembered.
///
/// A GHOST IS NOT A SEPARATE COLLECTION. The host sends a structure the commander has seen and
/// cannot see now from the ghost store, in the same list and as the same record (Net/Interest.h:
/// "A structure in `structures` is never here as well"), so the id never leaves the replica and
/// never arrives twice. What changes is that the record is the last-seen one, which the encoder
/// marks by sending no hit points - see GHOST_HIT_POINTS.
struct ReplicaStructure
{
  StructureState state;
  bool ghost = false;
};

/// The hit points a ghost carries. Net/FrameEncoder.cpp's WireGhost sends 0 and says why: the
/// commander has no idea what the building has taken since he last saw it, so zero means unknown
/// rather than destroyed. A standing structure at zero hit points does not exist - it would have
/// been removed the tick it reached zero - so this is a marker the wire can carry without a flag.
inline constexpr std::uint16_t GHOST_HIT_POINTS = 0;

struct ReplicaWreck
{
  WreckState state;
};

struct ReplicaFeature
{
  FeatureState state;
};

} // namespace Frontier
