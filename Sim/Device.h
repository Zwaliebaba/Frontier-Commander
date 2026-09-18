#pragma once

#include "ObjectId.h"

#include "ComponentDesc.h"

#include <array>
#include <cstdint>

// A built device and the design it was built from (GameDesign.md §6). Simulation fields only, in
// the units of TechnicalDesign.md §4.1: positions in 1/256 of a world unit, angles as binary
// angles, times in ticks. Nothing here is derived from the content tables, because a derived
// statistic is recomputed from the design and the seat's upgrades rather than stored and left to
// go stale (S5); what is stored is what a tick changes.

namespace Frontier
{

/// What a commander designed: indices into the content tables, not names, because a design is
/// hashed and sent every tick. S5 owns the rules; this is what S1 stores.
struct DeviceDesign
{
  std::uint32_t chassis; ///< Row index in the chassis table
  std::uint32_t drive;
  std::array<std::uint32_t, MAX_MOUNTS> modules; ///< The first moduleCount entries are meaningful
  std::uint8_t moduleCount;

  [[nodiscard]] constexpr bool operator==(const DeviceDesign&) const noexcept = default;
};

struct Device
{
  std::uint8_t seat;
  std::uint32_t design; ///< Index into the seat's designs

  std::int32_t x; ///< Subunits (1/256 world unit)
  std::int32_t y; ///< Height, same unit
  std::int32_t z;
  std::uint16_t facing; ///< Binary angle (TechnicalDesign.md §4.1)

  std::int32_t hitPoints;
  std::uint32_t experience; ///< Kills weighted by GameDesign.md §8; a rank is a threshold on it

  ObjectId target;           ///< What it is shooting at, or NO_OBJECT
  std::int32_t destinationX; ///< Where it is going, in subunits; read only while moving
  std::int32_t destinationZ;
  bool moving;

  std::array<std::uint32_t, MAX_MOUNTS> reloadTicks; ///< Ticks until each mount may fire again

  [[nodiscard]] constexpr bool operator==(const Device&) const noexcept = default;
};

} // namespace Frontier
