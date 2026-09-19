#include "pch.h"

#include "AiBlackboard.h"

#include "Economy.h"
#include "Sim.h"

#include <algorithm>

namespace Frontier
{

namespace
{

[[nodiscard]] const StructureDesc* RowOf(const ContentTree& _content, std::uint32_t _design) noexcept
{
  return _design < _content.structures.structures.size() ? &_content.structures.structures[_design] : nullptr;
}

/// Which of the five things an AI counts this structure is, or nothing.
[[nodiscard]] bool NeedOf(StructureRole _role, AiNeed& _out) noexcept
{
  switch (_role)
  {
  case StructureRole::CommandPost:
    _out = AiNeed::CommandPost;
    return true;
  case StructureRole::Extractor:
    _out = AiNeed::Extractor;
    return true;
  case StructureRole::Generator:
    _out = AiNeed::Generator;
    return true;
  case StructureRole::Factory:
    _out = AiNeed::Factory;
    return true;
  case StructureRole::ResearchLab:
    _out = AiNeed::Lab;
    return true;
  default:
    return false;
  }
}

[[nodiscard]] std::uint32_t CellOf(std::int32_t _subunits) noexcept
{
  return _subunits <= 0 ? 0u : static_cast<std::uint32_t>(_subunits / Neuron::SUBUNITS_PER_CELL);
}

/// True when this design carries a builder module, and true when it carries a weapon. A design can
/// be both; the counts below then count it as both, which is what it is.
void RolesOf(const ContentTree& _content, const Seat& _seat, std::uint32_t _design, bool& _builder, bool& _fighter) noexcept
{
  _builder = false;
  _fighter = false;
  if (_design >= _seat.designs.size())
  {
    return;
  }
  const DeviceDesign& design = _seat.designs[_design];
  for (std::uint8_t index = 0; index < design.moduleCount && index < MAX_MOUNTS; ++index)
  {
    const std::uint32_t row = design.modules[index];
    if (row >= _content.components.modules.size())
    {
      continue;
    }
    const ModuleDesc& module = _content.components.modules[row];
    _builder = _builder || module.systemKind == SystemKind::Builder;
    _fighter = _fighter || module.systemKind == SystemKind::None;
  }
}

} // namespace

void Observe(const Sim& _sim, std::uint8_t _seat, AiBlackboard& _out)
{
  _out = {};
  if (_seat >= _sim.Seats().size())
  {
    return;
  }
  const Seat& seat = _sim.Seats()[_seat];
  const World& world = _sim.Objects();
  const ContentTree& content = _sim.Content();
  const std::uint32_t cellsPerSide = seat.fog.CellsPerSide();

  _out.seat = _seat;
  _out.powerHundredths = seat.powerHundredths;
  _out.stockpileCapHundredths = seat.stockpileCapHundredths;
  _out.clustersPerSide = cellsPerSide == 0 ? 0 : (cellsPerSide + THREAT_CLUSTER_CELLS - 1) / THREAT_CLUSTER_CELLS;
  _out.threatByCluster.assign(static_cast<std::size_t>(_out.clustersPerSide) * _out.clustersPerSide, 0);

  std::vector<CellPosition> takenDeposits;
  world.ForEachStructure(
    [&](ObjectId _id, const Structure& _structure)
    {
      const StructureDesc* row = RowOf(content, _structure.design);
      if (row == nullptr)
      {
        return;
      }
      if (_structure.seat == _seat)
      {
        AiNeed need{};
        if (NeedOf(row->role, need) && _structure.state != StructurePhase::Demolishing)
        {
          ++_out.standing[static_cast<std::size_t>(need)];
        }
        if (row->role == StructureRole::Extractor)
        {
          takenDeposits.push_back({_structure.cellX, _structure.cellY});
          if (_structure.state == StructurePhase::Standing && !_sim.Power().Served(_id))
          {
            ++_out.unservedExtractors;
          }
        }
        if (row->role == StructureRole::Factory && _structure.state == StructurePhase::Standing && !_structure.working.Valid())
        {
          // Nothing queued for it either. A factory's queue is the seat's list (Sim/Design.h), and
          // an order submitted for tick t+2 is judged two ticks after this walk - so a factory that
          // is "idle" only because its queue has not started yet would be queued again every
          // decision and every one of those would be refused.
          const bool queued = std::any_of(seat.production.begin(), seat.production.end(),
                                          [_id](const ProductionEntry& _entry) { return _entry.factory == _id; });
          if (!queued)
          {
            _out.idleFactories.push_back(_id);
          }
        }
        if (_structure.state == StructurePhase::Plan || _structure.state == StructurePhase::UnderConstruction)
        {
          _out.unfinished.push_back(_id);
          _out.unfinishedPlaces.push_back({_structure.cellX, _structure.cellY});
        }
        return;
      }
      // An enemy structure it can see now. A different alliance, not merely a different seat.
      if (_sim.Seats()[_structure.seat].alliance == seat.alliance)
      {
        return;
      }
      if (seat.fog.Inside(_structure.cellX, _structure.cellY) && seat.fog.Visible(_structure.cellX, _structure.cellY))
      {
        _out.knownEnemyStructures.push_back(_id);
        _out.knownEnemyPlaces.push_back({_structure.cellX, _structure.cellY});
        _out.enemyColumns[TargetColumnOf(row->strength)] += 1;
        _out.contact = true;
      }
    });

  // And what it REMEMBERS: a base it scouted once is still somewhere to attack, which is the whole
  // reason the ghost store exists (TechnicalDesign.md §4.6).
  for (const Ghost& ghost : seat.ghosts.All())
  {
    const bool seenNow =
      std::find(_out.knownEnemyStructures.begin(), _out.knownEnemyStructures.end(), ghost.structure) != _out.knownEnemyStructures.end();
    if (!seenNow && ghost.seat < _sim.Seats().size() && _sim.Seats()[ghost.seat].alliance != seat.alliance)
    {
      _out.knownEnemyStructures.push_back(ghost.structure);
      _out.knownEnemyPlaces.push_back({ghost.cellX, ghost.cellY});
      _out.contact = true;
    }
  }

  world.ForEachDevice(
    [&](ObjectId _id, const Device& _device)
    {
      if (_device.seat == _seat)
      {
        bool builder = false;
        bool fighter = false;
        RolesOf(content, seat, _device.design, builder, fighter);
        _out.builders += builder ? 1 : 0;
        _out.fighters += fighter ? 1 : 0;
        if (builder)
        {
          _out.builderDevices.push_back(_id);
        }
        if (fighter && _device.primaryOrder == PrimaryOrder::Stop)
        {
          _out.idleFighters.push_back(_id);
        }
        return;
      }
      if (_device.seat >= _sim.Seats().size() || _sim.Seats()[_device.seat].alliance == seat.alliance)
      {
        return;
      }
      const std::uint32_t cellX = CellOf(_device.x);
      const std::uint32_t cellY = CellOf(_device.z);
      if (!seat.fog.Inside(cellX, cellY) || !seat.fog.Visible(cellX, cellY))
      {
        return;
      }
      ++_out.enemyDevicesSeen;
      _out.contact = true;
      if (_device.seat < _sim.Seats().size() && _device.design < _sim.Seats()[_device.seat].designs.size())
      {
        const DeviceDesign& design = _sim.Seats()[_device.seat].designs[_device.design];
        if (design.drive < content.components.drives.size())
        {
          _out.enemyColumns[TargetColumnOf(content.components.drives[design.drive].driveClass)] += 1;
        }
      }
      const std::uint32_t cluster = (cellY / THREAT_CLUSTER_CELLS) * _out.clustersPerSide + cellX / THREAT_CLUSTER_CELLS;
      if (cluster < _out.threatByCluster.size())
      {
        ++_out.threatByCluster[cluster];
      }
    });

  // The deposits nobody of this commander's has claimed, nearest first from wherever he is. A
  // deposit an ENEMY extractor stands on looks free from here, and the placement check is what
  // refuses it - which is the same answer a human gets for clicking there.
  const DepositField& deposits = _sim.Power().Deposits();
  std::uint32_t homeX = 0;
  std::uint32_t homeY = 0;
  world.ForEachStructure(
    [&](ObjectId, const Structure& _structure)
    {
      const StructureDesc* row = RowOf(content, _structure.design);
      if (_structure.seat == _seat && row != nullptr && row->role == StructureRole::CommandPost)
      {
        homeX = _structure.cellX;
        homeY = _structure.cellY;
      }
    });
  for (const Deposit& deposit : deposits.All())
  {
    const CellPosition cell{deposit.cellX, deposit.cellY};
    const bool taken = std::find(takenDeposits.begin(), takenDeposits.end(), cell) != takenDeposits.end();
    const std::int64_t dx = static_cast<std::int64_t>(cell.x) - homeX;
    const std::int64_t dy = static_cast<std::int64_t>(cell.y) - homeY;
    const bool withinReach = dx * dx + dy * dy <= static_cast<std::int64_t>(AI_EXPANSION_RANGE_CELLS) * AI_EXPANSION_RANGE_CELLS;
    if (!taken && withinReach)
    {
      _out.freeDeposits.push_back(cell);
    }
  }
  std::sort(_out.freeDeposits.begin(), _out.freeDeposits.end(),
            [homeX, homeY](const CellPosition& _a, const CellPosition& _b)
            {
              const auto distance = [homeX, homeY](const CellPosition& _cell)
              {
                const std::int64_t dx = static_cast<std::int64_t>(_cell.x) - homeX;
                const std::int64_t dy = static_cast<std::int64_t>(_cell.y) - homeY;
                return dx * dx + dy * dy;
              };
              const std::int64_t left = distance(_a);
              const std::int64_t right = distance(_b);
              // The tie-break is the cell itself, so that two hosts sort the same list one way.
              return left != right ? left < right : (_a.y != _b.y ? _a.y < _b.y : _a.x < _b.x);
            });
}

} // namespace Frontier
