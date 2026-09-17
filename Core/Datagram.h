#pragma once

#include "ByteWriter.h"
#include "Transport.h"

#include <cstddef>
#include <cstdint>
#include <span>

// Datagram framing (TechnicalDesign.md §5.6): a protocol version and a payload length at the head
// of every datagram, so that a datagram of another version or a short one is dropped and counted
// rather than parsed. The version changes whenever any record Net puts in a payload changes shape;
// two builds that disagree on it never parse each other's bytes.

namespace Neuron
{

inline constexpr std::uint16_t PROTOCOL_VERSION = 1;
inline constexpr std::size_t DATAGRAM_HEADER_BYTES = 2 + 2; ///< The version and the payload length
inline constexpr std::size_t MAX_DATAGRAM_PAYLOAD_BYTES = MAX_DATAGRAM_BYTES - DATAGRAM_HEADER_BYTES;

/// What the unframer dropped, by reason; Net reports them.
struct FramingCounters
{
  std::uint32_t wrongVersion = 0;
  std::uint32_t tooShort = 0; ///< Shorter than the header, or than the length the header claims
  std::uint32_t tooLong = 0;  ///< Bytes after the payload the header claims
};

/// Writes the header and the payload into _out; false, with nothing written, for a payload over
/// MAX_DATAGRAM_PAYLOAD_BYTES.
[[nodiscard]] bool FrameDatagram(std::span<const std::byte> _payload, ByteWriter& _out);

/// The payload of a framed datagram, as a view into _datagram; false, with the reason counted,
/// for another version, a short datagram or a long one.
[[nodiscard]] bool UnframeDatagram(std::span<const std::byte> _datagram, std::span<const std::byte>& _payload, FramingCounters& _counters);

} // namespace Neuron
