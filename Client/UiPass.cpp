#include "pch.h"

#include "UiPass.h"

#include "Log.h"
#include "ScaleMode.h"
#include "SceneTarget.h"

// Written by the shader compiler on every build (AGENTS.md §2); nothing else includes them.
#include "CompiledShaders/UiPS.h"
#include "CompiledShaders/UiVS.h"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstring>
#include <string>

namespace Neuron
{

namespace
{

/// Both atlases are decoded to RGBA8 before they reach the pass (BitmapFont, IconAtlas), so one
/// format serves both and the upload is a copy rather than a conversion.
constexpr DXGI_FORMAT ATLAS_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr std::uint32_t ATLAS_BYTES_PER_PIXEL = 4;

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

/// Creates the texture, its upload buffer and the view, and copies the decoded pixels into the
/// upload buffer row by row at the pitch the device asks for - which is 256-byte aligned and is not
/// the atlas's own row length in general, even though both of M1's atlases happen to be 1024 wide.
void PrepareAtlas(ID3D12Device* _device, std::span<const std::uint8_t> _pixels, std::uint32_t _width, std::uint32_t _height,
                  const wchar_t* _name, winrt::com_ptr<ID3D12Resource>& _outTexture, winrt::com_ptr<ID3D12Resource>& _outUpload,
                  D3D12_PLACED_SUBRESOURCE_FOOTPRINT& _outFootprint, D3D12_CPU_DESCRIPTOR_HANDLE _view)
{
  const CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
  const CD3DX12_RESOURCE_DESC description = CD3DX12_RESOURCE_DESC::Tex2D(ATLAS_FORMAT, _width, _height, 1, 1, 1, 0);
  winrt::check_hresult(_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_COPY_DEST,
                                                        nullptr, IID_PPV_ARGS(_outTexture.put())));
  winrt::check_hresult(_outTexture->SetName(_name));

  std::uint64_t totalBytes = 0;
  _device->GetCopyableFootprints(&description, 0, 1, 0, &_outFootprint, nullptr, nullptr, &totalBytes);
  _outUpload = UploadBuffer(_device, static_cast<std::size_t>(totalBytes), _name);

  void* mapped = nullptr;
  const D3D12_RANGE nothing{0, 0};
  winrt::check_hresult(_outUpload->Map(0, &nothing, &mapped));
  auto* destination = static_cast<std::uint8_t*>(mapped);
  const std::size_t sourcePitch = static_cast<std::size_t>(_width) * ATLAS_BYTES_PER_PIXEL;
  for (std::uint32_t row = 0; row < _height; ++row)
  {
    std::memcpy(destination + _outFootprint.Offset + static_cast<std::size_t>(row) * _outFootprint.Footprint.RowPitch,
                _pixels.data() + static_cast<std::size_t>(row) * sourcePitch, sourcePitch);
  }
  _outUpload->Unmap(0, nullptr);

  D3D12_SHADER_RESOURCE_VIEW_DESC view{};
  view.Format = ATLAS_FORMAT;
  view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  view.Texture2D.MipLevels = 1;
  _device->CreateShaderResourceView(_outTexture.get(), &view, _view);
}

} // namespace

