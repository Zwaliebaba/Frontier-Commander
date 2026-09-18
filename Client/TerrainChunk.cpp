#include "pch.h"

#include "TerrainChunk.h"

#include "Lighting.h"

#include <algorithm>
#include <cmath>

namespace Neuron
{

namespace
{

[[nodiscard]] std::uint32_t PackColor(float _r, float _g, float _b, float _a) noexcept
{
  const auto channel = [](float _value) { return static_cast<std::uint32_t>(std::lround(std::clamp(_value, 0.0f, 1.0f) * 255.0f)); };
  return channel(_r) | (channel(_g) << 8) | (channel(_b) << 16) | (channel(_a) << 24);
}

/// The height of a sample, or of the plain outside the field for a sample past its edge.
[[nodiscard]] float SampleHeight(const HeightView& _view, std::int64_t _x, std::int64_t _y) noexcept
{
  const std::int64_t side = _view.samplesPerSide;
  if (_x < 0 || _y < 0 || _x >= side || _y >= side)
  {
    return static_cast<float>(_view.waterLevel) - 26.0f;
  }
  return static_cast<float>(_view.samples[static_cast<std::size_t>(_y * side + _x)]);
}

/// A hash of the sample position to a value in [-1, 1), stable frame to frame, so that the
/// mottling of the low ground stays put.
[[nodiscard]] float SignedNoise(std::uint32_t _x, std::uint32_t _y) noexcept
{
  std::uint32_t hash = _x * 0x9E3779B1u ^ (_y + 0x7F4A7C15u) * 0x85EBCA77u;
  hash ^= hash >> 15;
  hash *= 0x2C1B3C6Du;
  hash ^= hash >> 12;
  return static_cast<float>(hash & 0xFFFFu) / 32768.0f - 1.0f;
}

/// The Species colour of a vertex (SpeciesTerrain.md §6): the slope from the central differences
/// of the samples around it, the height against the highest, and the noise that fades with altitude.
[[nodiscard]] std::uint32_t VertexColor(const HeightView& _view, std::uint32_t _x, std::uint32_t _y, std::uint32_t _stride,
                                        const TerrainPalette& _palette) noexcept
{
  const float spacing = static_cast<float>(_view.spacingWorldUnits) * static_cast<float>(_stride);
  const float height = SampleHeight(_view, _x, _y);
  const float slopeX =
    (SampleHeight(_view, static_cast<std::int64_t>(_x) + _stride, _y) - SampleHeight(_view, static_cast<std::int64_t>(_x) - _stride, _y)) /
    (2.0f * spacing);
  const float slopeY =
    (SampleHeight(_view, _x, static_cast<std::int64_t>(_y) + _stride) - SampleHeight(_view, _x, static_cast<std::int64_t>(_y) - _stride)) /
    (2.0f * spacing);
  const float normalY = 1.0f / std::sqrt(1.0f + slopeX * slopeX + slopeY * slopeY);
  const float u = std::pow(1.0f - normalY, 0.4f);
  const float above = std::max(height - static_cast<float>(_view.waterLevel), 0.0f);
  const float highest = std::max(static_cast<float>(_view.highest - _view.waterLevel), 1.0f);
  const float v = 1.0f - above / highest + SignedNoise(_x, _y) * (0.45f / (above + 2.0f));
  const std::uint32_t color = _palette.Lookup(u, v);
  return (color & 0x00FFFFFFu) | (PackColor(0.0f, 0.0f, 0.0f, VERTEX_ALPHA_LIT) & 0xFF000000u);
}

} // namespace

TerrainPalette TerrainPalette::BuiltIn()
{
  // Bottom row the summit, top row the sea level; left column flat ground, right column a cliff
  // (SpeciesTerrain.md §6): white peaks, green slopes, blue lowlands, and cliffs a dry brown.
  TerrainPalette palette;
  for (std::uint32_t row = 0; row < SIDE; ++row)
  {
    const float v = static_cast<float>(row) / static_cast<float>(SIDE - 1);
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    if (v < 0.5f)
    {
      const float t = v * 2.0f;
      r = 0.92f + (0.30f - 0.92f) * t;
      g = 0.92f + (0.62f - 0.92f) * t;
      b = 0.90f + (0.26f - 0.90f) * t;
    }
    else
    {
      const float t = (v - 0.5f) * 2.0f;
      r = 0.30f + (0.20f - 0.30f) * t;
      g = 0.62f + (0.48f - 0.62f) * t;
      b = 0.26f + (0.55f - 0.26f) * t;
    }
    for (std::uint32_t column = 0; column < SIDE; ++column)
    {
      const float u = static_cast<float>(column) / static_cast<float>(SIDE - 1);
      const float cliff = u * u;
      palette.colors[row * SIDE + column] = PackColor(r + (0.46f - r) * cliff, g + (0.36f - g) * cliff, b + (0.26f - b) * cliff, 1.0f);
    }
  }
  return palette;
}

bool TerrainPalette::FromTexture(const TextureFile& _texture, TerrainPalette& _out)
{
  if (_texture.Width() != SIDE || _texture.Height() != SIDE)
  {
    return false;
  }
  std::vector<std::uint8_t> rgba;
  if (!_texture.DecodeRgba8(0, rgba) || rgba.size() != static_cast<std::size_t>(SIDE) * SIDE * 4)
  {
    return false;
  }
  TerrainPalette palette;
  for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(SIDE) * SIDE; ++pixel)
  {
    const std::size_t byte = pixel * 4;
    palette.colors[pixel] = static_cast<std::uint32_t>(rgba[byte]) | (static_cast<std::uint32_t>(rgba[byte + 1]) << 8) |
                            (static_cast<std::uint32_t>(rgba[byte + 2]) << 16) | (static_cast<std::uint32_t>(rgba[byte + 3]) << 24);
  }
  _out = palette;
  return true;
}

