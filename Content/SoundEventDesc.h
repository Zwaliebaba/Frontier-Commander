#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The sound-event table on the Species Sounds.txt model (TechnicalDesign.md §8): an event the
// game raises by name, the waves it may play, and how it is mixed. One row per event; a row with
// several waves picks one from the simulation's own stream where the choice is a simulation
// event, and from the client's where it is not.

namespace Frontier
{

/// Where a sound sits. A World sound is attenuated by distance from the camera; an Interface
/// sound is not.
enum class SoundSpace : std::uint8_t
{
  World,
  Interface
};

struct SoundEventDesc
{
  std::string id;
  SoundSpace space;
  std::vector<std::string> waves; ///< Wave file names under Content\Sounds
  std::int32_t volumeHundredths;
  std::int32_t rangeSubunits;  ///< World: beyond this it is inaudible; unread for Interface
  std::uint32_t cooldownTicks; ///< The least between two plays of this event, so a volley is one sound
  bool loops;

  [[nodiscard]] bool operator==(const SoundEventDesc&) const noexcept = default;
};

} // namespace Frontier
