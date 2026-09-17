#include "pch.h"

#include "TextureFile.h"

#include "Assertion.h"
#include "ByteReader.h"

#include <array>
#include <cstdio>
#include <utility>

namespace Neuron
{

namespace
{

// The DDS layout, from the Direct3D programming guide ("DDS_HEADER structure", "DDS_HEADER_DXT10
// structure", "DDS_PIXELFORMAT structure"), byte for byte: a magic, the 124-byte header holding a
// 32-byte pixel format, and the 20-byte DX10 extension when the FourCC is "DX10".

inline constexpr std::uint32_t DDS_MAGIC = 0x20534444u; // "DDS "
inline constexpr std::uint32_t DDS_HEADER_SIZE = 124;
inline constexpr std::uint32_t DDS_PIXELFORMAT_SIZE = 32;

inline constexpr std::uint32_t DDSD_CAPS = 0x1;
inline constexpr std::uint32_t DDSD_HEIGHT = 0x2;
inline constexpr std::uint32_t DDSD_WIDTH = 0x4;
inline constexpr std::uint32_t DDSD_PIXELFORMAT = 0x1000;
inline constexpr std::uint32_t DDSD_MIPMAPCOUNT = 0x20000;
inline constexpr std::uint32_t DDSD_DEPTH = 0x800000;
inline constexpr std::uint32_t DDSD_REQUIRED = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;

inline constexpr std::uint32_t DDPF_ALPHAPIXELS = 0x1;
inline constexpr std::uint32_t DDPF_FOURCC = 0x4;
inline constexpr std::uint32_t DDPF_RGB = 0x40;
inline constexpr std::uint32_t DDPF_LUMINANCE = 0x20000;

inline constexpr std::uint32_t DDSCAPS2_CUBEMAP = 0x200;
inline constexpr std::uint32_t DDSCAPS2_VOLUME = 0x200000;

inline constexpr std::uint32_t D3D10_RESOURCE_DIMENSION_TEXTURE2D = 3;
inline constexpr std::uint32_t D3D10_RESOURCE_MISC_TEXTURECUBE = 0x4;

// dxgiformat.h values.
inline constexpr std::uint32_t DXGI_FORMAT_R8G8B8A8_UNORM = 28;
inline constexpr std::uint32_t DXGI_FORMAT_R8G8B8A8_UNORM_SRGB = 29;
inline constexpr std::uint32_t DXGI_FORMAT_R8_UNORM = 61;
inline constexpr std::uint32_t DXGI_FORMAT_BC1_UNORM = 71;
inline constexpr std::uint32_t DXGI_FORMAT_BC1_UNORM_SRGB = 72;
inline constexpr std::uint32_t DXGI_FORMAT_BC2_UNORM = 74;
inline constexpr std::uint32_t DXGI_FORMAT_BC3_UNORM = 77;
inline constexpr std::uint32_t DXGI_FORMAT_BC3_UNORM_SRGB = 78;
inline constexpr std::uint32_t DXGI_FORMAT_B8G8R8A8_UNORM = 87;
inline constexpr std::uint32_t DXGI_FORMAT_B8G8R8A8_UNORM_SRGB = 91;
inline constexpr std::uint32_t DXGI_FORMAT_BC7_UNORM = 98;
inline constexpr std::uint32_t DXGI_FORMAT_BC7_UNORM_SRGB = 99;

[[nodiscard]] constexpr std::uint32_t FourCc(char _a, char _b, char _c, char _d) noexcept
{
  return static_cast<std::uint32_t>(static_cast<unsigned char>(_a)) | (static_cast<std::uint32_t>(static_cast<unsigned char>(_b)) << 8) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(_c)) << 16) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(_d)) << 24);
}

inline constexpr std::uint32_t FOURCC_DXT1 = FourCc('D', 'X', 'T', '1');
inline constexpr std::uint32_t FOURCC_DXT3 = FourCc('D', 'X', 'T', '3');
inline constexpr std::uint32_t FOURCC_DXT5 = FourCc('D', 'X', 'T', '5');
inline constexpr std::uint32_t FOURCC_DX10 = FourCc('D', 'X', '1', '0');

struct PixelFormat
{
  std::uint32_t size;
  std::uint32_t flags;
  std::uint32_t fourCc;
  std::uint32_t rgbBitCount;
  std::uint32_t rBitMask;
  std::uint32_t gBitMask;
  std::uint32_t bBitMask;
  std::uint32_t aBitMask;
};

