// The present pass's pixel half (AGENTS.md §5): the resolved scene target sampled once, with the
// point sampler for the 1:1 and integer cases and the bilinear sampler otherwise; the root
// constant says which.

Texture2D<float4> g_scene : register(t0);
SamplerState g_pointSampler : register(s0);
SamplerState g_linearSampler : register(s1);

cbuffer PresentConstants : register(b0)
{
  uint g_bilinear;
};

float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
  if (g_bilinear != 0)
  {
    return g_scene.Sample(g_linearSampler, uv);
  }
  return g_scene.Sample(g_pointSampler, uv);
}
