#include "pch.h"

#include "Research.h"

#include "Construction.h"
#include "Design.h"
#include "Economy.h"
#include "Production.h"
#include "Sim.h"

#include <algorithm>
#include <vector>

namespace Frontier
{

namespace
{

[[nodiscard]] const StructureDesc* RowOf(const ContentTree& _content, const Structure& _structure) noexcept
{
  return _structure.design < _content.structures.structures.size() ? &_content.structures.structures[_structure.design] : nullptr;
}

[[nodiscard]] bool Complete(const Seat& _seat, std::uint32_t _item) noexcept
{
  return std::find(_seat.researchComplete.begin(), _seat.researchComplete.end(), _item) != _seat.researchComplete.end();
}

[[nodiscard]] bool InALab(const Seat& _seat, std::uint32_t _item) noexcept
{
  return std::any_of(_seat.researchActive.begin(), _seat.researchActive.end(),
                     [_item](const ResearchProgress& _progress) { return _progress.item == _item; });
}

/// The seat's progress in a lab, or nullptr.
[[nodiscard]] ResearchProgress* ProgressIn(Seat& _seat, ObjectId _lab) noexcept
{
  for (ResearchProgress& progress : _seat.researchActive)
  {
    if (progress.lab == _lab)
    {
      return &progress;
    }
  }
  return nullptr;
}

/// Whether a structure is a standing research lab of that seat.
[[nodiscard]] bool IsLab(const ContentTree& _content, const Structure& _structure, std::uint8_t _seat) noexcept
{
  const StructureDesc* row = RowOf(_content, _structure);
  return _structure.seat == _seat && _structure.state == StructurePhase::Standing && row != nullptr &&
         row->role == StructureRole::ResearchLab;
}

/// The maximum hit points of a device, as the seat's upgrades make them now.
[[nodiscard]] std::int32_t MaxHitPointsOf(const Seat& _seat, const ContentTree& _content, const Device& _device)
{
  DesignStats stats{};
  return DeriveSeatDesign(_seat, _content, _device.design, stats) == DesignFault::None ? stats.hitPoints : 0;
}

/// The maximum hit points of a structure, as the seat's upgrades make them now.
[[nodiscard]] std::int32_t MaxHitPointsOf(const Seat& _seat, const ContentTree& _content, const Structure& _structure)
{
  const StructureDesc* row = RowOf(_content, _structure);
  if (row == nullptr)
  {
    return 0;
  }
  return static_cast<std::int32_t>(static_cast<std::int64_t>(row->hitPoints) * (100 + _seat.upgrades.structureHitPointPercent) / 100);
}

/// What an upgrade adds to, or nullptr for an effect that adds to nothing here.
[[nodiscard]] std::int32_t* UpgradeSlot(ClassUpgrades& _upgrades, const ResearchItemDesc& _item) noexcept
{
  const std::size_t target = _item.targetClass;
  switch (_item.effect)
  {
  case ResearchEffect::Unlock:
    return nullptr;
  case ResearchEffect::ChassisArmor:
    return target < CHASSIS_CLASS_COUNT ? &_upgrades.chassisArmorPercent[target] : nullptr;
  case ResearchEffect::ChassisHitPoints:
    return target < CHASSIS_CLASS_COUNT ? &_upgrades.chassisHitPointPercent[target] : nullptr;
  case ResearchEffect::WeaponDamage:
    return target < WEAPON_CLASS_COUNT ? &_upgrades.weaponDamagePercent[target] : nullptr;
  case ResearchEffect::WeaponRate:
    return target < WEAPON_CLASS_COUNT ? &_upgrades.weaponRatePercent[target] : nullptr;
  case ResearchEffect::WeaponAccuracy:
    return target < WEAPON_CLASS_COUNT ? &_upgrades.weaponAccuracyPercent[target] : nullptr;
  case ResearchEffect::ExtractorRate:
    return &_upgrades.extractorRatePercent;
  case ResearchEffect::StructureHitPoints:
    return &_upgrades.structureHitPointPercent;
  }
  return nullptr;
}

} // namespace

bool PrerequisitesComplete(const Seat& _seat, const ContentTree& _content, std::uint32_t _item)
{
  if (_item >= _content.research.size())
  {
    return false;
  }
  for (const std::string& prerequisite : _content.research[_item].prerequisites)
  {
    if (prerequisite.empty())
    {
      continue;
    }
    const auto row = std::find_if(_content.research.begin(), _content.research.end(),
                                  [&prerequisite](const ResearchItemDesc& _row) { return _row.id == prerequisite; });
    if (row == _content.research.end())
    {
      return false; // An item whose prerequisite no row defines is unreachable, not free.
    }
    if (!Complete(_seat, static_cast<std::uint32_t>(row - _content.research.begin())))
    {
      return false;
    }
  }
  return true;
}

bool Available(const Seat& _seat, const ContentTree& _content, std::uint32_t _item)
{
  return _item < _content.research.size() && !Complete(_seat, _item) && !InALab(_seat, _item) &&
         PrerequisitesComplete(_seat, _content, _item);
}

std::uint32_t CheapestAvailable(const Seat& _seat, const ContentTree& _content)
{
  std::uint32_t cheapest = NO_RESEARCH_ITEM;
  std::int32_t best = 0;
  for (std::uint32_t item = 0; item < _content.research.size(); ++item)
  {
    if (!Available(_seat, _content, item))
    {
      continue;
    }
    // Strictly cheaper, so a tie keeps the lower row index and two hosts pick the same item.
    if (cheapest == NO_RESEARCH_ITEM || _content.research[item].costHundredths < best)
    {
      cheapest = item;
      best = _content.research[item].costHundredths;
    }
  }
  return cheapest;
}

bool SetResearch(Sim& _sim, std::uint8_t _seat, ObjectId _lab, std::uint32_t _item)
{
  Seat& seat = _sim.SeatAt(_seat);
  const Structure* lab = _sim.Objects().FindStructure(_lab);
  if (lab == nullptr || !IsLab(_sim.Content(), *lab, _seat))
  {
    return false;
  }
  if (ProgressIn(seat, _lab) != nullptr || !Available(seat, _sim.Content(), _item))
  {
    return false; // One item a lab, and nothing already done or already in another lab.
  }
  const ResearchItemDesc& item = _sim.Content().research[_item];
  if (!Economy::Draw(seat, item.costHundredths))
  {
    return false;
  }
  const std::int32_t reduction = ModuleTimeReductionPercent(*lab, _sim.Content(), StructureModuleEffect::ShortenResearchTime);
  seat.researchActive.push_back({_lab, _item, ShortenedTicks(item.timeTicks, reduction)});
  return true;
}

bool CancelResearch(Sim& _sim, std::uint8_t _seat, ObjectId _lab)
{
  Seat& seat = _sim.SeatAt(_seat);
  const auto found = std::find_if(seat.researchActive.begin(), seat.researchActive.end(),
                                  [_lab](const ResearchProgress& _progress) { return _progress.lab == _lab; });
  if (found == seat.researchActive.end())
  {
    return false;
  }
  // Nothing is refunded: what was bought was the work, and the work is gone.
  seat.researchActive.erase(found);
  return true;
}

void CompleteResearch(Sim& _sim, std::uint8_t _seat, std::uint32_t _item)
{
  Seat& seat = _sim.SeatAt(_seat);
  if (_item >= _sim.Content().research.size() || Complete(seat, _item))
  {
    return;
  }
  // Ascending, because Sim/Seat.h says so and because two runs must hash alike.
  seat.researchComplete.insert(std::upper_bound(seat.researchComplete.begin(), seat.researchComplete.end(), _item), _item);

  const ResearchItemDesc& item = _sim.Content().research[_item];
  std::int32_t* slot = UpgradeSlot(seat.upgrades, item);
  if (slot == nullptr)
  {
    // An Unlock: joining researchComplete IS the unlock, because a row says what unlocks it and
    // that is the direction the simulation reads (Sim/Design.h's UnlockedFor).
    return;
  }

  // The maximums as they are, then the upgrade, then the same maximums again: the fraction of its
  // health each object had is what survives, which is what GameDesign.md §7's "retroactively"
  // has to mean for a thing that can be damaged.
  std::vector<std::pair<ObjectId, std::int32_t>> deviceMaximums;
  std::vector<std::pair<ObjectId, std::int32_t>> structureMaximums;
  const bool touchesDevices = item.effect == ResearchEffect::ChassisHitPoints;
  const bool touchesStructures = item.effect == ResearchEffect::StructureHitPoints;
  if (touchesDevices)
  {
    _sim.Objects().ForEachDevice(
      [&deviceMaximums, &seat, &_sim, _seat](ObjectId _id, const Device& _device)
      {
        if (_device.seat == _seat)
        {
          deviceMaximums.emplace_back(_id, MaxHitPointsOf(seat, _sim.Content(), _device));
        }
      });
  }
  if (touchesStructures)
  {
    _sim.Objects().ForEachStructure(
      [&structureMaximums, &seat, &_sim, _seat](ObjectId _id, const Structure& _structure)
      {
        if (_structure.seat == _seat)
        {
          structureMaximums.emplace_back(_id, MaxHitPointsOf(seat, _sim.Content(), _structure));
        }
      });
  }

  *slot += item.upgradePercent;

  for (const auto& [id, before] : deviceMaximums)
  {
    Device* device = _sim.Objects().FindDevice(id);
    if (device == nullptr || before <= 0)
    {
      continue;
    }
    const std::int32_t after = MaxHitPointsOf(seat, _sim.Content(), *device);
    device->hitPoints = static_cast<std::int32_t>(std::max<std::int64_t>(1, static_cast<std::int64_t>(device->hitPoints) * after / before));
  }
  for (const auto& [id, before] : structureMaximums)
  {
    Structure* structure = _sim.Objects().FindStructure(id);
    if (structure == nullptr || before <= 0)
    {
      continue;
    }
    const std::int32_t after = MaxHitPointsOf(seat, _sim.Content(), *structure);
    structure->hitPoints =
      static_cast<std::int32_t>(std::max<std::int64_t>(1, static_cast<std::int64_t>(structure->hitPoints) * after / before));
  }
}

void AdvanceResearch(Sim& _sim)
{
  const ContentTree& content = _sim.Content();
  for (std::uint8_t index = 0; index < _sim.Seats().size(); ++index)
  {
    Seat& seat = _sim.SeatAt(index);

    // A lab that is gone takes its progress with it, and its cost with that: research is bought
    // when it begins, and a destroyed lab refunds nothing.
    std::erase_if(seat.researchActive,
                  [&_sim, &content, index](const ResearchProgress& _progress)
                  {
                    const Structure* lab = _sim.Objects().FindStructure(_progress.lab);
                    return lab == nullptr || !IsLab(content, *lab, index);
                  });

    // Auto-research fills the idle labs at the start of the tick, cheapest first (the lobby's
    // option, Sim/MatchSettings.h). Labs in ascending id, so two hosts fill them in one order.
    if (_sim.Settings().seats[index].autoResearch)
    {
      std::vector<ObjectId> idle;
      _sim.Objects().ForEachStructure(
        [&idle, &seat, &content, index](ObjectId _id, const Structure& _structure)
        {
          if (IsLab(content, _structure, index) && ProgressIn(seat, _id) == nullptr)
          {
            idle.push_back(_id);
          }
        });
      for (const ObjectId lab : idle)
      {
        const std::uint32_t item = CheapestAvailable(seat, content);
        if (item == NO_RESEARCH_ITEM || !SetResearch(_sim, index, lab, item))
        {
          break; // Nothing left to research, or nothing left to pay for it with.
        }
      }
    }

    // The tick. Completions are collected and applied after, because completing one walks the
    // world and rescales hit points, and the list being walked must not move under it.
    std::vector<std::uint32_t> finished;
    for (ResearchProgress& progress : seat.researchActive)
    {
      if (progress.remainingTicks > 0)
      {
        --progress.remainingTicks;
      }
      if (progress.remainingTicks == 0)
      {
        finished.push_back(progress.item);
      }
    }
    std::erase_if(seat.researchActive, [](const ResearchProgress& _progress) { return _progress.remainingTicks == 0; });
    for (const std::uint32_t item : finished)
    {
      CompleteResearch(_sim, index, item);
    }
  }
}

} // namespace Frontier
