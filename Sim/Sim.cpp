#include "pch.h"

#include "Sim.h"
#include "OrderValidation.h"
#include "StateHash.h"

#include <algorithm>
#include <array>
#include <utility>

namespace Frontier
{

Sim::Sim(const MatchSettings& _settings, const ContentTree& _content)
  : m_settings(_settings),
    m_content(&_content),
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
    // Field by field rather than an aggregate initialiser: a Seat gains fields as the systems
    // arrive, and a positional list would put the next one in the wrong place in silence.
    Seat seat{};
    seat.kind = lobby.kind;
    seat.alliance = lobby.alliance;
    seat.powerHundredths = power;
    // The army caps are the lobby's and fixed for the match (GameDesign.md §4), so they are set
    // once here; the stockpile cap follows the standing generators and so is stage 2's, recomputed
    // every tick. It is set here too, rather than left at zero, because a seat is judged against
    // it by order validation from the first tick and before stage 2 has ever run.
    seat.deviceCap = DEVICE_CAPS[std::min<std::size_t>(static_cast<std::size_t>(_settings.deviceCapLevel), DEVICE_CAPS.size() - 1)];
    seat.structureCap = STRUCTURE_CAP;
    seat.stockpileCapHundredths = Economy::StockpileCapHundredths(0);
    seat.defeated = lobby.kind == SeatKind::Empty;
    m_seats.push_back(std::move(seat));
  }
}

bool Sim::CreateLandscape(const LandscapeDefinition& _definition)
{
  if (!m_landscape.Create(_definition))
  {
    return false;
  }
  // A seat exists before the landscape does, so its fog grid is sized here rather than in the
  // constructor. S9 fills them; S1 gives every cell a viewer count and a state to hold.
  // A stamp names a cell of the landscape it was counted on, so a new landscape drops every stamp
  // and clears every grid rather than leaving discs counted against ground that no longer exists.
  m_visibility.Reset(m_seats, m_landscape);
  // The deposits are the definition's, so the index is rebuilt wherever the definition arrives:
  // here and in the snapshot's read. Nothing else sets a landscape.
  m_economy.SetLandscape(m_landscape);
  // The cluster graph is the landscape's shape, so it is cut here and its edges are built lazily,
  // per drive class, as the planner asks for them.
  m_clusters.Build(m_landscape, *m_content);
  m_planner.SetGraph(&m_clusters);
  return true;
}

void Sim::SetObstruction(std::uint32_t _cellX, std::uint32_t _cellY, std::uint8_t _obstruction)
{
  if (!m_landscape.Created() || _cellX >= m_landscape.CellsPerSide() || _cellY >= m_landscape.CellsPerSide())
  {
    return;
  }
  if (m_landscape.CellAt(_cellX, _cellY).obstruction == _obstruction)
  {
    return;
  }
  m_landscape.SetObstruction(_cellX, _cellY, _obstruction);
  // A component is a statement about which cells are passable, and that is exactly what changed,
  // so every class's graph is dropped and rebuilt on its next use. Coarser than invalidating the
  // one cluster: a wall across a cluster splits a component, and a split can change which
  // components a neighbouring cluster's are joined to, so the blast radius is not local in the way
  // an entrance's was. It costs one flood fill of 256 cells per cluster per class in use, on an
  // event that happens when a structure is placed rather than every tick.
  m_clusters.Invalidate();
}

