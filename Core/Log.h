#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

// A levelled log to a file under the user directory and, in Debug, to the debugger output
// (TechnicalDesign.md §9). Lines carry the simulation tick when the host has set one, else a
// wall-clock time of day; the host loop is the only caller of SetTick, so that the tick is the
// clock wherever there is one (AGENTS.md R16). Safe to call before any window exists and before
// Open, when only the debugger output receives the line. Not thread-safe: the host thread of
// M1 gets its own log file.

namespace Neuron
{

enum class LogLevel : std::uint8_t
{
  Debug,
  Info,
  Warning,
  Error
};

class Log
{
public:
  /// Opens the file, creating its directory; a file already larger than _rotateAtBytes is
  /// renamed to <name>.1 first, replacing an older .1, so a log never grows without bound.
  [[nodiscard]] static bool Open(const std::filesystem::path& _file, std::uintmax_t _rotateAtBytes = 4u * 1024u * 1024u);
  static void Close();

  /// Lines below the minimum are dropped; Info by default.
  static void SetMinimumLevel(LogLevel _level);

  /// The tick the next lines carry; UINT32_MAX (the default) means none, and the time of day is
  /// written instead.
  static void SetTick(std::uint32_t _tick);

  static void Write(LogLevel _level, std::string_view _message);

  [[nodiscard]] static bool IsOpen();
  [[nodiscard]] static std::uintmax_t BytesWritten();

  static constexpr std::uint32_t NO_TICK = 0xFFFFFFFFu;
};

} // namespace Neuron
