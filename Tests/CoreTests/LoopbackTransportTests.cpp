#include "pch.h"

#include "LoopbackTransport.h"

#include <cstddef>
#include <cstdint>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace CoreTests
{

namespace
{

std::vector<std::byte> Datagram(std::uint8_t _mark)
{
  return {std::byte{_mark}, std::byte{0xAB}, std::byte{_mark}};
}

/// Every datagram an end has received, by its first byte, after one Poll.
std::vector<int> Drain(Neuron::Transport& _end, std::vector<Neuron::ConnectionId>* _from = nullptr)
{
  std::vector<int> marks;
  Neuron::ConnectionId from = Neuron::NO_CONNECTION;
  std::span<const std::byte> bytes;
  while (_end.Receive(from, bytes))
  {
    Assert::AreEqual(static_cast<std::size_t>(3), bytes.size());
    marks.push_back(static_cast<int>(std::to_integer<std::uint8_t>(bytes[0])));
    if (_from != nullptr)
    {
      _from->push_back(from);
    }
  }
  return marks;
}

Neuron::ConnectionId AcceptOne(Neuron::Transport& _host)
{
  _host.Poll();
  Neuron::ConnectionId id = Neuron::NO_CONNECTION;
  Assert::IsTrue(_host.Accept(id), L"the host should have a connection to accept");
  return id;
}

} // namespace

TEST_CLASS(LoopbackTransportTests)
{
public:
  TEST_METHOD(TheHostAssignsConnectionIdsAndBothWaysDeliverOnPoll)
  {
    Neuron::LoopbackTransport network(1);
    Neuron::Transport& host = network.Host();
    Neuron::Transport& alice = network.Connect();
    Neuron::Transport& bob = network.Connect();
    host.Poll();
    Neuron::ConnectionId first = 0;
    Neuron::ConnectionId second = 0;
    Neuron::ConnectionId none = 0;
    Assert::IsTrue(host.Accept(first));
    Assert::IsTrue(host.Accept(second));
    Assert::IsFalse(host.Accept(none));
    Assert::AreEqual(2u, first);
    Assert::AreEqual(3u, second);
    Assert::IsTrue(host.IsOpen(first) && host.IsOpen(second));
    Assert::IsTrue(alice.IsOpen(Neuron::HOST_CONNECTION));
    Assert::IsFalse(alice.Accept(none), L"a client end never accepts");

    Assert::IsTrue(alice.Send(Neuron::HOST_CONNECTION, Datagram(1)));
    Assert::IsTrue(bob.Send(Neuron::HOST_CONNECTION, Datagram(2)));
    Assert::IsTrue(host.Send(first, Datagram(10)));
    Assert::IsTrue(host.Send(second, Datagram(20)));
    Assert::IsTrue(Drain(host).empty(), L"nothing arrives before a Poll");
    alice.Poll();
    bob.Poll();
    Assert::IsTrue(Drain(alice).empty(), L"the host has not polled yet");
    host.Poll();
    std::vector<Neuron::ConnectionId> from;
    Assert::IsTrue(Drain(host, &from) == std::vector<int>{1, 2});
    Assert::IsTrue(from == std::vector<Neuron::ConnectionId>{first, second});
    alice.Poll();
    bob.Poll();
    Assert::IsTrue(Drain(alice) == std::vector<int>{10});
    Assert::IsTrue(Drain(bob) == std::vector<int>{20});
  }

  TEST_METHOD(SendsToTheWrongConnectionOrOverTheLimitAreRefusedAndCounted)
  {
    Neuron::LoopbackTransport network(1);
    Neuron::Transport& host = network.Host();
    Neuron::Transport& client = network.Connect();
    const Neuron::ConnectionId id = AcceptOne(host);
    Assert::IsFalse(host.Send(id + 5, Datagram(1)), L"no such connection");
    Assert::IsFalse(client.Send(id, Datagram(1)), L"a client only talks to the host");
    const std::vector<std::byte> huge(Neuron::MAX_DATAGRAM_BYTES + 1, std::byte{1});
    Assert::IsFalse(client.Send(Neuron::HOST_CONNECTION, huge));
    const std::vector<std::byte> largest(Neuron::MAX_DATAGRAM_BYTES, std::byte{1});
    Assert::IsTrue(client.Send(Neuron::HOST_CONNECTION, largest));
    Assert::AreEqual(1u, host.Refused());
    Assert::AreEqual(2u, client.Refused());
  }

  TEST_METHOD(ClosingIsSeenFromBothSides)
  {
    Neuron::LoopbackTransport network(1);
    Neuron::Transport& host = network.Host();
    Neuron::Transport& client = network.Connect();
    const Neuron::ConnectionId id = AcceptOne(host);
    host.Close(id);
    Assert::IsFalse(host.IsOpen(id));
    Assert::IsFalse(client.IsOpen(Neuron::HOST_CONNECTION));
    Assert::IsFalse(client.Send(Neuron::HOST_CONNECTION, Datagram(1)));
    Assert::IsFalse(host.Send(id, Datagram(1)));

    Neuron::Transport& other = network.Connect();
    const Neuron::ConnectionId otherId = AcceptOne(host);
    other.Close(Neuron::HOST_CONNECTION);
    Assert::IsFalse(host.IsOpen(otherId));
    Assert::IsFalse(other.IsOpen(Neuron::HOST_CONNECTION));
  }

  TEST_METHOD(DropsAreDeterministicForASeed)
  {
    std::vector<int> firstRun;
    std::vector<int> secondRun;
    for (std::vector<int>* run : {&firstRun, &secondRun})
    {
      Neuron::LoopbackTransport network(77);
      Neuron::LoopbackFaults faults;
      faults.dropPerMille = 300;
      network.SetFaults(faults);
      Neuron::Transport& host = network.Host();
      Neuron::Transport& client = network.Connect();
      AcceptOne(host);
      for (std::uint8_t mark = 1; mark <= 100; ++mark)
      {
        Assert::IsTrue(client.Send(Neuron::HOST_CONNECTION, Datagram(mark)));
      }
      client.Poll();
      host.Poll();
      *run = Drain(host);
      Assert::IsTrue(run->size() > 40 && run->size() < 95, L"about thirty percent should be lost");
      Assert::AreEqual(100u, network.Statistics().sent);
      Assert::AreEqual(static_cast<std::uint32_t>(100 - run->size()), network.Statistics().dropped);
    }
    Assert::IsTrue(firstRun == secondRun, L"the same seed loses the same datagrams");

    Neuron::LoopbackTransport everything(5);
    Neuron::LoopbackFaults all;
    all.dropPerMille = 1000;
    everything.SetFaults(all);
    Neuron::Transport& client = everything.Connect();
    AcceptOne(everything.Host());
    Assert::IsTrue(client.Send(Neuron::HOST_CONNECTION, Datagram(1)));
    client.Poll();
    everything.Host().Poll();
    Assert::IsTrue(Drain(everything.Host()).empty());
  }

  TEST_METHOD(DuplicatesArriveTwice)
  {
    Neuron::LoopbackTransport network(3);
    Neuron::LoopbackFaults faults;
    faults.duplicatePerMille = 1000;
    network.SetFaults(faults);
    Neuron::Transport& host = network.Host();
    Neuron::Transport& client = network.Connect();
    AcceptOne(host);
    Assert::IsTrue(client.Send(Neuron::HOST_CONNECTION, Datagram(4)));
    client.Poll();
    host.Poll();
    Assert::IsTrue(Drain(host) == std::vector<int>{4, 4});
    Assert::AreEqual(1u, network.Statistics().duplicated);
    Assert::AreEqual(2u, network.Statistics().delivered);
  }

  TEST_METHOD(ADelayedDatagramWaitsTheGivenPolls)
  {
    Neuron::LoopbackTransport network(3);
    Neuron::LoopbackFaults faults;
    faults.delayPerMille = 1000;
    faults.delayPolls = 2;
    network.SetFaults(faults);
    Neuron::Transport& host = network.Host();
    Neuron::Transport& client = network.Connect();
    AcceptOne(host);
    Assert::IsTrue(client.Send(Neuron::HOST_CONNECTION, Datagram(9)));
    client.Poll();
    host.Poll();
    Assert::IsTrue(Drain(host).empty(), L"not on the first poll");
    host.Poll();
    Assert::IsTrue(Drain(host).empty(), L"nor the second");
    host.Poll();
    Assert::IsTrue(Drain(host) == std::vector<int>{9});
    Assert::AreEqual(1u, network.Statistics().delayed);
  }

  TEST_METHOD(ReorderingSwapsADatagramWithTheOneAheadOfIt)
  {
    Neuron::LoopbackTransport network(3);
    Neuron::LoopbackFaults faults;
    faults.reorderPerMille = 1000;
    network.SetFaults(faults);
    Neuron::Transport& host = network.Host();
    Neuron::Transport& client = network.Connect();
    AcceptOne(host);
    for (std::uint8_t mark = 1; mark <= 4; ++mark)
    {
      Assert::IsTrue(client.Send(Neuron::HOST_CONNECTION, Datagram(mark)));
    }
    client.Poll();
    host.Poll();
    const std::vector<int> marks = Drain(host);
    Assert::AreEqual(static_cast<std::size_t>(4), marks.size());
    // Each arrival slots in ahead of the one queued last: 1; 2,1; 2,3,1; 2,3,4,1.
    Assert::IsTrue(marks == std::vector<int>{2, 3, 4, 1});
    Assert::AreEqual(4u, network.Statistics().reordered);
  }

  TEST_METHOD(WithoutFaultsEveryDatagramArrivesOnceInOrder)
  {
    Neuron::LoopbackTransport network(11);
    Neuron::Transport& host = network.Host();
    Neuron::Transport& client = network.Connect();
    const Neuron::ConnectionId id = AcceptOne(host);
    std::vector<int> expected;
    for (std::uint8_t mark = 1; mark <= 50; ++mark)
    {
      Assert::IsTrue(host.Send(id, Datagram(mark)));
      expected.push_back(mark);
    }
    host.Poll();
    client.Poll();
    Assert::IsTrue(Drain(client) == expected);
    Assert::AreEqual(50u, network.Statistics().delivered);
    Assert::AreEqual(0u, network.Statistics().dropped + network.Statistics().duplicated + network.Statistics().delayed +
                           network.Statistics().reordered);
  }
};

} // namespace CoreTests
