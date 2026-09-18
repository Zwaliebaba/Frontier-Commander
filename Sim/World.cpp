#include "pch.h"

#include "World.h"

#include "StateHash.h"

namespace Frontier
{

namespace
{

/// The one resolution path (TechnicalDesign.md §4.3): the id's value is the map's key, the key
/// gives a generation-checked handle, and the handle gives the record. A key that was erased is
/// not in the map, and a key is never issued twice, so a stale id resolves to nothing rather than
/// to whatever took the slot.
template <class T> [[nodiscard]] T* Find(Neuron::SlotMap<T>& _map, ObjectId _id, ObjectKind _kind) noexcept
{
  if (!_id.Valid() || _id.kind != _kind)
  {
    return nullptr;
  }
  return _map.Resolve(_map.FindByKey(_id.value));
}

template <class T> [[nodiscard]] const T* Find(const Neuron::SlotMap<T>& _map, ObjectId _id, ObjectKind _kind) noexcept
{
  if (!_id.Valid() || _id.kind != _kind)
  {
    return nullptr;
  }
  return _map.Resolve(_map.FindByKey(_id.value));
}

template <class T> [[nodiscard]] bool RestoreInto(Neuron::SlotMap<T>& _map, ObjectId _id, ObjectKind _kind, const T& _value)
{
  if (!_id.Valid() || _id.kind != _kind || _map.FindByKey(_id.value) != Neuron::NULL_SLOT_HANDLE)
  {
    return false;
  }
  _map.Insert(_id.value, _value);
  return true;
}

} // namespace

ObjectId World::Create(const Device& _device)
{
  const ObjectId id{m_nextId++, ObjectKind::Device};
  m_devices.Insert(id.value, _device);
  return id;
}

ObjectId World::Create(const Structure& _structure)
{
  const ObjectId id{m_nextId++, ObjectKind::Structure};
  m_structures.Insert(id.value, _structure);
  return id;
}

ObjectId World::Create(const Projectile& _projectile)
{
  const ObjectId id{m_nextId++, ObjectKind::Projectile};
  m_projectiles.Insert(id.value, _projectile);
  return id;
}

ObjectId World::Create(const Feature& _feature)
{
  const ObjectId id{m_nextId++, ObjectKind::Feature};
  m_features.Insert(id.value, _feature);
  return id;
}

ObjectId World::Create(const Wreck& _wreck)
{
  const ObjectId id{m_nextId++, ObjectKind::Wreck};
  m_wrecks.Insert(id.value, _wreck);
  return id;
}

bool World::Remove(ObjectId _id)
{
  if (!_id.Valid())
  {
    return false;
  }
  switch (_id.kind)
  {
  case ObjectKind::Device:
    return m_devices.Erase(m_devices.FindByKey(_id.value));
  case ObjectKind::Structure:
    return m_structures.Erase(m_structures.FindByKey(_id.value));
  case ObjectKind::Projectile:
    return m_projectiles.Erase(m_projectiles.FindByKey(_id.value));
  case ObjectKind::Feature:
    return m_features.Erase(m_features.FindByKey(_id.value));
  case ObjectKind::Wreck:
    return m_wrecks.Erase(m_wrecks.FindByKey(_id.value));
  }
  return false;
}

Device* World::FindDevice(ObjectId _id) noexcept
{
  return Find(m_devices, _id, ObjectKind::Device);
}

const Device* World::FindDevice(ObjectId _id) const noexcept
{
  return Find(m_devices, _id, ObjectKind::Device);
}

Structure* World::FindStructure(ObjectId _id) noexcept
{
  return Find(m_structures, _id, ObjectKind::Structure);
}

const Structure* World::FindStructure(ObjectId _id) const noexcept
{
  return Find(m_structures, _id, ObjectKind::Structure);
}

Projectile* World::FindProjectile(ObjectId _id) noexcept
{
  return Find(m_projectiles, _id, ObjectKind::Projectile);
}

const Projectile* World::FindProjectile(ObjectId _id) const noexcept
{
  return Find(m_projectiles, _id, ObjectKind::Projectile);
}

Feature* World::FindFeature(ObjectId _id) noexcept
{
  return Find(m_features, _id, ObjectKind::Feature);
}

const Feature* World::FindFeature(ObjectId _id) const noexcept
{
  return Find(m_features, _id, ObjectKind::Feature);
}

Wreck* World::FindWreck(ObjectId _id) noexcept
{
  return Find(m_wrecks, _id, ObjectKind::Wreck);
}

const Wreck* World::FindWreck(ObjectId _id) const noexcept
{
  return Find(m_wrecks, _id, ObjectKind::Wreck);
}

bool World::Alive(ObjectId _id) const noexcept
{
  if (!_id.Valid())
  {
    return false;
  }
  switch (_id.kind)
  {
  case ObjectKind::Device:
    return FindDevice(_id) != nullptr;
  case ObjectKind::Structure:
    return FindStructure(_id) != nullptr;
  case ObjectKind::Projectile:
    return FindProjectile(_id) != nullptr;
  case ObjectKind::Feature:
    return FindFeature(_id) != nullptr;
  case ObjectKind::Wreck:
    return FindWreck(_id) != nullptr;
  }
  return false;
}

std::size_t World::Count(ObjectKind _kind) const noexcept
{
  switch (_kind)
  {
  case ObjectKind::Device:
    return m_devices.Size();
  case ObjectKind::Structure:
    return m_structures.Size();
  case ObjectKind::Projectile:
    return m_projectiles.Size();
  case ObjectKind::Feature:
    return m_features.Size();
  case ObjectKind::Wreck:
    return m_wrecks.Size();
  }
  return 0;
}

bool World::Restore(ObjectId _id, const Device& _device)
{
  return RestoreInto(m_devices, _id, ObjectKind::Device, _device);
}

bool World::Restore(ObjectId _id, const Structure& _structure)
{
  return RestoreInto(m_structures, _id, ObjectKind::Structure, _structure);
}

bool World::Restore(ObjectId _id, const Projectile& _projectile)
{
  return RestoreInto(m_projectiles, _id, ObjectKind::Projectile, _projectile);
}

bool World::Restore(ObjectId _id, const Feature& _feature)
{
  return RestoreInto(m_features, _id, ObjectKind::Feature, _feature);
}

bool World::Restore(ObjectId _id, const Wreck& _wreck)
{
  return RestoreInto(m_wrecks, _id, ObjectKind::Wreck, _wreck);
}

void World::AddToHash(StateHash& _hash) const noexcept
{
  _hash.Add(static_cast<std::uint32_t>(m_devices.Size()));
  ForEachDevice([&_hash](ObjectId _id, const Device& _device) { _hash.AddDevice(_id, _device); });
  _hash.Add(static_cast<std::uint32_t>(m_structures.Size()));
  ForEachStructure([&_hash](ObjectId _id, const Structure& _structure) { _hash.AddStructure(_id, _structure); });
  _hash.Add(static_cast<std::uint32_t>(m_projectiles.Size()));
  ForEachProjectile([&_hash](ObjectId _id, const Projectile& _projectile) { _hash.AddProjectile(_id, _projectile); });
  _hash.Add(static_cast<std::uint32_t>(m_features.Size()));
  ForEachFeature([&_hash](ObjectId _id, const Feature& _feature) { _hash.AddFeature(_id, _feature); });
  _hash.Add(static_cast<std::uint32_t>(m_wrecks.Size()));
  ForEachWreck([&_hash](ObjectId _id, const Wreck& _wreck) { _hash.AddWreck(_id, _wreck); });
  _hash.Add(m_nextId);
}

} // namespace Frontier
