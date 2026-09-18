#include "pch.h"

#include "ContentLoader.h"
#include "ContentValidator.h"
#include "Paths.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <exception>
#include <string>
#include <vector>

namespace
{

constexpr const char* VERSION_LINE = "FrontierHost 0.0 (m0-foundation)";
constexpr const char* USAGE = "usage: FrontierHost --version | --validate [directory]";

constexpr int EXIT_CLEAN = 0;
constexpr int EXIT_CONTENT_REFUSED = 1;
constexpr int EXIT_BAD_COMMAND_LINE = 2;

/// Loads and validates a content directory with the same code the game runs before its first tick
/// (TechnicalDesign.md §8), printing every diagnostic in the form the build tools print, so that a
/// table edit that would break the game breaks the build first.
[[nodiscard]] int Validate(const std::filesystem::path& _directory)
{
  std::vector<Frontier::ContentDiagnostic> diagnostics;
  Frontier::ContentTree tree;
  if (!Frontier::LoadContent(_directory, tree, diagnostics))
  {
    for (const Frontier::ContentDiagnostic& diagnostic : diagnostics)
    {
      std::printf("%s\n", diagnostic.ToString().c_str());
    }
    std::printf("FrontierHost: the content could not be read.\n");
    return EXIT_CONTENT_REFUSED;
  }
  if (!Frontier::ValidateContent(tree, _directory, diagnostics))
  {
    for (const Frontier::ContentDiagnostic& diagnostic : diagnostics)
    {
      std::printf("%s\n", diagnostic.ToString().c_str());
    }
    std::printf("FrontierHost: %zu finding(s) in %zu row(s).\n", diagnostics.size(), tree.RowCount());
    return EXIT_CONTENT_REFUSED;
  }
  std::printf("FrontierHost: %zu row(s) in %s, no findings.\n", tree.RowCount(), _directory.string().c_str());
  return EXIT_CLEAN;
}

[[nodiscard]] int Run(int _argumentCount, char** _arguments)
{
  for (int index = 1; index < _argumentCount; ++index)
  {
    if (std::strcmp(_arguments[index], "--version") == 0)
    {
      std::puts(VERSION_LINE);
      return EXIT_CLEAN;
    }
    if (std::strcmp(_arguments[index], "--validate") == 0)
    {
      const bool given = index + 1 < _argumentCount && _arguments[index + 1][0] != '-';
      const std::filesystem::path directory = given ? std::filesystem::path(_arguments[index + 1]) : Neuron::Paths::GameDataDirectory();
      return Validate(directory);
    }
  }
  std::puts(USAGE);
  return EXIT_BAD_COMMAND_LINE;
}

} // namespace

// The headless host's entry point. --version prints one line and returns 0 so that a script can
// tell the executable runs; --validate checks a content directory (m1-vertical-slice/C1); hosting
// a match arrives with m1-vertical-slice/N2 and M3.
//
// Everything is caught here, once. A path built from a command-line argument throws on bytes that
// are not valid in the system's code page, and a std::filesystem call throws on allocation even
// where it takes an error code; an exception leaving main ends the process without a diagnostic,
// which is the one thing a validation tool must not do.
int main(int _argumentCount, char** _arguments)
{
  try
  {
    return Run(_argumentCount, _arguments);
  }
  catch (const std::exception& error)
  {
    std::printf("FrontierHost: %s\n", error.what());
    return EXIT_CONTENT_REFUSED;
  }
  catch (...)
  {
    std::puts("FrontierHost: an unknown exception escaped.");
    return EXIT_CONTENT_REFUSED;
  }
}