struct Header
{
  std::uint32_t size;
  std::uint32_t flags;
  std::uint32_t height;
  std::uint32_t width;
  std::uint32_t pitchOrLinearSize;
  std::uint32_t depth;
  std::uint32_t mipMapCount;
  std::array<std::uint32_t, 11> reserved1;
  PixelFormat pixelFormat;
  std::uint32_t caps;
  std::uint32_t caps2;
  std::uint32_t caps3;
  std::uint32_t caps4;
  std::uint32_t reserved2;
};

struct HeaderDxt10
{
  std::uint32_t dxgiFormat;
  std::uint32_t resourceDimension;
  std::uint32_t miscFlag;
  std::uint32_t arraySize;
  std::uint32_t miscFlags2;
};

/// The refusal, with the byte the reader was at.
class Refusal
{
public:
  explicit Refusal(std::string& _error)
    : m_error(_error)
  {
  }

  bool At(std::size_t _offset, const char* _what)
  {
    m_error = "byte " + std::to_string(_offset) + ": " + _what;
    return false;
  }

  bool At(std::size_t _offset, const std::string& _what)
  {
    return At(_offset, _what.c_str());
  }

private:
  std::string& m_error;
};

[[nodiscard]] bool ReadHeader(ByteReader& _reader, Header& _out)
{
  PixelFormat& pf = _out.pixelFormat;
  bool ok = _reader.Read(_out.size) && _reader.Read(_out.flags) && _reader.Read(_out.height) && _reader.Read(_out.width) &&
            _reader.Read(_out.pitchOrLinearSize) && _reader.Read(_out.depth) && _reader.Read(_out.mipMapCount);
  for (std::uint32_t& word : _out.reserved1)
  {
    ok = ok && _reader.Read(word);
  }
  ok = ok && _reader.Read(pf.size) && _reader.Read(pf.flags) && _reader.Read(pf.fourCc) && _reader.Read(pf.rgbBitCount) &&
       _reader.Read(pf.rBitMask) && _reader.Read(pf.gBitMask) && _reader.Read(pf.bBitMask) && _reader.Read(pf.aBitMask);
  return ok && _reader.Read(_out.caps) && _reader.Read(_out.caps2) && _reader.Read(_out.caps3) && _reader.Read(_out.caps4) &&
         _reader.Read(_out.reserved2);
}

[[nodiscard]] std::string FourCcText(std::uint32_t _fourCc)
{
  std::string text;
  for (int index = 0; index < 4; ++index)
  {
    const char c = static_cast<char>((_fourCc >> (8 * index)) & 0xFF);
    text.push_back((c >= 0x20 && c < 0x7F) ? c : '?');
  }
  return text;
}

[[nodiscard]] std::string Hex(std::uint32_t _value)
{
  char buffer[16];
  std::snprintf(buffer, sizeof buffer, "0x%08X", static_cast<unsigned>(_value));
  return buffer;
}

/// The admitted format of a DXGI_FORMAT value, refusing by name; false when there is none.
[[nodiscard]] bool FormatOfDxgi(std::uint32_t _dxgi, TextureFormat& _out, std::string& _why)
{
  switch (_dxgi)
  {
  case DXGI_FORMAT_B8G8R8A8_UNORM:
    _out = TextureFormat::B8G8R8A8Unorm;
    return true;
  case DXGI_FORMAT_R8G8B8A8_UNORM:
    _out = TextureFormat::R8G8B8A8Unorm;
    return true;
  case DXGI_FORMAT_R8_UNORM:
    _out = TextureFormat::R8Unorm;
    return true;
  case DXGI_FORMAT_BC1_UNORM:
    _out = TextureFormat::Bc1Unorm;
    return true;
  case DXGI_FORMAT_BC3_UNORM:
    _out = TextureFormat::Bc3Unorm;
    return true;
  case DXGI_FORMAT_BC7_UNORM:
    _out = TextureFormat::Bc7Unorm;
    return true;
  case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
  case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
  case DXGI_FORMAT_BC1_UNORM_SRGB:
  case DXGI_FORMAT_BC3_UNORM_SRGB:
  case DXGI_FORMAT_BC7_UNORM_SRGB:
    _why = "DXGI_FORMAT " + std::to_string(_dxgi) + " is an _SRGB variant, refused for now (TechnicalDesign.md §8)";
    return false;
  case DXGI_FORMAT_BC2_UNORM:
    _why = "DXGI_FORMAT 74 (BC2_UNORM, the DXT3 FourCC) is not admitted";
    return false;
  default:
    _why = "DXGI_FORMAT " + std::to_string(_dxgi) +
           " is not admitted; the formats are B8G8R8A8_UNORM, R8G8B8A8_UNORM, R8_UNORM, BC1_UNORM, BC3_UNORM and BC7_UNORM";
    return false;
  }
}

} // namespace