bool Sim::FlattenTerrain(const HeightDelta& _delta)
{
  if (!m_landscape.ApplyDelta(_delta))
  {
    return false;
  }
  // A stamped disc was worked out against the heights as they were, and un-counting it against the
  // new ones would take viewers off cells that were never counted and leave others lit for ever.
  // So the whole fog is dropped and rebuilt: the budget refills it over the next few ticks, and a
  // flatten is a rare event that the construction system of S4 will do once per structure.
  m_visibility.Reset(m_seats, m_landscape);
  // The slope of every cell the rectangle touched has changed, and slope is passability, so the
  // graph is cut again. Rebuilding rather than invalidating: a delta can open or close an entrance,
  // and an entrance is a node, which invalidation does not move.
  m_clusters.Build(m_landscape, *m_content);
  m_planner.SetGraph(&m_clusters);
  return true;
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
  hash.AddBool(m_landscape.Created());
  if (m_landscape.Created())
  {
    m_landscape.AddToHash(hash);
  }
  m_world.AddToHash(hash);
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
  // Every seat's rejections are this tick's, so the list starts empty and what is in it at stage
  // 13 is what this tick refused.
  for (Seat& seat : m_seats)
  {
    seat.rejections.clear();
  }
  m_thisTick.clear();
  // The queue hands orders out in seat order then arrival order, which is the order they are
  // judged and applied in (TechnicalDesign.md §4.8, stage 1).
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
  // A seat outside the match, an empty seat or a defeated one is not a seat whose orders are
  // judged: the fault is in who sent it, so there is no rejection to report to anyone.
  if (_order.seat >= m_seats.size())
  {
    return false;
  }
  Seat& seat = m_seats[_order.seat];
  if (seat.kind == SeatKind::Empty || seat.defeated)
  {
    return false;
  }

  const OrderContext context{&m_world, m_seats, &m_landscape, m_tick, m_content, &m_economy.Deposits()};
  const OrderCheck checked = ValidateOrder(_order, context);
  if (!checked.Accepted())
  {
    seat.rejections.push_back({_order.kind, checked.reason});
    return false;
  }
  // The validated order, not the submitted one: an Attack the seat cannot see has become an
  // AttackMove to where it last saw the target (GameDesign.md §8).
  const Order& order = checked.order;

  // Validation resolved this a moment ago and nothing has run since, so it cannot be null; the
  // check is here because a later stage calling Apply would not have that guarantee.
  Device* device =
    order.operands[0] > 0 ? m_world.FindDevice({static_cast<std::uint32_t>(order.operands[0]), ObjectKind::Device}) : nullptr;

  switch (order.kind)
  {
  case OrderKind::Surrender:
    seat.defeated = true;
    seat.surrendered = true;
    return true;

  case OrderKind::Chat:
    return true; // Carried to the other commanders by Net; nothing in the state changes.

  case OrderKind::Move:
  case OrderKind::AttackMove:
  {
    Device* target = device;
    if (target == nullptr)
    {
      return false;
    }
    target->primaryOrder = order.kind == OrderKind::Move ? PrimaryOrder::Move : PrimaryOrder::AttackMove;
    target->destinationX = order.operands[1];
    target->destinationZ = order.operands[2];
    target->target = NO_OBJECT;
    return true;
  }

  case OrderKind::Stop:
  {
    Device* target = device;
    if (target == nullptr)
    {
      return false;
    }
    target->primaryOrder = PrimaryOrder::Stop;
    target->destinationX = target->x;
    target->destinationZ = target->z;
    target->target = NO_OBJECT;
    return true;
  }

  case OrderKind::SetStance:
  {
    Device* target = device;
    if (target == nullptr)
    {
      return false;
    }
    const auto value = static_cast<std::uint8_t>(order.operands[2]);
    switch (static_cast<StanceAxis>(order.operands[1]))
    {
    case StanceAxis::Fire:
      target->fire = static_cast<FireStance>(value);
      return true;
    case StanceAxis::Range:
      target->range = static_cast<RangeStance>(value);
      return true;
    case StanceAxis::Retreat:
      target->retreat = static_cast<RetreatStance>(value);
      return true;
    case StanceAxis::Movement:
      target->movement = static_cast<MovementStance>(value);
      return true;
    }
    return false;
  }

  case OrderKind::Group:
    if (device == nullptr)
    {
      return false;
    }
    device->group = static_cast<std::uint8_t>(order.operands[1]);
    return true;

  case OrderKind::Attack:
  case OrderKind::Patrol:
  case OrderKind::Guard:
  case OrderKind::ReturnToRepair:
  case OrderKind::PlaceStructure:
  case OrderKind::CancelStructure:
  case OrderKind::Demolish:
  case OrderKind::BuildModule:
  case OrderKind::SetProduction:
  case OrderKind::CancelProduction:
  case OrderKind::SetResearch:
  case OrderKind::CancelResearch:
  case OrderKind::SaveDesign:
    // Validated here and applied by the task that owns the system (m1-vertical-slice/S2): the
    // order passed every check this task can make, and there is nothing yet to apply it to. It
    // counts as applied rather than dropped, because nothing was wrong with it.
    return true;
  }
  return false;
}

// ── Stages 2 to 7: the systems of M1 (m1-vertical-slice S3 onward) ─────────────────────────

void Sim::AdvanceEconomy()
{
  m_economy.Advance(m_world, m_seats, *m_content);
}

void Sim::AdvanceResearch() {}

void Sim::AdvanceProduction() {}

void Sim::AdvanceConstruction() {}

void Sim::AdvanceMovement()
{
  // Planning is amortised inside the movement stage, ahead of the steering S8 will add: a device
  // that asked for a route this tick may have it next, and the budget is what bounds the wait.
  m_planner.Advance();
}

void Sim::RefreshVisibility()
{
  m_visibility.Advance(m_world, m_seats, m_landscape, *m_content, m_tick);
}

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
