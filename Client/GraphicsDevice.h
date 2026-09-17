#pragma once

#include "WindowsHeader.h"

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <unknwn.h>
#include <winrt/base.h>

#include <array>
#include <cstdint>
#include <string>

namespace Neuron
{

/// Frames the CPU may record while the GPU still works on earlier ones (TechnicalDesign.md §6.1).
inline constexpr std::uint32_t FRAMES_IN_FLIGHT = 3;

/// The Direct3D 12 device, its one direct queue, a command allocator per frame in flight with the
/// fence value that frees it again, and the debug layer's queue drained into the log (ADR-004).
/// A failed HRESULT throws winrt::hresult_error through winrt::check_hresult, and the frame loop
/// catches it once and stops (ADR-004): nothing here reports a failure by return value.
class GraphicsDevice
{
public:
  /// On WARP when _warp, else on the first hardware adapter that supports feature level 11.0. In
  /// Debug the debug layer is on when the SDK layers are installed, and the log says when not.
  explicit GraphicsDevice(bool _warp);
  ~GraphicsDevice();
  GraphicsDevice(const GraphicsDevice&) = delete;
  GraphicsDevice& operator=(const GraphicsDevice&) = delete;

  [[nodiscard]] ID3D12Device* Device() const noexcept
  {
    return m_device.get();
  }
  [[nodiscard]] IDXGIFactory4* Factory() const noexcept
  {
    return m_factory.get();
  }
  [[nodiscard]] ID3D12CommandQueue* Queue() const noexcept
  {
    return m_queue.get();
  }
  [[nodiscard]] bool IsWarp() const noexcept
  {
    return m_warp;
  }
  [[nodiscard]] const std::string& AdapterDescription() const noexcept
  {
    return m_adapterDescription;
  }

  /// Waits until the GPU has finished the frame that last used this slot's allocator, resets the
  /// allocator and opens the command list on it; the list records until EndFrame.
  [[nodiscard]] ID3D12GraphicsCommandList* BeginFrame();
  /// Closes and executes the list, then signals the value BeginFrame waits for three frames on.
  void EndFrame();
  /// Blocks until every frame submitted so far has completed on the GPU.
  void WaitForIdle();
  [[nodiscard]] std::uint32_t FrameIndex() const noexcept
  {
    return m_frameIndex;
  }
  [[nodiscard]] std::uint64_t FramesBegun() const noexcept
  {
    return m_framesBegun;
  }

  /// Moves every message the debug layer stored since the last call into the log; warnings and
  /// worse are counted, and the count is what --capture exits non-zero on. Nothing without the layer.
  void DrainDebugMessages();
  [[nodiscard]] std::uint32_t DebugMessageCount() const noexcept
  {
    return m_debugMessages;
  }
  [[nodiscard]] bool DebugLayerActive() const noexcept
  {
    return static_cast<bool>(m_infoQueue);
  }

private:
  void WaitForFence(std::uint64_t _value);

  winrt::com_ptr<IDXGIFactory4> m_factory;
  winrt::com_ptr<ID3D12Device> m_device;
  winrt::com_ptr<ID3D12InfoQueue> m_infoQueue;
  winrt::com_ptr<ID3D12CommandQueue> m_queue;
  std::array<winrt::com_ptr<ID3D12CommandAllocator>, FRAMES_IN_FLIGHT> m_allocators;
  winrt::com_ptr<ID3D12GraphicsCommandList> m_commandList;
  winrt::com_ptr<ID3D12Fence> m_fence;
  winrt::handle m_fenceEvent;
  std::array<std::uint64_t, FRAMES_IN_FLIGHT> m_frameFenceValues{};
  std::uint64_t m_nextFenceValue = 1;
  std::uint64_t m_framesBegun = 0;
  std::string m_adapterDescription;
  std::uint32_t m_frameIndex = 0;
  std::uint32_t m_debugMessages = 0;
  bool m_warp = false;
  bool m_recording = false;
};

} // namespace Neuron