std::uint32_t DxgiFormatOf(TextureFormat _format) noexcept
{
  switch (_format)
  {
  case TextureFormat::B8G8R8A8Unorm:
    return DXGI_FORMAT_B8G8R8A8_UNORM;
  case TextureFormat::R8G8B8A8Unorm:
    return DXGI_FORMAT_R8G8B8A8_UNORM;
  case TextureFormat::R8Unorm:
    return DXGI_FORMAT_R8_UNORM;
  case TextureFormat::Bc1Unorm:
    return DXGI_FORMAT_BC1_UNORM;
  case TextureFormat::Bc3Unorm:
    return DXGI_FORMAT_BC3_UNORM;
  case TextureFormat::Bc7Unorm:
    return DXGI_FORMAT_BC7_UNORM;
  }
  return 0;
}

bool IsBlockCompressed(TextureFormat _format) noexcept
{
  return _format == TextureFormat::Bc1Unorm || _format == TextureFormat::Bc3Unorm || _format == TextureFormat::Bc7Unorm;
}

std::uint32_t BytesPerUnit(TextureFormat _format) noexcept
{
  switch (_format)
  {
  case TextureFormat::B8G8R8A8Unorm:
  case TextureFormat::R8G8B8A8Unorm:
    return 4;
  case TextureFormat::R8Unorm:
    return 1;
  case TextureFormat::Bc1Unorm:
    return 8;
  case TextureFormat::Bc3Unorm:
  case TextureFormat::Bc7Unorm:
    return 16;
  }
  return 0;
}

