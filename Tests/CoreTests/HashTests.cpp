#include "pch.h"

#include "Hash.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace CoreTests
{

namespace
{

std::uint64_t HashText(const char* _text)
{
  return Neuron::Fnv1a64(std::as_bytes(std::span<const char>(_text, std::strlen(_text))));
}

} // namespace

TEST_CLASS(HashTests)
{
public:
  TEST_METHOD(MatchesThePublishedVectors)
  {
    Assert::AreEqual(0xCBF29CE484222325ull, HashText(""));
    Assert::AreEqual(0xAF63DC4C8601EC8Cull, HashText("a"));
    Assert::AreEqual(0x85944171F73967E8ull, HashText("foobar"));
  }

  TEST_METHOD(AnIntegerHashesAsItsLittleEndianBytes)
  {
    const std::array<std::byte, 4> bytes = {std::byte{0x78}, std::byte{0x56}, std::byte{0x34}, std::byte{0x12}};
    Assert::AreEqual(Neuron::Fnv1a64(bytes), Neuron::HashInteger(Neuron::FNV1A64_OFFSET, std::uint32_t{0x12345678}));
    Assert::AreEqual(Neuron::Fnv1a64(bytes), Neuron::HashInteger(Neuron::FNV1A64_OFFSET, std::int32_t{0x12345678}));
    const std::array<std::byte, 2> negative = {std::byte{0xFF}, std::byte{0xFF}};
    Assert::AreEqual(Neuron::Fnv1a64(negative), Neuron::HashInteger(Neuron::FNV1A64_OFFSET, std::int16_t{-1}));
    Assert::AreNotEqual(Neuron::HashInteger(Neuron::FNV1A64_OFFSET, std::int16_t{-1}),
                        Neuron::HashInteger(Neuron::FNV1A64_OFFSET, std::int32_t{-1}));
  }

  TEST_METHOD(ASpanChainsInOrder)
  {
    const std::array<std::int32_t, 3> values = {1, -2, 3};
    std::uint64_t chained = Neuron::FNV1A64_OFFSET;
    for (const std::int32_t value : values)
    {
      chained = Neuron::HashInteger(chained, value);
    }
    Assert::AreEqual(chained, Neuron::HashIntegers(Neuron::FNV1A64_OFFSET, std::span<const std::int32_t>(values)));
    const std::array<std::int32_t, 3> reordered = {3, -2, 1};
    Assert::AreNotEqual(chained, Neuron::HashIntegers(Neuron::FNV1A64_OFFSET, std::span<const std::int32_t>(reordered)));
  }

  TEST_METHOD(EnumsHashAsTheirUnderlyingValue)
  {
    enum class Kind : std::uint8_t
    {
      Device = 7
    };
    Assert::AreEqual(Neuron::HashInteger(Neuron::FNV1A64_OFFSET, std::uint8_t{7}),
                     Neuron::HashInteger(Neuron::FNV1A64_OFFSET, Kind::Device));
  }
};

} // namespace CoreTests
