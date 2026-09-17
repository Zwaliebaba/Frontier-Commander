#include "pch.h"

#include "PresentPass.h"

#include "GraphicsDevice.h"
#include "Log.h"
#include "SceneTarget.h"
#include "SwapChain.h"

// Written by the shader compiler on every build (AGENTS.md §2); nothing else includes them.
#include "CompiledShaders/PresentPS.h"
#include "CompiledShaders/PresentVS.h"

#include <algorithm>
#include <climits>
#include <iterator>
#include <string>

namespace Neuron
{

namespace
{

constexpr float BLACK[4] = {0.0f, 0.0f, 0.0f, 1.0f};

} // namespace

PresentPass::PresentPass(GraphicsDevice& _device, const SceneTarget& _scene)
  : m_sceneViews(_device.Device(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true)
{
  ID3D12Device* device = _device.Device();

  // b0 the one root constant, t0 the scene target, s0 point and s1 linear (PresentPS.hlsl).
  const CD3DX12_DESCRIPTOR_RANGE sceneRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
  CD3DX12_ROOT_PARAMETER parameters[2];
  parameters[0].InitAsConstants(1, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
  parameters[1].InitAsDescriptorTable(1, &sceneRange, D3D12_SHADER_VISIBILITY_PIXEL);
  CD3DX12_STATIC_SAMPLER_DESC samplers[2];
  samplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                   D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
  samplers[1].Init(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                   D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
  samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  const CD3DX12_ROOT_SIGNATURE_DESC description(2, parameters, 2, samplers, D3D12_ROOT_SIGNATURE_FLAG_NONE);
  winrt::com_ptr<ID3DBlob> serialized;
  winrt::com_ptr<ID3DBlob> errors;
  const HRESULT result = D3D12SerializeRootSignature(&description, D3D_ROOT_SIGNATURE_VERSION_1, serialized.put(), errors.put());
  if (FAILED(result))
  {
    if (errors)
    {
      Log::Write(LogLevel::Error, std::string("present pass: root signature: ") + static_cast<const char*>(errors->GetBufferPointer()));
    }
    winrt::throw_hresult(result);
  }
  winrt::check_hresult(
    device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(m_rootSignature.put())));

  // Every member set by hand rather than from {}: the description holds enumerations with no
  // zero enumerator (D3D12_BLEND, D3D12_CULL_MODE, D3D12_FILL_MODE, D3D12_STENCIL_OP), and a
  // zero-initialised one is not a value of its type.
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline;
  pipeline.pRootSignature = m_rootSignature.get();
  pipeline.VS = CD3DX12_SHADER_BYTECODE(g_PresentVS, sizeof g_PresentVS);
  pipeline.PS = CD3DX12_SHADER_BYTECODE(g_PresentPS, sizeof g_PresentPS);
  pipeline.DS = D3D12_SHADER_BYTECODE{};
  pipeline.HS = D3D12_SHADER_BYTECODE{};
  pipeline.GS = D3D12_SHADER_BYTECODE{};
  pipeline.StreamOutput = D3D12_STREAM_OUTPUT_DESC{};
  pipeline.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  pipeline.SampleMask = UINT_MAX;
  pipeline.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pipeline.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
  pipeline.DepthStencilState.DepthEnable = FALSE;
  pipeline.DepthStencilState.StencilEnable = FALSE;
  pipeline.InputLayout = D3D12_INPUT_LAYOUT_DESC{};
  pipeline.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
  pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pipeline.NumRenderTargets = 1;
  std::fill(std::begin(pipeline.RTVFormats), std::end(pipeline.RTVFormats), DXGI_FORMAT_UNKNOWN);
  pipeline.RTVFormats[0] = SwapChain::FORMAT;
  pipeline.DSVFormat = DXGI_FORMAT_UNKNOWN;
  pipeline.SampleDesc = DXGI_SAMPLE_DESC{1, 0};
  pipeline.NodeMask = 0;
  pipeline.CachedPSO = D3D12_CACHED_PIPELINE_STATE{};
  pipeline.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
  winrt::check_hresult(device->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(m_pipeline.put())));

  D3D12_SHADER_RESOURCE_VIEW_DESC view{};
  view.Format = SCENE_COLOR_FORMAT;
  view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  view.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(_scene.Resolved(), &view, m_sceneViews.Cpu(0));
}

void PresentPass::Draw(ID3D12GraphicsCommandList* _list, ID3D12Resource* _backBuffer, D3D12_CPU_DESCRIPTOR_HANDLE _backBufferView,
                       const ScaledRectangle& _fit)
{
  const CD3DX12_RESOURCE_BARRIER toTarget =
    CD3DX12_RESOURCE_BARRIER::Transition(_backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
  _list->ResourceBarrier(1, &toTarget);
  _list->ClearRenderTargetView(_backBufferView, BLACK, 0, nullptr);
  if (_fit.width > 0 && _fit.height > 0)
  {
    _list->OMSetRenderTargets(1, &_backBufferView, FALSE, nullptr);
    const CD3DX12_VIEWPORT viewport(static_cast<float>(_fit.x), static_cast<float>(_fit.y), static_cast<float>(_fit.width),
                                    static_cast<float>(_fit.height));
    const CD3DX12_RECT scissor(_fit.x, _fit.y, _fit.x + static_cast<std::int32_t>(_fit.width),
                               _fit.y + static_cast<std::int32_t>(_fit.height));
    _list->RSSetViewports(1, &viewport);
    _list->RSSetScissorRects(1, &scissor);
    _list->SetGraphicsRootSignature(m_rootSignature.get());
    _list->SetPipelineState(m_pipeline.get());
    ID3D12DescriptorHeap* heaps[] = {m_sceneViews.Heap()};
    _list->SetDescriptorHeaps(1, heaps);
    _list->SetGraphicsRoot32BitConstant(0, _fit.mode == ScaleMode::Bilinear ? 1u : 0u, 0);
    _list->SetGraphicsRootDescriptorTable(1, m_sceneViews.Gpu(0));
    _list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    _list->DrawInstanced(3, 1, 0, 0);
  }
  const CD3DX12_RESOURCE_BARRIER toPresent =
    CD3DX12_RESOURCE_BARRIER::Transition(_backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
  _list->ResourceBarrier(1, &toPresent);
}

} // namespace Neuron
