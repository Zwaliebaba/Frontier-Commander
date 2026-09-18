#include "pch.h"

#include "TerrainPass.h"

#include "GraphicsDevice.h"
#include "Log.h"
#include "SceneConstants.h"
#include "SceneTarget.h"

// Written by the shader compiler on every build (AGENTS.md §2); nothing else includes them.
#include "CompiledShaders/TerrainPS.h"
#include "CompiledShaders/TerrainVS.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <string>

namespace Neuron
{

namespace
{

struct Plane
{
  float a;
  float b;
  float c;
  float d;
};

/// The six planes of a row-vector view projection, normalized, facing inward.
[[nodiscard]] std::array<Plane, 6> FrustumOf(const DirectX::XMFLOAT4X4& _m) noexcept
{
  const auto plane = [](float _a, float _b, float _c, float _d)
  {
    const float length = std::sqrt(_a * _a + _b * _b + _c * _c);
    return Plane{_a / length, _b / length, _c / length, _d / length};
  };
  return {
    plane(_m._14 + _m._11, _m._24 + _m._21, _m._34 + _m._31, _m._44 + _m._41), // left
    plane(_m._14 - _m._11, _m._24 - _m._21, _m._34 - _m._31, _m._44 - _m._41), // right
    plane(_m._14 + _m._12, _m._24 + _m._22, _m._34 + _m._32, _m._44 + _m._42), // bottom
    plane(_m._14 - _m._12, _m._24 - _m._22, _m._34 - _m._32, _m._44 - _m._42), // top
    plane(_m._13, _m._23, _m._33, _m._43),                                     // z >= 0: the far plane under the reversed depth
    plane(_m._14 - _m._13, _m._24 - _m._23, _m._34 - _m._33, _m._44 - _m._43), // w - z >= 0: the near plane
  };
}

[[nodiscard]] bool Outside(const std::array<Plane, 6>& _frustum, float _minX, float _minY, float _minZ, float _maxX, float _maxY,
                           float _maxZ) noexcept
{
  for (const Plane& plane : _frustum)
  {
    // The corner furthest along the plane's normal; the box is outside when even it is behind.
    const float x = plane.a >= 0.0f ? _maxX : _minX;
    const float y = plane.b >= 0.0f ? _maxY : _minY;
    const float z = plane.c >= 0.0f ? _maxZ : _minZ;
    if (plane.a * x + plane.b * y + plane.c * z + plane.d < 0.0f)
    {
      return true;
    }
  }
  return false;
}

[[nodiscard]] winrt::com_ptr<ID3D12Resource> UploadBuffer(ID3D12Device* _device, const void* _bytes, std::size_t _size,
                                                          const wchar_t* _name)
{
  winrt::com_ptr<ID3D12Resource> buffer;
  const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
  const CD3DX12_RESOURCE_DESC description = CD3DX12_RESOURCE_DESC::Buffer(_size);
  winrt::check_hresult(_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                        nullptr, IID_PPV_ARGS(buffer.put())));
  winrt::check_hresult(buffer->SetName(_name));
  if (_bytes != nullptr)
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

TerrainPass::TerrainPass(GraphicsDevice& _device, const HeightView& _view, const TerrainPalette& _palette, std::uint32_t _sceneSampleCount)
  : m_device(&_device)
{
  ID3D12Device* device = _device.Device();
  const std::uint32_t chunksPerSide = ChunksPerSide(_view);
  m_chunkWidth = static_cast<float>(CHUNK_STEPS) * static_cast<float>(_view.spacingWorldUnits);
  m_extent = static_cast<float>(_view.samplesPerSide - 1) * static_cast<float>(_view.spacingWorldUnits);

  // Every chunk at every stride, packed into one vertex and one index buffer. Upload heaps, read
  // by the GPU in place: the copy to a default heap is a later task's, when a chunk rebuilds.
  std::vector<TerrainVertex> vertices;
  std::vector<std::uint16_t> indices;
  m_chunks.reserve(static_cast<std::size_t>(chunksPerSide) * chunksPerSide);
  for (std::uint32_t chunkY = 0; chunkY < chunksPerSide; ++chunkY)
  {
    for (std::uint32_t chunkX = 0; chunkX < chunksPerSide; ++chunkX)
    {
      Chunk chunk{};
      for (std::size_t level = 0; level < CHUNK_STRIDES.size(); ++level)
      {
        const TerrainMesh mesh = BuildTerrainChunk(_view, chunkX, chunkY, CHUNK_STRIDES[level], _palette);
        chunk.levels[level] = {static_cast<std::uint32_t>(vertices.size()), static_cast<std::uint32_t>(indices.size()),
                               static_cast<std::uint32_t>(mesh.indices.size())};
        vertices.insert(vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
        indices.insert(indices.end(), mesh.indices.begin(), mesh.indices.end());
        if (level == 0)
        {
          chunk.minX = mesh.minX;
          chunk.minY = mesh.minY;
          chunk.minZ = mesh.minZ;
          chunk.maxX = mesh.maxX;
          chunk.maxY = mesh.maxY;
          chunk.maxZ = mesh.maxZ;
          chunk.centerX = (mesh.minX + mesh.maxX) * 0.5f;
          chunk.centerZ = (mesh.minZ + mesh.maxZ) * 0.5f;
        }
      }
      m_chunks.push_back(chunk);
    }
  }
  m_vertexBuffer = UploadBuffer(device, vertices.data(), vertices.size() * sizeof(TerrainVertex), L"terrain vertices");
  m_indexBuffer = UploadBuffer(device, indices.data(), indices.size() * sizeof(std::uint16_t), L"terrain indices");
  m_vertexView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
  m_vertexView.SizeInBytes = static_cast<UINT>(vertices.size() * sizeof(TerrainVertex));
  m_vertexView.StrideInBytes = sizeof(TerrainVertex);
  m_indexView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
  m_indexView.SizeInBytes = static_cast<UINT>(indices.size() * sizeof(std::uint16_t));
  m_indexView.Format = DXGI_FORMAT_R16_UINT;
  Log::Write(LogLevel::Info, "terrain: " + std::to_string(m_chunks.size()) + " chunks at four strides, " + std::to_string(vertices.size()) +
                               " vertices, " + std::to_string(indices.size() / 3) + " triangles built");

  // The constants: one slot per frame in flight, mapped for the life of the pass.
  m_constants = UploadBuffer(device, nullptr, SCENE_CONSTANTS_STRIDE * FRAMES_IN_FLIGHT, L"scene constants");
  void* mapped = nullptr;
  const D3D12_RANGE nothing{0, 0};
  winrt::check_hresult(m_constants->Map(0, &nothing, &mapped));
  m_constantsMapped = static_cast<std::uint8_t*>(mapped);

  // b0 as a root descriptor, and the input assembler's layout.
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
      Log::Write(LogLevel::Error, std::string("terrain: root signature: ") + static_cast<const char*>(errors->GetBufferPointer()));
    }
    winrt::throw_hresult(result);
  }
  winrt::check_hresult(
    device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(m_rootSignature.put())));

