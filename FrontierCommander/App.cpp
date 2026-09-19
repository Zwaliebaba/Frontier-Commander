#include "pch.h"

#include "WindowsHeader.h"

#include "App.h"

#include "Match.h"

#include "Camera.h"
#include "CameraController.h"
#include "ContentHash.h"
#include "ContentLoader.h"
#include "FogPass.h"
#include "FrameCapture.h"
#include "FrameInput.h"
#include "FrameTimer.h"
#include "GeometryPass.h"
#include "GraphicsDevice.h"
#include "HeightView.h"
#include "InputQueue.h"
#include "InputRouter.h"
#include "LandscapeDefinition.h"
#include "Lighting.h"
#include "Log.h"
#include "RenderView.h"
#include "MatchSettings.h"
#include "ModelBuffers.h"
#include "Movement.h"
#include "Paths.h"
#include "PresentPass.h"
#include "ScaleMode.h"
#include "SceneTarget.h"
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
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace Frontier
{

namespace
{

// The Species sky is the clear colour, black (SpeciesLook.md §6); the terrain draws over it.
constexpr std::array<float, 4> CLEAR_COLOR = {0.0f, 0.0f, 0.0f, 1.0f};
constexpr std::uint32_t CAPTURE_EVERY_FRAMES = 100;
constexpr float PI = 3.14159265358979323846f;

/// Where the camera sits over the commander's base at the first frame, in world units: far enough
/// back that the command post and the ground around it are both in the frame, and high enough that
/// the look-down angle reads as an RTS camera rather than a chase one. Not a rule anywhere - the
/// design gives the camera's rates (SpeciesLook.md §7) and not its opening pose - so these are
/// this task's choice and G3's run is where the owner says whether they are right.
/// How long the capture waits for the loopback join before calling it a fault, and how often it
/// looks. Generous against a cold WARP start and short enough that a CI job never sits on it.
constexpr std::chrono::seconds JOIN_WAIT{10};
constexpr std::chrono::milliseconds JOIN_POLL{2};

constexpr float CAMERA_SETBACK = 420.0f;
constexpr float CAMERA_ELEVATION = 340.0f;

/// The landscape the slice is played on, from the content tree by its file stem
/// (m1-vertical-slice/G1a). Null when GameData carries no such file, which is a content fault and
/// not something to substitute a built-in for: M0's hard-coded recipe is gone, and a match played
/// on a landscape nobody authored is a match whose bases stand where nobody put them.
[[nodiscard]] const LandscapeDefinition* SliceLandscape(const ContentTree& _content) noexcept
{
  for (std::size_t index = 0; index < _content.landscapeIds.size() && index < _content.landscapes.size(); ++index)
  {
    if (_content.landscapeIds[index] == SLICE_LANDSCAPE)
    {
      return &_content.landscapes[index];
    }
  }
  return nullptr;
}

/// The tables a match is played by (OpenQuestions.md Q20), read once from the game-data directory
/// beside the executable. A tree that does not load is returned EMPTY and logged, and the caller
/// refuses the match: C2's tables are authored now, so an empty tree is GameData missing from
/// beside the executable rather than content nobody has written yet, and a match on no tables has
/// no command post to start either commander with.
[[nodiscard]] const Frontier::ContentTree& MatchContent()
{
  static const Frontier::ContentTree TREE = []
  {
    Frontier::ContentTree tree{};
    const std::filesystem::path directory = Neuron::Paths::GameDataDirectory();
    std::vector<Frontier::ContentDiagnostic> diagnostics;
    if (Frontier::LoadContent(directory, tree, diagnostics))
    {
      Neuron::Log::Write(Neuron::LogLevel::Info, "content: " + std::to_string(tree.components.chassis.size()) + " chassis, " +
                                                   std::to_string(tree.structures.structures.size()) + " structures, " +
                                                   std::to_string(tree.research.size()) + " research items from " + directory.string());
      return tree;
    }
    const std::string reason = diagnostics.empty() ? std::string("no diagnostic") : diagnostics.front().message;
    Neuron::Log::Write(Neuron::LogLevel::Warning,
                       "content: no tables at " + directory.string() + " (" + reason + "); the match runs on none");
    return Frontier::ContentTree{};
  }();
  return TREE;
}

/// The fixed lobby of M1, until M2's lobby lets the owner choose: this commander in seat 0 and the
/// scripted AI of S12 in seat 1, at the defaults GameDesign.md §2 names.
///
/// SEAT 0 IS HUMAN IN THE CAPTURE TOO, AND THAT IS NOT AN OVERSIGHT. G2's acceptance asks the
/// capture to run "with two AI seats", and a client cannot have one: Net/Host.cpp's FreeSeat hands
/// a joining client only a seat whose kind is Human, so an all-AI lobby refuses the join with
/// NoSeat and there is no replica to draw from. Proved here, not reasoned about - the probe of this
/// task ran it and got six failures and an empty view. So the capture watches an unattended human
/// seat, whose base stands still while the AI opposite it plays; making it a match of two playing
/// commanders needs either a scripted order stream for seat 0 or an observer connection in Net, and
/// that decision is G2's rather than something to settle inside this file.
[[nodiscard]] MatchSettings Lobby()
{
  MatchSettings settings{};
  settings.seed = 1;
  settings.sizeClass = SizeClass::Small;
  settings.seatCount = 2;
  // "Nothing is a builder and a command post" (GameDesign.md §2). It is the only level
  // FrontierCommander/StartingBase.h places; Small and Established arrive with the lobby that can
  // ask for them.
  settings.baseLevel = BaseLevel::Nothing;
  settings.powerLevel = PowerLevel::Medium;
  settings.technologyTiers = 0;
  settings.victory = VictoryCondition::Annihilation;
  settings.survivalTicks = 0;
  settings.deviceCapLevel = DeviceCapLevel::Medium; // 200 devices, the figure GameDesign.md §4 names
  settings.rejoinGraceTicks = 400;
  for (SeatSettings& seat : settings.seats)
  {
    seat = {SeatKind::Empty, NO_ALLIANCE};
  }
  settings.seats[0] = {SeatKind::Human, 0};
  settings.seats[1] = {SeatKind::Ai, 1};
  return settings;
}

/// Where the camera starts: over this commander's own base, high enough to see it and the ground
/// it has to expand into, looking down and inward toward the middle of the landscape so that the
/// first frame is the picture a player expects rather than a corner of sea.
void PoseOverBase(Neuron::Camera& _camera, const CellPosition& _start, float _extent, float _groundHeight) noexcept
{
  const float baseX = (static_cast<float>(_start.x) + 0.5f) * static_cast<float>(Neuron::WORLD_UNITS_PER_CELL);
  const float baseZ = (static_cast<float>(_start.y) + 0.5f) * static_cast<float>(Neuron::WORLD_UNITS_PER_CELL);
  const float center = _extent * 0.5f;
  // Back off along the line from the middle of the landscape to the base, so that "inward" is the
  // same direction whichever corner the seat starts in.
  const float awayX = baseX - center;
  const float awayZ = baseZ - center;
  const float distance = std::sqrt(awayX * awayX + awayZ * awayZ);
  const float unitX = distance > 1.0f ? awayX / distance : 0.0f;
  const float unitZ = distance > 1.0f ? awayZ / distance : -1.0f;
  _camera.SetPosition(baseX + unitX * CAMERA_SETBACK, _groundHeight + CAMERA_ELEVATION, baseZ + unitZ * CAMERA_SETBACK);
  _camera.LookAt(baseX, _groundHeight, baseZ);
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

/// The landscape's palette, from GameData\Terrain, falling back to the built-in gradient when the
/// game data is not beside the executable or the file is not a 64 by 64 texture. Nothing in the
/// build puts it there (owner, 2026-09-18), so the fallback is the ordinary case until it does and
/// the log says which was used: a frame coloured by the gradient and a frame coloured by the
/// authored palette are different pictures and nobody should have to guess which they are reading.
///
/// The biome names its palette (GameData\Biomes.json, m1-vertical-slice/C2) and this reads the
/// default directly until the tables exist.
[[nodiscard]] Neuron::TerrainPalette LoadTerrainPalette()
{
  const std::filesystem::path file = Neuron::Paths::GameDataDirectory() / "Terrain" / "LandscapeDefault.dds";
  std::ifstream stream(file, std::ios::binary);
  if (stream)
  {
    const std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    Neuron::TextureFile texture;
    std::string error;
    Neuron::TerrainPalette palette;
    if (!Neuron::TextureFile::Read(std::as_bytes(std::span<const char>(bytes.data(), bytes.size())), texture, error))
    {
      Neuron::Log::Write(Neuron::LogLevel::Warning, "palette: " + file.string() + " was refused: " + error);
    }
    else if (!Neuron::TerrainPalette::FromTexture(texture, palette))
    {
      Neuron::Log::Write(Neuron::LogLevel::Warning, "palette: " + file.string() + " is not a 64 by 64 palette");
    }
    else
    {
      Neuron::Log::Write(Neuron::LogLevel::Info, "palette: " + file.string());
      return palette;
    }
  }
  else
  {
    Neuron::Log::Write(Neuron::LogLevel::Info, "palette: no " + file.string());
  }
  Neuron::Log::Write(Neuron::LogLevel::Info, "palette: the built-in gradient");
  return Neuron::TerrainPalette::BuiltIn();
}

/// The three passes that cannot exist until there is a landscape, held as ONE object because they
/// are born together and die together: each is built from the same heights and the same sample
/// count, and there is no state in which one of them is the right thing to draw without the others.
///
/// M0 BUILT THESE AT STARTUP AND M1 CANNOT. The landscape arrives with the join (Match.h), which is
/// a datagram answered by a thread, so there are frames - one over loopback, a second or more over
/// a network - in which the device, the swap chain and the scene target exist and the terrain does
/// not. The window therefore holds this in an optional and presents the cleared target, which is
/// the Species sky, until it is filled: a window that stays black until a join lands is
/// indistinguishable from one that hung. The capture holds it as a plain object, because it waits
/// for the join before it builds anything at all.
struct MatchPasses
{
  MatchPasses(Neuron::GraphicsDevice& _device, const Neuron::SceneTarget& _scene, const Neuron::HeightView& _heights,
              std::uint32_t _cellsPerSide)
    : terrain(_device, _heights, LoadTerrainPalette(), _scene.SampleCount()),
      water(_device, _heights, _scene.SampleCount()),
      fog(_device, _scene, _cellsPerSide)
  {
  }

  Neuron::TerrainPass terrain;
  Neuron::WaterPass water;
  Neuron::FogPass fog;
};

/// Pushes the far plane out for this landscape and puts the camera over the commander's base.
/// Reads the CLIENT'S landscape (Match::Terrain) and never the host's, which is the fog boundary of
/// TechnicalDesign.md §5.2.
void AimAtBase(Neuron::Camera& _camera, const Match& _match, const CellPosition& _start, float _extent)
{
  _camera.SetFarPlane(_extent * Neuron::FAR_PLANE_EXTENT_FACTOR);
  // The ground the base actually stands on, read off the commander's own landscape rather than
  // guessed from the highest sample: a camera parked at half the island's height is underground on
  // a peak and in orbit over a beach.
  const std::int32_t baseSubunitsX = static_cast<std::int32_t>(_start.x) * Neuron::SUBUNITS_PER_CELL + Neuron::SUBUNITS_PER_CELL / 2;
  const std::int32_t baseSubunitsZ = static_cast<std::int32_t>(_start.y) * Neuron::SUBUNITS_PER_CELL + Neuron::SUBUNITS_PER_CELL / 2;
  const float ground = Neuron::WorldUnitsOfSubunits(GroundHeightSubunits(_match.Terrain(), baseSubunitsX, baseSubunitsZ));
  PoseOverBase(_camera, _start, _extent, ground);
}

/// The fog range is the landscape's no longer (ADR-007): it is absolute, so a Frontier landscape's
/// horizon reads exactly as a Small one's.
[[nodiscard]] Neuron::TerrainPass::Frame FrameOf(Neuron::FogMode _fog) noexcept
{
  Neuron::TerrainPass::Frame frame{};
  frame.aspect = static_cast<float>(Neuron::AUTHORED_WIDTH_PIXELS) / static_cast<float>(Neuron::AUTHORED_HEIGHT_PIXELS);
  frame.lighting = Neuron::BUILT_IN_LIGHTING;
  frame.fogMode = _fog;
  frame.fogStart = Neuron::FOG_START_WORLD_UNITS;
  frame.fogEnd = Neuron::FOG_FULL_WORLD_UNITS;
  frame.fogMaxDesaturation = Neuron::FOG_MAX_DESATURATION;
  frame.fogColor = {0.0f, 0.0f, 0.0f};
  return frame;
}

/// The capture's scripted path (TechnicalDesign.md §6.1): a hundred frames of a descending half
/// orbit round the island, then the commander's own base, held: frame 100 under the Species fog,
/// frame 200 under the desaturation, which is the pair ADR-005 compares. Returns the frame's fog
/// mode.
///
/// THE HELD VANTAGE IS THE BASE AND NOT A CORNER. M0 held a fixed point because there was nothing
/// on the island to hold on; there is now, and a capture of a match whose frames show no commander
/// in them proves only that the terrain still draws.
Neuron::FogMode PoseFor(std::uint32_t _frame, float _extent, const CellPosition& _start, Neuron::Camera& _camera) noexcept
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
  const float baseX = (static_cast<float>(_start.x) + 0.5f) * static_cast<float>(Neuron::WORLD_UNITS_PER_CELL);
  const float baseZ = (static_cast<float>(_start.y) + 0.5f) * static_cast<float>(Neuron::WORLD_UNITS_PER_CELL);
  _camera.SetPosition(baseX, CAMERA_ELEVATION * 1.5f, baseZ - CAMERA_SETBACK);
  _camera.LookAt(baseX, 0.0f, baseZ);
  return _frame < 200 ? Neuron::FogMode::LinearToColor : Neuron::FogMode::Desaturation;
}

/// One frame of the world, in the order TechnicalDesign.md §6.2 gives: the ground, the water over
/// it, the commanders' things on it, and the fog over everything. Shared by the window and the
/// capture so that the two cannot drift into showing different pictures of the same match - which
/// they would, because the capture is the only one anybody reviews.
void DrawMatch(ID3D12GraphicsCommandList* _list, const Neuron::SceneTarget& _scene, MatchPasses& _passes, Neuron::GeometryPass& _geometry,
               const Neuron::Camera& _camera, const Match& _match, Neuron::FogMode _fog)
{
  const Neuron::TerrainPass::Frame frame = FrameOf(_fog);
  _passes.terrain.Draw(_list, _camera, frame);
  // AFTER the draw and not before: TerrainPass::Draw is what writes this frame's constants and sets
  // the address to the slot it wrote, so an address read first is the previous frame's.
  const D3D12_GPU_VIRTUAL_ADDRESS constants = _passes.terrain.ConstantsAddress();
  _passes.water.Draw(_list, constants);
  _geometry.Draw(_list, constants, _match.View().instances);
  _passes.fog.Draw(_list, _scene, _camera, frame.aspect, _match.View().fog);
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

/// Microseconds as milliseconds to two places, for the title bar. Fixed to two places rather than
/// left to the default formatting, so that the three figures line up as the numbers move.
[[nodiscard]] std::wstring Micros(std::uint64_t _microseconds)
{
  const std::uint64_t hundredths = (_microseconds + 5) / 10;
  return std::to_wstring(hundredths / 100) + L"." + (hundredths % 100 < 10 ? L"0" : L"") + std::to_wstring(hundredths % 100);
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
    if (argument == L"--novsync")
    {
      _options.noVerticalSync = true;
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
  const ContentTree& content = MatchContent();
  const LandscapeDefinition* landscape = SliceLandscape(content);
  if (landscape == nullptr)
  {
    Neuron::Log::Write(Neuron::LogLevel::Error,
                       std::string("GameData carries no landscape called ") + SLICE_LANDSCAPE + "; there is nothing to play on");
    return EXIT_FAILED;
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

  // THE MODEL SET AND THE GEOMETRY PASS NEED NO LANDSCAPE, so they are built now rather than with
  // the rest: the models are content and the commander colours are content, and both are known
  // before a single datagram has been sent.
  Neuron::ModelBuffers models(device, content.models);
  Neuron::GeometryPass geometry(device, models, scene.SampleCount(), content.ui.commanders);

  // The two loops (TechnicalDesign.md §3). Everything from here to the draw is Match's; this
  // function keeps steps 1, 6 and 7 - the window's messages, the input routing and the drawing -
  // because those are the three that need a window and a device.
  Match match(content, Lobby(), ContentHash(content), Neuron::CHUNK_CELLS);
  if (!match.Start(*landscape, "Commander"))
  {
    return EXIT_FAILED; // Match::Start has logged which of its faults it was.
  }

  // The passes and the height view they read arrive together, when the join does. The optional is
  // what says "not yet", and every use of it below is under a check the analyser can follow.
  Neuron::HeightView heights{};
  std::optional<MatchPasses> passes;
  Neuron::Camera camera;
  const CameraController controller;
  const Neuron::FogMode fog = Neuron::DEFAULT_FOG_MODE;
  // The frame time is measured over the whole loop body, present included, which is what a player
  // waits for. With --novsync and a display that allows tearing it is the renderer's cost; without
  // either it is the refresh interval, and the title says which so that a figure read off it is
  // never mistaken for the other (m0-foundation/T22).
  Neuron::FrameTimer frameTimer;
  const std::uint32_t syncInterval = m_options.noVerticalSync ? 0u : 1u;
  const wchar_t* pacing = syncInterval != 0              ? L"vsync"
                          : swapChain.TearingSupported() ? L"unlocked"
                                                         : L"novsync (no tearing here: still paced by the display)";
  if (m_options.noVerticalSync && !swapChain.TearingSupported())
  {
    Neuron::Log::Write(Neuron::LogLevel::Warning,
                       "--novsync: this output does not allow tearing, so frames are still paced by the display");
  }
  const auto matchStarted = std::chrono::steady_clock::now();
  bool joinWarned = false;
  auto lastFrame = matchStarted;
  while (window.Pump())
  {
    const auto frameStart = std::chrono::steady_clock::now();
    // 1 and 6. The frame's input (TechnicalDesign.md §6.5): derive what the frame saw, offer it to
    // the sinks, mask what they took, fire the subscriptions, and only then read the view.
    const std::size_t consumed = Neuron::DeriveFrameInput(inputQueue.Events(), inputState);
    inputRouter.Dispatch(inputQueue.Events().first(consumed));
    Neuron::FrameInput inputView = inputState;
    inputRouter.Mask(inputView);
    inputRouter.FireSubscriptions(inputView);
    inputQueue.Erase(consumed);
    const auto now = std::chrono::steady_clock::now();
    const auto sinceLastFrame = now - lastFrame;
    lastFrame = now;
    const float seconds = std::min(std::chrono::duration<float>(sinceLastFrame).count(), 0.1f);

    // 2 to 5. Drain, apply, interpolate, build the render view. Given the WALL TIME and not the
    // clamped seconds: the clamp above is the camera's, so that a frame that took a second does not
    // fly it across the map, and the liveness clock must not be shortened by it - a timeout that
    // ran slow whenever the display hitched is a timeout that cannot be trusted.
    match.Advance(std::chrono::duration_cast<std::chrono::nanoseconds>(sinceLastFrame));
    if (!passes.has_value() && match.TerrainReady())
    {
      heights = ViewOf(match.Terrain());
      // emplace returns the reference, and that is what is used: there is no operator-> on the
      // optional here at all, so nothing has to reason about whether it is engaged.
      MatchPasses& built = passes.emplace(device, scene, heights, match.Terrain().CellsPerSide());
      AimAtBase(camera, match, landscape->starts.front(), built.terrain.ExtentWorldUnits());
      camera.ClampHeight(heights);
      Neuron::Log::Write(Neuron::LogLevel::Info, "match: the landscape arrived - " + std::to_string(heights.samplesPerSide) +
                                                   " samples a side, " + std::to_string(match.Terrain().CellsPerSide()) +
                                                   " cells, highest " + std::to_string(heights.highest));
    }
    if (passes.has_value())
    {
      controller.Advance(camera, inputView, heights, seconds, window.ClientWidth(), window.ClientHeight());
    }
    else if (!joinWarned && now - matchStarted > JOIN_WAIT)
    {
      // Said once, because a window of nothing but sky is indistinguishable from a window that
      // hung, and the log is the only place that can tell them apart.
      Neuron::Log::Write(Neuron::LogLevel::Warning, "match: the host has not answered the join after " + std::to_string(JOIN_WAIT.count()) +
                                                      " seconds; the frame is the sky");
      joinWarned = true;
    }

    if (window.TakeResized())
    {
      swapChain.Resize(device, window.ClientWidth(), window.ClientHeight());
    }
    if (window.ClientWidth() == 0 || window.ClientHeight() == 0)
    {
      WaitMessage();
      continue;
    }
    // 7. Draw and present. A frame before the join has landed presents the cleared target, which is
    // the Species sky; it is a picture rather than a hang.
    ID3D12GraphicsCommandList* list = device.BeginFrame();
    scene.Begin(list);
    if (passes.has_value())
    {
      DrawMatch(list, scene, *passes, geometry, camera, match, fog);
    }
    scene.Resolve(list);
    present.Draw(
      list, swapChain.CurrentBackBuffer(), swapChain.CurrentRenderTargetView(),
      Neuron::FitAuthored(window.ClientWidth(), window.ClientHeight(), Neuron::AUTHORED_WIDTH_PIXELS, Neuron::AUTHORED_HEIGHT_PIXELS));
    device.EndFrame();
    swapChain.Present(syncInterval);
    device.DrainDebugMessages();
    frameTimer.Add(static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - frameStart).count()));
    // Once a quarter of the window rather than every frame: a title that changes 500 times a
    // second is unreadable, and SetWindowTextW is a message to the window's own thread.
    if (frameTimer.TotalFrames() % (Neuron::FrameTimer::WINDOW_FRAMES / 4) == 0)
    {
      std::wstring title = L"Frontier Commander - ";
      title += pacing;
      title += L" - mean " + Micros(frameTimer.MeanMicroseconds()) + L" ms, median " + Micros(frameTimer.MedianMicroseconds()) +
               L", 99th " + Micros(frameTimer.PercentileMicroseconds(99)) + L" - tick " + std::to_wstring(match.HostSide().Tick()) + L", " +
               std::to_wstring(geometry.LastInstanceCount()) + L" instances in " + std::to_wstring(geometry.LastDrawCount()) + L" draws";
      window.SetTitle(title.c_str());
    }
  }
  device.WaitForIdle();
  device.DrainDebugMessages();
  window.AttachInput(nullptr);
  // The host thread is stopped and joined before the device goes, because a host that was still
  // publishing into a transport whose client end had been destroyed is a race nobody would find.
  match.Stop();
  Neuron::Log::Write(Neuron::LogLevel::Info, "exit: " + std::to_string(device.FramesBegun()) + " frames, " +
                                               std::to_string(device.DebugMessageCount()) + " debug-layer messages, " +
                                               std::to_string(inputQueue.Dropped()) + " input events dropped, host tick " +
                                               std::to_string(match.HostSide().Tick()) + ", " +
                                               std::to_string(match.HostSide().SlowedTicks()) + " tick(s) of wall time given away");
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
  const ContentTree& content = MatchContent();
  const LandscapeDefinition* landscape = SliceLandscape(content);
  if (landscape == nullptr)
  {
    Neuron::Log::Write(Neuron::LogLevel::Error,
                       std::string("GameData carries no landscape called ") + SLICE_LANDSCAPE + "; there is nothing to capture");
    return EXIT_FAILED;
  }
  Neuron::GraphicsDevice device(m_options.warp);
  Neuron::SceneTarget scene(device, CLEAR_COLOR);
  Neuron::FrameCapture capture(device, Neuron::AUTHORED_WIDTH_PIXELS, Neuron::AUTHORED_HEIGHT_PIXELS, Neuron::SCENE_COLOR_FORMAT);
  Neuron::ModelBuffers models(device, content.models);
  Neuron::GeometryPass geometry(device, models, scene.SampleCount(), content.ui.commanders);

  Match match(content, Lobby(), ContentHash(content), Neuron::CHUNK_CELLS);
  if (!match.Start(*landscape, "Capture"))
  {
    return EXIT_FAILED;
  }
  Neuron::Camera camera;

  // THE JOIN IS WAITED FOR BEFORE THE FIRST FRAME IS COUNTED, and the window deliberately is not.
  // A window shows the sky for the frame or two a loopback join takes and nobody minds; a capture
  // whose frame 0 is the cleared target has written a BLACK BMP as its first artefact, and the one
  // thing the agent ever sees of this game is those files. The wait is bounded: a join that never
  // lands is a fault to report rather than a capture to hang on.
  {
    const auto until = std::chrono::steady_clock::now() + JOIN_WAIT;
    auto last = std::chrono::steady_clock::now();
    while (!match.TerrainReady() && std::chrono::steady_clock::now() < until)
    {
      const auto now = std::chrono::steady_clock::now();
      match.Advance(std::chrono::duration_cast<std::chrono::nanoseconds>(now - last));
      last = now;
      std::this_thread::sleep_for(JOIN_POLL);
    }
    if (!match.TerrainReady())
    {
      Neuron::Log::Write(Neuron::LogLevel::Error, "capture: the host did not answer the join; there is nothing to capture");
      return EXIT_FAILED;
    }
  }
  // Plain objects rather than an optional: the landscape is known by the time this line runs, so
  // there is no "not yet" for the capture to carry, and nothing below has to ask whether it has one.
  const Neuron::HeightView heights = ViewOf(match.Terrain());
  MatchPasses passes(device, scene, heights, match.Terrain().CellsPerSide());
  const float extent = passes.terrain.ExtentWorldUnits();
  AimAtBase(camera, match, landscape->starts.front(), extent);
  Neuron::Log::Write(Neuron::LogLevel::Info, "capture: the landscape arrived - " + std::to_string(heights.samplesPerSide) +
                                               " samples a side, " + std::to_string(match.Terrain().CellsPerSide()) + " cells, highest " +
                                               std::to_string(heights.highest));

  // THE CAPTURE RUNS THE MATCH IN WALL TIME, WHICH IS WHAT G1a CAN PROVE AND NO MORE. The host
  // thread is driven by the clock, so a capture of N frames advances the simulation by however long
  // N frames took on WARP and not by a tick count anybody chose. G2 is the task that makes this a
  // scripted number of TICKS run as fast as the simulation allows; until then the frames show the
  // opening of a real match rather than M0's empty island, which is the step this task owes.
  auto lastFrame = std::chrono::steady_clock::now();
  for (std::uint32_t frame = 0; frame < m_options.captureFrames; ++frame)
  {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = now - lastFrame;
    lastFrame = now;
    match.Advance(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed));
    const Neuron::FogMode fog = PoseFor(frame, extent, landscape->starts.front(), camera);
    camera.ClampHeight(heights);

    ID3D12GraphicsCommandList* list = device.BeginFrame();
    scene.Begin(list);
    DrawMatch(list, scene, passes, geometry, camera, match, fog);
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
                           std::to_string(static_cast<int>(position.y)) + ", " + std::to_string(static_cast<int>(position.z)) +
                           "), host tick " + std::to_string(match.HostSide().Tick()) + ", " + std::to_string(geometry.LastInstanceCount()) +
                           " instances in " + std::to_string(geometry.LastDrawCount()) + " draws, " +
                           std::to_string(geometry.LastUnknownModelCount()) + " naming no model, fog " +
                           (fog == Neuron::FogMode::LinearToColor ? "linear to black" : "desaturation"));
    }
    device.DrainDebugMessages();
  }
  device.WaitForIdle();
  device.DrainDebugMessages();
  match.Stop();
  Neuron::Log::Write(Neuron::LogLevel::Info, "capture: " + std::to_string(device.FramesBegun()) + " frames, " +
                                               std::to_string(device.DebugMessageCount()) + " debug-layer messages, debug layer " +
                                               (device.DebugLayerActive() ? "on" : "off") + ", host tick " +
                                               std::to_string(match.HostSide().Tick()));
  return ExitCodeOf(device);
}

} // namespace Frontier
