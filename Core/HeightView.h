#pragma once

#include <cstdint>

namespace Neuron
{

/// The heightfield as the renderer reads it (TechnicalDesign.md §6.3; ADR-001): a plain view over
/// the simulation's samples, in the engine namespace, so that the executable builds it from Sim
/// and Client consumes it without naming a Sim type. The samples are int16 whole world units,
/// samplesPerSide by samplesPerSide, row-major with x along a row; the view owns nothing and is
/// valid while the samples are.
struct HeightView
{
  const std::int16_t* samples;
  std::uint32_t samplesPerSide;
  std::int32_t spacingWorldUnits; ///< Between adjacent samples
  std::int32_t waterLevel;        ///< The sea's surface, whole units; every sample below it is under water
  std::int32_t highest;           ///< The highest sample, for the palette's height axis
};

} // namespace Neuron
