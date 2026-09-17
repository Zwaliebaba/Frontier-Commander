#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "Assert.h"

// Storage per object kind (TechnicalDesign.md §4.3): a slot map over a std::vector (AGENTS.md R15)
// whose handles carry a generation, so that a handle to an erased element resolves to nothing
// rather than to whatever occupies the slot later. Iteration order is slot order, a pure function
// of the sequence of inserts and erases, so two runs of one match iterate identically; the
// caller's creation counter is kept per element as its key, and an ascending-key traversal and a
// key lookup exist for the state hash, the snapshot and the object-id resolution that must not
// depend on slot reuse. T is default-constructible and movable: a plain simulation record.

namespace Neuron
{

struct SlotHandle
{
  std::uint32_t index;
  std::uint32_t generation;

  [[nodiscard]] constexpr bool operator==(const SlotHandle&) const noexcept = default;
};

inline constexpr SlotHandle NULL_SLOT_HANDLE = {0xFFFFFFFFu, 0};

template <class T> class SlotMap
{
public:
  /// A generation never returns to 0, so NULL_SLOT_HANDLE matches no live slot even after a wrap.
  [[nodiscard]] static constexpr std::uint32_t NextGeneration(std::uint32_t _generation) noexcept
  {
    return _generation == 0xFFFFFFFFu ? 1u : _generation + 1u;
  }

  /// Stores _value under the caller's _key, reusing the most recently freed slot first.
  SlotHandle Insert(std::uint32_t _key, T _value)
  {
    std::uint32_t index;
    if (!m_free.empty())
    {
      index = m_free.back();
      m_free.pop_back();
      Slot& slot = m_slots[index];
      slot.live = true;
      slot.key = _key;
      slot.value = std::move(_value);
    }
    else
    {
      index = static_cast<std::uint32_t>(m_slots.size());
      m_slots.push_back(Slot{1u, _key, true, std::move(_value)});
    }
    const auto position = std::lower_bound(m_byKey.begin(), m_byKey.end(), _key,
                                           [](const KeyEntry& _entry, std::uint32_t _wanted) { return _entry.key < _wanted; });
    FRONTIER_ASSERT(position == m_byKey.end() || position->key != _key);
    m_byKey.insert(position, KeyEntry{_key, index});
    ++m_size;
    return SlotHandle{index, m_slots[index].generation};
  }

  /// Erases the element the handle names; false, and nothing else, for a stale or null handle.
  bool Erase(SlotHandle _handle) noexcept
  {
    Slot* slot = SlotOf(_handle);
    if (slot == nullptr)
    {
      return false;
    }
    const auto position = std::lower_bound(m_byKey.begin(), m_byKey.end(), slot->key,
                                           [](const KeyEntry& _entry, std::uint32_t _wanted) { return _entry.key < _wanted; });
    FRONTIER_ASSERT(position != m_byKey.end() && position->key == slot->key);
    m_byKey.erase(position);
    slot->live = false;
    slot->generation = NextGeneration(slot->generation);
    slot->value = T{};
    m_free.push_back(_handle.index);
    --m_size;
    return true;
  }

  [[nodiscard]] T* Resolve(SlotHandle _handle) noexcept
  {
    Slot* slot = SlotOf(_handle);
    return slot == nullptr ? nullptr : &slot->value;
  }

  [[nodiscard]] const T* Resolve(SlotHandle _handle) const noexcept
  {
    const Slot* slot = SlotOf(_handle);
    return slot == nullptr ? nullptr : &slot->value;
  }

  /// The handle of the live element stored under _key, or NULL_SLOT_HANDLE.
  [[nodiscard]] SlotHandle FindByKey(std::uint32_t _key) const noexcept
  {
    const auto position = std::lower_bound(m_byKey.begin(), m_byKey.end(), _key,
                                           [](const KeyEntry& _entry, std::uint32_t _wanted) { return _entry.key < _wanted; });
    if (position == m_byKey.end() || position->key != _key)
    {
      return NULL_SLOT_HANDLE;
    }
    return SlotHandle{position->index, m_slots[position->index].generation};
  }

  [[nodiscard]] std::uint32_t KeyOf(SlotHandle _handle) const noexcept
  {
    const Slot* slot = SlotOf(_handle);
    FRONTIER_ASSERT(slot != nullptr);
    return slot == nullptr ? 0u : slot->key;
  }

  [[nodiscard]] std::size_t Size() const noexcept
  {
    return m_size;
  }
  [[nodiscard]] bool Empty() const noexcept
  {
    return m_size == 0;
  }

  /// Visits every live element in slot order with (handle, key, value).
  template <class Fn> void ForEach(Fn&& _visit)
  {
    for (std::uint32_t index = 0; index < m_slots.size(); ++index)
    {
      Slot& slot = m_slots[index];
      if (slot.live)
      {
        _visit(SlotHandle{index, slot.generation}, slot.key, slot.value);
      }
    }
  }

  template <class Fn> void ForEach(Fn&& _visit) const
  {
    for (std::uint32_t index = 0; index < m_slots.size(); ++index)
    {
      const Slot& slot = m_slots[index];
      if (slot.live)
      {
        _visit(SlotHandle{index, slot.generation}, slot.key, slot.value);
      }
    }
  }

  /// Visits every live element in ascending key order with (handle, key, value): the order the
  /// state hash and the snapshot use, independent of slot reuse.
  template <class Fn> void ForEachByKey(Fn&& _visit) const
  {
    for (const KeyEntry& entry : m_byKey)
    {
      const Slot& slot = m_slots[entry.index];
      _visit(SlotHandle{entry.index, slot.generation}, entry.key, slot.value);
    }
  }

  template <class Fn> void ForEachByKey(Fn&& _visit)
  {
    for (const KeyEntry& entry : m_byKey)
    {
      Slot& slot = m_slots[entry.index];
      _visit(SlotHandle{entry.index, slot.generation}, entry.key, slot.value);
    }
  }

private:
  struct Slot
  {
    std::uint32_t generation;
    std::uint32_t key;
    bool live;
    T value;
  };

  struct KeyEntry
  {
    std::uint32_t key;
    std::uint32_t index;
  };

  [[nodiscard]] Slot* SlotOf(SlotHandle _handle) noexcept
  {
    if (_handle.index >= m_slots.size())
    {
      return nullptr;
    }
    Slot& slot = m_slots[_handle.index];
    return slot.live && slot.generation == _handle.generation ? &slot : nullptr;
  }

  [[nodiscard]] const Slot* SlotOf(SlotHandle _handle) const noexcept
  {
    if (_handle.index >= m_slots.size())
    {
      return nullptr;
    }
    const Slot& slot = m_slots[_handle.index];
    return slot.live && slot.generation == _handle.generation ? &slot : nullptr;
  }

  std::vector<Slot> m_slots;
  std::vector<std::uint32_t> m_free;
  std::vector<KeyEntry> m_byKey;
  std::size_t m_size = 0;
};

} // namespace Neuron
