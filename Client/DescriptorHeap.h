#pragma once

#include "WindowsHeader.h"

#include <d3d12.h>
#include <unknwn.h>
#include <winrt/base.h>

#include <cstdint>

namespace Neuron
{

/// One descriptor heap managed by hand (TechnicalDesign.md §6.1): a fixed capacity handed out
/// in order and never returned, which is all seven fixed pipelines need. A shader-visible heap
/// also answers with GPU handles.
class DescriptorHeap
{
public:
  DescriptorHeap(ID3D12Device* _device, D3D12_DESCRIPTOR_HEAP_TYPE _type, std::uint32_t _capacity, bool _shaderVisible);

  /// The next free index; asserts when the heap is full.
  [[nodiscard]] std::uint32_t Allocate();
  [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE Cpu(std::uint32_t _index) const noexcept;
  [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE Gpu(std::uint32_t _index) const noexcept;
  [[nodiscard]] ID3D12DescriptorHeap* Heap() const noexcept
  {
    return m_heap.get();
  }
  [[nodiscard]] std::uint32_t Capacity() const noexcept
  {
    return m_capacity;
  }

private:
  winrt::com_ptr<ID3D12DescriptorHeap> m_heap;
  D3D12_CPU_DESCRIPTOR_HANDLE m_cpuStart{};
  D3D12_GPU_DESCRIPTOR_HANDLE m_gpuStart{};
  std::uint32_t m_increment = 0;
  std::uint32_t m_capacity = 0;
  std::uint32_t m_used = 0;
};

} // namespace Neuron
