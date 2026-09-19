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

/// The primary order a device carries (GameDesign.md §8): one at a time, replaced by the next.
/// Stop is the resting state and what a device falls back to when its order can no longer be kept.
enum class PrimaryOrder : std::uint8_t
{
  Stop,
  Move,
  AttackMove,
  Attack,
  Patrol,
  Guard,
  ReturnToRepair
};

inline constexpr std::uint8_t PRIMARY_ORDER_COUNT = 7;

// The four standing stances of GameDesign.md §8, each set on its own axis so that setting one
// leaves the other three alone. Their order is the order OrderKind::SetStance's axis operand
// names them, and a value is an index into the axis, which is what makes the order validatable
// without the simulation knowing what the axis means.

enum class FireStance : std::uint8_t
{
  FireAtWill,
  ReturnFire,
  HoldFire
};

enum class RangeStance : std::uint8_t
{
  Optimal,
  LongRange
};

enum class RetreatStance : std::uint8_t
{
  AtHalf,
  AtQuarter,
  Never
};

enum class MovementStance : std::uint8_t
{
  Pursue,
  HoldPosition
};

/// The axes of SetStance, in the order its operand names them, and how many values each holds.
enum class StanceAxis : std::uint8_t
{
  Fire,
  Range,
  Retreat,
  Movement
};

inline constexpr std::uint8_t STANCE_AXIS_COUNT = 4;
inline constexpr std::array<std::uint8_t, STANCE_AXIS_COUNT> STANCE_VALUE_COUNTS = {3, 2, 3, 2};

/// Control groups are numbered 1 to 10 on the keyboard; 0 is no group (GameDesign.md §8).
inline constexpr std::uint8_t MAX_CONTROL_GROUP = 10;

/// The most designs a commander may save. A game rule rather than a stream bound, which is why
/// the snapshot's bound is this one rather than a number of its own.
inline constexpr std::uint32_t MAX_SAVED_DESIGNS = 256;

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

/// A device that is walking no route (Sim/Movement.h). It is a sentinel rather than an index of
/// zero because zero is the first cell of a real route, and a device that had just been given one
/// would otherwise be indistinguishable from one that had never asked.
inline constexpr std::uint32_t NO_PATH_INDEX = 0xFFFFFFFFu;

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

  PrimaryOrder primaryOrder; ///< Stop, or what it was last ordered to do
  ObjectId target;           ///< What it is shooting at or guarding, or NO_OBJECT
  std::int32_t destinationX; ///< Where it is going, in subunits; read unless the order is Stop
  std::int32_t destinationZ;
  /// The other end of a Patrol and the post a Guard returns to, in subunits. Set when the order is
  /// applied and read by stage 6; meaningless under any other order.
  std::int32_t anchorX;
  std::int32_t anchorZ;

  /// How far along the planner's route it has walked, and how many ticks it has made no headway
  /// (Sim/Movement.h). The route itself is the planner's, because it is a vector and a device is a
  /// fixed-layout record; how far along it this device is cannot be, because two devices share a
  /// destination and not a position.
  std::uint32_t pathIndex = NO_PATH_INDEX;
  std::uint32_t stalledTicks;

  FireStance fire;
  RangeStance range;
  RetreatStance retreat;
  MovementStance movement;
  std::uint8_t group; ///< 0 for none, else 1 to MAX_CONTROL_GROUP

  std::array<std::uint32_t, MAX_MOUNTS> reloadTicks; ///< Ticks until each mount may fire again

  [[nodiscard]] constexpr bool operator==(const Device&) const noexcept = default;
};

} // namespace Frontier
