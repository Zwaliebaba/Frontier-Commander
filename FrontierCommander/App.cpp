#include "pch.h"

#include "WindowsHeader.h"

#include "App.h"

#include "FrameCapture.h"
#include "GraphicsDevice.h"
#include "Log.h"
#include "Paths.h"
#include "PresentPass.h"
#include "ScaleMode.h"
#include "SceneTarget.h"
#include "SwapChain.h"
#include "Window.h"

#include <cstdio>
#include <cwchar>
#include <exception>
#include <string>
#include <system_error>

namespace Frontier
{

namespace
{

// Until the terrain pass draws over it (m0-foundation/T20): a colour a capture cannot mistake for
// nothing drawn.
constexpr std::array<float, 4> CLEAR_COLOR = {0.05f, 0.10f, 0.20f, 1.0f};
constexpr std::uint32_t CAPTURE_EVERY_FRAMES = 100;

[[nodiscard]] std::string Describe(const winrt::hresult_error& _error)
{
  char code[16];
  std::snprintf(code, sizeof code, "0x%08X", static_cast<unsigned>(static_cast<std::int32_t>(_error.code())));
  return std::string(code) + " " + winrt::to_string(_error.message());
}

[[nodiscard]] int ExitCodeOf(const Neuron::GraphicsDevice& _device)
{
  return _device.DebugMessageCount() == 0 ? EXIT_CLEAN : EXIT_DEBUG_MESSAGES;
}

} // namespace

bool ParseCommandLine(std::span<const std::wstring> _arguments, LaunchOptions& _options)
{
  for (std::size_t index = 1; index < _arguments.size(); ++index)
  {
    const std::wstring& argument = _arguments[index];
    if (argument == L"--warp")
    {
      _options.warp = true;
      continue;
    }
    if (argument == L"--capture" && index + 2 < _arguments.size())
    {
      wchar_t* end = nullptr;
      const unsigned long frames = std::wcstoul(_arguments[index + 1].c_str(), &end, 10);
      if (end == nullptr || *end != L'\0' || frames == 0)
      {
        return false;
      }
      _options.capture = true;
      _options.captureFrames = static_cast<std::uint32_t>(frames);
      _options.captureDirectory = _arguments[index + 2];
      index += 2;
      continue;
    }
    return false;
  }
  return true;
}

App::App(const LaunchOptions& _options)
  : m_options(_options)
{
}

int App::Run()
{
  int exitCode = EXIT_FAILED;
  try
  {
    exitCode = m_options.capture ? RunCapture() : RunWindowed();
  }
  catch (const winrt::hresult_error& error)
  {
    Neuron::Log::Write(Neuron::LogLevel::Error, "fatal: " + Describe(error));
  }
  catch (const std::exception& error)
  {
    Neuron::Log::Write(Neuron::LogLevel::Error, std::string("fatal: ") + error.what());
  }
  Neuron::Log::Close();
  return exitCode;
}

int App::RunWindowed()
{
  if (!Neuron::Log::Open(Neuron::Paths::UserDirectory() / "Logs" / "Client.log"))
  {
    Neuron::Log::Write(Neuron::LogLevel::Warning, "the log file could not be opened; logging to the debugger only");
  }
  Neuron::Window window;
  Neuron::GraphicsDevice device(m_options.warp);
  Neuron::SwapChain swapChain(device, window.Handle(), window.ClientWidth(), window.ClientHeight());
  Neuron::SceneTarget scene(device, CLEAR_COLOR);
  Neuron::PresentPass present(device, scene);
  while (window.Pump())
  {
    if (window.TakeResized())
    {
      swapChain.Resize(device, window.ClientWidth(), window.ClientHeight());
    }
    if (window.ClientWidth() == 0 || window.ClientHeight() == 0)
    {
      WaitMessage();
      continue;
    }
    ID3D12GraphicsCommandList* list = device.BeginFrame();
    scene.Begin(list);
    scene.Resolve(list);
    present.Draw(
      list, swapChain.CurrentBackBuffer(), swapChain.CurrentRenderTargetView(),
      Neuron::FitAuthored(window.ClientWidth(), window.ClientHeight(), Neuron::AUTHORED_WIDTH_PIXELS, Neuron::AUTHORED_HEIGHT_PIXELS));
    device.EndFrame();
    swapChain.Present();
    device.DrainDebugMessages();
  }
  device.WaitForIdle();
  device.DrainDebugMessages();
  Neuron::Log::Write(Neuron::LogLevel::Info, "exit: " + std::to_string(device.FramesBegun()) + " frames, " +
                                               std::to_string(device.DebugMessageCount()) + " debug-layer messages");
  return ExitCodeOf(device);
}

int App::RunCapture()
{
  std::error_code ignored;
  std::filesystem::create_directories(m_options.captureDirectory, ignored);
  if (!Neuron::Log::Open(m_options.captureDirectory / "capture.log"))
  {
    return EXIT_FAILED;
  }
  Neuron::Log::SetMinimumLevel(Neuron::LogLevel::Debug);
  Neuron::Log::Write(Neuron::LogLevel::Info, "capture: " + std::to_string(m_options.captureFrames) + " frames on " +
                                               (m_options.warp ? "WARP" : "the first hardware adapter") + " into " +
                                               m_options.captureDirectory.string());
  Neuron::GraphicsDevice device(m_options.warp);
  Neuron::SceneTarget scene(device, CLEAR_COLOR);
  Neuron::FrameCapture capture(device, Neuron::AUTHORED_WIDTH_PIXELS, Neuron::AUTHORED_HEIGHT_PIXELS, Neuron::SCENE_COLOR_FORMAT);
  for (std::uint32_t frame = 0; frame < m_options.captureFrames; ++frame)
  {
    ID3D12GraphicsCommandList* list = device.BeginFrame();
    scene.Begin(list);
    scene.Resolve(list);
    const bool captured = frame % CAPTURE_EVERY_FRAMES == 0;
    if (captured)
    {
      capture.Record(list, scene.Resolved());
    }
    device.EndFrame();
    if (captured)
    {
      device.WaitForIdle();
      const std::filesystem::path file = m_options.captureDirectory / ("frame-" + std::to_string(frame) + ".bmp");
      if (!capture.Write(file))
      {
        return EXIT_FAILED;
      }
      Neuron::Log::Write(Neuron::LogLevel::Info, "capture: wrote " + file.filename().string());
    }
    device.DrainDebugMessages();
  }
  device.WaitForIdle();
  device.DrainDebugMessages();
  Neuron::Log::Write(Neuron::LogLevel::Info, "capture: " + std::to_string(device.FramesBegun()) + " frames, " +
                                               std::to_string(device.DebugMessageCount()) + " debug-layer messages, debug layer " +
                                               (device.DebugLayerActive() ? "on" : "off"));
  return ExitCodeOf(device);
}

} // namespace Frontier
