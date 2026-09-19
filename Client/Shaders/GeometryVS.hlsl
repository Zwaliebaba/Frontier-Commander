// The geometry pass's vertex half (TechnicalDesign.md §6.4; ADR-011): a model's vertex turned by
// its instance's heading, moved to its instance's origin and put through the view projection. The
// normal is baked per triangle by ModelBuffers rather than taken from the pixel shader's
// derivatives, so it rides along and is turned by the same rotation - a rotation about y has no
// scale, so it is its own inverse transpose and the normal needs no other treatment.
//
// The heading arrives as a cosine and a sine, computed once per instance on the CPU, so that this
// is two multiply-adds per vertex and no trigonometry.

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
  float3 normal : NORMAL;
  float4 color : COLOR;
  float3 origin : ORIGIN;
  float2 heading : HEADING;
  float4 teamColor : TEAMCOLOR;
  float3 scale : SCALE;
};

struct Output
{
  float4 position : SV_Position;
  float3 world : WORLDPOS;
  nointerpolation float3 normal : NORMAL;
  nointerpolation float4 color : COLOR;
  nointerpolation float4 teamColor : TEAMCOLOR;
};

/// Heading zero looks along +z and grows toward +x, which is the convention the marker importer
/// writes and the simulation's binary angle counts in.
float3 Turn(float3 value, float2 heading)
{
  return float3(value.x * heading.x + value.z * heading.y, value.y, value.z * heading.x - value.x * heading.y);
}

Output main(Input input)
{
  // SCALED BEFORE IT IS TURNED, because the scale is in the model's own axes: a construction site
  // is short along ITS vertical, which is the same vertical either way, but a row drawn at another
  // size must be scaled where its vertices are and not after they have been placed in the world.
  const float3 world = Turn(input.position * input.scale, input.heading) + input.origin;
  Output output;
  output.world = world;
  output.position = mul(float4(world, 1.0), g_viewProjection);
  // THE NORMAL IS NOT SCALED, and for a non-uniform scale that is not exactly right: the correct
  // transform is the inverse transpose, which for a squash along y tilts the normals of every
  // sloped face. What it would buy is correct lighting on a building for the seconds it is going
  // up, at the cost of an inverse transpose per instance on every instance in the frame for the
  // whole match. The lighting is a little flat on a construction site; nothing else is wrong.
  output.normal = Turn(input.normal, input.heading);
  output.color = input.color;
  output.teamColor = input.teamColor;
  return output;
}
