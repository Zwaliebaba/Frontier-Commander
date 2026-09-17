#pragma once

#include "ContentTree.h"

#include <filesystem>
#include <string>
#include <vector>

// The one reader of Content\ (TechnicalDesign.md §8). It reads the tree file by file through
// Core's JSON reader and stops at the first file that fails, naming the file, the line and the
// column; a document is accepted whole or rejected whole, and a tree that fails leaves the
// ContentTree untouched.
//
// Loading is only half of it: ContentValidator checks what loading cannot, because a cross-file
// rule has no single file to blame until every file is in. FrontierHost --validate runs both, and
// so does the game before its first tick.

namespace Frontier
{

/// One thing wrong, in the form the build tools print: "file(line,column): message". A loader
/// diagnostic carries a line; a validator diagnostic carries the file whose row is wrong and a
/// line where the reader recorded one, 0 otherwise.
struct ContentDiagnostic
{
  std::string file;
  int line = 0;
  int column = 0;
  std::string message;

  [[nodiscard]] std::string ToString() const;
};

/// Reads every file of a content directory into _out. On failure _out is untouched, _diagnostics
/// holds exactly one entry — the first file that failed — and the result is false. A directory
/// with no Components.json is not a content directory and fails as such.
[[nodiscard]] bool LoadContent(const std::filesystem::path& _directory, ContentTree& _out, std::vector<ContentDiagnostic>& _diagnostics);

/// The file names and subdirectories LoadContent reads, in the order it reads them.
inline constexpr const char* COMPONENTS_FILE = "Components.json";
inline constexpr const char* STRUCTURES_FILE = "Structures.json";
inline constexpr const char* RESEARCH_FILE = "Research.json";
inline constexpr const char* DAMAGE_FILE = "Damage.json";
inline constexpr const char* BIOMES_FILE = "Biomes.json";
inline constexpr const char* SOUNDS_FILE = "Sounds.json";
inline constexpr const char* LANDSCAPES_DIRECTORY = "Landscapes";
inline constexpr const char* STAMPS_DIRECTORY = "Stamps";
inline constexpr const char* MODELS_DIRECTORY = "Models";
inline constexpr const char* TEXTURES_DIRECTORY = "Textures";
inline constexpr const char* SOUNDS_DIRECTORY = "Sounds";

} // namespace Frontier
