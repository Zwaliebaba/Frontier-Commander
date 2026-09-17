#include "pch.h"

#include "GraphicsDevice.h"

#include "Log.h"

#include <cstdio>
#include <string>
#include <vector>

namespace Neuron
{

namespace
{

[[nodiscard]] std::string HexOf(HRESULT _code)
{
  char buffer[16];
  std::snprintf(buffer, sizeof buffer, "0x%08X", static_cast<unsigned>(_code));
  return buffer;
}

[[nodiscard]] winrt::com_ptr<IDXGIAdapter1> ChooseAdapter(IDXGIFactory4* _factory, bool _warp)
{
  winrt::com_ptr<IDXGIAdapter1> adapter;
  if (_warp)
  {
    winrt::check_hresult(_factory->EnumWarpAdapter(IID_PPV_ARGS(adapter.put())));
    return adapter;
  }
  // The first adapter is the primary display's; a software adapter is never chosen by accident,
  // and a device is only created on one that admits feature level 11.0.
  for (UINT index = 0; _factory->EnumAdapters1(index, adapter.put()) != DXGI_ERROR_NOT_FOUND; ++index)
  {
    DXGI_ADAPTER_DESC1 description{};
    winrt::check_hresult(adapter->GetDesc1(&description));
    const bool software = (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
    if (!software && SUCCEEDED(D3D12CreateDevice(adapter.get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)))
    {
      return adapter;
    }
    adapter = nullptr;
  }
  Log::Write(LogLevel::Error, "device: no hardware adapter supports Direct3D 12 feature level 11.0");
  winrt::throw_hresult(DXGI_ERROR_NOT_FOUND);
}

[[nodiscard]] LogLevel LevelOf(D3D12_MESSAGE_SEVERITY _severity) noexcept
{
  switch (_severity)
  {
  case D3D12_MESSAGE_SEVERITY_CORRUPTION:
  case D3D12_MESSAGE_SEVERITY_ERROR:
    return LogLevel::Error;
  case D3D12_MESSAGE_SEVERITY_WARNING:
    return LogLevel::Warning;
  case D3D12_MESSAGE_SEVERITY_INFO:
  case D3D12_MESSAGE_SEVERITY_MESSAGE:
    return LogLevel::Debug;
  }
  return LogLevel::Debug;
}

} // namespace

GraphicsDevice::GraphicsDevice(bool _warp)
  : m_warp(_warp)
{
  UINT factoryFlags = 0;
#if defined(_DEBUG)
  // The debug layer needs the SDK layers (the Graphics Tools feature); without them the call fails
  // with DXGI_ERROR_SDK_COMPONENT_MISSING and the device runs unvalidated, which the log records so
  // that a clean capture on such a machine is not mistaken for a validated one.
  winrt::com_ptr<ID3D12Debug> debug;
  const HRESULT layer = D3D12GetDebugInterface(IID_PPV_ARGS(debug.put()));
  if (SUCCEEDED(layer))
  {
    debug->EnableDebugLayer();
    factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
  }
  else
  {
    Log::Write(LogLevel::Warning, "device: debug layer unavailable (" + HexOf(layer) + "); running without validation");
  }
#endif
  winrt::check_hresult(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(m_factory.put())));
  const winrt::com_ptr<IDXGIAdapter1> adapter = ChooseAdapter(m_factory.get(), _warp);
  DXGI_ADAPTER_DESC1 description{};
  winrt::check_hresult(adapter->GetDesc1(&description));
  m_adapterDescription = winrt::to_string(description.Description);
  winrt::check_hresult(D3D12CreateDevice(adapter.get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(m_device.put())));
  m_infoQueue = m_device.try_as<ID3D12InfoQueue>();
  Log::Write(LogLevel::Info, "device: " + m_adapterDescription + (m_warp ? " (WARP)" : "") +
                               (DebugLayerActive() ? ", debug layer on" : ", debug layer off"));

  D3D12_COMMAND_QUEUE_DESC queueDescription{};
  queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  winrt::check_hresult(m_device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(m_queue.put())));
  for (auto& allocator : m_allocators)
  {
    winrt::check_hresult(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.put())));
  }
  winrt::check_hresult(
    m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocators[0].get(), nullptr, IID_PPV_ARGS(m_commandList.put())));
  // Created open; closed here so that BeginFrame reopens it the same way every frame.
  winrt::check_hresult(m_commandList->Close());
  winrt::check_hresult(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.put())));
  m_fenceEvent.attach(CreateEventW(nullptr, FALSE, FALSE, nullptr));
  winrt::check_bool(static_cast<bool>(m_fenceEvent));
}

GraphicsDevice::~GraphicsDevice()
{
  try
  {
    WaitForIdle();
  }
  catch (const winrt::hresult_error&)
  {
    Log::Write(LogLevel::Warning, "device: the queue did not drain before the device was released");
  }
}

ID3D12GraphicsCommandList* GraphicsDevice::BeginFrame()
{
  FRONTIER_ASSERT(!m_recording);
  WaitForFence(m_frameFenceValues[m_frameIndex]);
  ID3D12CommandAllocator* allocator = m_allocators[m_frameIndex].get();
  winrt::check_hresult(allocator->Reset());
  winrt::check_hresult(m_commandList->Reset(allocator, nullptr));
  m_recording = true;
  ++m_framesBegun;
  return m_commandList.get();
}

void GraphicsDevice::EndFrame()
{
  FRONTIER_ASSERT(m_recording);
  winrt::check_hresult(m_commandList->Close());
  ID3D12CommandList* lists[] = {m_commandList.get()};
  m_queue->ExecuteCommandLists(1, lists);
  winrt::check_hresult(m_queue->Signal(m_fence.get(), m_nextFenceValue));
  m_frameFenceValues[m_frameIndex] = m_nextFenceValue;
  ++m_nextFenceValue;
  m_frameIndex = (m_frameIndex + 1) % FRAMES_IN_FLIGHT;
  m_recording = false;
}

void GraphicsDevice::WaitForIdle()
{
  const std::uint64_t value = m_nextFenceValue;
  ++m_nextFenceValue;
  winrt::check_hresult(m_queue->Signal(m_fence.get(), value));
  WaitForFence(value);
}

void GraphicsDevice::WaitForFence(std::uint64_t _value)
{
  // Zero is a slot never submitted, and a completed value needs no event.
  if (_value == 0 || m_fence->GetCompletedValue() >= _value)
  {
    return;
  }
  winrt::check_hresult(m_fence->SetEventOnCompletion(_value, m_fenceEvent.get()));
  WaitForSingleObjectEx(m_fenceEvent.get(), INFINITE, FALSE);
}

void GraphicsDevice::DrainDebugMessages()
{
  if (!DebugLayerActive())
  {
    return;
  }
  const UINT64 count = m_infoQueue->GetNumStoredMessages();
  std::vector<std::byte> storage;
  for (UINT64 index = 0; index < count; ++index)
  {
    SIZE_T length = 0;
    if (FAILED(m_infoQueue->GetMessage(index, nullptr, &length)) || length == 0)
    {
      continue;
    }
    storage.resize(length);
    auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
    if (FAILED(m_infoQueue->GetMessage(index, message, &length)))
    {
      continue;
    }
    const LogLevel level = LevelOf(message->Severity);
    if (level != LogLevel::Debug)
    {
      ++m_debugMessages;
    }
    std::string text(message->pDescription, message->DescriptionByteLength);
    while (!text.empty() && text.back() == '\0')
    {
      text.pop_back();
    }
    Log::Write(level, "d3d12: " + text);
  }
  m_infoQueue->ClearStoredMessages();
}

} // namespace Neuron
