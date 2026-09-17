#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Neuron
{

/// A 24-bit bottom-up BMP (BITMAPFILEHEADER, BITMAPINFOHEADER, BGR rows padded to four bytes) from
/// RGBA8 rows given top row first; the alpha is dropped. The capture mode's screenshots and nothing
/// else (TechnicalDesign.md §6.1): a capture is a picture the agent looks at, which is why it is
/// BMP where every texture is DDS, and nothing reads BMP at runtime.
[[nodiscard]] std::vector<std::byte> WriteBitmap(std::uint32_t _width, std::uint32_t _height, std::span<const std::uint8_t> _rgba8);

/// The size in bytes of the bitmap WriteBitmap produces for an extent.
[[nodiscard]] std::size_t BitmapBytes(std::uint32_t _width, std::uint32_t _height) noexcept;

} // namespace Neuron
