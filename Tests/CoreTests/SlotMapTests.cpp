#include "pch.h"

#include "SlotMap.h"

#include <cstdint>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace CoreTests
{

namespace
{

struct Record
{
  int hitPoints = 0;
};

std::vector<std::uint32_t> KeysInSlotOrder(const Neuron::SlotMap<Record>& _map)
{
  std::vector<std::uint32_t> keys;
  _map.ForEach([&](Neuron::SlotHandle, std::uint32_t _key, const Record&) { keys.push_back(_key); });
  return keys;
}

std::vector<std::uint32_t> KeysByKey(const Neuron::SlotMap<Record>& _map)
{
  std::vector<std::uint32_t> keys;
  _map.ForEachByKey([&](Neuron::SlotHandle, std::uint32_t _key, const Record&) { keys.push_back(_key); });
  return keys;
}

} // namespace

TEST_CLASS(SlotMapTests)
{
public:
  TEST_METHOD(InsertResolveErase)
  {
    Neuron::SlotMap<Record> map;
    const Neuron::SlotHandle handle = map.Insert(10, Record{7});
    Assert::AreEqual(std::size_t{1}, map.Size());
    Assert::IsNotNull(map.Resolve(handle));
    Assert::AreEqual(7, map.Resolve(handle)->hitPoints);
    Assert::AreEqual(10u, map.KeyOf(handle));
    Assert::IsTrue(map.Erase(handle));
    Assert::IsTrue(map.Empty());
    Assert::IsNull(map.Resolve(handle));
    Assert::IsFalse(map.Erase(handle));
    Assert::IsNull(map.Resolve(Neuron::NULL_SLOT_HANDLE));
  }

  TEST_METHOD(AStaleHandleDoesNotResolveToTheReuser)
  {
    Neuron::SlotMap<Record> map;
    const Neuron::SlotHandle first = map.Insert(1, Record{1});
    map.Erase(first);
    const Neuron::SlotHandle second = map.Insert(2, Record{2});
    Assert::AreEqual(first.index, second.index); // the freed slot is reused
    Assert::AreNotEqual(first.generation, second.generation);
    Assert::IsNull(map.Resolve(first));
    Assert::AreEqual(2, map.Resolve(second)->hitPoints);
  }

  TEST_METHOD(TheGenerationNeverReturnsToZero)
  {
    Assert::AreEqual(6u, Neuron::SlotMap<Record>::NextGeneration(5));
    Assert::AreEqual(1u, Neuron::SlotMap<Record>::NextGeneration(0xFFFFFFFFu));
  }

  TEST_METHOD(IdenticalHistoriesIterateIdentically)
  {
    Neuron::SlotMap<Record> left;
    Neuron::SlotMap<Record> right;
    for (Neuron::SlotMap<Record>* map : {&left, &right})
    {
      const Neuron::SlotHandle a = map->Insert(100, Record{});
      map->Insert(200, Record{});
      const Neuron::SlotHandle c = map->Insert(300, Record{});
      map->Erase(a);
      map->Insert(400, Record{});
      map->Erase(c);
      map->Insert(500, Record{});
      map->Insert(600, Record{});
    }
    const std::vector<std::uint32_t> expected = {400, 200, 500, 600};
    Assert::IsTrue(KeysInSlotOrder(left) == expected);
    Assert::IsTrue(KeysInSlotOrder(right) == expected);
  }

  TEST_METHOD(ByKeyTraversalIsAscendingAfterOutOfOrderErases)
  {
    Neuron::SlotMap<Record> map;
    map.Insert(5, Record{});
    map.Insert(1, Record{});
    const Neuron::SlotHandle nine = map.Insert(9, Record{});
    map.Insert(3, Record{});
    map.Erase(nine);
    map.Insert(7, Record{});
    const std::vector<std::uint32_t> expected = {1, 3, 5, 7};
    Assert::IsTrue(KeysByKey(map) == expected);
    Assert::IsTrue(map.FindByKey(9) == Neuron::NULL_SLOT_HANDLE);
    Assert::AreEqual(7u, map.KeyOf(map.FindByKey(7)));
    Assert::IsTrue(map.FindByKey(4) == Neuron::NULL_SLOT_HANDLE);
  }

  TEST_METHOD(MutationThroughTheVisitorSticks)
  {
    Neuron::SlotMap<Record> map;
    const Neuron::SlotHandle handle = map.Insert(1, Record{1});
    map.ForEach([](Neuron::SlotHandle, std::uint32_t, Record& _record) { _record.hitPoints = 42; });
    Assert::AreEqual(42, map.Resolve(handle)->hitPoints);
    map.ForEachByKey([](Neuron::SlotHandle, std::uint32_t, Record& _record) { _record.hitPoints += 1; });
    Assert::AreEqual(43, map.Resolve(handle)->hitPoints);
  }
};

} // namespace CoreTests
