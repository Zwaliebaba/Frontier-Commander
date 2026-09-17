#pragma once

#include "ContentLoader.h"
#include "ContentTree.h"

#include <filesystem>
#include <vector>

// What loading cannot check, because it spans files (TechnicalDesign.md §8): every research
// prerequisite exists and the tree has no cycle, every unlock names a real component or
// structure, every model, texture and sound a row names is on disk, no id is duplicated, every
// number is inside the range its field declares, and every landscape's palette names a biome.
//
// The validator reports every fault it finds rather than the first, because a table edit usually
// breaks several rows and one pass should show them all. FrontierHost --validate prints them and
// exits non-zero; the game refuses to start.

namespace Frontier
{

/// Checks a loaded tree. _directory is where the files came from, so that the row that names a
/// missing model can be told which path was looked for; pass an empty path to skip the checks
/// that touch the disk. Returns true when _diagnostics is left empty.
[[nodiscard]] bool ValidateContent(const ContentTree& _tree, const std::filesystem::path& _directory,
                                   std::vector<ContentDiagnostic>& _diagnostics);

} // namespace Frontier
