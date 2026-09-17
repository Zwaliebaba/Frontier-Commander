#include "pch.h"

#include "Datagram.h"

#include "ByteReader.h"

namespace Neuron
{

bool FrameDatagram(std::span<const std::byte> _payload, ByteWriter& _out)
{
  if (_payload.size() > MAX_DATAGRAM_PAYLOAD_BYTES)
  {
    return false;
  }
  _out.Write(PROTOCOL_VERSION);
  _out.Write(static_cast<std::uint16_t>(_payload.size()));
  _out.WriteBytes(_payload);
  return true;
}

bool UnframeDatagram(std::span<const std::byte> _datagram, std::span<const std::byte>& _payload, FramingCounters& _counters)
{
  ByteReader reader(_datagram);
  std::uint16_t version = 0;
  std::uint16_t length = 0;
  if (!reader.Read(version) || !reader.Read(length))
  {
    ++_counters.tooShort;
    return false;
  }
  if (version != PROTOCOL_VERSION)
  {
    ++_counters.wrongVersion;
    return false;
  }
  if (reader.Remaining() < length)
  {
    ++_counters.tooShort;
    return false;
  }
  if (reader.Remaining() > length)
  {
    ++_counters.tooLong;
    return false;
  }
  _payload = _datagram.subspan(DATAGRAM_HEADER_BYTES, length);
  return true;
}

} // namespace Neuron
