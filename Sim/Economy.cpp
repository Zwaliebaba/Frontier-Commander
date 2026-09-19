#include "pch.h"

#include "Economy.h"

#include "FixedPoint.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace Frontier
{

namespace
{

/// The centre of a footprint that starts at _cell and runs _extent cells, in subunits. A cell is a
/// power of two of subunits, so the half is exact and there is no rounding to disagree about.
[[nodiscard]] constexpr std::int64_t FootprintCenterSubunits(std::uint32_t _cell, std::uint32_t _extent) noexcept
{
  return static_cast<std::int64_t>(_cell) * Neuron::SUBUNITS_PER_CELL +
         static_cast<std::int64_t>(_extent) * (Neuron::SUBUNITS_PER_CELL / 2);
}

[[nodiscard]] constexpr std::int64_t DistanceSquared(std::int64_t _x0, std::int64_t _y0, std::int64_t _x1, std::int64_t _y1) noexcept
{
  const std::int64_t dx = _x0 - _x1;
  const std::int64_t dy = _y0 - _y1;
  return dx * dx + dy * dy;
}

/// The row a structure was built from, or null for a design index no table holds.
[[nodiscard]] const StructureDesc* RowOf(const ContentTree& _content, const Structure& _structure) noexcept
{
  const std::vector<StructureDesc>& rows = _content.structures.structures;
  return _structure.design < rows.size() ? &rows[_structure.design] : nullptr;
}

/// How many more extractors a generator's modules let it serve (GameDesign.md §5).
[[nodiscard]] std::uint32_t ExtraServed(const ContentTree& _content, const Structure& _structure) noexcept
{
  const std::vector<StructureModuleDesc>& rows = _content.structures.modules;
  std::int64_t extra = 0;
  for (std::uint8_t index = 0; index < _structure.moduleCount && index < MAX_STRUCTURE_MODULES; ++index)
  {
    const std::uint32_t module = _structure.modules[index];
    if (module < rows.size() && rows[module].effect == StructureModuleEffect::ServeMoreExtractors && rows[module].amount > 0)
    {
      extra += rows[module].amount;
    }
  }
  return static_cast<std::uint32_t>(std::min<std::int64_t>(extra, static_cast<std::int64_t>(MAX_STRUCTURE_MODULES) * 16));
}

} // namespace

void Economy::SetLandscape(const Landscape& _landscape)
{
  if (_landscape.Created())
  {
    m_deposits.Build(_landscape.Definition());
  }
  else
  {
    m_deposits.Clear();
  }
}

std::int32_t Economy::StockpileCapHundredths(std::uint32_t _standingGenerators) noexcept
{
  // Bounded by the structure cap, so the product cannot overflow: 300 generators is 15,000,000
  // hundredths, well inside an int32.
  const std::int64_t cap = BASE_STOCKPILE_CAP_HUNDREDTHS +
                           static_cast<std::int64_t>(std::min(_standingGenerators, STRUCTURE_CAP)) * GENERATOR_STOCKPILE_CAP_HUNDREDTHS;
  return static_cast<std::int32_t>(cap);
}

bool Economy::Draw(Seat& _seat, std::int32_t _costHundredths) noexcept
{
  if (_costHundredths <= 0)
  {
    return true;
  }
  if (_seat.powerHundredths < _costHundredths)
  {
    return false;
  }
  _seat.powerHundredths -= _costHundredths;
  return true;
}

void Economy::Credit(Seat& _seat, std::int32_t _amountHundredths) noexcept
{
  if (_amountHundredths <= 0)
  {
    return;
  }
  // Already at or over the cap: the credit is lost rather than banked, and the balance is left as
  // it is. A stockpile over its cap is reachable from the lobby (PowerLevel::High starts at 2,500
  // against a base cap of 1,000), so this is a case a match meets on its first tick.
  if (_seat.powerHundredths >= _seat.stockpileCapHundredths)
  {
    return;
  }
  const std::int64_t credited = static_cast<std::int64_t>(_seat.powerHundredths) + _amountHundredths;
  _seat.powerHundredths = static_cast<std::int32_t>(std::min<std::int64_t>(credited, _seat.stockpileCapHundredths));
}

void Economy::RefundCanceled(Seat& _seat, std::int32_t _costHundredths, std::int32_t _buildProgressHundredths) noexcept
{
  if (_costHundredths <= 0)
  {
    return;
  }
  const std::int64_t progress = std::clamp<std::int64_t>(_buildProgressHundredths, 0, 10000);
  // The share not yet built, rounded down, so that cancelling can never return more than was spent.
  const std::int64_t refund = (static_cast<std::int64_t>(_costHundredths) * (10000 - progress)) / 10000;
  Credit(_seat, static_cast<std::int32_t>(refund));
}

void Economy::RefundDemolished(Seat& _seat, std::int32_t _costHundredths) noexcept
{
  if (_costHundredths <= 0)
  {
    return;
  }
  Credit(_seat, static_cast<std::int32_t>(static_cast<std::int64_t>(_costHundredths) / 2));
}

bool Economy::Served(ObjectId _extractor) const noexcept
{
  return ServingGenerator(_extractor) != NO_OBJECT;
}

ObjectId Economy::ServingGenerator(ObjectId _extractor) const noexcept
{
  const auto found = std::lower_bound(m_service.begin(), m_service.end(), _extractor, [](const ServedExtractor& _entry, ObjectId _wanted)
                                      { return _entry.extractor.value < _wanted.value; });
  if (found == m_service.end() || found->extractor != _extractor)
  {
    return NO_OBJECT;
  }
  return found->generator;
}

void Economy::CollectSites(const World& _world, const ContentTree& _content, std::uint8_t _seat, std::int32_t _extractorRatePercent)
{
  m_generators.clear();
  m_extractors.clear();
  _world.ForEachStructure(
    [this, &_content, _seat, _extractorRatePercent](ObjectId _id, const Structure& _structure)
    {
      if (_structure.seat != _seat || _structure.state != StructureState::Standing)
      {
        return;
      }
      const StructureDesc* row = RowOf(_content, _structure);
      if (row == nullptr)
      {
        return;
      }
      if (row->role == StructureRole::Generator)
      {
        const std::int64_t range = std::max<std::int64_t>(row->serviceRangeSubunits, 0);
        m_generators.push_back({_id, FootprintCenterSubunits(_structure.cellX, row->footprintCellsX),
                                FootprintCenterSubunits(_structure.cellY, row->footprintCellsY), 0,
                                row->servesExtractors + ExtraServed(_content, _structure), range * range});
        return;
      }
      // An extractor produces only where a deposit is. Placement already refuses any other cell,
      // so this is the same rule read from the other end: a snapshot that arrived with an
      // extractor standing on bare ground yields nothing rather than inventing power.
      if (row->role == StructureRole::Extractor && m_deposits.Has(_structure.cellX, _structure.cellY))
      {
        const std::int32_t yield =
          static_cast<std::int32_t>(static_cast<std::int64_t>(row->powerHundredthsPerTick) * (100 + _extractorRatePercent) / 100);
        m_extractors.push_back({_id, FootprintCenterSubunits(_structure.cellX, row->footprintCellsX),
                                FootprintCenterSubunits(_structure.cellY, row->footprintCellsY), yield, 0, 0});
      }
    });
  // ForEachStructure walks in ascending id, so both arrays are already in the order the
  // assignment's tie-break wants.
}

std::int64_t Economy::AssignService()
{
  if (m_generators.empty() || m_extractors.empty())
  {
    return 0;
  }
  std::int64_t income = 0;
  // The extractors this pass has already given away. Indexed as m_extractors is, so the lookup is
  // a subscript rather than a search.
  std::vector<bool> taken(m_extractors.size(), false);
  // Reused across generators so the pass allocates once rather than once a generator.
  std::vector<std::pair<std::int64_t, std::size_t>> reachable;
  for (const Site& generator : m_generators)
  {
    if (generator.serves == 0)
    {
      continue;
    }
    reachable.clear();
    for (std::size_t index = 0; index < m_extractors.size(); ++index)
    {
      if (taken[index])
      {
        continue;
      }
      const std::int64_t distance =
        DistanceSquared(generator.centerX, generator.centerY, m_extractors[index].centerX, m_extractors[index].centerY);
      if (distance <= generator.rangeSquared)
      {
        reachable.emplace_back(distance, index);
      }
    }
    // Nearest first, and ties by ascending id: m_extractors is in ascending id, so the index is
    // the id's order and comparing the pair lexicographically is the rule GameDesign.md §4 states.
    std::sort(reachable.begin(), reachable.end());
    const std::size_t serves = std::min<std::size_t>(generator.serves, reachable.size());
    for (std::size_t rank = 0; rank < serves; ++rank)
    {
      const std::size_t index = reachable[rank].second;
      taken[index] = true;
      m_service.push_back({m_extractors[index].id, generator.id});
      // The yield is the extractor's own row, added where the extractor is chosen: a served
      // extractor is served exactly once, so this cannot double-count and needs no second walk.
      income += m_extractors[index].powerHundredthsPerTick;
    }
  }
  return income;
}

void Economy::Advance(const World& _world, std::span<Seat> _seats, const ContentTree& _content)
{
  m_service.clear();

  // One walk of the world for the counts, rather than a running total every system would have to
  // remember to adjust. The Seat's fields stay, because validation tests them on every order and a
  // subscript beats a walk; what changes is that they are filled by counting once a tick, which no
  // system can forget to do and no pair of systems can disagree about.
  std::array<std::uint32_t, MAX_SEATS> devices{};
  std::array<std::uint32_t, MAX_SEATS> structures{};
  std::array<std::uint32_t, MAX_SEATS> generators{};
  _world.ForEachDevice(
    [&devices](ObjectId, const Device& _device)
    {
      if (_device.seat < MAX_SEATS)
      {
        ++devices[_device.seat];
      }
    });
  _world.ForEachStructure(
    [&structures, &generators, &_content](ObjectId, const Structure& _structure)
    {
      if (_structure.seat >= MAX_SEATS)
      {
        return;
      }
      // Every structure the seat holds counts, a plan included: the cap is what a placement is
      // refused against, so a cap that ignored plans could be walked past by placing them.
      ++structures[_structure.seat];
      const StructureDesc* row = RowOf(_content, _structure);
      if (row != nullptr && row->role == StructureRole::Generator && _structure.state == StructureState::Standing)
      {
        ++generators[_structure.seat];
      }
    });

  for (std::size_t index = 0; index < _seats.size() && index < MAX_SEATS; ++index)
  {
    Seat& seat = _seats[index];
    seat.deviceCount = devices[index];
    seat.structureCount = structures[index];
    seat.stockpileCapHundredths = StockpileCapHundredths(generators[index]);

    // The tick's income: a served extractor and a command post, both from their rows, so that
    // rebalancing the numbers is an edit to Structures.json and not to this file.
    const auto seatIndex = static_cast<std::uint8_t>(index);
    CollectSites(_world, _content, seatIndex, seat.upgrades.extractorRatePercent);
    // What the served extractors produced this tick. It is banked on the seat before the stockpile
    // cap or the command post's trickle touch it, because the Survival condition is decided on
    // what a commander EXTRACTED (GameDesign.md §2) - power lost to a full stockpile was still
    // extracted, and the command post's own output never was.
    const std::int64_t extracted = AssignService();
    seat.extractedHundredths += extracted;
    std::int64_t income = extracted;
    _world.ForEachStructure(
      [&income, &_content, seatIndex](ObjectId, const Structure& _structure)
      {
        if (_structure.seat != seatIndex || _structure.state != StructureState::Standing)
        {
          return;
        }
        const StructureDesc* row = RowOf(_content, _structure);
        if (row != nullptr && row->role == StructureRole::CommandPost)
        {
          income += row->powerHundredthsPerTick;
        }
      });
    Credit(seat, static_cast<std::int32_t>(std::min<std::int64_t>(income, std::numeric_limits<std::int32_t>::max())));
  }

  // Ascending by extractor id across every seat, so that ServingGenerator can bisect and so that
  // two hosts hold the list in one order whatever order the seats were walked in.
  std::sort(m_service.begin(), m_service.end(),
            [](const ServedExtractor& _left, const ServedExtractor& _right) { return _left.extractor.value < _right.extractor.value; });
}

} // namespace Frontier
