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
  /// The fog range as a fraction of the landscape's extent, in hundredths (ADR-005: 19 and 74).
  std::int32_t fogStartExtentHundredths;
  std::int32_t fogEndExtentHundredths;
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
