#pragma once

#include "WindowsHeader.h"

#include <d3d12.h>
#include <unknwn.h>
#include <winrt/base.h>

#include "DescriptorHeap.h"
#include "ScaleMode.h" // AUTHORED_WIDTH_PIXELS and AUTHORED_HEIGHT_PIXELS moved here (m1/K3)

#include <array>
#include <cstdint>

namespace Neuron
{

class GraphicsDevice;

/// The scene target's multisampling (ADR-004); a device without it draws single-sampled and says so.
inline constexpr std::uint32_t SCENE_SAMPLE_COUNT = 4;
inline constexpr DXGI_FORMAT SCENE_COLOR_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
inline constexpr DXGI_FORMAT SCENE_DEPTH_FORMAT = DXGI_FORMAT_D32_FLOAT;
/// The depth is reversed (ADR-005): cleared to 0, the near plane at 1, and every world pass tests
/// GREATER, so that the float's precision lies in the far field, which is drawn rather than fogged.
inline constexpr float SCENE_DEPTH_CLEAR = 0.0f;

/// The colour target and depth buffer the passes draw into, at the authored resolution and
/// multisampled, and the single-sample texture they resolve into for the present pass and the
/// capture (AGENTS.md §5; TechnicalDesign.md §6.1). Resting states: the colour target in
/// RENDER_TARGET, the depth buffer in DEPTH_WRITE, the resolved texture in PIXEL_SHADER_RESOURCE;
/// whoever transitions one of them puts it back.
class SceneTarget
{
public:
  /// _clearColor is also the target's optimised clear value, so Begin clears to it without a
  /// debug-layer warning; it cannot change afterwards.
  SceneTarget(GraphicsDevice& _device, const std::array<float, 4>& _clearColor);

  /// Clears both targets, binds them, and sets the viewport and scissor to the authored extent.
  void Begin(ID3D12GraphicsCommandList* _list);
  /// Binds both targets again, clearing neither: what a pass that unbound one of them calls to put
  /// the frame back the way it found it.
  void Bind(ID3D12GraphicsCommandList* _list) const;
  /// Binds the colour target alone, leaving the depth buffer free to be read as a texture. The
  /// caller transitions the depth buffer itself and calls Bind when it is done (m1-vertical-slice/K2).
  void BindColorOnly(ID3D12GraphicsCommandList* _list) const;
  /// Resolves the samples into the single-sample texture.
  void Resolve(ID3D12GraphicsCommandList* _list);

  [[nodiscard]] ID3D12Resource* Resolved() const noexcept
  {
    return m_resolved.get();
  }
  /// The depth buffer, for a pass that reads it; its resting state is DEPTH_WRITE.
  [[nodiscard]] ID3D12Resource* Depth() const noexcept
  {
    return m_depth.get();
  }
  [[nodiscard]] std::uint32_t SampleCount() const noexcept
  {
    return m_sampleCount;
  }

private:
  winrt::com_ptr<ID3D12Resource> m_color;
  winrt::com_ptr<ID3D12Resource> m_depth;
  winrt::com_ptr<ID3D12Resource> m_resolved;
  DescriptorHeap m_renderTargetViews;
  DescriptorHeap m_depthStencilViews;
  std::array<float, 4> m_clearColor;
  std::uint32_t m_sampleCount = SCENE_SAMPLE_COUNT;
};

} // namespace Neuron
