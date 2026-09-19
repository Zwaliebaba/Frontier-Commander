#include "pch.h"

#include "ReliableStream.h"

#include "Random.h"

#include <cstdint>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// The reliable order stream (TechnicalDesign.md §5.5; m1-vertical-slice/N3). What is asserted here
// is the contract a commander's clicks depend on: every order arrives exactly once and in the order
// it was given, however the link mangles the datagrams that carry it.
namespace NetTests
{

namespace
{

Frontier::Order Numbered(std::int32_t _which)
{
  Frontier::Order order{};
  order.tick = static_cast<std::uint32_t>(100 + _which);
  order.seat = 1;
  order.kind = Frontier::OrderKind::Move;
  order.operands = {_which, 0, 0, 0};
  return order;
}

/// A link that loses, duplicates and reorders what is put into it, from a seeded stream, and hands
/// it over a fixed number of ticks later. It carries OrderMessages rather than bytes: what is
/// under test here is the stream's contract, and Records.h already pins what a datagram looks like.
class Link
{
public:
  struct Faults
  {
    std::uint32_t dropPerMille = 0;
    std::uint32_t duplicatePerMille = 0;
    std::uint32_t reorderPerMille = 0;
    std::uint32_t latencyTicks = 3;
  };

  Link(std::uint64_t _seed, const Faults& _faults)
    : m_random(_seed),
      m_faults(_faults)
  {
  }

  void Put(const std::vector<Frontier::OrderMessage>& _messages, std::uint32_t _tick)
  {
    for (const Frontier::OrderMessage& message : _messages)
    {
      if (m_random.Below(1000) < m_faults.dropPerMille)
      {
        ++m_dropped;
        continue;
      }
      std::uint32_t arrival = _tick + m_faults.latencyTicks;
      if (m_random.Below(1000) < m_faults.reorderPerMille)
      {
        arrival += 1 + m_random.Below(3);
        ++m_reordered;
      }
      m_inFlight.push_back({message, arrival});
      if (m_random.Below(1000) < m_faults.duplicatePerMille)
      {
        m_inFlight.push_back({message, arrival + 1});
        ++m_duplicated;
      }
    }
  }

  /// Everything due at or before _tick, in the order it happens to arrive.
  std::vector<Frontier::OrderMessage> Take(std::uint32_t _tick)
  {
    std::vector<Frontier::OrderMessage> arrived;
    std::vector<Carried> still;
    for (const Carried& carried : m_inFlight)
    {
      if (carried.arrival <= _tick)
      {
        arrived.push_back(carried.message);
      }
      else
      {
        still.push_back(carried);
      }
    }
    m_inFlight = still;
    return arrived;
  }

  [[nodiscard]] std::uint32_t Dropped() const noexcept
  {
    return m_dropped;
  }

private:
  struct Carried
  {
    Frontier::OrderMessage message;
    std::uint32_t arrival;
  };

