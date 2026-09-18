#include "pch.h"

#include "FogPass.h"

#include "FixedPoint.h"
#include "Log.h"
#include "SceneTarget.h"

// Written by the shader compiler on every build (AGENTS.md §2); nothing else includes them.
#include "CompiledShaders/FogPS.h"
#include "CompiledShaders/FogVS.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace Neuron
{

namespace
{

/// A constant buffer's offset is 256-byte aligned, as it is for the scene constants.
constexpr std::size_t FOG_CONSTANTS_STRIDE = 256;

/// One byte a cell (RenderView.h's FogShade), fetched rather than sampled.
constexpr DXGI_FORMAT FOG_FORMAT = DXGI_FORMAT_R8_UINT;
/// The depth buffer read as a texture: D32_FLOAT has no typeless view, R32_FLOAT is the one it takes.
constexpr DXGI_FORMAT DEPTH_VIEW_FORMAT = DXGI_FORMAT_R32_FLOAT;

/// An upload row is aligned to the placement alignment rather than only to the pitch alignment, so
/// that the offset of ANY row is a legal CopyTextureRegion source and a run of changed rows can be
/// copied from where it already lies. 512 is a multiple of the 256 a row pitch needs.
[[nodiscard]] constexpr std::uint32_t AlignedRowBytes(std::uint32_t _cells) noexcept
{
  constexpr std::uint32_t ALIGNMENT = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
  return ((_cells + ALIGNMENT - 1) / ALIGNMENT) * ALIGNMENT;
}

[[nodiscard]] winrt::com_ptr<ID3D12Resource> UploadBuffer(ID3D12Device* _device, std::size_t _size, const wchar_t* _name)
{
  winrt::com_ptr<ID3D12Resource> buffer;
  const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
  const CD3DX12_RESOURCE_DESC description = CD3DX12_RESOURCE_DESC::Buffer(std::max<std::size_t>(_size, 16));
  winrt::check_hresult(_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                        nullptr, IID_PPV_ARGS(buffer.put())));
  winrt::check_hresult(buffer->SetName(_name));
  return buffer;
}

} // namespace

