#pragma once

#include "WindowsHeader.h"

#include <d3d12.h>
#include <unknwn.h>
#include <winrt/base.h>

#include "GraphicsDevice.h"
#include "InterfaceDesc.h"
#include "ModelBuffers.h"
#include "RenderView.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Neuron
{

// The geometry pass (TechnicalDesign.md §6.4): every instance of the render view drawn from the
// shared model buffers, lit by the two directional lights the terrain pass already put in b0 and
// fogged over the same range, with the team-colour slot written unlit in the seat's colour.
//
// THE INSTANCES ARE GROUPED BY MODEL, IN ONE BUFFER PER FRAME IN FLIGHT. m1-vertical-slice/K1 asks
// for "one instance buffer per model per frame"; what it gets is one buffer per frame holding a
// contiguous RUN per model, and one DrawIndexedInstanced per run. That is the same batching - a
// draw per model, never a draw per instance - with one allocation and one map instead of a
// resource per model per frame. The grouping is a counting sort over the model index, so the order
// a run is drawn in does not depend on the order the render view happened to list its objects.
//
// THE BUFFER GROWS ON ITS OWN SLOT ONLY. GraphicsDevice::BeginFrame has already waited for the
// frame that last used this slot, so this slot's buffer is one the GPU has finished with and may be
// replaced; the other two slots may still be in flight and are left alone. Each slot therefore
// carries its own capacity and reaches the high-water mark on its own.
//
// WHAT THIS PASS DOES NOT DO. It does not cull: an instance is a few dozen triangles and M1 draws
// hundreds of them, so the frustum test would cost more list-building than it saves, and the number
// that would change that is the frame time G2's capture measures. It does not draw the rank badge
// of RenderInstance either - a badge is a textured quad and belongs with the UI pass, not with the
// lit geometry.
//
// THE PALETTE IS TAKEN ONCE AND THE INDEX IS RESOLVED HERE (Design/Interface.md §11 row 2;
// m1-vertical-slice/R2). A RenderInstance carries the commander's colour as an INDEX, not as a
// packed word: the eight colours are content (GameData\Interface.json, C6), and copying one into
// every instance of every frame is how the geometry and the minimap come to disagree about what
// seat 3 looks like. The pass holds the table for the life of the match and looks it up once per
// instance, which is the same work the caller was doing and one fewer place for it to be wrong.
//
// A WRECK WEARS ITS COMMANDER'S COLOUR LIKE ANYTHING ELSE. TechnicalDesign.md §6.2's table draws
// "every device, structure, feature and wreck" with "team colour substituted", so this pass does
// not read RenderInstanceKind at all; that distinction is picking's and the minimap's.

class GeometryPass
{
public:
  /// _models outlives the pass; it is the whole model set, built once. _commanderColors is C6's
  /// table in seat order; a shorter one leaves the seats it does not reach drawn in white, which
  /// is visible rather than silent.
  GeometryPass(GraphicsDevice& _device, const ModelBuffers& _models, std::uint32_t _sceneSampleCount,
               std::span<const Frontier::Rgba8> _commanderColors);

  /// _constants is the terrain pass's scene constants for this frame, as the water pass takes them.
  void Draw(ID3D12GraphicsCommandList* _list, D3D12_GPU_VIRTUAL_ADDRESS _constants, std::span<const RenderInstance> _instances);

  [[nodiscard]] std::uint32_t LastInstanceCount() const noexcept
  {
    return m_lastInstances;
  }
  [[nodiscard]] std::uint32_t LastDrawCount() const noexcept
  {
    return m_lastDraws;
  }
  /// Instances of the last frame naming a model the buffers do not hold; a content fault, counted
  /// rather than drawn.
  [[nodiscard]] std::uint32_t LastUnknownModelCount() const noexcept
  {
    return m_lastUnknownModels;
  }

private:
  /// Makes this frame's slot hold at least _instances of them, growing in powers of two.
  void Reserve(std::uint32_t _slot, std::uint32_t _instances);

  /// What a seat the palette does not reach is drawn in: opaque white, which nobody authored and
  /// so nobody mistakes for a commander's colour.
  static constexpr std::uint32_t UNKNOWN_COMMANDER_COLOR = PackedRgba8(255, 255, 255, 255);

  GraphicsDevice* m_device;
  const ModelBuffers* m_models;
  /// The eight of Frontier::COMMANDER_COLOR_COUNT, packed once at construction.
  std::array<std::uint32_t, Frontier::COMMANDER_COLOR_COUNT> m_commanderColors{};
  winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_pipeline;
  std::array<winrt::com_ptr<ID3D12Resource>, FRAMES_IN_FLIGHT> m_instanceBuffers;
  std::array<std::uint8_t*, FRAMES_IN_FLIGHT> m_instanceMapped{};
  std::array<std::uint32_t, FRAMES_IN_FLIGHT> m_instanceCapacity{};
  std::vector<GeometryInstance> m_ordered; ///< This frame's instances, grouped by model
  std::vector<std::uint32_t> m_runStart;   ///< Per model, where its run begins in m_ordered
  std::vector<std::uint32_t> m_runLength;  ///< Per model, how long that run is
  std::vector<std::uint32_t> m_runCursor;  ///< Per model, the scatter's write position
  std::uint32_t m_lastInstances = 0;
  std::uint32_t m_lastDraws = 0;
  std::uint32_t m_lastUnknownModels = 0;
};

} // namespace Neuron
