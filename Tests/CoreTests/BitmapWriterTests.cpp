#include "pch.h"

#include "BitmapWriter.h"

#include <cstddef>
#include <cstdint>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace CoreTests
{

TEST_CLASS(BitmapWriterTests)
{
public:
  TEST_METHOD(ATwoByTwoImageMatchesTheLiteralByteForByte)
  {
    // Top row: red, green. Bottom row: blue, white with zero alpha (dropped).
    const std::vector<std::uint8_t> rgba = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 0};
    const std::vector<std::byte> bitmap = Neuron::WriteBitmap(2, 2, rgba);
    const std::uint8_t expected[] = {
      // BITMAPFILEHEADER: "BM", the 70-byte file, two reserved words, the pixels at byte 54.
      'B', 'M', 70, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0,
      // BITMAPINFOHEADER: 40 bytes, 2 by 2, one plane, 24 bits, BI_RGB, 16 image bytes, 2835 pixels per metre twice, no palette.
      40, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 1, 0, 24, 0, 0, 0, 0, 0, 16, 0, 0, 0, 0x13, 0x0B, 0, 0, 0x13, 0x0B, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      // The bottom row first, as B, G, R, padded to four bytes; then the top row.
      255, 0, 0, 255, 255, 255, 0, 0, 0, 0, 255, 0, 255, 0, 0, 0};
    Assert::AreEqual(sizeof expected, bitmap.size());
    Assert::AreEqual(Neuron::BitmapBytes(2, 2), bitmap.size());
    for (std::size_t index = 0; index < sizeof expected; ++index)
    {
      Assert::AreEqual(static_cast<int>(expected[index]), static_cast<int>(std::to_integer<std::uint8_t>(bitmap[index])));
    }
  }

  TEST_METHOD(RowsArePaddedToFourBytes)
  {
    const std::vector<std::uint8_t> rgba(12, 7); // three pixels
    const std::vector<std::byte> bitmap = Neuron::WriteBitmap(3, 1, rgba);
    Assert::AreEqual(static_cast<std::size_t>(54 + 12), bitmap.size());
    Assert::AreEqual(static_cast<std::size_t>(54 + 12), Neuron::BitmapBytes(3, 1));
    Assert::AreEqual(0, static_cast<int>(std::to_integer<std::uint8_t>(bitmap[54 + 9])), L"the pad bytes are zero");
    Assert::AreEqual(0, static_cast<int>(std::to_integer<std::uint8_t>(bitmap[54 + 11])));
    Assert::AreEqual(static_cast<std::size_t>(54 + 4 * 1024), Neuron::BitmapBytes(1, 1024));
  }
};

} // namespace CoreTests
