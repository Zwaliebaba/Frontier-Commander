#include "pch.h"

#include "WindowsHeader.h"

#include "App.h"

#include "Camera.h"
#include "CameraController.h"
#include "FrameCapture.h"
#include "FrameInput.h"
#include "GraphicsDevice.h"
#include "HeightView.h"
#include "InputQueue.h"
#include "InputRouter.h"
#include "LandscapeDefinition.h"
#include "Lighting.h"
#include "Log.h"
#include "MatchSettings.h"
#include "Paths.h"
#include "PresentPass.h"
#include "ScaleMode.h"
#include "SceneTarget.h"
#include "Sim.h"
#include "SwapChain.h"
#include "TerrainChunk.h"
#include "TerrainPass.h"
#include "WaterPass.h"
#include "Window.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <exception>
#include <string>
#include <system_error>

namespace Frontier
{

namespace
{

// The Species sky is the clear colour, black (SpeciesLook.md §6); the terrain draws over it.
constexpr std::array<float, 4> CLEAR_COLOR = {0.0f, 0.0f, 0.0f, 1.0f};
constexpr std::uint32_t CAPTURE_EVERY_FRAMES = 100;
constexpr float PI = 3.14159265358979323846f;

/// The Small landscape of M0: seed 1's recipe, as Tools/LandscapeTool.py --define wrote it to
/// Tests/SimTests/Fixtures/Landscape/small-0001.json. The JSON loader of M1 replaces this.
[[nodiscard]] LandscapeDefinition SmallLandscape()
{
  LandscapeDefinition definition{};
  definition.version = LANDSCAPE_DEFINITION_VERSION;
  definition.sizeClass = SizeClass::Small;
  definition.cellsPerSide = SIZE_CLASS_CELLS[static_cast<std::size_t>(SizeClass::Small)];
  definition.seed = 1;
  definition.palette = "Default";
  definition.tiles = {
    {0, 0, 512, 170, 90, 100, 48, 70, 1, 32},   {58, -24, 256, 190, 90, 60, 0, 80, 0, 16},  {311, 61, 256, 190, 90, 90, 0, 80, 0, 16},
    {36, 209, 256, 210, 90, 75, 0, 80, 0, 16},  {269, 220, 256, 230, 90, 90, 0, 80, 0, 16}, {113, 189, 128, 290, 90, 130, 0, 87, 1, 16},
    {-23, 34, 128, 270, 90, 130, 0, 87, 1, 16}, {65, 246, 128, 270, 90, 130, 0, 87, 1, 16},
  };
  definition.starts = {{36, 92}, {108, 20}};
  return definition;
}

/// A two-seat lobby, the least a match needs.
[[nodiscard]] MatchSettings Lobby()
{
  MatchSettings settings{};
  settings.seed = 1;
  settings.sizeClass = SizeClass::Small;
  settings.seatCount = 2;
  settings.baseLevel = BaseLevel::Nothing;
  settings.powerLevel = PowerLevel::Medium;
  settings.technologyTiers = 0;
  settings.victory = VictoryCondition::Annihilation;
  settings.survivalTicks = 0;
  for (SeatSettings& seat : settings.seats)
  {
    seat = {SeatKind::Empty, NO_ALLIANCE};
  }
  settings.seats[0] = {SeatKind::Human, 0};
  settings.seats[1] = {SeatKind::Ai, 1};
  return settings;
}

/// The renderer's view over the simulation's landscape (TechnicalDesign.md §6.3): the one place
/// a Sim type meets a Client one.
[[nodiscard]] Neuron::HeightView ViewOf(const Landscape& _landscape) noexcept
{
  std::int32_t highest = 1;
  for (const std::int16_t sample : _landscape.Heights())
  {
    highest = std::max<std::int32_t>(highest, sample);
  }
  return {_landscape.Heights().data(), _landscape.SamplesPerSide(), SAMPLE_SPACING_WORLD_UNITS, 0, highest};
}

[[nodiscard]] Neuron::TerrainPass::Frame FrameOf(const Neuron::TerrainPass& _terrain, Neuron::FogMode _fog) noexcept
{
  Neuron::TerrainPass::Frame frame{};
  frame.aspect = static_cast<float>(Neuron::AUTHORED_WIDTH_PIXELS) / static_cast<float>(Neuron::AUTHORED_HEIGHT_PIXELS);
  frame.lighting = Neuron::GARDEN_LIGHTING;
  frame.fogMode = _fog;
  frame.fogStart = _terrain.ExtentWorldUnits() * Neuron::FOG_START_FRACTION;
  frame.fogEnd = _terrain.ExtentWorldUnits() * Neuron::FOG_END_FRACTION;
  frame.fogColor = {0.0f, 0.0f, 0.0f};
  return frame;
}

/// The capture's scripted path (TechnicalDesign.md §6.1): a hundred frames of a descending half
/// orbit round the island, then the vantage ADR-005 compares its two frames from, held: frame 100
/// under the Species fog, frame 200 under the desaturation. Returns the frame's fog mode.
Neuron::FogMode PoseFor(std::uint32_t _frame, float _extent, Neuron::Camera& _camera) noexcept
{
  const float center = _extent * 0.5f;
  if (_frame < 100)
  {
    const float t = static_cast<float>(_frame) / 100.0f;
    const float angle = t * PI;
    const float radius = _extent * 0.45f;
    _camera.SetPosition(center + std::sin(angle) * radius, 1500.0f - 900.0f * t, center + std::cos(angle) * radius);
    _camera.LookAt(center, 0.0f, center);
    return Neuron::FogMode::LinearToColor;
  }
  _camera.SetPosition(_extent * 0.12f, 320.0f, _extent * 0.12f);
  _camera.LookAt(_extent * 0.55f, 0.0f, _extent * 0.55f);
  return _frame < 200 ? Neuron::FogMode::LinearToColor : Neuron::FogMode::Desaturation;
}

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
  Neuron::InputQueue inputQueue;
  Neuron::FrameInput inputState;
  Neuron::InputRouter inputRouter;
  window.AttachInput(&inputQueue);
  Neuron::GraphicsDevice device(m_options.warp);
  Neuron::SwapChain swapChain(device, window.Handle(), window.ClientWidth(), window.ClientHeight());
  Neuron::SceneTarget scene(device, CLEAR_COLOR);
  Neuron::PresentPass present(device, scene);
  Sim sim(Lobby());
  if (!sim.CreateLandscape(SmallLandscape()))
  {
    Neuron::Log::Write(Neuron::LogLevel::Error, "the built-in landscape was refused");
    return EXIT_FAILED;
  }
  const Neuron::HeightView heights = ViewOf(sim.Terrain());
  Neuron::TerrainPass terrain(device, heights, Neuron::TerrainPalette::BuiltIn(), scene.SampleCount());
  Neuron::WaterPass water(device, heights, scene.SampleCount());
  Neuron::Camera camera;
  camera.SetFarPlane(terrain.ExtentWorldUnits() * Neuron::FAR_PLANE_EXTENT_FACTOR);
  PoseFor(100, terrain.ExtentWorldUnits(), camera); // The capture's vantage; the fog is the ADR's, not the script's
  const Neuron::FogMode fog = Neuron::DEFAULT_FOG_MODE;
  camera.ClampHeight(heights);
  const CameraController controller;
  auto lastFrame = std::chrono::steady_clock::now();
  while (window.Pump())
  {
    // The frame's input (TechnicalDesign.md §6.5): derive what the frame saw, offer it to the sinks,
    // mask what they took, fire the subscriptions, and only then read the view.
    const std::size_t consumed = Neuron::DeriveFrameInput(inputQueue.Events(), inputState);
    inputRouter.Dispatch(inputQueue.Events().first(consumed));
    Neuron::FrameInput inputView = inputState;
    inputRouter.Mask(inputView);
    inputRouter.FireSubscriptions(inputView);
    inputQueue.Erase(consumed);
    const auto now = std::chrono::steady_clock::now();
    const float seconds = std::min(std::chrono::duration<float>(now - lastFrame).count(), 0.1f);
    lastFrame = now;
    controller.Advance(camera, inputView, heights, seconds, window.ClientWidth(), window.ClientHeight());
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
    terrain.Draw(list, camera, FrameOf(terrain, fog));
    water.Draw(list, terrain.ConstantsAddress());
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
  window.AttachInput(nullptr);
  Neuron::Log::Write(Neuron::LogLevel::Info, "exit: " + std::to_string(device.FramesBegun()) + " frames, " +
                                               std::to_string(device.DebugMessageCount()) + " debug-layer messages, " +
                                               std::to_string(inputQueue.Dropped()) + " input events dropped");
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
  Sim sim(Lobby());
  if (!sim.CreateLandscape(SmallLandscape()))
  {
    Neuron::Log::Write(Neuron::LogLevel::Error, "the built-in landscape was refused");
    return EXIT_FAILED;
  }
  const Neuron::HeightView heights = ViewOf(sim.Terrain());
  Neuron::Log::Write(Neuron::LogLevel::Info, "landscape: " + std::to_string(heights.samplesPerSide) + " samples a side, highest " +
                                               std::to_string(heights.highest));
  Neuron::TerrainPass terrain(device, heights, Neuron::TerrainPalette::BuiltIn(), scene.SampleCount());
  Neuron::WaterPass water(device, heights, scene.SampleCount());
  Neuron::Camera camera;
  camera.SetFarPlane(terrain.ExtentWorldUnits() * Neuron::FAR_PLANE_EXTENT_FACTOR);
  for (std::uint32_t frame = 0; frame < m_options.captureFrames; ++frame)
  {
    const Neuron::FogMode fog = PoseFor(frame, terrain.ExtentWorldUnits(), camera);
    camera.ClampHeight(heights);
    ID3D12GraphicsCommandList* list = device.BeginFrame();
    scene.Begin(list);
    terrain.Draw(list, camera, FrameOf(terrain, fog));
    water.Draw(list, terrain.ConstantsAddress());
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
      const DirectX::XMFLOAT3 position = camera.Position();
      Neuron::Log::Write(Neuron::LogLevel::Info,
                         "capture: wrote " + file.filename().string() + " from (" + std::to_string(static_cast<int>(position.x)) + ", " +
                           std::to_string(static_cast<int>(position.y)) + ", " + std::to_string(static_cast<int>(position.z)) + "), fog " +
                           (fog == Neuron::FogMode::LinearToColor ? "linear to black" : "desaturation") + ", " +
                           std::to_string(terrain.LastChunkCount()) + " chunks, " + std::to_string(terrain.LastTriangleCount()) +
                           " triangles, " + std::to_string(water.QuadCount()) + " water quads");
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