std::uint32_t TerrainPalette::Lookup(float _u, float _v) const noexcept
{
  const auto index = [](float _value)
  { return static_cast<std::uint32_t>(std::clamp(_value * static_cast<float>(SIDE), 0.0f, static_cast<float>(SIDE - 1))); };
  return colors[index(_v) * SIDE + index(_u)];
}

std::uint32_t ChunksPerSide(const HeightView& _view) noexcept
{
  if (_view.samplesPerSide < 2)
  {
    return 0;
  }
  return (_view.samplesPerSide - 2) / CHUNK_STEPS + 1;
}

TerrainMesh BuildTerrainChunk(const HeightView& _view, std::uint32_t _chunkX, std::uint32_t _chunkY, std::uint32_t _stride,
                              const TerrainPalette& _palette)
{
  TerrainMesh mesh{};
  const std::uint32_t stride = std::clamp<std::uint32_t>(_stride, 1, 8);
  const std::uint32_t steps = CHUNK_STEPS / stride;
  const std::uint32_t side = steps + 1;
  const std::uint32_t originX = _chunkX * CHUNK_STEPS;
  const std::uint32_t originY = _chunkY * CHUNK_STEPS;
  const float spacing = static_cast<float>(_view.spacingWorldUnits);
  const float water = static_cast<float>(_view.waterLevel);
  mesh.vertices.reserve(static_cast<std::size_t>(side) * side + static_cast<std::size_t>(side) * 4);
  mesh.indices.reserve(static_cast<std::size_t>(steps) * steps * 6 + static_cast<std::size_t>(steps) * 24);
  mesh.minX = mesh.minY = mesh.minZ = 1.0e30f;
  mesh.maxX = mesh.maxY = mesh.maxZ = -1.0e30f;

  // The grid, row by row: the sample's height, dipped under the water at the shore.
  std::vector<float> heights(static_cast<std::size_t>(side) * side);
  for (std::uint32_t row = 0; row < side; ++row)
  {
    for (std::uint32_t column = 0; column < side; ++column)
    {
      const std::uint32_t sampleX = originX + column * stride;
      const std::uint32_t sampleY = originY + row * stride;
      const float raw = SampleHeight(_view, sampleX, sampleY);
      heights[static_cast<std::size_t>(row) * side + column] = raw;
      if (raw < water)
      {
        mesh.hasWater = true;
      }
      const float height = raw < water + SHORE_LIP ? water - SHORE_DIP : raw;
      TerrainVertex vertex{};
      vertex.x = static_cast<float>(sampleX) * spacing;
      vertex.y = height;
      vertex.z = static_cast<float>(sampleY) * spacing;
      vertex.color = VertexColor(_view, sampleX, sampleY, stride, _palette);
      mesh.vertices.push_back(vertex);
      mesh.minX = std::min(mesh.minX, vertex.x);
      mesh.maxX = std::max(mesh.maxX, vertex.x);
      mesh.minY = std::min(mesh.minY, vertex.y);
      mesh.maxY = std::max(mesh.maxY, vertex.y);
      mesh.minZ = std::min(mesh.minZ, vertex.z);
      mesh.maxZ = std::max(mesh.maxZ, vertex.z);
    }
  }
  const auto at = [side](std::uint32_t _column, std::uint32_t _row) { return static_cast<std::uint16_t>(_row * side + _column); };
  for (std::uint32_t row = 0; row < steps; ++row)
  {
    for (std::uint32_t column = 0; column < steps; ++column)
    {
      const float h00 = heights[static_cast<std::size_t>(row) * side + column];
      const float h10 = heights[static_cast<std::size_t>(row) * side + column + 1];
      const float h01 = heights[static_cast<std::size_t>(row + 1) * side + column];
      const float h11 = heights[static_cast<std::size_t>(row + 1) * side + column + 1];
      // The underwater plain is never drawn (SpeciesTerrain.md §6).
      if (h00 <= water && h10 <= water && h01 <= water && h11 <= water)
      {
        continue;
      }
      // Two triangles, each led by a different corner, so that the flat colour a triangle takes
      // from its first vertex (nointerpolation) comes from a corner of its own.
      mesh.indices.push_back(at(column, row));
      mesh.indices.push_back(at(column + 1, row));
      mesh.indices.push_back(at(column, row + 1));
      mesh.indices.push_back(at(column + 1, row + 1));
      mesh.indices.push_back(at(column, row + 1));
      mesh.indices.push_back(at(column + 1, row));
    }
  }

  // The skirt: a copy of each border vertex hung SKIRT_DEPTH under the water, and a quad from each
  // border edge down to it, so that a neighbouring chunk at another stride leaves no crack.
  const float skirtY = water - SKIRT_DEPTH;
  const auto skirt = [&mesh, skirtY](std::uint16_t _border)
  {
    TerrainVertex vertex = mesh.vertices[_border];
    vertex.y = skirtY;
    mesh.vertices.push_back(vertex);
    return static_cast<std::uint16_t>(mesh.vertices.size() - 1);
  };
  const auto wall = [&mesh, &skirt](std::uint16_t _a, std::uint16_t _b)
  {
    const std::uint16_t skirtA = skirt(_a);
    const std::uint16_t skirtB = skirt(_b);
    mesh.indices.push_back(_a);
    mesh.indices.push_back(_b);
    mesh.indices.push_back(skirtA);
    mesh.indices.push_back(skirtB);
    mesh.indices.push_back(skirtA);
    mesh.indices.push_back(_b);
  };
  for (std::uint32_t index = 0; index < steps; ++index)
  {
    wall(at(index, 0), at(index + 1, 0));
    wall(at(index, steps), at(index + 1, steps));
    wall(at(0, index), at(0, index + 1));
    wall(at(steps, index), at(steps, index + 1));
  }
  mesh.minY = std::min(mesh.minY, skirtY);
  return mesh;
}

} // namespace Neuron
