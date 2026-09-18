#include "pch.h"

#include "TerrainChunk.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// The chunk builder is pure over a HeightView, so what it owes is pinned here: the vertex and
// index counts of each stride, the skirt under every border, the underwater plain skipped, and a
// flat field whose silhouette every stride agrees on.
namespace ClientTests
{

namespace
{

constexpr std::uint32_t FIELD_SIDE = Neuron::CHUNK_SAMPLES; // one chunk

struct Field
{
  std::vector<std::int16_t> samples;
  Neuron::HeightView view;
};

Field Flat(std::int16_t _height)
{
  Field field;
  field.samples.assign(static_cast<std::size_t>(FIELD_SIDE) * FIELD_SIDE, _height);
  field.view = {field.samples.data(), FIELD_SIDE, 16, 0, std::max<std::int32_t>(_height, 1)};
  return field;
}

/// A 64 by 64 B8G8R8A8 DDS with the DX10 extension, laid out exactly as Tools/MakeTerrainPalette.py
/// writes one, so that this test exercises the file the game will actually read.
[[nodiscard]] std::vector<std::byte> PaletteDds(std::uint8_t _r, std::uint8_t _g, std::uint8_t _b, std::uint32_t _height = 0)
{
  const auto put = [](std::vector<std::byte>& _bytes, std::uint32_t _value)
  {
    for (int shift = 0; shift < 32; shift += 8)
    {
      _bytes.push_back(static_cast<std::byte>((_value >> shift) & 0xFFu));
    }
  };
  const std::uint32_t side = Neuron::TerrainPalette::SIDE;
  const std::uint32_t height = _height == 0 ? side : _height;
  std::vector<std::byte> bytes;
  put(bytes, 0x20534444u);                         // "DDS "
  put(bytes, 124);                                 // header size
  put(bytes, 0x1u | 0x2u | 0x4u | 0x1000u | 0x8u); // caps, height, width, pixel format, pitch
  put(bytes, height);                              // rows
  put(bytes, side);                                // columns
  put(bytes, side * 4);                            // pitch
  put(bytes, 0);                                   // depth
  put(bytes, 1);                                   // mip count
  for (int reserved = 0; reserved < 11; ++reserved)
  {
    put(bytes, 0);
  }
  put(bytes, 32);                            // pixel format size
  put(bytes, 0x4u);                          // DDPF_FOURCC
  put(bytes, 0x30315844u);                   // "DX10"
  for (int unused = 0; unused < 5; ++unused) // bit count and the four masks
  {
    put(bytes, 0);
  }
  put(bytes, 0x1000u); // DDSCAPS_TEXTURE
  for (int caps = 0; caps < 4; ++caps)
  {
    put(bytes, 0);
  }
  put(bytes, 87); // DXGI_FORMAT_B8G8R8A8_UNORM
  put(bytes, 3);  // TEXTURE2D
  put(bytes, 0);
  put(bytes, 1); // array size
  put(bytes, 0);
  for (std::uint32_t pixel = 0; pixel < side * height; ++pixel)
  {
    bytes.push_back(static_cast<std::byte>(_b));
    bytes.push_back(static_cast<std::byte>(_g));
    bytes.push_back(static_cast<std::byte>(_r));
    bytes.push_back(static_cast<std::byte>(255));
  }
  return bytes;
}

} // namespace

TEST_CLASS(TerrainChunkTests)
{
public:
  TEST_METHOD(EveryStrideHasTheCountsOfItsGridAndSkirt)
  {
    const Field field = Flat(100);
    const Neuron::TerrainPalette palette = Neuron::TerrainPalette::BuiltIn();
    for (const std::uint32_t stride : Neuron::CHUNK_STRIDES)
    {
      const std::uint32_t steps = Neuron::CHUNK_STEPS / stride;
      const Neuron::TerrainMesh mesh = Neuron::BuildTerrainChunk(field.view, 0, 0, stride, palette);
      const std::size_t gridVertices = static_cast<std::size_t>(steps + 1) * (steps + 1);
      Assert::AreEqual(gridVertices + static_cast<std::size_t>(steps) * 8, mesh.vertices.size(),
                       L"the grid plus two skirt vertices per border edge");
      Assert::AreEqual(static_cast<std::size_t>(steps) * steps * 6 + static_cast<std::size_t>(steps) * 24, mesh.indices.size(),
                       L"two triangles a quad, and two per skirt edge on four sides");
      Assert::IsTrue(mesh.vertices.size() <= 65536, L"sixteen-bit indices");
      for (const std::uint16_t index : mesh.indices)
      {
        Assert::IsTrue(index < mesh.vertices.size());
      }
    }
  }

  TEST_METHOD(TheSkirtHangsUnderEveryBorder)
  {
    const Field field = Flat(100);
    const Neuron::TerrainMesh mesh = Neuron::BuildTerrainChunk(field.view, 0, 0, 8, Neuron::TerrainPalette::BuiltIn());
    const float skirtY = static_cast<float>(field.view.waterLevel) - Neuron::SKIRT_DEPTH;
    std::size_t skirtVertices = 0;
    for (const Neuron::TerrainVertex& vertex : mesh.vertices)
    {
      if (vertex.y == skirtY)
      {
        ++skirtVertices;
        const bool onBorder = vertex.x == 0.0f || vertex.z == 0.0f || vertex.x == 128.0f * 16.0f || vertex.z == 128.0f * 16.0f;
        Assert::IsTrue(onBorder, L"a skirt vertex sits under a border vertex");
      }
    }
    Assert::AreEqual(static_cast<std::size_t>(16) * 8, skirtVertices);
    Assert::AreEqual(skirtY, mesh.minY);
  }

  TEST_METHOD(AFlatFieldHasTheSameSilhouetteAtEveryStride)
  {
    const Field field = Flat(100);
    const Neuron::TerrainPalette palette = Neuron::TerrainPalette::BuiltIn();
    const Neuron::TerrainMesh finest = Neuron::BuildTerrainChunk(field.view, 0, 0, 1, palette);
    for (const std::uint32_t stride : Neuron::CHUNK_STRIDES)
    {
      const Neuron::TerrainMesh mesh = Neuron::BuildTerrainChunk(field.view, 0, 0, stride, palette);
      Assert::AreEqual(finest.minX, mesh.minX);
      Assert::AreEqual(finest.maxX, mesh.maxX);
      Assert::AreEqual(finest.minZ, mesh.minZ);
      Assert::AreEqual(finest.maxZ, mesh.maxZ);
      Assert::AreEqual(finest.maxY, mesh.maxY);
      Assert::AreEqual(100.0f, mesh.maxY);
      Assert::IsFalse(mesh.hasWater);
      for (const Neuron::TerrainVertex& vertex : mesh.vertices)
      {
        Assert::IsTrue(vertex.y == 100.0f || vertex.y == mesh.minY, L"a grid vertex at the field's height or a skirt vertex");
      }
    }
  }

  TEST_METHOD(TheUnderwaterPlainIsSkippedAndTheShoreDips)
  {
    Field field = Flat(-26);
    // A single island sample well above the water in the middle of the plain.
    field.samples[static_cast<std::size_t>(64) * FIELD_SIDE + 64] = 50;
    field.view.highest = 50;
    const Neuron::TerrainMesh mesh = Neuron::BuildTerrainChunk(field.view, 0, 0, 1, Neuron::TerrainPalette::BuiltIn());
    Assert::IsTrue(mesh.hasWater);
    // Four quads touch the island sample; everything else is the plain and is not drawn.
    Assert::AreEqual(static_cast<std::size_t>(4) * 6 + static_cast<std::size_t>(128) * 24, mesh.indices.size());
    std::size_t dipped = 0;
    for (const Neuron::TerrainVertex& vertex : mesh.vertices)
    {
      if (vertex.y == -Neuron::SHORE_DIP)
      {
        ++dipped;
      }
    }
    Assert::IsTrue(dipped > 0, L"the plain's vertices are pushed under the water plane");
  }

  TEST_METHOD(ThePaletteReadsSummitAtTheBottomAndCliffsToTheRight)
  {
    const Neuron::TerrainPalette palette = Neuron::TerrainPalette::BuiltIn();
    const std::uint32_t summit = palette.Lookup(0.0f, 0.0f);
    const std::uint32_t shore = palette.Lookup(0.0f, 1.0f);
    const std::uint32_t cliff = palette.Lookup(1.0f, 0.5f);
    Assert::IsTrue((summit & 0xFF) > 200 && ((summit >> 8) & 0xFF) > 200, L"white peaks");
    Assert::IsTrue(((shore >> 16) & 0xFF) > (shore & 0xFF), L"blue lowlands");
    Assert::IsTrue((cliff & 0xFF) > ((cliff >> 16) & 0xFF), L"brown cliffs");
    Assert::AreEqual(palette.Lookup(-5.0f, 9.0f), palette.Lookup(0.0f, 1.0f), L"clamped");
  }

  TEST_METHOD(APaletteFromATextureKeepsItsChannelsInOrder)
  {
    // The one thing that silently ruins a landscape: a palette read blue for red. The file stores
    // B, G, R, A and the vertex colour packs R in the low byte, so this pins the swizzle across
    // both conversions with a colour whose channels are all different.
    const std::vector<std::byte> bytes = PaletteDds(10, 120, 240);
    Neuron::TextureFile texture;
    std::string error;
    Assert::IsTrue(Neuron::TextureFile::Read(bytes, texture, error), L"the fixture is a DDS the reader accepts");
    Neuron::TerrainPalette palette;
    Assert::IsTrue(Neuron::TerrainPalette::FromTexture(texture, palette));
    const std::uint32_t color = palette.Lookup(0.5f, 0.5f);
    Assert::AreEqual(std::uint32_t{10}, color & 0xFFu, L"red is the low byte");
    Assert::AreEqual(std::uint32_t{120}, (color >> 8) & 0xFFu);
    Assert::AreEqual(std::uint32_t{240}, (color >> 16) & 0xFFu);
    Assert::AreEqual(std::uint32_t{255}, (color >> 24) & 0xFFu);
  }

  TEST_METHOD(APaletteOfTheWrongSizeIsRefusedAndLeavesTheFallbackAlone)
  {
    // A whole, valid 64 by 32 texture: the reader accepts it and the palette refuses it for its
    // shape rather than for its bytes.
    const std::vector<std::byte> bytes = PaletteDds(1, 2, 3, 32);
    Neuron::TextureFile texture;
    std::string error;
    Assert::IsTrue(Neuron::TextureFile::Read(bytes, texture, error), L"a 64 by 32 texture is a valid file");
    Neuron::TerrainPalette palette = Neuron::TerrainPalette::BuiltIn();
    const std::uint32_t before = palette.Lookup(0.0f, 0.0f);
    Assert::IsFalse(Neuron::TerrainPalette::FromTexture(texture, palette), L"but not a palette");
    Assert::AreEqual(before, palette.Lookup(0.0f, 0.0f), L"and the caller's palette is untouched");
  }
};

} // namespace ClientTests
