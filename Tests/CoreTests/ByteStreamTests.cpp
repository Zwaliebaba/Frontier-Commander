#include "pch.h"

#include "ByteReader.h"
#include "ByteWriter.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace CoreTests
{

namespace
{

enum class Kind : std::uint8_t
{
  Device = 3,
  Structure = 4
};

// A record of every width, 22 bytes on the wire: the layout is pinned below so that a change is
// a visible diff.
struct Record
{
  std::uint8_t a = 0x01;
  std::int8_t b = -2;
  std::uint16_t c = 0x0304;
  std::int16_t d = -5;
  std::uint32_t e = 0x06070809u;
  std::int32_t f = -10;
  std::uint64_t g = 0x0B0C0D0E0F101112ull;
  Kind kind = Kind::Structure;
  bool flag = true;
};

constexpr std::size_t RECORD_BYTES = 1 + 1 + 2 + 2 + 4 + 4 + 8 + 1 + 1;

void WriteRecord(Neuron::ByteWriter& _writer, const Record& _record)
{
  _writer.Write(_record.a);
  _writer.Write(_record.b);
  _writer.Write(_record.c);
  _writer.Write(_record.d);
  _writer.Write(_record.e);
  _writer.Write(_record.f);
  _writer.Write(_record.g);
  _writer.Write(_record.kind);
  _writer.WriteBool(_record.flag);
}

bool ReadRecord(Neuron::ByteReader& _reader, Record& _record)
{
  return _reader.Read(_record.a) && _reader.Read(_record.b) && _reader.Read(_record.c) && _reader.Read(_record.d) &&
         _reader.Read(_record.e) && _reader.Read(_record.f) && _reader.Read(_record.g) && _reader.Read(_record.kind) &&
         _reader.ReadBool(_record.flag);
}

} // namespace

TEST_CLASS(ByteStreamTests)
{
public:
  TEST_METHOD(EveryWidthRoundTrips)
  {
    Neuron::ByteWriter writer;
    const Record written;
    WriteRecord(writer, written);
    Assert::AreEqual(RECORD_BYTES, writer.Size());
    Neuron::ByteReader reader(writer.Bytes());
    Record read;
    read = Record{0, 0, 0, 0, 0, 0, 0, Kind::Device, false};
    Assert::IsTrue(ReadRecord(reader, read));
    Assert::IsTrue(reader.AtEnd());
    Assert::IsFalse(reader.Failed());
    Assert::AreEqual(static_cast<int>(written.a), static_cast<int>(read.a));
    Assert::AreEqual(static_cast<int>(written.b), static_cast<int>(read.b));
    Assert::AreEqual(static_cast<int>(written.c), static_cast<int>(read.c));
    Assert::AreEqual(static_cast<int>(written.d), static_cast<int>(read.d));
    Assert::AreEqual(written.e, read.e);
    Assert::AreEqual(written.f, read.f);
    Assert::AreEqual(written.g, read.g);
    Assert::IsTrue(written.kind == read.kind);
    Assert::AreEqual(written.flag, read.flag);
  }

  TEST_METHOD(TheBytesArePinnedLittleEndian)
  {
    Neuron::ByteWriter writer;
    WriteRecord(writer, Record{});
    const std::array<std::byte, RECORD_BYTES> expected = {
      std::byte{0x01}, std::byte{0xFE}, std::byte{0x04}, std::byte{0x03}, std::byte{0xFB}, std::byte{0xFF},
      std::byte{0x09}, std::byte{0x08}, std::byte{0x07}, std::byte{0x06}, std::byte{0xF6}, std::byte{0xFF},
      std::byte{0xFF}, std::byte{0xFF}, std::byte{0x12}, std::byte{0x11}, std::byte{0x10}, std::byte{0x0F},
      std::byte{0x0E}, std::byte{0x0D}, std::byte{0x0C}, std::byte{0x0B}, std::byte{0x04}, std::byte{0x01}};
    const std::span<const std::byte> bytes = writer.Bytes();
    Assert::AreEqual(expected.size(), bytes.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
      Assert::AreEqual(static_cast<int>(expected[index]), static_cast<int>(bytes[index]));
    }
  }

  TEST_METHOD(TruncationAtEveryByteFailsAndStaysFailed)
  {
    Neuron::ByteWriter writer;
    WriteRecord(writer, Record{});
    const std::span<const std::byte> whole = writer.Bytes();
    for (std::size_t length = 0; length < whole.size(); ++length)
    {
      Neuron::ByteReader reader(whole.subspan(0, length));
      Record record;
      Assert::IsFalse(ReadRecord(reader, record), L"a truncated record read whole");
      Assert::IsTrue(reader.Failed());
      std::uint8_t more;
      Assert::IsFalse(reader.Read(more), L"a failed reader read again");
    }
  }

  TEST_METHOD(ALengthPrefixBeyondTheBufferOrTheLimitFails)
  {
    Neuron::ByteWriter writer;
    writer.Write<std::uint32_t>(1000);
    writer.Write<std::uint8_t>(1);
    {
      Neuron::ByteReader reader(writer.Bytes());
      std::span<const std::byte> out;
      Assert::IsFalse(reader.ReadSpan(1000, out));
      Assert::IsTrue(reader.Failed());
    }
    Neuron::ByteWriter honest;
    const std::array<std::byte, 3> payload = {std::byte{7}, std::byte{8}, std::byte{9}};
    honest.WriteSpan(payload);
    {
      Neuron::ByteReader reader(honest.Bytes());
      std::span<const std::byte> out;
      Assert::IsFalse(reader.ReadSpan(2, out), L"a span over the caller's limit was accepted");
    }
    {
      Neuron::ByteReader reader(honest.Bytes());
      std::span<const std::byte> out;
      Assert::IsTrue(reader.ReadSpan(3, out));
      Assert::AreEqual(std::size_t{3}, out.size());
      Assert::AreEqual(9, static_cast<int>(out[2]));
      Assert::IsTrue(reader.AtEnd());
    }
  }

  TEST_METHOD(TheHeaderChecksMagicAndVersion)
  {
    Neuron::ByteWriter writer;
    writer.WriteHeader({0x46435350u, 3});
    Neuron::StreamHeader header{};
    {
      Neuron::ByteReader reader(writer.Bytes());
      Assert::IsTrue(reader.ReadHeader(0x46435350u, 1, 3, header));
      Assert::AreEqual(3, static_cast<int>(header.version));
    }
    {
      Neuron::ByteReader reader(writer.Bytes());
      Assert::IsFalse(reader.ReadHeader(0x46435350u, 4, 5, header), L"a version below the oldest was accepted");
      Assert::IsTrue(reader.Failed());
    }
    {
      Neuron::ByteReader reader(writer.Bytes());
      Assert::IsFalse(reader.ReadHeader(0x46435350u, 1, 2, header), L"a version above the newest was accepted");
    }
    {
      Neuron::ByteReader reader(writer.Bytes());
      Assert::IsFalse(reader.ReadHeader(0x12345678u, 1, 3, header), L"a wrong magic was accepted");
    }
    {
      Neuron::ByteReader reader(writer.Bytes().subspan(0, 5));
      Assert::IsFalse(reader.ReadHeader(0x46435350u, 1, 3, header), L"a short header was accepted");
    }
  }

  TEST_METHOD(RawBytesAndReleaseMoveTheBytesOut)
  {
    Neuron::ByteWriter writer;
    const std::array<std::byte, 2> raw = {std::byte{0xAA}, std::byte{0xBB}};
    writer.WriteBytes(raw);
    std::vector<std::byte> released = writer.Release();
    Assert::AreEqual(std::size_t{2}, released.size());
    Assert::AreEqual(std::size_t{0}, writer.Size());
    Neuron::ByteReader reader(released);
    std::array<std::byte, 2> back{};
    Assert::IsTrue(reader.ReadBytes(back));
    Assert::AreEqual(0xBB, static_cast<int>(back[1]));
    Assert::IsFalse(reader.ReadBytes(back), L"read past the end");
  }
};

} // namespace CoreTests
