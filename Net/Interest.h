#pragma once

#include "Records.h"

#include "Sim.h"

#include <cstdint>
#include <vector>

// What a client is told (TechnicalDesign.md §5.2). This is a SECURITY boundary rather than a
// bandwidth one: the fog of war is enforced here, by the host, so that a modified client sees
// nothing an honest one does not (GameDesign.md §10). Everything the encoder may name comes out of
// this file, and the interest test asserts that nothing else ever reaches a frame.
//
// THE ONE LEAK THIS MODEL KEEPS, named in §5.2 rather than hidden: the landscape's DEFINITION is
// public. The seed, the size class, the tile list, the start positions and the deposits go to
// every client at the join, because they are known to every commander from the first tick in most
// strategy games. The flatten deltas are not: a structure's footprint reaches a client only inside
// that structure's own record or its ghost, so the ground under an unscouted base stays as the
// generator made it.

namespace Frontier
{

/// What one seat may be told about this publish, as ids in ascending order so that two hosts
/// building the same set hold it in one order.
struct InterestSet
{
  std::vector<std::uint32_t> devices;
  std::vector<std::uint32_t> structures;
  std::vector<std::uint32_t> wrecks;
  std::vector<std::uint32_t> features;
  /// Structures the seat cannot see now but has seen: sent from the ghost store at their last-seen
  /// state. A structure in `structures` is never here as well.
  std::vector<ObjectId> ghosts;

  void Clear() noexcept
  {
    devices.clear();
    structures.clear();
    wrecks.clear();
    features.clear();
    ghosts.clear();
  }
};

/// True when this seat or one of its allies has the cell visible now. Alliances share vision
/// (GameDesign.md §2), and that is one union over the alliance's grids rather than a grid of its
/// own, because a seat that leaves an alliance would leave a shared grid holding what it saw.
[[nodiscard]] bool VisibleToAlliance(const Sim& _sim, std::uint8_t _seat, std::uint32_t _cellX, std::uint32_t _cellY) noexcept;

/// The seat's interest set for this publish: every object in a cell the alliance sees, every object
/// the seat owns wherever it is, and a ghost for every structure it has seen and cannot see now.
void GatherInterest(const Sim& _sim, std::uint8_t _seat, InterestSet& _out);

} // namespace Frontier