  const D3D12_INPUT_ELEMENT_DESC layout[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };
  // Every member set by hand: the description holds enumerations with no zero enumerator.
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline;
  pipeline.pRootSignature = m_rootSignature.get();
  pipeline.VS = CD3DX12_SHADER_BYTECODE(g_TerrainVS, sizeof g_TerrainVS);
  pipeline.PS = CD3DX12_SHADER_BYTECODE(g_TerrainPS, sizeof g_TerrainPS);
  pipeline.DS = D3D12_SHADER_BYTECODE{};
  pipeline.HS = D3D12_SHADER_BYTECODE{};
  pipeline.GS = D3D12_SHADER_BYTECODE{};
  pipeline.StreamOutput = D3D12_STREAM_OUTPUT_DESC{};
  pipeline.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  pipeline.SampleMask = UINT_MAX;
  pipeline.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  // Both faces, until the winding is confirmed on a running build: the terrain is one surface seen
  // from above, and the pixel shader takes its normal from the derivatives whichever way it winds.
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

void TerrainPass::Draw(ID3D12GraphicsCommandList* _list, const Camera& _camera, const Frame& _frame)
{
  // The constants of this frame, in this frame's slot.
  const DirectX::XMMATRIX viewProjection = _camera.View() * _camera.Projection(_frame.aspect);
  SceneConstants constants{};
  DirectX::XMStoreFloat4x4(&constants.viewProjection, DirectX::XMMatrixTranspose(viewProjection));
  const DirectX::XMFLOAT3 position = _camera.Position();
  constants.cameraPosition = {position.x, position.y, position.z, 1.0f};
  const auto normalized = [](const std::array<float, 3>& _direction)
  {
    const float length = std::sqrt(_direction[0] * _direction[0] + _direction[1] * _direction[1] + _direction[2] * _direction[2]);
    return DirectX::XMFLOAT4{_direction[0] / length, _direction[1] / length, _direction[2] / length, 0.0f};
  };
  constants.lightDirection0 = normalized(_frame.lighting.key.direction);
  constants.lightColor0 = {_frame.lighting.key.color[0], _frame.lighting.key.color[1], _frame.lighting.key.color[2], 0.0f};
  constants.lightDirection1 = normalized(_frame.lighting.sun.direction);
  constants.lightColor1 = {_frame.lighting.sun.color[0], _frame.lighting.sun.color[1], _frame.lighting.sun.color[2], 0.0f};
  constants.fog = {_frame.fogStart, _frame.fogEnd, static_cast<float>(static_cast<std::uint32_t>(_frame.fogMode)),
                   _frame.fogMaxDesaturation};
  constants.fogColor = {_frame.fogColor[0], _frame.fogColor[1], _frame.fogColor[2], 1.0f};
  const std::size_t slot = m_device->FrameIndex();
  std::memcpy(m_constantsMapped + slot * SCENE_CONSTANTS_STRIDE, &constants, sizeof constants);
  m_constantsAddress = m_constants->GetGPUVirtualAddress() + slot * SCENE_CONSTANTS_STRIDE;

  // The chunks the frustum keeps, each with the stride its distance asks for, then the budget.
  DirectX::XMFLOAT4X4 matrix;
  DirectX::XMStoreFloat4x4(&matrix, viewProjection);
  const std::array<Plane, 6> frustum = FrustumOf(matrix);
  struct Visible
  {
    std::size_t chunk;
    float distance;
    std::size_t level;
  };
  std::vector<Visible> visible;
  visible.reserve(m_chunks.size());
  for (std::size_t index = 0; index < m_chunks.size(); ++index)
  {
    const Chunk& chunk = m_chunks[index];
    if (Outside(frustum, chunk.minX, chunk.minY, chunk.minZ, chunk.maxX, chunk.maxY, chunk.maxZ))
    {
      continue;
    }
    const float dx = chunk.centerX - position.x;
    const float dz = chunk.centerZ - position.z;
    const float distance = std::sqrt(dx * dx + dz * dz);
    // The near band at full detail; each band beyond it doubles the stride.
    std::size_t level = 0;
    float band = 1.5f * m_chunkWidth;
    while (level + 1 < CHUNK_STRIDES.size() && distance > band)
    {
      ++level;
      band *= 2.0f;
    }
    visible.push_back({index, distance, level});
  }
  const auto triangles = [this](const Visible& _visible) { return m_chunks[_visible.chunk].levels[_visible.level].indexCount / 3; };
  std::uint32_t total = 0;
  for (const Visible& entry : visible)
  {
    total += triangles(entry);
  }
  std::sort(visible.begin(), visible.end(), [](const Visible& _a, const Visible& _b) { return _a.distance > _b.distance; });
  for (Visible& entry : visible)
  {
    if (total <= TERRAIN_TRIANGLE_BUDGET)
    {
      break;
    }
    // The farthest first, to the coarsest stride, until the frame fits.
    while (entry.level + 1 < CHUNK_STRIDES.size() && total > TERRAIN_TRIANGLE_BUDGET)
    {
      total -= triangles(entry);
      ++entry.level;
      total += triangles(entry);
    }
  }

  _list->SetGraphicsRootSignature(m_rootSignature.get());
  _list->SetPipelineState(m_pipeline.get());
  _list->SetGraphicsRootConstantBufferView(0, m_constantsAddress);
  _list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  _list->IASetVertexBuffers(0, 1, &m_vertexView);
  _list->IASetIndexBuffer(&m_indexView);
  for (const Visible& entry : visible)
  {
    const Level& level = m_chunks[entry.chunk].levels[entry.level];
    if (level.indexCount > 0)
    {
      _list->DrawIndexedInstanced(level.indexCount, 1, level.indexOffset, static_cast<INT>(level.vertexOffset), 0);
    }
  }
  m_lastTriangles = total;
  m_lastChunks = static_cast<std::uint32_t>(visible.size());
  Log::Write(LogLevel::Debug, "terrain: " + std::to_string(m_lastChunks) + " chunks, " + std::to_string(m_lastTriangles) + " triangles");
}

} // namespace Neuron
