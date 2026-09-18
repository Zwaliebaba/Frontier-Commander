#pragma once

#include "Sim.h"

#include "ByteReader.h"
#include "ByteWriter.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Frontier
{

/// "FCSP", little-endian, at the head of every snapshot.
inline constexpr std::uint32_t SNAPSHOT_MAGIC = 0x50534346u;
/// 7 since 2026-09-18: 4 brought the world's five object maps and the seat's economy, research,
/// designs, caps, fog grid and ghost store (m1-vertical-slice/S1); 5 brought a device's primary
/// order and stances and a seat's dropped orders (S2); 6 brought the content hash the stream is
/// bound to (OpenQuestions.md Q20); 7 brings the lobby's device cap (S3).
inline constexpr std::uint16_t SNAPSHOT_VERSION = 7;

/// The full serialisation of a Sim through the versioned byte stream (TechnicalDesign.md §4.9),
/// in the layout ADR-003 fixes: the header, the settings, the tick, the Random state, the seats,
/// the landscape's definition and deltas, the outcome, the pending orders, and a digest of everything written so that a truncated or
/// altered stream is refused rather than read. A Sim read from a snapshot is indistinguishable
/// from the one written: the same hash now and the same hashes after any number of ticks.
class Snapshot
{
public:
  static void Write(const Sim& _sim, Neuron::ByteWriter& _writer);
  [[nodiscard]] static std::vector<std::byte> Write(const Sim& _sim);

  /// The Sim the bytes hold, or nothing when the stream is short, of another version, altered, or
  /// **written against tables other than _content**: a match reloaded against different rules is a
  /// different match, and the hash the stream carries turns that from a divergence nobody notices
  /// into a refusal here (OpenQuestions.md Q20, owner 2026-09-18). The tree must outlive the Sim.
  [[nodiscard]] static std::optional<Sim> Read(std::span<const std::byte> _bytes, const ContentTree& _content);

  /// The most orders a snapshot may carry pending, a bound on a hostile file rather than a limit
  /// a match reaches: eight seats at one order a tick for a minute is under a thousand.
  static constexpr std::uint32_t MAX_PENDING_ORDERS = 1u << 20;
  static constexpr std::uint32_t MAX_TILES = 4096;
  static constexpr std::uint32_t MAX_POSITIONS = 4096;
  static constexpr std::uint32_t MAX_DELTAS = 1u << 20;
  static constexpr std::uint32_t MAX_PALETTE_BYTES = 256;
  /// Bounds on a hostile file rather than limits a match reaches: a Frontier landscape's cell
  /// count is 1,048,576, and no match approaches a million objects of one kind.
  static constexpr std::uint32_t MAX_OBJECTS = 1u << 20;
  static constexpr std::uint32_t MAX_FOG_CELLS = 1u << 20;
  static constexpr std::uint32_t MAX_DESIGNS = MAX_SAVED_DESIGNS;
  static constexpr std::uint32_t MAX_RESEARCH = 4096;
  static constexpr std::uint32_t MAX_GHOSTS = 1u << 20;
  /// A tick cannot refuse more orders than a hostile client can send in one.
  static constexpr std::uint32_t MAX_REJECTIONS = 1u << 16;
};

} // namespace Frontier
