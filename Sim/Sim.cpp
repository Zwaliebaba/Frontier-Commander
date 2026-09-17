#include "pch.h"

#include "Sim.h"
#include "StateHash.h"

#include <algorithm>
#include <array>

namespace Frontier
{

Sim::Sim(const MatchSettings& _settings)
  : m_settings(_settings),
    m_random(_settings.seed)
{
  FRONTIER_ASSERT(_settings.seatCount >= MIN_SEATS && _settings.seatCount <= MAX_SEATS);
  const std::uint8_t seatCount = std::clamp(_settings.seatCount, MIN_SEATS, MAX_SEATS);
  m_settings.seatCount = seatCount;
  const std::int32_t power =
    STARTING_POWER_HUNDREDTHS[std::min<std::size_t>(static_cast<std::size_t>(_settings.powerLevel), STARTING_POWER_HUNDREDTHS.size() - 1)];
  m_seats.reserve(seatCount);
  for (std::uint8_t index = 0; index < seatCount; ++index)
  {
    const SeatSettings& lobby = _settings.seats[index];
    m_seats.push_back({lobby.kind, lobby.alliance, power, lobby.kind == SeatKind::Empty});
  }
}

void Sim::Submit(Order _order)
{
  if (_order.tick <= m_tick)
  {
    _order.tick = m_tick + 1;
  }
  m_orders.Push(_order);
}

void Sim::Advance()
{
  ++m_tick;
  ApplyOrders();
  AdvanceEconomy();
  AdvanceResearch();
  AdvanceProduction();
  AdvanceConstruction();
  AdvanceMovement();
  RefreshVisibility();
  ResolveTargeting();
  AdvanceProjectiles();
  ResolveDamage();
  AdvanceAiSeats();
  CheckVictory();
  HashState();
  MarkPublish();
}

std::uint64_t Sim::ComputeHash() const noexcept
{
  StateHash hash;
  hash.Add(m_tick);
  hash.AddSpan(std::span<const std::uint32_t>(m_random.GetState()));
  for (const Seat& seat : m_seats)
  {
    hash.AddSeat(seat);
  }
  // The object maps go here, each in ascending id order, as the systems arrive (ADR-002).
  hash.Add(m_lastRoll);
  hash.Add(m_appliedOrders);
  hash.Add(m_droppedOrders);
  hash.AddBool(m_finished);
  hash.Add(m_winningAlliance);
  return hash.Value();
}

// ── Stage 1 ─────────────────────────────────────────────────────────────────────────────────

void Sim::ApplyOrders()
{
  m_thisTick.clear();
  m_orders.Drain(m_tick, m_thisTick);
  for (const Order& order : m_thisTick)
  {
    if (Apply(order))
    {
      ++m_appliedOrders;
    }
    else
    {
      ++m_droppedOrders;
    }
  }
  m_thisTick.clear();
}

bool Sim::Apply(const Order& _order)
{
  if (_order.seat >= m_seats.size())
  {
    return false;
  }
  Seat& seat = m_seats[_order.seat];
  if (seat.kind == SeatKind::Empty || seat.defeated)
  {
    return false;
  }
  switch (_order.kind)
  {
  case OrderKind::Surrender:
    seat.defeated = true;
    return true;
  case OrderKind::Chat:
    return true; // Carried to the other commanders by Net; nothing in the state changes.
  case OrderKind::Move:
  case OrderKind::AttackMove:
  case OrderKind::Attack:
  case OrderKind::Patrol:
  case OrderKind::Guard:
  case OrderKind::Stop:
  case OrderKind::ReturnToRepair:
  case OrderKind::SetStance:
  case OrderKind::PlaceStructure:
  case OrderKind::CancelStructure:
  case OrderKind::Demolish:
  case OrderKind::BuildModule:
  case OrderKind::SetProduction:
  case OrderKind::CancelProduction:
  case OrderKind::SetResearch:
  case OrderKind::CancelResearch:
  case OrderKind::SaveDesign:
  case OrderKind::Group:
    // Every one of these names something the seat must own, and no system owns anything yet:
    // validation fails, as it would for an id the seat does not own (TechnicalDesign.md §4.7).
    return false;
  }
  return false;
}

// ── Stages 2 to 7: the systems of M1 (m1-vertical-slice S3 onward) ─────────────────────────

void Sim::AdvanceEconomy() {}

void Sim::AdvanceResearch() {}

void Sim::AdvanceProduction() {}

void Sim::AdvanceConstruction() {}

void Sim::AdvanceMovement() {}

void Sim::RefreshVisibility() {}

// ── Stage 8 ─────────────────────────────────────────────────────────────────────────────────

void Sim::ResolveTargeting()
{
  // One draw per tick until the roll has a target, so that the stream is exercised from M0 and
  // the determinism tests cover it (m0-foundation/T15 notes).
  m_lastRoll = m_random.Next();
}

// ── Stages 9 to 11 ──────────────────────────────────────────────────────────────────────────

void Sim::AdvanceProjectiles() {}

void Sim::ResolveDamage() {}

void Sim::AdvanceAiSeats() {}

// ── Stage 12 ────────────────────────────────────────────────────────────────────────────────

void Sim::CheckVictory()
{
  if (m_finished)
  {
    return;
  }
  // The alliances still standing: a seat that is present and not defeated keeps its alliance in.
  std::array<bool, 256> standing{};
  std::uint32_t standingCount = 0;
  for (const Seat& seat : m_seats)
  {
    if (seat.kind != SeatKind::Empty && !seat.defeated && !standing[seat.alliance])
    {
      standing[seat.alliance] = true;
      ++standingCount;
    }
  }
  const auto lastStanding = [&standing]() -> std::uint8_t
  {
    for (std::size_t alliance = 0; alliance < standing.size(); ++alliance)
    {
      if (standing[alliance])
      {
        return static_cast<std::uint8_t>(alliance);
      }
    }
    return NO_ALLIANCE;
  };
  switch (m_settings.victory)
  {
  case VictoryCondition::Annihilation:
  case VictoryCondition::Dominance: // Deposits arrive with the landscape (M1); until then the annihilation rule.
    if (standingCount <= 1)
    {
      m_finished = true;
      m_winningAlliance = standingCount == 1 ? lastStanding() : NO_ALLIANCE;
    }
    return;
  case VictoryCondition::Survival:
    if (standingCount <= 1)
    {
      m_finished = true;
      m_winningAlliance = standingCount == 1 ? lastStanding() : NO_ALLIANCE;
      return;
    }
    if (m_tick >= m_settings.survivalTicks)
    {
      // The clock ran out: the alliance with the most power wins, and a tie is a draw.
      std::array<std::int64_t, 256> power{};
      for (const Seat& seat : m_seats)
      {
        if (seat.kind != SeatKind::Empty && !seat.defeated)
        {
          power[seat.alliance] += seat.powerHundredths;
        }
      }
      std::int64_t best = -1;
      std::uint8_t winner = NO_ALLIANCE;
      bool tied = false;
      for (std::size_t alliance = 0; alliance < power.size(); ++alliance)
      {
        if (!standing[alliance])
        {
          continue;
        }
        if (power[alliance] > best)
        {
          best = power[alliance];
          winner = static_cast<std::uint8_t>(alliance);
          tied = false;
        }
        else if (power[alliance] == best)
        {
          tied = true;
        }
      }
      m_finished = true;
      m_winningAlliance = tied ? NO_ALLIANCE : winner;
    }
    return;
  }
}

// ── Stages 13 and 14 ────────────────────────────────────────────────────────────────────────

void Sim::HashState()
{
  m_hash = ComputeHash();
}

void Sim::MarkPublish()
{
  m_publishDue = (m_tick % 2) == 0;
}

} // namespace Frontier
