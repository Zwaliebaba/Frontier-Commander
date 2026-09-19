#pragma once

#include <cstdint>

// What a kill is worth (GameDesign.md §8; m1-vertical-slice/S10). Sim/Design.h holds the eight
// ranks and their thresholds - the owner's answer to OpenQuestions.md Q22 - and says experience is
// "kills weighted by GameDesign.md §8". The design gives the ranks and the word "weighted" and no
// weights, so the weighting is stated here, once, where the one caller reads it.
//
// THE WEIGHT IS WHAT THE KILL COST ITS OWNER, over the cost of the reference device: the light on
// wheels with a machine gun that GameDesign.md §6 works through, at 130 power. A scout is worth
// one, a heavy on tracks with a cannon three, a command post three. That makes rank 7's 160 an
// order of a hundred kills rather than a number a good minute reaches, which is what Q22's answer
// says the curve is for, and it needs no table of its own: a mod that adds a costlier device gets
// a costlier kill without an entry anywhere.

namespace Frontier
{

/// The cost of the device GameDesign.md §6 sizes everything else against, in hundredths.
inline constexpr std::int32_t REFERENCE_KILL_COST_HUNDREDTHS = 13000;

/// The experience destroying something of this cost awards. Never nought, so that killing the
/// cheapest thing on the field still counts toward a rank.
[[nodiscard]] constexpr std::uint32_t ExperienceForKill(std::int32_t _costHundredths) noexcept
{
  const std::int32_t weighted = _costHundredths > 0 ? _costHundredths / REFERENCE_KILL_COST_HUNDREDTHS : 0;
  return static_cast<std::uint32_t>(weighted < 1 ? 1 : weighted);
}

} // namespace Frontier
