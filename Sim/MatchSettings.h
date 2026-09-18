#pragma once

#include "LandscapeDefinition.h"

#include <array>
#include <cstdint>

// The lobby's values, fixed before the first tick and carried whole by every snapshot and replay
// (GameDesign.md §2 and §3; TechnicalDesign.md §4.9). Plain aggregates (AGENTS.md R8): nothing
// here changes during a match, and the simulation reads them and never writes them.

namespace Frontier
{

inline constexpr std::uint8_t MIN_SEATS = 2;
inline constexpr std::uint8_t MAX_SEATS = 8;

/// An alliance number no seat has: the answer when nobody has won.
inline constexpr std::uint8_t NO_ALLIANCE = 0xFF;

// SizeClass and SIZE_CLASS_CELLS are Content's (LandscapeDefinition.h): the landscape is defined below the lobby.

/// What a commander starts with (GameDesign.md §2).
enum class BaseLevel : std::uint8_t
{
  Nothing,
  Small,
  Established
};

/// The starting stockpile: 400, 1,000 or 2,500 power, held in hundredths (ImplementationPlan.md §6).
enum class PowerLevel : std::uint8_t
{
  Low,
  Medium,
  High
};

inline constexpr std::array<std::int32_t, 3> STARTING_POWER_HUNDREDTHS = {40000, 100000, 250000};

/// The most devices a commander may field at once: 100, 200 or 300 (GameDesign.md §4). It is a
/// lobby setting because the cap is the brake on an army, and what that brake should be is the
/// question a match is set up to ask.
enum class DeviceCapLevel : std::uint8_t
{
  Low,
  Medium,
  High
};

inline constexpr std::array<std::uint32_t, 3> DEVICE_CAPS = {100, 200, 300};

/// The structure cap is not a lobby setting: GameDesign.md §4 gives one number for every match,
/// and a commander who wants more structures is answered by the stockpile cap rather than by this.
inline constexpr std::uint32_t STRUCTURE_CAP = 300;

enum class VictoryCondition : std::uint8_t
{
  Annihilation,
  Dominance,
  Survival
};

/// Who sits in a seat. An empty seat takes no part: its orders are dropped and it is never a winner.
enum class SeatKind : std::uint8_t
{
  Empty,
  Human,
  Ai
};

struct SeatSettings
{
  SeatKind kind;
  std::uint8_t alliance; ///< Seats sharing a number share vision and victory (GameDesign.md §2).

  [[nodiscard]] constexpr bool operator==(const SeatSettings&) const noexcept = default;
};

struct MatchSettings
{
  std::uint64_t seed;
  SizeClass sizeClass;
  std::uint8_t seatCount; ///< MIN_SEATS to MAX_SEATS; seats [0, seatCount) are the commanders.
  BaseLevel baseLevel;
  PowerLevel powerLevel;
  std::uint8_t technologyTiers; ///< Research tiers pre-completed: 0, 1 or 2.
  VictoryCondition victory;
  std::uint32_t survivalTicks;   ///< The clock of the Survival condition, in ticks; unread otherwise.
  DeviceCapLevel deviceCapLevel; ///< Indexes DEVICE_CAPS; the structure cap is STRUCTURE_CAP for every match.
  std::array<SeatSettings, MAX_SEATS> seats;

  [[nodiscard]] constexpr bool operator==(const MatchSettings&) const noexcept = default;
};

} // namespace Frontier
