#include "pch.h"

#include "SwapChain.h"

#include "GraphicsDevice.h"

namespace Neuron
{

SwapChain::SwapChain(GraphicsDevice& _device, HWND _window, std::uint32_t _width, std::uint32_t _height)
  : m_views(_device.Device(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, BUFFER_COUNT, false),
    m_width(_width),
    m_height(_height)
{
  DXGI_SWAP_CHAIN_DESC1 description{};
  description.Width = _width;
  description.Height = _height;
  description.Format = FORMAT;
  description.SampleDesc.Count = 1;
  description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  description.BufferCount = BUFFER_COUNT;
  description.Scaling = DXGI_SCALING_NONE;
  description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  description.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
  winrt::com_ptr<IDXGISwapChain1> created;
  winrt::check_hresult(_device.Factory()->CreateSwapChainForHwnd(_device.Queue(), _window, &description, nullptr, nullptr, created.put()));
  // Alt+Enter is the game's like Escape and Alt+F4 (ADR-004): DXGI's own full-screen toggle would
  // fight a window that already covers the monitor.
  winrt::check_hresult(_device.Factory()->MakeWindowAssociation(_window, DXGI_MWA_NO_ALT_ENTER));
  m_swapChain = created.as<IDXGISwapChain3>();
  m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
  CreateViews(_device.Device());
}

void SwapChain::Resize(GraphicsDevice& _device, std::uint32_t _width, std::uint32_t _height)
{
  if (_width == 0 || _height == 0 || (_width == m_width && _height == m_height))
  {
    return;
  }
  _device.WaitForIdle();
  for (auto& buffer : m_buffers)
  {
    buffer = nullptr;
  }
  winrt::check_hresult(m_swapChain->ResizeBuffers(BUFFER_COUNT, _width, _height, FORMAT, 0));
  m_width = _width;
  m_height = _height;
  m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
  CreateViews(_device.Device());
}

void SwapChain::Present()
{
  winrt::check_hresult(m_swapChain->Present(1, 0));
  m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void SwapChain::CreateViews(ID3D12Device* _device)
{
  for (std::uint32_t index = 0; index < BUFFER_COUNT; ++index)
  {
    winrt::check_hresult(m_swapChain->GetBuffer(index, IID_PPV_ARGS(m_buffers[index].put())));
    _device->CreateRenderTargetView(m_buffers[index].get(), nullptr, m_views.Cpu(index));
  }
}

} // namespace Neuron
