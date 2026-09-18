#pragma once

#include "Device.h"
#include "ObjectId.h"

#include "ContentTree.h"
#include "DesignStats.h"

#include <array>
#include <cstdint>
#include <string_view>

// What a commander designs and what a factory is building (GameDesign.md §6; m1-vertical-slice/S5).
//
// TWO SHAPES OF ONE IDEA, AND WHY BOTH EXIST. Content's DesignRecipe names its parts by id, because
// it is what the design screen commits and what is saved between matches (TechnicalDesign.md §9);
// Sim's DeviceDesign names them by row index, because a design is hashed and replicated every tick
// and a string is neither cheap nor fixed-layout. RecipeFor is the one conversion between them, so
// that the simulation derives its statistics from Content/DesignStats.cpp and not from a second
// implementation that can disagree with the design screen.
//
// NOTHING DERIVED IS STORED ON A DEVICE, and that is Device.h's rule rather than this task's
// preference. The task's acceptance says the statistics are "stored on the device at spawn", and
// its own next line asks that "an upgrade researched later changes fielded devices"; a stored
// maximum satisfies the first and breaks the second. What is stored is the CURRENT hit points,
// because damage is state, and what is derived is everything else - the maximum included. A
// retroactive upgrade then changes what a device can hold without walking the world, and S6's
// "hit points keeping their fraction" is the one rescale it does have to do.

namespace Frontier
{

struct Seat;

/// One thing a factory is building or is queued to build. It hangs off the SEAT and not off the
/// structure (Sim/Structure.h says so) because a queue is a list and a structure is a fixed-layout
/// record that the hash and the snapshot walk field by field.
struct ProductionEntry
{
  ObjectId factory;
  std::uint32_t design;    ///< Index into the seat's designs
  std::uint32_t remaining; ///< How many of it are still to build, including the one in progress

  [[nodiscard]] constexpr bool operator==(const ProductionEntry&) const noexcept = default;
};

/// What one commander may have queued across every factory, and how many of one design one order
/// may ask for. Bounds rather than rules: they are what keeps a snapshot's size a function of the
/// game's caps rather than of how long somebody held a key down.
inline constexpr std::uint32_t MAX_PRODUCTION_ENTRIES = 64;
inline constexpr std::uint32_t MAX_PRODUCTION_REPEAT = 100;

// ── Ranks (GameDesign.md §8) ──────────────────────────────────────────────────────────────────
//
// "A device that destroys things gains experience through eight ranks, each adding a small
// percentage to accuracy and damage." The design says eight and says "small" and gives no numbers,
// so THE NUMBERS BELOW ARE THIS TASK'S PROPOSAL and OpenQuestions.md carries them for the owner.
// The shape is the argument: the thresholds double, so a rank costs as much as every rank before
// it put together and the eighth is a thing a commander protects rather than a thing that happens;
// the percentages grow to 24, which is worth retreating and repairing for (GameDesign.md §8) and
// is well under the 60 a class upgrade can reach, so a veteran is an edge and not a second tier.

inline constexpr std::uint8_t RANK_COUNT = 8;

/// The experience each rank begins at. Experience is kills weighted by GameDesign.md §8, which S10
/// awards; a device starts at rank 0 with none.
inline constexpr std::array<std::uint32_t, RANK_COUNT> RANK_EXPERIENCE = {0, 2, 5, 10, 20, 40, 80, 160};

/// What a rank adds to a device's hit chance and to its damage, as percentages.
inline constexpr std::array<std::int32_t, RANK_COUNT> RANK_ACCURACY_PERCENT = {0, 2, 4, 7, 10, 14, 18, 24};
inline constexpr std::array<std::int32_t, RANK_COUNT> RANK_DAMAGE_PERCENT = {0, 2, 4, 7, 10, 14, 18, 24};

/// The rank an experience total has reached, 0 to RANK_COUNT - 1.
[[nodiscard]] constexpr std::uint8_t RankOf(std::uint32_t _experience) noexcept
{
  std::uint8_t rank = 0;
  for (std::uint8_t index = 1; index < RANK_COUNT; ++index)
  {
    if (_experience >= RANK_EXPERIENCE[index])
    {
      rank = index;
    }
  }
  return rank;
}

/// The id-named form of a design the seat holds, for Content/DesignStats.cpp. An index that names
/// no row becomes an empty id, which DeriveDesignStats refuses by name rather than by subscript.
[[nodiscard]] DesignRecipe RecipeFor(const ContentTree& _content, const DeviceDesign& _design);

/// Whether the seat has completed the research a row's unlockedBy names. An empty id is a row
/// available from the first tick; an id no research row defines is unreachable rather than free,
/// because a table naming a prerequisite it does not define is a content fault and ContentValidator
/// is what reports it. Here rather than beside one caller, because placement, the design screen and
/// the module builder all ask the same question of different tables.
[[nodiscard]] bool UnlockedFor(const Seat& _seat, const ContentTree& _content, std::string_view _unlockedBy);

/// The byte a SaveDesign order writes in a module slot it is not using (Sim/Order.h's table). It
/// is 0xFF rather than 0 because 0 is a real row, and an order that leaves a slot empty must not
/// read as one that mounted the first module in the table.
inline constexpr std::uint8_t NO_PACKED_MODULE = 0xFFu;

/// The design a SaveDesign order names. The modules are one per byte of the fourth operand, low
/// byte first, and the count is how many come before the first empty slot.
[[nodiscard]] DeviceDesign DesignFromOrder(std::int32_t _chassis, std::int32_t _drive, std::int32_t _packedModules) noexcept;

} // namespace Frontier
