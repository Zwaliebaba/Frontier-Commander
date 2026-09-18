#pragma once

#include "WindowsHeader.h"

#include <d3d12.h>
#include <DirectXMath.h>
#include <unknwn.h>
#include <winrt/base.h>

#include "Camera.h"
#include "HeightView.h"
#include "Lighting.h"
#include "TerrainChunk.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Neuron
{

class GraphicsDevice;

/// The near band's chunks draw at stride 1, and the frame's triangles stay near this many.
inline constexpr std::uint32_t TERRAIN_TRIANGLE_BUDGET = 1000000;

/// The terrain pass (TechnicalDesign.md §6.2): every chunk of the view at every stride, built once
/// into one vertex buffer and one index buffer, and each frame the chunks the frustum keeps, the
/// near ones at full detail and the rest at the stride that keeps the count near the budget, lit by
/// the two lights and fogged as the frame's constants say. The pass also owns the scene constants
/// the water pass reads.
class TerrainPass
{
public:
  TerrainPass(GraphicsDevice& _device, const HeightView& _view, const TerrainPalette& _palette, std::uint32_t _sceneSampleCount);

  struct Frame
  {
    float aspect;
    SceneLighting lighting;
    FogMode fogMode;
    float fogStart;
    float fogEnd;
    float fogMaxDesaturation; ///< The ceiling of ADR-007; the linear mode ignores it
    std::array<float, 3> fogColor;
  };

  /// Writes this frame's constants and draws; the scene target is bound and cleared by the caller.
  void Draw(ID3D12GraphicsCommandList* _list, const Camera& _camera, const Frame& _frame);

  /// This frame's constants, for the passes that follow.
  [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS ConstantsAddress() const noexcept
  {
    return m_constantsAddress;
  }
  [[nodiscard]] std::uint32_t LastTriangleCount() const noexcept
  {
    return m_lastTriangles;
  }
  [[nodiscard]] std::uint32_t LastChunkCount() const noexcept
  {
    return m_lastChunks;
  }
  /// The landscape's extent in world units. The fog no longer scales with it (ADR-007); the far
  /// plane still does (ADR-005).
  [[nodiscard]] float ExtentWorldUnits() const noexcept
  {
    return m_extent;
  }

private:
  struct Level
  {
    std::uint32_t vertexOffset;
    std::uint32_t indexOffset;
    std::uint32_t indexCount;
  };
  struct Chunk
  {
    std::array<Level, 4> levels;
    float minX;
    float minY;
    float minZ;
    float maxX;
    float maxY;
    float maxZ;
    float centerX;
    float centerZ;
  };

  GraphicsDevice* m_device;
  std::vector<Chunk> m_chunks;
  winrt::com_ptr<ID3D12Resource> m_vertexBuffer;
  winrt::com_ptr<ID3D12Resource> m_indexBuffer;
  winrt::com_ptr<ID3D12Resource> m_constants;
  winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_pipeline;
  D3D12_VERTEX_BUFFER_VIEW m_vertexView{};
  D3D12_INDEX_BUFFER_VIEW m_indexView{};
  D3D12_GPU_VIRTUAL_ADDRESS m_constantsAddress = 0;
  std::uint8_t* m_constantsMapped = nullptr;
  float m_extent = 0.0f;
  float m_chunkWidth = 0.0f;
  std::uint32_t m_lastTriangles = 0;
  std::uint32_t m_lastChunks = 0;
};

} // namespace Neuron
