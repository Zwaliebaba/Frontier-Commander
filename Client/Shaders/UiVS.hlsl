// The UI pass's vertex half (Design/Interface.md §3; m1-vertical-slice/K3): a quad's corner in
// AUTHORED PIXELS turned into clip space, with no matrix and no camera. The interface is drawn at
// the authored resolution and never scaled (ADR-004), so the projection is a divide by the frame's
// size; a 4x4 orthographic matrix would be the same two numbers behind fourteen zeroes.
//
// THE MAPPING IS EXACT AND THAT IS THE POINT. A quad from x = 100 to x = 110 covers pixels 100 to
// 109 and no others, because Direct3D samples at the pixel centre and the rectangle is given by its
// edges. That is what makes a 16-texel glyph land on 16 pixels, one texel to one pixel, which §3
// requires of every layout in the document.

cbuffer UiConstants : register(b0)
{
  float2 g_authoredSize; // AUTHORED_WIDTH_PIXELS, AUTHORED_HEIGHT_PIXELS (Client/ScaleMode.h)
};

struct Input
{
  float2 position : POSITION;
  float2 uv : TEXCOORD0;
  float4 color : COLOR0;
  uint kind : TEXKIND0;
};

struct Output
{
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
  float4 color : COLOR0;
  // FLAT, because an interpolated integer is not a thing the rasterizer will do: nointerpolation is
  // required on an integer attribute, and a quad's four corners carry the same kind anyway.
  nointerpolation uint kind : TEXKIND0;
};

Output main(Input input)
{
  Output output;
  const float2 unit = input.position / g_authoredSize;
  // Y down in authored pixels, up in clip space.
  output.position = float4(unit.x * 2.0 - 1.0, 1.0 - unit.y * 2.0, 0.0, 1.0);
  output.uv = input.uv;
  output.color = input.color;
  output.kind = input.kind;
  return output;
}
