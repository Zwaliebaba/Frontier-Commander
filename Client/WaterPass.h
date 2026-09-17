#pragma once

#include "WindowsHeader.h"

#include <d3d12.h>
#include <unknwn.h>
#include <winrt/base.h>

#include "HeightView.h"

#include <cstdint>

namespace Neuron
{

class GraphicsDevice;

/// The Species water plane sits this far under the sea level, so that the shore, dipped to ten
/// under it by the chunk builder, meets it without a seam (SpeciesTerrain.md §7).
inline constexpr float WATER_PLANE_DEPTH = 9.0f;

/// The water pass of M0 (TechnicalDesign.md §6.2): one flat quad of a single colour per chunk that
/// has any sample under the water, at the plane's depth, fogged like the terrain, drawn after it
/// with the depth test on and depth writes off. Waves and the shore band are M2's.
class WaterPass
{
public:
  WaterPass(GraphicsDevice& _device, const HeightView& _view, std::uint32_t _sceneSampleCount);

  /// _constants is the terrain pass's scene constants for this frame.
  void Draw(ID3D12GraphicsCommandList* _list, D3D12_GPU_VIRTUAL_ADDRESS _constants);

  [[nodiscard]] std::uint32_t QuadCount() const noexcept
  {
    return m_indexCount / 6;
  }

private:
  winrt::com_ptr<ID3D12Resource> m_vertexBuffer;
  winrt::com_ptr<ID3D12Resource> m_indexBuffer;
  winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_pipeline;
  D3D12_VERTEX_BUFFER_VIEW m_vertexView{};
  D3D12_INDEX_BUFFER_VIEW m_indexView{};
  std::uint32_t m_indexCount = 0;
};

} // namespace Neuron
