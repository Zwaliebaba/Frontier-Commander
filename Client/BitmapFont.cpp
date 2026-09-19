#include "pch.h"

#include "BitmapFont.h"

#include <algorithm>

namespace Neuron
{

namespace
{

/// The atlas cell a byte takes, or GLYPH_COUNT for one the atlas has no cell for.
[[nodiscard]] std::uint32_t CellOf(char _character) noexcept
{
  const std::uint32_t code = static_cast<unsigned char>(_character);
  if (code < FIRST_GLYPH_CODE || code >= FIRST_GLYPH_CODE + GLYPH_COUNT)
  {
    return GLYPH_COUNT;
  }
  return code - FIRST_GLYPH_CODE;
}

} // namespace

UiRect MeasuredSize(std::string_view _text) noexcept
{
  std::int32_t widest = 0;
  std::int32_t onThisLine = 0;
  std::int32_t lines = 1;
  for (const char character : _text)
  {
    if (character == '\n')
    {
      widest = std::max(widest, onThisLine);
      onThisLine = 0;
      ++lines;
      continue;
    }
    onThisLine += GLYPH_ADVANCE_PIXELS;
  }
  widest = std::max(widest, onThisLine);
  return UiRect{0, 0, widest, lines * LINE_HEIGHT_PIXELS};
}

void LayoutText(std::string_view _text, std::int32_t _x, std::int32_t _y, std::vector<GlyphQuad>& _outQuads)
{
  std::int32_t penX = _x;
  std::int32_t penY = _y;
  for (const char character : _text)
  {
    if (character == '\n')
    {
      penX = _x;
      penY += LINE_HEIGHT_PIXELS;
      continue;
    }
    const std::uint32_t cell = CellOf(character);
    if (cell < GLYPH_COUNT)
    {
      GlyphQuad quad{};
      quad.screen = UiRect{penX, penY, GLYPH_SIZE_PIXELS, GLYPH_SIZE_PIXELS};
      quad.atlasX = static_cast<std::int32_t>(cell % FONT_ATLAS_COLUMNS) * GLYPH_SIZE_PIXELS;
      quad.atlasY = static_cast<std::int32_t>(cell / FONT_ATLAS_COLUMNS) * GLYPH_SIZE_PIXELS;
      _outQuads.push_back(quad);
    }
    // Advanced whether or not a quad was made: see the header. A byte the atlas has no cell for
    // costs its column and nothing else.
    penX += GLYPH_ADVANCE_PIXELS;
  }
}

bool BitmapFont::Load(std::span<const std::byte> _ddsBytes, std::string& _error)
{
  TextureFile file;
  if (!TextureFile::Read(_ddsBytes, file, _error))
  {
    return false;
  }
  const std::uint32_t expectedWidth = FONT_ATLAS_COLUMNS * static_cast<std::uint32_t>(GLYPH_SIZE_PIXELS);
  const std::uint32_t expectedHeight = FONT_ATLAS_ROWS * static_cast<std::uint32_t>(GLYPH_SIZE_PIXELS);
  // CHECKED RATHER THAN ASSUMED. Every cell's position is worked out from the atlas's geometry, so
  // an atlas of another size does not fail to load - it loads and draws the wrong glyph for every
  // letter in the game, which is a fault somebody has to notice by reading the screen.
  if (file.Width() != expectedWidth || file.Height() != expectedHeight)
  {
    _error = "the font atlas is " + std::to_string(file.Width()) + "x" + std::to_string(file.Height()) + ", not " +
             std::to_string(expectedWidth) + "x" + std::to_string(expectedHeight) + " (SpeciesCanvas.md 4)";
    return false;
  }
  std::vector<std::uint8_t> pixels;
  if (!file.DecodeRgba8(0, pixels))
  {
    _error = "the font atlas could not be decoded to RGBA8";
    return false;
  }
  m_pixels = std::move(pixels);
  m_width = file.Width();
  m_height = file.Height();
  return true;
}

} // namespace Neuron
