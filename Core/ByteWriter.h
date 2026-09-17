#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

// The serialisation snapshots, replays and the wire share (TechnicalDesign.md §4.9, §5.3): fixed
// width little-endian integers, byte spans with a length prefix, and a stream header of magic and
// version. Only trivially copyable integers cross the stream, and they cross as bytes written one
// at a time, never as an object representation, so the bytes are the same on every machine.

namespace Neuron
{

struct StreamHeader
{
  std::uint32_t magic;
  std::uint16_t version;
};

class ByteWriter
{
public:
  template <class T>
    requires std::is_integral_v<T> && (!std::is_same_v<T, bool>)
  void Write(T _value)
  {
    // Shift a 64-bit copy: a shift by eight on a one-byte operand is MSVC's C4333.
    std::uint64_t bits = static_cast<std::uint64_t>(static_cast<std::make_unsigned_t<T>>(_value));
    for (std::size_t index = 0; index < sizeof(T); ++index)
    {
      m_bytes.push_back(static_cast<std::byte>(bits & 0xFF));
      bits >>= 8;
    }
  }

  template <class T>
    requires std::is_enum_v<T>
  void Write(T _value)
  {
    Write(static_cast<std::underlying_type_t<T>>(_value));
  }

  void WriteBool(bool _value)
  {
    Write(static_cast<std::uint8_t>(_value ? 1 : 0));
  }

  /// Raw bytes with no prefix; the reader must know the count.
  void WriteBytes(std::span<const std::byte> _bytes)
  {
    m_bytes.insert(m_bytes.end(), _bytes.begin(), _bytes.end());
  }

  /// A byte span with a 32-bit length prefix, read back with ByteReader::ReadSpan.
  void WriteSpan(std::span<const std::byte> _bytes)
  {
    Write(static_cast<std::uint32_t>(_bytes.size()));
    WriteBytes(_bytes);
  }

  void WriteHeader(StreamHeader _header)
  {
    Write(_header.magic);
    Write(_header.version);
  }

  [[nodiscard]] std::span<const std::byte> Bytes() const noexcept
  {
    return m_bytes;
  }
  [[nodiscard]] std::size_t Size() const noexcept
  {
    return m_bytes.size();
  }
  void Clear() noexcept
  {
    m_bytes.clear();
  }

  /// Takes the bytes out, leaving the writer empty.
  [[nodiscard]] std::vector<std::byte> Release() noexcept
  {
    return std::move(m_bytes);
  }

private:
  std::vector<std::byte> m_bytes;
};

} // namespace Neuron
