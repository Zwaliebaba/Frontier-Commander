#include "pch.h"

#include "Paths.h"

#include <filesystem>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace CoreTests
{

namespace
{

// A scratch directory of this test's own under the system temporary directory, so that no test
// touches the real executable or user directory (TechnicalDesign.md §10).
struct ScratchDirectory
{
  std::filesystem::path path;

  ScratchDirectory()
  {
    path =
      std::filesystem::temp_directory_path() / L"FrontierCommanderTests" / std::filesystem::path(std::to_wstring(::GetCurrentProcessId()));
    std::filesystem::create_directories(path);
  }

  ~ScratchDirectory()
  {
    Neuron::Paths::ClearOverride();
    try
    {
      std::error_code ignored;
      std::filesystem::remove_all(path, ignored);
    }
    catch (...)
    {
      // A scratch directory left behind is not a test failure, and a destructor must not throw.
      Logger::WriteMessage("the scratch directory could not be removed");
    }
  }
};

} // namespace

TEST_CLASS(PathsTests)
{
public:
  TEST_METHOD(TheRealExecutableDirectoryIsAbsoluteAndExists)
  {
    Neuron::Paths::ClearOverride();
    const std::filesystem::path directory = Neuron::Paths::ExecutableDirectory();
    Assert::IsTrue(directory.is_absolute());
    Assert::IsTrue(std::filesystem::is_directory(directory));
    Assert::IsTrue(Neuron::Paths::ContentDirectory() == directory / L"Content");
    Assert::IsTrue(Neuron::Paths::ModsDirectory() == directory / L"Mods");
  }

  TEST_METHOD(TheOverrideRedirectsBothRootsAndCreatesTheUserDirectory)
  {
    ScratchDirectory scratch;
    const std::filesystem::path executable = scratch.path / L"Game";
    const std::filesystem::path userRoot = scratch.path / L"Profile";
    Neuron::Paths::OverrideForTests(executable, userRoot);
    Assert::IsTrue(Neuron::Paths::ExecutableDirectory() == executable);
    Assert::IsTrue(Neuron::Paths::ContentDirectory() == executable / L"Content");
    const std::filesystem::path user = Neuron::Paths::UserDirectory();
    Assert::IsTrue(user == userRoot / L"FrontierCommander");
    Assert::IsTrue(std::filesystem::is_directory(user), L"the user directory was not created on first use");
    Neuron::Paths::ClearOverride();
    Assert::IsFalse(Neuron::Paths::ExecutableDirectory() == executable);
  }
};

} // namespace CoreTests
