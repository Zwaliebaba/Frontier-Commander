#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include "ByteWriter.h"

// The bounded half of the byte stream: every read checks the bytes that remain and reports
// failure through its return value rather than reading past the end, and a reader that has
// failed once stays failed, so that a caller can read a whole record and test once.

namespace Neuron
{

class ByteReader
{
public:
  explicit ByteReader(std::span<const std::byte> _bytes) noexcept
    : m_bytes(_bytes)
  {
  }

  template <class T>
    requires std::is_integral_v<T> && (!std::is_same_v<T, bool>)
  [[nodiscard]] bool Read(T& _out) noexcept
  {
    if (!Require(sizeof(T)))
    {
      return false;
    }
    using Unsigned = std::make_unsigned_t<T>;
    Unsigned bits = 0;
    for (std::size_t index = 0; index < sizeof(T); ++index)
    {
      bits =
        static_cast<Unsigned>(bits | (static_cast<Unsigned>(std::to_integer<std::uint8_t>(m_bytes[m_position + index])) << (8 * index)));
    }
    m_position += sizeof(T);
    _out = static_cast<T>(bits);
    return true;
  }

  template <class T>
    requires std::is_enum_v<T>
  [[nodiscard]] bool Read(T& _out) noexcept
  {
    std::underlying_type_t<T> value;
    if (!Read(value))
    {
      return false;
    }
    _out = static_cast<T>(value);
    return true;
  }

  [[nodiscard]] bool ReadBool(bool& _out) noexcept
  {
    std::uint8_t value;
    if (!Read(value))
    {
      return false;
    }
    _out = value != 0;
    return true;
  }

  /// Raw bytes of a known count, copied out.
  [[nodiscard]] bool ReadBytes(std::span<std::byte> _out) noexcept
  {
    if (!Require(_out.size()))
    {
      return false;
    }
    for (std::size_t index = 0; index < _out.size(); ++index)
    {
      _out[index] = m_bytes[m_position + index];
    }
    m_position += _out.size();
    return true;
  }

  /// A length-prefixed span, viewed in place; fails when the prefix exceeds the bytes that remain
  /// or the caller's limit, so that a hostile count cannot allocate or read anything.
  [[nodiscard]] bool ReadSpan(std::size_t _maxLength, std::span<const std::byte>& _out) noexcept
  {
    std::uint32_t length;
    if (!Read(length))
    {
      return false;
    }
    if (length > _maxLength || !Require(length))
    {
      return false;
    }
    _out = m_bytes.subspan(m_position, length);
    m_position += length;
    return true;
  }

  /// Reads the header and fails unless the magic matches and the version is within the range.
  [[nodiscard]] bool ReadHeader(std::uint32_t _magic, std::uint16_t _oldestVersion, std::uint16_t _newestVersion,
                                StreamHeader& _out) noexcept
  {
    if (!Read(_out.magic) || !Read(_out.version))
    {
      return false;
    }
    if (_out.magic != _magic || _out.version < _oldestVersion || _out.version > _newestVersion)
    {
      m_failed = true;
      return false;
    }
    return true;
  }

  [[nodiscard]] bool Failed() const noexcept
  {
    return m_failed;
  }
  [[nodiscard]] std::size_t Remaining() const noexcept
  {
    return m_bytes.size() - m_position;
  }
  [[nodiscard]] std::size_t Position() const noexcept
  {
    return m_position;
  }
  [[nodiscard]] bool AtEnd() const noexcept
  {
    return m_position == m_bytes.size();
  }

private:
  [[nodiscard]] bool Require(std::size_t _count) noexcept
  {
    if (m_failed || _count > Remaining())
    {
      m_failed = true;
      return false;
    }
    return true;
  }

  std::span<const std::byte> m_bytes;
  std::size_t m_position = 0;
  bool m_failed = false;
};

} // namespace Neuron
