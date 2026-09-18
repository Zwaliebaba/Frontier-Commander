// The fog pass's pixel half (TechnicalDesign.md §6.2; GameDesign.md §3): the shade a pixel keeps,
// written as a colour that the blend state multiplies the scene by - source ZERO, destination
// SRC_COLOR - so the scene target is darkened in place and never read as a texture while it is a
// render target.
//
// Where the pixel is comes from the depth buffer and the inverse view projection: a pixel is
// darkened by the cell of whatever was DRAWN there, ground or device or structure alike. A depth
// of zero is the reversed depth's far value (SceneTarget.h) and means nothing was drawn, so the
// background is left as the clear colour rather than blacked.
//
// Two depth views are declared and one of them is null. A Texture2DMS view of a one-sample
// resource is not legal and neither is the reverse, the scene target falls back to one sample on an
// adapter without multisampling, and a null descriptor is legal; g_grid.z carries the sample count
// so the branch is uniform across the draw.

Texture2D<uint> g_fog : register(t0);
Texture2DMS<float> g_depthMultisampled : register(t1);
Texture2D<float> g_depth : register(t2);

cbuffer FogConstants : register(b0)
{
  float4x4 g_inverseViewProjection;
  float4 g_grid;  // cells per side, world units per cell, the scene's sample count, 0
  float4 g_shade; // unexplored, explored, visible, 0
};

float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
  const int2 pixel = int2(position.xy);
  const float depth = g_grid.z > 1.5 ? g_depthMultisampled.Load(pixel, 0) : g_depth.Load(int3(pixel, 0));
  if (depth <= 0.0)
  {
    discard;
  }
  const float4 clip = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
  const float4 homogeneous = mul(clip, g_inverseViewProjection);
  const float3 surface = homogeneous.xyz / homogeneous.w;
  const float2 cell = floor(surface.xz / g_grid.y);
  if (cell.x < 0.0 || cell.y < 0.0 || cell.x >= g_grid.x || cell.y >= g_grid.x)
  {
    // Off the grid is ground nobody has walked, and it is drawn as such rather than left lit.
    return float4(g_shade.xxx, 1.0);
  }
  const uint state = g_fog.Load(int3(int(cell.x), int(cell.y), 0));
  const float shade = state == 0 ? g_shade.x : (state == 1 ? g_shade.y : g_shade.z);
  return float4(shade.xxx, 1.0);
}
