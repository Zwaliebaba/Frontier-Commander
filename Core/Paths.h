#pragma once

#include <filesystem>

// The two roots every path resolves from (TechnicalDesign.md §9): the executable's own directory
// for what the game reads, and the user's profile for what it writes. Nothing resolves against
// the working directory. This header includes no platform header, so that every project may use
// it; Paths.cpp is where Windows is asked.

namespace Neuron
{

class Paths
{
public:
  /// The directory holding the running executable, absolute.
  [[nodiscard]] static std::filesystem::path ExecutableDirectory();

  /// <executable directory>\Content, read-only content.
  [[nodiscard]] static std::filesystem::path ContentDirectory();

  /// <executable directory>\Mods.
  [[nodiscard]] static std::filesystem::path ModsDirectory();

  /// %LOCALAPPDATA%\FrontierCommander, created on first use; everything the game writes goes
  /// under it.
  [[nodiscard]] static std::filesystem::path UserDirectory();

  /// Redirects the two roots, so that a test never touches the real executable directory or
  /// the real user directory (TechnicalDesign.md §10). Empty paths mean no override.
  static void OverrideForTests(const std::filesystem::path& _executableDirectory, const std::filesystem::path& _userRoot);
  static void ClearOverride();

private:
  [[nodiscard]] static std::filesystem::path UserRoot();
};

} // namespace Neuron
