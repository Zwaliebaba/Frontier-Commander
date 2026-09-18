#pragma once

#include <array>
#include <cstdint>

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
inline constexpr SceneLighting GARDEN_LIGHTING = {{{0.04f, 0.39f, -0.92f}, {1.06f, 0.96f, 0.72f}},
                                                  {{0.57f, 0.0f, -0.82f}, {3.58f, 0.79f, 0.14f}}};

/// THE UNLIT TEAM-COLOUR SLOT (ADR-005; TechnicalDesign.md §6.4; OpenQuestions.md R4): a vertex
/// whose colour carries this alpha is a team-colour slot, and the pixel shader writes its colour
/// as it is, neither lit nor fogged, so that no sun tints a commander's colour and no distance
/// greys it. Every other vertex carries VERTEX_ALPHA_LIT. Specified here for the geometry pass of
/// M1 (m1-vertical-slice/K1) to implement; the terrain never uses it, and the terrain shader
/// honours it already, as the reference.
inline constexpr float VERTEX_ALPHA_UNLIT = 0.0f;
inline constexpr float VERTEX_ALPHA_LIT = 1.0f;

/// How the far field fades (SpeciesLook.md §5; ADR-005): the Species fog scaled to the landscape,
/// or distance desaturation. Both stay in the shaders, because the capture draws both for the
/// comparison the ADR rests on; the game draws DEFAULT_FOG_MODE.
enum class FogMode : std::uint32_t
{
  LinearToColor,
  Desaturation
};

/// ADR-005's choice, until the owner confirms or overrides it (m0-foundation/T22).
inline constexpr FogMode DEFAULT_FOG_MODE = FogMode::Desaturation;

/// THE FOG RANGE IS ABSOLUTE (ADR-007): one chunk width to four of them, in world units, and not a
/// fraction of the landscape's extent. ADR-005 scaled it with the landscape and the owner's frame
/// showed why that cannot work: the camera's distance scales with the landscape too, because an RTS
/// player's zoom is set by how much map they want on screen, so the two never separate and framing a
/// landscape puts all of it past the far end. 63.9% of that frame's landscape measured exactly gray.
inline constexpr float FOG_START_WORLD_UNITS = 2048.0f;
inline constexpr float FOG_FULL_WORLD_UNITS = 8192.0f;
static_assert(FOG_START_WORLD_UNITS < FOG_FULL_WORLD_UNITS);

/// The most of its own saturation a pixel may lose to distance, and the terminus the desaturation
/// does not otherwise have (ADR-007). Fading to the background is self-limiting: at full strength
/// the surface is the background and is never seen. Desaturation at full strength leaves the terrain
/// in full detail with its colour gone, which deletes the saturated rim the horizontal sun of
/// SpeciesLook.md §2 exists to paint. FogMode::LinearToColor takes no ceiling and still reaches one,
/// because reaching the background is what it is for and the capture's comparison rests on it.
inline constexpr float FOG_MAX_DESATURATION = 0.35f;
static_assert(FOG_MAX_DESATURATION > 0.0f && FOG_MAX_DESATURATION <= 1.0f);

} // namespace Neuron
