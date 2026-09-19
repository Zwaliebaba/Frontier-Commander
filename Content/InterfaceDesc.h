#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// The look of the interface as data (Design/Interface.md §3; TechnicalDesign.md §8): the chrome
// palette every panel, button and bar is drawn in, and the eight commander colours a device wears
// (GameDesign.md §11). One file holds both because the same panels and the same minimap read them,
// and eight rows do not earn a second loader.
//
// THESE ARE 8-BIT RGBA AND NOT HUNDREDTHS, unlike BiomeDesc's renderer numbers. A biome's light is
// multiplied into a surface and needs headroom above one; a chrome colour is written to the
// framebuffer as authored, and a commander colour is drawn unlit by the ruling of GameDesign.md
// §11, so for both the authored byte is the byte that reaches the glass and a hundredth would only
// be a rounding step on the way.

namespace Frontier
{

/// One colour as the interface draws it.
struct Rgba8
{
  std::uint8_t red;
  std::uint8_t green;
  std::uint8_t blue;
  std::uint8_t alpha;

  [[nodiscard]] constexpr bool operator==(const Rgba8&) const noexcept = default;
};

/// Every colour Design/Interface.md §3 names, under that document's own names, so that a reader
/// with the document open finds each one where they expect it.
struct ChromePalette
{
  Rgba8 panelFill;
  Rgba8 panelBorder;
  Rgba8 panelTitleFrom;
  Rgba8 panelTitleTo;
  Rgba8 titleText;
  Rgba8 bodyText;
  Rgba8 dimText;
  Rgba8 accent;
  Rgba8 warning;
  Rgba8 buttonFill;
  Rgba8 buttonFillHover;
  Rgba8 buttonFillDown;
  Rgba8 buttonDisabled;
  Rgba8 barEmpty;
  Rgba8 barBuild;
  Rgba8 barHealth;
  Rgba8 barHealthLow;

  [[nodiscard]] constexpr bool operator==(const ChromePalette&) const noexcept = default;
};

inline constexpr std::size_t CHROME_ROLE_COUNT = 17;

/// A role's name beside the field it fills. The loader walks this table rather than seventeen
/// hand-written lines, which is what lets it say both "the role 'accent' is missing" and "there is
/// no role 'acccent'" — a palette that silently ignored a misspelled role would leave the panel
/// drawn in whatever the field defaulted to, and nobody would find out until they looked at it.
struct ChromeRole
{
  const char* name;
  Rgba8 ChromePalette::*member;
};

inline constexpr std::array<ChromeRole, CHROME_ROLE_COUNT> CHROME_ROLES = {{
  {"panelFill", &ChromePalette::panelFill},
  {"panelBorder", &ChromePalette::panelBorder},
  {"panelTitleFrom", &ChromePalette::panelTitleFrom},
  {"panelTitleTo", &ChromePalette::panelTitleTo},
  {"titleText", &ChromePalette::titleText},
  {"bodyText", &ChromePalette::bodyText},
  {"dimText", &ChromePalette::dimText},
  {"accent", &ChromePalette::accent},
  {"warning", &ChromePalette::warning},
  {"buttonFill", &ChromePalette::buttonFill},
  {"buttonFillHover", &ChromePalette::buttonFillHover},
  {"buttonFillDown", &ChromePalette::buttonFillDown},
  {"buttonDisabled", &ChromePalette::buttonDisabled},
  {"barEmpty", &ChromePalette::barEmpty},
  {"barBuild", &ChromePalette::barBuild},
  {"barHealth", &ChromePalette::barHealth},
  {"barHealthLow", &ChromePalette::barHealthLow},
}};

/// How many commanders a match holds, and so how many colours this file must carry in seat order.
/// SIM'S MAX_SEATS IS THE AUTHORITY and this repeats it, because Sim reads Content and Content may
/// not read Sim (AGENTS.md R9). Sim/Sim.cpp static_asserts that the two agree; it is the first
/// translation unit where both are visible, so a change to either is caught at the build rather
/// than by a seat drawn in whatever was left in the array.
inline constexpr std::size_t COMMANDER_COLOR_COUNT = 8;

struct InterfaceDesc
{
  ChromePalette chrome;
  std::array<Rgba8, COMMANDER_COLOR_COUNT> commanders;

  [[nodiscard]] constexpr bool operator==(const InterfaceDesc&) const noexcept = default;
};

} // namespace Frontier
