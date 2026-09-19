#include "pch.h"

#include "Sim.h"
#include "Construction.h"
#include "Damage.h"
#include "Movement.h"
#include "OrderValidation.h"
#include "Production.h"
#include "Research.h"
#include "StateHash.h"
#include "Targeting.h"
#include "Victory.h"
#include "Weapons.h"

#include <algorithm>
#include <array>
#include <utility>

namespace Frontier
{

Sim::Sim(const MatchSettings& _settings, const ContentTree& _content)
{
  // Assigned rather than initialised in a list, because the members are a private base's
  // (Sim/Sim.h says why) and a mem-initialiser list cannot name one of those.
  m_settings = _settings;
  m_content = &_content;
  m_random = Neuron::Random(_settings.seed);
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
    // An empty seat is Eliminated from the first tick, which is what "takes no part" is in a
    // field with four values: its orders are dropped, it is never counted as standing, and it
    // cannot win. It is not Lost, because losing is something that happens to a commander.
    seat.victory = lobby.kind == SeatKind::Empty ? VictoryState::Eliminated : VictoryState::Playing;
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

Sim::Sim(const Sim& _other)
  : SimState(_other)
{
  Rebind();
}

Sim::Sim(Sim&& _other) noexcept
  : SimState(std::move(_other))
{
  Rebind();
}

Sim& Sim::operator=(const Sim& _other)
{
  if (this != &_other)
  {
    SimState::operator=(_other);
    Rebind();
  }
  return *this;
}

Sim& Sim::operator=(Sim&& _other) noexcept
{
  if (this != &_other)
  {
    SimState::operator=(std::move(_other));
    Rebind();
  }
  return *this;
}

void Sim::Rebind() noexcept
{
  // The two pointers that aim into this object, and nothing else: the graph holds the landscape
  // and the planner holds the graph. Neither is rebuilt - the landscape and the graph are the same
  // data at a new address - so the routes in flight survive the copy, which is what makes a Sim
  // handed back by Snapshot::Read go on planning where the one it was read into left off.
  m_clusters.Rebind(&m_landscape);
  m_planner.Rebind(&m_clusters);
}

std::uint32_t Sim::Roll(std::uint32_t _bound) noexcept
{
  m_lastRoll = m_random.Below(_bound == 0 ? 1 : _bound);
  return m_lastRoll;
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
  // A stamped disc was worked out against the heights as they were, and un-counting it against the
  // new ones would take viewers off cells that were never counted and leave others lit for ever.
  // So the discs that REACH the rectangle are un-counted here, before the heights move, and the
  // budget counts them again over the next few ticks. m1-vertical-slice/S9 dropped the whole fog
  // instead and left the narrowing to S4, which is the task that actually flattens: a structure
  // going up must not black out every commander's explored map, and it built one every few
  // seconds. A disc that does not reach the rectangle cannot have a cell changed by it.
  const std::uint32_t cellX0 = _delta.x / SAMPLES_PER_CELL_EDGE;
  const std::uint32_t cellY0 = _delta.y / SAMPLES_PER_CELL_EDGE;
  const std::uint32_t cellX1 = _delta.width == 0 ? cellX0 : (_delta.x + _delta.width - 1) / SAMPLES_PER_CELL_EDGE;
  const std::uint32_t cellY1 = _delta.height == 0 ? cellY0 : (_delta.y + _delta.height - 1) / SAMPLES_PER_CELL_EDGE;
  m_visibility.InvalidateRegion(m_seats, m_landscape, cellX0, cellY0, cellX1, cellY1);
  if (!m_landscape.ApplyDelta(_delta))
  {
    // The stamps dropped above are not a fault: the budget counts those viewers again next tick,
    // which is what it does for every viewer that moves.
    return false;
  }
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
  // A decided match does not advance (m1-vertical-slice/S11). The outcome is what every system
  // above the simulation is waiting for, and a tick after it can only move objects nobody is
  // playing any more: the host stops calling this and the state stays as the last tick left it,
  // which is the state the snapshot of a finished match holds and the hash of it reports.
  if (m_finished)
  {
    return;
  }
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
  if (seat.kind == SeatKind::Empty || seat.Defeated())
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
    // Out at once rather than at stage 12: the rest of this tick must not carry his orders, and a
    // commander who has conceded should not go on shooting while the stages catch up.
    seat.victory = VictoryState::Eliminated;
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
    // A new destination withdraws the old route. Stage 6 asks for another on the next tick, from
    // where the device actually is rather than from where the withdrawn route began.
    m_planner.Cancel(ObjectId{static_cast<std::uint32_t>(order.operands[0]), ObjectKind::Device});
    target->pathIndex = NO_PATH_INDEX;
    target->stalledTicks = 0;
    return true;
  }

  case OrderKind::Patrol:
  {
    Device* target = device;
    if (target == nullptr)
    {
      return false;
    }
    // The far end is the order's, and the near end is where the device stood when it was given:
    // a patrol is between two points and an order names one (Sim/Order.h's table).
    target->primaryOrder = PrimaryOrder::Patrol;
    target->destinationX = order.operands[1];
    target->destinationZ = order.operands[2];
    target->anchorX = target->x;
    target->anchorZ = target->z;
    target->target = NO_OBJECT;
    m_planner.Cancel(ObjectId{static_cast<std::uint32_t>(order.operands[0]), ObjectKind::Device});
    target->pathIndex = NO_PATH_INDEX;
    target->stalledTicks = 0;
    return true;
  }

  case OrderKind::Guard:
  {
    Device* target = device;
    if (target == nullptr)
    {
      return false;
    }
    // Operand 3 names a device to guard, or nought to guard the position in operands 1 and 2. The
    // post is the anchor either way, so that stage 6 has somewhere to return to; following the
    // guarded device about is S10's, which is the task that knows what it is guarding against.
    target->primaryOrder = PrimaryOrder::Guard;
    const Device* guarded =
      order.operands[3] > 0 ? m_world.FindDevice({static_cast<std::uint32_t>(order.operands[3]), ObjectKind::Device}) : nullptr;
    target->anchorX = guarded != nullptr ? guarded->x : order.operands[1];
    target->anchorZ = guarded != nullptr ? guarded->z : order.operands[2];
    target->destinationX = target->anchorX;
    target->destinationZ = target->anchorZ;
    target->target = guarded != nullptr ? ObjectId{static_cast<std::uint32_t>(order.operands[3]), ObjectKind::Device} : NO_OBJECT;
    m_planner.Cancel(ObjectId{static_cast<std::uint32_t>(order.operands[0]), ObjectKind::Device});
    target->pathIndex = NO_PATH_INDEX;
    target->stalledTicks = 0;
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
    m_planner.Cancel(ObjectId{static_cast<std::uint32_t>(order.operands[0]), ObjectKind::Device});
    target->pathIndex = NO_PATH_INDEX;
    target->stalledTicks = 0;
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

  case OrderKind::PlaceStructure:
    return PlaceStructurePlan(*this, order.seat, static_cast<std::uint32_t>(order.operands[0]),
                              static_cast<std::uint32_t>(order.operands[1]), static_cast<std::uint32_t>(order.operands[2]));

  case OrderKind::CancelStructure:
    return CancelStructure(*this, order.seat, {static_cast<std::uint32_t>(order.operands[0]), ObjectKind::Structure});

  case OrderKind::Demolish:
    return DemolishStructure(*this, order.seat, {static_cast<std::uint32_t>(order.operands[0]), ObjectKind::Structure});

  case OrderKind::BuildModule:
    return BeginModule(*this, order.seat, {static_cast<std::uint32_t>(order.operands[0]), ObjectKind::Structure},
                       static_cast<std::uint32_t>(order.operands[1]));

  case OrderKind::SetProduction:
    return SetProduction(*this, order.seat, {static_cast<std::uint32_t>(order.operands[0]), ObjectKind::Structure},
                         static_cast<std::uint32_t>(order.operands[1]), static_cast<std::uint32_t>(order.operands[2]));

  case OrderKind::CancelProduction:
    return CancelProduction(*this, order.seat, {static_cast<std::uint32_t>(order.operands[0]), ObjectKind::Structure},
                            static_cast<std::uint32_t>(order.operands[1]));

  case OrderKind::SaveDesign:
    return SaveDesign(*this, order.seat, static_cast<std::uint32_t>(order.operands[0]),
                      DesignFromOrder(order.operands[1], order.operands[2], order.operands[3]));

  case OrderKind::SetResearch:
    return SetResearch(*this, order.seat, {static_cast<std::uint32_t>(order.operands[0]), ObjectKind::Structure},
                       static_cast<std::uint32_t>(order.operands[1]));

  case OrderKind::CancelResearch:
    return CancelResearch(*this, order.seat, {static_cast<std::uint32_t>(order.operands[0]), ObjectKind::Structure});

  case OrderKind::Attack:
  {
    Device* target = device;
    if (target == nullptr)
    {
      return false;
    }
    // Validation has already turned an Attack on something the commander cannot see into an
    // AttackMove to where it was last seen (GameDesign.md §8), so reaching here means the target
    // is visible and is somebody else's. The destination is where it stands now; stage 8 keeps it
    // current while the order lasts.
    const ObjectId aim{static_cast<std::uint32_t>(order.operands[1]), static_cast<ObjectKind>(order.operands[2])};
    TargetPoint point{};
    if (!TargetAt(*this, aim, point))
    {
      return false;
    }
    target->primaryOrder = PrimaryOrder::Attack;
    target->target = aim;
    target->destinationX = point.x;
    target->destinationZ = point.z;
    m_planner.Cancel(ObjectId{static_cast<std::uint32_t>(order.operands[0]), ObjectKind::Device});
    target->pathIndex = NO_PATH_INDEX;
    target->stalledTicks = 0;
    return true;
  }

  case OrderKind::ReturnToRepair:
  {
    Device* target = device;
    if (target == nullptr)
    {
      return false;
    }
    // A commander may send a device back by hand as well as its retreat stance sending it
    // (Sim/Retreat.cpp). With nothing in reach it stays where it is rather than walking nowhere.
    std::int32_t x = 0;
    std::int32_t z = 0;
    if (!RepairPointNear(*this, order.seat, target->x, target->z, x, z))
    {
      return true;
    }
    target->primaryOrder = PrimaryOrder::ReturnToRepair;
    target->destinationX = x;
    target->destinationZ = z;
    target->target = NO_OBJECT;
    m_planner.Cancel(ObjectId{static_cast<std::uint32_t>(order.operands[0]), ObjectKind::Device});
    target->pathIndex = NO_PATH_INDEX;
    target->stalledTicks = 0;
    return true;
  }
  }
  return false;
}

// ── Stages 2 to 7: the systems of M1 (m1-vertical-slice S3 onward) ─────────────────────────

void Sim::AdvanceEconomy()
{
  m_economy.Advance(m_world, m_seats, *m_content);
}

void Sim::AdvanceResearch()
{
  Frontier::AdvanceResearch(*this);
}

void Sim::AdvanceProduction()
{
  Frontier::AdvanceProduction(*this);
}

void Sim::AdvanceConstruction()
{
  Frontier::AdvanceConstruction(*this);
}

void Sim::AdvanceMovement()
{
  Frontier::AdvanceMovement(*this);
}

void Sim::RefreshVisibility()
{
  m_visibility.Advance(m_world, m_seats, m_landscape, *m_content, m_tick);
}

// ── Stage 8 ─────────────────────────────────────────────────────────────────────────────────

void Sim::ResolveTargeting()
{
  Frontier::AdvanceTargeting(*this);
}

// ── Stages 9 to 11 ──────────────────────────────────────────────────────────────────────────

void Sim::AdvanceProjectiles()
{
  Frontier::AdvanceProjectiles(*this);
}

void Sim::ResolveDamage()
{
  Frontier::ResolveDamage(*this);
}

void Sim::AdvanceAiSeats() {}

// ── Stage 12 ────────────────────────────────────────────────────────────────────────────────

void Sim::CheckVictory()
{
  Frontier::CheckVictory(*this);
}

void Sim::Decide(std::uint8_t _winningAlliance) noexcept
{
  m_finished = true;
  m_winningAlliance = _winningAlliance;
  // Everyone still playing when the match ended is told which way it went, here rather than at
  // each of stage 12's exits, so that there is one place the Won-or-Lost rule is written. A seat
  // that had already left keeps Eliminated: the match's outcome is not his.
  for (Seat& seat : m_seats)
  {
    if (seat.victory != VictoryState::Playing)
    {
      continue;
    }
    seat.victory = _winningAlliance != NO_ALLIANCE && seat.alliance == _winningAlliance ? VictoryState::Won : VictoryState::Lost;
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
