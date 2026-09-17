#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Neuron
{

/// A sound as the mixer takes it (TechnicalDesign.md §8): 16-bit PCM, mono or stereo, any sample
/// rate, the samples interleaved by channel.
struct Wave
{
  std::uint32_t sampleRate = 0;
  std::uint16_t channels = 0;
  std::vector<std::int16_t> samples;

  [[nodiscard]] std::size_t FrameCount() const noexcept
  {
    return channels == 0 ? 0 : samples.size() / channels;
  }
};

/// Reads a RIFF/WAVE file with a fmt chunk and a data chunk, skipping the chunks it does not know
/// (LIST, cue, smpl and whatever an editor added), and refuses anything but 16-bit PCM in one or
/// two channels with the chunk named. False, with _error as "byte <offset>: <what>", and _out
/// untouched, for anything refused; a chunk is never read past its declared size.
[[nodiscard]] bool ReadWave(std::span<const std::byte> _bytes, Wave& _out, std::string& _error);

} // namespace Neuron
