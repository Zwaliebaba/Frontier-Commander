#pragma once

#include "ObjectId.h"

#include <cstdint>

// What a destroyed device or structure leaves (GameDesign.md §5, §8). A wreck obstructs nothing -
// only structures under construction or standing do - so it is drawn and reclaimed and otherwise
// inert, and it decays on a tick count rather than being removed at the moment of death, so that
// a client sees the death it was sent.

namespace Frontier
{

struct Wreck
{
  std::uint8_t seat;    ///< Who owned what died, for the reclaim rules and for the colour
  ObjectId origin;      ///< The device or structure it was; stale by construction, kept for the record
  std::uint32_t design; ///< The row it was built from, which is what names the wreck's model

  std::int32_t x; ///< Subunits
  std::int32_t y;
  std::int32_t z;
  std::uint16_t facing; ///< Binary angle: a wreck lies where it fell

  std::uint32_t decayTicks; ///< Ticks until it is removed

  [[nodiscard]] constexpr bool operator==(const Wreck&) const noexcept = default;
};

} // namespace Frontier
