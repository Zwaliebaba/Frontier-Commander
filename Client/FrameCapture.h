#pragma once

#include "WindowsHeader.h"

#include <d3d12.h>
#include <unknwn.h>
#include <winrt/base.h>

#include <cstdint>
#include <filesystem>

namespace Neuron
{

class GraphicsDevice;

/// The capture mode's eyes (TechnicalDesign.md §6.1, §10): the resolved scene target copied into
/// a readback buffer and written as a BMP through Core's BitmapWriter, which is how an agent that
/// cannot run the game sees a frame.
class FrameCapture
{
public:
  FrameCapture(GraphicsDevice& _device, std::uint32_t _width, std::uint32_t _height, DXGI_FORMAT _format);

  /// Records the copy of _source, which rests in PIXEL_SHADER_RESOURCE, into the readback buffer.
  void Record(ID3D12GraphicsCommandList* _list, ID3D12Resource* _source);
  /// After the recorded frame has completed on the GPU: the buffer read back and written as a BMP.
  /// False, with the reason logged, when the file cannot be written.
  [[nodiscard]] bool Write(const std::filesystem::path& _file);

private:
  winrt::com_ptr<ID3D12Resource> m_readback;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_footprint{};
  std::uint64_t m_bytes = 0;
  std::uint32_t m_width = 0;
  std::uint32_t m_height = 0;
};

} // namespace Neuron
