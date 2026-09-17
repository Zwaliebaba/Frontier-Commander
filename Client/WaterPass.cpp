#include "pch.h"

#include "WaterPass.h"

#include "GraphicsDevice.h"
#include "Log.h"
#include "SceneTarget.h"
#include "TerrainChunk.h"

// Written by the shader compiler on every build (AGENTS.md §2); nothing else includes them.
#include "CompiledShaders/WaterPS.h"
#include "CompiledShaders/WaterVS.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace Neuron
{

namespace
{

struct WaterVertex
{
  float x;
  float y;
  float z;
};

/// Whether any sample of the chunk lies under the water.
[[nodiscard]] bool ChunkHasWater(const HeightView& _view, std::uint32_t _chunkX, std::uint32_t _chunkY) noexcept
{
  const std::uint32_t side = _view.samplesPerSide;
  const std::uint32_t x0 = _chunkX * CHUNK_STEPS;
  const std::uint32_t y0 = _chunkY * CHUNK_STEPS;
  for (std::uint32_t y = y0; y <= y0 + CHUNK_STEPS && y < side; ++y)
  {
    for (std::uint32_t x = x0; x <= x0 + CHUNK_STEPS && x < side; ++x)
    {
      if (_view.samples[static_cast<std::size_t>(y) * side + x] < _view.waterLevel)
      {
        return true;
      }
    }
  }
  return false;
}

[[nodiscard]] winrt::com_ptr<ID3D12Resource> UploadBuffer(ID3D12Device* _device, const void* _bytes, std::size_t _size,
                                                          const wchar_t* _name)
{
  winrt::com_ptr<ID3D12Resource> buffer;
  const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
  const CD3DX12_RESOURCE_DESC description = CD3DX12_RESOURCE_DESC::Buffer(std::max<std::size_t>(_size, 16));
  winrt::check_hresult(_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                        nullptr, IID_PPV_ARGS(buffer.put())));
  winrt::check_hresult(buffer->SetName(_name));
  if (_size > 0)
  {
    void* mapped = nullptr;
    const D3D12_RANGE nothing{0, 0};
    winrt::check_hresult(buffer->Map(0, &nothing, &mapped));
    std::memcpy(mapped, _bytes, _size);
    buffer->Unmap(0, nullptr);
  }
  return buffer;
}

} // namespace

