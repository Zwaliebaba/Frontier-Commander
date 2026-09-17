#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// The DDS reader (TechnicalDesign.md §8; ImplementationPlan.md §6, item 11): textures are DDS and
// nothing else, so one strict reader serves every pass and an upload is a copy, never a
// conversion. It reads the magic, the 124-byte header, the pixel format and the DX10 extension
// when the FourCC says so, maps the legacy FourCCs and the masked RGB(A) formats to their DXGI
// formats, admits six formats and refuses everything else by name, computes the mip chain from
// the format's block size and checks the payload against it before any level is exposed. The
// structures are written in TextureFile.cpp from the documented DDS_HEADER and DDS_HEADER_DXT10
// layouts rather than from ddraw.h, which pulls in far more than a reader needs.

namespace Neuron
{

/// The formats a texture may use (TechnicalDesign.md §8; the content ADR records them). The
/// _SRGB variants are refused for now, and everything else is refused by name.
enum class TextureFormat : std::uint8_t
{
  B8G8R8A8Unorm,
  R8G8B8A8Unorm,
  R8Unorm,
  Bc1Unorm,
  Bc3Unorm,
  Bc7Unorm
};

/// The DXGI_FORMAT value of a format, as dxgiformat.h numbers it, for the upload path.
[[nodiscard]] std::uint32_t DxgiFormatOf(TextureFormat _format) noexcept;

[[nodiscard]] bool IsBlockCompressed(TextureFormat _format) noexcept;

/// Bytes per pixel of an uncompressed format, or per 4x4 block of a compressed one.
[[nodiscard]] std::uint32_t BytesPerUnit(TextureFormat _format) noexcept;

struct TextureLevel
{
  std::uint32_t width;
  std::uint32_t height;
  std::uint32_t rowBytes; ///< One row of pixels, or of 4x4 blocks
  std::uint32_t offset;   ///< Into the payload
  std::uint32_t bytes;
};

class TextureFile
{
public:
  static constexpr std::uint32_t MAX_EXTENT = 16384;

  /// Parses a whole DDS file. False, with _error as "byte <offset>: <what>" and _out untouched,
  /// for anything the reader does not admit: it never reads past what it has checked.
  [[nodiscard]] static bool Read(std::span<const std::byte> _bytes, TextureFile& _out, std::string& _error);

  [[nodiscard]] TextureFormat Format() const noexcept
  {
    return m_format;
  }

  [[nodiscard]] std::uint32_t Width() const noexcept
  {
    return m_levels.empty() ? 0 : m_levels[0].width;
  }

  [[nodiscard]] std::uint32_t Height() const noexcept
  {
    return m_levels.empty() ? 0 : m_levels[0].height;
  }

  [[nodiscard]] std::uint32_t LevelCount() const noexcept
  {
    return static_cast<std::uint32_t>(m_levels.size());
  }

  [[nodiscard]] const TextureLevel& Level(std::uint32_t _level) const noexcept;

  /// A level's bytes as the file holds them, which the GPU takes unchanged.
  [[nodiscard]] std::span<const std::byte> LevelBytes(std::uint32_t _level) const noexcept;

  /// A level as RGBA8 (R, G, B, A per pixel, row by row, top row first) for a CPU consumer: the
  /// palette lookup and the font metrics. B8G8R8A8 is swizzled and R8 broadcast to R, R, R, 255.
  /// False for a block-compressed format, whose raw level is the upload path's and nobody else's.
  [[nodiscard]] bool DecodeRgba8(std::uint32_t _level, std::vector<std::uint8_t>& _out) const;

private:
  TextureFormat m_format = TextureFormat::B8G8R8A8Unorm;
  std::vector<TextureLevel> m_levels;
  std::vector<std::byte> m_payload;
};

} // namespace Neuron
