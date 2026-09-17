#include "pch.h"

#include "TerrainChunk.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
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
};

} // namespace ClientTests
