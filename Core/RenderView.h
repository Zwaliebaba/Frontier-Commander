#pragma once

#include "BinaryAngle.h"
#include "FixedPoint.h"

#include <cstdint>
#include <numbers>
#include <vector>

namespace Neuron
{

// THE TWO CONVERSIONS THAT MAKE A RENDER NUMBER OUT OF A SIMULATION ONE live here, beside the
// aggregate whose fields they produce. §6.3 says this is where the conversion happens; before
// m1-vertical-slice/R2 each caller kept its own copy of them - Client/ModelBuffers.cpp a
// WorldUnits, Replica/Interpolation.cpp a RADIANS_PER_HEADING_SUBSTEP - and a third was about to be
// written for the model composer.
//
// NOTHING IN Sim MAY CALL EITHER OF THEM. The simulation is fixed point and binary angles from end
// to end because that is what makes it identical on every machine (ADR-002); a float that reached
// it would be a divergence nobody could reproduce. These are the render side of the boundary, and
// Sim does not include this header.

/// Subunits as world units. SUBUNITS_PER_WORLD_UNIT is a power of two, so this divide is exact and
/// a position converted twice is the same float both times.
[[nodiscard]] constexpr float WorldUnitsOfSubunits(std::int32_t _subunits) noexcept
{
  return static_cast<float>(_subunits) / static_cast<float>(SUBUNITS_PER_WORLD_UNIT);
}

/// A binary angle (Core/BinaryAngle.h) in radians. One conversion, so that a device's heading and a
/// marker's cannot disagree about which way round the circle goes.
[[nodiscard]] constexpr float RadiansOfBinaryAngle(std::int64_t _binaryAngle) noexcept
{
  return static_cast<float>(_binaryAngle) * (2.0f * std::numbers::pi_v<float> / static_cast<float>(FULL_TURN));
}

/// The one packing of a colour into a 32-bit word, named here because this header is where the
/// convention started and because everything that carries one agrees with it: red in the LOW byte,
/// which is the byte order DXGI_FORMAT_R8G8B8A8_UNORM reads a vertex attribute in. Core takes four
/// bytes rather than a colour type because the authored colours are Content's (Frontier::Rgba8) and
/// Core is below Content.
[[nodiscard]] constexpr std::uint32_t PackedRgba8(std::uint8_t _red, std::uint8_t _green, std::uint8_t _blue, std::uint8_t _alpha) noexcept
{
  return static_cast<std::uint32_t>(_red) | (static_cast<std::uint32_t>(_green) << 8) | (static_cast<std::uint32_t>(_blue) << 16) |
         (static_cast<std::uint32_t>(_alpha) << 24);
}

/// What an instance is, for the things that are NOT the geometry pass. TechnicalDesign.md §6.2
/// draws "every device, structure, feature and wreck" the same way - the model with the team colour
/// substituted - so this is not a drawing switch. It is what picking and the minimap read: a wreck
/// is "drawn for a while and blocks nothing" (GameDesign.md §7) and is selected by nobody, and a
/// projectile is a cosmetic instance that lives for the few ticks an event describes.
enum class RenderInstanceKind : std::uint8_t
{
  Object,    ///< A device, a structure or a feature: it can be picked, and it is on the minimap
  Wreck,     ///< Drawn, and selected by nothing
  Projectile ///< A shot in flight, gone before anybody could click it
};

/// One thing to draw (TechnicalDesign.md §6.3): a model by id at an interpolated position and
/// heading, the first and only float conversion of a simulation number, with the commander's colour
/// index and the rank to badge it with. The geometry pass of M1 (m1-vertical-slice/K1) draws them.
///
/// The position is in WORLD UNITS, as the camera and the terrain are, not in the simulation's
/// subunits: this aggregate is where the conversion has already happened, which is what "the only
/// place it happens" in §6.3 means. Models are converted once at load (Client/ModelBuffers.h).
///
/// THE COLOUR IS AN INDEX AND NOT A COLOUR (Design/Interface.md §11 row 2, m1-vertical-slice/R2).
/// It used to be the packed word itself, which meant the palette was copied into every instance of
/// every frame and could disagree with the one the minimap and the panels read. The eight commander
/// colours are content now (GameData\Interface.json, m1-vertical-slice/C6); the geometry pass takes
/// the table once per match and resolves the index, so there is one palette and one place it is read.
struct RenderInstance
{
  std::uint32_t modelId;
  float x;
  float y;
  float z;
  float headingRadians;
  RenderInstanceKind kind = RenderInstanceKind::Object;
  std::uint8_t colorIndex = 0; ///< The seat, and so an index into the eight commander colours
  std::uint8_t rankBadge = 0;
  /// 0 to 100, as Net's StructureState::buildPercent carries it - "which is what a client draws".
  /// 100 for anything not being built, so that a reader never has to ask which way round it is.
  std::uint8_t buildPercent = 100;
  bool selected = false;
  /// The factor the model is drawn at, 1 being its authored size. Content carries it per ROW rather
  /// than per model (ComponentDesc.h's modelScaleHundredths, "so that one model serves two rows at
  /// two sizes"), so a chassis, its drive and its modules can each want a different one and the
  /// instance is the only place that can say so. Converted here, like the position and the heading.
  float scale = 1.0f;
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
