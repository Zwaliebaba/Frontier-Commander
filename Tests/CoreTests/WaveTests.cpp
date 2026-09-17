#include "pch.h"

#include "Wave.h"

#include "ByteWriter.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace CoreTests
{

namespace
{

void WriteId(Neuron::ByteWriter& _writer, const char (&_id)[5])
{
  for (int index = 0; index < 4; ++index)
  {
    _writer.Write(static_cast<std::uint8_t>(_id[index]));
  }
}

/// One chunk: the id, the size, the body, and the pad byte an odd body needs.
void WriteChunk(Neuron::ByteWriter& _writer, const char (&_id)[5], std::span<const std::uint8_t> _body)
{
  WriteId(_writer, _id);
  _writer.Write(static_cast<std::uint32_t>(_body.size()));
  for (const std::uint8_t byte : _body)
  {
    _writer.Write(byte);
  }
  if (_body.size() % 2 == 1)
  {
    _writer.Write(static_cast<std::uint8_t>(0));
  }
}

std::vector<std::uint8_t> FormatChunk(std::uint16_t _tag, std::uint16_t _channels, std::uint32_t _rate, std::uint16_t _blockAlign,
                                      std::uint16_t _bits)
{
  Neuron::ByteWriter writer;
  writer.Write(_tag);
  writer.Write(_channels);
  writer.Write(_rate);
  writer.Write(_rate * _blockAlign);
  writer.Write(_blockAlign);
  writer.Write(_bits);
  const std::span<const std::byte> bytes = writer.Bytes();
  std::vector<std::uint8_t> body(bytes.size());
  for (std::size_t index = 0; index < bytes.size(); ++index)
  {
    body[index] = std::to_integer<std::uint8_t>(bytes[index]);
  }
  return body;
}

std::vector<std::uint8_t> SampleBytes(std::span<const std::int16_t> _samples)
{
  std::vector<std::uint8_t> body;
  for (const std::int16_t sample : _samples)
  {
    const std::uint16_t bits = static_cast<std::uint16_t>(sample);
    body.push_back(static_cast<std::uint8_t>(bits & 0xFF));
    body.push_back(static_cast<std::uint8_t>(bits >> 8));
  }
  return body;
}

/// A RIFF/WAVE file from its chunks, the RIFF size computed unless overridden.
struct RiffBuilder
{
  std::vector<std::vector<std::uint8_t>> chunkBodies;
  std::vector<std::string> chunkIds;
  const char* riff = "RIFF";
  const char* form = "WAVE";
  std::int32_t riffSizeAdjust = 0;
  std::vector<std::uint8_t> trailing;

  RiffBuilder& Chunk(const std::string& _id, std::vector<std::uint8_t> _body)
  {
    chunkIds.push_back(_id);
    chunkBodies.push_back(std::move(_body));
    return *this;
  }

  [[nodiscard]] std::vector<std::byte> Build() const
  {
    Neuron::ByteWriter chunks;
    for (std::size_t index = 0; index < chunkIds.size(); ++index)
    {
      char id[5] = {chunkIds[index][0], chunkIds[index][1], chunkIds[index][2], chunkIds[index][3], 0};
      WriteChunk(chunks, id, chunkBodies[index]);
    }
    Neuron::ByteWriter writer;
    char riffId[5] = {riff[0], riff[1], riff[2], riff[3], 0};
    WriteId(writer, riffId);
    writer.Write(static_cast<std::uint32_t>(static_cast<std::int32_t>(4 + chunks.Size()) + riffSizeAdjust));
    char formId[5] = {form[0], form[1], form[2], form[3], 0};
    WriteId(writer, formId);
    writer.WriteBytes(chunks.Bytes());
    for (const std::uint8_t byte : trailing)
    {
      writer.Write(byte);
    }
    return writer.Release();
  }
};

RiffBuilder Mono(std::span<const std::int16_t> _samples)
{
  RiffBuilder riff;
  riff.Chunk("fmt ", FormatChunk(1, 1, 22050, 2, 16));
  riff.Chunk("data", SampleBytes(_samples));
  return riff;
}

void ExpectRefused(const RiffBuilder& _riff, const char* _fragment, const wchar_t* _case)
{
  const std::vector<std::byte> bytes = _riff.Build();
  Neuron::Wave wave;
  std::string error;
  Assert::IsFalse(Neuron::ReadWave(bytes, wave, error), _case);
  Assert::IsTrue(error.rfind("byte ", 0) == 0, L"the error names a byte offset");
  if (error.find(_fragment) == std::string::npos)
  {
    const std::string message = std::string("expected '") + _fragment + "' in: " + error;
    Assert::Fail(std::wstring(message.begin(), message.end()).c_str());
  }
}

} // namespace

TEST_CLASS(WaveTests)
{
public:
  TEST_METHOD(AMonoFileReadsItsSamples)
  {
    const std::int16_t samples[] = {0, 1000, -1000, 32767, -32768};
    Neuron::Wave wave;
    std::string error;
    Assert::IsTrue(Neuron::ReadWave(Mono(samples).Build(), wave, error), L"the file should read");
    Assert::AreEqual(22050u, wave.sampleRate);
    Assert::AreEqual(1, static_cast<int>(wave.channels));
    Assert::AreEqual(static_cast<std::size_t>(5), wave.FrameCount());
    Assert::AreEqual(1000, static_cast<int>(wave.samples[1]));
    Assert::AreEqual(-32768, static_cast<int>(wave.samples[4]));
  }

  TEST_METHOD(AStereoFileInterleavesAndUnknownChunksAreSkipped)
  {
    const std::int16_t samples[] = {1, 2, 3, 4}; // two frames of left, right
    RiffBuilder riff;
    riff.Chunk("LIST", {'I', 'N', 'F', 'O', 'x'}); // odd size, so a pad byte follows
    riff.Chunk("fmt ", FormatChunk(1, 2, 44100, 4, 16));
    riff.Chunk("CSET", {1, 2, 3});
    riff.Chunk("data", SampleBytes(samples));
    riff.Chunk("id3 ", {9, 9, 9, 9});
    Neuron::Wave wave;
    std::string error;
    Assert::IsTrue(Neuron::ReadWave(riff.Build(), wave, error), L"the file should read");
    Assert::AreEqual(44100u, wave.sampleRate);
    Assert::AreEqual(2, static_cast<int>(wave.channels));
    Assert::AreEqual(static_cast<std::size_t>(2), wave.FrameCount());
    Assert::AreEqual(3, static_cast<int>(wave.samples[2]));
  }

  TEST_METHOD(BytesBeyondTheRiffAreIgnored)
  {
    const std::int16_t samples[] = {5};
    RiffBuilder riff = Mono(samples);
    riff.trailing = {0xAB};
    Neuron::Wave wave;
    std::string error;
    Assert::IsTrue(Neuron::ReadWave(riff.Build(), wave, error), L"a byte after the RIFF is not a chunk");
    Assert::AreEqual(static_cast<std::size_t>(1), wave.FrameCount());
  }

  TEST_METHOD(EveryOtherFormatIsRefusedWithTheChunkNamed)
  {
    const std::int16_t samples[] = {1, 2};

    RiffBuilder notRiff = Mono(samples);
    notRiff.riff = "RIFX";
    ExpectRefused(notRiff, "not a RIFF", L"another container");

    RiffBuilder notWave = Mono(samples);
    notWave.form = "AVI ";
    ExpectRefused(notWave, "not a WAVE", L"another form");

    RiffBuilder tooLong = Mono(samples);
    tooLong.riffSizeAdjust = 8;
    ExpectRefused(tooLong, "runs past the end", L"a RIFF size beyond the file");

    RiffBuilder noFormat;
    noFormat.Chunk("data", SampleBytes(samples));
    ExpectRefused(noFormat, "no \"fmt \"", L"no fmt chunk");

    RiffBuilder noData;
    noData.Chunk("fmt ", FormatChunk(1, 1, 22050, 2, 16));
    ExpectRefused(noData, "no \"data\"", L"no data chunk");

    RiffBuilder floatFormat;
    floatFormat.Chunk("fmt ", FormatChunk(3, 1, 22050, 4, 32)).Chunk("data", SampleBytes(samples));
    ExpectRefused(floatFormat, "format tag 3", L"IEEE float");

    RiffBuilder eightBit;
    eightBit.Chunk("fmt ", FormatChunk(1, 1, 22050, 1, 8)).Chunk("data", SampleBytes(samples));
    ExpectRefused(eightBit, "8 bits per sample", L"8-bit PCM");

    RiffBuilder threeChannels;
    threeChannels.Chunk("fmt ", FormatChunk(1, 3, 22050, 6, 16)).Chunk("data", SampleBytes(samples));
    ExpectRefused(threeChannels, "3 channels", L"three channels");

    RiffBuilder badAlign;
    badAlign.Chunk("fmt ", FormatChunk(1, 1, 22050, 4, 16)).Chunk("data", SampleBytes(samples));
    ExpectRefused(badAlign, "block align 4", L"a wrong block align");

    RiffBuilder noRate;
    noRate.Chunk("fmt ", FormatChunk(1, 1, 0, 2, 16)).Chunk("data", SampleBytes(samples));
    ExpectRefused(noRate, "sample rate 0", L"a zero sample rate");

    RiffBuilder shortFormat;
    shortFormat.Chunk("fmt ", {1, 0, 1, 0, 0x22, 0x56, 0, 0}).Chunk("data", SampleBytes(samples));
    ExpectRefused(shortFormat, "PCM needs 16", L"a short fmt chunk");

    RiffBuilder oddData;
    oddData.Chunk("fmt ", FormatChunk(1, 2, 22050, 4, 16)).Chunk("data", {1, 2, 3, 4, 5, 6});
    ExpectRefused(oddData, "whole number of 4-byte frames", L"a data chunk that is not whole frames");

    RiffBuilder twoData = Mono(samples);
    twoData.Chunk("data", SampleBytes(samples));
    ExpectRefused(twoData, "second \"data\"", L"two data chunks");

    // A chunk whose size runs past the RIFF's end: the RIFF says 4 + chunks, so shrink the RIFF.
    RiffBuilder overrun = Mono(samples);
    overrun.riffSizeAdjust = -2;
    ExpectRefused(overrun, "runs past the end of the RIFF", L"a chunk beyond the RIFF");
  }

  TEST_METHOD(ARefusedReadLeavesTheOutputAsItWas)
  {
    const std::int16_t samples[] = {7, 8, 9};
    Neuron::Wave wave;
    std::string error;
    Assert::IsTrue(Neuron::ReadWave(Mono(samples).Build(), wave, error));
    RiffBuilder broken;
    broken.Chunk("fmt ", FormatChunk(1, 1, 22050, 2, 8)).Chunk("data", SampleBytes(samples));
    Assert::IsFalse(Neuron::ReadWave(broken.Build(), wave, error));
    Assert::AreEqual(static_cast<std::size_t>(3), wave.FrameCount());
    Assert::AreEqual(22050u, wave.sampleRate);
  }
};

} // namespace CoreTests