bool TextureFile::Read(std::span<const std::byte> _bytes, TextureFile& _out, std::string& _error)
{
  Refusal refuse(_error);
  ByteReader reader(_bytes);
  std::uint32_t magic = 0;
  if (!reader.Read(magic) || magic != DDS_MAGIC)
  {
    return refuse.At(0, "not a DDS file: the magic is not \"DDS \"");
  }
  Header header{};
  const std::size_t headerAt = reader.Position();
  if (!ReadHeader(reader, header))
  {
    return refuse.At(_bytes.size(), "the file ends inside the 124-byte header");
  }
  if (header.size != DDS_HEADER_SIZE)
  {
    return refuse.At(headerAt, "the header size is " + std::to_string(header.size) + ", and DDS_HEADER is 124 bytes");
  }
  if (header.pixelFormat.size != DDS_PIXELFORMAT_SIZE)
  {
    return refuse.At(headerAt + 72,
                     "the pixel format size is " + std::to_string(header.pixelFormat.size) + ", and DDS_PIXELFORMAT is 32 bytes");
  }
  if ((header.flags & DDSD_REQUIRED) != DDSD_REQUIRED)
  {
    return refuse.At(headerAt + 4,
                     "the header flags " + Hex(header.flags) + " lack one of DDSD_CAPS, DDSD_HEIGHT, DDSD_WIDTH and DDSD_PIXELFORMAT");
  }
  if (header.width == 0 || header.height == 0 || header.width > MAX_EXTENT || header.height > MAX_EXTENT)
  {
    return refuse.At(headerAt + 8,
                     "the extent " + std::to_string(header.width) + "x" + std::to_string(header.height) + " is outside 1..16384");
  }
  if ((header.flags & DDSD_DEPTH) != 0 || (header.caps2 & DDSCAPS2_VOLUME) != 0)
  {
    return refuse.At(headerAt + 4, "volume textures are refused (DDSD_DEPTH or DDSCAPS2_VOLUME is set)");
  }
  if ((header.caps2 & DDSCAPS2_CUBEMAP) != 0)
  {
    return refuse.At(headerAt + 112, "cube maps are refused (DDSCAPS2_CUBEMAP is set)");
  }

  const PixelFormat& pf = header.pixelFormat;
  TextureFormat format = TextureFormat::B8G8R8A8Unorm;
  std::string why;
  const std::size_t pixelFormatAt = headerAt + 72;
  if ((pf.flags & DDPF_FOURCC) != 0)
  {
    if (pf.fourCc == FOURCC_DX10)
    {
      HeaderDxt10 extension{};
      const std::size_t extensionAt = reader.Position();
      if (!reader.Read(extension.dxgiFormat) || !reader.Read(extension.resourceDimension) || !reader.Read(extension.miscFlag) ||
          !reader.Read(extension.arraySize) || !reader.Read(extension.miscFlags2))
      {
        return refuse.At(_bytes.size(), "the file ends inside the 20-byte DX10 header");
      }
      if (extension.resourceDimension != D3D10_RESOURCE_DIMENSION_TEXTURE2D)
      {
        return refuse.At(extensionAt + 4, "the DX10 resource dimension is " + std::to_string(extension.resourceDimension) +
                                            "; only TEXTURE2D (3) is admitted");
      }
      if ((extension.miscFlag & D3D10_RESOURCE_MISC_TEXTURECUBE) != 0)
      {
        return refuse.At(extensionAt + 8, "cube maps are refused (D3D10_RESOURCE_MISC_TEXTURECUBE is set)");
      }
      if (extension.arraySize != 1)
      {
        return refuse.At(extensionAt + 12,
                         "texture arrays are refused (the DX10 array size is " + std::to_string(extension.arraySize) + ")");
      }
      if (!FormatOfDxgi(extension.dxgiFormat, format, why))
      {
        return refuse.At(extensionAt, why);
      }
    }
    else if (pf.fourCc == FOURCC_DXT1)
    {
      format = TextureFormat::Bc1Unorm;
    }
    else if (pf.fourCc == FOURCC_DXT5)
    {
      format = TextureFormat::Bc3Unorm;
    }
    else if (pf.fourCc == FOURCC_DXT3)
    {
      return refuse.At(pixelFormatAt + 8, "the FourCC DXT3 maps to BC2_UNORM, which is not admitted");
    }
    else
    {
      return refuse.At(pixelFormatAt + 8, "the FourCC \"" + FourCcText(pf.fourCc) + "\" names no admitted format");
    }
  }
  else if ((pf.flags & DDPF_RGB) != 0)
  {
    const bool alpha = (pf.flags & DDPF_ALPHAPIXELS) != 0 && pf.aBitMask == 0xFF000000u;
    if (pf.rgbBitCount == 32 && alpha && pf.rBitMask == 0x00FF0000u && pf.gBitMask == 0x0000FF00u && pf.bBitMask == 0x000000FFu)
    {
      format = TextureFormat::B8G8R8A8Unorm;
    }
    else if (pf.rgbBitCount == 32 && alpha && pf.rBitMask == 0x000000FFu && pf.gBitMask == 0x0000FF00u && pf.bBitMask == 0x00FF0000u)
    {
      format = TextureFormat::R8G8B8A8Unorm;
    }
    else
    {
      return refuse.At(pixelFormatAt + 12, "the RGB masks R=" + Hex(pf.rBitMask) + " G=" + Hex(pf.gBitMask) + " B=" + Hex(pf.bBitMask) +
                                             " A=" + Hex(pf.aBitMask) + " at " + std::to_string(pf.rgbBitCount) +
                                             " bits map to no admitted format (B8G8R8A8_UNORM or R8G8B8A8_UNORM)");
    }
  }
  else if ((pf.flags & DDPF_LUMINANCE) != 0)
  {
    if (pf.rgbBitCount == 8 && pf.rBitMask == 0xFFu)
    {
      format = TextureFormat::R8Unorm;
    }
    else
    {
      return refuse.At(pixelFormatAt + 12, "a luminance format at " + std::to_string(pf.rgbBitCount) + " bits with mask " +
                                             Hex(pf.rBitMask) + " maps to no admitted format (R8_UNORM is 8 bits, mask 0xFF)");
    }
  }
  else
  {
    return refuse.At(pixelFormatAt + 4,
                     "the pixel format flags " + Hex(pf.flags) + " name no admitted format (DDPF_FOURCC, DDPF_RGB or DDPF_LUMINANCE)");
  }

  // The mip chain, from the format's block size.
  std::uint32_t levelCount = 1;
  if ((header.flags & DDSD_MIPMAPCOUNT) != 0)
  {
    levelCount = header.mipMapCount;
  }
  std::uint32_t deepest = 1;
  for (std::uint32_t extent = std::max(header.width, header.height); extent > 1; extent >>= 1)
  {
    ++deepest;
  }
  if (levelCount == 0 || levelCount > deepest)
  {
    return refuse.At(headerAt + 24, "the mip count " + std::to_string(levelCount) + " is outside 1.." + std::to_string(deepest) + " for " +
                                      std::to_string(header.width) + "x" + std::to_string(header.height));
  }
  const bool compressed = IsBlockCompressed(format);
  const std::uint32_t unit = BytesPerUnit(format);
  std::vector<TextureLevel> levels;
  levels.reserve(levelCount);
  std::uint64_t total = 0;
  for (std::uint32_t index = 0; index < levelCount; ++index)
  {
    const std::uint32_t width = std::max(1u, header.width >> index);
    const std::uint32_t height = std::max(1u, header.height >> index);
    const std::uint32_t rowUnits = compressed ? (width + 3) / 4 : width;
    const std::uint32_t rows = compressed ? (height + 3) / 4 : height;
    const std::uint32_t rowBytes = rowUnits * unit;
    const std::uint32_t bytes = rowBytes * rows;
    levels.push_back({width, height, rowBytes, static_cast<std::uint32_t>(total), bytes});
    total += bytes;
  }
  const std::size_t payloadAt = reader.Position();
  if (reader.Remaining() != total)
  {
    return refuse.At(payloadAt, "the payload is " + std::to_string(reader.Remaining()) + " bytes, and the " + std::to_string(levelCount) +
                                  " level(s) of " + std::to_string(header.width) + "x" + std::to_string(header.height) + " need " +
                                  std::to_string(total));
  }
  _out.m_format = format;
  _out.m_levels = std::move(levels);
  _out.m_payload.assign(_bytes.begin() + static_cast<std::ptrdiff_t>(payloadAt), _bytes.end());
  return true;
}

