// The terrain pass's vertex half (TechnicalDesign.md §6.2): the world position through the view
// projection, and the vertex colour handed on flat, so that every triangle takes the colour of its
// first vertex, as the Species landscape is one colour per triangle.

cbuffer SceneConstants : register(b0)
{
  float4x4 g_viewProjection;
  float4 g_cameraPosition;
  float4 g_lightDirection0;
  float4 g_lightColor0;
  float4 g_lightDirection1;
  float4 g_lightColor1;
  float4 g_fog;
  float4 g_fogColor;
};

struct Input
{
  float3 position : POSITION;
  float4 color : COLOR;
};

struct Output
{
  float4 position : SV_Position;
  float3 world : WORLDPOS;
  nointerpolation float4 color : COLOR;
};

Output main(Input input)
{
  Output output;
  output.world = input.position;
  output.position = mul(float4(input.position, 1.0), g_viewProjection);
  output.color = input.color;
  return output;
}
