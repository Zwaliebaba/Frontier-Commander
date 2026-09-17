// The water pass's pixel half: one colour, fogged as the terrain is. The caustic texture, the shore
// lightmap and the waves of SpeciesTerrain.md §7 are M2's.

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

static const float3 WATER_COLOR = float3(0.08, 0.22, 0.38);

float4 main(float4 position : SV_Position, float3 world : WORLDPOS) : SV_Target
{
  const float distance = length(world - g_cameraPosition.xyz);
  const float amount = saturate((distance - g_fog.x) / max(g_fog.y - g_fog.x, 1.0));
  float3 color = WATER_COLOR;
  if (g_fog.z < 0.5)
  {
    color = lerp(color, g_fogColor.rgb, amount);
  }
  else
  {
    const float luminance = dot(color, float3(0.299, 0.587, 0.114));
    color = lerp(color, luminance.xxx, amount);
  }
  return float4(color, 1.0);
}
