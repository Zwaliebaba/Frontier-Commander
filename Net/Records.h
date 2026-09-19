#pragma once

#include "Design.h"
#include "Device.h"
#include "FogGrid.h"
#include "MatchSettings.h"
#include "ObjectId.h"
#include "Order.h"
#include "Structure.h"
#include "Victory.h"

#include "ComponentDesc.h"

#include "ByteReader.h"
#include "ByteWriter.h"
#include "FixedPoint.h"

#include <cstdint>

// The wire form of everything a client is told about (TechnicalDesign.md §5.3). Every record here
// is a plain aggregate of fixed-width integers (AGENTS.md R8) with a byte layout written down once
// in Records.cpp, and NetTests pins the bytes of one instance of each so that a layout change is a
// visible diff rather than a silent incompatibility.
//
// WHY THESE ARE NOT THE SIMULATION'S RECORDS. A Device is 80 bytes of simulation state and a
// DeviceState is 31 of what can be drawn: the wire carries what a client needs to render and to
// decide, and nothing it could use to see through the fog. The two are deliberately separate types
// rather than one type with a narrowing writer, because a field added to the simulation must not
// reach the wire by default - §5.2's leak posture is enforced by what the encoder can name.
//
// WHY THE LAYOUT IS NOT THE SNAPSHOT'S. Sim/Snapshot.cpp writes the same settings and the same
// landscape definition, and this file writes them again rather than calling into it. They are two
// streams with two versions: a snapshot that gains a field bumps SNAPSHOT_VERSION, and a wire that
// shared its writer would break every client of a host that could save. They also carry different
// payloads - a snapshot carries the landscape's flatten deltas and the wire must not, because the
// terrain under an unscouted base is not public (§5.2).
//
// NO FLOAT CROSSES THIS BOUNDARY, as §5.3 requires. Positions are quantised to a quarter of a
// world unit; angles are the high byte of a binary angle; everything else is already an integer.

namespace Frontier
{

/// Bumped by any change to a layout in this file or in Messages.h. A client whose version differs
/// from the host's is refused at the join rather than left to misread a frame (§5.4).
///
/// 2 (m1-vertical-slice/N4, 2026-09-19): SeatState carries the seat's last refused order, which
/// Design/Interface.md §6 draws. Nothing shipped against version 1, but the bump is not skipped for
/// that: the rule is the layout's and not the audience's, and a version that is bumped only when
/// someone might notice is a version nobody can reason about.
inline constexpr std::uint16_t NET_PROTOCOL_VERSION = 2;

/// A quarter of a world unit, in the subunits the simulation holds positions in (§5.3). Coarse on
/// purpose: a device is several world units across and the replica interpolates between frames, so
/// a quarter-unit step is under the width of the pixel it lands on at any playable camera height.
inline constexpr std::int32_t SUBUNITS_PER_WIRE_UNIT = Neuron::SUBUNITS_PER_WORLD_UNIT / 4;

/// Floor division rather than truncation, so that the quantisation is uniform across zero: -1 and
/// +1 subunit must not both become wire 0 while every other step is 64 wide.
[[nodiscard]] constexpr std::int32_t WireFromSubunits(std::int32_t _subunits) noexcept
{
  return static_cast<std::int32_t>(Neuron::FloorDiv(_subunits, SUBUNITS_PER_WIRE_UNIT));
}

/// The middle of the quarter-unit the wire named, so that the round trip's error is half a step
/// either way rather than a whole step downwards.
[[nodiscard]] constexpr std::int32_t SubunitsFromWire(std::int32_t _wire) noexcept
{
  return _wire * SUBUNITS_PER_WIRE_UNIT + SUBUNITS_PER_WIRE_UNIT / 2;
}

/// A binary angle's high byte: 256 headings, 1.4 degrees apart, which is finer than a model's
/// silhouette resolves at any camera height.
[[nodiscard]] constexpr std::uint8_t WireFromFacing(std::uint16_t _facing) noexcept
{
  return static_cast<std::uint8_t>(_facing >> 8);
}

[[nodiscard]] constexpr std::uint16_t FacingFromWire(std::uint8_t _wire) noexcept
{
  return static_cast<std::uint16_t>(static_cast<std::uint16_t>(_wire) << 8);
}

/// A device's primary order and its four stances in ONE byte, and that is what makes §5.7's
/// fourteen-byte change record fit: seven orders times three fire, two range, three retreat and
/// two movement stances is 252 combinations, four short of a byte. A bit field would need nine
/// bits for the same five values and would cost the change record a second byte for every device
/// that turned round.
struct OrderAndStances
{
  PrimaryOrder order;
  FireStance fire;
  RangeStance range;
  RetreatStance retreat;
  MovementStance movement;