WaterPass::WaterPass(GraphicsDevice& _device, const HeightView& _view, std::uint32_t _sceneSampleCount)
{
  ID3D12Device* device = _device.Device();
  const std::uint32_t chunksPerSide = ChunksPerSide(_view);
  const float spacing = static_cast<float>(_view.spacingWorldUnits);
  const float planeY = static_cast<float>(_view.waterLevel) - WATER_PLANE_DEPTH;
  std::vector<WaterVertex> vertices;
  std::vector<std::uint16_t> indices;
  for (std::uint32_t chunkY = 0; chunkY < chunksPerSide; ++chunkY)
  {
    for (std::uint32_t chunkX = 0; chunkX < chunksPerSide; ++chunkX)
    {
      if (!ChunkHasWater(_view, chunkX, chunkY))
      {
        continue;
      }
      const float x0 = static_cast<float>(chunkX * CHUNK_STEPS) * spacing;
      const float z0 = static_cast<float>(chunkY * CHUNK_STEPS) * spacing;
      const float x1 = x0 + static_cast<float>(CHUNK_STEPS) * spacing;
      const float z1 = z0 + static_cast<float>(CHUNK_STEPS) * spacing;
      const auto first = static_cast<std::uint16_t>(vertices.size());
      vertices.push_back({x0, planeY, z0});
      vertices.push_back({x1, planeY, z0});
      vertices.push_back({x0, planeY, z1});
      vertices.push_back({x1, planeY, z1});
      for (const int corner : {0, 1, 2, 3, 2, 1})
      {
        indices.push_back(static_cast<std::uint16_t>(first + corner));
      }
    }
  }
  m_indexCount = static_cast<std::uint32_t>(indices.size());
  m_vertexBuffer = UploadBuffer(device, vertices.data(), vertices.size() * sizeof(WaterVertex), L"water vertices");
  m_indexBuffer = UploadBuffer(device, indices.data(), indices.size() * sizeof(std::uint16_t), L"water indices");
  m_vertexView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
  m_vertexView.SizeInBytes = static_cast<UINT>(vertices.size() * sizeof(WaterVertex));
  m_vertexView.StrideInBytes = sizeof(WaterVertex);
  m_indexView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
  m_indexView.SizeInBytes = static_cast<UINT>(indices.size() * sizeof(std::uint16_t));
  m_indexView.Format = DXGI_FORMAT_R16_UINT;
  Log::Write(LogLevel::Info, "water: " + std::to_string(QuadCount()) + " chunks under the plane");

  CD3DX12_ROOT_PARAMETER parameters[1];
  parameters[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
  const CD3DX12_ROOT_SIGNATURE_DESC description(1, parameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
  winrt::com_ptr<ID3DBlob> serialized;
  winrt::com_ptr<ID3DBlob> errors;
  const HRESULT result = D3D12SerializeRootSignature(&description, D3D_ROOT_SIGNATURE_VERSION_1, serialized.put(), errors.put());
  if (FAILED(result))
  {
    if (errors)
    {
      Log::Write(LogLevel::Error, std::string("water: root signature: ") + static_cast<const char*>(errors->GetBufferPointer()));
    }
    winrt::throw_hresult(result);
  }
  winrt::check_hresult(
    device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(m_rootSignature.put())));

  const D3D12_INPUT_ELEMENT_DESC layout[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline;
  pipeline.pRootSignature = m_rootSignature.get();
  pipeline.VS = CD3DX12_SHADER_BYTECODE(g_WaterVS, sizeof g_WaterVS);
  pipeline.PS = CD3DX12_SHADER_BYTECODE(g_WaterPS, sizeof g_WaterPS);
  pipeline.DS = D3D12_SHADER_BYTECODE{};
  pipeline.HS = D3D12_SHADER_BYTECODE{};
  pipeline.GS = D3D12_SHADER_BYTECODE{};
  pipeline.StreamOutput = D3D12_STREAM_OUTPUT_DESC{};
  pipeline.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  pipeline.SampleMask = UINT_MAX;
  pipeline.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pipeline.RasterizerState.MultisampleEnable = _sceneSampleCount > 1 ? TRUE : FALSE;
  pipeline.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
  // Tested against the terrain, never written: the plane hides nothing drawn after it.
  pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  pipeline.InputLayout = {layout, static_cast<UINT>(std::size(layout))};
  pipeline.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
  pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pipeline.NumRenderTargets = 1;
  std::fill(std::begin(pipeline.RTVFormats), std::end(pipeline.RTVFormats), DXGI_FORMAT_UNKNOWN);
  pipeline.RTVFormats[0] = SCENE_COLOR_FORMAT;
  pipeline.DSVFormat = SCENE_DEPTH_FORMAT;
  pipeline.SampleDesc = DXGI_SAMPLE_DESC{_sceneSampleCount, 0};
  pipeline.NodeMask = 0;
  pipeline.CachedPSO = D3D12_CACHED_PIPELINE_STATE{};
  pipeline.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
  winrt::check_hresult(device->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(m_pipeline.put())));
}

void WaterPass::Draw(ID3D12GraphicsCommandList* _list, D3D12_GPU_VIRTUAL_ADDRESS _constants)
{
  if (m_indexCount == 0)
  {
    return;
  }
  _list->SetGraphicsRootSignature(m_rootSignature.get());
  _list->SetPipelineState(m_pipeline.get());
  _list->SetGraphicsRootConstantBufferView(0, _constants);
  _list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  _list->IASetVertexBuffers(0, 1, &m_vertexView);
  _list->IASetIndexBuffer(&m_indexView);
  _list->DrawIndexedInstanced(m_indexCount, 1, 0, 0, 0);
}

} // namespace Neuron
