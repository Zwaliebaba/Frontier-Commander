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
  // Tearing has to be asked for when the chain is created, not when it is presented, so it is
  // asked for whenever the machine allows it and --novsync decides per frame whether to use it.
  // The query lives on IDXGIFactory5 and the device holds an IDXGIFactory4, so it is asked for
  // here; a machine whose DXGI is older simply answers no, which is the honest answer anyway.
  winrt::com_ptr<IDXGIFactory5> factory5;
  if (SUCCEEDED(_device.Factory()->QueryInterface(IID_PPV_ARGS(factory5.put()))))
  {
    BOOL allowTearing = FALSE;
    if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing))))
    {
      m_tearingSupported = allowTearing != FALSE;
    }
  }
  if (m_tearingSupported)
  {
    description.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
  }
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
  // The same flag the chain was created with, or ResizeBuffers refuses it.
  const UINT flags = m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;
  winrt::check_hresult(m_swapChain->ResizeBuffers(BUFFER_COUNT, _width, _height, FORMAT, flags));
  m_width = _width;
  m_height = _height;
  m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
  CreateViews(_device.Device());
}

void SwapChain::Present(std::uint32_t _syncInterval)
{
  // DXGI allows the tearing flag only with a sync interval of zero, and only on a chain that was
  // created with it. Asking for it anywhere else is an error rather than a hint that is ignored.
  const UINT flags = _syncInterval == 0 && m_tearingSupported ? DXGI_PRESENT_ALLOW_TEARING : 0u;
  winrt::check_hresult(m_swapChain->Present(_syncInterval, flags));
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
