#include "pch.h"

#include "OrderQueue.h"

#include <cstdint>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace SimTests
{

namespace
{

Frontier::Order Make(std::uint32_t _tick, std::uint8_t _seat, Frontier::OrderKind _kind, std::int32_t _mark)
{
  Frontier::Order order{};
  order.tick = _tick;
  order.seat = _seat;
  order.kind = _kind;
  order.operands = {_mark, 0, 0, 0};
  return order;
}

} // namespace

TEST_CLASS(OrderQueueTests)
{
public:
  TEST_METHOD(OrdersComeOutForTheirTickInSeatOrderThenArrivalOrder)
  {
    Frontier::OrderQueue queue;
    queue.Push(Make(2, 1, Frontier::OrderKind::Move, 1));
    queue.Push(Make(1, 2, Frontier::OrderKind::Stop, 2));
    queue.Push(Make(2, 0, Frontier::OrderKind::Chat, 3));
    queue.Push(Make(2, 1, Frontier::OrderKind::Attack, 4));
    queue.Push(Make(1, 0, Frontier::OrderKind::Guard, 5));
    Assert::AreEqual(static_cast<std::size_t>(5), queue.Size());

    std::vector<Frontier::Order> out;
    queue.Drain(1, out);
    Assert::AreEqual(static_cast<std::size_t>(2), out.size());
    Assert::AreEqual(5, out[0].operands[0]); // seat 0 before seat 2
    Assert::AreEqual(2, out[1].operands[0]);
    Assert::AreEqual(static_cast<std::size_t>(3), queue.Size());

    out.clear();
    queue.Drain(2, out);
    Assert::AreEqual(static_cast<std::size_t>(3), out.size());
    Assert::AreEqual(3, out[0].operands[0]); // seat 0
    Assert::AreEqual(1, out[1].operands[0]); // seat 1, arrived first
    Assert::AreEqual(4, out[2].operands[0]); // seat 1, arrived second
    Assert::IsTrue(queue.Empty());
  }

  TEST_METHOD(ALateOrderGoesOutWithTheNextDrainAndAFutureOneWaits)
  {
    Frontier::OrderQueue queue;
    queue.Push(Make(9, 0, Frontier::OrderKind::Move, 1));
    std::vector<Frontier::Order> out;
    queue.Drain(5, out);
    Assert::IsTrue(out.empty(), L"an order for tick 9 must wait");
    queue.Push(Make(2, 0, Frontier::OrderKind::Stop, 2)); // for a tick already drained
    queue.Drain(6, out);
    Assert::AreEqual(static_cast<std::size_t>(1), out.size());
    Assert::AreEqual(2, out[0].operands[0]);
    out.clear();
    queue.Drain(9, out);
    Assert::AreEqual(static_cast<std::size_t>(1), out.size());
    Assert::AreEqual(1, out[0].operands[0]);
  }

  TEST_METHOD(DrainAppendsToWhatTheCallerAlreadyHolds)
  {
    Frontier::OrderQueue queue;
    queue.Push(Make(1, 0, Frontier::OrderKind::Chat, 7));
    std::vector<Frontier::Order> out = {Make(0, 0, Frontier::OrderKind::Chat, 6)};
    queue.Drain(1, out);
    Assert::AreEqual(static_cast<std::size_t>(2), out.size());
    Assert::AreEqual(6, out[0].operands[0]);
    Assert::AreEqual(7, out[1].operands[0]);
  }

  TEST_METHOD(RestoreCarriesTheArrivalOrderAndTheCounter)
  {
    Frontier::OrderQueue original;
    original.Push(Make(3, 1, Frontier::OrderKind::Move, 1));
    original.Push(Make(3, 1, Frontier::OrderKind::Stop, 2));
    original.Push(Make(3, 0, Frontier::OrderKind::Guard, 3));
    std::vector<Frontier::Order> drained;
    original.Drain(2, drained);
    Assert::IsTrue(drained.empty());

    Frontier::OrderQueue restored;
    restored.Restore(original.NextArrival(), original.Entries());
    Assert::AreEqual(original.NextArrival(), restored.NextArrival());
    Assert::IsTrue(original.Entries() == restored.Entries());

    std::vector<Frontier::Order> a;
    std::vector<Frontier::Order> b;
    original.Drain(3, a);
    restored.Drain(3, b);
    Assert::IsTrue(a == b);
    Assert::AreEqual(3, a[0].operands[0]);
    Assert::AreEqual(1, a[1].operands[0]);
    Assert::AreEqual(2, a[2].operands[0]);
    restored.Push(Make(4, 0, Frontier::OrderKind::Chat, 4));
    Assert::AreEqual(static_cast<std::uint32_t>(4), restored.NextArrival());
  }
};

} // namespace SimTests
