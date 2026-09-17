#include "pch.h"

#include "Random.h"

#include <array>
#include <cstdint>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace CoreTests
{

// The vectors below were printed by `python3 Tools/Xoshiro.py`, the same algorithm in Python, so
// that the landscape tool and this generator draw the same numbers from the same seed.
TEST_CLASS(RandomTests)
{
public:
  TEST_METHOD(SplitMix64MatchesTheReference)
  {
    std::uint64_t state = 0;
    Assert::AreEqual(0xE220A8397B1DCDAFull, Neuron::SplitMix64Next(state));
    Assert::AreEqual(0x6E789E6AA1B965F4ull, Neuron::SplitMix64Next(state));
    Assert::AreEqual(0x06C45D188009454Full, Neuron::SplitMix64Next(state));
  }

  TEST_METHOD(SeedingFillsTheStateFromTwoSplitMixOutputs)
  {
    const Neuron::Random zero(0);
    Assert::AreEqual(0x7B1DCDAFu, zero.GetState()[0]);
    Assert::AreEqual(0xE220A839u, zero.GetState()[1]);
    Assert::AreEqual(0xA1B965F4u, zero.GetState()[2]);
    Assert::AreEqual(0x6E789E6Au, zero.GetState()[3]);
    const Neuron::Random one(1);
    Assert::AreEqual(0x89025CC1u, one.GetState()[0]);
    Assert::AreEqual(0x910A2DECu, one.GetState()[1]);
    Assert::AreEqual(0x658EEC67u, one.GetState()[2]);
    Assert::AreEqual(0xBEEB8DA1u, one.GetState()[3]);
  }

  TEST_METHOD(NextMatchesTheReferenceForThreeSeeds)
  {
    const std::array<std::uint32_t, 10> seedZero = {0xDEC9045Du, 0x9A089D75u, 0xAB77D362u, 0xC3E16405u, 0x5C95A8DAu,
                                                    0x60DEA056u, 0xC25A5140u, 0xA4290614u, 0x9E0525AFu, 0x953D37B9u};
    const std::array<std::uint32_t, 10> seedOne = {0x650941BAu, 0x54D30301u, 0x25D2F321u, 0x3FABDCA9u, 0x2AB8E0A6u,
                                                   0xF9890067u, 0xE12B0AD9u, 0xA193D86Au, 0xAA60A3ADu, 0xA0512E90u};
    const std::array<std::uint32_t, 10> seedFortyTwo = {0x69E85A2Au, 0xF843FAD0u, 0x0105185Fu, 0x8A1F1EA6u, 0xA66BE2A9u,
                                                        0x9844904Eu, 0xAF4213E7u, 0x85C95CD7u, 0xD4A5504Au, 0xAE8D0101u};
    Neuron::Random zero(0);
    Neuron::Random one(1);
    Neuron::Random fortyTwo(42);
    for (std::size_t index = 0; index < 10; ++index)
    {
      Assert::AreEqual(seedZero[index], zero.Next());
      Assert::AreEqual(seedOne[index], one.Next());
      Assert::AreEqual(seedFortyTwo[index], fortyTwo.Next());
    }
  }

  TEST_METHOD(BelowMatchesTheReferenceAndStaysInRange)
  {
    const std::array<std::uint32_t, 10> seedZero = {1, 1, 2, 3, 0, 0, 0, 0, 3, 3};
    const std::array<std::uint32_t, 10> seedOne = {4, 5, 1, 5, 4, 3, 3, 0, 5, 2};
    Neuron::Random zero(0);
    Neuron::Random one(1);
    for (std::size_t index = 0; index < 10; ++index)
    {
      Assert::AreEqual(seedZero[index], zero.Below(6));
      Assert::AreEqual(seedOne[index], one.Below(6));
    }
    Neuron::Random any(7);
    for (int draw = 0; draw < 10000; ++draw)
    {
      Assert::IsTrue(any.Below(1) == 0);
      Assert::IsTrue(any.Below(7) < 7);
      const std::int32_t between = any.Between(-3, 3);
      Assert::IsTrue(between >= -3 && between <= 3);
    }
    Assert::IsTrue(any.Between(5, 5) == 5);
  }

  TEST_METHOD(DerivedSeedsMatchTheReference)
  {
    Assert::AreEqual(0x910A2DEC89025CC1ull, Neuron::DeriveSeed(1, 0));
    Assert::AreEqual(0xBEEB8DA1658EEC67ull, Neuron::DeriveSeed(1, 1));
    Assert::AreEqual(0xF893A2EEFB32555Eull, Neuron::DeriveSeed(1, 2));
    Assert::AreEqual(0x71C18690EE42C90Bull, Neuron::DeriveSeed(1, 3));
  }

  TEST_METHOD(TheStateRestoresTheSequence)
  {
    Neuron::Random original(12345);
    for (int draw = 0; draw < 100; ++draw)
    {
      (void)original.Next();
    }
    const Neuron::Random::State saved = original.GetState();
    Neuron::Random resumed(saved);
    for (int draw = 0; draw < 100; ++draw)
    {
      Assert::AreEqual(original.Next(), resumed.Next());
    }
  }
};

} // namespace CoreTests
