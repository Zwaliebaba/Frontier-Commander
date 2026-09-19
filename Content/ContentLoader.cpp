#include "pch.h"

#include "ContentLoader.h"

#include "Json.h"

#include <array>
#include <cstdio>
#include <fstream>
#include <map>
#include <span>
#include <utility>

namespace Frontier
{
namespace
{

// The ranges every number declares, in one place so that a reader and a reviewer see the same
// bounds. A number outside its range is refused with the line it is on, which is the difference
// between a table a designer can edit safely and one that fails at the first tick.
constexpr std::int32_t MAX_POWER_HUNDREDTHS = 1000000; ///< 10,000 power: above any cost the design names
constexpr std::int32_t MAX_HIT_POINTS = 100000;
constexpr std::int32_t MAX_ARMOR = 1000;
constexpr std::int32_t MAX_SPEED_SUBUNITS_PER_TICK = 100000;
constexpr std::int32_t MAX_DISTANCE_SUBUNITS = 100000000; ///< 390,625 world units, or 6,104 cells: no sight or weapon range approaches it
constexpr std::uint32_t MAX_TICKS = 1000000;              ///< Fourteen hours
constexpr std::int32_t MAX_PERCENT = 10000;
constexpr std::int32_t MAX_FACTOR_HUNDREDTHS = 100000;
constexpr std::uint32_t MAX_FOOTPRINT_CELLS = 16;
constexpr std::uint32_t MAX_MODEL_VERTICES = 65535;
/// A model may be drawn from a hundredth of its authored size to a hundred times it. The Species
/// review found shapes needing 0.45 and 0.6 to fit their footprints (SpeciesLineage.md §5), so the
/// range is generous at the small end and bounded at the large one.
constexpr std::int32_t MIN_MODEL_SCALE_HUNDREDTHS = 1;
constexpr std::int32_t MAX_MODEL_SCALE_HUNDREDTHS = 10000;
constexpr std::uint32_t MAX_STAMP_SAMPLES = 1025;

/// Reads a whole file as text. Returns false when it cannot be opened or read.
[[nodiscard]] bool ReadFileText(const std::filesystem::path& _path, std::string& _out)
{
  std::ifstream stream(_path, std::ios::binary);
  if (!stream)
  {
    return false;
  }
  stream.seekg(0, std::ios::end);
  const std::streamoff size = stream.tellg();
  if (size < 0)
  {
    return false;
  }
  stream.seekg(0, std::ios::beg);
  _out.resize(static_cast<std::size_t>(size));
  if (size > 0 && !stream.read(_out.data(), size))
  {
    return false;
  }
  return true;
}

/// One file's reader: every accessor either fills its output or records the one diagnostic and
/// returns false, and the caller stops at the first false. A document is accepted whole or
/// rejected whole, so there is never a half-read table in the tree.
class Reader
{
public:
  Reader(std::string _file, ContentDiagnostic& _diagnostic) noexcept
    : m_file(std::move(_file)),
      m_diagnostic(_diagnostic)
  {
  }

  bool Fail(const Neuron::JsonValue& _at, const std::string& _message)
  {
    m_diagnostic.file = m_file;
    m_diagnostic.line = _at.Line();
    m_diagnostic.column = _at.Column();
    m_diagnostic.message = _message;
    return false;
  }

  [[nodiscard]] bool Object(const Neuron::JsonValue& _value, const char* _what)
  {
    return _value.IsObject() ? true : Fail(_value, std::string(_what) + " is an object");
  }

  [[nodiscard]] bool Array(const Neuron::JsonValue& _value, const char* _what)
  {
    return _value.IsArray() ? true : Fail(_value, std::string(_what) + " is an array");
  }

  /// A required member, or a diagnostic naming it on the object's own line.
  [[nodiscard]] const Neuron::JsonValue* Member(const Neuron::JsonValue& _object, const char* _key)
  {
    const Neuron::JsonValue* member = _object.Find(_key);
    if (member == nullptr)
    {
      Fail(_object, std::string("the member '") + _key + "' is missing");
      return nullptr;
    }
    return member;
  }

  [[nodiscard]] bool String(const Neuron::JsonValue& _object, const char* _key, std::string& _out)
  {
    const Neuron::JsonValue* member = Member(_object, _key);
    if (member == nullptr)
    {
      return false;
    }
    if (!member->IsString())
    {
      return Fail(*member, std::string("'") + _key + "' is a string");
    }
    _out = member->AsString();
    return true;
  }

  /// A member that may be absent, in which case _out is left empty.
  [[nodiscard]] bool OptionalString(const Neuron::JsonValue& _object, const char* _key, std::string& _out)
  {
    const Neuron::JsonValue* member = _object.Find(_key);
    if (member == nullptr || member->IsNull())
    {
      _out.clear();
      return true;
    }
    if (!member->IsString())
    {
      return Fail(*member, std::string("'") + _key + "' is a string");
    }
    _out = member->AsString();
    return true;
  }

  [[nodiscard]] bool Integer(const Neuron::JsonValue& _object, const char* _key, std::int64_t _low, std::int64_t _high, std::int64_t& _out)
  {
    const Neuron::JsonValue* member = Member(_object, _key);
    return member == nullptr ? false : IntegerAt(*member, _key, _low, _high, _out);
  }

  [[nodiscard]] bool IntegerAt(const Neuron::JsonValue& _value, const char* _key, std::int64_t _low, std::int64_t _high, std::int64_t& _out)
  {
    if (!_value.IsInteger())
    {
      return Fail(_value, std::string("'") + _key + "' is a whole number");
    }
    const std::int64_t value = _value.AsInteger();
    if (value < _low || value > _high)
    {
      char range[96];
      std::snprintf(range, sizeof range, "' is between %lld and %lld", static_cast<long long>(_low), static_cast<long long>(_high));
      return Fail(_value, std::string("'") + _key + range);
    }
    _out = value;
    return true;
  }

  [[nodiscard]] bool Int32(const Neuron::JsonValue& _object, const char* _key, std::int32_t _low, std::int32_t _high, std::int32_t& _out)
  {
    std::int64_t wide = 0;
    if (!Integer(_object, _key, _low, _high, wide))
    {
      return false;
    }
    _out = static_cast<std::int32_t>(wide);
    return true;
  }

  [[nodiscard]] bool UInt32(const Neuron::JsonValue& _object, const char* _key, std::uint32_t _low, std::uint32_t _high,
                            std::uint32_t& _out)
  {
    std::int64_t wide = 0;
    if (!Integer(_object, _key, _low, _high, wide))
    {
      return false;
    }
    _out = static_cast<std::uint32_t>(wide);
    return true;
  }

  /// An optional whole number, left at _out's current value when absent.
  [[nodiscard]] bool OptionalInt32(const Neuron::JsonValue& _object, const char* _key, std::int32_t _low, std::int32_t _high,
                                   std::int32_t& _out)
  {
    if (_object.Find(_key) == nullptr)
    {
      return true;
    }
    return Int32(_object, _key, _low, _high, _out);
  }

  [[nodiscard]] bool Boolean(const Neuron::JsonValue& _object, const char* _key, bool& _out)
  {
    const Neuron::JsonValue* member = _object.Find(_key);
    if (member == nullptr)
    {
      _out = false;
      return true;
    }
    if (!member->IsBool())
    {
      return Fail(*member, std::string("'") + _key + "' is true or false");
    }
    _out = member->AsBool();
    return true;
  }

