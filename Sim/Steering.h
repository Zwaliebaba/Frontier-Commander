#pragma once

#include "ObjectId.h"

#include "FixedPoint.h"

#include <cstdint>
#include <span>

// Separation steering (m1-vertical-slice/S8): the one flocking rule a commander's army needs, in
// integers. Devices that end a tick inside a radius of one another are nudged apart before the
// next, so that a column does not walk through itself and a group that is told to go to one place
// spreads over it instead of stacking on one point.
//
// THE PUSH IS NOT PURELY RADIAL, AND THAT IS THE WHOLE TRICK. Two devices meeting head-on are
// pushed apart along exactly the line they are travelling, so a radial push is a brake and they
// grind to a halt facing each other. Turning the push a quarter turn - always the same way round -
// makes it antisymmetric in a second sense: each takes the side the other does not, and they pass.
// Written as away-plus-a-quarter-turn rather than as a rule about which side is which, because the
// arithmetic is the rule and there is no case to get the wrong way round.
//
// A TIE-BREAK BY ID IS NEEDED FOR ONE CASE AND ONE ONLY: two devices at exactly the same point have
// no direction between them to push along. Then the higher id steps and the lower stands, in one of
// eight directions taken from the id, so that a pile spreads rather than sliding off along one
// line. Every other case has a direction and needs no tie-break at all.

namespace Frontier
{

/// How close two devices come before they steer apart: a quarter of a cell, which is about two
/// device widths (GameDesign.md §6 sizes a chassis in the tens of world units).
inline constexpr std::int32_t SEPARATION_RADIUS_SUBUNITS = Neuron::SUBUNITS_PER_CELL / 4;

/// The most one tick of separation moves a device: two world units, which is 40 a second against
/// the 104 a light scout drives at, so steering never outruns driving.
inline constexpr std::int32_t SEPARATION_STEP_SUBUNITS = 2 * Neuron::SUBUNITS_PER_WORLD_UNIT;

/// One device as the separation pass sees it: where it is and which it is, and nothing else, so
/// that the pass is a function of positions and ids and can be measured on its own.
struct SteeredDevice
{
  ObjectId id;
  std::int32_t x;
  std::int32_t z;

  [[nodiscard]] constexpr bool operator==(const SteeredDevice&) const noexcept = default;
};

/// What one tick of separation would move a device by, in subunits.
struct SeparationPush
{
  std::int32_t x;
  std::int32_t z;

  [[nodiscard]] constexpr bool operator==(const SeparationPush&) const noexcept = default;
};

/// Fills _pushes, one per device of _devices and in the same order, with what separation asks of
/// each. Nothing is moved here: the caller applies a push only where the ground takes it, which
/// is a question about a drive and a landscape that this pass is deliberately free of.
///
/// Bucketed by cell rather than pair by pair. The radius is a quarter of a cell, so every
/// neighbour is in a device's own cell or in one of the eight around it; the eight-seat device cap
/// is 2,400, and pair by pair that would be 2.9 million tests a tick against a 50 ms budget.
void Separate(std::span<const SteeredDevice> _devices, std::span<SeparationPush> _pushes);

} // namespace Frontier
