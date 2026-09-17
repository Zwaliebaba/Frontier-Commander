// The terrain pass's pixel half: the Species lighting (SpeciesLook.md §2, TechnicalDesign.md §6.4),
// Lambert only, no ambient, two directional lights whose colours may exceed one, summed and then
// clamped, one normal per triangle from the derivatives of the world position; a vertex colour
// with alpha zero is an unlit team-colour slot (Lighting.h); then the fog of the fog-and-lighting
// ADR, linear to the fog colour or a desaturation, by the mode in the constants.

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
  float4 position : SV_Position;
  float3 world : WORLDPOS;
  nointerpolation float4 color : COLOR;
};

float3 Fogged(float3 color, float3 world)
{
  const float distance = length(world - g_cameraPosition.xyz);
  const float amount = saturate((distance - g_fog.x) / max(g_fog.y - g_fog.x, 1.0));
  if (g_fog.z < 0.5)
  {
    return lerp(color, g_fogColor.rgb, amount);
  }
  const float luminance = dot(color, float3(0.299, 0.587, 0.114));
  return lerp(color, luminance.xxx, amount);
}

float4 main(Input input) : SV_Target
{
  float3 normal = normalize(cross(ddy(input.world), ddx(input.world)));
  if (normal.y < 0.0)
  {
    normal = -normal;
  }
  float3 lit = input.color.rgb;
  if (input.color.a > 0.0)
  {
    const float3 light = g_lightColor0.rgb * saturate(dot(normal, g_lightDirection0.xyz)) + g_lightColor1.rgb * saturate(dot(normal, g_lightDirection1.xyz));
    lit = min(input.color.rgb * light, 1.0);
  }
  return float4(Fogged(lit, input.world), 1.0);
}
