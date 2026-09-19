#pragma once

#include "UiLayout.h"

#include "TextureFile.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The Species Spectrum font, as the interface draws it (Design/Interface.md §3; SpeciesCanvas.md
// §4). The atlas is 16 columns by 14 rows of 16x16 cells whose first cell is ASCII 32, which is
// Species's own layout and is why GameData\Textures\SpectrumFont.dds is the shape it is.
//
// TEXT IS MONOSPACED AT THE ATLAS'S OWN CELL SIZE, AND THAT IS A RULING AGAINST SPECIES.
// SpeciesCanvas.md §4 draws a glyph at 0.6 of its height and measures a line as
// `characters x size x 0.6`, because Species scaled its whole interface to the window. This game
// does not scale (ADR-004), and 0.6 of 16 is 9.6 - a glyph on a fraction of a pixel, resampled,
// which is the single thing the authored resolution and pillar 3 exist to prevent. Interface.md §3
// therefore rules 16 pixels a glyph box with a 16-pixel advance, states that a line of n characters
// is exactly 16n pixels wide, and says every layout in that document is built on that arithmetic;
// §12 records it as overturnable. m1-vertical-slice/K3's acceptance still quoted Species's 0.6 when
// it was written, and the design is what is implemented here.
//
// MEASURING NEEDS NO ATLAS. A monospaced line's width is its length times the advance, so the
// measurement functions are free and constexpr and a layout can be tested without a texture, a
// device or a file. Only drawing needs the pixels.

namespace Neuron
{

/// Species's atlas layout (SpeciesCanvas.md §4), which the shipped DDS must match.
inline constexpr std::uint32_t FONT_ATLAS_COLUMNS = 16;
inline constexpr std::uint32_t FONT_ATLAS_ROWS = 14;
inline constexpr std::int32_t GLYPH_SIZE_PIXELS = 16;
inline constexpr std::uint32_t FIRST_GLYPH_CODE = 32; ///< The atlas's first cell is the space
inline constexpr std::uint32_t GLYPH_COUNT = FONT_ATLAS_COLUMNS * FONT_ATLAS_ROWS;

/// The measurements of Interface.md §3.
inline constexpr std::int32_t GLYPH_ADVANCE_PIXELS = 16;
inline constexpr std::int32_t LINE_HEIGHT_PIXELS = 20;

/// One glyph to draw: where it lands on the frame, and which cell of the atlas it takes. The cell
/// is given in texels rather than as normalized coordinates, because the pass knows the atlas's
/// size and a normalized coordinate computed twice is a normalized coordinate that can disagree.
struct GlyphQuad
{
  UiRect screen;
  std::int32_t atlasX = 0;
  std::int32_t atlasY = 0;

  [[nodiscard]] constexpr bool operator==(const GlyphQuad&) const noexcept = default;
};

/// How wide one line of _text is. Newlines are not handled here - this is one line - so a string
/// carrying one measures as though the newline were a character, which MeasuredSize below is for.
[[nodiscard]] constexpr std::int32_t LineWidthPixels(std::string_view _text) noexcept
{
  return static_cast<std::int32_t>(_text.size()) * GLYPH_ADVANCE_PIXELS;
}

/// The box _text occupies, counting newlines: the widest line by the number of lines.
[[nodiscard]] UiRect MeasuredSize(std::string_view _text) noexcept;

/// Lays _text out at _x, _y and appends a quad per drawable glyph to _outQuads.
///
/// WHAT IS NOT DRAWN AND WHAT STILL ADVANCES. A newline starts a line and advances nothing across.
/// A character the atlas has no cell for - anything under 32, and anything at or past 32 plus the
/// atlas's 224 cells - is drawn as nothing but STILL ADVANCES, so that a string with one stray byte
/// in it stays in the columns the layout put it in rather than sliding every character after it one
/// place left. A space is a real cell and is drawn like any other; the atlas's first cell is blank.
void LayoutText(std::string_view _text, std::int32_t _x, std::int32_t _y, std::vector<GlyphQuad>& _outQuads);

/// The atlas itself: the DDS decoded to RGBA8, ready for the pass to upload.
class BitmapFont
{
public:
  /// Reads the DDS through Core's TextureFile and checks that it is the atlas this font expects.
  /// False with a reason in _error, rather than a font that draws the wrong glyph for every letter.
  [[nodiscard]] bool Load(std::span<const std::byte> _ddsBytes, std::string& _error);

  [[nodiscard]] bool Loaded() const noexcept
  {
    return !m_pixels.empty();
  }

  [[nodiscard]] std::uint32_t AtlasWidth() const noexcept
  {
    return m_width;
  }

  [[nodiscard]] std::uint32_t AtlasHeight() const noexcept
  {
    return m_height;
  }

  /// RGBA8, row major, AtlasWidth() * AtlasHeight() * 4 bytes.
  [[nodiscard]] std::span<const std::uint8_t> Pixels() const noexcept
  {
    return m_pixels;
  }

private:
  std::vector<std::uint8_t> m_pixels;
  std::uint32_t m_width = 0;
  std::uint32_t m_height = 0;
};

} // namespace Neuron
