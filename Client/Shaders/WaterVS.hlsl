// The water pass's vertex half (TechnicalDesign.md §6.2): the plane's corners through the view projection.

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

struct Output
{
  float4 position : SV_Position;
  float3 world : WORLDPOS;
};

Output main(float3 position : POSITION)
{
  Output output;
  output.world = position;
  output.position = mul(float4(position, 1.0), g_viewProjection);
  return output;
}
