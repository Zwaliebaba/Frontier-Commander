#pragma once

#include "Device.h"
#include "Feature.h"
#include "ObjectId.h"
#include "Order.h"
#include "Projectile.h"
#include "Seat.h"
#include "Structure.h"
#include "Wreck.h"

#include "Hash.h"

#include <cstdint>
#include <span>
#include <type_traits>

namespace Frontier
{

/// The state hash of TechnicalDesign.md §4.9: FNV-1a over the simulation's fields, each fed as
/// little-endian bytes so that the digest is the same on every machine, in one fixed order that
/// Sim::HashState (stage 13) writes down once. What a determinism test compares and a replay is
/// checked against.
class StateHash
{
public:
  template <class T>
    requires (std::is_integral_v<T> && !std::is_same_v<T, bool>) || std::is_enum_v<T>
  void Add(T _value) noexcept
  {
    m_hash = Neuron::HashInteger(m_hash, _value);
  }

  void AddBool(bool _value) noexcept
  {
    Add(static_cast<std::uint8_t>(_value ? 1 : 0));
  }

  template <class T>
    requires (std::is_integral_v<T> && !std::is_same_v<T, bool>) || std::is_enum_v<T>
  void AddSpan(std::span<const T> _values) noexcept
  {
    m_hash = Neuron::HashIntegers(m_hash, _values);
  }

  void AddSeat(const Seat& _seat) noexcept;
  void AddOrder(const Order& _order) noexcept;

  /// Each record with the id it is stored under, because two worlds holding the same records at
  /// different ids are different states: the ids are on the wire and in every later reference.
  void AddObjectId(ObjectId _id) noexcept;
  void AddDevice(ObjectId _id, const Device& _device) noexcept;
  void AddStructure(ObjectId _id, const Structure& _structure) noexcept;
  void AddProjectile(ObjectId _id, const Projectile& _projectile) noexcept;
  void AddFeature(ObjectId _id, const Feature& _feature) noexcept;
  void AddWreck(ObjectId _id, const Wreck& _wreck) noexcept;

  [[nodiscard]] std::uint64_t Value() const noexcept
  {
    return m_hash;
  }

private:
  std::uint64_t m_hash = Neuron::FNV1A64_OFFSET;
};

} // namespace Frontier
