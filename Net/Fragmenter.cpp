#include "pch.h"

#include "Fragmenter.h"

#include <algorithm>

namespace Frontier
{

std::size_t FragmentCount(std::size_t _payloadBytes) noexcept
{
  if (_payloadBytes == 0)
  {
    return 1;
  }
  return (_payloadBytes + FRAGMENT_PAYLOAD_BYTES - 1) / FRAGMENT_PAYLOAD_BYTES;
}

bool SplitIntoFragments(std::uint32_t _frameSequence, std::span<const std::byte> _payload, std::vector<Fragment>& _out)
{
  const std::size_t count = FragmentCount(_payload.size());
  if (count > MAX_FRAGMENTS)
  {
    return false;
  }
  _out.clear();
  for (std::size_t index = 0; index < count; ++index)
  {
    const std::size_t from = index * FRAGMENT_PAYLOAD_BYTES;
    const std::size_t to = std::min(_payload.size(), from + FRAGMENT_PAYLOAD_BYTES);
    Fragment piece{};
    piece.frameSequence = _frameSequence;
    piece.index = static_cast<std::uint8_t>(index);
    piece.count = static_cast<std::uint8_t>(count);
    piece.bytes.assign(_payload.begin() + static_cast<std::ptrdiff_t>(from), _payload.begin() + static_cast<std::ptrdiff_t>(to));
    _out.push_back(std::move(piece));
  }
  return true;
}

} // namespace Frontier
