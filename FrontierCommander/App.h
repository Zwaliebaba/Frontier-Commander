#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace Frontier
{

/// What the command line asked for: `--warp` for the software rasteriser, `--capture <frames>
/// <directory>` for the headless path (TechnicalDesign.md §6.1).
struct LaunchOptions
{
  bool warp = false;
  bool capture = false;
  std::uint32_t captureFrames = 0;
  std::filesystem::path captureDirectory;
};

/// The arguments as CommandLineToArgvW splits them, the executable first; false for anything
/// that is not the two options above, spelled exactly.
[[nodiscard]] bool ParseCommandLine(std::span<const std::wstring> _arguments, LaunchOptions& _options);

/// The process exit codes. A capture that saw a debug-layer warning or error exits with
/// EXIT_DEBUG_MESSAGES, which is what fails the CI job (TechnicalDesign.md §10).
inline constexpr int EXIT_CLEAN = 0;
inline constexpr int EXIT_FAILED = 1;
inline constexpr int EXIT_DEBUG_MESSAGES = 2;

/// The game: a window over the primary monitor with the scene target presented scaled into it, or,
/// with --capture, no window and no swap chain but the scene target written to disk every hundredth
/// frame (ADR-004). A failed HRESULT anywhere below is caught once here, logged and fatal.
class App
{
public:
  explicit App(const LaunchOptions& _options);

  [[nodiscard]] int Run();

private:
  [[nodiscard]] int RunWindowed();
  [[nodiscard]] int RunCapture();

  LaunchOptions m_options;
};

} // namespace Frontier
