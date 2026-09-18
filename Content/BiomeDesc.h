#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// One biome as data (SpeciesLook.md §11; TechnicalDesign.md §8): the bitmaps the landscape and
// water are drawn from, the two directional lights, the fog and the sky. The renderer's numbers
// are the one place a content row holds a value the renderer will read as a float, and they are
// held here as hundredths so that Content names no float and the client converts once
// (AGENTS.md R16 covers Sim; this rule keeps Content's rows comparable and hashable).

namespace Frontier
{

/// A directional light of the Species model (SpeciesLook.md §2): the direction toward the light
/// and a colour with no upper bound, because values above one paint the saturated rims.
struct BiomeLight
{
  std::array<std::int32_t, 3> directionHundredths;
  std::array<std::int32_t, 3> colorHundredths;

  [[nodiscard]] bool operator==(const BiomeLight&) const noexcept = default;
};

/// The far a fog range may reach, in world units. It is a sanity bound on authored content and
/// deliberately names no size class: ADR-007 makes the range absolute, so 2,048 to 8,192 is the
/// range on every landscape and a value anywhere near this ceiling would defeat the decision. The
/// number is eighty times the authored end, which leaves room for a biome that wants a far longer
/// draw without leaving room for a typo.
inline constexpr std::int32_t MAX_FOG_WORLD_UNITS = 655360;

/// How the far field fades. The names are ADR-005's, and the client's FogMode reads them.
enum class FogMode : std::uint8_t
{
  LinearToColor,
  Desaturation
};

struct BiomeDesc
{
  std::string id;
  std::string name;
  std::string paletteTexture; ///< The landscape colour ramp, read on the CPU for the vertex colours
  std::string waterTexture;
  std::string waveTexture;
  BiomeLight key;
  BiomeLight sun;
  FogMode fogMode;
  /// THE FOG RANGE IS IN WORLD UNITS, NOT A FRACTION OF THE LANDSCAPE (ADR-007: 2,048 and 8,192).
  /// ADR-005 carried it as hundredths of the extent and the owner's frame of 2026-09-18 showed why
  /// that cannot work: the camera's distance scales with the landscape too, so the two never
  /// separate and a biome authored on a Small landscape fogs a Frontier one out of existence.
  std::int32_t fogStartWorldUnits;
  std::int32_t fogEndWorldUnits;
  /// The most of its own saturation a pixel may lose to distance, in hundredths (ADR-007: 35). The
  /// terminus FogMode::Desaturation does not otherwise have; FogMode::LinearToColor ignores it,
  /// because reaching the background is what that mode is for.
  std::int32_t fogMaxDesaturationHundredths;
  std::array<std::int32_t, 3> fogColorHundredths;
  /// Species has no sky colour at all: the sky is the clear colour, black, with additive layers
  /// over it (SpeciesLook.md §6), and zero here reproduces that exactly. The field exists so that
  /// a biome which is not the Garden can lift its background without a schema change.
  std::array<std::int32_t, 3> skyColorHundredths;

  // The cloud layers themselves are not here yet. They are camera-relative with their noise in
  // world space (OpenQuestions.md Q17, owner 2026-09-18), and m2-skirmish/T8 adds a row per layer
  // — height, world-space repeat period, colour — plus the drift, with the numbers it measures.
  // That is a version bump of this file, which ADR-006 already provides for.

  [[nodiscard]] bool operator==(const BiomeDesc&) const noexcept = default;
};

} // namespace Frontier
