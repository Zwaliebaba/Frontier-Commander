#include "pch.h"

#include "TextureFile.h"

#include "ByteWriter.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace CoreTests
{

namespace
{

// The DDS constants the fixtures are built from, as the Direct3D programming guide lists them;
// the reader has its own copy, so a wrong value here fails a test rather than agreeing with itself.
inline constexpr std::uint32_t DDSD_CAPS = 0x1;
inline constexpr std::uint32_t DDSD_HEIGHT = 0x2;
inline constexpr std::uint32_t DDSD_WIDTH = 0x4;
inline constexpr std::uint32_t DDSD_PIXELFORMAT = 0x1000;
inline constexpr std::uint32_t DDSD_MIPMAPCOUNT = 0x20000;
inline constexpr std::uint32_t DDSD_DEPTH = 0x800000;
inline constexpr std::uint32_t DDPF_ALPHAPIXELS = 0x1;
inline constexpr std::uint32_t DDPF_FOURCC = 0x4;
inline constexpr std::uint32_t DDPF_RGB = 0x40;
inline constexpr std::uint32_t DDPF_LUMINANCE = 0x20000;
inline constexpr std::uint32_t DDSCAPS_TEXTURE = 0x1000;
inline constexpr std::uint32_t DDSCAPS2_CUBEMAP = 0x200;

constexpr std::uint32_t FourCc(const char (&_text)[5])
{
  return static_cast<std::uint32_t>(static_cast<unsigned char>(_text[0])) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(_text[1])) << 8) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(_text[2])) << 16) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(_text[3])) << 24);
}

/// A DDS file assembled field by field, so that each fixture states only what it changes.
struct DdsBuilder
{
  std::uint32_t magic = FourCc("DDS ");
  std::uint32_t headerSize = 124;
  std::uint32_t flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
  std::uint32_t height = 1;
  std::uint32_t width = 1;
  std::uint32_t mipMapCount = 0;
  std::uint32_t pixelFormatSize = 32;
  std::uint32_t pfFlags = 0;
  std::uint32_t fourCc = 0;
  std::uint32_t bitCount = 0;
  std::array<std::uint32_t, 4> masks = {0, 0, 0, 0};
  std::uint32_t caps = DDSCAPS_TEXTURE;
  std::uint32_t caps2 = 0;
  bool dx10 = false;
  std::uint32_t dxgiFormat = 0;
  std::uint32_t resourceDimension = 3;
  std::uint32_t miscFlag = 0;
  std::uint32_t arraySize = 1;
  std::vector<std::uint8_t> payload;

  [[nodiscard]] std::vector<std::byte> Build() const
  {
    Neuron::ByteWriter writer;
    writer.Write(magic);
    writer.Write(headerSize);
    writer.Write(flags);
    writer.Write(height);
    writer.Write(width);
    writer.Write(static_cast<std::uint32_t>(0)); // pitch or linear size, unread
    writer.Write(static_cast<std::uint32_t>(0)); // depth
    writer.Write(mipMapCount);
    for (int reserved = 0; reserved < 11; ++reserved)
    {
      writer.Write(static_cast<std::uint32_t>(0));
    }
    writer.Write(pixelFormatSize);
    writer.Write(pfFlags);
    writer.Write(fourCc);
    writer.Write(bitCount);
    for (const std::uint32_t mask : masks)
    {
      writer.Write(mask);
    }
    writer.Write(caps);
    writer.Write(caps2);
    writer.Write(static_cast<std::uint32_t>(0));
    writer.Write(static_cast<std::uint32_t>(0));
    writer.Write(static_cast<std::uint32_t>(0));
    if (dx10)
    {
      writer.Write(dxgiFormat);
      writer.Write(resourceDimension);
      writer.Write(miscFlag);
      writer.Write(arraySize);
      writer.Write(static_cast<std::uint32_t>(0));
    }
    for (const std::uint8_t byte : payload)
    {
      writer.Write(byte);
    }
    return writer.Release();
  }
};

DdsBuilder Bgra2x2TwoMips()
{
  DdsBuilder dds;
  dds.width = 2;
  dds.height = 2;
  dds.flags |= DDSD_MIPMAPCOUNT;
  dds.mipMapCount = 2;
  dds.pfFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
  dds.bitCount = 32;
  dds.masks = {0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u};
  // Level 0, four pixels as B, G, R, A; level 1, one pixel.
  dds.payload = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 21, 22, 23, 24};
  return dds;
}

DdsBuilder Bc1FourByFour()
{
  DdsBuilder dds;
  dds.width = 4;
  dds.height = 4;
  dds.pfFlags = DDPF_FOURCC;
  dds.fourCc = FourCc("DXT1");
  dds.payload = {0x00, 0xF8, 0x1F, 0x00, 0x00, 0x55, 0xAA, 0xFF}; // red, blue, and a block of indices
  return dds;
}

