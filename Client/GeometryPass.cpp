#include "pch.h"

#include "GeometryPass.h"

#include "Log.h"
#include "SceneTarget.h"

// Written by the shader compiler on every build (AGENTS.md §2); nothing else includes them.
#include "CompiledShaders/GeometryPS.h"
#include "CompiledShaders/GeometryVS.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <string>

namespace Neuron
{

namespace
{

/// The smallest instance buffer worth allocating; below this the growth is all overhead.
constexpr std::uint32_t MINIMUM_INSTANCE_CAPACITY = 64;

} // namespace

GeometryPass::GeometryPass(GraphicsDevice& _device, const ModelBuffers& _models, std::uint32_t _sceneSampleCount,
                           std::span<const Frontier::Rgba8> _commanderColors)
  : m_device(&_device),
    m_models(&_models)
{
  m_commanderColors.fill(UNKNOWN_COMMANDER_COLOR);
  for (std::size_t seat = 0; seat < m_commanderColors.size() && seat < _commanderColors.size(); ++seat)
  {
    const Frontier::Rgba8& color = _commanderColors[seat];
    m_commanderColors[seat] = PackedRgba8(color.red, color.green, color.blue, color.alpha);
  }

  ID3D12Device* device = _device.Device();

  // b0 as a root descriptor, exactly as the terrain pass declares it, because the constants are the
  // terrain pass's and this pass only reads them.
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
      Log::Write(LogLevel::Error, std::string("geometry: root signature: ") + static_cast<const char*>(errors->GetBufferPointer()));
    }
    winrt::throw_hresult(result);
  }
  winrt::check_hresult(
    device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(m_rootSignature.put())));

  // Slot 0 is the model's vertices, slot 1 the frame's instances.
  const D3D12_INPUT_ELEMENT_DESC layout[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"ORIGIN", 0, DXGI_FORMAT_R32G32B32_FLOAT, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
    {"HEADING", 0, DXGI_FORMAT_R32G32_FLOAT, 1, 12, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
    {"TEAMCOLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 1, 20, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
    {"SCALE", 0, DXGI_FORMAT_R32G32B32_FLOAT, 1, 24, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
  };
  // Every member set by hand: the description holds enumerations with no zero enumerator.
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline;
  pipeline.pRootSignature = m_rootSignature.get();
  pipeline.VS = CD3DX12_SHADER_BYTECODE(g_GeometryVS, sizeof g_GeometryVS);
  pipeline.PS = CD3DX12_SHADER_BYTECODE(g_GeometryPS, sizeof g_GeometryPS);
  pipeline.DS = D3D12_SHADER_BYTECODE{};
  pipeline.HS = D3D12_SHADER_BYTECODE{};
  pipeline.GS = D3D12_SHADER_BYTECODE{};
  pipeline.StreamOutput = D3D12_STREAM_OUTPUT_DESC{};
  pipeline.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  pipeline.SampleMask = UINT_MAX;
  pipeline.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  // Both faces, as the terrain draws both. The normal no longer depends on which way a triangle
  // winds - ModelBuffers baked it, and counted the triangles that disagree - so culling would buy
  // fill rate and nothing else, and it would turn a model wound the other way into a hole rather
  // than into a dark face. The measurement that would change this is G2's frame time.
  pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pipeline.RasterizerState.MultisampleEnable = _sceneSampleCount > 1 ? TRUE : FALSE;
  pipeline.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
  pipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER; // The reversed depth of SceneTarget.h
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

void GeometryPass::Reserve(std::uint32_t _slot, std::uint32_t _instances)
{
  if (_instances <= m_instanceCapacity[_slot])
  {
    return;
  }
  std::uint32_t capacity = std::max(m_instanceCapacity[_slot], MINIMUM_INSTANCE_CAPACITY);
  while (capacity < _instances)
  {
    capacity *= 2;
  }
  // The frame that last used this slot has completed on the GPU (GraphicsDevice::BeginFrame waited
  // for it), so releasing this slot's buffer here frees a resource nothing is reading.
  winrt::com_ptr<ID3D12Resource> buffer;
  const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
  const CD3DX12_RESOURCE_DESC description = CD3DX12_RESOURCE_DESC::Buffer(static_cast<std::uint64_t>(capacity) * sizeof(GeometryInstance));
  winrt::check_hresult(m_device->Device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
                                                                   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(buffer.put())));
  winrt::check_hresult(buffer->SetName(L"model instances"));
  void* mapped = nullptr;
  const D3D12_RANGE nothing{0, 0};
  winrt::check_hresult(buffer->Map(0, &nothing, &mapped));
  m_instanceBuffers[_slot] = buffer;
  m_instanceMapped[_slot] = static_cast<std::uint8_t*>(mapped);
  m_instanceCapacity[_slot] = capacity;
}

void GeometryPass::Draw(ID3D12GraphicsCommandList* _list, D3D12_GPU_VIRTUAL_ADDRESS _constants, std::span<const RenderInstance> _instances)
{
  m_lastInstances = 0;
  m_lastDraws = 0;
  m_lastUnknownModels = 0;
  const std::uint32_t models = m_models->Count();
  if (_instances.empty() || models == 0)
  {
    return;
  }

  // A counting sort over the model index: count, then the runs, then the scatter. The result does
  // not depend on the order the render view listed its objects.
  m_runLength.assign(models, 0);
  for (const RenderInstance& instance : _instances)
  {
    if (instance.modelId >= models)
    {
      ++m_lastUnknownModels;
      continue;
    }
    ++m_runLength[instance.modelId];
  }
  m_runStart.assign(models, 0);
  std::uint32_t running = 0;
  for (std::uint32_t model = 0; model < models; ++model)
  {
    m_runStart[model] = running;
    running += m_runLength[model];
  }
  if (running == 0)
  {
    return;
  }
  m_runCursor = m_runStart;
  m_ordered.resize(running);
  for (const RenderInstance& instance : _instances)
  {
    if (instance.modelId >= models)
    {
      continue;
    }
    // The one place a heading becomes a pair of floats: once per instance rather than once per
    // vertex, which is what the vertex shader's two multiply-adds buy.
    const std::uint32_t color =
      instance.colorIndex < m_commanderColors.size() ? m_commanderColors[instance.colorIndex] : UNKNOWN_COMMANDER_COLOR;
    const GeometryInstance ordered{instance.x, instance.y, instance.z, std::cos(instance.headingRadians), std::sin(instance.headingRadians),
                                   color};
    m_ordered[m_runCursor[instance.modelId]++] = ordered;
  }

  const std::uint32_t slot = m_device->FrameIndex();
  Reserve(slot, running);
  std::memcpy(m_instanceMapped[slot], m_ordered.data(), static_cast<std::size_t>(running) * sizeof(GeometryInstance));
  D3D12_VERTEX_BUFFER_VIEW views[2] = {m_models->VertexView(), {}};
  views[1].BufferLocation = m_instanceBuffers[slot]->GetGPUVirtualAddress();
  views[1].SizeInBytes = static_cast<UINT>(static_cast<std::size_t>(running) * sizeof(GeometryInstance));
  views[1].StrideInBytes = sizeof(GeometryInstance);
  const D3D12_INDEX_BUFFER_VIEW indexView = m_models->IndexView();

  _list->SetGraphicsRootSignature(m_rootSignature.get());
  _list->SetPipelineState(m_pipeline.get());
  _list->SetGraphicsRootConstantBufferView(0, _constants);
  _list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  _list->IASetVertexBuffers(0, 2, views);
  _list->IASetIndexBuffer(&indexView);
  for (std::uint32_t model = 0; model < models; ++model)
  {
    const ModelRange& range = m_models->Range(model);
    if (m_runLength[model] == 0 || range.indexCount == 0)
    {
      continue;
    }
    _list->DrawIndexedInstanced(range.indexCount, m_runLength[model], range.indexOffset, static_cast<INT>(range.vertexOffset),
                                m_runStart[model]);
    ++m_lastDraws;
  }
  m_lastInstances = running;
  Log::Write(LogLevel::Debug, "geometry: " + std::to_string(m_lastInstances) + " instances in " + std::to_string(m_lastDraws) + " draws");
}

} // namespace Neuron
