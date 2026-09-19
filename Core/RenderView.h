#pragma once

#include <cstdint>
#include <vector>

namespace Neuron
{

/// One thing to draw (TechnicalDesign.md §6.3): a model by id at an interpolated position and
/// heading, the first and only float conversion of a simulation number, with the team's colour
/// and the rank to badge it with. The geometry pass of M1 (m1-vertical-slice/K1) draws them.
///
/// The position is in WORLD UNITS, as the camera and the terrain are, not in the simulation's
/// subunits: this aggregate is where the conversion has already happened, which is what "the only
/// place it happens" in §6.3 means. Models are converted once at load (Client/ModelBuffers.h).
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

/// What a cell is to the commander whose view this is (GameDesign.md §3; ADR-008). The order is
/// the simulation's, so that the replica hands its own bytes across without a table.
enum class FogShade : std::uint8_t
{
  Unexplored,
  Explored,
  Visible
};

/// The commander's fog of war as the replica knows it, for the fog pass (m1-vertical-slice/K2) and
/// the minimap (Interface.md §7), which read the same grid so that the two can never disagree.
///
/// THE CHANGED ROWS ARE PART OF THE VIEW, not something the client works out. Every device that
/// moves changes fog, so "did anything change" is true every frame and only "which rows" is worth
/// having; the producer knows it for free while it writes the grid and the consumer would have to
/// diff a megabyte to recover it. Empty means nothing changed. This is the same bargain
/// changedChunks strikes for the terrain.
struct FogView
{
  std::vector<std::uint8_t> cells;        ///< One FogShade a cell, row major, cellsPerSide a row
  std::vector<std::uint32_t> changedRows; ///< Ascending, without repeats; empty when nothing moved
  std::uint32_t cellsPerSide;
};

/// What the executable builds each frame from the replica and Client draws (ADR-001): the
/// instances, the terrain chunks whose heights changed since the last frame, by chunk index, and
/// the commander's fog.
struct RenderView
{
  std::vector<RenderInstance> instances;
  std::vector<std::uint32_t> changedChunks;
  FogView fog;
};

} // namespace Neuron