FogPass::FogPass(GraphicsDevice& _device, const SceneTarget& _scene, std::uint32_t _cellsPerSide)
  : m_device(&_device),
    m_views(_device.Device(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3, true),
    m_cellsPerSide(_cellsPerSide),
    m_uploadRowBytes(AlignedRowBytes(_cellsPerSide)),
    m_sampleCount(_scene.SampleCount())
{
  ID3D12Device* device = _device.Device();

  const CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
  const CD3DX12_RESOURCE_DESC textureDescription =
    CD3DX12_RESOURCE_DESC::Tex2D(FOG_FORMAT, m_cellsPerSide, m_cellsPerSide, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_NONE);
  winrt::check_hresult(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDescription,
                                                       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(m_texture.put())));
  winrt::check_hresult(m_texture->SetName(L"fog grid"));

  const std::size_t uploadBytes = static_cast<std::size_t>(m_uploadRowBytes) * m_cellsPerSide;
  for (std::uint32_t slot = 0; slot < FRAMES_IN_FLIGHT; ++slot)
  {
    m_uploads[slot] = UploadBuffer(device, uploadBytes, L"fog grid upload");
    void* mapped = nullptr;
    const D3D12_RANGE nothing{0, 0};
    winrt::check_hresult(m_uploads[slot]->Map(0, &nothing, &mapped));
    m_uploadMapped[slot] = static_cast<std::uint8_t*>(mapped);
  }
  m_constants = UploadBuffer(device, FOG_CONSTANTS_STRIDE * FRAMES_IN_FLIGHT, L"fog constants");
  void* mapped = nullptr;
  const D3D12_RANGE nothing{0, 0};
  winrt::check_hresult(m_constants->Map(0, &nothing, &mapped));
  m_constantsMapped = static_cast<std::uint8_t*>(mapped);

  // The three views the shader declares, in its order. One depth view is null; FogPass.h says why.
  D3D12_SHADER_RESOURCE_VIEW_DESC fogView{};
  fogView.Format = FOG_FORMAT;
  fogView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  fogView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  fogView.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(m_texture.get(), &fogView, m_views.Cpu(m_views.Allocate()));

  D3D12_SHADER_RESOURCE_VIEW_DESC multisampled{};
  multisampled.Format = DEPTH_VIEW_FORMAT;
  multisampled.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
  multisampled.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  device->CreateShaderResourceView(m_sampleCount > 1 ? _scene.Depth() : nullptr, &multisampled, m_views.Cpu(m_views.Allocate()));

  D3D12_SHADER_RESOURCE_VIEW_DESC single{};
  single.Format = DEPTH_VIEW_FORMAT;
  single.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  single.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  single.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(m_sampleCount > 1 ? nullptr : _scene.Depth(), &single, m_views.Cpu(m_views.Allocate()));

  // b0 as a root descriptor and the three textures as a table. No sampler: every fetch is by
  // integer coordinate, because a cell is either seen or it is not.
  CD3DX12_DESCRIPTOR_RANGE textures[1];
  textures[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0);
  CD3DX12_ROOT_PARAMETER parameters[2];
  parameters[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
  parameters[1].InitAsDescriptorTable(1, textures, D3D12_SHADER_VISIBILITY_PIXEL);
  const CD3DX12_ROOT_SIGNATURE_DESC description(2, parameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);
  winrt::com_ptr<ID3DBlob> serialized;
  winrt::com_ptr<ID3DBlob> errors;
  const HRESULT result = D3D12SerializeRootSignature(&description, D3D_ROOT_SIGNATURE_VERSION_1, serialized.put(), errors.put());
  if (FAILED(result))
  {
    if (errors)
    {
      Log::Write(LogLevel::Error, std::string("fog: root signature: ") + static_cast<const char*>(errors->GetBufferPointer()));
    }
    winrt::throw_hresult(result);
  }
  winrt::check_hresult(
    device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(m_rootSignature.put())));

  // Every member set by hand: the description holds enumerations with no zero enumerator.
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline;
  pipeline.pRootSignature = m_rootSignature.get();
  pipeline.VS = CD3DX12_SHADER_BYTECODE(g_FogVS, sizeof g_FogVS);
  pipeline.PS = CD3DX12_SHADER_BYTECODE(g_FogPS, sizeof g_FogPS);
  pipeline.DS = D3D12_SHADER_BYTECODE{};
  pipeline.HS = D3D12_SHADER_BYTECODE{};
  pipeline.GS = D3D12_SHADER_BYTECODE{};
  pipeline.StreamOutput = D3D12_STREAM_OUTPUT_DESC{};
  // The darkening itself: zero of the source plus the destination times the source, which is the
  // destination multiplied by the shade. The alpha is left exactly as the scene wrote it.
  pipeline.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  pipeline.BlendState.RenderTarget[0].BlendEnable = TRUE;
  pipeline.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
  pipeline.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_COLOR;
  pipeline.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
  pipeline.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
  pipeline.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
  pipeline.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
  pipeline.SampleMask = UINT_MAX;
  pipeline.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pipeline.RasterizerState.MultisampleEnable = m_sampleCount > 1 ? TRUE : FALSE;
  // No depth buffer is bound while this pass reads it as a texture, so there is nothing to test.
  pipeline.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
  pipeline.DepthStencilState.DepthEnable = FALSE;
  pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  pipeline.InputLayout = {nullptr, 0};
  pipeline.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
  pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pipeline.NumRenderTargets = 1;
  std::fill(std::begin(pipeline.RTVFormats), std::end(pipeline.RTVFormats), DXGI_FORMAT_UNKNOWN);
  pipeline.RTVFormats[0] = SCENE_COLOR_FORMAT;
  pipeline.DSVFormat = DXGI_FORMAT_UNKNOWN;
  pipeline.SampleDesc = DXGI_SAMPLE_DESC{m_sampleCount, 0};
  pipeline.NodeMask = 0;
  pipeline.CachedPSO = D3D12_CACHED_PIPELINE_STATE{};
  pipeline.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
  winrt::check_hresult(device->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(m_pipeline.put())));

  Log::Write(LogLevel::Info, "fog: " + std::to_string(m_cellsPerSide) + "x" + std::to_string(m_cellsPerSide) + " cells, " +
                               std::to_string(static_cast<std::size_t>(m_uploadRowBytes) * m_cellsPerSide) + " bytes an upload slot");
}

std::vector<FogRowRun> CoalesceFogRows(std::span<const std::uint32_t> _rows, std::uint32_t _cellsPerSide)
{
  std::vector<FogRowRun> runs;
  for (const std::uint32_t row : _rows)
  {
    if (row >= _cellsPerSide)
    {
      continue;
    }
    if (!runs.empty() && runs.back().first + runs.back().count == row)
    {
      ++runs.back().count;
      continue;
    }
    runs.push_back({row, 1});
  }
  return runs;
}

