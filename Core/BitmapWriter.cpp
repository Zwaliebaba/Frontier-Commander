#include "pch.h"

#include "BitmapWriter.h"

#include "Assert.h"
#include "ByteWriter.h"

namespace Neuron
{

namespace
{

inline constexpr std::uint32_t FILE_HEADER_BYTES = 14;
inline constexpr std::uint32_t INFO_HEADER_BYTES = 40;
inline constexpr std::uint16_t BITS_PER_PIXEL = 24;
inline constexpr std::int32_t PIXELS_PER_METRE = 2835; // 72 dots per inch, the customary value

[[nodiscard]] constexpr std::uint32_t RowBytes(std::uint32_t _width) noexcept
{
  return (_width * 3 + 3) & ~3u;
}

} // namespace

std::size_t BitmapBytes(std::uint32_t _width, std::uint32_t _height) noexcept
{
  return FILE_HEADER_BYTES + INFO_HEADER_BYTES + static_cast<std::size_t>(RowBytes(_width)) * _height;
}

std::vector<std::byte> WriteBitmap(std::uint32_t _width, std::uint32_t _height, std::span<const std::uint8_t> _rgba8)
{
  FRONTIER_ASSERT(_rgba8.size() == static_cast<std::size_t>(_width) * _height * 4);
  const std::uint32_t rowBytes = RowBytes(_width);
  const std::uint32_t imageBytes = rowBytes * _height;
  ByteWriter writer;
  // BITMAPFILEHEADER
  writer.Write(static_cast<std::uint8_t>('B'));
  writer.Write(static_cast<std::uint8_t>('M'));
  writer.Write(FILE_HEADER_BYTES + INFO_HEADER_BYTES + imageBytes);
  writer.Write(static_cast<std::uint16_t>(0));
  writer.Write(static_cast<std::uint16_t>(0));
  writer.Write(FILE_HEADER_BYTES + INFO_HEADER_BYTES);
  // BITMAPINFOHEADER
  writer.Write(INFO_HEADER_BYTES);
  writer.Write(static_cast<std::int32_t>(_width));
  writer.Write(static_cast<std::int32_t>(_height)); // positive: bottom-up
  writer.Write(static_cast<std::uint16_t>(1));
  writer.Write(BITS_PER_PIXEL);
  writer.Write(static_cast<std::uint32_t>(0)); // BI_RGB
  writer.Write(imageBytes);
  writer.Write(PIXELS_PER_METRE);
  writer.Write(PIXELS_PER_METRE);
  writer.Write(static_cast<std::uint32_t>(0));
  writer.Write(static_cast<std::uint32_t>(0));
  // The rows, bottom first, BGR, each padded to four bytes.
  const std::uint32_t padding = rowBytes - _width * 3;
  for (std::uint32_t row = _height; row > 0; --row)
  {
    const std::uint8_t* pixels = _rgba8.data() + static_cast<std::size_t>(row - 1) * _width * 4;
    for (std::uint32_t x = 0; x < _width; ++x)
    {
      const std::size_t at = static_cast<std::size_t>(x) * 4;
      writer.Write(pixels[at + 2]);
      writer.Write(pixels[at + 1]);
      writer.Write(pixels[at]);
    }
    for (std::uint32_t pad = 0; pad < padding; ++pad)
    {
      writer.Write(static_cast<std::uint8_t>(0));
    }
  }
  return writer.Release();
}

} // namespace Neuron