  [[nodiscard]] constexpr bool operator==(const OrderAndStances&) const noexcept = default;
};

[[nodiscard]] constexpr std::uint8_t PackOrderAndStances(const OrderAndStances& _state) noexcept
{
  std::uint32_t packed = static_cast<std::uint32_t>(_state.order);
  packed = packed * 3 + static_cast<std::uint32_t>(_state.fire);
  packed = packed * 2 + static_cast<std::uint32_t>(_state.range);
  packed = packed * 3 + static_cast<std::uint32_t>(_state.retreat);
  packed = packed * 2 + static_cast<std::uint32_t>(_state.movement);
  return static_cast<std::uint8_t>(packed);
}

/// False for the four values no combination produces, which is what keeps a hostile byte from
/// naming an order or a stance the enumerations do not have.
[[nodiscard]] constexpr bool UnpackOrderAndStances(std::uint8_t _packed, OrderAndStances& _out) noexcept
{
  std::uint32_t packed = _packed;
  const auto take = [&packed](std::uint32_t _radix)
  {
    const std::uint32_t value = packed % _radix;
    packed /= _radix;
    return value;
  };
  const std::uint32_t movement = take(2);
  const std::uint32_t retreat = take(3);
  const std::uint32_t range = take(2);
  const std::uint32_t fire = take(3);
  if (packed >= PRIMARY_ORDER_COUNT)
  {
    return false;
  }
  _out.order = static_cast<PrimaryOrder>(packed);
  _out.fire = static_cast<FireStance>(fire);
  _out.range = static_cast<RangeStance>(range);
  _out.retreat = static_cast<RetreatStance>(retreat);
  _out.movement = static_cast<MovementStance>(movement);
  return true;
}

// ── The records ─────────────────────────────────────────────────────────────────────────────

/// A device as a client draws it (§5.3). Positions are absolute wire units here and deltas in
/// DeviceChange, because a creation has no baseline to be a delta from.
struct DeviceState
{
  std::uint32_t id;
  std::uint32_t design; ///< Index into the owning seat's designs; DesignState says what it is
  std::uint8_t seat;
  std::int32_t x; ///< Wire units (a quarter of a world unit)
  std::int32_t y;
  std::int32_t z;
  std::uint8_t heading;
  std::uint16_t hitPoints;
  std::uint8_t rank;
  std::uint32_t target; ///< The id it is shooting at or guarding, or 0
  ObjectKind targetKind;
  OrderAndStances stances;

  [[nodiscard]] constexpr bool operator==(const DeviceState&) const noexcept = default;
};

/// Which fields a DeviceChange carries. One byte, one bit a field, and a change with no bits set
/// is never sent - a device that did nothing costs nothing (§5.3).
enum class DeviceField : std::uint8_t
{
  Position = 0x01,
  Heading = 0x02,
  HitPoints = 0x04,
  Stances = 0x08
};

/// A device that changed since the baseline: the id, a mask, and the changed fields in the order
/// of the mask's bits. The position is a delta in wire units and fits two bytes an axis, which is
/// ±8,192 world units a frame - a hundred times what anything in this game moves in a fifth of a
/// second, and the encoder sends a creation rather than a change if it ever does not fit.
///
/// **Fourteen bytes at most, plus the mask** (TechnicalDesign.md §5.7): four of id, six of position
/// delta, one of heading, two of hit points, one of stances. The fields NOT in the mask - design,
/// seat, rank and target - are the ones that rarely change, and a device whose rank or target
/// changed is re-sent as a creation rather than widening every frame's worst case.
struct DeviceChange
{
  std::uint32_t id;
  std::uint8_t mask;
  std::int16_t deltaX;
  std::int16_t deltaY;
  std::int16_t deltaZ;
  std::uint8_t heading;
  std::uint16_t hitPoints;
  OrderAndStances stances;

