#pragma once

#include <array>

namespace Neuron
{

/// A directional light of the Species model (SpeciesLook.md §2): the direction toward the light,
/// normalized, and a colour with no upper bound, because values above one are what paint the
/// saturated rims. Lambert only, no ambient, two of these summed and then clamped, one normal per
/// triangle (TechnicalDesign.md §6.4).
struct DirectionalLight
{
  std::array<float, 3> direction;
  std::array<float, 3> color;
};

struct SceneLighting
{
  DirectionalLight key;
  DirectionalLight sun;
};

/// The Garden's pair until Content\Biomes.json carries one per biome (SpeciesLook.md §11): a near
/// white key at 23 degrees and a horizontal orange sun at three and a half times white.
inline constexpr SceneLighting GARDEN_LIGHTING = {{{0.04f, 0.39f, -0.92f}, {1.06f, 0.96f, 0.72f}}, {{0.57f, 0.0f, -0.82f}, {3.58f, 0.79f, 0.14f}}};

/// THE UNLIT TEAM-COLOUR SLOT (TechnicalDesign.md §6.4; OpenQuestions.md R4): a vertex whose colour
/// carries this alpha is a team-colour slot, and the pixel shader writes its colour as it is, so
/// that no sun tints a commander's colour. Every other vertex carries VERTEX_ALPHA_LIT. Specified
/// here for the geometry pass of M1 (m1-vertical-slice/K1) to implement; the terrain never uses
/// it, and the terrain shader honours it already.
inline constexpr float VERTEX_ALPHA_UNLIT = 0.0f;
inline constexpr float VERTEX_ALPHA_LIT = 1.0f;

/// How the far field fades (SpeciesLook.md §5; the fog-and-lighting ADR): the Species fog scaled
/// to the landscape, or distance desaturation.
enum class FogMode : std::uint32_t
{
  LinearToColor,
  Desaturation
};

/// The Species fog range as a fraction of the landscape's extent: 1,000 to 4,000 units on maps
/// about 5,400 across.
inline constexpr float FOG_START_FRACTION = 0.185f;
inline constexpr float FOG_END_FRACTION = 0.74f;

} // namespace Neuron
