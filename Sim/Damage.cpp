#include "pch.h"

#include "Damage.h"
#include "Construction.h"
#include "Design.h"
#include "Experience.h"
#include "Sim.h"
#include "Targeting.h"

#include <algorithm>
#include <vector>

namespace Frontier
{

namespace
{

/// Who struck the blow that finished something off, so that the kill credits one shooter and not
/// whichever of the tick's hits the walk happened to reach last.
struct Kill
{
  ObjectId victim;
  ObjectId killer;
  std::uint8_t seat;
};

} // namespace

void ResolveDamage(Sim& _sim)
{
  World& world = _sim.Objects();
  std::vector<Kill> killed;

  for (const DamageEvent& hit : _sim.Damage())
  {
    if (hit.points <= 0)
    {
      continue;
    }
    if (Device* device = world.FindDevice(hit.target); device != nullptr)
    {
      const bool wasAlive = device->hitPoints > 0;
      device->hitPoints -= hit.points;
      // Return fire answers what shot at it, and this is where it learns who that was: the stance
      // never goes looking for a fight, so without this line it never has one (GameDesign.md §8).
      if (device->fire == FireStance::ReturnFire && device->target == NO_OBJECT && hit.shooter != NO_OBJECT)
      {
        device->target = hit.shooter;
      }
      if (wasAlive && device->hitPoints <= 0)
      {
        killed.push_back({hit.target, hit.shooter, hit.seat});
      }
      continue;
    }
    if (Structure* structure = world.FindStructure(hit.target); structure != nullptr)
    {
      const bool wasAlive = structure->hitPoints > 0;
      structure->hitPoints -= hit.points;
      if (wasAlive && structure->hitPoints <= 0)
      {
        killed.push_back({hit.target, hit.shooter, hit.seat});
      }
    }
  }
  _sim.Damage().clear();

  for (const Kill& kill : killed)
  {
    // What it was worth has to be read before it is gone, and so does where it stood.
    TargetPoint victim{};
    const bool known = TargetAt(_sim, kill.victim, victim);

    if (Device* device = world.FindDevice(kill.victim); device != nullptr)
    {
      Wreck wreck{};
      wreck.seat = device->seat;
      wreck.origin = kill.victim;
      // A device's design is its commander's slot and a structure's is a content row; the origin's
      // kind is what tells a reader which, and Sim/Wreck.h's field carries whichever it was.
      wreck.design = device->design;
      wreck.x = device->x;
      wreck.y = device->y;
      wreck.z = device->z;
      wreck.facing = device->facing;
      wreck.decayTicks = WRECK_DECAY_TICKS;
      (void)world.Create(wreck);
      // The duty m1-vertical-slice/S8 left here in writing: a planner job outlives its device and
      // goes on taking budget from the ones still alive (Sim/Movement.h).
      _sim.Planner().Cancel(kill.victim);
      (void)world.Remove(kill.victim);
    }
    else if (world.FindStructure(kill.victim) != nullptr)
    {
      DestroyStructure(_sim, kill.victim); // Leaves the wreck and clears the obstruction (S4)
    }

    // The killer ranks up, if it is a device and it is still alive to enjoy it. A structure holds
    // no experience: GameDesign.md §8 gives ranks to devices.
    if (!known)
    {
      continue;
    }
    if (Device* killer = world.FindDevice(kill.killer); killer != nullptr && killer->seat == kill.seat)
    {
      killer->experience += ExperienceForKill(victim.costHundredths);
    }
  }

  AdvanceRetreat(_sim);
  AdvanceWrecks(world);
}

} // namespace Frontier
