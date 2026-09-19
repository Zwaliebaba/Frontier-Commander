#include "pch.h"

#include "StartingBase.h"

#include "AiBlackboard.h"
#include "AiDesigner.h"
#include "Construction.h"
#include "FixedPoint.h"
#include "Log.h"
#include "Movement.h"
#include "Placement.h"

#include <string>

namespace Frontier
{

namespace
{

/// How far from the start a builder may be set down. Three cells out, so that it is never inside
/// the command post's own footprint, and sixteen at the furthest, which is a quarter of the Slice
/// landscape: past that the truck is not at this base at all and the landscape's start is wrong.
constexpr std::uint32_t BUILDER_NEAREST_RING = 3;
constexpr std::uint32_t BUILDER_FURTHEST_RING = 16;

/// The row of the first structure with a role, or NO_ROW.
constexpr std::uint32_t NO_ROW = 0xFFFFFFFFu;

[[nodiscard]] std::uint32_t RowOf(const ContentTree& _content, StructureRole _role) noexcept
{
  const std::vector<StructureDesc>& rows = _content.structures.structures;
  for (std::uint32_t row = 0; row < rows.size(); ++row)
  {
    if (rows[row].role == _role)
    {
      return row;
    }
  }
  return NO_ROW;
}

/// The command post, standing and whole, with the ground under it levelled exactly as
/// Sim/Construction.cpp levels it when a builder starts one.
[[nodiscard]] bool PlaceCommandPost(Sim& _sim, std::uint8_t _seat, const CellPosition& _start)
{
  const std::uint32_t row = RowOf(_sim.Content(), StructureRole::CommandPost);
  if (row == NO_ROW)
  {
    Neuron::Log::Write(Neuron::LogLevel::Error, "starting base: the tables carry no structure with the CommandPost role");
    return false;
  }
  const StructureDesc& post = _sim.Content().structures.structures[row];
  const Footprint footprint = FootprintAt(post, _start.x, _start.y);

  // The flatten FIRST, so that the height the structure records is the height it will stand at.
  // An empty delta means the footprint is not on the landscape, which is a landscape whose start
  // is off its own edge.
  const HeightDelta delta = FlattenDelta(_sim.Terrain(), footprint);
  if (delta.heights.empty())
  {
    Neuron::Log::Write(Neuron::LogLevel::Error, "starting base: seat " + std::to_string(_seat) + "'s start at cell (" +
                                                  std::to_string(_start.x) + ", " + std::to_string(_start.y) + ") is not on the landscape");
    return false;
  }
  const std::int32_t mean = FootprintMeanHeightWorldUnits(_sim.Terrain(), footprint);
  if (!_sim.FlattenTerrain(delta))
  {
    Neuron::Log::Write(Neuron::LogLevel::Error,
                       "starting base: the landscape refused the flatten under seat " + std::to_string(_seat) + "'s command post");
    return false;
  }

  Structure standing{};
  standing.seat = _seat;
  standing.design = row;
  standing.cellX = footprint.cellX;
  standing.cellY = footprint.cellY;
  standing.y = mean * Neuron::SUBUNITS_PER_WORLD_UNIT;
  standing.state = StructurePhase::Standing;
  standing.hitPoints = post.hitPoints;
  standing.buildEffortHundredths = RequiredEffortHundredths(post.buildTimeTicks);
  standing.working = NO_OBJECT;
  if (!_sim.Objects().Create(standing).Valid())
  {
    Neuron::Log::Write(Neuron::LogLevel::Error, "starting base: seat " + std::to_string(_seat) + "'s command post could not be created");
    return false;
  }
  for (std::uint32_t y = 0; y < footprint.cellsY; ++y)
  {
    for (std::uint32_t x = 0; x < footprint.cellsX; ++x)
    {
      _sim.SetObstruction(footprint.cellX + x, footprint.cellY + y, OBSTRUCTION_STRUCTURE);
    }
  }
  return true;
}

/// The cell nearest the start that _drive can stand on AND that the cluster graph has a component
/// for. Both, and not just the first: a device dropped on ground its drive can cross but that no
/// component reaches has every route come back unreachable and stands where it was put for the
/// whole match. UINT32_MAX in both fields when there is none within BUILDER_FURTHEST_RING.
[[nodiscard]] CellPosition StandableNear(const Sim& _sim, const CellPosition& _start, DriveClass _drive)
{
  const std::uint32_t side = _sim.Terrain().CellsPerSide();
  for (std::uint32_t ring = BUILDER_NEAREST_RING; ring < BUILDER_FURTHEST_RING; ++ring)
  {
    const std::uint32_t lowY = _start.y > ring ? _start.y - ring : 0;
    const std::uint32_t lowX = _start.x > ring ? _start.x - ring : 0;
    for (std::uint32_t y = lowY; y <= _start.y + ring; ++y)
    {
      for (std::uint32_t x = lowX; x <= _start.x + ring; ++x)
      {
        if (x < side && y < side && _sim.Clusters().Passable(x, y, _drive) && _sim.Clusters().ComponentAt(x, y, _drive) != NO_COMPONENT)
        {
          return {x, y};
        }
      }
    }
  }
  return {0xFFFFFFFFu, 0xFFFFFFFFu};
}

/// One builder, from a design the seat saves as its own. The design is the AI designer's choice of
/// builder even for a human seat, which is not a shortcut: BestDesign picks the best builder the
/// seat's unlocked tables offer, which is what a commander would pick too, and it is the same truck
/// the scripted opponent starts with - so neither commander begins the match a chassis ahead.
[[nodiscard]] bool PlaceBuilder(Sim& _sim, std::uint8_t _seat, const CellPosition& _start)
{
  AiBlackboard blackboard;
  Observe(_sim, _seat, blackboard);
  DeviceDesign design{};
  if (!BestDesign(_sim, _seat, AiRole::Builder, blackboard, design))
  {
    Neuron::Log::Write(Neuron::LogLevel::Error,
                       "starting base: the tables offer seat " + std::to_string(_seat) + " no builder to start with");
    return false;
  }
  const std::uint32_t slot = static_cast<std::uint32_t>(_sim.SeatAt(_seat).designs.size());
  _sim.SeatAt(_seat).designs.push_back(design);

  const DriveClass drive = _sim.Content().components.drives[design.drive].driveClass;
  const CellPosition standable = StandableNear(_sim, _start, drive);
  if (standable.x == 0xFFFFFFFFu)
  {
    Neuron::Log::Write(Neuron::LogLevel::Error, "starting base: nothing within " + std::to_string(BUILDER_FURTHEST_RING) +
                                                  " cells of seat " + std::to_string(_seat) +
                                                  "'s start is ground its builder can stand on");
    return false;
  }

  Device builder{};
  builder.seat = _seat;
  builder.design = slot;
  builder.x = static_cast<std::int32_t>(standable.x) * Neuron::SUBUNITS_PER_CELL + Neuron::SUBUNITS_PER_CELL / 2;
  builder.z = static_cast<std::int32_t>(standable.y) * Neuron::SUBUNITS_PER_CELL + Neuron::SUBUNITS_PER_CELL / 2;
  // On the ground rather than at zero. Movement writes this every tick it moves, but the first
  // frame is drawn before the first tick moves anything and a truck at zero is a truck at sea level.
  builder.y = GroundHeightSubunits(_sim.Terrain(), builder.x, builder.z);
  builder.hitPoints = 100;
  builder.target = NO_OBJECT;
  if (!_sim.Objects().Create(builder).Valid())
  {
    Neuron::Log::Write(Neuron::LogLevel::Error, "starting base: seat " + std::to_string(_seat) + "'s builder could not be created");
    return false;
  }
  return true;
}

} // namespace

bool PlaceStartingBase(Sim& _sim, std::uint8_t _seat, const CellPosition& _start, BaseLevel _level)
{
  if (_level != BaseLevel::Nothing)
  {
    Neuron::Log::Write(Neuron::LogLevel::Error,
                       "starting base: only the Nothing base level is placed; Small and Established arrive with M2's lobby");
    return false;
  }
  return PlaceCommandPost(_sim, _seat, _start) && PlaceBuilder(_sim, _seat, _start);
}

} // namespace Frontier
