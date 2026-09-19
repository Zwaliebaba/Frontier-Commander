#pragma once

#include "Records.h"

#include "MatchSettings.h"

#include "LandscapeDefinition.h"

#include "ByteReader.h"
#include "ByteWriter.h"

#include <array>
#include <cstdint>
#include <vector>

// The datagrams host and client exchange (TechnicalDesign.md §5.3 and §5.4). Every datagram starts
// with a MessageKind, so the reader knows what follows before it reads anything; Core's Datagram
// owns the framing under this - the version, the length and the bounds - and this file owns what
// is inside one.
//
// A MESSAGE IS NOT A RECORD. The records of Records.h are fixed-layout aggregates and a message is
// a header plus lists of them, so a message carries vectors and is read with a bound the caller
// gives rather than one the stream chose. Every bound here is a MAX_ constant below, and a count
// over it is a refusal: a hostile datagram must not be able to make a host reserve memory.

namespace Frontier
{

enum class MessageKind : std::uint8_t
{
  Join,
  JoinAccepted,
  JoinRefused,
  Frame,
  Fragment,
  Ack,
  Orders,
  Heartbeat
};

inline constexpr std::uint8_t MESSAGE_KIND_COUNT = 8;

/// A frame's payload is split at this size (§5.3): 1,200 bytes is under every common path MTU,
/// and a frame at or under it is one datagram with no fragment header at all.
inline constexpr std::size_t MAX_FRAME_PAYLOAD_BYTES = 1200;

inline constexpr std::size_t MAX_PLAYER_NAME_BYTES = 32;

// Bounds on a hostile datagram rather than limits a match reaches: a commander may field 300
// devices and hold 300 structures, so a frame that names more of either than eight commanders
// could own is refused before a vector is grown for it.
inline constexpr std::uint32_t MAX_FRAME_OBJECTS = 8 * (300 + 300);
inline constexpr std::uint32_t MAX_FRAME_EVENTS = 4096;
inline constexpr std::uint32_t MAX_FRAME_FOG_DELTAS = 4096;
inline constexpr std::uint32_t MAX_FRAME_DESIGNS = MAX_SAVED_DESIGNS;
inline constexpr std::uint32_t MAX_FRAGMENTS = 64;
inline constexpr std::uint32_t MAX_ORDERS_IN_ONE_MESSAGE = 64;
/// The landscape definition's own bounds, on the same footing: a Frontier landscape is one tile
/// list and one deposit list, and neither is large, but the stream chooses the counts.
inline constexpr std::uint32_t MAX_LANDSCAPE_TILES = 4096;
inline constexpr std::uint32_t MAX_LANDSCAPE_DEPOSITS = 1u << 16;
inline constexpr std::size_t MAX_PALETTE_NAME_BYTES = 256;
/// SizeClass has as many values as SIZE_CLASS_CELLS has entries, which is where the count lives.
inline constexpr std::uint8_t SIZE_CLASS_COUNT = static_cast<std::uint8_t>(SIZE_CLASS_CELLS.size());

/// Why a join was refused (§5.4). The reason is told to the player, so it names what he can do
/// something about and nothing else.
enum class RefusalReason : std::uint8_t
{
  ProtocolVersion, ///< His build speaks a different protocol
  ContentHash,     ///< His tables are not the host's; ADR-009's binding, at the join this time
  NoSeat,          ///< The match is full, or has started without room for him
  Banned
};

inline constexpr std::uint8_t REFUSAL_REASON_COUNT = 4;

struct Join
{
  std::uint16_t protocolVersion;
  std::uint64_t contentHash; ///< Content/ContentHash.h, the same digest a snapshot is bound by
  std::uint64_t token;       ///< The same token rejoins the same seat (§5.4)
  std::uint8_t nameBytes;
  std::array<char, MAX_PLAYER_NAME_BYTES> name; ///< Not null-terminated; nameBytes says how much is his

  [[nodiscard]] constexpr bool operator==(const Join&) const noexcept = default;
};

/// The answer to a Join: which seat he has, the match he has joined, the ground it is played on
/// and the tick it has reached. The full frame that follows is a Frame message of its own with no
/// baseline, so that the join path and the history-exhausted path are one path.
///
/// The landscape's DEFINITION travels and its flatten deltas do not (§5.2): the seed, the tiles,
/// the starts and the deposits are known to every commander from the first tick, and the terrain
/// under an unscouted base is not.
struct JoinAccepted
{
  std::uint8_t seat;
  std::uint32_t tick;
  MatchSettings settings;
  LandscapeDefinition landscape;

