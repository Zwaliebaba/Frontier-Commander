#include "pch.h"

#include "Fragmenter.h"
#include "Reassembler.h"

#include <cstdint>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// Splitting a frame and putting it back together (TechnicalDesign.md §5.3).
namespace NetTests
{

namespace
{

std::vector<std::byte> Counting(std::size_t _bytes)
{
  std::vector<std::byte> payload;
  payload.reserve(_bytes);
  for (std::size_t index = 0; index < _bytes; ++index)
  {
    payload.push_back(static_cast<std::byte>(index & 0xFF));
  }
  return payload;
}

} // namespace

TEST_CLASS(FragmenterTests)
{
public:
  TEST_METHOD(AFrameThatFitsADatagramIsOnePiece)
  {
    Assert::AreEqual(static_cast<std::size_t>(1), Frontier::FragmentCount(0));
    Assert::AreEqual(static_cast<std::size_t>(1), Frontier::FragmentCount(Frontier::FRAGMENT_PAYLOAD_BYTES));
    Assert::AreEqual(static_cast<std::size_t>(2), Frontier::FragmentCount(Frontier::FRAGMENT_PAYLOAD_BYTES + 1));
  }

  TEST_METHOD(EveryPieceFitsADatagramWithItsOwnHeader)
  {
    // The whole point of FRAGMENT_PAYLOAD_BYTES: a piece plus the Fragment message's header plus
    // the datagram's own header must be under the path MTU, or IP fragments it and the design's
    // "a lost fragment costs one frame" becomes "a lost IP fragment costs one frame".
    std::vector<Frontier::Fragment> pieces;
    Assert::IsTrue(Frontier::SplitIntoFragments(7, Counting(10000), pieces));
    for (const Frontier::Fragment& piece : pieces)
    {
      Neuron::ByteWriter payload;
      Frontier::Write(payload, piece);
      Assert::IsTrue(payload.Bytes().size() <= Neuron::MAX_DATAGRAM_PAYLOAD_BYTES, L"a piece and its header fit a datagram");
      Neuron::ByteWriter datagram;
      Assert::IsTrue(Neuron::FrameDatagram(payload.Bytes(), datagram));
      Assert::IsTrue(datagram.Bytes().size() <= Neuron::MAX_DATAGRAM_BYTES);
    }
  }

  TEST_METHOD(APayloadSplitAndReassembledIsTheSameBytes)
  {
    for (const std::size_t bytes :
         {std::size_t{1}, std::size_t{1000}, Frontier::FRAGMENT_PAYLOAD_BYTES, Frontier::FRAGMENT_PAYLOAD_BYTES + 1, std::size_t{20000}})
    {
      const std::vector<std::byte> payload = Counting(bytes);
      std::vector<Frontier::Fragment> pieces;
      Assert::IsTrue(Frontier::SplitIntoFragments(3, payload, pieces));
      Assert::AreEqual(Frontier::FragmentCount(bytes), pieces.size());

      Frontier::Reassembler reassembler;
      std::vector<std::byte> whole;
      bool complete = false;
      for (const Frontier::Fragment& piece : pieces)
      {
        complete = reassembler.Add(piece, whole);
      }
      Assert::IsTrue(complete, L"the last piece completed it");
      Assert::AreEqual(payload.size(), whole.size());
      for (std::size_t index = 0; index < payload.size(); ++index)
      {
        Assert::AreEqual(static_cast<int>(payload[index]), static_cast<int>(whole[index]));
      }
    }
  }

  TEST_METHOD(PiecesOutOfOrderStillReassemble)
  {
    const std::vector<std::byte> payload = Counting(5000);
    std::vector<Frontier::Fragment> pieces;
    Assert::IsTrue(Frontier::SplitIntoFragments(9, payload, pieces));
    Assert::IsTrue(pieces.size() >= 4);

    Frontier::Reassembler reassembler;
    std::vector<std::byte> whole;
    bool complete = false;
    for (std::size_t index = pieces.size(); index > 0; --index)
    {
      complete = reassembler.Add(pieces[index - 1], whole) || complete;
    }
    Assert::IsTrue(complete);
    Assert::AreEqual(payload.size(), whole.size());

    // And a duplicate piece is counted rather than taken twice.
    std::vector<std::byte> again;
    Assert::IsFalse(reassembler.Add(pieces[0], again));
  }

  TEST_METHOD(AFrameTooLargeForTheFragmentCountIsRefused)
  {
    std::vector<Frontier::Fragment> pieces;
    Assert::IsFalse(Frontier::SplitIntoFragments(1, Counting(Frontier::FRAGMENT_PAYLOAD_BYTES * (Frontier::MAX_FRAGMENTS + 1)), pieces));
  }
};

} // namespace NetTests
