#pragma once

#include "BiomeDesc.h"
#include "ComponentDesc.h"
#include "DamageTable.h"
#include "LandscapeDefinition.h"
#include "ModelDesc.h"
#include "ResearchItemDesc.h"
#include "SoundEventDesc.h"
#include "StampDesc.h"
#include "StructureDesc.h"

#include <string>
#include <string_view>
#include <vector>

// Everything Content\ holds, loaded (TechnicalDesign.md §8). One tree per process: the host loads
// it before the first tick and the client is handed the same one, and nothing writes to it after
// the load. Rows keep the order their files declare, so that a lookup by id is a search and a
// listing is a loop, and the content hash of M3 has a stable order to digest.
//
// The find functions return a pointer into the tree or nullptr, and the pointer is stable because
// nothing is inserted after loading.

namespace Frontier
{

/// Every table is a vector of rows with an id; the search is linear because a content table is tens
/// of rows and a map would cost more to build than every lookup a match makes. Free, so that
/// ContentTree stays a plain aggregate (AGENTS.md R8).
template <typename Row> [[nodiscard]] const Row* FindById(const std::vector<Row>& _rows, std::string_view _id) noexcept
{
  for (const Row& row : _rows)
  {
    if (row.id == _id)
    {
      return &row;
    }
  }
  return nullptr;
}

/// Where a row was read from. Rows themselves carry no line, because a row is compared, hashed
/// and replicated and a line is none of those things; the tree keeps the lines beside them, in
/// load order, so that ContentValidator can name a line for a fault that spans files — a duplicate
/// id, a prerequisite nothing defines — exactly as the loader names one for a fault inside a file.
struct RowOrigin
{
  std::string id;
  std::string file;
  int line;

  [[nodiscard]] bool operator==(const RowOrigin&) const noexcept = default;
};

struct ContentTree
{
  ComponentTables components;
  StructureTables structures;
  std::vector<ResearchItemDesc> research;
  DamageTable damage;
  std::vector<BiomeDesc> biomes;
  std::vector<LandscapeDefinition> landscapes;
  std::vector<std::string> landscapeIds; ///< The file stem of each landscape, in the same order
  std::vector<StampDesc> stamps;
  std::vector<ModelDesc> models;
  std::vector<SoundEventDesc> sounds;
  std::vector<RowOrigin> origins; ///< Every row above, in load order, with the file and line it came from

  [[nodiscard]] const ChassisDesc* FindChassis(std::string_view _id) const noexcept
  {
    return FindById(components.chassis, _id);
  }
  [[nodiscard]] const DriveDesc* FindDrive(std::string_view _id) const noexcept
  {
    return FindById(components.drives, _id);
  }
  [[nodiscard]] const ModuleDesc* FindModule(std::string_view _id) const noexcept
  {
    return FindById(components.modules, _id);
  }
  [[nodiscard]] const StructureDesc* FindStructure(std::string_view _id) const noexcept
  {
    return FindById(structures.structures, _id);
  }
  [[nodiscard]] const StructureModuleDesc* FindStructureModule(std::string_view _id) const noexcept
  {
    return FindById(structures.modules, _id);
  }
  [[nodiscard]] const ResearchItemDesc* FindResearch(std::string_view _id) const noexcept
  {
    return FindById(research, _id);
  }
  [[nodiscard]] const BiomeDesc* FindBiome(std::string_view _id) const noexcept
  {
    return FindById(biomes, _id);
  }
  [[nodiscard]] const ModelDesc* FindModel(std::string_view _id) const noexcept
  {
    return FindById(models, _id);
  }
  [[nodiscard]] const SoundEventDesc* FindSound(std::string_view _id) const noexcept
  {
    return FindById(sounds, _id);
  }
  [[nodiscard]] const StampDesc* FindStamp(std::string_view _id) const noexcept
  {
    return FindById(stamps, _id);
  }

  /// Whether the id names a chassis, a drive, a module, a structure or a structure module: what a
  /// research item's unlock is allowed to name.
  [[nodiscard]] bool IsUnlockable(std::string_view _id) const noexcept
  {
    return FindChassis(_id) != nullptr || FindDrive(_id) != nullptr || FindModule(_id) != nullptr || FindStructure(_id) != nullptr ||
           FindStructureModule(_id) != nullptr;
  }

  /// Where a row with this id was read from, or nullptr. The first of two rows sharing an id.
  [[nodiscard]] const RowOrigin* FindOrigin(std::string_view _id) const noexcept
  {
    return FindById(origins, _id);
  }

  /// Every row of every table, counted; what the loader logs and the validator reports over.
  [[nodiscard]] std::size_t RowCount() const noexcept
  {
    return components.chassis.size() + components.drives.size() + components.modules.size() + structures.structures.size() +
           structures.modules.size() + research.size() + biomes.size() + landscapes.size() + stamps.size() + models.size() + sounds.size();
  }
};

} // namespace Frontier
