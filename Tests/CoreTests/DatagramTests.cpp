#include "pch.h"

#include "Datagram.h"

#include "ByteWriter.h"

#include <cstddef>
#include <cstdint>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace CoreTests
{

namespace
{

std::vector<std::byte> Bytes(std::initializer_list<int> _values)
{
  std::vector<std::byte> bytes;
  for (const int value : _values)
  {
    bytes.push_back(static_cast<std::byte>(value));
  }
  return bytes;
}

} // namespace

TEST_CLASS(DatagramTests)
{
public:
  TEST_METHOD(AFramedPayloadRoundTrips)
  {
    const std::vector<std::byte> payload = Bytes({1, 2, 3, 4, 5});
    Neuron::ByteWriter writer;
    Assert::IsTrue(Neuron::FrameDatagram(payload, writer));
    Assert::AreEqual(Neuron::DATAGRAM_HEADER_BYTES + payload.size(), writer.Size());
    const std::span<const std::byte> datagram = writer.Bytes();
    Assert::AreEqual(1, static_cast<int>(std::to_integer<std::uint8_t>(datagram[0]))); // the version, little-endian
    Assert::AreEqual(0, static_cast<int>(std::to_integer<std::uint8_t>(datagram[1])));
    Assert::AreEqual(5, static_cast<int>(std::to_integer<std::uint8_t>(datagram[2]))); // the length
    Neuron::FramingCounters counters;
    std::span<const std::byte> unframed;
    Assert::IsTrue(Neuron::UnframeDatagram(datagram, unframed, counters));
    Assert::AreEqual(payload.size(), unframed.size());
    Assert::IsTrue(std::vector<std::byte>(unframed.begin(), unframed.end()) == payload);
    Assert::AreEqual(0u, counters.wrongVersion + counters.tooShort + counters.tooLong);
  }

  TEST_METHOD(AnEmptyPayloadIsAValidDatagram)
  {
    Neuron::ByteWriter writer;
    Assert::IsTrue(Neuron::FrameDatagram({}, writer));
    Neuron::FramingCounters counters;
    std::span<const std::byte> unframed;
    Assert::IsTrue(Neuron::UnframeDatagram(writer.Bytes(), unframed, counters));
    Assert::AreEqual(static_cast<std::size_t>(0), unframed.size());
  }

  TEST_METHOD(ThePayloadIsBoundedByTheDatagram)
  {
    const std::vector<std::byte> largest(Neuron::MAX_DATAGRAM_PAYLOAD_BYTES, std::byte{7});
    Neuron::ByteWriter writer;
    Assert::IsTrue(Neuron::FrameDatagram(largest, writer));
    Assert::AreEqual(Neuron::MAX_DATAGRAM_BYTES, writer.Size());
    const std::vector<std::byte> tooLarge(Neuron::MAX_DATAGRAM_PAYLOAD_BYTES + 1, std::byte{7});
    writer.Clear();
    Assert::IsFalse(Neuron::FrameDatagram(tooLarge, writer));
    Assert::AreEqual(static_cast<std::size_t>(0), writer.Size(), L"a refused frame writes nothing");
  }

  TEST_METHOD(AnotherVersionIsDroppedAndCounted)
  {
    Neuron::FramingCounters counters;
    std::span<const std::byte> unframed;
    const std::vector<std::byte> other = Bytes({2, 0, 1, 0, 9});
    Assert::IsFalse(Neuron::UnframeDatagram(other, unframed, counters));
    Assert::AreEqual(1u, counters.wrongVersion);
    const std::vector<std::byte> high = Bytes({1, 1, 1, 0, 9});
    Assert::IsFalse(Neuron::UnframeDatagram(high, unframed, counters));
    Assert::AreEqual(2u, counters.wrongVersion);
    Assert::AreEqual(0u, counters.tooShort);
  }

  TEST_METHOD(AShortOrLongDatagramIsDroppedAndCounted)
  {
    Neuron::FramingCounters counters;
    std::span<const std::byte> unframed;
    Assert::IsFalse(Neuron::UnframeDatagram(Bytes({1, 0, 1}), unframed, counters), L"inside the header");
    Assert::AreEqual(1u, counters.tooShort);
    Assert::IsFalse(Neuron::UnframeDatagram(Bytes({1, 0, 3, 0, 9, 9}), unframed, counters), L"claims three, carries two");
    Assert::AreEqual(2u, counters.tooShort);
    Assert::IsFalse(Neuron::UnframeDatagram(Bytes({1, 0, 1, 0, 9, 9}), unframed, counters), L"claims one, carries two");
    Assert::AreEqual(1u, counters.tooLong);
    Assert::IsFalse(Neuron::UnframeDatagram({}, unframed, counters), L"nothing at all");
    Assert::AreEqual(3u, counters.tooShort);
    Assert::AreEqual(0u, counters.wrongVersion, L"the version is not judged on a datagram too short to hold it");
  }
};

} // namespace CoreTests
