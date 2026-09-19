#pragma once

#include "Messages.h"

#include "Datagram.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Splitting a frame too large for a datagram (TechnicalDesign.md §5.3). Numbered pieces, no
// redundancy and no retransmission: the client applies a frame only when every piece has arrived
// and discards one with a piece missing, which costs a larger next delta and nothing else.

namespace Frontier
{

/// What one fragment may carry, which is the datagram's payload less the Fragment message's own
/// header: the kind, the frame sequence, the index, the count and the length prefix on the bytes.
inline constexpr std::size_t FRAGMENT_HEADER_BYTES = 1 + 4 + 1 + 1 + 4;
inline constexpr std::size_t FRAGMENT_PAYLOAD_BYTES = Neuron::MAX_DATAGRAM_PAYLOAD_BYTES - FRAGMENT_HEADER_BYTES;

/// Splits an encoded frame into numbered fragments. False, with nothing written, when the frame
/// would need more than MAX_FRAGMENTS pieces - which is a frame of over 75 KB, far past anything
/// one commander can see, and a bug rather than a big battle.
[[nodiscard]] bool SplitIntoFragments(std::uint32_t _frameSequence, std::span<const std::byte> _payload, std::vector<Fragment>& _out);

/// How many pieces a payload of this size takes; 1 when it fits in one datagram whole.
[[nodiscard]] std::size_t FragmentCount(std::size_t _payloadBytes) noexcept;

} // namespace Frontier