  [[nodiscard]] constexpr bool operator==(const DeviceChange&) const noexcept = default;
};

inline constexpr std::size_t DEVICE_CHANGE_MAX_BYTES = 14;
inline constexpr std::size_t DEVICE_CHANGE_MASK_BYTES = 1;
static_assert(sizeof(std::uint32_t)          // the id
                  + 3 * sizeof(std::int16_t) // the position delta
                  + sizeof(std::uint8_t)     // the heading
                  + sizeof(std::uint16_t)    // the hit points
                  + sizeof(std::uint8_t)     // the order and the four stances
                == DEVICE_CHANGE_MAX_BYTES,
              "a device change is 14 bytes plus its mask (TechnicalDesign.md §5.7)");

struct StructureState
{
  std::uint32_t id;
  std::uint32_t design;
  std::uint8_t seat;
  std::uint16_t cellX; ///< Cells, not wire units: a structure is placed on the grid
  std::uint16_t cellY;
  std::int32_t y; ///< The flattened height under the footprint, in wire units
  StructurePhase phase;
  std::uint16_t hitPoints;
  std::uint8_t buildPercent;                               ///< 0 to 100, which is what a client draws
  std::array<std::uint8_t, MAX_STRUCTURE_MODULES> modules; ///< Row indices; the first moduleCount count
  std::uint8_t moduleCount;

  [[nodiscard]] constexpr bool operator==(const StructureState&) const noexcept = default;
};

struct WreckState
{
  std::uint32_t id;
  std::uint32_t design;
  std::uint8_t seat;
  std::uint8_t origin; ///< The ObjectKind the wreck was, which says which table `design` indexes
  std::int32_t x;
  std::int32_t y;
  std::int32_t z;
  std::uint8_t heading;

  [[nodiscard]] constexpr bool operator==(const WreckState&) const noexcept = default;
};

struct FeatureState
{
  std::uint32_t id;
  std::uint32_t design;
  std::uint16_t cellX;
  std::uint16_t cellY;
  std::int32_t y;
  std::uint8_t heading;

  [[nodiscard]] constexpr bool operator==(const FeatureState&) const noexcept = default;
};

/// A commander's own state (§5.3). Sent to that commander and to nobody else: the stockpile of a
/// seat a client is not sitting in is not its business, and the interest test of N2 asserts it.
struct SeatState
{
  std::uint8_t seat;
  std::int32_t powerHundredths;
  std::int32_t stockpileCapHundredths;
  std::int64_t extractedHundredths; ///< What the survival clock is settled on (GameDesign.md §2)
  std::uint32_t researchItem;       ///< The row a lab is on, or 0xFFFFFFFF for none
  std::uint32_t researchRemainingTicks;
  std::uint8_t victory; ///< A VictoryState (Sim/Victory.h)
  std::uint16_t deviceCount;
  std::uint16_t deviceCap;
  std::uint16_t structureCount;
  std::uint16_t structureCap;

  /// The seat's most recent refused order, for the one line of warning text Design/Interface.md §6
  /// draws for two seconds and then replaces.
  ///
  /// NOT A LIST, although Sim/Seat.h keeps one. The display shows one refusal at a time, and a
  /// variable-length field in a record the encoder compares by value would be walked and sent every
  /// frame for a line that is usually the same one. THE SEQUENCE IS WHY THREE FIELDS AND NOT TWO:
  /// two identical refusals in a row are equal by value, so without a counter a commander who asks
  /// twice for what he cannot afford would see the line fail to restart and read it as not having
  /// been heard. It counts refusals rather than naming a tick, so the client compares it for
  /// inequality and never for order; it wraps, and skips 0 when it does, because 0 is how a seat
  /// that has had no refusal at all says so.
  std::uint16_t rejectSequence;
  std::uint8_t rejectKind;   ///< An OrderKind. Meaningless, and zero, while rejectSequence is 0
  std::uint8_t rejectReason; ///< A RejectReason, never Accepted while rejectSequence is not 0

  [[nodiscard]] constexpr bool operator==(const SeatState&) const noexcept = default;
};

/// No lab is researching: a row index no table has, rather than a flag beside the two fields.
inline constexpr std::uint32_t NO_RESEARCH_ITEM = 0xFFFFFFFFu;

/// What a design is made of, sent with the first device of that design a client sees (§5.3), so
/// that a client can show what it is fighting rather than a nameless box.
struct DesignState
{
  std::uint8_t seat;
  std::uint32_t index; ///< Which of the seat's designs this is; DeviceState::design names it
  std::uint32_t chassis;
  std::uint32_t drive;
  std::array<std::uint32_t, MAX_MOUNTS> modules;
  std::uint8_t moduleCount;

