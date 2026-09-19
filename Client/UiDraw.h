#pragma once

#include "BitmapFont.h"
#include "UiLayout.h"
#include "UiPanel.h"
#include "UiWidgets.h"

#include "InterfaceDesc.h"
#include "TextureFile.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// A panel turned into the quads that draw it (Design/Interface.md §3; m1-vertical-slice/K3). This
// is the whole of the look - the fills, the border, the title gradient, the button states, the bar
// colours and where every glyph lands - and it holds no Direct3D at all. Client/UiPass.h uploads
// what this produces and issues the draw.
//
// THE SPLIT IS WHAT MAKES THE LOOK TESTABLE. UiPass.h includes d3d12.h, so anything declared beside
// the pass can only be exercised where that header exists. The arithmetic here is the same kind
// UiLayout's is - rectangles, insets, thresholds - and it is the kind that is wrong by one pixel
// rather than wrong by a crash, which is exactly what a unit test catches and an eye does not. It
// is the same reason AUTHORED_WIDTH_PIXELS moved out of SceneTarget.h in this task.
//
// A COLOUR IS RGBA8 WITH RED IN THE LOW BYTE, which is Core/RenderView.h's teamColor convention and
// the byte order DXGI_FORMAT_R8G8B8A8_UNORM reads a vertex attribute in. One packing, named once.
//
// THE PALETTE IS THE GAME'S DATA AND THAT IS ALLOWED. Frontier::ChromePalette is Content's
// (GameData\Interface.json, m1-vertical-slice/C6) and Client is built on Content
// (TechnicalDesign.md §2), the same edge Client/ModelBuffers.h already uses for ModelDesc. What R9
// forbids is Client knowing Sim or Replica, and nothing here does.

namespace Neuron
{

/// What a quad's texture coordinates mean, and therefore which atlas its pixel shader reads. The
/// values are the ones Shaders/UiPS.hlsl branches on; the two say so in each other's comments.
enum class UiQuadKind : std::uint32_t
{
  Solid = 0, ///< No texture: the interpolated colour, straight
  Glyph = 1, ///< The font atlas, tinted: colour.rgb with the atlas's alpha as the coverage
  Icon = 2   ///< The icon atlas, tinted: colour.rgb with the atlas's red as the mask (§3)
};

/// One axis-aligned rectangle to draw, in authored pixels, with its atlas source in texels.
///
/// TWO COLOURS BECAUSE §3 ASKS FOR A GRADIENT. A panel's title strip is "the vertical gradient
/// panelTitleFrom -> panelTitleTo", and a vertex colour interpolates for nothing; every other quad
/// sets both to the same value, which costs one uint and no branch.
struct UiQuad
{
  UiRect rect;
  UiRect atlas;                        ///< In texels; ignored when kind is Solid
  std::uint32_t colorTop = 0xFFFFFFFF; ///< RGBA8, red in the low byte
  std::uint32_t colorBottom = 0xFFFFFFFF;
  UiQuadKind kind = UiQuadKind::Solid;

  [[nodiscard]] constexpr bool operator==(const UiQuad&) const noexcept = default;
};

/// A palette colour as a quad carries it.
[[nodiscard]] constexpr std::uint32_t PackedColor(const Frontier::Rgba8& _color) noexcept
{
  return static_cast<std::uint32_t>(_color.red) | (static_cast<std::uint32_t>(_color.green) << 8) |
         (static_cast<std::uint32_t>(_color.blue) << 16) | (static_cast<std::uint32_t>(_color.alpha) << 24);
}

/// The icon sheet: 32x32 authored pixels a cell (Design/Interface.md §3), checked on load for the
/// same reason BitmapFont checks the font's - every cell's position is derived from the sheet's
/// geometry, so a sheet of another shape draws the wrong icon rather than failing.
///
/// IT IS A MASK AND THE SHIPPED SHEET IS NOT ONE YET. §3 rules that "an icon is a single-channel
/// mask tinted at draw time, never a coloured bitmap"; GameData\Textures\Icons.dds today is the
/// eight 64x64 Species order banners C4 imported unkeyed, opaque and coloured (ADR-010, which says
/// they stand "until the icon atlas is rebuilt from this game's own art", and §11 row 7, which
/// assigns the 32x32 grid to C4 and blocks K4). This class implements §3: it loads the sheet at
/// whatever size divides into 32x32 cells, and the pass tints its red channel. Until the sheet is
/// re-authored an icon draws as a tinted square, which is a placeholder that looks like one.
class IconAtlas
{
public:
  [[nodiscard]] bool Load(std::span<const std::byte> _ddsBytes, std::string& _error);

  [[nodiscard]] bool Loaded() const noexcept
  {
    return !m_pixels.empty();
  }

