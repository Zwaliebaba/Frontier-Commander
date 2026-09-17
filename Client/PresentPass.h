#pragma once

#include "WindowsHeader.h"

#include <d3d12.h>
#include <unknwn.h>
#include <winrt/base.h>

#include "DescriptorHeap.h"
#include "ScaleMode.h"

namespace Neuron
{

class GraphicsDevice;
class SceneTarget;

/// The last pass of the frame (TechnicalDesign.md §6.2): the resolved scene target drawn into the
/// back buffer inside the rectangle ScaleMode chose, through the point sampler at 1:1 and at a
/// whole-number multiple and the bilinear one otherwise, with black bars around it. One triangle
/// from the vertex id, one root constant, one texture, two static samplers.
class PresentPass
{
public:
  PresentPass(GraphicsDevice& _device, const SceneTarget& _scene);

  /// The back buffer is PRESENT on entry and on exit.
  void Draw(ID3D12GraphicsCommandList* _list, ID3D12Resource* _backBuffer, D3D12_CPU_DESCRIPTOR_HANDLE _backBufferView,
            const ScaledRectangle& _fit);

private:
  winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_pipeline;
  DescriptorHeap m_sceneViews;
};

} // namespace Neuron
