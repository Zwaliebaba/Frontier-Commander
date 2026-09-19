#pragma once

#include "Messages.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// Putting a fragmented frame back together (TechnicalDesign.md §5.3). The rule the design gives is
// the whole design: **a frame with a fragment missing is discarded**, not waited for and not asked
// for again. A frame is a delta against a baseline the client has acknowledged, so a frame that
// never completes costs one larger next frame and nothing else - and a retransmission would arrive
// after the frame it was meant to complete had been overtaken by a newer one anyway.

namespace Frontier
{

/// How many frames may be part-way through at once. Two is enough for the case this exists for -
/// the last fragment of one frame arriving after the first of the next - and a third would only
/// hold a frame that is already older than the client's baseline.
inline constexpr std::size_t MAX_PARTIAL_FRAMES = 2;

class Reassembler
{
public:
  struct Counters
  {
    std::uint32_t completed = 0;  ///< Frames every fragment of which arrived
    std::uint32_t abandoned = 0;  ///< Frames dropped with a fragment missing
    std::uint32_t duplicates = 0; ///< A fragment that had already arrived
    std::uint32_t mismatched = 0; ///< A fragment whose count disagrees with the frame's others
  };

  /// Takes one fragment. True, with _out filled, when this fragment was the last one missing; the
  /// partial frame it completed is then forgotten.
  [[nodiscard]] bool Add(const Fragment& _fragment, std::vector<std::byte>& _out);

  /// Forgets every partial frame; what a rejoin does, so that pieces of the old conversation
  /// cannot complete a frame in the new one.
  void Clear() noexcept;

  [[nodiscard]] const Counters& Statistics() const noexcept
  {
    return m_counters;
  }

private:
  struct Partial
  {
    std::uint32_t sequence = 0;
    std::uint8_t count = 0;
    std::uint8_t have = 0;
    std::vector<std::vector<std::byte>> pieces;
    std::vector<bool> arrived;
  };

  /// The slot this frame's fragments go in, making one if it is new; nullptr when the frame is
  /// older than both partials, which is a fragment of a frame already given up on.
  [[nodiscard]] Partial* SlotFor(const Fragment& _fragment);

  std::vector<Partial> m_partials;
  Counters m_counters;
};

} // namespace Frontier
