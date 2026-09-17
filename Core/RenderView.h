#pragma once

#include <cstdint>
#include <vector>

namespace Neuron
{

/// One thing to draw (TechnicalDesign.md §6.3): a model by id at an interpolated position and
/// heading, the first and only float conversion of a simulation number, with the team's colour
/// and the rank to badge it with. The geometry pass of M1 (m1-vertical-slice/K1) draws them.
struct RenderInstance
{
  std::uint32_t modelId;
  float x;
  float y;
  float z;
  float headingRadians;
  std::uint32_t teamColor; ///< RGBA8, R in the low byte
  std::uint8_t rankBadge;
};

/// What the executable builds each frame from the replica and Client draws (ADR-001): the
/// instances, and the terrain chunks whose heights changed since the last frame, by chunk index.
struct RenderView
{
  std::vector<RenderInstance> instances;
  std::vector<std::uint32_t> changedChunks;
};

} // namespace Neuron
