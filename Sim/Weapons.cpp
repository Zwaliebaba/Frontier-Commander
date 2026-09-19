#include "pch.h"

#include "Weapons.h"
#include "Damage.h"
#include "Design.h"
#include "Placement.h"
#include "Plan.h"
#include "Sim.h"
#include "Targeting.h"

#include <algorithm>
#include <vector>

namespace Frontier
{

namespace
{

/// Every enemy of the shot's seat inside the splash takes it. Not "everything": GameDesign.md §8
/// never names friendly fire, and an area weapon that hurts its own side is a mechanic a design
/// states rather than one a task infers from the word "everything" in an acceptance line, which is
/// there to say the splash hits what STANDS there rather than what was aimed at. The one line that
/// would change it is here, so the owner can have it the other way by deleting a condition.
void Splash(Sim& _sim, const Projectile& _shot)
{
  if (_shot.seat >= _sim.Seats().size())
  {
    return;
  }
  const ContentTree& content = _sim.Content();
  if (_shot.module >= content.components.modules.size())
  {
    return;
  }
  const ModuleDesc& row = content.components.modules[_shot.module];
  const std::uint8_t alliance = _sim.Seats()[_shot.seat].alliance;
  const auto radius = static_cast<std::int64_t>(row.splashSubunits);

  // Ascending by id, and the ids are collected before anything is rolled for, so that two hosts
  // draw for the same objects in the same order.
  std::vector<ObjectId> caught;
  _sim.Objects().ForEachDevice(
    [&caught, &_sim, alliance, radius, &_shot](ObjectId _id, const Device& _device)
    {
      if (_device.seat < _sim.Seats().size() && _sim.Seats()[_device.seat].alliance != alliance &&
          Neuron::LengthSquared(_device.x - _shot.impactX, _device.z - _shot.impactZ) <= radius * radius)
      {
        caught.push_back(_id);
      }
    });
  _sim.Objects().ForEachStructure(
    [&caught, &_sim, alliance, radius, &_shot, &content](ObjectId _id, const Structure& _structure)
    {
      if (_structure.seat >= _sim.Seats().size() || _sim.Seats()[_structure.seat].alliance == alliance || !Occupies(_structure.state))
      {
        return;
      }
      const Footprint footprint = FootprintOf(_structure, &content);
      const std::int32_t x =
        static_cast<std::int32_t>(footprint.cellX * Neuron::SUBUNITS_PER_CELL + footprint.cellsX * Neuron::SUBUNITS_PER_CELL / 2);
      const std::int32_t z =
        static_cast<std::int32_t>(footprint.cellY * Neuron::SUBUNITS_PER_CELL + footprint.cellsY * Neuron::SUBUNITS_PER_CELL / 2);
      if (Neuron::LengthSquared(x - _shot.impactX, z - _shot.impactZ) <= radius * radius)
      {
        caught.push_back(_id);
      }
    });

  for (const ObjectId id : caught)
  {
    TargetPoint target{};
    if (!TargetAt(_sim, id, target))
    {
      continue;
    }
    // The roll is at the impact and per object caught, which is what "decides at impact against
    // whatever is in the splash" means: the shell is already committed and what it finds there is
    // the question. The shipped mortar is 100 at both ranges, so for M1 this always connects.
    if (static_cast<std::int32_t>(_sim.Roll(100)) >= _shot.hitPercent)
    {
      continue;
    }
    const std::int32_t armor = ArmorAgainst(content.damage, row.weaponClass, target.armor);
    const std::int32_t points = DamageDealt(content.damage, row.weaponClass, target.armor.column, _shot.damage, armor);
    _sim.Damage().push_back({id, _shot.shooter, _shot.seat, points});
  }
}

} // namespace

std::uint8_t WeaponsOf(const ContentTree& _content, const DeviceDesign& _design, std::array<WeaponMount, MAX_MOUNTS>& _out)
{
  std::uint8_t found = 0;
  const std::uint8_t mounts = std::min<std::uint8_t>(_design.moduleCount, static_cast<std::uint8_t>(MAX_MOUNTS));
  for (std::uint8_t mount = 0; mount < mounts; ++mount)
  {
    const std::uint32_t row = _design.modules[mount];
    if (row >= _content.components.modules.size() || _content.components.modules[row].systemKind != SystemKind::None)
    {
      continue; // A system module takes a mount and fires nothing.
    }
    _out[found] = {row, mount};
    ++found;
  }
  return found;
}

bool WeaponOf(const ContentTree& _content, const Structure& _structure, WeaponMount& _out)
{
  if (_structure.design >= _content.structures.structures.size())
  {
    return false;
  }
  const std::string& weapon = _content.structures.structures[_structure.design].weapon;
  if (weapon.empty())
  {
    return false;
  }
  // By id rather than by row index, because a structure row names its weapon the way the tables do
  // and ContentValidator is what refuses one that names a module the components do not define.
  for (std::size_t index = 0; index < _content.components.modules.size(); ++index)
  {
    if (_content.components.modules[index].id == weapon && _content.components.modules[index].systemKind == SystemKind::None)
    {
      _out = {static_cast<std::uint32_t>(index), 0};
      return true;
    }
  }
  return false;
}

std::int32_t HitPercent(const ModuleDesc& _module, const ClassUpgrades& _upgrades, std::uint8_t _rank, bool _longRange) noexcept
{
  const auto weapon = static_cast<std::size_t>(_module.weaponClass);
  const std::int32_t base = _longRange ? _module.longHitPercent : _module.shortHitPercent;
  const std::int32_t rank = _rank < RANK_COUNT ? RANK_ACCURACY_PERCENT[_rank] : 0;
  const std::int32_t added = weapon < WEAPON_CLASS_COUNT ? _upgrades.weaponAccuracyPercent[weapon] : 0;
  return std::clamp(base + rank + added, 0, 100);
}

std::int32_t WeaponDamage(const ModuleDesc& _module, const ClassUpgrades& _upgrades, std::uint8_t _rank) noexcept
{
  const auto weapon = static_cast<std::size_t>(_module.weaponClass);
  const std::int32_t rank = _rank < RANK_COUNT ? RANK_DAMAGE_PERCENT[_rank] : 0;
  const std::int32_t added = weapon < WEAPON_CLASS_COUNT ? _upgrades.weaponDamagePercent[weapon] : 0;
  return std::max(0, Neuron::MulDiv(_module.damage, 100 + rank + added, 100));
}

std::uint32_t ReloadTicks(const ModuleDesc& _module, const ClassUpgrades& _upgrades) noexcept
{
  const auto weapon = static_cast<std::size_t>(_module.weaponClass);
  const std::int32_t added = weapon < WEAPON_CLASS_COUNT ? _upgrades.weaponRatePercent[weapon] : 0;
  // Rate up is reload down, and never to nought: a weapon that reloads in no time fires every tick
  // for ever, which no percentage a research tree can reach is meant to buy.
  const std::int32_t percent = std::max(1, 100 + added);
  const auto ticks = static_cast<std::int32_t>(std::min<std::uint32_t>(_module.reloadTicks, 0x7FFFFFFFu));
  return static_cast<std::uint32_t>(std::max(1, Neuron::MulDiv(ticks, 100, percent)));
}

void AdvanceProjectiles(Sim& _sim)
{
  World& world = _sim.Objects();
  std::vector<ObjectId> flying;
  world.ForEachProjectile([&flying](ObjectId _id, const Projectile&) { flying.push_back(_id); });

  for (const ObjectId id : flying)
  {
    Projectile* shot = world.FindProjectile(id);
    if (shot == nullptr)
    {
      continue;
    }
    if (shot->ticksToImpact > 1)
    {
      // Toward the impact by one tick's share of what is left, so that a client interpolating the
      // shell is following the simulation rather than guessing at an arc.
      const auto left = static_cast<std::int32_t>(shot->ticksToImpact);
      shot->x += (shot->impactX - shot->x) / left;
      shot->y += (shot->impactY - shot->y) / left;
      shot->z += (shot->impactZ - shot->z) / left;
      --shot->ticksToImpact;
      continue;
    }
    // It lands. Removed first, so that the splash cannot find the shell that made it.
    const Projectile landed = *shot;
    (void)world.Remove(id);
    Splash(_sim, landed);
  }
}

} // namespace Frontier