const TextureLevel& TextureFile::Level(std::uint32_t _level) const noexcept
{
  FRONTIER_ASSERT(_level < m_levels.size());
  return m_levels[_level];
}

std::span<const std::byte> TextureFile::LevelBytes(std::uint32_t _level) const noexcept
{
  FRONTIER_ASSERT(_level < m_levels.size());
  const TextureLevel& level = m_levels[_level];
  return std::span<const std::byte>(m_payload).subspan(level.offset, level.bytes);
}

bool TextureFile::DecodeRgba8(std::uint32_t _level, std::vector<std::uint8_t>& _out) const
{
  if (_level >= m_levels.size() || IsBlockCompressed(m_format))
  {
    return false;
  }
  const TextureLevel& level = m_levels[_level];
  const std::span<const std::byte> bytes = LevelBytes(_level);
  const std::size_t pixels = static_cast<std::size_t>(level.width) * level.height;
  _out.resize(pixels * 4);
  for (std::size_t pixel = 0; pixel < pixels; ++pixel)
  {
    std::uint8_t* out = _out.data() + pixel * 4;
    switch (m_format)
    {
    case TextureFormat::R8G8B8A8Unorm:
      out[0] = std::to_integer<std::uint8_t>(bytes[pixel * 4]);
      out[1] = std::to_integer<std::uint8_t>(bytes[pixel * 4 + 1]);
      out[2] = std::to_integer<std::uint8_t>(bytes[pixel * 4 + 2]);
      out[3] = std::to_integer<std::uint8_t>(bytes[pixel * 4 + 3]);
      break;
    case TextureFormat::B8G8R8A8Unorm:
      out[0] = std::to_integer<std::uint8_t>(bytes[pixel * 4 + 2]);
      out[1] = std::to_integer<std::uint8_t>(bytes[pixel * 4 + 1]);
      out[2] = std::to_integer<std::uint8_t>(bytes[pixel * 4]);
      out[3] = std::to_integer<std::uint8_t>(bytes[pixel * 4 + 3]);
      break;
    case TextureFormat::R8Unorm:
      out[0] = out[1] = out[2] = std::to_integer<std::uint8_t>(bytes[pixel]);
      out[3] = 255;
      break;
    case TextureFormat::Bc1Unorm:
    case TextureFormat::Bc3Unorm:
    case TextureFormat::Bc7Unorm:
      return false;
    }
  }
  return true;
}

} // namespace Neuron
