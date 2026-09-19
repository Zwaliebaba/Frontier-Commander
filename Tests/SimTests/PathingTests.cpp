#include "pch.h"

#include "ClusterGraph.h"
#include "Json.h"
#include "PathPlanner.h"
#include "Sim.h"

#include "FixedPoint.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <queue>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// Pathing (TechnicalDesign.md §4.5). This is the system that decides whether a large landscape
// works, so what is under test is not "it finds a path" but the three properties the design rests
// on: the abstraction agrees with the ground truth, the answer depends only on the state, and the
// work per tick is bounded by a budget rather than by the size of the request.
namespace SimTests
{

namespace
{

/// The definitions Tools/LandscapeTool.py wrote (Fixtures/Landscape/README.md). Twenty Small
/// landscapes is the reachability test's whole point: a hand-made map exercises the entrance
/// construction the way its author imagined, and twenty generated ones do not.
std::filesystem::path FixtureDirectory()
{
  return std::filesystem::path(__FILE__).parent_path() / "Fixtures" / "Landscape";
}

[[nodiscard]] bool ReadDefinition(const std::filesystem::path& _path, Frontier::LandscapeDefinition& _out)
{
  std::ifstream file(_path, std::ios::binary);
  if (!file)
  {
    return false;
  }
  const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  Neuron::JsonValue document;
  Neuron::JsonError error;
  if (!Neuron::ParseJson(text, _path.string(), document, error))
  {
    return false;
  }
  Frontier::LandscapeDefinition definition{};
  definition.version = Frontier::LANDSCAPE_DEFINITION_VERSION;
  definition.cellsPerSide = static_cast<std::uint32_t>(document.Find("cellsPerSide")->AsInteger());
  definition.sizeClass = Frontier::SizeClass::Small;
  definition.seed = static_cast<std::uint64_t>(document.Find("seed")->AsInteger());
  definition.palette = "Default";
  const Neuron::JsonValue* tiles = document.Find("tiles");
  for (std::size_t index = 0; index < tiles->Size(); ++index)
  {
    const Neuron::JsonValue& row = tiles->At(index);
    Frontier::LandscapeTile tile{};
    tile.x = static_cast<std::int32_t>(row.Find("x")->AsInteger());
    tile.y = static_cast<std::int32_t>(row.Find("y")->AsInteger());
    tile.extent = static_cast<std::uint32_t>(row.Find("extent")->AsInteger());
    tile.fractalDimensionHundredths = static_cast<std::int32_t>(row.Find("fractalDimensionHundredths")->AsInteger());
    tile.amplitude = static_cast<std::int32_t>(row.Find("amplitude")->AsInteger());
    tile.desiredHeight = static_cast<std::int32_t>(row.Find("desiredHeight")->AsInteger());
    tile.heightShift = static_cast<std::int32_t>(row.Find("heightShift")->AsInteger());
    tile.lowlandExponentHundredths = static_cast<std::int32_t>(row.Find("lowlandExponentHundredths")->AsInteger());
    tile.method = static_cast<std::uint8_t>(row.Find("method")->AsInteger());
    tile.edgeFalloff = static_cast<std::uint32_t>(row.Find("edgeFalloff")->AsInteger());
    definition.tiles.push_back(tile);
  }
  _out = definition;
  return true;
}

/// The tables the graph reads its passability from: one row per drive class of GameDesign.md §6,
/// so that all six classes are defined and the graph's per-class rules are exercised.
const Frontier::ContentTree& Tables()
{
  static const Frontier::ContentTree TREE = []
  {
    Frontier::ContentTree tree;
    const auto drive = [](const char* _id, Frontier::DriveClass _class, std::int32_t _slope, bool _water)
    {
      Frontier::DriveDesc row{};
      row.id = _id;
      row.driveClass = _class;
      row.maxSlopePercent = _slope;
      row.crossesWater = _water;
      row.speedFactorHundredths = 100;
      row.hitPointFactorHundredths = 100;
      row.costHundredths = 3000;
      return row;
    };
    tree.components.drives = {
      drive("Wheels", Frontier::DriveClass::Wheels, 25, false), drive("HalfTrack", Frontier::DriveClass::HalfTrack, 35, false),
      drive("Tracks", Frontier::DriveClass::Tracks, 40, false), drive("Hover", Frontier::DriveClass::Hover, 20, true),
      drive("Legs", Frontier::DriveClass::Legs, 60, false),     drive("Lift", Frontier::DriveClass::Lift, 100, true)};
    return tree;
  }();
  return TREE;
}

Frontier::MatchSettings TwoSeats()
{
  Frontier::MatchSettings settings{};
  settings.seed = 5;
  settings.sizeClass = Frontier::SizeClass::Small;
  settings.seatCount = 2;
  settings.baseLevel = Frontier::BaseLevel::Nothing;
  settings.powerLevel = Frontier::PowerLevel::Medium;
  settings.deviceCapLevel = Frontier::DeviceCapLevel::Medium;
  settings.victory = Frontier::VictoryCondition::Annihilation;
  settings.seats[0] = {Frontier::SeatKind::Human, 0};
  settings.seats[1] = {Frontier::SeatKind::Ai, 1};
  return settings;
}

/// The ground truth: every cell a flood fill over the SAME passability rule reaches from a start.
/// It is deliberately the dumbest possible implementation, because its whole job is to disagree
/// with the clever one when the clever one is wrong.
std::vector<bool> FloodFill(const Frontier::ClusterGraph& _graph, std::uint32_t _side, std::uint32_t _startX, std::uint32_t _startY,
                            Frontier::DriveClass _drive)
{
  std::vector<bool> reached(static_cast<std::size_t>(_side) * _side, false);
  if (!_graph.Passable(_startX, _startY, _drive))
  {
    return reached;
  }
  std::queue<std::uint32_t> open;
  reached[static_cast<std::size_t>(_startY) * _side + _startX] = true;
  open.push(_startY * _side + _startX);
  while (!open.empty())
  {
    const std::uint32_t cell = open.front();
    open.pop();
    const std::uint32_t x = cell % _side;
    const std::uint32_t y = cell / _side;
    for (std::int32_t dy = -1; dy <= 1; ++dy)
    {
      for (std::int32_t dx = -1; dx <= 1; ++dx)
      {
        if (dx == 0 && dy == 0)
        {
          continue;
        }
        const std::int64_t nx = static_cast<std::int64_t>(x) + dx;
        const std::int64_t ny = static_cast<std::int64_t>(y) + dy;
        if (nx < 0 || ny < 0 || nx >= _side || ny >= _side)
        {
          continue;
        }
        const auto cellX = static_cast<std::uint32_t>(nx);
        const auto cellY = static_cast<std::uint32_t>(ny);
        if (!_graph.Passable(cellX, cellY, _drive))
        {
          continue;
        }
        // The same corner rule the cell search uses, or the two would disagree about a diagonal
        // squeeze and the disagreement would be the test's rather than the code's.
        if (dx != 0 && dy != 0 &&
            (!_graph.Passable(static_cast<std::uint32_t>(static_cast<std::int64_t>(x) + dx), y, _drive) ||
             !_graph.Passable(x, static_cast<std::uint32_t>(static_cast<std::int64_t>(y) + dy), _drive)))
        {
          continue;
        }
        const std::size_t next = static_cast<std::size_t>(cellY) * _side + cellX;
        if (!reached[next])
        {
          reached[next] = true;
          open.push(cellY * _side + cellX);
        }
      }
    }
  }
  return reached;
}

/// Runs a request to completion, however many ticks the budget takes, and fails rather than
/// looping for ever if it never finishes.
const Frontier::Path& PlanFully(Frontier::Sim& _sim, Frontier::ObjectId _device)
{
  for (std::uint32_t tick = 0; tick < 2000; ++tick)
  {
    const Frontier::Path* path = _sim.Planner().Result(_device);
    if (path != nullptr && path->state != Frontier::PathState::Planning)
    {
      return *path;
    }
    _sim.Advance();
  }
  Assert::Fail(L"the request never finished");
}

constexpr Frontier::ObjectId MOVER = {1, Frontier::ObjectKind::Device};

} // namespace

TEST_CLASS(PathingTests)
{
public:
  TEST_METHOD(ReachabilityAgreesWithAFloodFillOnTheTwentyFixturesForEveryDriveClass)
  {
    // The one test that can catch an entrance built wrongly. A cluster graph that misses an
    // opening still finds paths - just not all of them - and nothing but the ground truth notices.
    std::uint32_t checked = 0;
    for (std::uint32_t seed = 1; seed <= 20; ++seed)
    {
      wchar_t name[32] = {};
      swprintf(name, 32, L"small-%04u.json", seed);
      Frontier::LandscapeDefinition definition{};
      if (!ReadDefinition(FixtureDirectory() / name, definition))
      {
        Assert::Fail(L"a fixture definition would not read");
      }
      Frontier::Sim sim(TwoSeats(), Tables());
      Assert::IsTrue(sim.CreateLandscape(definition));
      const Frontier::ClusterGraph& graph = sim.Clusters();
      const std::uint32_t side = sim.Terrain().CellsPerSide();

      for (std::uint8_t driveIndex = 0; driveIndex < Frontier::DRIVE_CLASS_COUNT; ++driveIndex)
      {
        const auto drive = static_cast<Frontier::DriveClass>(driveIndex);
        // A start the class can stand on, scanning in a fixed order so the test is the same run
        // to run. A class that can stand nowhere on this landscape has nothing to compare.
        std::uint32_t startX = side;
        std::uint32_t startY = side;
        for (std::uint32_t y = 0; y < side && startX == side; ++y)
        {
          for (std::uint32_t x = 0; x < side; ++x)
          {
            if (graph.Passable(x, y, drive))
            {
              startX = x;
              startY = y;
              break;
            }
          }
        }
        if (startX == side)
        {
          continue;
        }
        const std::vector<bool> truth = FloodFill(graph, side, startX, startY, drive);

        // Sampled rather than exhaustive: a full cross-product is 16,384 requests a class a seed,
        // and the property is a property of the entrance construction rather than of any one pair.
        // The stride is prime to the side so the samples do not all land in one cluster column.
        for (std::uint32_t cell = 0; cell < side * side; cell += 337)
        {
          const std::uint32_t toX = cell % side;
          const std::uint32_t toY = cell / side;
          if (!graph.Passable(toX, toY, drive))
          {
            continue;
          }
          sim.Planner().Request(MOVER, startX, startY, toX, toY, drive);
          const Frontier::Path& path = PlanFully(sim, MOVER);
          const bool found = path.Usable();
          const bool expected = truth[static_cast<std::size_t>(toY) * side + toX];
          if (found != expected)
          {
            const std::wstring message = L"seed " + std::to_wstring(seed) + L", drive " + std::to_wstring(driveIndex) + L": (" +
                                         std::to_wstring(startX) + L"," + std::to_wstring(startY) + L") to (" + std::to_wstring(toX) +
                                         L"," + std::to_wstring(toY) + L"): the graph says " + (found ? L"reachable" : L"not") +
                                         L" and the flood fill says " + (expected ? L"reachable" : L"not");
            Assert::Fail(message.c_str());
          }
          ++checked;
        }
      }
    }
    Logger::WriteMessage((L"measured: reachability agreed with the flood fill on " + std::to_wstring(checked) + L" requests").c_str());
    Assert::IsTrue(checked > 1000, L"the sampling must actually have sampled something");
  }

  TEST_METHOD(TheSameRequestInTwoSimsYieldsTheSamePath)
  {
    Frontier::LandscapeDefinition definition{};
    Assert::IsTrue(ReadDefinition(FixtureDirectory() / L"small-0003.json", definition));
    Frontier::Sim left(TwoSeats(), Tables());
    Frontier::Sim right(TwoSeats(), Tables());
    Assert::IsTrue(left.CreateLandscape(definition));
    Assert::IsTrue(right.CreateLandscape(definition));

    const std::uint32_t side = left.Terrain().CellsPerSide();
    // A start the class can stand on, found rather than assumed: a generated landscape owes no
    // particular cell to a test, and cell (4, 4) of this fixture is under water.
    std::uint32_t fromX = 0;
    std::uint32_t fromY = 0;
    bool haveStart = false;
    for (std::uint32_t cell = 0; cell < side * side && !haveStart; ++cell)
    {
      if (left.Clusters().Passable(cell % side, cell / side, Frontier::DriveClass::Tracks))
      {
        fromX = cell % side;
        fromY = cell / side;
        haveStart = true;
      }
    }
    Assert::IsTrue(haveStart, L"the fixture must offer somewhere to stand");
    std::uint32_t found = 0;
    for (std::uint32_t cell = 0; cell < side * side && found < 8; cell += 911)
    {
      const std::uint32_t toX = cell % side;
      const std::uint32_t toY = cell / side;
      if (!left.Clusters().Passable(toX, toY, Frontier::DriveClass::Tracks))
      {
        continue;
      }
      left.Planner().Request(MOVER, fromX, fromY, toX, toY, Frontier::DriveClass::Tracks);
      right.Planner().Request(MOVER, fromX, fromY, toX, toY, Frontier::DriveClass::Tracks);
      const Frontier::Path& a = PlanFully(left, MOVER);
      const Frontier::Path& b = PlanFully(right, MOVER);
      Assert::IsTrue(a == b, L"cell for cell, node for node, and the same count of nodes expanded");
      ++found;
    }
    Assert::IsTrue(found > 0, L"the fixture must offer somewhere to walk");
  }

  TEST_METHOD(TheBudgetBoundsTheNodesExpandedPerTick)
  {
    // The property the design asks for: what a tick costs is a number in the code and not a
    // function of how far the commander clicked.
    Frontier::LandscapeDefinition definition{};
    Assert::IsTrue(ReadDefinition(FixtureDirectory() / L"small-0001.json", definition));
    Frontier::Sim sim(TwoSeats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(definition));

    constexpr std::uint32_t TIGHT = 3;
    const std::uint32_t side = sim.Terrain().CellsPerSide();
    std::uint32_t toX = 0;
    std::uint32_t toY = 0;
    for (std::uint32_t cell = side * side; cell > 0; --cell)
    {
      if (sim.Clusters().Passable((cell - 1) % side, (cell - 1) / side, Frontier::DriveClass::Legs))
      {
        toX = (cell - 1) % side;
        toY = (cell - 1) / side;
        break;
      }
    }
    std::uint32_t fromX = 0;
    std::uint32_t fromY = 0;
    for (std::uint32_t cell = 0; cell < side * side; ++cell)
    {
      if (sim.Clusters().Passable(cell % side, cell / side, Frontier::DriveClass::Legs))
      {
        fromX = cell % side;
        fromY = cell / side;
        break;
      }
    }
    sim.Planner().Request(MOVER, fromX, fromY, toX, toY, Frontier::DriveClass::Legs);
    std::uint32_t ticks = 0;
    while (sim.Planner().Result(MOVER)->state == Frontier::PathState::Planning && ticks < 5000)
    {
      sim.Planner().Advance(TIGHT);
      Assert::IsTrue(sim.Planner().LastNodesExpanded() <= TIGHT, L"never more than the budget in one tick");
      ++ticks;
    }
    Assert::IsTrue(ticks > 1, L"a tight budget must take more than one tick, or it bounds nothing");
    const Frontier::Path* path = sim.Planner().Result(MOVER);
    Assert::IsTrue(path->state != Frontier::PathState::Planning, L"and it must still finish");
    Logger::WriteMessage((L"measured: the request expanded " + std::to_wstring(path->nodesExpanded) + L" nodes over " +
                          std::to_wstring(ticks) + L" ticks at a budget of " + std::to_wstring(TIGHT))
                           .c_str());
  }

  TEST_METHOD(PlacingAStructureRebuildsTheGraphAndOnlyWhenSomethingChanged)
  {
    // An obstruction changes which cells are passable, which is what a component is a statement
    // about, so every class's graph is dropped. What is under test is that it is dropped when the
    // grid changes and NOT when a write leaves it as it was: a rebuild on every structure tick
    // would be the expensive mistake.
    Frontier::LandscapeDefinition definition{};
    Assert::IsTrue(ReadDefinition(FixtureDirectory() / L"small-0002.json", definition));
    Frontier::Sim sim(TwoSeats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(definition));

    static_cast<void>(sim.Clusters().ComponentCount(Frontier::DriveClass::Legs));
    const std::uint64_t afterFirst = sim.Clusters().GraphBuilds();
    Assert::AreEqual(std::uint64_t{1}, afterFirst, L"one class asked for, one graph built");
    static_cast<void>(sim.Clusters().ComponentCount(Frontier::DriveClass::Legs));
    Assert::AreEqual(afterFirst, sim.Clusters().GraphBuilds(), L"and it is not built twice");

    sim.SetObstruction(8, 8, 0);
    static_cast<void>(sim.Clusters().ComponentCount(Frontier::DriveClass::Legs));
    Assert::AreEqual(afterFirst, sim.Clusters().GraphBuilds(), L"a write that changes nothing rebuilds nothing");

    sim.SetObstruction(8, 8, 1);
    static_cast<void>(sim.Clusters().ComponentCount(Frontier::DriveClass::Legs));
    Assert::AreEqual(afterFirst + 1, sim.Clusters().GraphBuilds(), L"and one that does, does");
    Assert::AreEqual(Frontier::NO_COMPONENT, sim.Clusters().ComponentAt(8, 8, Frontier::DriveClass::Legs), L"the cell is closed");
  }

  TEST_METHOD(AnObstructionClosesARouteAndTheGraphNoticesRatherThanServingAStalePath)
  {
    // The whole reason invalidation exists: a cached edge that says a cluster can be crossed, when
    // a structure now fills it, is a device walking into a wall.
    Frontier::Sim sim(TwoSeats(), Tables());
    Frontier::LandscapeDefinition definition{};
    definition.version = Frontier::LANDSCAPE_DEFINITION_VERSION;
    definition.sizeClass = Frontier::SizeClass::Small;
    definition.cellsPerSide = Frontier::SIZE_CLASS_CELLS[0];
    definition.seed = 1;
    definition.palette = "Default";
    // Flat and above sea level: amplitude 0 so there is no noise, and a height shift so the plain
    // is not the outside height of -26, which is under water and passable to nothing.
    definition.tiles = {{0, 0, 512, 170, 0, 60, 60, 70, 1, 0, ""}};
    Assert::IsTrue(sim.CreateLandscape(definition));
    const std::uint32_t side = sim.Terrain().CellsPerSide();

    sim.Planner().Request(MOVER, 2, 2, 40, 2, Frontier::DriveClass::Wheels);
    Assert::IsTrue(PlanFully(sim, MOVER).Usable(), L"open ground, so there is a way");

    // A wall from edge to edge, one cell wide, between the two.
    for (std::uint32_t y = 0; y < side; ++y)
    {
      sim.SetObstruction(20, y, 1);
    }
    sim.Planner().Request(MOVER, 2, 2, 40, 2, Frontier::DriveClass::Wheels);
    const Frontier::Path& blocked = PlanFully(sim, MOVER);
    Assert::IsTrue(blocked.state == Frontier::PathState::Unreachable, L"and now there is not");

    // One cell taken out of the wall, and the route is back.
    sim.SetObstruction(20, 10, 0);
    sim.Planner().Request(MOVER, 2, 2, 40, 2, Frontier::DriveClass::Wheels);
    Assert::IsTrue(PlanFully(sim, MOVER).Usable(), L"a gate is a gate");
  }

  TEST_METHOD(APathIsRefinedFromTheFrontAndFinishesAtTheDestination)
  {
    Frontier::Sim sim(TwoSeats(), Tables());
    Frontier::LandscapeDefinition definition{};
    definition.version = Frontier::LANDSCAPE_DEFINITION_VERSION;
    definition.sizeClass = Frontier::SizeClass::Small;
    definition.cellsPerSide = Frontier::SIZE_CLASS_CELLS[0];
    definition.seed = 1;
    definition.palette = "Default";
    definition.tiles = {{0, 0, 512, 170, 0, 60, 60, 70, 1, 0, ""}};
    Assert::IsTrue(sim.CreateLandscape(definition));

    sim.Planner().Request(MOVER, 2, 2, 120, 120, Frontier::DriveClass::Tracks);
    const Frontier::Path& path = PlanFully(sim, MOVER);
    Assert::IsTrue(path.Usable());
    Assert::IsFalse(path.cells.empty(), L"the front of a route is cells from the first answer");

    // Refining to the end: every step is one cell from the last, which is what a mover walks.
    std::uint32_t guard = 0;
    while (sim.Planner().Result(MOVER)->state == Frontier::PathState::Partial && guard < 200)
    {
      Assert::IsTrue(sim.Planner().RefineFurther(MOVER));
      ++guard;
    }
    const Frontier::Path& whole = *sim.Planner().Result(MOVER);
    Assert::IsTrue(whole.state == Frontier::PathState::Complete, L"and it refines all the way");
    Assert::IsTrue(whole.cells.back() == Frontier::PathCell{120, 120}, L"to the cell that was asked for");
    for (std::size_t step = 1; step < whole.cells.size(); ++step)
    {
      const std::uint32_t dx = whole.cells[step].x > whole.cells[step - 1].x ? whole.cells[step].x - whole.cells[step - 1].x
                                                                             : whole.cells[step - 1].x - whole.cells[step].x;
      const std::uint32_t dy = whole.cells[step].y > whole.cells[step - 1].y ? whole.cells[step].y - whole.cells[step - 1].y
                                                                             : whole.cells[step - 1].y - whole.cells[step].y;
      Assert::IsTrue(dx <= 1 && dy <= 1 && (dx + dy) > 0, L"every step is to a neighbouring cell");
    }
  }

  TEST_METHOD(ADestinationTheDriveCannotStandOnIsRefusedRatherThanSearched)
  {
    Frontier::LandscapeDefinition definition{};
    Assert::IsTrue(ReadDefinition(FixtureDirectory() / L"small-0001.json", definition));
    Frontier::Sim sim(TwoSeats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(definition));
    const std::uint32_t side = sim.Terrain().CellsPerSide();

    // A cell no wheeled drive can stand on: water, or too steep. Refusing is a different answer
    // from "no route", and a commander is owed the difference.
    for (std::uint32_t cell = 0; cell < side * side; ++cell)
    {
      const std::uint32_t x = cell % side;
      const std::uint32_t y = cell / side;
      if (sim.Clusters().Passable(x, y, Frontier::DriveClass::Wheels))
      {
        continue;
      }
      sim.Planner().Request(MOVER, 4, 4, x, y, Frontier::DriveClass::Wheels);
      Assert::IsTrue(sim.Planner().Result(MOVER)->state == Frontier::PathState::Refused);
      Assert::AreEqual(0u, sim.Planner().Result(MOVER)->nodesExpanded, L"and without expanding a node");
      return;
    }
    Logger::WriteMessage(L"this fixture has no impassable cell for wheels, so the case was not exercised");
  }

  TEST_METHOD(TheCostOfAFullyPlannedRequestIsMeasuredForTheTickAdr)
  {
    // TechnicalDesign.md §12 leaves hierarchical A* against flow fields to be decided by
    // measurement, and m1-vertical-slice/G3 is where it is decided. These are the numbers.
    Frontier::LandscapeDefinition definition{};
    Assert::IsTrue(ReadDefinition(FixtureDirectory() / L"small-0001.json", definition));
    Frontier::Sim sim(TwoSeats(), Tables());
    Assert::IsTrue(sim.CreateLandscape(definition));

    const auto start = std::chrono::steady_clock::now();
    std::uint32_t planned = 0;
    std::uint64_t nodes = 0;
    const std::uint32_t side = sim.Terrain().CellsPerSide();
    for (std::uint32_t cell = 0; cell < side * side && planned < 50; cell += 409)
    {
      const std::uint32_t toX = cell % side;
      const std::uint32_t toY = cell / side;
      if (!sim.Clusters().Passable(4, 4, Frontier::DriveClass::Legs) || !sim.Clusters().Passable(toX, toY, Frontier::DriveClass::Legs))
      {
        continue;
      }
      sim.Planner().Request(MOVER, 4, 4, toX, toY, Frontier::DriveClass::Legs);
      nodes += PlanFully(sim, MOVER).nodesExpanded;
      ++planned;
    }
    const auto end = std::chrono::steady_clock::now();
    const std::int64_t microseconds = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    Assert::IsTrue(planned > 0);
    const std::wstring message = L"measured: " + std::to_wstring(planned) + L" full Small-landscape requests took " +
                                 std::to_wstring(microseconds) + L" us, " + std::to_wstring(nodes / planned) +
                                 L" components expanded a request, over " +
                                 std::to_wstring(sim.Clusters().ComponentCount(Frontier::DriveClass::Legs)) + L" components and " +
                                 std::to_wstring(sim.Clusters().LocalSearches()) + L" cell searches";
    Logger::WriteMessage(message.c_str());
  }
};

} // namespace SimTests
