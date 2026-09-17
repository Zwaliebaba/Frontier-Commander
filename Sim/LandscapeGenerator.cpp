#include "pch.h"

#include "LandscapeGenerator.h"
#include "FractalTable.h"

#include "Random.h"

#include <algorithm>
#include <cstdlib>

namespace Frontier
{

namespace
{

[[nodiscard]] int LowlandIndex(std::int32_t _exponentHundredths) noexcept
{
  for (std::size_t index = 0; index < LOWLAND_EXPONENTS.size(); ++index)
  {
    if (LOWLAND_EXPONENTS[index] == _exponentHundredths)
    {
      return static_cast<int>(index);
    }
  }
  return -1;
}

} // namespace

bool LandscapeGenerator::IsValid(const LandscapeTile& _tile) noexcept
{
  const bool powerOfTwo = _tile.extent >= 2 && (_tile.extent & (_tile.extent - 1)) == 0;
  return powerOfTwo && _tile.fractalDimensionHundredths >= FRACTAL_DIMENSION_MIN &&
         _tile.fractalDimensionHundredths <= FRACTAL_DIMENSION_MAX && LowlandIndex(_tile.lowlandExponentHundredths) >= 0 &&
         _tile.method <= 2 && _tile.amplitude >= 0;
}

void LandscapeGenerator::GenerateTile(const LandscapeTile& _tile, std::uint64_t _tileSeed, std::vector<std::int32_t>& _grid)
{
  const std::uint32_t extent = _tile.extent;
  const std::uint32_t n = extent + 1;
  const std::int32_t outside = OUTSIDE_HEIGHT * HEIGHT_FIXED_ONE;
  _grid.assign(static_cast<std::size_t>(n) * n, outside);
  Neuron::Random random(_tileSeed);
  const std::int64_t falloff = FALLOFF_16_16[static_cast<std::size_t>(_tile.fractalDimensionHundredths - FRACTAL_DIMENSION_MIN)];
  const std::array<std::int32_t, LOWLAND_HEIGHT_ENTRIES>& power =
    LOWLAND_POWER_16_16[static_cast<std::size_t>(LowlandIndex(_tile.lowlandExponentHundredths))];
  std::int32_t amplitude = _tile.amplitude * HEIGHT_FIXED_ONE;
  const std::uint8_t method = _tile.method;

  // noise(base): the Species 0.1 + 0.15 * |base|^s in 16.16, times a draw in [-amplitude, amplitude].
  const auto noise = [&](std::int32_t _base) -> std::int32_t
  {
    std::int32_t whole = std::abs(_base) >> 8;
    if (whole > LOWLAND_HEIGHT_ENTRIES - 1)
    {
      whole = LOWLAND_HEIGHT_ENTRIES - 1;
    }
    const std::int64_t lowland =
      LOWLAND_MIN_16 + ((static_cast<std::int64_t>(LOWLAND_GAIN_16) * power[static_cast<std::size_t>(whole)]) >> 16);
    const std::int64_t draw = static_cast<std::int64_t>(random.Below(static_cast<std::uint32_t>(2 * amplitude + 1))) - amplitude;
    return static_cast<std::int32_t>((draw * lowland) >> 16);
  };
  // The base of a midpoint from two pairs of samples: the draw for the base comes before the draw for the noise.
  const auto baseOf = [&](std::int32_t _a, std::int32_t _b, std::int32_t _c, std::int32_t _d) -> std::int32_t
  {
    if (method == 0)
    {
      return (_a + _b + _c + _d) >> 2;
    }
    if (method == 1)
    {
      return (random.Next() & 1) == 0 ? (_a + _b) >> 1 : (_c + _d) >> 1;
    }
    const std::array<std::int32_t, 4> samples = {_a, _b, _c, _d};
    return samples[random.Below(4)];
  };

  for (std::uint32_t step = extent; step >= 2; step >>= 1)
  {
    const std::uint32_t half = step >> 1;
    for (std::uint32_t y = 0; y < extent; y += step)
    {
      const std::size_t row = static_cast<std::size_t>(y) * n;
      const std::size_t rowMid = static_cast<std::size_t>(y + half) * n;
      const std::size_t rowEnd = static_cast<std::size_t>(y + step) * n;
      for (std::uint32_t x = 0; x < extent; x += step)
      {
        // The square midpoint from the corners: first pair one diagonal, second pair the other.
        std::int32_t base = baseOf(_grid[row + x], _grid[rowEnd + x + step], _grid[row + x + step], _grid[rowEnd + x]);
        _grid[rowMid + x + half] = base + noise(base);
        if (y > 0)
        {
          // The top edge midpoint: the corners left and right, then the centres above and below.
          base = baseOf(_grid[row + x], _grid[row + x + step], _grid[static_cast<std::size_t>(y - half) * n + x + half],
                        _grid[rowMid + x + half]);
          _grid[row + x + half] = base + noise(base);
        }
        if (x > 0)
        {
          // The left edge midpoint: the centres left and right, then the corners above and below.
          base = baseOf(_grid[rowMid + x - half], _grid[rowMid + x + half], _grid[row + x], _grid[rowEnd + x]);
          _grid[rowMid + x] = base + noise(base);
        }
      }
    }
    amplitude = static_cast<std::int32_t>((static_cast<std::int64_t>(amplitude) * falloff) >> 16);
  }
  if (_tile.heightShift != 0)
  {
    const std::int32_t shift = _tile.heightShift * HEIGHT_FIXED_ONE;
    for (std::int32_t& height : _grid)
    {
      height += shift;
    }
  }
  if (_tile.edgeFalloff > 0)
  {
    const std::int64_t margin = _tile.edgeFalloff;
    for (std::uint32_t j = 0; j < n; ++j)
    {
      const std::uint32_t dj = std::min(j, extent - j);
      const std::size_t row = static_cast<std::size_t>(j) * n;
      for (std::uint32_t i = 0; i < n; ++i)
      {
        const std::uint32_t di = std::min(i, extent - i);
        const std::int64_t d = std::min(di, dj);
        if (d < margin)
        {
          const std::int64_t raw = std::max(_grid[row + i], outside);
          _grid[row + i] = static_cast<std::int32_t>(outside + ((raw - outside) * d) / margin);
        }
      }
    }
  }
}

void LandscapeGenerator::MergeTile(std::vector<std::int32_t>& _land, std::uint32_t _samplesPerSide, const LandscapeTile& _tile,
                                   const std::vector<std::int32_t>& _grid)
{
  const std::int64_t n = static_cast<std::int64_t>(_tile.extent) + 1;
  const std::int64_t outside = static_cast<std::int64_t>(OUTSIDE_HEIGHT) * HEIGHT_FIXED_ONE;
  const std::int64_t tileMax = *std::max_element(_grid.begin(), _grid.end());
  const std::int64_t desired = static_cast<std::int64_t>(_tile.desiredHeight) * HEIGHT_FIXED_ONE;
  const std::int64_t span = tileMax - outside;
  const std::int64_t scale = desired - outside;
  const std::int64_t side = _samplesPerSide;
  const std::int64_t x0 = std::max<std::int64_t>(0, -_tile.x);
  const std::int64_t x1 = std::min<std::int64_t>(n, side - _tile.x);
  const std::int64_t y0 = std::max<std::int64_t>(0, -_tile.y);
  const std::int64_t y1 = std::min<std::int64_t>(n, side - _tile.y);
  if (x0 >= x1 || y0 >= y1)
  {
    return;
  }
  for (std::int64_t j = y0; j < y1; ++j)
  {
    const std::size_t source = static_cast<std::size_t>(j * n);
    const std::size_t destination = static_cast<std::size_t>((_tile.y + j) * side + _tile.x);
    for (std::int64_t i = x0; i < x1; ++i)
    {
      const std::int64_t raw = std::max<std::int64_t>(_grid[source + static_cast<std::size_t>(i)], outside);
      const std::int64_t h = span > 0 ? outside + ((raw - outside) * scale) / span : raw;
      std::int32_t& sample = _land[destination + static_cast<std::size_t>(i)];
      if (h > sample)
      {
        sample = static_cast<std::int32_t>(h);
      }
    }
  }
}

bool LandscapeGenerator::Generate(const LandscapeDefinition& _definition, std::vector<std::int16_t>& _heights)
{
  for (const LandscapeTile& tile : _definition.tiles)
  {
    if (!IsValid(tile))
    {
      return false;
    }
  }
  const std::uint32_t side = SamplesPerSide(_definition.cellsPerSide);
  std::vector<std::int32_t> land(static_cast<std::size_t>(side) * side, OUTSIDE_HEIGHT * HEIGHT_FIXED_ONE);
  std::vector<std::int32_t> grid;
  for (std::size_t index = 0; index < _definition.tiles.size(); ++index)
  {
    GenerateTile(_definition.tiles[index], Neuron::DeriveSeed(_definition.seed, index), grid);
    MergeTile(land, side, _definition.tiles[index], grid);
  }
  std::vector<std::int16_t> heights(land.size());
  for (std::size_t index = 0; index < land.size(); ++index)
  {
    const std::int32_t whole = land[index] >> 8; // arithmetic: the floor, as the tool's >>
    if (whole < -32768 || whole > 32767)
    {
      return false;
    }
    heights[index] = static_cast<std::int16_t>(whole);
  }
  _heights = std::move(heights);
  return true;
}

} // namespace Frontier
