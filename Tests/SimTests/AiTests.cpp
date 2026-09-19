#include "pch.h"

#include "AiSeat.h"
#include "Construction.h"
#include "ContentLoader.h"
#include "Json.h"
#include "Sim.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// The scripted commander (TechnicalDesign.md §7; m1-vertical-slice/S12). This suite is the one that
// exercises every system at once: two scripted commanders play the slice landscape from a seed,
// with the real GameData tables, and everything from the economy to the damage matrix runs because
// they use it. What it asserts is not that the AI plays WELL - that is M2's, against a measured
// match - but that it plays at all, and that it does so as a pure function of the state and the
// tick.
namespace SimTests
{

namespace
{

constexpr std::int32_t CELL = Neuron::SUBUNITS_PER_CELL;

/// The repository's own tables and its slice landscape, read from the source tree the way the
/// landscape fixtures already are (Tests/SimTests/Fixtures/Landscape/README.md).
[[nodiscard]] std::filesystem::path GameData()
{
  return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "GameData";
}

const Frontier::ContentTree& Tables()
{
  static const Frontier::ContentTree TREE = []
  {
    Frontier::ContentTree tree;
    std::vector<Frontier::ContentDiagnostic> diagnostics;
    if (!Frontier::LoadContent(GameData(), tree, diagnostics))
    {
      // A suite that silently ran against an empty tree would assert nothing at all.
      const std::string what = diagnostics.empty() ? "no diagnostic" : diagnostics.front().ToString();
      Assert::Fail(std::wstring(what.begin(), what.end()).c_str());
    }
    return tree;
  }();
  return TREE;
}

const Frontier::LandscapeDefinition& Slice()
{
  static const Frontier::LandscapeDefinition DEFINITION = []
  {
    Frontier::LandscapeDefinition definition{};
    Frontier::ContentTree tree;
    std::vector<Frontier::ContentDiagnostic> diagnostics;
    if (!Frontier::LoadContent(GameData(), tree, diagnostics))
    {
      Assert::Fail(L"the content did not load");
    }
    for (const Frontier::LandscapeDefinition& landscape : tree.landscapes)
    {
      if (landscape.cellsPerSide == Frontier::SIZE_CLASS_CELLS[0] && landscape.starts.size() >= 2 && !landscape.deposits.empty())
      {
        definition = landscape;
      }
    }
    return definition;
  }();
  return DEFINITION;
}

Frontier::MatchSettings TwoScriptedSeats(std::uint32_t _survivalTicks = 0)
{
  Frontier::MatchSettings settings{};
  settings.seed = 20260919;
  settings.sizeClass = Frontier::SizeClass::Small;
  settings.seatCount = 2;
  settings.baseLevel = Frontier::BaseLevel::Nothing;
  settings.powerLevel = Frontier::PowerLevel::High;
  settings.deviceCapLevel = Frontier::DeviceCapLevel::Low;
  settings.victory = _survivalTicks == 0 ? Frontier::VictoryCondition::Annihilation : Frontier::VictoryCondition::Survival;
  settings.survivalTicks = _survivalTicks;
  // Research is the seat's own auto-research (m1-vertical-slice/S6), which is why there is no
  // research behaviour in the list: a scripted seat is set up with it on.
  settings.seats[0] = {Frontier::SeatKind::Ai, 0, true};
  settings.seats[1] = {Frontier::SeatKind::Ai, 1, true};
  return settings;
}

/// The row of the first structure with a role, in the real tables.
[[nodiscard]] std::uint32_t RowOf(Frontier::StructureRole _role)
{
  const std::vector<Frontier::StructureDesc>& rows = Tables().structures.structures;
  for (std::uint32_t row = 0; row < rows.size(); ++row)
  {
    if (rows[row].role == _role)
    {
      return row;
    }
  }
  Assert::Fail(L"the tables have no structure with that role");
}

/// The base level of GameDesign.md §2: nothing but a builder and a command post. Placed by the
/// test because NOTHING IN THE TREE OWNS BASE-LEVEL PLACEMENT YET - it is the application's
/// (m1-vertical-slice/G1), and until G1 lands a match is set up by whoever starts one.
void PlaceStartingBase(Frontier::Sim& _sim, std::uint8_t _seat, std::uint32_t _cellX, std::uint32_t _cellY)
{
  const std::uint32_t postRow = RowOf(Frontier::StructureRole::CommandPost);
  Frontier::Structure post{};
  post.seat = _seat;
  post.design = postRow;
  post.cellX = _cellX;
  post.cellY = _cellY;
  post.state = Frontier::StructurePhase::Standing;
  post.hitPoints = Tables().structures.structures[postRow].hitPoints;
  post.buildEffortHundredths = Frontier::RequiredEffortHundredths(Tables().structures.structures[postRow].buildTimeTicks);
  post.working = Frontier::NO_OBJECT;
  const Frontier::ObjectId placed = _sim.Objects().Create(post);
  const Frontier::StructureDesc& row = Tables().structures.structures[postRow];
  for (std::uint32_t y = 0; y < row.footprintCellsY; ++y)
  {
    for (std::uint32_t x = 0; x < row.footprintCellsX; ++x)
    {
      _sim.SetObstruction(_cellX + x, _cellY + y, 1);
    }
  }
  Assert::IsTrue(placed.Valid());

  // One builder, from a design the seat saves for itself: the AI's own designer picks it on its
  // first decision, so the starting truck is built from whatever the designer would have chosen.
  Frontier::AiBlackboard blackboard;
  Frontier::Observe(_sim, _seat, blackboard);
  Frontier::DeviceDesign design{};
  Assert::IsTrue(Frontier::BestDesign(_sim, _seat, Frontier::AiRole::Builder, blackboard, design), L"the tables offer no builder");
  _sim.SeatAt(_seat).designs.push_back(design);
  // On ground the truck's drive can actually stand on. A device dropped on a cliff has no component
  // in the cluster graph, so every route it is ever given comes back unreachable and it stands
  // where it was put for the whole match - which is exactly what the first run of this suite did
  // to the second commander, and it looked like an AI fault rather than a placement one.
  const Frontier::DriveClass drive = Tables().components.drives[_sim.Seats()[_seat].designs[0].drive].driveClass;
  std::uint32_t truckX = _cellX;
  std::uint32_t truckY = _cellY;
  bool standable = false;
  for (std::uint32_t ring = 3; ring < 16 && !standable; ++ring)
  {
    for (std::uint32_t y = _cellY > ring ? _cellY - ring : 0; y <= _cellY + ring && !standable; ++y)
    {
      for (std::uint32_t x = _cellX > ring ? _cellX - ring : 0; x <= _cellX + ring && !standable; ++x)
      {
        if (x < _sim.Terrain().CellsPerSide() && y < _sim.Terrain().CellsPerSide() && _sim.Clusters().Passable(x, y, drive) &&
            _sim.Clusters().ComponentAt(x, y, drive) != Frontier::NO_COMPONENT)
        {
          truckX = x;
          truckY = y;
          standable = true;
        }
      }
    }
  }
  Assert::IsTrue(standable, L"nowhere near the start that this drive can stand");

  Frontier::Device truck{};
  truck.seat = _seat;
  truck.design = 0;
  truck.x = static_cast<std::int32_t>(truckX) * CELL + CELL / 2;
  truck.z = static_cast<std::int32_t>(truckY) * CELL + CELL / 2;
  truck.hitPoints = 100;
  truck.target = Frontier::NO_OBJECT;
  Assert::IsTrue(_sim.Objects().Create(truck).Valid());
}

/// A match on the slice landscape with both seats set up as the lobby's "nothing" base level.
Frontier::Sim SliceMatch(std::uint32_t _survivalTicks = 0)
{
  Frontier::Sim sim(TwoScriptedSeats(_survivalTicks), Tables());
  Assert::IsTrue(sim.CreateLandscape(Slice()), L"the slice landscape did not generate");
  const std::vector<Frontier::CellPosition>& starts = Slice().starts;
  PlaceStartingBase(sim, 0, starts[0].x, starts[0].y);
  PlaceStartingBase(sim, 1, starts[1].x, starts[1].y);
  return sim;
}

[[nodiscard]] std::uint32_t CountStructures(const Frontier::Sim& _sim, std::uint8_t _seat, Frontier::StructureRole _role)
{
  std::uint32_t count = 0;
  _sim.Objects().ForEachStructure(
    [&](Frontier::ObjectId, const Frontier::Structure& _structure)
    {
      if (_structure.seat != _seat || _structure.state != Frontier::StructurePhase::Standing)
      {
        return;
      }
      if (_structure.design < _sim.Content().structures.structures.size() &&
          _sim.Content().structures.structures[_structure.design].role == _role)
      {
        ++count;
      }
    });
  return count;
}

[[nodiscard]] std::uint32_t CountDevices(const Frontier::Sim& _sim, std::uint8_t _seat)
{
  std::uint32_t count = 0;
  _sim.Objects().ForEachDevice([&](Frontier::ObjectId, const Frontier::Device& _device) { count += _device.seat == _seat ? 1 : 0; });
  return count;
}

} // namespace

TEST_CLASS(AiTests)
{
public:
  TEST_METHOD(TheDesignerPicksTheBestDamagePerPowerAgainstWhatItHasSeen)
  {
    Frontier::Sim sim = SliceMatch();
    Frontier::AiBlackboard blackboard;
    Frontier::Observe(sim, 0, blackboard);

    Frontier::DeviceDesign fighter{};
    Assert::IsTrue(Frontier::BestDesign(sim, 0, Frontier::AiRole::Fighter, blackboard, fighter));
    Frontier::DeviceDesign builder{};
    Assert::IsTrue(Frontier::BestDesign(sim, 0, Frontier::AiRole::Builder, blackboard, builder));
    Assert::IsTrue(fighter.modules[0] != builder.modules[0], L"a gun is not a builder");

    // With nothing seen, the score is the mean over every column; with a composition seen, it is
    // that composition's. What must be true is that the matrix makes a DIFFERENCE - that some
    // weapon in the tables is worth a different amount against tracks than against legs - because
    // otherwise the designer is an expensive way to pick the cheapest gun.
    Frontier::AiBlackboard againstTracks = blackboard;
    againstTracks.enemyColumns = {};
    againstTracks.enemyColumns[Frontier::TargetColumnOf(Frontier::DriveClass::Tracks)] = 10;
    Frontier::AiBlackboard againstLegs = blackboard;
    againstLegs.enemyColumns = {};
    againstLegs.enemyColumns[Frontier::TargetColumnOf(Frontier::DriveClass::Legs)] = 10;
    bool anyDifference = false;
    for (const Frontier::ModuleDesc& module : sim.Content().components.modules)
    {
      if (module.systemKind != Frontier::SystemKind::None)
      {
        continue;
      }
      anyDifference = anyDifference || Frontier::ExpectedDamage(sim.Content(), module, againstTracks) !=
                                         Frontier::ExpectedDamage(sim.Content(), module, againstLegs);
    }
    Assert::IsTrue(anyDifference, L"the matrix says something different about tracks than about legs");
  }

  TEST_METHOD(AScriptedCommanderBuildsAnEconomyAnArmyAndMeetsTheOther)
  {
    // The acceptance's first run: within 12,000 ticks - ten minutes - both commanders must have a
    // served extractor, a factory, a lab and a device of their own, and must have met.
    Frontier::Sim sim = SliceMatch();
    const auto started = std::chrono::steady_clock::now();
    std::uint32_t firstContactTick = 0;
    for (std::uint32_t tick = 0; tick < 12000 && !sim.Finished(); ++tick)
    {
      sim.Advance();
      if (firstContactTick == 0)
      {
        Frontier::AiBlackboard blackboard;
        Frontier::Observe(sim, 0, blackboard);
        firstContactTick = blackboard.contact ? sim.Tick() : 0;
      }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);

    for (std::uint8_t seat = 0; seat < 2; ++seat)
    {
      Assert::IsTrue(CountStructures(sim, seat, Frontier::StructureRole::Extractor) > 0, L"an extractor");
      Assert::IsTrue(CountStructures(sim, seat, Frontier::StructureRole::Generator) > 0, L"a generator to serve it");
      Assert::IsTrue(CountStructures(sim, seat, Frontier::StructureRole::Factory) > 0, L"a factory");
      Assert::IsTrue(CountStructures(sim, seat, Frontier::StructureRole::ResearchLab) > 0, L"a lab");
      Assert::IsTrue(CountDevices(sim, seat) > 1, L"and a device it built itself");
      Assert::IsFalse(sim.Seats()[seat].researchComplete.empty(), L"auto-research ran");
    }
    Assert::IsTrue(firstContactTick > 0, L"the two commanders met");
    Logger::WriteMessage(("    measured: two scripted commanders reached first contact on tick " + std::to_string(firstContactTick) +
                          "; 12,000 ticks took " + std::to_string(elapsed.count()) + " ms\n")
                           .c_str());
  }

  TEST_METHOD(TwoSimsFromOneSeedAgreeHashForHash)
  {
    // The determinism the whole design rests on, over the one system that decides for itself. If a
    // scripted commander read wall time, a float or an unordered container, this is where it shows.
    Frontier::Sim first = SliceMatch();
    Frontier::Sim second = SliceMatch();
    Assert::AreEqual(first.ComputeHash(), second.ComputeHash(), L"the same setup is the same state");
    for (std::uint32_t tick = 0; tick < 3000; ++tick)
    {
      first.Advance();
      second.Advance();
      if (first.Hash() != second.Hash())
      {
        Assert::Fail((L"they diverged on tick " + std::to_wstring(first.Tick())).c_str());
      }
    }
    Assert::IsTrue(first.Objects().NextId() > 4, L"and the match really did happen");
  }

  TEST_METHOD(AScriptedMatchReachesADecidedVictoryState)
  {
    // The acceptance's second run. The survival clock is what makes it affordable in CI: a
    // 36,000-tick annihilation between two commanders who both rebuild can run for ever, and what
    // the acceptance asks for is that the match REACHES a decided state, not that one of them wins
    // by conquest. The duration is recorded in the task's notes.
    Frontier::Sim sim = SliceMatch(36000);
    const auto started = std::chrono::steady_clock::now();
    std::uint32_t ticks = 0;
    while (!sim.Finished() && ticks < 40000)
    {
      sim.Advance();
      ++ticks;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);

    Assert::IsTrue(sim.Finished(), L"the match was decided");
    for (std::uint8_t seat = 0; seat < 2; ++seat)
    {
      Assert::IsTrue(sim.Seats()[seat].victory != Frontier::VictoryState::Playing, L"and every commander knows how it went");
    }
    Logger::WriteMessage(("    measured: a decided match took " + std::to_string(ticks) + " ticks and " + std::to_string(elapsed.count()) +
                          " ms, extracted " + std::to_string(sim.Seats()[0].extractedHundredths / 100) + " and " +
                          std::to_string(sim.Seats()[1].extractedHundredths / 100) + " power\n")
                           .c_str());
  }

  TEST_METHOD(AScriptedSeatEmitsItsOrdersThroughTheOrdinaryQueueForALaterTick)
  {
    // §7: "emits orders for tick t plus its delay through the same queue as a client". Nothing in
    // the AI reaches into the simulation directly, which is what makes an AI seat and a human seat
    // the same thing below stage 1.
    Frontier::Sim sim = SliceMatch();
    Assert::IsTrue(sim.Orders().Empty());
    Frontier::DecideForSeat(sim, 0);
    Assert::IsFalse(sim.Orders().Empty(), L"it decided something");
    const Frontier::Order& first = sim.Orders().Entries()[0].order;
    Assert::AreEqual(sim.Tick() + Frontier::AI_ORDER_DELAY_TICKS, first.tick, L"for a later tick");
    Assert::AreEqual(static_cast<int>(0), static_cast<int>(first.seat));
  }
};

} // namespace SimTests