  /// A string member matched against a table of names; the diagnostic lists what was allowed.
  template <typename Enumeration>
  [[nodiscard]] bool Enumerated(const Neuron::JsonValue& _object, const char* _key,
                                std::span<const std::pair<const char*, Enumeration>> _names, Enumeration& _out)
  {
    std::string text;
    if (!String(_object, _key, text))
    {
      return false;
    }
    for (const auto& [name, value] : _names)
    {
      if (text == name)
      {
        _out = value;
        return true;
      }
    }
    std::string allowed;
    for (const std::pair<const char*, Enumeration>& entry : _names)
    {
      allowed += allowed.empty() ? "" : ", ";
      allowed += entry.first;
    }
    return Fail(*_object.Find(_key), std::string("'") + _key + "' is one of: " + allowed);
  }

  /// A fixed-length array of whole numbers, as a light's direction or a matrix row.
  [[nodiscard]] bool IntegerArray(const Neuron::JsonValue& _object, const char* _key, std::size_t _count, std::int64_t _low,
                                  std::int64_t _high, std::span<std::int32_t> _out)
  {
    const Neuron::JsonValue* member = Member(_object, _key);
    if (member == nullptr)
    {
      return false;
    }
    if (!member->IsArray() || member->Size() != _count)
    {
      char expected[64];
      std::snprintf(expected, sizeof expected, "' is an array of %zu whole numbers", _count);
      return Fail(*member, std::string("'") + _key + expected);
    }
    for (std::size_t index = 0; index < _count; ++index)
    {
      std::int64_t value = 0;
      if (!IntegerAt(member->At(index), _key, _low, _high, value))
      {
        return false;
      }
      _out[index] = static_cast<std::int32_t>(value);
    }
    return true;
  }

  /// An optional array of strings, empty when absent.
  [[nodiscard]] bool StringArray(const Neuron::JsonValue& _object, const char* _key, std::size_t _most, std::vector<std::string>& _out)
  {
    _out.clear();
    const Neuron::JsonValue* member = _object.Find(_key);
    if (member == nullptr || member->IsNull())
    {
      return true;
    }
    if (!member->IsArray())
    {
      return Fail(*member, std::string("'") + _key + "' is an array of strings");
    }
    if (member->Size() > _most)
    {
      char limit[64];
      std::snprintf(limit, sizeof limit, "' holds at most %zu entries", _most);
      return Fail(*member, std::string("'") + _key + limit);
    }
    for (std::size_t index = 0; index < member->Size(); ++index)
    {
      const Neuron::JsonValue& element = member->At(index);
      if (!element.IsString())
      {
        return Fail(element, std::string("'") + _key + "' holds strings");
      }
      _out.push_back(element.AsString());
    }
    return true;
  }

  /// Records where a row was read from, so that a fault found later can name its line.
  void Record(const Neuron::JsonValue& _row, const std::string& _id)
  {
    if (m_origins != nullptr)
    {
      m_origins->push_back(RowOrigin{_id, m_file, _row.Line()});
    }
  }

  void RecordInto(std::vector<RowOrigin>* _origins) noexcept
  {
    m_origins = _origins;
  }

