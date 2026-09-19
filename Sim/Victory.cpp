#include "pch.h"

#include "Victory.h"

#include "Sim.h"

#include <array>

namespace Frontier
{

namespace
{

/// What each seat holds that the annihilation rule counts, from one walk of the world rather than
/// one a seat: the world is walked at most twice however many commanders are playing.
struct Holdings
{
  std::array<bool, MAX_SEATS> structures{};
  std::array<bool, MAX_SEATS> builders{};
};

[[nodiscard]] bool CarriesBuilder(const Device& _device, std::span<const Seat> _seats, const ContentTree& _content) noexcept
{
  if (_device.seat >= _seats.size())
  {
    return false;
  }
  const Seat& seat = _seats[_device.seat];
  if (_device.design >= seat.designs.size())
  {
    return false;
  }
  const DeviceDesign& design = seat.designs[_device.design];
  for (std::uint8_t index = 0; index < design.moduleCount && index < MAX_MOUNTS; ++index)
  {
    const std::uint32_t row = design.modules[index];
    if (row < _content.components.modules.size() && _content.components.modules[row].systemKind == SystemKind::Builder)
    {
      return true;
    }
  }
  return false;
}

[[nodiscard]] Holdings WalkHoldings(const Sim& _sim)
{
  Holdings held;
  const std::span<const Seat> seats = _sim.Seats();
  const ContentTree& content = _sim.Content();
  _sim.Objects().ForEachStructure(
    [&held](ObjectId, const Structure& _structure)
    {
      if (_structure.seat < MAX_SEATS && _structure.state != StructurePhase::Plan)
      {
        held.structures[_structure.seat] = true;
      }
    });
  _sim.Objects().ForEachDevice(
    [&held, seats, &content](ObjectId, const Device& _device)
    {
      if (_device.seat < MAX_SEATS && !held.builders[_device.seat] && CarriesBuilder(_device, seats, content))
      {
        held.builders[_device.seat] = true;
      }
    });
  return held;
}

/// The alliances with a seat still playing, and how many there are.
struct Standing
{
  std::array<bool, 256> alliances{};
  std::uint32_t count = 0;

  [[nodiscard]] std::uint8_t Only() const noexcept
  {
    for (std::size_t alliance = 0; alliance < alliances.size(); ++alliance)
    {
      if (alliances[alliance])
      {
        return static_cast<std::uint8_t>(alliance);
      }
    }
    return NO_ALLIANCE;
  }
};

[[nodiscard]] Standing StandingAlliances(std::span<const Seat> _seats) noexcept
{
  Standing standing;
  for (const Seat& seat : _seats)
  {
    if (seat.kind != SeatKind::Empty && seat.victory == VictoryState::Playing && !standing.alliances[seat.alliance])
    {
      standing.alliances[seat.alliance] = true;
      ++standing.count;
    }
  }
  return standing;
}

/// The Survival ruling of GameDesign.md §2: at the clock, the side that extracted the most power
/// wins, and a tie is a draw. Extracted rather than held, so that a commander who spent what he
/// dug is not beaten by one who sat on it - Seat::extractedHundredths is the running total stage 2
/// keeps.
[[nodiscard]] std::uint8_t RichestAlliance(std::span<const Seat> _seats, const Standing& _standing) noexcept
{
  std::array<std::int64_t, 256> extracted{};
  for (const Seat& seat : _seats)
  {
    if (seat.kind != SeatKind::Empty && seat.victory == VictoryState::Playing)
    {
      extracted[seat.alliance] += seat.extractedHundredths;
    }
  }
  std::int64_t best = -1;
  std::uint8_t winner = NO_ALLIANCE;
  bool tied = false;
  for (std::size_t alliance = 0; alliance < extracted.size(); ++alliance)
  {
    if (!_standing.alliances[alliance])
    {
      continue;
    }
    if (extracted[alliance] > best)
    {
      best = extracted[alliance];
      winner = static_cast<std::uint8_t>(alliance);
      tied = false;
    }
    else if (extracted[alliance] == best)
    {
      tied = true;
    }
  }
  return tied ? NO_ALLIANCE : winner;
}

} // namespace

bool Annihilated(const Sim& _sim, std::uint8_t _seat)
{
  if (_seat >= _sim.Seats().size())
  {
    return false;
  }
  const Holdings held = WalkHoldings(_sim);
  return !held.structures[_seat] && !held.builders[_seat];
}

void CheckVictory(Sim& _sim)
{
  if (_sim.Finished())
  {
    return;
  }

  // Annihilation first, because it is what decides who is still standing. A seat is out when it
  // holds neither a structure nor a builder AND has held one of them at some point: without the
  // second half a match would end on its first tick, before the lobby's base level has been
  // placed, and so would every test that advances a tick over an empty world.
  const Holdings held = WalkHoldings(_sim);
  const std::uint8_t seatCount = static_cast<std::uint8_t>(_sim.Seats().size());
  for (std::uint8_t index = 0; index < seatCount; ++index)
  {
    Seat& seat = _sim.SeatAt(index);
    if (seat.kind == SeatKind::Empty || seat.victory != VictoryState::Playing)
    {
      continue;
    }
    if (held.structures[index] || held.builders[index])
    {
      seat.everHeldBase = true;
      continue;
    }
    if (seat.everHeldBase)
    {
      seat.victory = VictoryState::Eliminated;
    }
  }

  const Standing standing = StandingAlliances(_sim.Seats());

  // One alliance left, or none, ends every condition: there is nobody for the survivor to play
  // against, and a clock that has not run out cannot change the answer.
  if (standing.count <= 1)
  {
    _sim.Decide(standing.count == 1 ? standing.Only() : NO_ALLIANCE);
    return;
  }

  switch (_sim.Settings().victory)
  {
  case VictoryCondition::Annihilation:
  case VictoryCondition::Dominance:
    // Dominance is M2's: it needs 60% of a landscape's deposits held for ten continuous minutes
    // (GameDesign.md §2) and ships with the first Large landscapes, so until then a match set to
    // it is played by the annihilation rule above rather than refused in the lobby.
    return;

  case VictoryCondition::Survival:
    if (_sim.Tick() >= _sim.Settings().survivalTicks)
    {
      _sim.Decide(RichestAlliance(_sim.Seats(), standing));
    }
    return;
  }
}

} // namespace Frontier