  [[nodiscard]] bool operator==(const JoinAccepted&) const noexcept = default;
};

struct JoinRefused
{
  RefusalReason reason;
  std::uint16_t hostProtocolVersion; ///< So that a mismatch says which build to fetch
  std::uint64_t hostContentHash;

  [[nodiscard]] constexpr bool operator==(const JoinRefused&) const noexcept = default;
};

/// What the host publishes every second tick (§5.3): what this client's commander can now see,
/// encoded against the newest frame he acknowledged.
struct Frame
{
  std::uint32_t sequence;
  std::uint32_t baselineSequence; ///< The frame this is a delta from; NO_BASELINE for a full frame
  std::uint32_t tick;

  std::vector<DeviceState> createdDevices;
  std::vector<StructureState> createdStructures;
  std::vector<WreckState> createdWrecks;
  std::vector<FeatureState> createdFeatures;
  std::vector<DesignState> designs;

  std::vector<DeviceChange> changedDevices;
  std::vector<StructureState> changedStructures; ///< Whole, not masked: a structure changes rarely

  std::vector<std::uint32_t> removed; ///< Ids, of any kind; the client knows which map holds each

  std::vector<Event> events;
  std::vector<FogDelta> fog;
  SeatState seat;

  [[nodiscard]] bool operator==(const Frame&) const noexcept = default;
};

/// A frame with no baseline: everything the commander can see, sent whole. It is what a joining
/// client gets and what a client whose acknowledged baseline has aged out of the history gets.
inline constexpr std::uint32_t NO_BASELINE = 0;

/// One piece of a frame too large for a datagram (§5.3). The client applies a frame only when
/// every piece has arrived and discards one with a piece missing, which costs nothing but a larger
/// next delta - so a fragment carries no redundancy and no retransmission.
struct Fragment
{
  std::uint32_t frameSequence;
  std::uint8_t index;
  std::uint8_t count;
  std::vector<std::byte> bytes;

  [[nodiscard]] bool operator==(const Fragment&) const noexcept = default;
};

/// What the client tells the host in every datagram it sends: the newest frame it applied, and
/// the order stream's cumulative acknowledgement (§5.5).
struct Ack
{
  std::uint32_t frameSequence;
  std::uint32_t orderSequence; ///< Every order up to and including this one has arrived

  [[nodiscard]] constexpr bool operator==(const Ack&) const noexcept = default;
};

/// Orders travelling client to host, with the acknowledgement that rides every datagram.
struct Orders
{
  Ack ack;
  std::vector<OrderMessage> orders;

  [[nodiscard]] bool operator==(const Orders&) const noexcept = default;
};

/// Liveness, both ways (§5.4). It carries the acknowledgement too, so that a client with nothing
/// to say still moves the order stream along and a host still learns what it has applied.
struct Heartbeat
{
  Ack ack;
  std::uint32_t tick;

  [[nodiscard]] constexpr bool operator==(const Heartbeat&) const noexcept = default;
};

// ── The byte layout ─────────────────────────────────────────────────────────────────────────
//
// Each writer puts the MessageKind first and each reader expects it to have been read already, so
// that a receiver can switch on the kind before it knows what the rest is. ReadMessageKind is the
// one that peels it off.

[[nodiscard]] bool ReadMessageKind(Neuron::ByteReader& _reader, MessageKind& _out);

void Write(Neuron::ByteWriter& _writer, const Join& _message);
[[nodiscard]] bool Read(Neuron::ByteReader& _reader, Join& _out);
void Write(Neuron::ByteWriter& _writer, const JoinAccepted& _message);
[[nodiscard]] bool Read(Neuron::ByteReader& _reader, JoinAccepted& _out);
void Write(Neuron::ByteWriter& _writer, const JoinRefused& _message);
[[nodiscard]] bool Read(Neuron::ByteReader& _reader, JoinRefused& _out);
void Write(Neuron::ByteWriter& _writer, const Frame& _message);
[[nodiscard]] bool Read(Neuron::ByteReader& _reader, Frame& _out);
void Write(Neuron::ByteWriter& _writer, const Fragment& _message);
[[nodiscard]] bool Read(Neuron::ByteReader& _reader, Fragment& _out);
void Write(Neuron::ByteWriter& _writer, const Ack& _message);
[[nodiscard]] bool Read(Neuron::ByteReader& _reader, Ack& _out);
void Write(Neuron::ByteWriter& _writer, const Orders& _message);
[[nodiscard]] bool Read(Neuron::ByteReader& _reader, Orders& _out);
void Write(Neuron::ByteWriter& _writer, const Heartbeat& _message);
[[nodiscard]] bool Read(Neuron::ByteReader& _reader, Heartbeat& _out);

} // namespace Frontier