void FogPass::Upload(ID3D12GraphicsCommandList* _list, const FogView& _fog, std::uint32_t _slot)
{
  // The first frame has no previous grid to differ from and sends all of it.
  const std::vector<FogRowRun> runs =
    m_uploaded ? CoalesceFogRows(_fog.changedRows, m_cellsPerSide) : std::vector<FogRowRun>{{0, m_cellsPerSide}};
  m_lastRowsUploaded = 0;
  if (runs.empty())
  {
    return;
  }

  for (const FogRowRun& run : runs)
  {
    for (std::uint32_t row = run.first; row < run.first + run.count; ++row)
    {
      std::memcpy(m_uploadMapped[_slot] + static_cast<std::size_t>(row) * m_uploadRowBytes,
                  _fog.cells.data() + static_cast<std::size_t>(row) * m_cellsPerSide, m_cellsPerSide);
    }
    m_lastRowsUploaded += run.count;
  }

  const CD3DX12_RESOURCE_BARRIER toCopy =
    CD3DX12_RESOURCE_BARRIER::Transition(m_texture.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
  _list->ResourceBarrier(1, &toCopy);
  for (const FogRowRun& run : runs)
  {
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    footprint.Offset = static_cast<std::uint64_t>(run.first) * m_uploadRowBytes;
    footprint.Footprint.Format = FOG_FORMAT;
    footprint.Footprint.Width = m_cellsPerSide;
    footprint.Footprint.Height = run.count;
    footprint.Footprint.Depth = 1;
    footprint.Footprint.RowPitch = m_uploadRowBytes;
    const CD3DX12_TEXTURE_COPY_LOCATION source(m_uploads[_slot].get(), footprint);
    const CD3DX12_TEXTURE_COPY_LOCATION destination(m_texture.get(), 0);
    _list->CopyTextureRegion(&destination, 0, run.first, 0, &source, nullptr);
  }
  const CD3DX12_RESOURCE_BARRIER toRead =
    CD3DX12_RESOURCE_BARRIER::Transition(m_texture.get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  _list->ResourceBarrier(1, &toRead);
  m_uploaded = true;
}

void FogPass::Draw(ID3D12GraphicsCommandList* _list, const SceneTarget& _scene, const Camera& _camera, float _aspect, const FogView& _fog)
{
  m_lastRowsUploaded = 0;
  if (_fog.cellsPerSide != m_cellsPerSide || _fog.cells.size() < static_cast<std::size_t>(m_cellsPerSide) * m_cellsPerSide)
  {
    // A view that does not match the landscape the pass was built for is not fog, it is a bug
    // elsewhere; drawing it would black the frame, which is the worst way to report it.
    Log::Write(LogLevel::Warning, "fog: the render view's grid is " + std::to_string(_fog.cellsPerSide) + " cells a side against " +
                                    std::to_string(m_cellsPerSide) + "; not drawn");
    return;
  }
  const std::uint32_t slot = m_device->FrameIndex();
  Upload(_list, _fog, slot);

  const DirectX::XMMATRIX viewProjection = _camera.View() * _camera.Projection(_aspect);
  Constants constants{};
  DirectX::XMStoreFloat4x4(&constants.inverseViewProjection, DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(nullptr, viewProjection)));
  constants.grid = {static_cast<float>(m_cellsPerSide), static_cast<float>(WORLD_UNITS_PER_CELL), static_cast<float>(m_sampleCount), 0.0f};
  constants.shade = {FOG_SHADES[0], FOG_SHADES[1], FOG_SHADES[2], 0.0f};
  std::memcpy(m_constantsMapped + slot * FOG_CONSTANTS_STRIDE, &constants, sizeof constants);

  // The depth buffer becomes a texture for the length of this draw, which means it cannot be bound
  // as a depth target at the same time; SceneTarget's contract is that whoever moves it moves it back.
  const CD3DX12_RESOURCE_BARRIER toRead =
    CD3DX12_RESOURCE_BARRIER::Transition(_scene.Depth(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  _list->ResourceBarrier(1, &toRead);
  _scene.BindColorOnly(_list);

  ID3D12DescriptorHeap* heaps[] = {m_views.Heap()};
  _list->SetDescriptorHeaps(1, heaps);
  _list->SetGraphicsRootSignature(m_rootSignature.get());
  _list->SetPipelineState(m_pipeline.get());
  _list->SetGraphicsRootConstantBufferView(0, m_constants->GetGPUVirtualAddress() + slot * FOG_CONSTANTS_STRIDE);
  _list->SetGraphicsRootDescriptorTable(1, m_views.Gpu(0));
  _list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  _list->DrawInstanced(3, 1, 0, 0);

  const CD3DX12_RESOURCE_BARRIER toWrite =
    CD3DX12_RESOURCE_BARRIER::Transition(_scene.Depth(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
  _list->ResourceBarrier(1, &toWrite);
  _scene.Bind(_list);
}

} // namespace Neuron
