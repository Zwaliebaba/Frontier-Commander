#include "pch.h"

#include "DesignStats.h"

#include "FixedPoint.h"

namespace Frontier
{
namespace
{

/// _value x _factorHundredths / 100, rounded half up, widened so that the product cannot overflow.
/// One rounding per derived statistic, at the end of its chain, is what keeps 80 x 1.3 at 104.
[[nodiscard]] std::int32_t ApplyPercent(std::int64_t _value, std::int64_t _factorHundredths) noexcept
{
  const std::int64_t product = _value * _factorHundredths;
  const std::int64_t rounded = product >= 0 ? product + 50 : product - 50;
  return static_cast<std::int32_t>(rounded / 100);
}

} // namespace

std::uint32_t BuildTimeTicksFor(std::int32_t _costHundredths) noexcept
{
  // Ten power a second (GameDesign.md §6): the cost in hundredths over a thousand hundredths a
  // second, in ticks, rounded half up so that no design builds instantly.
  const std::int64_t ticks = (static_cast<std::int64_t>(_costHundredths) * Neuron::TICKS_PER_SECOND + 500) / 1000;
  return static_cast<std::uint32_t>(ticks < 1 ? 1 : ticks);
}

std::int32_t WorldUnitsPerSecond(std::int32_t _subunitsPerTick) noexcept
{
  const std::int64_t perSecond = static_cast<std::int64_t>(_subunitsPerTick) * Neuron::TICKS_PER_SECOND;
  const std::int64_t half = Neuron::SUBUNITS_PER_WORLD_UNIT / 2;
  return static_cast<std::int32_t>((perSecond + (perSecond >= 0 ? half : -half)) / Neuron::SUBUNITS_PER_WORLD_UNIT);
}

DesignFault DeriveDesignStats(const ContentTree& _tree, const DesignRecipe& _design, const ClassUpgrades& _upgrades, DesignStats& _out)
{
  const ChassisDesc* chassis = _tree.FindChassis(_design.chassis);
  if (chassis == nullptr)
  {
    return DesignFault::UnknownChassis;
  }
  const DriveDesc* drive = _tree.FindDrive(_design.drive);
  if (drive == nullptr)
  {
    return DesignFault::UnknownDrive;
  }
  if (_design.modules.empty())
  {
    return DesignFault::NoModules;
  }
  if (_design.modules.size() > chassis->mounts)
  {
    return DesignFault::TooManyModules;
  }

  std::int32_t weightPenaltyPercent = 0;
  std::int32_t moduleCostHundredths = 0;
  std::int32_t moduleSightSubunits = 0;
  for (const std::string& id : _design.modules)
  {
    const ModuleDesc* module = _tree.FindModule(id);
    if (module == nullptr)
    {
      return DesignFault::UnknownModule;
    }
    if (module->chassisClassMask != 0 && (module->chassisClassMask & ChassisClassBit(chassis->chassisClass)) == 0)
    {
      return DesignFault::ModuleRefusesChassis;
    }
    weightPenaltyPercent += module->weightPenaltyPercent;
    moduleCostHundredths += module->costHundredths;
    if (module->systemKind == SystemKind::Sensor)
    {
      moduleSightSubunits = std::max(moduleSightSubunits, module->sightSubunits);
    }
  }
  // A device is never brought to a standstill by its load, whatever a mod's weights add up to.
  weightPenaltyPercent = std::min(weightPenaltyPercent, 90);

  const std::size_t chassisClass = static_cast<std::size_t>(chassis->chassisClass);

  // speed = chassis base x drive factor x (100 - the modules' weight) / 100, one rounding at the end.
  const std::int64_t speedTimesDrive = static_cast<std::int64_t>(chassis->baseSpeedSubunitsPerTick) * drive->speedFactorHundredths;
  _out.speedSubunitsPerTick = ApplyPercent(speedTimesDrive / 100, 100 - weightPenaltyPercent);

  // hit points = chassis x drive factor x (100 + the class's upgrade) / 100.
  const std::int64_t hitPointsTimesDrive = static_cast<std::int64_t>(chassis->hitPoints) * drive->hitPointFactorHundredths;
  _out.hitPoints = ApplyPercent(hitPointsTimesDrive / 100, 100 + _upgrades.chassisHitPointPercent[chassisClass]);

  const std::int32_t armorPercent = 100 + _upgrades.chassisArmorPercent[chassisClass];
  _out.kineticArmor = ApplyPercent(chassis->kineticArmor, armorPercent);
  _out.thermalArmor = ApplyPercent(chassis->thermalArmor, armorPercent);

  _out.costHundredths = chassis->costHundredths + drive->costHundredths + moduleCostHundredths;
  _out.buildTimeTicks = BuildTimeTicksFor(_out.costHundredths);
  _out.sightSubunits = std::max(chassis->sightSubunits, moduleSightSubunits);
  _out.maxSlopePercent = drive->maxSlopePercent;
  _out.crossesWater = drive->crossesWater;
  return DesignFault::None;
}

} // namespace Frontier
