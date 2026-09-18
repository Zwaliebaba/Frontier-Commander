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
/// 3 since 2026-09-18: a tile carries the biome it is coloured by (OpenQuestions.md Q18).
inline constexpr std::uint16_t SNAPSHOT_VERSION = 3;

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

  /// The Sim the bytes hold, or nothing when the stream is short, of another version, or altered.
  [[nodiscard]] static std::optional<Sim> Read(std::span<const std::byte> _bytes);

  /// The most orders a snapshot may carry pending, a bound on a hostile file rather than a limit
  /// a match reaches: eight seats at one order a tick for a minute is under a thousand.
  static constexpr std::uint32_t MAX_PENDING_ORDERS = 1u << 20;
  static constexpr std::uint32_t MAX_TILES = 4096;
  static constexpr std::uint32_t MAX_POSITIONS = 4096;
  static constexpr std::uint32_t MAX_DELTAS = 1u << 20;
  static constexpr std::uint32_t MAX_PALETTE_BYTES = 256;
};

} // namespace Frontier