UiPass::UiPass(GraphicsDevice& _device, const SceneTarget& _scene, const BitmapFont& _font, const IconAtlas& _icons)
  : m_device(&_device),
    m_views(_device.Device(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true)
{
  ID3D12Device* device = _device.Device();

  // b0 the two root constants the vertex shader divides by, t0 the font and t1 the icons in one
  // table, s0 the point sampler (UiVS.hlsl, UiPS.hlsl).
  const CD3DX12_DESCRIPTOR_RANGE atlasRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0);
  CD3DX12_ROOT_PARAMETER parameters[2];
  parameters[0].InitAsConstants(2, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
  parameters[1].InitAsDescriptorTable(1, &atlasRange, D3D12_SHADER_VISIBILITY_PIXEL);
  CD3DX12_STATIC_SAMPLER_DESC sampler;
  sampler.Init(0, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
               D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
  sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  const CD3DX12_ROOT_SIGNATURE_DESC description(2, parameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
  winrt::com_ptr<ID3DBlob> serialized;
  winrt::com_ptr<ID3DBlob> errors;
  const HRESULT result = D3D12SerializeRootSignature(&description, D3D_ROOT_SIGNATURE_VERSION_1, serialized.put(), errors.put());
  if (FAILED(result))
  {
    if (errors)
    {
      Log::Write(LogLevel::Error, std::string("ui pass: root signature: ") + static_cast<const char*>(errors->GetBufferPointer()));
    }
    winrt::throw_hresult(result);
  }
  winrt::check_hresult(
    device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(m_rootSignature.put())));

  const D3D12_INPUT_ELEMENT_DESC elements[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(UiVertex, x), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(UiVertex, u), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(UiVertex, color), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXKIND", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(UiVertex, kind), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };

  // Every member set by hand rather than from {}, for the reason PresentPass.cpp gives.
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline;
  pipeline.pRootSignature = m_rootSignature.get();
  pipeline.VS = CD3DX12_SHADER_BYTECODE(g_UiVS, sizeof g_UiVS);
  pipeline.PS = CD3DX12_SHADER_BYTECODE(g_UiPS, sizeof g_UiPS);
  pipeline.DS = D3D12_SHADER_BYTECODE{};
  pipeline.HS = D3D12_SHADER_BYTECODE{};
  pipeline.GS = D3D12_SHADER_BYTECODE{};
  pipeline.StreamOutput = D3D12_STREAM_OUTPUT_DESC{};
  pipeline.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  // STRAIGHT ALPHA OVER WHAT IS ALREADY THERE. panelFill is (24, 10, 12, 245) and a glyph's
  // coverage is its atlas alpha, so both the palette's transparency and the font's edges come
  // through the same one blend. The alpha channel is left alone: the scene target's alpha is not
  // read by anything downstream, and blending it would only make the value meaningless twice.
  pipeline.BlendState.RenderTarget[0].BlendEnable = TRUE;
  pipeline.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
  pipeline.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
  pipeline.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
  pipeline.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
  pipeline.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
  pipeline.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
  pipeline.SampleMask = UINT_MAX;
  pipeline.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pipeline.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
  pipeline.DepthStencilState.DepthEnable = FALSE;
  pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  pipeline.DepthStencilState.StencilEnable = FALSE;
  pipeline.InputLayout = D3D12_INPUT_LAYOUT_DESC{elements, static_cast<UINT>(std::size(elements))};
  pipeline.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
  pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pipeline.NumRenderTargets = 1;
  std::fill(std::begin(pipeline.RTVFormats), std::end(pipeline.RTVFormats), DXGI_FORMAT_UNKNOWN);
  pipeline.RTVFormats[0] = SCENE_COLOR_FORMAT;
  // No depth is bound for this pass, so the state names no format; the scene's sample count is the
  // target's and a pipeline that disagreed with it would not draw.
  pipeline.DSVFormat = DXGI_FORMAT_UNKNOWN;
  pipeline.SampleDesc = DXGI_SAMPLE_DESC{_scene.SampleCount(), 0};
  pipeline.NodeMask = 0;
  pipeline.CachedPSO = D3D12_CACHED_PIPELINE_STATE{};
  pipeline.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
  winrt::check_hresult(device->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(m_pipeline.put())));

  m_fontWidth = _font.AtlasWidth();
  m_fontHeight = _font.AtlasHeight();
  m_iconWidth = _icons.Width();
  m_iconHeight = _icons.Height();
  // A sheet that did not load leaves a 1x1 opaque white texel, so that the descriptor table is
  // complete and the shader reads something defined. BuildUiVertices drops the quads that would
  // have used it, which is what stops that texel reaching the frame as a white square.
  static constexpr std::uint8_t WHITE_TEXEL[ATLAS_BYTES_PER_PIXEL] = {255, 255, 255, 255};
  const std::span<const std::uint8_t> fontPixels = _font.Loaded() ? _font.Pixels() : std::span<const std::uint8_t>(WHITE_TEXEL);
  const std::span<const std::uint8_t> iconPixels = _icons.Loaded() ? _icons.Pixels() : std::span<const std::uint8_t>(WHITE_TEXEL);
  PrepareAtlas(device, fontPixels, _font.Loaded() ? m_fontWidth : 1u, _font.Loaded() ? m_fontHeight : 1u, L"ui font atlas", m_fontTexture,
               m_fontUpload, m_fontFootprint, m_views.Cpu(m_views.Allocate()));
  PrepareAtlas(device, iconPixels, _icons.Loaded() ? m_iconWidth : 1u, _icons.Loaded() ? m_iconHeight : 1u, L"ui icon sheet", m_iconTexture,
               m_iconUpload, m_iconFootprint, m_views.Cpu(m_views.Allocate()));
  if (!_font.Loaded())
  {
    Log::Write(LogLevel::Warning, "ui: the font atlas did not load; no text is drawn");
  }
  if (!_icons.Loaded())
  {
    Log::Write(LogLevel::Warning, "ui: the icon sheet did not load; no icon is drawn");
  }

  const std::size_t vertexBytes = static_cast<std::size_t>(MAX_UI_QUADS) * VERTICES_PER_QUAD * sizeof(UiVertex);
  for (std::uint32_t slot = 0; slot < FRAMES_IN_FLIGHT; ++slot)
  {
    m_vertices[slot] = UploadBuffer(device, vertexBytes, L"ui vertices");
    void* mapped = nullptr;
    const D3D12_RANGE nothing{0, 0};
    winrt::check_hresult(m_vertices[slot]->Map(0, &nothing, &mapped));
    m_verticesMapped[slot] = static_cast<std::uint8_t*>(mapped);
  }
  m_staging.reserve(static_cast<std::size_t>(MAX_UI_QUADS) * VERTICES_PER_QUAD);
}

void UiPass::UploadAtlases(ID3D12GraphicsCommandList* _list)
{
  const auto copy = [_list](ID3D12Resource* _texture, ID3D12Resource* _upload, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& _footprint)
  {
    const CD3DX12_TEXTURE_COPY_LOCATION source(_upload, _footprint);
    const CD3DX12_TEXTURE_COPY_LOCATION destination(_texture, 0);
    _list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    const CD3DX12_RESOURCE_BARRIER toRead =
      CD3DX12_RESOURCE_BARRIER::Transition(_texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    _list->ResourceBarrier(1, &toRead);
  };
  copy(m_fontTexture.get(), m_fontUpload.get(), m_fontFootprint);
  copy(m_iconTexture.get(), m_iconUpload.get(), m_iconFootprint);
  m_atlasesUploaded = true;
}

void UiPass::Draw(ID3D12GraphicsCommandList* _list, const SceneTarget& _scene, std::span<const UiQuad> _quads)
{
  if (!m_atlasesUploaded)
  {
    UploadAtlases(_list);
  }
  std::span<const UiQuad> quads = _quads;
  if (quads.size() > MAX_UI_QUADS)
  {
    Log::Write(LogLevel::Warning, "ui: " + std::to_string(quads.size()) + " quads in one frame against a buffer of " +
                                    std::to_string(MAX_UI_QUADS) + "; the rest are not drawn");
    quads = quads.first(MAX_UI_QUADS);
  }
  BuildUiVertices(quads, m_fontWidth, m_fontHeight, m_iconWidth, m_iconHeight, m_staging);
  m_lastQuadsDrawn = static_cast<std::uint32_t>(m_staging.size() / VERTICES_PER_QUAD);
  if (m_staging.empty())
  {
    return;
  }

  const std::uint32_t slot = m_device->FrameIndex();
  const std::size_t bytes = m_staging.size() * sizeof(UiVertex);
  std::memcpy(m_verticesMapped[slot], m_staging.data(), bytes);

  // The depth buffer is unbound rather than transitioned: this pass neither writes nor reads it,
  // and a pipeline state with no DSV format may not have one bound. SceneTarget's contract is that
  // whoever unbinds it binds it again, which is the last line of this function.
  _scene.BindColorOnly(_list);
  const CD3DX12_VIEWPORT viewport(0.0f, 0.0f, static_cast<float>(AUTHORED_WIDTH_PIXELS), static_cast<float>(AUTHORED_HEIGHT_PIXELS));
  const CD3DX12_RECT scissor(0, 0, static_cast<LONG>(AUTHORED_WIDTH_PIXELS), static_cast<LONG>(AUTHORED_HEIGHT_PIXELS));
  _list->RSSetViewports(1, &viewport);
  _list->RSSetScissorRects(1, &scissor);

  ID3D12DescriptorHeap* heaps[] = {m_views.Heap()};
  _list->SetDescriptorHeaps(1, heaps);
  _list->SetGraphicsRootSignature(m_rootSignature.get());
  _list->SetPipelineState(m_pipeline.get());
  const float authored[2] = {static_cast<float>(AUTHORED_WIDTH_PIXELS), static_cast<float>(AUTHORED_HEIGHT_PIXELS)};
  _list->SetGraphicsRoot32BitConstants(0, 2, authored, 0);
  _list->SetGraphicsRootDescriptorTable(1, m_views.Gpu(0));

  D3D12_VERTEX_BUFFER_VIEW vertexView{};
  vertexView.BufferLocation = m_vertices[slot]->GetGPUVirtualAddress();
  vertexView.SizeInBytes = static_cast<UINT>(bytes);
  vertexView.StrideInBytes = sizeof(UiVertex);
  _list->IASetVertexBuffers(0, 1, &vertexView);
  _list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  _list->DrawInstanced(static_cast<UINT>(m_staging.size()), 1, 0, 0);

  _scene.Bind(_list);
}

} // namespace Neuron
