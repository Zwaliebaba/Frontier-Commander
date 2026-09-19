#pragma once

#include "Device.h"
#include "Feature.h"
#include "ObjectId.h"
#include "Projectile.h"
#include "Structure.h"
#include "Wreck.h"

#include "SlotMap.h"

#include <cstdint>

// Every object in the match (TechnicalDesign.md §4.3): a slot map per kind over a std::vector,
// with stable generation-checked handles, and one creation counter shared by all five so that an
// ObjectId is unique across the match and not merely within its kind. Iteration for the hash and
// the snapshot is in ascending id, never in slot order, so that a run that reused a freed slot
// hashes the same as one that did not (AGENTS.md R16).
//
// World holds objects and nothing else: per-commander state is Seat's, indexed by seat, and the
// landscape is Landscape's.

namespace Frontier
{

class StateHash;

class World
{
public:
  /// Issues the next id and stores the record. The record's own id is the slot map's key, so it
  /// is never a field that can disagree with where the record lives.
  ObjectId Create(const Device& _device);
  ObjectId Create(const Structure& _structure);
  ObjectId Create(const Projectile& _projectile);
  ObjectId Create(const Feature& _feature);
  ObjectId Create(const Wreck& _wreck);

  /// Erases what the id names; false for an id already removed, never issued, or of a kind that
  /// does not hold it. A removed id is never issued again, so it resolves to nothing for ever.
  bool Remove(ObjectId _id);

  [[nodiscard]] Device* FindDevice(ObjectId _id) noexcept;
  [[nodiscard]] const Device* FindDevice(ObjectId _id) const noexcept;
  [[nodiscard]] Structure* FindStructure(ObjectId _id) noexcept;
  [[nodiscard]] const Structure* FindStructure(ObjectId _id) const noexcept;
  [[nodiscard]] Projectile* FindProjectile(ObjectId _id) noexcept;
  [[nodiscard]] const Projectile* FindProjectile(ObjectId _id) const noexcept;
  [[nodiscard]] Feature* FindFeature(ObjectId _id) noexcept;
  [[nodiscard]] const Feature* FindFeature(ObjectId _id) const noexcept;
  [[nodiscard]] Wreck* FindWreck(ObjectId _id) noexcept;
  [[nodiscard]] const Wreck* FindWreck(ObjectId _id) const noexcept;

  /// True when the id names a live object of its kind.
  [[nodiscard]] bool Alive(ObjectId _id) const noexcept;

  /// Visits every live record of a kind in ascending id with (id, record).
  template <class Fn> void ForEachDevice(Fn&& _visit) const
  {
    Visit(m_devices, ObjectKind::Device, _visit);
  }
  template <class Fn> void ForEachStructure(Fn&& _visit) const
  {
    Visit(m_structures, ObjectKind::Structure, _visit);
  }
  template <class Fn> void ForEachProjectile(Fn&& _visit) const
  {
    Visit(m_projectiles, ObjectKind::Projectile, _visit);
  }
  template <class Fn> void ForEachFeature(Fn&& _visit) const
  {
    Visit(m_features, ObjectKind::Feature, _visit);
  }
  template <class Fn> void ForEachWreck(Fn&& _visit) const
  {
    Visit(m_wrecks, ObjectKind::Wreck, _visit);
  }

  [[nodiscard]] std::size_t Count(ObjectKind _kind) const noexcept;

  /// The id the next Create will issue. It is state: two worlds holding the same objects but
  /// differing here would issue different ids from the next tick on, so the hash covers it.
  [[nodiscard]] std::uint32_t NextId() const noexcept
  {
    return m_nextId;
  }

  /// Stores a record under an id the caller already knows, for the snapshot. False when the id is
  /// null, of the wrong kind for the record, or already live. The caller sets the counter with
  /// SetNextId once every record is back.
  [[nodiscard]] bool Restore(ObjectId _id, const Device& _device);
  [[nodiscard]] bool Restore(ObjectId _id, const Structure& _structure);
  [[nodiscard]] bool Restore(ObjectId _id, const Projectile& _projectile);
  [[nodiscard]] bool Restore(ObjectId _id, const Feature& _feature);
  [[nodiscard]] bool Restore(ObjectId _id, const Wreck& _wreck);
  void SetNextId(std::uint32_t _next) noexcept
  {
    m_nextId = _next;
  }

  /// The five maps in kind order, each in ascending id, then the counter (ADR-002).
  void AddToHash(StateHash& _hash) const noexcept;

private:
  template <class T, class Fn> static void Visit(const Neuron::SlotMap<T>& _map, ObjectKind _kind, Fn& _visit)
  {
    _map.ForEachByKey([&_visit, _kind](Neuron::SlotHandle, std::uint32_t _key, const T& _value) { _visit(ObjectId{_key, _kind}, _value); });
  }

  std::uint32_t m_nextId = 1; ///< 0 is NO_OBJECT, so the first object issued is 1
  Neuron::SlotMap<Device> m_devices;
  Neuron::SlotMap<Structure> m_structures;
  Neuron::SlotMap<Projectile> m_projectiles;
  Neuron::SlotMap<Feature> m_features;
  Neuron::SlotMap<Wreck> m_wrecks;
};

} // namespace Frontier
