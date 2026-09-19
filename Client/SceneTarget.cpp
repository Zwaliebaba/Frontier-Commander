#include "pch.h"

#include "SceneTarget.h"

#include "GraphicsDevice.h"
#include "Log.h"

#include <iterator>

namespace Neuron
{

namespace
{

[[nodiscard]] bool SupportsSampling(ID3D12Device* _device, DXGI_FORMAT _format, std::uint32_t _samples)
{
  D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels{};
  levels.Format = _format;
  levels.SampleCount = _samples;
  levels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
  return SUCCEEDED(_device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &levels, sizeof levels)) &&
         levels.NumQualityLevels > 0;
}

} // namespace

SceneTarget::SceneTarget(GraphicsDevice& _device, const std::array<float, 4>& _clearColor)
  : m_renderTargetViews(_device.Device(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false),
    m_depthStencilViews(_device.Device(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false),
    m_clearColor(_clearColor)
{
  ID3D12Device* device = _device.Device();
  if (!SupportsSampling(device, SCENE_COLOR_FORMAT, SCENE_SAMPLE_COUNT) ||
      !SupportsSampling(device, SCENE_DEPTH_FORMAT, SCENE_SAMPLE_COUNT))
  {
    Log::Write(LogLevel::Warning, "scene target: the adapter has no 4x multisampling for it; drawing single-sampled");
    m_sampleCount = 1;
  }
  const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);

  const CD3DX12_RESOURCE_DESC colorDescription = CD3DX12_RESOURCE_DESC::Tex2D(
    SCENE_COLOR_FORMAT, AUTHORED_WIDTH_PIXELS, AUTHORED_HEIGHT_PIXELS, 1, 1, m_sampleCount, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
  const CD3DX12_CLEAR_VALUE colorClear(SCENE_COLOR_FORMAT, m_clearColor.data());
  winrt::check_hresult(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &colorDescription, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                       &colorClear, IID_PPV_ARGS(m_color.put())));
  winrt::check_hresult(m_color->SetName(L"scene color"));

  const CD3DX12_RESOURCE_DESC depthDescription = CD3DX12_RESOURCE_DESC::Tex2D(
    SCENE_DEPTH_FORMAT, AUTHORED_WIDTH_PIXELS, AUTHORED_HEIGHT_PIXELS, 1, 1, m_sampleCount, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
  const CD3DX12_CLEAR_VALUE depthClear(SCENE_DEPTH_FORMAT, SCENE_DEPTH_CLEAR, 0);
  winrt::check_hresult(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &depthDescription, D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                                       &depthClear, IID_PPV_ARGS(m_depth.put())));
  winrt::check_hresult(m_depth->SetName(L"scene depth"));

  const CD3DX12_RESOURCE_DESC resolvedDescription =
    CD3DX12_RESOURCE_DESC::Tex2D(SCENE_COLOR_FORMAT, AUTHORED_WIDTH_PIXELS, AUTHORED_HEIGHT_PIXELS, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_NONE);
  winrt::check_hresult(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &resolvedDescription,
                                                       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                                                       IID_PPV_ARGS(m_resolved.put())));
  winrt::check_hresult(m_resolved->SetName(L"scene resolved"));

  device->CreateRenderTargetView(m_color.get(), nullptr, m_renderTargetViews.Cpu(0));
  device->CreateDepthStencilView(m_depth.get(), nullptr, m_depthStencilViews.Cpu(0));
  Log::Write(LogLevel::Info, "scene target: " + std::to_string(AUTHORED_WIDTH_PIXELS) + "x" + std::to_string(AUTHORED_HEIGHT_PIXELS) +
                               ", " + std::to_string(m_sampleCount) + " samples");
}

void SceneTarget::Bind(ID3D12GraphicsCommandList* _list) const
{
  const D3D12_CPU_DESCRIPTOR_HANDLE renderTarget = m_renderTargetViews.Cpu(0);
  const D3D12_CPU_DESCRIPTOR_HANDLE depthStencil = m_depthStencilViews.Cpu(0);
  _list->OMSetRenderTargets(1, &renderTarget, FALSE, &depthStencil);
}

void SceneTarget::BindColorOnly(ID3D12GraphicsCommandList* _list) const
{
  const D3D12_CPU_DESCRIPTOR_HANDLE renderTarget = m_renderTargetViews.Cpu(0);
  _list->OMSetRenderTargets(1, &renderTarget, FALSE, nullptr);
}

void SceneTarget::Begin(ID3D12GraphicsCommandList* _list)
{
  const D3D12_CPU_DESCRIPTOR_HANDLE renderTarget = m_renderTargetViews.Cpu(0);
  const D3D12_CPU_DESCRIPTOR_HANDLE depthStencil = m_depthStencilViews.Cpu(0);
  Bind(_list);
  _list->ClearRenderTargetView(renderTarget, m_clearColor.data(), 0, nullptr);
  _list->ClearDepthStencilView(depthStencil, D3D12_CLEAR_FLAG_DEPTH, SCENE_DEPTH_CLEAR, 0, 0, nullptr);
  const CD3DX12_VIEWPORT viewport(0.0f, 0.0f, static_cast<float>(AUTHORED_WIDTH_PIXELS), static_cast<float>(AUTHORED_HEIGHT_PIXELS));
  const CD3DX12_RECT scissor(0, 0, static_cast<LONG>(AUTHORED_WIDTH_PIXELS), static_cast<LONG>(AUTHORED_HEIGHT_PIXELS));
  _list->RSSetViewports(1, &viewport);
  _list->RSSetScissorRects(1, &scissor);
}

void SceneTarget::Resolve(ID3D12GraphicsCommandList* _list)
{
  if (m_sampleCount > 1)
  {
    const D3D12_RESOURCE_BARRIER before[] = {
      CD3DX12_RESOURCE_BARRIER::Transition(m_color.get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE),
      CD3DX12_RESOURCE_BARRIER::Transition(m_resolved.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                           D3D12_RESOURCE_STATE_RESOLVE_DEST)};
    _list->ResourceBarrier(static_cast<UINT>(std::size(before)), before);
    _list->ResolveSubresource(m_resolved.get(), 0, m_color.get(), 0, SCENE_COLOR_FORMAT);
    const D3D12_RESOURCE_BARRIER after[] = {
      CD3DX12_RESOURCE_BARRIER::Transition(m_color.get(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
      CD3DX12_RESOURCE_BARRIER::Transition(m_resolved.get(), D3D12_RESOURCE_STATE_RESOLVE_DEST,
                                           D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)};
    _list->ResourceBarrier(static_cast<UINT>(std::size(after)), after);
    return;
  }
  // Single-sampled, a resolve is not allowed and a copy is the same picture.
  const D3D12_RESOURCE_BARRIER before[] = {
    CD3DX12_RESOURCE_BARRIER::Transition(m_color.get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE),
    CD3DX12_RESOURCE_BARRIER::Transition(m_resolved.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST)};
  _list->ResourceBarrier(static_cast<UINT>(std::size(before)), before);
  _list->CopyResource(m_resolved.get(), m_color.get());
  const D3D12_RESOURCE_BARRIER after[] = {
    CD3DX12_RESOURCE_BARRIER::Transition(m_color.get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
    CD3DX12_RESOURCE_BARRIER::Transition(m_resolved.get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)};
  _list->ResourceBarrier(static_cast<UINT>(std::size(after)), after);
}

} // namespace Neuron