  [[nodiscard]] std::uint32_t Width() const noexcept
  {
    return m_width;
  }

  [[nodiscard]] std::uint32_t Height() const noexcept
  {
    return m_height;
  }

  /// How many cells across and down, and the product, which is the range a cell index may take.
  [[nodiscard]] std::uint32_t Columns() const noexcept
  {
    return m_width / static_cast<std::uint32_t>(ICON_SIZE_PIXELS);
  }

  [[nodiscard]] std::uint32_t Rows() const noexcept
  {
    return m_height / static_cast<std::uint32_t>(ICON_SIZE_PIXELS);
  }

  [[nodiscard]] std::uint32_t CellCount() const noexcept
  {
    return Columns() * Rows();
  }

  /// RGBA8, row major, Width() * Height() * 4 bytes.
  [[nodiscard]] std::span<const std::uint8_t> Pixels() const noexcept
  {
    return m_pixels;
  }

  /// Where cell _index lies in the sheet, or an empty rectangle for a cell the sheet does not have.
  [[nodiscard]] UiRect CellRect(std::int32_t _index) const noexcept;

private:
  std::vector<std::uint8_t> m_pixels;
  std::uint32_t m_width = 0;
  std::uint32_t m_height = 0;
};

/// A filled rectangle. Nothing is appended for an empty one, so a caller may pass a rectangle that
/// the layout arithmetic already refused without checking it twice.
void AppendRect(const UiRect& _rect, std::uint32_t _color, std::vector<UiQuad>& _outQuads);

/// A rectangle filled top to bottom, for §3's title strip.
void AppendGradient(const UiRect& _rect, std::uint32_t _top, std::uint32_t _bottom, std::vector<UiQuad>& _outQuads);

/// A border _thickness pixels wide drawn INSIDE _rect, as four rectangles rather than one outline:
/// a rectangle is the only primitive this pass has, and four of them is what an outline is.
void AppendBorder(const UiRect& _rect, std::int32_t _thickness, std::uint32_t _color, std::vector<UiQuad>& _outQuads);

/// _text at _x, _y in _color, one quad a drawable glyph, through BitmapFont's own layout - so the
/// advance, the newline and the undrawable byte behave exactly as its tests pin them.
void AppendText(std::string_view _text, std::int32_t _x, std::int32_t _y, std::uint32_t _color, std::vector<UiQuad>& _outQuads);

/// One 32x32 cell of the icon sheet at _rect, tinted _color. Nothing is appended for a cell the
/// sheet does not have, so a widget holding a stale index draws nothing rather than a slice of
/// whatever lies at that offset.
void AppendIcon(const IconAtlas& _icons, std::int32_t _cell, const UiRect& _rect, std::uint32_t _color, std::vector<UiQuad>& _outQuads);

/// Every quad of _panel, in back-to-front order, per Design/Interface.md §3. Nothing is appended
/// for a panel that is not visible. The panel's own hover and press state chooses a button's fill,
/// which is why this takes the panel rather than a span of widgets.
void BuildPanelQuads(const UiPanel& _panel, const Frontier::ChromePalette& _palette, const IconAtlas& _icons,
                     std::vector<UiQuad>& _outQuads);

/// One corner of a quad, as Shaders/UiVS.hlsl declares its input. Declared here rather than beside
/// the pass for the reason everything else in this file is: the texel-to-uv divide is the one place
/// a glyph can land half a texel out of its cell, and that is worth a test on any machine.
struct UiVertex
{
  float x;
  float y;
  float u;
  float v;
  std::uint32_t color; ///< RGBA8, red in the low byte
  std::uint32_t kind;  ///< UiQuadKind, as Shaders/UiPS.hlsl branches on it
};
static_assert(sizeof(UiVertex) == 24, "the pass writes its input layout's offsets from this");

/// Two triangles, and therefore six vertices, because there is no index buffer: an index buffer
/// would save a third of a megabyte on a full screen of text and cost a second resource, a second
/// upload and a cap on how many quads one buffer may address.
inline constexpr std::uint32_t VERTICES_PER_QUAD = 6;

/// Six vertices a quad - two triangles, no index buffer - appended to _outVertices: authored pixels
/// as the floats the shader takes, and atlas texels as normalized coordinates against whichever
/// atlas the quad's kind names. A quad whose kind is Solid gets zeroes for its coordinates, which
/// the pixel shader never reads.
void BuildUiVertices(std::span<const UiQuad> _quads, std::uint32_t _fontWidth, std::uint32_t _fontHeight, std::uint32_t _iconWidth,
                     std::uint32_t _iconHeight, std::vector<UiVertex>& _outVertices);

} // namespace Neuron
