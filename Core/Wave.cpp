#include "pch.h"

#include "Wave.h"

#include "ByteReader.h"

#include <utility>

namespace Neuron
{

namespace
{

[[nodiscard]] constexpr std::uint32_t FourCc(char _a, char _b, char _c, char _d) noexcept
{
  return static_cast<std::uint32_t>(static_cast<unsigned char>(_a)) | (static_cast<std::uint32_t>(static_cast<unsigned char>(_b)) << 8) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(_c)) << 16) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(_d)) << 24);
}

inline constexpr std::uint32_t FOURCC_RIFF = FourCc('R', 'I', 'F', 'F');
inline constexpr std::uint32_t FOURCC_WAVE = FourCc('W', 'A', 'V', 'E');
inline constexpr std::uint32_t FOURCC_FMT = FourCc('f', 'm', 't', ' ');
inline constexpr std::uint32_t FOURCC_DATA = FourCc('d', 'a', 't', 'a');

inline constexpr std::uint16_t WAVE_FORMAT_PCM = 1;
inline constexpr std::uint16_t BITS_PER_SAMPLE = 16;
inline constexpr std::uint32_t MAX_SAMPLE_RATE = 192000;

[[nodiscard]] std::string ChunkName(std::uint32_t _fourCc)
{
  std::string text;
  for (int index = 0; index < 4; ++index)
  {
    const char c = static_cast<char>((_fourCc >> (8 * index)) & 0xFF);
    text.push_back((c >= 0x20 && c < 0x7F) ? c : '?');
  }
  return text;
}

[[nodiscard]] bool Refuse(std::string& _error, std::size_t _offset, const std::string& _what)
{
  _error = "byte " + std::to_string(_offset) + ": " + _what;
  return false;
}

} // namespace

bool ReadWave(std::span<const std::byte> _bytes, Wave& _out, std::string& _error)
{
  ByteReader reader(_bytes);
  std::uint32_t riff = 0;
  std::uint32_t riffBytes = 0;
  std::uint32_t form = 0;
  if (!reader.Read(riff) || !reader.Read(riffBytes) || !reader.Read(form))
  {
    return Refuse(_error, _bytes.size(), "the file ends inside the 12-byte RIFF header");
  }
  if (riff != FOURCC_RIFF)
  {
    return Refuse(_error, 0, "not a RIFF file");
  }
  if (form != FOURCC_WAVE)
  {
    return Refuse(_error, 8, "not a WAVE file (the RIFF form is \"" + ChunkName(form) + "\")");
  }
  if (static_cast<std::uint64_t>(riffBytes) + 8 > _bytes.size())
  {
    return Refuse(_error, 4,
                  "the RIFF size " + std::to_string(riffBytes) + " runs past the end of the " + std::to_string(_bytes.size()) +
                    "-byte file");
  }
  // Chunks end where the RIFF says, not where the file does: a trailing byte an editor left is not a chunk.
  const std::size_t end = static_cast<std::size_t>(riffBytes) + 8;

  bool haveFormat = false;
  std::uint32_t sampleRate = 0;
  std::uint16_t channels = 0;
  std::uint16_t blockAlign = 0;
  std::size_t dataAt = 0;
  std::uint32_t dataBytes = 0;
  bool haveData = false;
  while (reader.Position() + 8 <= end)
  {
    const std::size_t chunkAt = reader.Position();
    std::uint32_t id = 0;
    std::uint32_t size = 0;
    if (!reader.Read(id) || !reader.Read(size))
    {
      return Refuse(_error, chunkAt, "the file ends inside a chunk header");
    }
    const std::size_t bodyAt = reader.Position();
    if (static_cast<std::uint64_t>(bodyAt) + size > end)
    {
      return Refuse(_error, chunkAt,
                    "the chunk \"" + ChunkName(id) + "\" of " + std::to_string(size) + " bytes runs past the end of the RIFF");
    }
    if (id == FOURCC_FMT)
    {
      std::uint16_t formatTag = 0;
      std::uint32_t byteRate = 0;
      std::uint16_t bitsPerSample = 0;
      if (size < 16 || !reader.Read(formatTag) || !reader.Read(channels) || !reader.Read(sampleRate) || !reader.Read(byteRate) ||
          !reader.Read(blockAlign) || !reader.Read(bitsPerSample))
      {
        return Refuse(_error, chunkAt, "the \"fmt \" chunk is " + std::to_string(size) + " bytes, and PCM needs 16");
      }
      if (formatTag != WAVE_FORMAT_PCM)
      {
        return Refuse(_error, bodyAt, "\"fmt \": format tag " + std::to_string(formatTag) + " is not PCM (1)");
      }
      if (bitsPerSample != BITS_PER_SAMPLE)
      {
        return Refuse(_error, bodyAt + 14, "\"fmt \": " + std::to_string(bitsPerSample) + " bits per sample; only 16 is admitted");
      }
      if (channels != 1 && channels != 2)
      {
        return Refuse(_error, bodyAt + 2, "\"fmt \": " + std::to_string(channels) + " channels; only mono and stereo are admitted");
      }
      if (sampleRate == 0 || sampleRate > MAX_SAMPLE_RATE)
      {
        return Refuse(_error, bodyAt + 4, "\"fmt \": the sample rate " + std::to_string(sampleRate) + " is outside 1..192000");
      }
      if (blockAlign != channels * 2)
      {
        return Refuse(_error, bodyAt + 12,
                      "\"fmt \": the block align " + std::to_string(blockAlign) + " is not " + std::to_string(channels * 2) +
                        " for 16-bit PCM in " + std::to_string(channels) + " channel(s)");
      }
      haveFormat = true;
    }
    else if (id == FOURCC_DATA)
    {
      if (haveData)
      {
        return Refuse(_error, chunkAt, "a second \"data\" chunk");
      }
      dataAt = bodyAt;
      dataBytes = size;
      haveData = true;
    }
    // The chunk body, and the pad byte that keeps the next chunk on an even offset; a last chunk
    // whose pad the RIFF does not include ends the walk.
    const std::size_t next = bodyAt + size + (size & 1);
    if (next > end || !reader.Skip(next - reader.Position()))
    {
      break;
    }
  }
  if (!haveFormat)
  {
    return Refuse(_error, end, "no \"fmt \" chunk");
  }
  if (!haveData)
  {
    return Refuse(_error, end, "no \"data\" chunk");
  }
  if (dataBytes % blockAlign != 0)
  {
    return Refuse(_error, dataAt,
                  "the \"data\" chunk of " + std::to_string(dataBytes) + " bytes is not a whole number of " + std::to_string(blockAlign) +
                    "-byte frames");
  }
  Wave wave;
  wave.sampleRate = sampleRate;
  wave.channels = channels;
  wave.samples.resize(dataBytes / 2);
  ByteReader samples(_bytes.subspan(dataAt, dataBytes));
  for (std::int16_t& sample : wave.samples)
  {
    if (!samples.Read(sample))
    {
      return Refuse(_error, dataAt, "the \"data\" chunk could not be read"); // unreachable: the size was checked
    }
  }
  _out = std::move(wave);
  return true;
}

} // namespace Neuron
