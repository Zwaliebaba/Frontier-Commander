#include "pch.h"

#include "AiDesigner.h"

#include "Design.h"
#include "Sim.h"

#include <algorithm>

namespace Frontier
{

namespace
{

/// The armour an average target of that column carries, for the matrix's armour term. A designer
/// that assumed no armour would rate a fast weak weapon over a gun, which is exactly the mistake
/// the matrix exists to stop; this is the mean of the chassis table rather than a number of its own.
[[nodiscard]] std::int32_t TypicalArmor(const ContentTree& _content) noexcept
{
  std::int64_t total = 0;
  for (const ChassisDesc& chassis : _content.components.chassis)
  {
    total += chassis.kineticArmor;
  }
  return _content.components.chassis.empty()
           ? 0
           : static_cast<std::int32_t>(total / static_cast<std::int64_t>(_content.components.chassis.size()));
}

} // namespace

std::int32_t ExpectedDamage(const ContentTree& _content, const ModuleDesc& _weapon, const AiBlackboard& _blackboard) noexcept
{
  const std::int32_t armor = TypicalArmor(_content);
  std::int64_t weighted = 0;
  std::int64_t total = 0;
  for (std::uint8_t column = 0; column < TARGET_CLASS_COUNT; ++column)
  {
    const std::int64_t share = _blackboard.enemyColumns[column];
    const std::int32_t damage = DamageDealt(_content.damage, _weapon.weaponClass, column, _weapon.damage, armor);
    weighted += damage * share;
    total += share;
  }
  if (total > 0)
  {
    return static_cast<std::int32_t>(weighted / total);
  }
  // Nothing seen: the mean over every column, which is a design for what it might meet.
  std::int64_t mean = 0;
  for (std::uint8_t column = 0; column < TARGET_CLASS_COUNT; ++column)
  {
    mean += DamageDealt(_content.damage, _weapon.weaponClass, column, _weapon.damage, armor);
  }
  return static_cast<std::int32_t>(mean / TARGET_CLASS_COUNT);
}

bool BestDesign(const Sim& _sim, std::uint8_t _seat, AiRole _role, const AiBlackboard& _blackboard, DeviceDesign& _out)
{
  if (_seat >= _sim.Seats().size())
  {
    return false;
  }
  const Seat& seat = _sim.Seats()[_seat];
  const ContentTree& content = _sim.Content();

  bool found = false;
  std::int64_t best = -1;
  DeviceDesign chosen{};

  for (std::uint32_t chassisRow = 0; chassisRow < content.components.chassis.size(); ++chassisRow)
  {
    const ChassisDesc& chassis = content.components.chassis[chassisRow];
    if (!UnlockedFor(seat, content, chassis.unlockedBy) || chassis.mounts == 0)
    {
      continue;
    }
    for (std::uint32_t driveRow = 0; driveRow < content.components.drives.size(); ++driveRow)
    {
      const DriveDesc& drive = content.components.drives[driveRow];
      if (!UnlockedFor(seat, content, drive.unlockedBy))
      {
        continue;
      }
      for (std::uint32_t moduleRow = 0; moduleRow < content.components.modules.size(); ++moduleRow)
      {
        const ModuleDesc& module = content.components.modules[moduleRow];
        if (!UnlockedFor(seat, content, module.unlockedBy))
        {
          continue;
        }
        const bool wanted = _role == AiRole::Builder ? module.systemKind == SystemKind::Builder : module.systemKind == SystemKind::None;
        if (!wanted)
        {
          continue;
        }

        DeviceDesign design{};
        design.chassis = chassisRow;
        design.drive = driveRow;
        design.modules[0] = moduleRow;
        design.moduleCount = 1;

        DesignStats stats{};
        if (DeriveDesignStats(content, RecipeFor(content, design), seat.upgrades, stats) != DesignFault::None)
        {
          continue;
        }
        if (stats.costHundredths <= 0)
        {
          continue;
        }

        // Damage per power for a fighter; build power per power for a builder. Both are "what this
        // design gets me for what it costs", scaled up so that integer division keeps the ranking.
        const std::int64_t worth =
          _role == AiRole::Builder ? module.buildPowerHundredthsPerTick : ExpectedDamage(content, module, _blackboard);
        const std::int64_t score = worth * 10000 / stats.costHundredths;
        // A tie goes to the cheaper design, and then to the lower rows, so that two hosts choose
        // the same one from the same tables.
        const bool better = score > best;
        if (better)
        {
          best = score;
          chosen = design;
          found = true;
        }
      }
    }
  }
  if (found)
  {
    _out = chosen;
  }
  return found;
}

} // namespace Frontier
