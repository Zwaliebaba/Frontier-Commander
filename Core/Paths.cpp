#include "pch.h"

#include "WindowsHeader.h"

#include "Paths.h"
#include "Assert.h"

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace Neuron
{

namespace
{

constexpr const wchar_t* USER_DIRECTORY_NAME = L"FrontierCommander";

std::filesystem::path g_executableOverride;
std::filesystem::path g_userRootOverride;

[[nodiscard]] std::filesystem::path ModulePath()
{
  std::vector<wchar_t> buffer(MAX_PATH);
  for (;;)
  {
    const DWORD length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0)
    {
      return {};
    }
    if (length < buffer.size())
    {
      return std::filesystem::path(std::wstring(buffer.data(), length));
    }
    buffer.resize(buffer.size() * 2); // truncated: the path is longer than the buffer
  }
}

[[nodiscard]] std::filesystem::path EnvironmentDirectory(const wchar_t* _name)
{
  const DWORD needed = ::GetEnvironmentVariableW(_name, nullptr, 0);
  if (needed == 0)
  {
    return {};
  }
  std::wstring value(needed, L'\0');
  const DWORD written = ::GetEnvironmentVariableW(_name, value.data(), needed);
  if (written == 0 || written >= needed)
  {
    return {};
  }
  value.resize(written);
  return std::filesystem::path(value);
}

} // namespace

std::filesystem::path Paths::ExecutableDirectory()
{
  if (!g_executableOverride.empty())
  {
    return g_executableOverride;
  }
  const std::filesystem::path module = ModulePath();
  FRONTIER_ASSERT(!module.empty());
  return module.parent_path();
}

std::filesystem::path Paths::ContentDirectory()
{
  return ExecutableDirectory() / L"Content";
}

std::filesystem::path Paths::ModsDirectory()
{
  return ExecutableDirectory() / L"Mods";
}

std::filesystem::path Paths::UserRoot()
{
  if (!g_userRootOverride.empty())
  {
    return g_userRootOverride;
  }
  return EnvironmentDirectory(L"LOCALAPPDATA");
}

std::filesystem::path Paths::UserDirectory()
{
  const std::filesystem::path root = UserRoot();
  FRONTIER_ASSERT(!root.empty());
  const std::filesystem::path directory = root / USER_DIRECTORY_NAME;
  std::error_code ignored;
  std::filesystem::create_directories(directory, ignored);
  return directory;
}

void Paths::OverrideForTests(const std::filesystem::path& _executableDirectory, const std::filesystem::path& _userRoot)
{
  g_executableOverride = _executableDirectory;
  g_userRootOverride = _userRoot;
}

void Paths::ClearOverride()
{
  g_executableOverride.clear();
  g_userRootOverride.clear();
}

} // namespace Neuron
