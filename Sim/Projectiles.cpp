#include "pch.h"

#include "Weapons.h"

// Stage 9 of the tick (TechnicalDesign.md §4.8) has no code of its own. AdvanceProjectiles and the
// splash it resolves are in Sim/Weapons.cpp, beside the Fire that launches a shell and the numbers
// the shell carries: a shot's flight and its landing are two halves of one rule, and splitting them
// across two translation units would put the launch and the impact out of each other's sight.
//
// The file exists because m1-vertical-slice/S10 names it and because a reader who goes looking for
// the projectile stage should find this note rather than nothing.
