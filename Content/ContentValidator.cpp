#include "pch.h"

#include "ContentValidator.h"

#include <filesystem>
#include <map>
#include <set>

namespace Frontier
{
namespace
{

/// A fault on a row, reported at the line the loader read that row from (ContentTree::origins), so
/// that a fault spanning files points at a line exactly as a fault inside one file does. A row the
/// tree has no origin for — one a test built in memory — is reported against the file it belongs
/// to with no line.
void Report(const ContentTree& _tree, std::vector<ContentDiagnostic>& _diagnostics, const char* _file, const std::string& _id,
            const std::string& _message)
{
  const RowOrigin* origin = _tree.FindOrigin(_id);
  const std::string file = origin == nullptr ? std::string(_file) : origin->file;
  const int line = origin == nullptr ? 0 : origin->line;
  _diagnostics.push_back(ContentDiagnostic{file, line, 1, "'" + _id + "': " + _message});
}

/// No id is used twice. Every row the loader read is in origins, in load order, so a duplicate is
/// a second entry with the same id and its own line — and the diagnostic names the line of the
/// second, which is the one a designer deletes.
void CollectIds(const ContentTree& _tree, std::vector<ContentDiagnostic>& _diagnostics)
{
  std::set<std::string> seen;
  for (const RowOrigin& origin : _tree.origins)
  {
    if (origin.id.empty())
    {
      _diagnostics.push_back(ContentDiagnostic{origin.file, origin.line, 1, "a row has an empty id"});
      continue;
    }
    if (!seen.insert(origin.id).second)
    {
      const RowOrigin* first = _tree.FindOrigin(origin.id);
      _diagnostics.push_back(
        ContentDiagnostic{origin.file, origin.line, 1,
                          "'" + origin.id + "': this id is already used at " + first->file + "(" + std::to_string(first->line) + ")"});
    }
  }
}

/// Every research prerequisite exists, every unlock names something unlockable, and the tree has
/// no cycle. The cycle search is a depth-first walk with a mark per item, which names one item
/// on the cycle rather than the whole ring: naming one is enough to find it, and the whole ring
/// is a list that grows with the tree.
void CheckResearch(const ContentTree& _tree, std::vector<ContentDiagnostic>& _diagnostics)
{
  for (const ResearchItemDesc& item : _tree.research)
  {
    for (const std::string& prerequisite : item.prerequisites)
    {
      if (_tree.FindResearch(prerequisite) == nullptr)
      {
        Report(_tree, _diagnostics, RESEARCH_FILE, item.id, "the prerequisite '" + prerequisite + "' is not a research item");
      }
      if (prerequisite == item.id)
      {
        Report(_tree, _diagnostics, RESEARCH_FILE, item.id, "an item is not its own prerequisite");
      }
    }
    if (item.effect == ResearchEffect::Unlock && !_tree.IsUnlockable(item.unlocks))
    {
      Report(_tree, _diagnostics, RESEARCH_FILE, item.id,
             "it unlocks '" + item.unlocks + "', which is not a chassis, a drive, a module, a structure or a structure module");
    }
    if (item.effect != ResearchEffect::Unlock && !item.unlocks.empty())
    {
      Report(_tree, _diagnostics, RESEARCH_FILE, item.id, "an upgrade names no unlock");
    }
  }

  // 0 unvisited, 1 on the stack, 2 finished.
  std::map<std::string, int> visited;
  for (const ResearchItemDesc& item : _tree.research)
  {
    visited[item.id] = 0;
  }
  const auto walk = [&](auto&& _self, const std::string& _id) -> bool
  {
    int& mark = visited[_id];
    if (mark == 1)
    {
      return false;
    }
    if (mark == 2)
    {
      return true;
    }
    mark = 1;
    const ResearchItemDesc* item = _tree.FindResearch(_id);
    if (item != nullptr)
    {
      for (const std::string& prerequisite : item->prerequisites)
      {
        if (_tree.FindResearch(prerequisite) != nullptr && !_self(_self, prerequisite))
        {
          mark = 2;
          return false;
        }
      }
    }
    mark = 2;
    return true;
  };
  for (const ResearchItemDesc& item : _tree.research)
  {
    if (visited[item.id] == 0 && !walk(walk, item.id))
    {
      Report(_tree, _diagnostics, RESEARCH_FILE, item.id, "its prerequisites lead back to it: the research tree has a cycle here");
    }
  }
}

/// Every id a row names is defined: a component's unlock, a structure's modules and weapon, a
/// landscape's palette.
void CheckReferences(const ContentTree& _tree, std::vector<ContentDiagnostic>& _diagnostics)
{
  const auto unlock = [&](const std::string& _id, const std::string& _unlockedBy, const char* _file)
  {
    if (!_unlockedBy.empty() && _tree.FindResearch(_unlockedBy) == nullptr)
    {
      Report(_tree, _diagnostics, _file, _id, "it is unlocked by '" + _unlockedBy + "', which is not a research item");
    }
  };
  for (const ChassisDesc& row : _tree.components.chassis)
  {
    unlock(row.id, row.unlockedBy, COMPONENTS_FILE);
  }
  for (const DriveDesc& row : _tree.components.drives)
  {
    unlock(row.id, row.unlockedBy, COMPONENTS_FILE);
  }
  for (const ModuleDesc& row : _tree.components.modules)
  {
    unlock(row.id, row.unlockedBy, COMPONENTS_FILE);
    if (row.systemKind == SystemKind::None && row.longRangeSubunits < row.shortRangeSubunits)
    {
      Report(_tree, _diagnostics, COMPONENTS_FILE, row.id, "its long range is under its short range");
    }
    if (row.fireKind == FireKind::Indirect && row.minimumRangeSubunits >= row.longRangeSubunits)
    {
      Report(_tree, _diagnostics, COMPONENTS_FILE, row.id, "an indirect weapon's minimum range is under its long range");
    }
  }
  for (const StructureDesc& row : _tree.structures.structures)
  {
    unlock(row.id, row.unlockedBy, STRUCTURES_FILE);
    for (const std::string& module : row.modules)
    {
      if (_tree.FindStructureModule(module) == nullptr)
      {
        Report(_tree, _diagnostics, STRUCTURES_FILE, row.id, "it takes the module '" + module + "', which is not a structure module");
      }
    }
    if (row.modules.size() > row.moduleSlots)
    {
      Report(_tree, _diagnostics, STRUCTURES_FILE, row.id, "it lists more modules than it has slots");
    }
    if (!row.weapon.empty() && _tree.FindModule(row.weapon) == nullptr)
    {
      Report(_tree, _diagnostics, STRUCTURES_FILE, row.id, "it carries the weapon '" + row.weapon + "', which is not a module");
    }
  }
  for (const StructureModuleDesc& row : _tree.structures.modules)
  {
    unlock(row.id, row.unlockedBy, STRUCTURES_FILE);
  }
  for (std::size_t index = 0; index < _tree.landscapes.size(); ++index)
  {
    const LandscapeDefinition& landscape = _tree.landscapes[index];
    const std::string& name = _tree.landscapeIds[index];
    if (_tree.FindBiome(landscape.palette) == nullptr)
    {
      Report(_tree, _diagnostics, LANDSCAPES_DIRECTORY, name, "its palette '" + landscape.palette + "' is not a biome");
    }
    if (landscape.starts.empty())
    {
      Report(_tree, _diagnostics, LANDSCAPES_DIRECTORY, name, "a landscape has at least one start");
    }
    for (std::size_t tileIndex = 0; tileIndex < landscape.tiles.size(); ++tileIndex)
    {
      const std::string& tilePalette = landscape.tiles[tileIndex].palette;
      if (!tilePalette.empty() && _tree.FindBiome(tilePalette) == nullptr)
      {
        Report(_tree, _diagnostics, LANDSCAPES_DIRECTORY, name,
               "tile " + std::to_string(tileIndex) + " is coloured by '" + tilePalette + "', which is not a biome");
      }
    }
  }
}

/// Every model, texture and sound a row names is on disk. Skipped when no directory is given,
/// which is how a test validates a tree it built in memory.
void CheckAssets(const ContentTree& _tree, const std::filesystem::path& _directory, std::vector<ContentDiagnostic>& _diagnostics)
{
  if (_directory.empty())
  {
    return;
  }
  const auto model = [&](const std::string& _id, const std::string& _model, const char* _file)
  {
    if (!_model.empty() && _tree.FindModel(_model) == nullptr)
    {
      Report(_tree, _diagnostics, _file, _id, "it names the model '" + _model + "', which no file under Models defines");
    }
  };
  for (const ChassisDesc& row : _tree.components.chassis)
  {
    model(row.id, row.model, COMPONENTS_FILE);
  }
  for (const DriveDesc& row : _tree.components.drives)
  {
    model(row.id, row.model, COMPONENTS_FILE);
  }
  for (const ModuleDesc& row : _tree.components.modules)
  {
    model(row.id, row.model, COMPONENTS_FILE);
  }
  for (const StructureDesc& row : _tree.structures.structures)
  {
    model(row.id, row.model, STRUCTURES_FILE);
  }
  for (const StructureModuleDesc& row : _tree.structures.modules)
  {
    model(row.id, row.model, STRUCTURES_FILE);
  }

  std::error_code code;
  const auto texture = [&](const std::string& _id, const std::string& _name)
  {
    if (_name.empty())
    {
      return;
    }
    if (!std::filesystem::is_regular_file(_directory / TEXTURES_DIRECTORY / _name, code))
    {
      Report(_tree, _diagnostics, BIOMES_FILE, _id, "the texture '" + _name + "' is not under Textures");
    }
  };
  for (const BiomeDesc& row : _tree.biomes)
  {
    texture(row.id, row.paletteTexture);
    texture(row.id, row.waterTexture);
    texture(row.id, row.waveTexture);
  }
  for (const SoundEventDesc& row : _tree.sounds)
  {
    for (const std::string& wave : row.waves)
    {
      if (!std::filesystem::is_regular_file(_directory / SOUNDS_DIRECTORY / wave, code))
      {
        Report(_tree, _diagnostics, SOUNDS_FILE, row.id, "the wave '" + wave + "' is not under Sounds");
      }
    }
  }
}

} // namespace

bool ValidateContent(const ContentTree& _tree, const std::filesystem::path& _directory, std::vector<ContentDiagnostic>& _diagnostics)
{
  _diagnostics.clear();
  CollectIds(_tree, _diagnostics);
  CheckResearch(_tree, _diagnostics);
  CheckReferences(_tree, _diagnostics);
  CheckAssets(_tree, _directory, _diagnostics);
  return _diagnostics.empty();
}

} // namespace Frontier
