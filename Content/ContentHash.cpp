#include "pch.h"

#include "ContentHash.h"

#include "Hash.h"

#include <span>
#include <string_view>

namespace Frontier
{

namespace
{

class Digest
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

  /// The length then the bytes, so that two rows whose ids run together cannot hash as one.
  void AddText(std::string_view _text) noexcept
  {
    Add(static_cast<std::uint32_t>(_text.size()));
    for (const char character : _text)
    {
      Add(static_cast<std::uint8_t>(character));
    }
  }

  void AddTexts(const std::vector<std::string>& _texts) noexcept
  {
    Add(static_cast<std::uint32_t>(_texts.size()));
    for (const std::string& text : _texts)
    {
      AddText(text);
    }
  }

  [[nodiscard]] std::uint64_t Value() const noexcept
  {
    return m_hash;
  }

private:
  std::uint64_t m_hash = Neuron::FNV1A64_OFFSET;
};

void AddChassis(Digest& _digest, const ChassisDesc& _row) noexcept
{
  _digest.AddText(_row.id);
  _digest.AddText(_row.name);
  _digest.Add(_row.chassisClass);
  _digest.AddText(_row.unlockedBy);
  _digest.AddText(_row.model);
  _digest.Add(_row.modelScaleHundredths);
  _digest.Add(_row.hitPoints);
  _digest.Add(_row.kineticArmor);
  _digest.Add(_row.thermalArmor);
  _digest.Add(_row.baseSpeedSubunitsPerTick);
  _digest.Add(_row.sightSubunits);
  _digest.Add(_row.costHundredths);
  _digest.Add(_row.mounts);
}

void AddDrive(Digest& _digest, const DriveDesc& _row) noexcept
{
  _digest.AddText(_row.id);
  _digest.AddText(_row.name);
  _digest.Add(_row.driveClass);
  _digest.AddText(_row.unlockedBy);
  _digest.AddText(_row.model);
  _digest.Add(_row.modelScaleHundredths);
  _digest.Add(_row.speedFactorHundredths);
  _digest.Add(_row.maxSlopePercent);
  _digest.AddBool(_row.crossesWater);
  _digest.Add(_row.hitPointFactorHundredths);
  _digest.Add(_row.costHundredths);
}

void AddModule(Digest& _digest, const ModuleDesc& _row) noexcept
{
  _digest.AddText(_row.id);
  _digest.AddText(_row.name);
  _digest.Add(_row.systemKind);
  _digest.AddText(_row.unlockedBy);
  _digest.AddText(_row.model);
  _digest.Add(_row.modelScaleHundredths);
  _digest.Add(_row.weightPenaltyPercent);
  _digest.Add(_row.costHundredths);
  _digest.Add(_row.chassisClassMask);
  _digest.Add(_row.weaponClass);
  _digest.Add(_row.damage);
  _digest.Add(_row.shotsPerSalvo);
  _digest.Add(_row.reloadTicks);
  _digest.Add(_row.fireKind);
  _digest.Add(_row.minimumRangeSubunits);
  _digest.Add(_row.shortRangeSubunits);
  _digest.Add(_row.longRangeSubunits);
  _digest.Add(_row.shortHitPercent);
  _digest.Add(_row.longHitPercent);
  _digest.Add(_row.splashSubunits);
  _digest.Add(_row.systemRangeSubunits);
  _digest.Add(_row.buildPowerHundredthsPerTick);
  _digest.Add(_row.repairHitPointsHundredthsPerTick);
  _digest.Add(_row.sightSubunits);
}

void AddStructure(Digest& _digest, const StructureDesc& _row) noexcept
{
  _digest.AddText(_row.id);
  _digest.AddText(_row.name);
  _digest.Add(_row.role);
  _digest.Add(_row.strength);
  _digest.AddText(_row.unlockedBy);
  _digest.AddText(_row.model);
  _digest.Add(_row.modelScaleHundredths);
  _digest.Add(_row.footprintCellsX);
  _digest.Add(_row.footprintCellsY);
  _digest.Add(_row.hitPoints);
  _digest.Add(_row.kineticArmor);
  _digest.Add(_row.thermalArmor);
  _digest.Add(_row.costHundredths);
  _digest.Add(_row.buildTimeTicks);
  _digest.Add(_row.sightSubunits);
  _digest.Add(_row.moduleSlots);
  _digest.AddTexts(_row.modules);
  _digest.Add(_row.powerHundredthsPerTick);
  _digest.Add(_row.servesExtractors);
  _digest.Add(_row.serviceRangeSubunits);
  _digest.Add(_row.repairHitPointsHundredthsPerTick);
  _digest.AddText(_row.weapon);
}

void AddStructureModule(Digest& _digest, const StructureModuleDesc& _row) noexcept
{
  _digest.AddText(_row.id);
  _digest.AddText(_row.name);
  _digest.AddText(_row.unlockedBy);
  _digest.AddText(_row.model);
  _digest.Add(_row.modelScaleHundredths);
  _digest.Add(_row.costHundredths);
  _digest.Add(_row.buildTimeTicks);
  _digest.Add(_row.effect);
  _digest.Add(_row.amount);
}

void AddResearch(Digest& _digest, const ResearchItemDesc& _row) noexcept
{
  _digest.AddText(_row.id);
  _digest.AddText(_row.name);
  _digest.AddText(_row.description);
  _digest.AddTexts(_row.prerequisites);
  _digest.Add(_row.costHundredths);
  _digest.Add(_row.timeTicks);
  _digest.AddText(_row.unlocks);
  _digest.Add(_row.targetClass);
  _digest.Add(_row.upgradePercent);
}

template <class T, class Fn> void AddTable(Digest& _digest, const std::vector<T>& _rows, Fn _add) noexcept
{
  _digest.Add(static_cast<std::uint32_t>(_rows.size()));
  for (const T& row : _rows)
  {
    _add(_digest, row);
  }
}

} // namespace

std::uint64_t ContentHash(const ContentTree& _tree) noexcept
{
  Digest digest;
  AddTable(digest, _tree.components.chassis, AddChassis);
  AddTable(digest, _tree.components.drives, AddDrive);
  AddTable(digest, _tree.components.modules, AddModule);
  AddTable(digest, _tree.structures.structures, AddStructure);
  AddTable(digest, _tree.structures.modules, AddStructureModule);
  AddTable(digest, _tree.research, AddResearch);
  for (const std::array<std::int32_t, TARGET_CLASS_COUNT>& row : _tree.damage.modifierPercent)
  {
    for (const std::int32_t modifier : row)
    {
      digest.Add(modifier);
    }
  }
  for (const std::int32_t factor : _tree.damage.armorFactorPercent)
  {
    digest.Add(factor);
  }
  for (const ArmorKind kind : _tree.damage.armorKind)
  {
    digest.Add(kind);
  }
  return digest.Value();
}

} // namespace Frontier