  /// The document's version member, which every table file carries.
  [[nodiscard]] bool Version(const Neuron::JsonValue& _document, std::uint32_t _expected)
  {
    std::uint32_t version = 0;
    if (!UInt32(_document, "version", 1, 1000, version))
    {
      return false;
    }
    if (version != _expected)
    {
      char message[96];
      std::snprintf(message, sizeof message, "this reader knows version %u of this file, not %u", _expected, version);
      return Fail(_document, message);
    }
    return true;
  }

private:
  std::string m_file;
  ContentDiagnostic& m_diagnostic;
  std::vector<RowOrigin>* m_origins = nullptr;
};

constexpr std::pair<const char*, ChassisClass> CHASSIS_CLASS_NAMES[] = {
  {"Light", ChassisClass::Light}, {"Medium", ChassisClass::Medium}, {"Heavy", ChassisClass::Heavy}};

constexpr std::pair<const char*, DriveClass> DRIVE_CLASS_NAMES[] = {{"Wheels", DriveClass::Wheels}, {"HalfTrack", DriveClass::HalfTrack},
                                                                    {"Tracks", DriveClass::Tracks}, {"Hover", DriveClass::Hover},
                                                                    {"Legs", DriveClass::Legs},     {"Lift", DriveClass::Lift}};

constexpr std::pair<const char*, WeaponClass> WEAPON_CLASS_NAMES[] = {{"AntiLight", WeaponClass::AntiLight},
                                                                      {"AntiTank", WeaponClass::AntiTank},
                                                                      {"Flame", WeaponClass::Flame},
                                                                      {"Artillery", WeaponClass::Artillery},
                                                                      {"Energy", WeaponClass::Energy}};

constexpr std::pair<const char*, ArmorKind> ARMOR_KIND_NAMES[] = {{"Kinetic", ArmorKind::Kinetic}, {"Thermal", ArmorKind::Thermal}};

constexpr std::pair<const char*, SystemKind> SYSTEM_KIND_NAMES[] = {{"None", SystemKind::None},
                                                                    {"Builder", SystemKind::Builder},
                                                                    {"Sensor", SystemKind::Sensor},
                                                                    {"Repair", SystemKind::Repair},
                                                                    {"Command", SystemKind::Command}};

constexpr std::pair<const char*, FireKind> FIRE_KIND_NAMES[] = {{"Direct", FireKind::Direct}, {"Indirect", FireKind::Indirect}};

constexpr std::pair<const char*, StrengthClass> STRENGTH_CLASS_NAMES[] = {
  {"Soft", StrengthClass::Soft}, {"Medium", StrengthClass::Medium}, {"Hard", StrengthClass::Hard}, {"Bunker", StrengthClass::Bunker}};

constexpr std::pair<const char*, StructureRole> STRUCTURE_ROLE_NAMES[] = {
  {"CommandPost", StructureRole::CommandPost}, {"Extractor", StructureRole::Extractor},
  {"Generator", StructureRole::Generator},     {"Factory", StructureRole::Factory},
  {"ResearchLab", StructureRole::ResearchLab}, {"RepairBay", StructureRole::RepairBay},
  {"SensorTower", StructureRole::SensorTower}, {"Wall", StructureRole::Wall},
  {"Hardpoint", StructureRole::Hardpoint},     {"Tower", StructureRole::Tower},
  {"Bunker", StructureRole::Bunker},           {"Uplink", StructureRole::Uplink}};

constexpr std::pair<const char*, StructureModuleEffect> STRUCTURE_MODULE_EFFECT_NAMES[] = {
  {"ShortenBuildTime", StructureModuleEffect::ShortenBuildTime},
  {"ShortenResearchTime", StructureModuleEffect::ShortenResearchTime},
  {"ServeMoreExtractors", StructureModuleEffect::ServeMoreExtractors},
  {"AddWeapon", StructureModuleEffect::AddWeapon}};

constexpr std::pair<const char*, ResearchEffect> RESEARCH_EFFECT_NAMES[] = {{"Unlock", ResearchEffect::Unlock},
                                                                            {"ChassisArmor", ResearchEffect::ChassisArmor},
                                                                            {"ChassisHitPoints", ResearchEffect::ChassisHitPoints},
                                                                            {"WeaponDamage", ResearchEffect::WeaponDamage},
                                                                            {"WeaponRate", ResearchEffect::WeaponRate},
                                                                            {"WeaponAccuracy", ResearchEffect::WeaponAccuracy},
                                                                            {"ExtractorRate", ResearchEffect::ExtractorRate},
                                                                            {"StructureHitPoints", ResearchEffect::StructureHitPoints}};

constexpr std::pair<const char*, FogMode> FOG_MODE_NAMES[] = {{"LinearToColor", FogMode::LinearToColor},
                                                              {"Desaturation", FogMode::Desaturation}};

constexpr std::pair<const char*, SoundSpace> SOUND_SPACE_NAMES[] = {{"World", SoundSpace::World}, {"Interface", SoundSpace::Interface}};

constexpr std::pair<const char*, SizeClass> SIZE_CLASS_NAMES[] = {
  {"Small", SizeClass::Small}, {"Medium", SizeClass::Medium}, {"Large", SizeClass::Large}, {"Frontier", SizeClass::Frontier}};

} // namespace

namespace
{

/// The version every table file carries; a file from a newer reader is refused by name rather than
/// misread. The landscape, stamp and model documents carry their own, declared beside their row.
constexpr std::uint32_t TABLE_VERSION = 1;

[[nodiscard]] bool ReadChassis(Reader& _reader, const Neuron::JsonValue& _row, ChassisDesc& _out)
{
  return _reader.Object(_row, "a chassis") && _reader.String(_row, "id", _out.id) && _reader.String(_row, "name", _out.name) &&
         _reader.Enumerated<ChassisClass>(_row, "class", CHASSIS_CLASS_NAMES, _out.chassisClass) &&
         _reader.OptionalString(_row, "unlockedBy", _out.unlockedBy) && _reader.String(_row, "model", _out.model) &&
         _reader.OptionalInt32(_row, "modelScaleHundredths", MIN_MODEL_SCALE_HUNDREDTHS, MAX_MODEL_SCALE_HUNDREDTHS,
                               _out.modelScaleHundredths) &&
         _reader.Int32(_row, "hitPoints", 1, MAX_HIT_POINTS, _out.hitPoints) &&
         _reader.Int32(_row, "kineticArmor", 0, MAX_ARMOR, _out.kineticArmor) &&
         _reader.Int32(_row, "thermalArmor", 0, MAX_ARMOR, _out.thermalArmor) &&
         _reader.Int32(_row, "baseSpeedSubunitsPerTick", 1, MAX_SPEED_SUBUNITS_PER_TICK, _out.baseSpeedSubunitsPerTick) &&
         _reader.Int32(_row, "sightSubunits", 0, MAX_DISTANCE_SUBUNITS, _out.sightSubunits) &&
         _reader.Int32(_row, "costHundredths", 1, MAX_POWER_HUNDREDTHS, _out.costHundredths) && [&]
  {
    std::uint32_t mounts = 0;
    if (!_reader.UInt32(_row, "mounts", 1, MAX_MOUNTS, mounts))
    {
      return false;
    }
    _out.mounts = static_cast<std::uint8_t>(mounts);
    return true;
  }();
}

[[nodiscard]] bool ReadDrive(Reader& _reader, const Neuron::JsonValue& _row, DriveDesc& _out)
{
  return _reader.Object(_row, "a drive") && _reader.String(_row, "id", _out.id) && _reader.String(_row, "name", _out.name) &&
         _reader.Enumerated<DriveClass>(_row, "class", DRIVE_CLASS_NAMES, _out.driveClass) &&
         _reader.OptionalString(_row, "unlockedBy", _out.unlockedBy) && _reader.String(_row, "model", _out.model) &&
         _reader.OptionalInt32(_row, "modelScaleHundredths", MIN_MODEL_SCALE_HUNDREDTHS, MAX_MODEL_SCALE_HUNDREDTHS,
                               _out.modelScaleHundredths) &&
         _reader.Int32(_row, "speedFactorHundredths", 1, MAX_FACTOR_HUNDREDTHS, _out.speedFactorHundredths) &&
         _reader.Int32(_row, "maxSlopePercent", 0, MAX_PERCENT, _out.maxSlopePercent) &&
         _reader.Boolean(_row, "crossesWater", _out.crossesWater) &&
         _reader.Int32(_row, "hitPointFactorHundredths", 1, MAX_FACTOR_HUNDREDTHS, _out.hitPointFactorHundredths) &&
         _reader.Int32(_row, "costHundredths", 1, MAX_POWER_HUNDREDTHS, _out.costHundredths);
}

[[nodiscard]] bool ReadModule(Reader& _reader, const Neuron::JsonValue& _row, ModuleDesc& _out)
{
  if (!_reader.Object(_row, "a module") || !_reader.String(_row, "id", _out.id) || !_reader.String(_row, "name", _out.name) ||
      !_reader.Enumerated<SystemKind>(_row, "systemKind", SYSTEM_KIND_NAMES, _out.systemKind) ||
      !_reader.OptionalString(_row, "unlockedBy", _out.unlockedBy) || !_reader.String(_row, "model", _out.model) ||
      !_reader.OptionalInt32(_row, "modelScaleHundredths", MIN_MODEL_SCALE_HUNDREDTHS, MAX_MODEL_SCALE_HUNDREDTHS,
                             _out.modelScaleHundredths) ||
      !_reader.Int32(_row, "weightPenaltyPercent", 0, 100, _out.weightPenaltyPercent) ||
      !_reader.Int32(_row, "costHundredths", 1, MAX_POWER_HUNDREDTHS, _out.costHundredths))
  {
    return false;
  }

  // Which chassis take it: absent or empty means every chassis.
  std::vector<std::string> classes;
  if (!_reader.StringArray(_row, "chassisClasses", CHASSIS_CLASS_COUNT, classes))
  {
    return false;
  }
  _out.chassisClassMask = 0;
  for (const std::string& name : classes)
  {
    bool found = false;
    for (const std::pair<const char*, ChassisClass>& entry : CHASSIS_CLASS_NAMES)
    {
      if (name == entry.first)
      {
        _out.chassisClassMask = static_cast<std::uint8_t>(_out.chassisClassMask | ChassisClassBit(entry.second));
        found = true;
      }
    }
    if (!found)
    {
      return _reader.Fail(*_row.Find("chassisClasses"), "'chassisClasses' holds chassis class names");
    }
  }

  if (_out.systemKind == SystemKind::None)
  {
    std::uint32_t salvo = 1;
    return _reader.Enumerated<WeaponClass>(_row, "weaponClass", WEAPON_CLASS_NAMES, _out.weaponClass) &&
           _reader.Int32(_row, "damage", 1, MAX_HIT_POINTS, _out.damage) && _reader.UInt32(_row, "shotsPerSalvo", 1, 64, salvo) &&
           _reader.UInt32(_row, "reloadTicks", 1, MAX_TICKS, _out.reloadTicks) &&
           _reader.Enumerated<FireKind>(_row, "fireKind", FIRE_KIND_NAMES, _out.fireKind) &&
           _reader.OptionalInt32(_row, "minimumRangeSubunits", 0, MAX_DISTANCE_SUBUNITS, _out.minimumRangeSubunits) &&
           _reader.Int32(_row, "shortRangeSubunits", 1, MAX_DISTANCE_SUBUNITS, _out.shortRangeSubunits) &&
           _reader.Int32(_row, "longRangeSubunits", 1, MAX_DISTANCE_SUBUNITS, _out.longRangeSubunits) &&
           _reader.Int32(_row, "shortHitPercent", 0, 100, _out.shortHitPercent) &&
           _reader.Int32(_row, "longHitPercent", 0, 100, _out.longHitPercent) &&
           _reader.OptionalInt32(_row, "splashSubunits", 0, MAX_DISTANCE_SUBUNITS, _out.splashSubunits) && [&]
    {
      _out.shotsPerSalvo = static_cast<std::uint8_t>(salvo);
      return true;
    }();
  }

  return _reader.OptionalInt32(_row, "systemRangeSubunits", 0, MAX_DISTANCE_SUBUNITS, _out.systemRangeSubunits) &&
         _reader.OptionalInt32(_row, "buildPowerHundredthsPerTick", 0, MAX_POWER_HUNDREDTHS, _out.buildPowerHundredthsPerTick) &&
         _reader.OptionalInt32(_row, "repairHitPointsHundredthsPerTick", 0, MAX_HIT_POINTS, _out.repairHitPointsHundredthsPerTick) &&
         _reader.OptionalInt32(_row, "sightSubunits", 0, MAX_DISTANCE_SUBUNITS, _out.sightSubunits);
}

[[nodiscard]] bool ReadComponents(Reader& _reader, const Neuron::JsonValue& _document, ComponentTables& _out)
{
  if (!_reader.Object(_document, "Components.json") || !_reader.Version(_document, TABLE_VERSION))
  {
    return false;
  }
  const Neuron::JsonValue* chassis = _reader.Member(_document, "chassis");
  const Neuron::JsonValue* drives = chassis == nullptr ? nullptr : _reader.Member(_document, "drives");
  const Neuron::JsonValue* modules = drives == nullptr ? nullptr : _reader.Member(_document, "modules");
  if (modules == nullptr || !_reader.Array(*chassis, "'chassis'") || !_reader.Array(*drives, "'drives'") ||
      !_reader.Array(*modules, "'modules'"))
  {
    return false;
  }
  for (std::size_t index = 0; index < chassis->Size(); ++index)
  {
    ChassisDesc row{};
    if (!ReadChassis(_reader, chassis->At(index), row))
    {
      return false;
    }
    _reader.Record(chassis->At(index), row.id);
    _out.chassis.push_back(std::move(row));
  }
  for (std::size_t index = 0; index < drives->Size(); ++index)
  {
    DriveDesc row{};
    if (!ReadDrive(_reader, drives->At(index), row))
    {
      return false;
    }
    _reader.Record(drives->At(index), row.id);
    _out.drives.push_back(std::move(row));
  }
  for (std::size_t index = 0; index < modules->Size(); ++index)
  {
    ModuleDesc row{};
    if (!ReadModule(_reader, modules->At(index), row))
    {
      return false;
    }
    _reader.Record(modules->At(index), row.id);
    _out.modules.push_back(std::move(row));
  }
  return true;
}

[[nodiscard]] bool ReadStructure(Reader& _reader, const Neuron::JsonValue& _row, StructureDesc& _out)
{
  std::uint32_t slots = 0;
  return _reader.Object(_row, "a structure") && _reader.String(_row, "id", _out.id) && _reader.String(_row, "name", _out.name) &&
         _reader.Enumerated<StructureRole>(_row, "role", STRUCTURE_ROLE_NAMES, _out.role) &&
         _reader.Enumerated<StrengthClass>(_row, "strength", STRENGTH_CLASS_NAMES, _out.strength) &&
         _reader.OptionalString(_row, "unlockedBy", _out.unlockedBy) && _reader.String(_row, "model", _out.model) &&
         _reader.OptionalInt32(_row, "modelScaleHundredths", MIN_MODEL_SCALE_HUNDREDTHS, MAX_MODEL_SCALE_HUNDREDTHS,
                               _out.modelScaleHundredths) &&
         _reader.UInt32(_row, "footprintCellsX", 1, MAX_FOOTPRINT_CELLS, _out.footprintCellsX) &&
         _reader.UInt32(_row, "footprintCellsY", 1, MAX_FOOTPRINT_CELLS, _out.footprintCellsY) &&
         _reader.Int32(_row, "hitPoints", 1, MAX_HIT_POINTS, _out.hitPoints) &&
         _reader.Int32(_row, "kineticArmor", 0, MAX_ARMOR, _out.kineticArmor) &&
         _reader.Int32(_row, "thermalArmor", 0, MAX_ARMOR, _out.thermalArmor) &&
         _reader.Int32(_row, "costHundredths", 1, MAX_POWER_HUNDREDTHS, _out.costHundredths) &&
         _reader.UInt32(_row, "buildTimeTicks", 1, MAX_TICKS, _out.buildTimeTicks) &&
         _reader.Int32(_row, "sightSubunits", 0, MAX_DISTANCE_SUBUNITS, _out.sightSubunits) &&
         _reader.UInt32(_row, "moduleSlots", 0, MAX_MOUNTS, slots) && _reader.StringArray(_row, "modules", MAX_MOUNTS, _out.modules) &&
         _reader.OptionalInt32(_row, "powerHundredthsPerTick", 0, MAX_POWER_HUNDREDTHS, _out.powerHundredthsPerTick) &&
         [&]
  {
    _out.moduleSlots = static_cast<std::uint8_t>(slots);
    return true;
  }() && _reader.OptionalInt32(_row, "serviceRangeSubunits", 0, MAX_DISTANCE_SUBUNITS, _out.serviceRangeSubunits) &&
         _reader.OptionalInt32(_row, "repairHitPointsHundredthsPerTick", 0, MAX_HIT_POINTS, _out.repairHitPointsHundredthsPerTick) &&
         _reader.OptionalString(_row, "weapon", _out.weapon) && [&]
  {
    if (_row.Find("servesExtractors") == nullptr)
    {
      return true;
    }
    return _reader.UInt32(_row, "servesExtractors", 0, 64, _out.servesExtractors);
  }();
}

[[nodiscard]] bool ReadStructureModule(Reader& _reader, const Neuron::JsonValue& _row, StructureModuleDesc& _out)
{
  return _reader.Object(_row, "a structure module") && _reader.String(_row, "id", _out.id) && _reader.String(_row, "name", _out.name) &&
         _reader.OptionalString(_row, "unlockedBy", _out.unlockedBy) && _reader.String(_row, "model", _out.model) &&
         _reader.OptionalInt32(_row, "modelScaleHundredths", MIN_MODEL_SCALE_HUNDREDTHS, MAX_MODEL_SCALE_HUNDREDTHS,
                               _out.modelScaleHundredths) &&
         _reader.Int32(_row, "costHundredths", 1, MAX_POWER_HUNDREDTHS, _out.costHundredths) &&
         _reader.UInt32(_row, "buildTimeTicks", 1, MAX_TICKS, _out.buildTimeTicks) &&
         _reader.Enumerated<StructureModuleEffect>(_row, "effect", STRUCTURE_MODULE_EFFECT_NAMES, _out.effect) &&
         _reader.Int32(_row, "amount", 0, MAX_PERCENT, _out.amount);
}

[[nodiscard]] bool ReadStructures(Reader& _reader, const Neuron::JsonValue& _document, StructureTables& _out)
{
  if (!_reader.Object(_document, "Structures.json") || !_reader.Version(_document, TABLE_VERSION))
  {
    return false;
  }
  const Neuron::JsonValue* structures = _reader.Member(_document, "structures");
  const Neuron::JsonValue* modules = structures == nullptr ? nullptr : _reader.Member(_document, "modules");
  if (modules == nullptr || !_reader.Array(*structures, "'structures'") || !_reader.Array(*modules, "'modules'"))
  {
    return false;
  }
  for (std::size_t index = 0; index < structures->Size(); ++index)
  {
    StructureDesc row{};
    if (!ReadStructure(_reader, structures->At(index), row))
    {
      return false;
    }
    _reader.Record(structures->At(index), row.id);
    _out.structures.push_back(std::move(row));
  }
  for (std::size_t index = 0; index < modules->Size(); ++index)
  {
    StructureModuleDesc row{};
    if (!ReadStructureModule(_reader, modules->At(index), row))
    {
      return false;
    }
    _reader.Record(modules->At(index), row.id);
    _out.modules.push_back(std::move(row));
  }
  return true;
}

[[nodiscard]] bool ReadResearch(Reader& _reader, const Neuron::JsonValue& _document, std::vector<ResearchItemDesc>& _out)
{
  if (!_reader.Object(_document, "Research.json") || !_reader.Version(_document, TABLE_VERSION))
  {
    return false;
  }
  const Neuron::JsonValue* items = _reader.Member(_document, "items");
  if (items == nullptr || !_reader.Array(*items, "'items'"))
  {
    return false;
  }
  for (std::size_t index = 0; index < items->Size(); ++index)
  {
    const Neuron::JsonValue& row = items->At(index);
    ResearchItemDesc item{};
    std::uint32_t targetClass = 0;
    if (!_reader.Object(row, "a research item") || !_reader.String(row, "id", item.id) || !_reader.String(row, "name", item.name) ||
        !_reader.OptionalString(row, "description", item.description) ||
        !_reader.StringArray(row, "prerequisites", MAX_PREREQUISITES, item.prerequisites) ||
        !_reader.Int32(row, "costHundredths", 0, MAX_POWER_HUNDREDTHS, item.costHundredths) ||
        !_reader.UInt32(row, "timeTicks", 1, MAX_TICKS, item.timeTicks) ||
        !_reader.Enumerated<ResearchEffect>(row, "effect", RESEARCH_EFFECT_NAMES, item.effect))
    {
      return false;
    }
    if (item.effect == ResearchEffect::Unlock)
    {
      if (!_reader.String(row, "unlocks", item.unlocks))
      {
        return false;
      }
    }
    else if (!_reader.UInt32(row, "targetClass", 0, 15, targetClass) ||
             !_reader.Int32(row, "upgradePercent", -100, MAX_PERCENT, item.upgradePercent))
    {
      return false;
    }
    item.targetClass = static_cast<std::uint8_t>(targetClass);
    _reader.Record(row, item.id);
    _out.push_back(std::move(item));
  }
  return true;
}

[[nodiscard]] bool ReadDamage(Reader& _reader, const Neuron::JsonValue& _document, DamageTable& _out)
{
  if (!_reader.Object(_document, "Damage.json") || !_reader.Version(_document, TABLE_VERSION))
  {
    return false;
  }
  const Neuron::JsonValue* weapons = _reader.Member(_document, "weapons");
  if (weapons == nullptr || !_reader.Array(*weapons, "'weapons'"))
  {
    return false;
  }
  if (weapons->Size() != WEAPON_CLASS_COUNT)
  {
    return _reader.Fail(*weapons, "'weapons' holds one row per weapon class, five in all");
  }
  std::array<bool, WEAPON_CLASS_COUNT> seen{};
  for (std::size_t index = 0; index < weapons->Size(); ++index)
  {
    const Neuron::JsonValue& row = weapons->At(index);
    WeaponClass weapon = WeaponClass::AntiLight;
    if (!_reader.Object(row, "a weapon class") || !_reader.Enumerated<WeaponClass>(row, "class", WEAPON_CLASS_NAMES, weapon))
    {
      return false;
    }
    const std::size_t slot = static_cast<std::size_t>(weapon);
    if (seen[slot])
    {
      return _reader.Fail(row, "this weapon class already has a row");
    }
    seen[slot] = true;
    if (!_reader.Int32(row, "armorFactorPercent", 0, 100, _out.armorFactorPercent[slot]) ||
        !_reader.Enumerated<ArmorKind>(row, "armorKind", ARMOR_KIND_NAMES, _out.armorKind[slot]) ||
        !_reader.IntegerArray(row, "modifierPercent", TARGET_CLASS_COUNT, 0, MAX_PERCENT, _out.modifierPercent[slot]))
    {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool ReadLight(Reader& _reader, const Neuron::JsonValue& _object, const char* _key, BiomeLight& _out)
{
  const Neuron::JsonValue* light = _reader.Member(_object, _key);
  if (light == nullptr || !_reader.Object(*light, _key))
  {
    return false;
  }
  return _reader.IntegerArray(*light, "directionHundredths", 3, -100, 100, _out.directionHundredths) &&
         _reader.IntegerArray(*light, "colorHundredths", 3, 0, MAX_FACTOR_HUNDREDTHS, _out.colorHundredths);
}

[[nodiscard]] bool ReadBiomes(Reader& _reader, const Neuron::JsonValue& _document, std::vector<BiomeDesc>& _out)
{
  if (!_reader.Object(_document, "Biomes.json") || !_reader.Version(_document, TABLE_VERSION))
  {
    return false;
  }
  const Neuron::JsonValue* biomes = _reader.Member(_document, "biomes");
  if (biomes == nullptr || !_reader.Array(*biomes, "'biomes'"))
  {
    return false;
  }
  for (std::size_t index = 0; index < biomes->Size(); ++index)
  {
    const Neuron::JsonValue& row = biomes->At(index);
    BiomeDesc biome{};
    if (!_reader.Object(row, "a biome") || !_reader.String(row, "id", biome.id) || !_reader.String(row, "name", biome.name) ||
        !_reader.String(row, "paletteTexture", biome.paletteTexture) || !_reader.OptionalString(row, "waterTexture", biome.waterTexture) ||
        !_reader.OptionalString(row, "waveTexture", biome.waveTexture) || !ReadLight(_reader, row, "key", biome.key) ||
        !ReadLight(_reader, row, "sun", biome.sun) || !_reader.Enumerated<FogMode>(row, "fogMode", FOG_MODE_NAMES, biome.fogMode) ||
        !_reader.Int32(row, "fogStartWorldUnits", 0, MAX_FOG_WORLD_UNITS, biome.fogStartWorldUnits) ||
        !_reader.Int32(row, "fogEndWorldUnits", 0, MAX_FOG_WORLD_UNITS, biome.fogEndWorldUnits) ||
        !_reader.Int32(row, "fogMaxDesaturationHundredths", 0, 100, biome.fogMaxDesaturationHundredths) ||
        !_reader.IntegerArray(row, "fogColorHundredths", 3, 0, 100, biome.fogColorHundredths) ||
        !_reader.IntegerArray(row, "skyColorHundredths", 3, 0, 100, biome.skyColorHundredths))
    {
      return false;
    }
    if (biome.fogEndWorldUnits <= biome.fogStartWorldUnits)
    {
      return _reader.Fail(row, "'fogEndWorldUnits' is beyond 'fogStartWorldUnits'");
    }
    _reader.Record(row, biome.id);
    _out.push_back(std::move(biome));
  }
  return true;
}

/// A colour as Interface.json writes one: four whole numbers, red first, each a byte. Read from the
/// value rather than from a key, because the commander list holds bare arrays.
[[nodiscard]] bool ReadRgba8At(Reader& _reader, const Neuron::JsonValue& _value, const char* _what, Rgba8& _out)
{
  if (!_value.IsArray() || _value.Size() != 4)
  {
    return _reader.Fail(_value, std::string("'") + _what + "' is an array of four whole numbers, red first");
  }
  std::array<std::int32_t, 4> channels{};
  for (std::size_t index = 0; index < channels.size(); ++index)
  {
    std::int64_t value = 0;
    if (!_reader.IntegerAt(_value.At(index), _what, 0, 255, value))
    {
      return false;
    }
    channels[index] = static_cast<std::int32_t>(value);
  }
  _out = Rgba8{static_cast<std::uint8_t>(channels[0]), static_cast<std::uint8_t>(channels[1]), static_cast<std::uint8_t>(channels[2]),
               static_cast<std::uint8_t>(channels[3])};
  return true;
}

[[nodiscard]] bool ReadRgba8(Reader& _reader, const Neuron::JsonValue& _object, const char* _key, Rgba8& _out)
{
  const Neuron::JsonValue* member = _reader.Member(_object, _key);
  return member != nullptr && ReadRgba8At(_reader, *member, _key, _out);
}

[[nodiscard]] bool ReadInterface(Reader& _reader, const Neuron::JsonValue& _document, InterfaceDesc& _out)
{
  if (!_reader.Object(_document, "Interface.json") || !_reader.Version(_document, TABLE_VERSION))
  {
    return false;
  }

  const Neuron::JsonValue* chrome = _reader.Member(_document, "chrome");
  if (chrome == nullptr || !_reader.Object(*chrome, "'chrome'"))
  {
    return false;
  }
  for (const ChromeRole& role : CHROME_ROLES)
  {
    if (!ReadRgba8(_reader, *chrome, role.name, _out.chrome.*role.member))
    {
      return false;
    }
  }
  // Every role is now read; this pass is the other half, and it is the half that catches a
  // misspelling. Without it 'acccent' would be read as an unknown member the loop above never asked
  // for, the real accent would come back missing - which is the diagnostic the author would then
  // have to work backwards from - or, worse, a role renamed in the document would leave the field
  // at whatever the aggregate initialised it to and the panel would draw in a colour nobody chose.
  for (std::size_t index = 0; index < chrome->Size(); ++index)
  {
    const std::string& key = chrome->KeyAt(index);
    bool known = false;
    for (const ChromeRole& role : CHROME_ROLES)
    {
      known = known || key == role.name;
    }
    if (!known)
    {
      return _reader.Fail(chrome->ValueAt(index), "'" + key + "' is not a chrome role of Design/Interface.md 3");
    }
  }

  const Neuron::JsonValue* commanders = _reader.Member(_document, "commanders");
  if (commanders == nullptr || !_reader.Array(*commanders, "'commanders'"))
  {
    return false;
  }
  if (commanders->Size() != COMMANDER_COLOR_COUNT)
  {
    char message[96];
    std::snprintf(message, sizeof message, "'commanders' holds one colour a seat, which is %zu", COMMANDER_COLOR_COUNT);
    return _reader.Fail(*commanders, message);
  }
  for (std::size_t index = 0; index < COMMANDER_COLOR_COUNT; ++index)
  {
    if (!ReadRgba8At(_reader, commanders->At(index), "commanders", _out.commanders[index]))
    {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool ReadSounds(Reader& _reader, const Neuron::JsonValue& _document, std::vector<SoundEventDesc>& _out)
{
  if (!_reader.Object(_document, "Sounds.json") || !_reader.Version(_document, TABLE_VERSION))
  {
    return false;
  }
  const Neuron::JsonValue* events = _reader.Member(_document, "events");
  if (events == nullptr || !_reader.Array(*events, "'events'"))
  {
    return false;
  }
  for (std::size_t index = 0; index < events->Size(); ++index)
  {
    const Neuron::JsonValue& row = events->At(index);
    SoundEventDesc event{};
    if (!_reader.Object(row, "a sound event") || !_reader.String(row, "id", event.id) ||
        !_reader.Enumerated<SoundSpace>(row, "space", SOUND_SPACE_NAMES, event.space) ||
        !_reader.StringArray(row, "waves", 16, event.waves) || !_reader.Int32(row, "volumeHundredths", 0, 100, event.volumeHundredths) ||
        !_reader.OptionalInt32(row, "rangeSubunits", 0, MAX_DISTANCE_SUBUNITS, event.rangeSubunits) ||
        !_reader.UInt32(row, "cooldownTicks", 0, MAX_TICKS, event.cooldownTicks) || !_reader.Boolean(row, "loops", event.loops))
    {
      return false;
    }
    if (event.waves.empty())
    {
      return _reader.Fail(row, "a sound event names at least one wave");
    }
    _reader.Record(row, event.id);
    _out.push_back(std::move(event));
  }
  return true;
}

} // namespace

namespace
{

[[nodiscard]] bool ReadCellList(Reader& _reader, const Neuron::JsonValue& _document, const char* _key, std::uint32_t _cellsPerSide,
                                std::vector<CellPosition>& _out)
{
  const Neuron::JsonValue* list = _reader.Member(_document, _key);
  if (list == nullptr || !_reader.Array(*list, _key))
  {
    return false;
  }
  for (std::size_t index = 0; index < list->Size(); ++index)
  {
    const Neuron::JsonValue& row = list->At(index);
    CellPosition cell{};
    if (!_reader.Object(row, "a cell position") || !_reader.UInt32(row, "cellX", 0, _cellsPerSide - 1, cell.x) ||
        !_reader.UInt32(row, "cellY", 0, _cellsPerSide - 1, cell.y))
    {
      return false;
    }
    _out.push_back(cell);
  }
  return true;
}

/// The schema Tools/LandscapeTool.py --define writes, field for field (m0-foundation/T16). The
/// derived members it also writes — samplesPerSide, sampleSpacingWorldUnits, outsideHeight — are
/// checked against this build's constants rather than read, so that a definition generated by a
/// tool of another shape is refused rather than silently regenerated differently.
[[nodiscard]] bool ReadLandscape(Reader& _reader, const Neuron::JsonValue& _document, LandscapeDefinition& _out)
{
  if (!_reader.Object(_document, "a landscape definition") || !_reader.Version(_document, LANDSCAPE_DEFINITION_VERSION))
  {
    return false;
  }
  _out.version = LANDSCAPE_DEFINITION_VERSION;
  if (!_reader.Enumerated<SizeClass>(_document, "sizeClass", SIZE_CLASS_NAMES, _out.sizeClass))
  {
    return false;
  }
  const std::uint32_t expectedCells = SIZE_CLASS_CELLS[static_cast<std::size_t>(_out.sizeClass)];
  if (!_reader.UInt32(_document, "cellsPerSide", expectedCells, expectedCells, _out.cellsPerSide))
  {
    return false;
  }

  std::uint32_t samples = 0;
  std::int32_t spacing = 0;
  std::int32_t outside = 0;
  if (!_reader.UInt32(_document, "samplesPerSide", SamplesPerSide(expectedCells), SamplesPerSide(expectedCells), samples) ||
      !_reader.Int32(_document, "sampleSpacingWorldUnits", SAMPLE_SPACING_WORLD_UNITS, SAMPLE_SPACING_WORLD_UNITS, spacing) ||
      !_reader.Int32(_document, "outsideHeight", OUTSIDE_HEIGHT, OUTSIDE_HEIGHT, outside))
  {
    return false;
  }

  std::int64_t seed = 0;
  if (!_reader.Integer(_document, "seed", 0, std::numeric_limits<std::int64_t>::max(), seed) ||
      !_reader.String(_document, "palette", _out.palette))
  {
    return false;
  }
  _out.seed = static_cast<std::uint64_t>(seed);

  const Neuron::JsonValue* tiles = _reader.Member(_document, "tiles");
  if (tiles == nullptr || !_reader.Array(*tiles, "'tiles'"))
  {
    return false;
  }
  if (tiles->Size() == 0)
  {
    return _reader.Fail(*tiles, "a landscape has at least one tile");
  }
  for (std::size_t index = 0; index < tiles->Size(); ++index)
  {
    const Neuron::JsonValue& row = tiles->At(index);
    LandscapeTile tile{};
    std::uint32_t method = 0;
    if (!_reader.Object(row, "a tile") || !_reader.Int32(row, "x", -100000, 100000, tile.x) ||
        !_reader.Int32(row, "y", -100000, 100000, tile.y) || !_reader.UInt32(row, "extent", 2, 8192, tile.extent) ||
        !_reader.Int32(row, "fractalDimensionHundredths", 1, 1000, tile.fractalDimensionHundredths) ||
        !_reader.Int32(row, "amplitude", 0, 100000, tile.amplitude) ||
        !_reader.Int32(row, "desiredHeight", -100000, 100000, tile.desiredHeight) ||
        !_reader.Int32(row, "heightShift", -100000, 100000, tile.heightShift) ||
        !_reader.Int32(row, "lowlandExponentHundredths", 1, 1000, tile.lowlandExponentHundredths) ||
        !_reader.UInt32(row, "method", 0, 2, method) || !_reader.UInt32(row, "edgeFalloff", 0, 8192, tile.edgeFalloff))
    {
      return false;
    }
    if ((tile.extent & (tile.extent - 1)) != 0)
    {
      return _reader.Fail(row, "'extent' is a power of two");
    }
    // Absent means the landscape's own palette; a tile that names one is a region (Q18).
    if (!_reader.OptionalString(row, "palette", tile.palette))
    {
      return false;
    }
    tile.method = static_cast<std::uint8_t>(method);
    _out.tiles.push_back(tile);
  }

  return ReadCellList(_reader, _document, "starts", _out.cellsPerSide, _out.starts) &&
         ReadCellList(_reader, _document, "deposits", _out.cellsPerSide, _out.deposits);
}

[[nodiscard]] bool ReadStamp(Reader& _reader, const Neuron::JsonValue& _document, StampDesc& _out)
{
  if (!_reader.Object(_document, "a stamp") || !_reader.Version(_document, STAMP_DESC_VERSION))
  {
    return false;
  }
  _out.version = STAMP_DESC_VERSION;
  if (!_reader.String(_document, "id", _out.id) || !_reader.String(_document, "name", _out.name) ||
      !_reader.UInt32(_document, "samplesPerSideX", 1, MAX_STAMP_SAMPLES, _out.samplesPerSideX) ||
      !_reader.UInt32(_document, "samplesPerSideY", 1, MAX_STAMP_SAMPLES, _out.samplesPerSideY) ||
      !_reader.Boolean(_document, "replaces", _out.replaces))
  {
    return false;
  }
  const Neuron::JsonValue* offsets = _reader.Member(_document, "heightOffsets");
  if (offsets == nullptr || !_reader.Array(*offsets, "'heightOffsets'"))
  {
    return false;
  }
  const std::size_t expected = static_cast<std::size_t>(_out.samplesPerSideX) * _out.samplesPerSideY;
  if (offsets->Size() != expected)
  {
    return _reader.Fail(*offsets, "'heightOffsets' holds samplesPerSideX times samplesPerSideY entries");
  }
  _out.heightOffsets.reserve(expected);
  for (std::size_t index = 0; index < expected; ++index)
  {
    std::int64_t value = 0;
    if (!_reader.IntegerAt(offsets->At(index), "heightOffsets", -100000, 100000, value))
    {
      return false;
    }
    _out.heightOffsets.push_back(static_cast<std::int32_t>(value));
  }
  _reader.Record(_document, _out.id);
  return true;
}

[[nodiscard]] bool ReadModel(Reader& _reader, const Neuron::JsonValue& _document, ModelDesc& _out)
{
  if (!_reader.Object(_document, "a model") || !_reader.Version(_document, MODEL_DESC_VERSION) || !_reader.String(_document, "id", _out.id))
  {
    return false;
  }
  _out.version = MODEL_DESC_VERSION;

  const Neuron::JsonValue* vertices = _reader.Member(_document, "vertices");
  const Neuron::JsonValue* triangles = vertices == nullptr ? nullptr : _reader.Member(_document, "triangles");
  if (triangles == nullptr || !_reader.Array(*vertices, "'vertices'") || !_reader.Array(*triangles, "'triangles'"))
  {
    return false;
  }
  if (vertices->Size() > MAX_MODEL_VERTICES)
  {
    return _reader.Fail(*vertices, "a model holds at most 65,535 vertices, because its indices are 16 bits");
  }
  for (std::size_t index = 0; index < vertices->Size(); ++index)
  {
    const Neuron::JsonValue& row = vertices->At(index);
    std::array<std::int32_t, 3> position{};
    if (!_reader.Array(row, "a vertex") || row.Size() != 3)
    {
      return _reader.Fail(row, "a vertex is an array of three whole numbers");
    }
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
      std::int64_t value = 0;
      if (!_reader.IntegerAt(row.At(axis), "a vertex", -MAX_DISTANCE_SUBUNITS, MAX_DISTANCE_SUBUNITS, value))
      {
        return false;
      }
      position[axis] = static_cast<std::int32_t>(value);
    }
    _out.vertices.push_back(ModelVertex{position[0], position[1], position[2]});
  }

  const std::uint32_t vertexCount = static_cast<std::uint32_t>(_out.vertices.size());
  for (std::size_t index = 0; index < triangles->Size(); ++index)
  {
    const Neuron::JsonValue& row = triangles->At(index);
    ModelTriangle triangle{};
    std::array<std::uint32_t, 3> corners{};
    std::array<std::int32_t, 4> color{};
    std::uint32_t fragment = 0;
    if (!_reader.Object(row, "a triangle") || !_reader.UInt32(row, "a", 0, vertexCount == 0 ? 0 : vertexCount - 1, corners[0]) ||
        !_reader.UInt32(row, "b", 0, vertexCount == 0 ? 0 : vertexCount - 1, corners[1]) ||
        !_reader.UInt32(row, "c", 0, vertexCount == 0 ? 0 : vertexCount - 1, corners[2]) ||
        !_reader.IntegerArray(row, "color", 4, 0, 255, color) || !_reader.UInt32(row, "fragment", 0, 65535, fragment))
    {
      return false;
    }
    triangle.a = static_cast<std::uint16_t>(corners[0]);
    triangle.b = static_cast<std::uint16_t>(corners[1]);
    triangle.c = static_cast<std::uint16_t>(corners[2]);
    for (std::size_t channel = 0; channel < 4; ++channel)
    {
      triangle.color[channel] = static_cast<std::uint8_t>(color[channel]);
    }
    triangle.fragment = static_cast<std::uint16_t>(fragment);
    _out.triangles.push_back(triangle);
  }

  const Neuron::JsonValue* markers = _document.Find("markers");
  if (markers != nullptr && !markers->IsNull())
  {
    if (!_reader.Array(*markers, "'markers'"))
    {
      return false;
    }
    for (std::size_t index = 0; index < markers->Size(); ++index)
    {
      const Neuron::JsonValue& row = markers->At(index);
      ModelMarker marker{};
      std::array<std::int32_t, 3> position{};
      std::uint32_t heading = 0;
      std::uint32_t pitch = 0;
      if (!_reader.Object(row, "a marker") || !_reader.String(row, "name", marker.name) ||
          !_reader.IntegerArray(row, "position", 3, -MAX_DISTANCE_SUBUNITS, MAX_DISTANCE_SUBUNITS, position) ||
          !_reader.UInt32(row, "headingBinaryAngle", 0, 65535, heading) || !_reader.UInt32(row, "pitchBinaryAngle", 0, 65535, pitch))
      {
        return false;
      }
      marker.position = ModelVertex{position[0], position[1], position[2]};
      marker.headingBinaryAngle = static_cast<std::uint16_t>(heading);
      marker.pitchBinaryAngle = static_cast<std::uint16_t>(pitch);
      _out.markers.push_back(std::move(marker));
    }
  }

  const Neuron::JsonValue* fragments = _document.Find("fragments");
  if (fragments != nullptr && !fragments->IsNull())
  {
    if (!_reader.Array(*fragments, "'fragments'"))
    {
      return false;
    }
    for (std::size_t index = 0; index < fragments->Size(); ++index)
    {
      const Neuron::JsonValue& row = fragments->At(index);
      ModelFragment piece{};
      std::array<std::int32_t, 3> center{};
      if (!_reader.Object(row, "a fragment") || !_reader.String(row, "name", piece.name) ||
          !_reader.IntegerArray(row, "center", 3, -MAX_DISTANCE_SUBUNITS, MAX_DISTANCE_SUBUNITS, center))
      {
        return false;
      }
      piece.center = ModelVertex{center[0], center[1], center[2]};
      _out.fragments.push_back(std::move(piece));
    }
  }
  _reader.Record(_document, _out.id);
  return true;
}

/// Parses one file and hands the document to _read. Any failure fills _diagnostics with exactly
/// one entry and returns false.
template <typename Read>
[[nodiscard]] bool LoadFile(const std::filesystem::path& _path, const std::string& _name, std::vector<ContentDiagnostic>& _diagnostics,
                            std::vector<RowOrigin>* _origins, Read _read)
{
  std::string text;
  if (!ReadFileText(_path, text))
  {
    _diagnostics.push_back(ContentDiagnostic{_name, 0, 0, "the file could not be read"});
    return false;
  }
  Neuron::JsonValue document;
  Neuron::JsonError error;
  if (!Neuron::ParseJson(text, _name, document, error))
  {
    _diagnostics.push_back(ContentDiagnostic{error.file, error.line, error.column, error.message});
    return false;
  }
  ContentDiagnostic diagnostic;
  Reader reader(_name, diagnostic);
  reader.RecordInto(_origins);
  if (!_read(reader, document))
  {
    _diagnostics.push_back(std::move(diagnostic));
    return false;
  }
  return true;
}

/// Every .json file of a subdirectory, sorted by name, so that two machines load the same tree in
/// the same order and the content hash of M3 agrees.
[[nodiscard]] std::vector<std::filesystem::path> JsonFilesOf(const std::filesystem::path& _directory)
{
  std::vector<std::filesystem::path> files;
  std::error_code code;
  if (!std::filesystem::is_directory(_directory, code))
  {
    return files;
  }
  for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(_directory, code))
  {
    if (entry.is_regular_file(code) && entry.path().extension() == ".json")
    {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

} // namespace

std::string ContentDiagnostic::ToString() const
{
  if (line == 0)
  {
    return file + ": " + message;
  }
  return file + "(" + std::to_string(line) + "," + std::to_string(column) + "): " + message;
}

bool LoadContent(const std::filesystem::path& _directory, ContentTree& _out, std::vector<ContentDiagnostic>& _diagnostics)
{
  _diagnostics.clear();
  std::error_code code;
  if (!std::filesystem::is_regular_file(_directory / COMPONENTS_FILE, code))
  {
    _diagnostics.push_back(ContentDiagnostic{_directory.string(), 0, 0, "this is not a content directory: it holds no Components.json"});
    return false;
  }

  ContentTree tree;
  const auto table = [&](const char* _name, auto _read) { return LoadFile(_directory / _name, _name, _diagnostics, &tree.origins, _read); };

  if (!table(COMPONENTS_FILE,
             [&](Reader& _reader, const Neuron::JsonValue& _document) { return ReadComponents(_reader, _document, tree.components); }) ||
      !table(STRUCTURES_FILE,
             [&](Reader& _reader, const Neuron::JsonValue& _document) { return ReadStructures(_reader, _document, tree.structures); }) ||
      !table(RESEARCH_FILE,
             [&](Reader& _reader, const Neuron::JsonValue& _document) { return ReadResearch(_reader, _document, tree.research); }) ||
      !table(DAMAGE_FILE,
             [&](Reader& _reader, const Neuron::JsonValue& _document) { return ReadDamage(_reader, _document, tree.damage); }) ||
      !table(BIOMES_FILE,
             [&](Reader& _reader, const Neuron::JsonValue& _document) { return ReadBiomes(_reader, _document, tree.biomes); }) ||
      !table(INTERFACE_FILE,
             [&](Reader& _reader, const Neuron::JsonValue& _document) { return ReadInterface(_reader, _document, tree.ui); }) ||
      !table(SOUNDS_FILE, [&](Reader& _reader, const Neuron::JsonValue& _document) { return ReadSounds(_reader, _document, tree.sounds); }))
  {
    return false;
  }

  for (const std::filesystem::path& file : JsonFilesOf(_directory / LANDSCAPES_DIRECTORY))
  {
    LandscapeDefinition landscape{};
    const std::string name = std::string(LANDSCAPES_DIRECTORY) + "/" + file.filename().string();
    if (!LoadFile(file, name, _diagnostics, &tree.origins,
                  [&](Reader& _reader, const Neuron::JsonValue& _document) { return ReadLandscape(_reader, _document, landscape); }))
    {
      return false;
    }
    tree.origins.push_back(RowOrigin{file.stem().string(), name, 1});
    tree.landscapes.push_back(std::move(landscape));
    tree.landscapeIds.push_back(file.stem().string());
  }
  for (const std::filesystem::path& file : JsonFilesOf(_directory / STAMPS_DIRECTORY))
  {
    StampDesc stamp{};
    const std::string name = std::string(STAMPS_DIRECTORY) + "/" + file.filename().string();
    if (!LoadFile(file, name, _diagnostics, &tree.origins,
                  [&](Reader& _reader, const Neuron::JsonValue& _document) { return ReadStamp(_reader, _document, stamp); }))
    {
      return false;
    }
    tree.stamps.push_back(std::move(stamp));
  }
  for (const std::filesystem::path& file : JsonFilesOf(_directory / MODELS_DIRECTORY))
  {
    ModelDesc model{};
    const std::string name = std::string(MODELS_DIRECTORY) + "/" + file.filename().string();
    if (!LoadFile(file, name, _diagnostics, &tree.origins,
                  [&](Reader& _reader, const Neuron::JsonValue& _document) { return ReadModel(_reader, _document, model); }))
    {
      return false;
    }
    tree.models.push_back(std::move(model));
  }

  _out = std::move(tree);
  return true;
}

} // namespace Frontier
