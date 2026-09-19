#pragma once

#include "AiBlackboard.h"
#include "Device.h"

#include <cstdint>

// What a scripted commander builds (TechnicalDesign.md §7, m1-vertical-slice/S12). A design is a
// chassis, a drive and modules, and the question is which combination to spend power on. The answer
// here is the one the acceptance asks for: **expected damage per power against the enemy
// composition this commander has actually seen**, through the same damage matrix the shooting uses.
//
// IT IS NOT A SEARCH. Every combination of the tables is a few hundred at M1's size and the
// evaluation is a handful of integer multiplies, so the designer walks all of them. When the tables
// grow past that, this is where a pruning step goes - not a cleverer scoring rule.

namespace Frontier
{

class Sim;

/// What a design is FOR. The behaviour list asks for one of each and produces to a composition.
enum class AiRole : std::uint8_t
{
  Builder, ///< Carries a builder module; what puts structures up
  Fighter  ///< Carries a weapon; what the attack behaviour sends
};

/// The best design this commander can build for the role, or false when the tables offer none he
/// has unlocked. Deterministic: ties break on the row indices, so two hosts choose the same design.
[[nodiscard]] bool BestDesign(const Sim& _sim, std::uint8_t _seat, AiRole _role, const AiBlackboard& _blackboard, DeviceDesign& _out);

/// What one hit of this weapon is worth against the composition the blackboard has seen, as damage
/// weighted by how much of that composition each column is. With nothing seen yet it is the mean
/// over every column, which is what "design for what you might meet" comes to.
[[nodiscard]] std::int32_t ExpectedDamage(const ContentTree& _content, const ModuleDesc& _weapon, const AiBlackboard& _blackboard) noexcept;

} // namespace Frontier