  Neuron::Random m_random;
  Faults m_faults;
  std::vector<Carried> m_inFlight;
  std::uint32_t m_dropped = 0;
  std::uint32_t m_duplicated = 0;
  std::uint32_t m_reordered = 0;
};

} // namespace

TEST_CLASS(ReliableStreamTests)
{
public:
  TEST_METHOD(AnOrderIsSentOnceAndAcknowledgedOnce)
  {
    Frontier::ReliableStream sender;
    Frontier::ReliableStream receiver;
    Assert::AreEqual(static_cast<std::uint32_t>(1), sender.Send(Numbered(1)), L"sequence 0 is never issued");

    std::vector<Frontier::OrderMessage> due;
    sender.Due(10, due);
    Assert::AreEqual(static_cast<std::size_t>(1), due.size());

    // Nothing more is due until the resend timer has run out.
    due.clear();
    sender.Due(11, due);
    Assert::IsTrue(due.empty(), L"an order in flight is not sent again on the next tick");

    std::vector<Frontier::Order> delivered;
    receiver.Receive(std::span<const Frontier::OrderMessage>(std::vector<Frontier::OrderMessage>{{1, Numbered(1)}}), delivered);
    Assert::AreEqual(static_cast<std::size_t>(1), delivered.size());
    Assert::AreEqual(static_cast<std::uint32_t>(1), receiver.Acknowledgement());

    sender.Acknowledged(receiver.Acknowledgement(), 14);
    Assert::AreEqual(static_cast<std::size_t>(0), sender.Unacknowledged());
    due.clear();
    sender.Due(100, due);
    Assert::IsTrue(due.empty(), L"an acknowledged order is never sent again");
  }

  TEST_METHOD(AnUnacknowledgedOrderIsSentAgainAfterARoundTrip)
  {
    Frontier::ReliableStream sender;
    Assert::AreEqual(static_cast<std::uint32_t>(1), sender.Send(Numbered(1)));
    std::vector<Frontier::OrderMessage> due;
    sender.Due(0, due);
    Assert::AreEqual(static_cast<std::size_t>(1), due.size());

    const std::uint32_t resend = sender.ResendTicks();
    due.clear();
    sender.Due(resend - 1, due);
    Assert::IsTrue(due.empty());
    sender.Due(resend, due);
    Assert::AreEqual(static_cast<std::size_t>(1), due.size(), L"the round-trip estimate has run out");
    Assert::AreEqual(static_cast<std::uint32_t>(1), sender.Statistics().resent);
  }

  TEST_METHOD(ADuplicateIsDroppedAndAnEarlyOrderWaitsForItsPredecessor)
  {
    Frontier::ReliableStream receiver;
    std::vector<Frontier::Order> delivered;

    const std::vector<Frontier::OrderMessage> outOfOrder = {{2, Numbered(2)}, {3, Numbered(3)}};
    receiver.Receive(outOfOrder, delivered);
    Assert::IsTrue(delivered.empty(), L"two and three cannot be delivered before one");
    Assert::AreEqual(Frontier::NO_SEQUENCE, receiver.Acknowledgement());

    const std::vector<Frontier::OrderMessage> late = {{1, Numbered(1)}};
    receiver.Receive(late, delivered);
    Assert::AreEqual(static_cast<std::size_t>(3), delivered.size(), L"one lets two and three through behind it");
    Assert::AreEqual(1, delivered[0].operands[0]);
    Assert::AreEqual(2, delivered[1].operands[0]);
    Assert::AreEqual(3, delivered[2].operands[0]);
    Assert::AreEqual(static_cast<std::uint32_t>(3), receiver.Acknowledgement());

    delivered.clear();
    receiver.Receive(outOfOrder, delivered);
    receiver.Receive(late, delivered);
    Assert::IsTrue(delivered.empty(), L"every one of them a duplicate now");
    Assert::AreEqual(static_cast<std::uint32_t>(3), receiver.Statistics().duplicates);
  }

  TEST_METHOD(EveryOrderArrivesOnceAndInOrderThroughLossDuplicationAndReorder)
  {
    // The contract, over a link that loses a twelfth of what it carries, duplicates a twentieth and
    // delays a tenth past the datagram behind it. Sixty orders, and every one must arrive exactly
    // once and in the order it was given.
    Frontier::ReliableStream sender;
    Frontier::ReliableStream receiver;
    Link there(20260919, {80, 50, 100, 3});
    Link back(20260920, {80, 0, 0, 3});

    std::vector<Frontier::Order> delivered;
    constexpr std::int32_t ORDERS = 60;
    std::int32_t given = 0;
    for (std::uint32_t tick = 0; tick < 600; ++tick)
    {
      if (given < ORDERS && tick % 5 == 0)
      {
        ++given;
        Assert::AreNotEqual(Frontier::NO_SEQUENCE, sender.Send(Numbered(given)));
      }
      std::vector<Frontier::OrderMessage> due;
      sender.Due(tick, due);
      there.Put(due, tick);

      const std::vector<Frontier::OrderMessage> arrived = there.Take(tick);
      receiver.Receive(arrived, delivered);

      // The acknowledgement rides back over the same lossy link, as it does in a real datagram.
      if (!arrived.empty())
      {
        back.Put({{receiver.Acknowledgement(), Frontier::Order{}}}, tick);
      }
      for (const Frontier::OrderMessage& acknowledgement : back.Take(tick))
      {
        sender.Acknowledged(acknowledgement.sequence, tick);
      }
    }

    Assert::AreEqual(static_cast<std::size_t>(ORDERS), delivered.size(), L"every order, exactly once");
    for (std::int32_t index = 0; index < ORDERS; ++index)
    {
      Assert::AreEqual(index + 1, delivered[static_cast<std::size_t>(index)].operands[0], L"and in the order they were given");
    }
    Assert::AreEqual(static_cast<std::size_t>(0), sender.Unacknowledged(), L"and all of them acknowledged");
    Assert::IsTrue(there.Dropped() > 0, L"the link really did lose some");
    Assert::IsTrue(sender.Statistics().resent > 0, L"and they really were sent again");
    Logger::WriteMessage(("    measured: " + std::to_string(ORDERS) + " orders over a link that lost " + std::to_string(there.Dropped()) +
                          " of " + std::to_string(sender.Statistics().sent + sender.Statistics().resent) + " sends, resent " +
                          std::to_string(sender.Statistics().resent) + ", duplicates dropped " +
                          std::to_string(receiver.Statistics().duplicates) + "\n")
                           .c_str());
  }

  TEST_METHOD(TheRoundTripEstimateConvergesOnTheLink)
  {
    // A clean link of a known latency: the estimate must walk from its initial guess to the real
    // round trip and stay there. Held in eighths of a tick, so it can move at all.
    Frontier::ReliableStream sender;
    Frontier::ReliableStream receiver;
    constexpr std::uint32_t LATENCY = 2;
    Link there(1, {0, 0, 0, LATENCY});
    Link back(2, {0, 0, 0, LATENCY});

    std::vector<Frontier::Order> delivered;
    const std::uint32_t first = sender.RoundTripTicks();
    for (std::uint32_t tick = 0; tick < 400; ++tick)
    {
      if (tick % 8 == 0)
      {
        Assert::AreNotEqual(Frontier::NO_SEQUENCE, sender.Send(Numbered(static_cast<std::int32_t>(tick))));
      }
      std::vector<Frontier::OrderMessage> due;
      sender.Due(tick, due);
      there.Put(due, tick);
      const std::vector<Frontier::OrderMessage> arrived = there.Take(tick);
      receiver.Receive(arrived, delivered);
      if (!arrived.empty())
      {
        back.Put({{receiver.Acknowledgement(), Frontier::Order{}}}, tick);
      }
      for (const Frontier::OrderMessage& acknowledgement : back.Take(tick))
      {
        sender.Acknowledged(acknowledgement.sequence, tick);
      }
    }
    const std::uint32_t settled = sender.RoundTripTicks();
    Assert::AreEqual(Frontier::INITIAL_ROUND_TRIP_TICKS, first);
    Assert::IsTrue(settled <= 2 * LATENCY + 1, L"the estimate came down to the link");
    Assert::IsTrue(settled >= 2 * LATENCY - 1, L"and not below it");
    Logger::WriteMessage(("    measured: the round-trip estimate settled at " + std::to_string(settled) + " ticks on a link of " +
                          std::to_string(2 * LATENCY) + "\n")
                           .c_str());
  }

  TEST_METHOD(AnOrderIsRefusedWhenNothingIsBeingAcknowledged)
  {
    // A host that has stopped acknowledging must not make a client hold an unbounded queue: the
    // window fills and Send says so, which is the one place a commander can be told.
    Frontier::ReliableStream sender;
    for (std::size_t index = 0; index < Frontier::MAX_UNACKNOWLEDGED_ORDERS; ++index)
    {
      Assert::AreNotEqual(Frontier::NO_SEQUENCE, sender.Send(Numbered(static_cast<std::int32_t>(index))));
    }
    Assert::AreEqual(Frontier::NO_SEQUENCE, sender.Send(Numbered(9999)));
    Assert::AreEqual(static_cast<std::uint32_t>(1), sender.Statistics().refused);
  }
};

} // namespace NetTests
