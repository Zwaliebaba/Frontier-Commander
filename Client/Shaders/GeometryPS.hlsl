// The geometry pass's pixel half: the same Species lighting the terrain uses (SpeciesLook.md §2,
// TechnicalDesign.md §6.4) - Lambert only, no ambient, two directional lights whose colours may
// exceed one, summed and then clamped - over the flat normal the vertex carries, and then the fog
// of ADR-005 over the absolute range of ADR-007.
//
// THE TEAM-COLOUR SLOT IS THE EXCEPTION PILLAR 3 DEMANDS (Lighting.h; ADR-005). A vertex colour
// with alpha zero is a slot: the instance's seat colour is written as it is, neither lit nor
// fogged, so that no sun tints a commander's colour and no distance greys it, and a fight reads
// from the far end of the map.
//
// The normal is not normalized here. ModelBuffers wrote it to unit length and nointerpolation
// carries it from the triangle's first vertex without arithmetic, so it arrives as it was written;
// the terrain normalizes because its cross product of two derivatives has no length worth having.

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
  nointerpolation float3 normal : NORMAL;
  nointerpolation float4 color : COLOR;
  nointerpolation float4 teamColor : TEAMCOLOR;
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
  return lerp(color, luminance.xxx, amount * g_fog.w);
}

float4 main(Input input) : SV_Target
{
  if (input.color.a <= 0.0)
  {
    return float4(input.teamColor.rgb, 1.0);
  }
  const float3 light = g_lightColor0.rgb * saturate(dot(input.normal, g_lightDirection0.xyz)) +
                       g_lightColor1.rgb * saturate(dot(input.normal, g_lightDirection1.xyz));
  const float3 lit = min(input.color.rgb * light, 1.0);
  return float4(Fogged(lit, input.world), 1.0);
}
