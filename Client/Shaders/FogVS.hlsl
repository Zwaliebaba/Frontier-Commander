// The fog pass's vertex half (TechnicalDesign.md §6.2): one triangle covering the scene target,
// from the vertex id alone, so that no vertex buffer is bound. The same triangle the present pass
// draws, and for the same reason.

struct Output
{
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

Output main(uint vertexId : SV_VertexID)
{
  Output output;
  const float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
  output.uv = uv;
  output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
  return output;
}
