#include "pch.h"

#include "FrameCapture.h"

#include "BitmapWriter.h"
#include "GraphicsDevice.h"
#include "Log.h"

#include <cstring>
#include <fstream>
#include <vector>

namespace Neuron
{

FrameCapture::FrameCapture(GraphicsDevice& _device, std::uint32_t _width, std::uint32_t _height, DXGI_FORMAT _format)
  : m_width(_width),
    m_height(_height)
{
  // The footprint the copy lays the rows out in: each row padded to D3D12_TEXTURE_DATA_PITCH_ALIGNMENT.
  const CD3DX12_RESOURCE_DESC texture = CD3DX12_RESOURCE_DESC::Tex2D(_format, _width, _height, 1, 1);
  UINT rows = 0;
  UINT64 rowBytes = 0;
  _device.Device()->GetCopyableFootprints(&texture, 0, 1, 0, &m_footprint, &rows, &rowBytes, &m_bytes);
  const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_READBACK);
  const CD3DX12_RESOURCE_DESC buffer = CD3DX12_RESOURCE_DESC::Buffer(m_bytes);
  winrt::check_hresult(_device.Device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST,
                                                                 nullptr, IID_PPV_ARGS(m_readback.put())));
  winrt::check_hresult(m_readback->SetName(L"frame capture readback"));
}

void FrameCapture::Record(ID3D12GraphicsCommandList* _list, ID3D12Resource* _source)
{
  const CD3DX12_RESOURCE_BARRIER toSource =
    CD3DX12_RESOURCE_BARRIER::Transition(_source, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
  _list->ResourceBarrier(1, &toSource);
  const CD3DX12_TEXTURE_COPY_LOCATION destination(m_readback.get(), m_footprint);
  const CD3DX12_TEXTURE_COPY_LOCATION source(_source, 0);
  _list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
  const CD3DX12_RESOURCE_BARRIER back =
    CD3DX12_RESOURCE_BARRIER::Transition(_source, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  _list->ResourceBarrier(1, &back);
}

bool FrameCapture::Write(const std::filesystem::path& _file)
{
  const D3D12_RANGE readRange{0, static_cast<SIZE_T>(m_bytes)};
  void* mapped = nullptr;
  winrt::check_hresult(m_readback->Map(0, &readRange, &mapped));
  const std::size_t rowBytes = static_cast<std::size_t>(m_width) * 4;
  std::vector<std::uint8_t> rgba(rowBytes * m_height);
  const auto* rows = static_cast<const std::uint8_t*>(mapped) + m_footprint.Offset;
  for (std::uint32_t row = 0; row < m_height; ++row)
  {
    std::memcpy(rgba.data() + static_cast<std::size_t>(row) * rowBytes,
                rows + static_cast<std::size_t>(row) * m_footprint.Footprint.RowPitch, rowBytes);
  }
  const D3D12_RANGE written{0, 0};
  m_readback->Unmap(0, &written);

  const std::vector<std::byte> bitmap = WriteBitmap(m_width, m_height, rgba);
  std::ofstream file(_file, std::ios::binary);
  file.write(reinterpret_cast<const char*>(bitmap.data()), static_cast<std::streamsize>(bitmap.size()));
  if (!file)
  {
    Log::Write(LogLevel::Error, "capture: cannot write " + _file.string());
    return false;
  }
  return true;
}

} // namespace Neuron
