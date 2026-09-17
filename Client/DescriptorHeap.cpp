#include "pch.h"

#include "DescriptorHeap.h"

namespace Neuron
{

DescriptorHeap::DescriptorHeap(ID3D12Device* _device, D3D12_DESCRIPTOR_HEAP_TYPE _type, std::uint32_t _capacity, bool _shaderVisible)
  : m_capacity(_capacity)
{
  D3D12_DESCRIPTOR_HEAP_DESC description{};
  description.Type = _type;
  description.NumDescriptors = _capacity;
  description.Flags = _shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  winrt::check_hresult(_device->CreateDescriptorHeap(&description, IID_PPV_ARGS(m_heap.put())));
  m_increment = _device->GetDescriptorHandleIncrementSize(_type);
  m_cpuStart = m_heap->GetCPUDescriptorHandleForHeapStart();
  if (_shaderVisible)
  {
    m_gpuStart = m_heap->GetGPUDescriptorHandleForHeapStart();
  }
}

std::uint32_t DescriptorHeap::Allocate()
{
  FRONTIER_ASSERT(m_used < m_capacity);
  const std::uint32_t index = m_used;
  ++m_used;
  return index;
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeap::Cpu(std::uint32_t _index) const noexcept
{
  FRONTIER_ASSERT(_index < m_capacity);
  D3D12_CPU_DESCRIPTOR_HANDLE handle = m_cpuStart;
  handle.ptr += static_cast<SIZE_T>(_index) * m_increment;
  return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeap::Gpu(std::uint32_t _index) const noexcept
{
  FRONTIER_ASSERT(_index < m_capacity);
  D3D12_GPU_DESCRIPTOR_HANDLE handle = m_gpuStart;
  handle.ptr += static_cast<UINT64>(_index) * m_increment;
  return handle;
}

} // namespace Neuron
