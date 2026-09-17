#include "pch.h"

#include "WindowsHeader.h"

#include "Log.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <system_error>

namespace Neuron
{

namespace
{

std::ofstream g_file;
std::uintmax_t g_bytesWritten = 0;
LogLevel g_minimumLevel = LogLevel::Info;
std::uint32_t g_tick = Log::NO_TICK;

[[nodiscard]] const char* LevelName(LogLevel _level) noexcept
{
  switch (_level)
  {
  case LogLevel::Debug:
    return "debug";
  case LogLevel::Info:
    return "info";
  case LogLevel::Warning:
    return "warning";
  case LogLevel::Error:
    return "error";
  }
  return "?";
}

[[nodiscard]] std::string Prefix(LogLevel _level)
{
  char buffer[64];
  if (g_tick != Log::NO_TICK)
  {
    std::snprintf(buffer, sizeof buffer, "[tick %u] [%s] ", static_cast<unsigned>(g_tick), LevelName(_level));
    return buffer;
  }
  const auto sinceEpoch =
    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  const auto ofDay = static_cast<unsigned long long>(sinceEpoch % (24LL * 60 * 60 * 1000));
  std::snprintf(buffer, sizeof buffer, "[%02u:%02u:%02u.%03u] [%s] ", static_cast<unsigned>(ofDay / 3600000u),
                static_cast<unsigned>(ofDay / 60000u % 60u), static_cast<unsigned>(ofDay / 1000u % 60u),
                static_cast<unsigned>(ofDay % 1000u), LevelName(_level));
  return buffer;
}

} // namespace

bool Log::Open(const std::filesystem::path& _file, std::uintmax_t _rotateAtBytes)
{
  Close();
  std::error_code ignored;
  std::filesystem::create_directories(_file.parent_path(), ignored);
  const std::uintmax_t existing = std::filesystem::file_size(_file, ignored);
  if (!ignored && existing > _rotateAtBytes)
  {
    std::filesystem::path rotated = _file;
    rotated += L".1";
    std::filesystem::remove(rotated, ignored);
    std::filesystem::rename(_file, rotated, ignored);
  }
  // Binary, so that the line ending is the one written here on every platform and the bytes
  // written equal the file's growth; the CRT's text mode would rewrite '\n' as "\r\n".
  g_file.open(_file, std::ios::out | std::ios::app | std::ios::binary);
  g_bytesWritten = 0;
  return g_file.is_open();
}

void Log::Close()
{
  if (g_file.is_open())
  {
    g_file.close();
  }
}

void Log::SetMinimumLevel(LogLevel _level)
{
  g_minimumLevel = _level;
}

void Log::SetTick(std::uint32_t _tick)
{
  g_tick = _tick;
}

void Log::Write(LogLevel _level, std::string_view _message)
{
  if (_level < g_minimumLevel)
  {
    return;
  }
  std::string line = Prefix(_level);
  line.append(_message.data(), _message.size());
  line.append("\r\n");
#if defined(_DEBUG)
  ::OutputDebugStringA(line.c_str());
#endif
  if (g_file.is_open())
  {
    g_file.write(line.data(), static_cast<std::streamsize>(line.size()));
    g_file.flush();
    g_bytesWritten += line.size();
  }
}

bool Log::IsOpen()
{
  return g_file.is_open();
}

std::uintmax_t Log::BytesWritten()
{
  return g_bytesWritten;
}

} // namespace Neuron