  [[nodiscard]] constexpr bool operator==(const DesignState&) const noexcept = default;
};

/// What happened in the interval a frame covers (§5.3). A destruction carries no geometry: its
/// source is the id of the thing that died, and the client still holds that object's last record
/// from the frame before, which is everything the debris of §6.2 needs.
enum class EventKind : std::uint8_t
{
  Shot,       ///< A weapon fired: source is the shooter, target what it aimed at
  Impact,     ///< A projectile landed at the position
  Destroyed,  ///< The object at source died; target is what killed it, or 0
  Completed,  ///< A structure or a module finished; source is the structure
  Produced,   ///< A device left a factory; source is the factory, target the device
  Researched, ///< An item completed; source is the lab
  RankedUp    ///< A device reached a new rank; source is the device
};

inline constexpr std::uint8_t EVENT_KIND_COUNT = 7;

struct Event
{
  EventKind kind;
  std::uint32_t source;
  ObjectKind sourceKind;
  std::uint32_t target;
  ObjectKind targetKind;
  std::int32_t x; ///< Wire units
  std::int32_t y;
  std::int32_t z;
  std::uint32_t tick; ///< When it happened, so that a client can place it inside the interval

  [[nodiscard]] constexpr bool operator==(const Event&) const noexcept = default;
};

/// One run of cells that changed fog state, in the commander's own grid (§5.3). Runs rather than
/// cells because fog changes in bands as a viewer moves, and a run of a thousand cells is seven
/// bytes. The cell index is row-major over the grid's side, which the client holds from the
/// landscape definition it was given at the join.
struct FogDelta
{
  std::uint32_t firstCell;
  std::uint16_t cells;
  FogState state;

  [[nodiscard]] constexpr bool operator==(const FogDelta&) const noexcept = default;
};

/// An order on its way to the host, carried by the reliable stream of §5.5. The sequence is the
/// stream's, not the simulation's: it is what an acknowledgement names and what a resend repeats,
/// and the order's own tick is what the simulation applies it on.
struct OrderMessage
{
  std::uint32_t sequence;
  Order order;

  [[nodiscard]] constexpr bool operator==(const OrderMessage&) const noexcept = default;
};

// ── The byte layout ─────────────────────────────────────────────────────────────────────────
//
// One writer and one reader a record, and the reader returns false rather than a half-read record
// for a stream that is short or that names a value an enumeration does not have. Every count a
// reader trusts is bounded by the caller, never by the stream.

void Write(Neuron::ByteWriter& _writer, const DeviceState& _record);
[[nodiscard]] bool Read(Neuron::ByteReader& _reader, DeviceState& _out);
void Write(Neuron::ByteWriter& _writer, const DeviceChange& _record);
[[nodiscard]] bool Read(Neuron::ByteReader& _reader, DeviceChange& _out);
void Write(Neuron::ByteWriter& _writer, const StructureState& _record);
[[nodiscard]] bool Read(Neuron::ByteReader& _reader, StructureState& _out);
void Write(Neuron::ByteWriter& _writer, const WreckState& _record);
[[nodiscard]] bool Read(Neuron::ByteReader& _reader, WreckState& _out);
void Write(Neuron::ByteWriter& _writer, const FeatureState& _record);
[[nodiscard]] bool Read(Neuron::ByteReader& _reader, FeatureState& _out);
void Write(Neuron::ByteWriter& _writer, const SeatState& _record);
[[nodiscard]] bool Read(Neuron::ByteReader& _reader, SeatState& _out);
void Write(Neuron::ByteWriter& _writer, const DesignState& _record);
[[nodiscard]] bool Read(Neuron::ByteReader& _reader, DesignState& _out);
void Write(Neuron::ByteWriter& _writer, const Event& _record);
[[nodiscard]] bool Read(Neuron::ByteReader& _reader, Event& _out);
void Write(Neuron::ByteWriter& _writer, const FogDelta& _record);
[[nodiscard]] bool Read(Neuron::ByteReader& _reader, FogDelta& _out);
void Write(Neuron::ByteWriter& _writer, const OrderMessage& _record);
[[nodiscard]] bool Read(Neuron::ByteReader& _reader, OrderMessage& _out);

/// The bytes each record takes, for the encoder's budget and for the tests that pin them. A
/// DeviceChange is the one that varies, so its entry is the worst case.
inline constexpr std::size_t DEVICE_STATE_BYTES = 31;
inline constexpr std::size_t STRUCTURE_STATE_BYTES = 26;
inline constexpr std::size_t WRECK_STATE_BYTES = 23;
inline constexpr std::size_t FEATURE_STATE_BYTES = 17;
inline constexpr std::size_t SEAT_STATE_BYTES = 38; ///< 34, and four for the refusal (m1/N4)
inline constexpr std::size_t DESIGN_STATE_BYTES = 46;
inline constexpr std::size_t EVENT_BYTES = 27;
inline constexpr std::size_t FOG_DELTA_BYTES = 7;

} // namespace Frontier