DdsBuilder R8Atlas()
{
  DdsBuilder dds;
  dds.width = 4;
  dds.height = 2;
  dds.pfFlags = DDPF_LUMINANCE;
  dds.bitCount = 8;
  dds.masks = {0xFFu, 0, 0, 0};
  dds.payload = {0, 1, 2, 3, 4, 5, 6, 7};
  return dds;
}

DdsBuilder Dx10Rgba()
{
  DdsBuilder dds;
  dds.width = 2;
  dds.height = 1;
  dds.pfFlags = DDPF_FOURCC;
  dds.fourCc = FourCc("DX10");
  dds.dx10 = true;
  dds.dxgiFormat = 28; // DXGI_FORMAT_R8G8B8A8_UNORM
  dds.payload = {10, 20, 30, 40, 50, 60, 70, 80};
  return dds;
}

void ExpectRefused(const DdsBuilder& _dds, const char* _fragment, const wchar_t* _case)
{
  const std::vector<std::byte> bytes = _dds.Build();
  Neuron::TextureFile texture;
  std::string error;
  Assert::IsFalse(Neuron::TextureFile::Read(bytes, texture, error), _case);
  Assert::IsTrue(error.rfind("byte ", 0) == 0, L"the error names a byte offset");
  if (error.find(_fragment) == std::string::npos)
  {
    const std::string message = std::string("expected '") + _fragment + "' in: " + error;
    Assert::Fail(std::wstring(message.begin(), message.end()).c_str());
  }
}

} // namespace

