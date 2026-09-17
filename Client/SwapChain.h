#pragma once

#include "WindowsHeader.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <unknwn.h>
#include <winrt/base.h>

#include "DescriptorHeap.h"

#include <array>
#include <cstdint>

namespace Neuron
{

class GraphicsDevice;

/// A flip-model swap chain of three back buffers on the window (TechnicalDesign.md §6.1), each
/// with a render-target view. The back buffer is never multisampled, which the API forbids
/// (AGENTS.md §5); the scene target is, and the present pass copies it in.
class SwapChain
{
public:
  static constexpr std::uint32_t BUFFER_COUNT = 3;
  static constexpr DXGI_FORMAT FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;

  SwapChain(GraphicsDevice& _device, HWND _window, std::uint32_t _width, std::uint32_t _height);

  /// Recreates the buffers at the new client size, after the GPU has let go of the old ones.
  void Resize(GraphicsDevice& _device, std::uint32_t _width, std::uint32_t _height);
  /// Presents with vertical synchronisation and moves to the next back buffer.
  void Present();

  [[nodiscard]] ID3D12Resource* CurrentBackBuffer() const noexcept
  {
    return m_buffers[m_backBufferIndex].get();
  }
  [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE CurrentRenderTargetView() const noexcept
  {
    return m_views.Cpu(m_backBufferIndex);
  }
  [[nodiscard]] std::uint32_t Width() const noexcept
  {
    return m_width;
  }
  [[nodiscard]] std::uint32_t Height() const noexcept
  {
    return m_height;
  }

private:
  void CreateViews(ID3D12Device* _device);

  winrt::com_ptr<IDXGISwapChain3> m_swapChain;
  std::array<winrt::com_ptr<ID3D12Resource>, BUFFER_COUNT> m_buffers;
  DescriptorHeap m_views;
  std::uint32_t m_width = 0;
  std::uint32_t m_height = 0;
  std::uint32_t m_backBufferIndex = 0;
};

} // namespace Neuron
