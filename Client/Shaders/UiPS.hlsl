// The UI pass's pixel half (Design/Interface.md §3): a solid colour, a glyph, or an icon, chosen by
// the quad's kind. The three values are Client/UiDraw.h's UiQuadKind and the two files say so in
// each other's comments; there is no fourth.
//
// BOTH ATLASES ARE MASKS, AND EACH IN THE CHANNEL IT ACTUALLY CARRIES. The font is a two-colour
// cutout - white glyph, transparent field, keyed on black by the importer (ADR-010) - so its ALPHA
// is its coverage. An icon is "a single-channel mask tinted at draw time, never a coloured bitmap"
// (§3), and a single-channel texture has only RED. Neither is sampled for colour: the colour is
// always the vertex's, which is what keeps every pixel of the interface a palette entry from
// GameData\Interface.json rather than something baked into a texture.
//
// POINT SAMPLED, NEVER FILTERED. A glyph is one texel to one pixel and an icon is 32 to 32; a
// bilinear tap would blend in the neighbouring cell's edge, which is the resampling ADR-004's
// authored resolution exists to prevent.

Texture2D<float4> g_font : register(t0);
Texture2D<float4> g_icons : register(t1);
SamplerState g_pointSampler : register(s0);

// UiQuadKind's two textured values. Solid is 0 and is the fall-through below rather than a third
// name, because it is also the safe answer for a value BuildUiVertices could not have written: it
// is the one case that reads no texture.
static const uint KIND_GLYPH = 1;
static const uint KIND_ICON = 2;

float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0, float4 color : COLOR0,
            nointerpolation uint kind : TEXKIND0) : SV_Target
{
  if (kind == KIND_GLYPH)
  {
    return float4(color.rgb, color.a * g_font.Sample(g_pointSampler, uv).a);
  }
  if (kind == KIND_ICON)
  {
    return float4(color.rgb, color.a * g_icons.Sample(g_pointSampler, uv).r);
  }
  return color;
}