TEST_CLASS(TextureFileTests)
{
public:
  TEST_METHOD(AMaskedBgraFileWithTwoMipsReadsAndDecodes)
  {
    Neuron::TextureFile texture;
    std::string error;
    Assert::IsTrue(Neuron::TextureFile::Read(Bgra2x2TwoMips().Build(), texture, error), L"the file should read");
    Assert::IsTrue(texture.Format() == Neuron::TextureFormat::B8G8R8A8Unorm);
    Assert::AreEqual(87u, Neuron::DxgiFormatOf(texture.Format()));
    Assert::AreEqual(2u, texture.Width());
    Assert::AreEqual(2u, texture.Height());
    Assert::AreEqual(2u, texture.LevelCount());
    Assert::AreEqual(8u, texture.Level(0).rowBytes);
    Assert::AreEqual(16u, texture.Level(0).bytes);
    Assert::AreEqual(0u, texture.Level(0).offset);
    Assert::AreEqual(1u, texture.Level(1).width);
    Assert::AreEqual(1u, texture.Level(1).height);
    Assert::AreEqual(4u, texture.Level(1).bytes);
    Assert::AreEqual(16u, texture.Level(1).offset);
    Assert::AreEqual(static_cast<std::size_t>(4), texture.LevelBytes(1).size());
    Assert::AreEqual(21, static_cast<int>(std::to_integer<std::uint8_t>(texture.LevelBytes(1)[0])));
    std::vector<std::uint8_t> rgba;
    Assert::IsTrue(texture.DecodeRgba8(0, rgba));
    const std::vector<std::uint8_t> expected = {3, 2, 1, 4, 7, 6, 5, 8, 11, 10, 9, 12, 15, 14, 13, 16};
    Assert::IsTrue(rgba == expected, L"B8G8R8A8 is swizzled to RGBA");
    Assert::IsTrue(texture.DecodeRgba8(1, rgba));
    Assert::IsTrue(rgba == std::vector<std::uint8_t>{23, 22, 21, 24});
  }

  TEST_METHOD(ADxt1FileIsBc1AndItsBlockIsHandedOverUnchanged)
  {
    Neuron::TextureFile texture;
    std::string error;
    const DdsBuilder dds = Bc1FourByFour();
    Assert::IsTrue(Neuron::TextureFile::Read(dds.Build(), texture, error), L"the file should read");
    Assert::IsTrue(texture.Format() == Neuron::TextureFormat::Bc1Unorm);
    Assert::IsTrue(Neuron::IsBlockCompressed(texture.Format()));
    Assert::AreEqual(71u, Neuron::DxgiFormatOf(texture.Format()));
    Assert::AreEqual(1u, texture.LevelCount());
    Assert::AreEqual(8u, texture.Level(0).rowBytes);
    Assert::AreEqual(8u, texture.Level(0).bytes);
    const std::span<const std::byte> block = texture.LevelBytes(0);
    Assert::AreEqual(static_cast<std::size_t>(8), block.size());
    for (std::size_t index = 0; index < 8; ++index)
    {
      Assert::AreEqual(static_cast<int>(dds.payload[index]), static_cast<int>(std::to_integer<std::uint8_t>(block[index])));
    }
    std::vector<std::uint8_t> rgba;
    Assert::IsFalse(texture.DecodeRgba8(0, rgba), L"a block-compressed level is not decoded on the CPU");
  }

  TEST_METHOD(ALuminanceFileIsR8AndBroadcastsOnDecode)
  {
    Neuron::TextureFile texture;
    std::string error;
    Assert::IsTrue(Neuron::TextureFile::Read(R8Atlas().Build(), texture, error), L"the file should read");
    Assert::IsTrue(texture.Format() == Neuron::TextureFormat::R8Unorm);
    Assert::AreEqual(61u, Neuron::DxgiFormatOf(texture.Format()));
    Assert::AreEqual(4u, texture.Level(0).rowBytes);
    std::vector<std::uint8_t> rgba;
    Assert::IsTrue(texture.DecodeRgba8(0, rgba));
    Assert::AreEqual(static_cast<std::size_t>(32), rgba.size());
    for (std::size_t pixel = 0; pixel < 8; ++pixel)
    {
      Assert::AreEqual(static_cast<int>(pixel), static_cast<int>(rgba[pixel * 4]));
      Assert::AreEqual(static_cast<int>(pixel), static_cast<int>(rgba[pixel * 4 + 1]));
      Assert::AreEqual(static_cast<int>(pixel), static_cast<int>(rgba[pixel * 4 + 2]));
      Assert::AreEqual(255, static_cast<int>(rgba[pixel * 4 + 3]));
    }
  }

  TEST_METHOD(ADx10HeaderNamesTheFormat)
  {
    Neuron::TextureFile texture;
    std::string error;
    Assert::IsTrue(Neuron::TextureFile::Read(Dx10Rgba().Build(), texture, error), L"the file should read");
    Assert::IsTrue(texture.Format() == Neuron::TextureFormat::R8G8B8A8Unorm);
    std::vector<std::uint8_t> rgba;
    Assert::IsTrue(texture.DecodeRgba8(0, rgba));
    Assert::IsTrue(rgba == std::vector<std::uint8_t>{10, 20, 30, 40, 50, 60, 70, 80}, L"R8G8B8A8 is copied as it is");

    DdsBuilder bc7 = Bc1FourByFour();
    bc7.fourCc = FourCc("DX10");
    bc7.dx10 = true;
    bc7.dxgiFormat = 98; // DXGI_FORMAT_BC7_UNORM
    bc7.payload.resize(16);
    Assert::IsTrue(Neuron::TextureFile::Read(bc7.Build(), texture, error), L"a DX10 BC7 file should read");
    Assert::IsTrue(texture.Format() == Neuron::TextureFormat::Bc7Unorm);
    Assert::AreEqual(16u, texture.Level(0).bytes);

    DdsBuilder dxt5 = Bc1FourByFour();
    dxt5.fourCc = FourCc("DXT5");
    dxt5.payload.resize(16);
    Assert::IsTrue(Neuron::TextureFile::Read(dxt5.Build(), texture, error), L"a DXT5 file should read");
    Assert::IsTrue(texture.Format() == Neuron::TextureFormat::Bc3Unorm);
  }

  TEST_METHOD(TheMipChainOfABlockCompressedFileRoundsToBlocks)
  {
    DdsBuilder dds = Bc1FourByFour();
    dds.width = 8;
    dds.height = 4;
    dds.flags |= DDSD_MIPMAPCOUNT;
    dds.mipMapCount = 4; // 8x4, 4x2, 2x1, 1x1: two blocks, then one block each
    dds.payload.assign(16 + 8 + 8 + 8, 0);
    Neuron::TextureFile texture;
    std::string error;
    Assert::IsTrue(Neuron::TextureFile::Read(dds.Build(), texture, error), L"the file should read");
    Assert::AreEqual(4u, texture.LevelCount());
    Assert::AreEqual(16u, texture.Level(0).bytes);
    Assert::AreEqual(2u, texture.Level(1).height);
    Assert::AreEqual(8u, texture.Level(1).bytes);
    Assert::AreEqual(8u, texture.Level(3).bytes);
    Assert::AreEqual(32u, texture.Level(3).offset);
  }

  TEST_METHOD(EveryBrokenFileIsRefusedByNameWithALocation)
  {
    DdsBuilder magic = Bgra2x2TwoMips();
    magic.magic = FourCc("DDX ");
    ExpectRefused(magic, "magic", L"a wrong magic");

    DdsBuilder headerSize = Bgra2x2TwoMips();
    headerSize.headerSize = 100;
    ExpectRefused(headerSize, "DDS_HEADER is 124", L"a wrong header size");

    DdsBuilder pixelFormatSize = Bgra2x2TwoMips();
    pixelFormatSize.pixelFormatSize = 24;
    ExpectRefused(pixelFormatSize, "DDS_PIXELFORMAT is 32", L"a wrong pixel format size");

    DdsBuilder flags = Bgra2x2TwoMips();
    flags.flags &= ~DDSD_WIDTH;
    ExpectRefused(flags, "DDSD_WIDTH", L"a missing required flag");

    DdsBuilder extent = Bgra2x2TwoMips();
    extent.width = 0;
    ExpectRefused(extent, "outside 1..16384", L"a zero width");

    DdsBuilder volume = Bgra2x2TwoMips();
    volume.flags |= DDSD_DEPTH;
    ExpectRefused(volume, "volume", L"a volume texture");

    DdsBuilder cube = Bgra2x2TwoMips();
    cube.caps2 = DDSCAPS2_CUBEMAP;
    ExpectRefused(cube, "cube", L"a cube map");

    DdsBuilder mips = Bgra2x2TwoMips();
    mips.mipMapCount = 5;
    ExpectRefused(mips, "mip count", L"too many mips");

    DdsBuilder dxt3 = Bc1FourByFour();
    dxt3.fourCc = FourCc("DXT3");
    dxt3.payload.resize(16);
    ExpectRefused(dxt3, "BC2", L"DXT3");

    DdsBuilder unknownFourCc = Bc1FourByFour();
    unknownFourCc.fourCc = FourCc("ATI2");
    ExpectRefused(unknownFourCc, "\"ATI2\"", L"an unknown FourCC");

    DdsBuilder srgb = Dx10Rgba();
    srgb.dxgiFormat = 29; // R8G8B8A8_UNORM_SRGB
    ExpectRefused(srgb, "_SRGB", L"an sRGB variant");

    DdsBuilder floatFormat = Dx10Rgba();
    floatFormat.dxgiFormat = 10; // R16G16B16A16_FLOAT
    ExpectRefused(floatFormat, "not admitted", L"a format outside the six");

    DdsBuilder array = Dx10Rgba();
    array.arraySize = 6;
    ExpectRefused(array, "arrays", L"a texture array");

    DdsBuilder dimension = Dx10Rgba();
    dimension.resourceDimension = 4;
    ExpectRefused(dimension, "resource dimension", L"a 3D resource");

    DdsBuilder dx10Cube = Dx10Rgba();
    dx10Cube.miscFlag = 0x4;
    ExpectRefused(dx10Cube, "cube", L"a DX10 cube map");

    DdsBuilder rgb24 = Bgra2x2TwoMips();
    rgb24.pfFlags = DDPF_RGB;
    rgb24.bitCount = 24;
    rgb24.masks = {0xFF0000u, 0xFF00u, 0xFFu, 0};
    ExpectRefused(rgb24, "map to no admitted format", L"24-bit RGB");

    DdsBuilder luminance16 = R8Atlas();
    luminance16.bitCount = 16;
    luminance16.masks = {0xFFFFu, 0, 0, 0};
    ExpectRefused(luminance16, "R8_UNORM is 8 bits", L"16-bit luminance");

    DdsBuilder noFlags = Bgra2x2TwoMips();
    noFlags.pfFlags = 0;
    ExpectRefused(noFlags, "name no admitted format", L"empty pixel format flags");

    DdsBuilder shortPayload = Bgra2x2TwoMips();
    shortPayload.payload.pop_back();
    ExpectRefused(shortPayload, "need 20", L"a short payload");

    DdsBuilder longPayload = Bgra2x2TwoMips();
    longPayload.payload.push_back(0);
    ExpectRefused(longPayload, "need 20", L"a long payload");
  }

  TEST_METHOD(ATruncatedHeaderIsRefusedAndTheOutputIsUntouched)
  {
    Neuron::TextureFile texture;
    std::string error;
    Assert::IsTrue(Neuron::TextureFile::Read(R8Atlas().Build(), texture, error));
    const std::vector<std::byte> bytes = Bgra2x2TwoMips().Build();
    Assert::IsFalse(Neuron::TextureFile::Read(std::span<const std::byte>(bytes.data(), 100), texture, error));
    Assert::IsTrue(error.find("ends inside the 124-byte header") != std::string::npos);
    const std::vector<std::byte> dx10 = Dx10Rgba().Build();
    Assert::IsFalse(Neuron::TextureFile::Read(std::span<const std::byte>(dx10.data(), 4 + 124 + 8), texture, error));
    Assert::IsTrue(error.find("ends inside the 20-byte DX10 header") != std::string::npos);
    Assert::IsTrue(texture.Format() == Neuron::TextureFormat::R8Unorm, L"a refused read leaves the output as it was");
    Assert::AreEqual(4u, texture.Width());
  }
};

} // namespace CoreTests
