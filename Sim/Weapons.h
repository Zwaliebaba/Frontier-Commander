#pragma once

#include "Device.h"
#include "ObjectId.h"
#include "Structure.h"

#include "ComponentDesc.h"
#include "ContentTree.h"
#include "DesignStats.h"

#include <array>
#include <cstdint>

// What a weapon is and what it does in a tick (GameDesign.md §8; m1-vertical-slice/S10). The
// numbers are the module row's; what this adds is the rank and the research that modify them, so
// that stage 8 asks one question and gets the answer the design screen would show.
//
// A SHOOTER IS A DEVICE OR A STRUCTURE, and the difference is only where its weapons come from: a
// device's are the modules of its design, a structure's is the one module its row names (a Tower,
// a Bunker). Everything after that - range, reload, the roll, the damage - is the same, which is
// why it is written once here rather than twice in the two walks of stage 8.
//
// RELOAD IS ON THE RECORD AND THE RATE IS DERIVED. Device::reloadTicks counts down per mount and
// Structure::reloadTicks does the same for the one weapon a structure carries - a field of its own
// rather than a second use of workRemainingTicks, which is a factory's production countdown and a
// builder's, and which a mod that put a weapon on a factory would then have to share. What a
// research upgrade changes is how far the counter is set back, so an upgrade reaches a device
// already in the field on its next shot without anything walking the world (Sim/Design.h's rule).

namespace Frontier
{

class Sim;

/// How fast a shell flies, in subunits a tick. The design gives ranges and reloads and no speed,
/// so this is S10's: 48 world units a tick is 960 a second, which puts a mortar's longest shot
/// (28 cells) just under two seconds in the air. That is the number the "walks out from under it"
/// rule is made of - a scout covers three cells in that time and a mortar's splash is two - so it
/// is a game rule and not a presentation detail, and it lives beside the range it is measured with.
inline constexpr std::int32_t PROJECTILE_SPEED_SUBUNITS_PER_TICK = 12288;

/// One weapon a shooter carries: the row it is and the mount it sits in, which is the index its
/// reload counter has on the record.
struct WeaponMount
{
  std::uint32_t module; ///< Row index in the module table
  std::uint8_t mount;   ///< Which of the chassis's mounts, and which reload counter

  [[nodiscard]] constexpr bool operator==(const WeaponMount&) const noexcept = default;
};

/// The weapons of a device's design, in mount order; returns how many were found. A module that is
/// a system rather than a weapon takes a mount and no entry here, so the mount indices are the
/// record's and not a second numbering.
[[nodiscard]] std::uint8_t WeaponsOf(const ContentTree& _content, const DeviceDesign& _design, std::array<WeaponMount, MAX_MOUNTS>& _out);

/// The one weapon a structure's row names, if it names one and the row exists.
[[nodiscard]] bool WeaponOf(const ContentTree& _content, const Structure& _structure, WeaponMount& _out);

/// The chance a shot hits, as a percentage in [0, 100]: the row's chance at this range, plus what
/// the shooter's rank adds (Sim/Design.h) and what research has added to the weapon's class.
[[nodiscard]] std::int32_t HitPercent(const ModuleDesc& _module, const ClassUpgrades& _upgrades, std::uint8_t _rank,
                                      bool _longRange) noexcept;

/// The damage a shot carries before the matrix and the armour: the row's, plus the rank's and the
/// class upgrade's percentages.
[[nodiscard]] std::int32_t WeaponDamage(const ModuleDesc& _module, const ClassUpgrades& _upgrades, std::uint8_t _rank) noexcept;

/// Ticks between salvos: the row's, shortened by what research has added to the class's rate.
/// Never nought, because a weapon that reloads in no time fires every tick for ever.
[[nodiscard]] std::uint32_t ReloadTicks(const ModuleDesc& _module, const ClassUpgrades& _upgrades) noexcept;

/// Stage 9 of the tick (TechnicalDesign.md §4.8): every shell in flight advances, and one that
/// lands queues its splash. Indirect fire is decided HERE and not at the shot, which is the whole
/// of "a moving target walks out from under it" (GameDesign.md §8).
void AdvanceProjectiles(Sim& _sim);

} // namespace Frontier
