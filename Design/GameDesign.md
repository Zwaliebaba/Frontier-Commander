# Frontier Commander — Game Design

**Status: DESIGN (accepted by the owner on 2026-09-17, after the questions in [`OpenQuestions.md`](OpenQuestions.md) were answered; revised through ADRs; revised again on 2026-09-17 after an external review, whose six questions the owner answered the same day).** Every number in this document is a proposed default for tuning to start from, not a measurement, and every one of them is expected to move once the vertical slice runs. Decisions carry the date they were taken.

---

## 1. Vision

*Frontier Commander* is a real-time strategy game of base building, research and designed war machines, fought on a large open landscape against AI commanders and other players. Its rules descend from *Warzone 2100*: power comes out of the ground, factories turn it into units, a research tree unlocks the parts those units are made of, and the player designs the units rather than picking them from a list. Its look descends from *Species*: flat-shaded, vertex-coloured geometry on a fractal landscape coloured by its own height and slope, seen from a free-flying camera through the fiction of an operator at a terminal.

**Five pillars.** When two sections of this document disagree, the earlier pillar wins.

1. **You design the army.** A unit — a *device* — is a chassis, a drive and a module the player chose. Research does not hand out units; it hands out parts. The interesting decisions are in the design screen, and the component tables are where the depth of the game lives.
2. **The land is the game.** The landscape is large, continuous and uneven. Height gives sight and range, slope costs movement, water blocks all but hover, and power deposits are spread thinly enough that a base which does not expand starves. Distance is a resource.
3. **Readable at a glance.** No texture detail, no lighting tricks: flat colour, hard silhouettes, one colour per commander that no light may tint, and a landscape whose colour tells you its height and slope — the vertex mottling of the low ground is part of that reading, not noise (owner, 2026-09-17). A player must be able to read a fight from the far end of the map, and where the look and this pillar disagree, this pillar wins.
4. **The simulation is the referee.** The same orders produce the same game on every machine, every time. Multiplayer, replays, saved games and bug reports all rest on that one property, and no feature is worth losing it.
5. **Data over code.** Every component, structure, research item and tuning value is a row in a table, not a constant in a function. Adding a weapon is adding a row and a model. That is what "moddable" means in the first place, and it is what makes balance a spreadsheet problem rather than a code problem.

**What it is not.** It is not a persistent world with colonies that live while their player is away — that is the ambition of the Species repository, not of this game, which is match-based with a long-running host reachable later (owner, 2026-09-17; §10). It is not a story-driven campaign, at least not before the skirmish game is complete (§12). It is not a physics sandbox: the simulation is integer, tick-based and deliberately simple.

---

## 2. The player and the session

**The player is a commander.** They see the landscape from a free camera, own a base or several, and give orders to structures and devices. They never control a unit directly; every action is an order the simulation carries out. The fiction, inherited from Species, is that the player sits at a terminal: the interface is a console, not a cockpit.

**The session is a match.** A match is one landscape, two to eight commanders — humans or AI in any mix — and a victory condition. It starts from a chosen base level (nothing but a builder and a command post; a small base; an established base), a power level and a technology level, all set in the lobby, and it ends when the condition is met or the players agree it has. Expected length is 30 minutes on a small landscape and two hours or more on a large one, which is long enough that a match must be **pausable, savable and resumable** by the host and **rejoinable** by a player who dropped (§10).

**Victory conditions**, chosen per match:

| Condition | Ends the match when | For |
|---|---|---|
| Annihilation | Every enemy structure and every enemy builder is destroyed | The default, and the *Warzone 2100* rule |
| Dominance | One side has held — an extractor built and served (§4) — at least 60% of the landscape's deposits for 10 continuous minutes | Large landscapes, where hunting the last builder across a landscape sixteen Gardens wide is not a game; it ships with the first Large landscapes (§12) |
| Survival | The clock runs out; the side that extracted the most power wins | Short matches and AI stress tests |

Alliances are fixed in the lobby; allied commanders share vision and victory and cannot attack each other.

**Base, power and technology levels** are lobby settings with values. Base level: *nothing* is a builder and a command post; *small* adds two served extractors, a generator and a factory; *established* adds a lab, a repair bay and four hardpoints. Power level sets the starting stockpile: 400, 1,000 or 2,500. Technology level pre-completes the first zero, one or two tiers of the research tree.

---

## 3. The landscape

**The landscape is a heightfield on a grid of cells, and it is large.** The world unit is the Species unit, so that the Species models import at their native scale: a soldier is 14 units tall, a tank body 49 long, a wall segment 46 wide, a power station 82 by 103 (`SpeciesLineage.md` §4 has the measurements). One cell is 64 world units on a side, which is one tank or one wall segment; the first draft of this document had a cell at 4 units, and the art review corrected it. Sizes, as proposed defaults, halved on 2026-09-17 after the review's crossing-time arithmetic:

| Class | Cells per side | World units per side | Deposits | Commanders |
|---|---|---|---|---|
| Small | 128 | 8,192 | 12 | 2 |
| Medium | 256 | 16,384 | 24 | 2–4 |
| Large | 512 | 32,768 | 60 | 4–8 |
| Frontier | 1,024 | 65,536 | 160 | 4–8 |

For scale: the largest Species map, the Garden, is 2,002 world units across, which is 31 of these cells, so a Small landscape is four Gardens across and a Frontier one thirty-three; *Warzone 2100* maps run up to 250 tiles a side, and a tile is one tank, the same as the cell proposed here, so a Medium landscape is a *Warzone* map, Large is four times its area and Frontier sixteen. **Distance is the point of the game and its biggest risk.** With the speeds of §6 a light device crosses a Small landscape in 1.3 minutes and a heavy in 4.7; a Large one in 5.3 and 19. Whether that is decisions or dead time is the premise everything in `TechnicalDesign.md` §4 is sized by, and the test that decides it costs an evening and no engine: *Warzone 2100*, whose stats are files, with its unit speeds and power numbers set to this game's, played for thirty minutes against a stock AI, logging the time to first contact and the share of a unit's life spent in transit. Small targets four to six minutes to first contact and under two minutes for a light device to cross (owner, 2026-09-17), which the halved sizes meet on paper; the test is run before M1, and its numbers replace these.

**Terrain has three properties the simulation reads:** height, slope and water. Height sets sight (§8) and, for indirect fire, range. Slope is the gradient between neighbouring cells; each drive class has a maximum it can climb (§6), and cliffs are slopes nothing climbs. Water is any cell below the water level — the Species `outsideHeight` — and only hover and lift drives cross it. Nothing else about the terrain is simulated: no soil types, no destruction, no terraforming, and that holds through M3 (owner, 2026-09-17); the delta format in `TechnicalDesign.md` §4.4 is chosen so it can change later.

**Deposits** are point features the landscape generator scatters with a minimum spacing: a modest cluster at each start, and the largest clusters midway between starts, so that expansion pulls commanders toward each other rather than away. An extractor is built on a deposit and nowhere else. Deposits are the only thing on the landscape worth fighting over that cannot be moved, and that is what gives the landscape its shape as a game.

**Features** are scenery the simulation treats as obstacles: rock, ruins, the Species temples and caves. They block movement and line of sight; they do not take damage in the first version.

**Landscapes are generated from a seed and optionally stamped.** The generator is the Species diamond-square landscape — tiles of fractal terrain with a fractal dimension, a height scale and a desired height each, merged and smoothed — ported to integer arithmetic so every machine generates the same heights (`TechnicalDesign.md` §4.4). A landscape definition is therefore a seed, a size class, a tile list and a palette, and it is a few hundred bytes. Authored content enters as **stamps**: a start base, a ruin, a chokepoint, each an authored patch of heights and features that the generator places at a chosen or a seeded position. There are no hand-authored whole maps (owner, 2026-09-17): a stamp library plus a seed does what a hand-authored map does at a fraction of the content cost, and a one-developer project has no content budget to spend.

**What the generator guarantees**, measured by the landscape tool of `TechnicalDesign.md` §4.4 over a hundred seeds before a size class ships: every start is on flat ground with room for the established base; every pair of starts is connected for wheels, tracks and hover alike; every deposit is on land and reachable by a builder; and at least seven tenths of the land is passable to tracks. A seed that fails is rejected and the next tried, which is what makes the guarantee cheap. How a seed becomes a tile list — how many tiles, of what sizes, fractal dimensions and heights, per size class — is the generator's recipe, tuned against those measurements, and it is the first deliverable of M0's landscape task.

**Fog of war** has three states per cell per commander: unexplored (black), explored (the terrain and the last-seen structures, dimmed) and visible (live). Vision comes from devices and structures with a sight radius, extended by height: a sensor on a hill sees further than the same sensor in a valley, and a ridge between the sensor and the target blocks it. The rules and their cost are in §8 and `TechnicalDesign.md` §4.6.

---

## 4. Economy

**One currency: power.** Everything — structures, devices, research, repair — costs power, and power comes from the ground. There is no second resource and no population cap; the limits on an army are power, factory throughput and the commander's attention.

**The chain is deposit → extractor → generator.** An extractor on a deposit produces nothing until a generator serves it; a generator serves the four nearest unserved extractors within 48 cells of it, and an extractor beyond every generator's reach produces nothing. That radius is what keeps generators at the front rather than in the core: a commander who expands carries generators outward with the extractors, and a raid on a forward generator stops four extractors at once — the generator is the target and the extractors are the bait, as in *Warzone 2100*, at this game's distances. A command post produces a trickle so a commander who has lost everything can rebuild an extractor.

**Proposed starting numbers**, for tuning to begin from:

| Source | Power per second |
|---|---|
| Extractor, served | 5 |
| Extractor, unserved | 0 |
| Command post | 1 |

| Cost | Power | Build time (s) |
|---|---|---|
| Command post | 500 | 60 |
| Extractor | 50 | 15 |
| Generator | 250 | 40 |
| Factory | 400 | 60 |
| Research lab | 300 | 45 |
| Repair bay | 300 | 45 |
| Sensor tower | 100 | 20 |
| Wall, per segment | 25 | 5 |
| Hardpoint | 200 | 30 |
| Light device | 100–250 | 15–30 |
| Medium device | 250–500 | 30–50 |
| Heavy device | 500–900 | 50–90 |
| Research item | 50–2,000 | 30–600 |

**Power is a stockpile with a cap, and the cap cannot be parked around.** Income accumulates into a per-commander stockpile capped at 1,000 plus 500 per generator, so a turtled base cannot bank an unlimited army and a raided one loses something real. Cost is drawn when construction or production begins, not when a plan is placed; cancelling refunds the share of the cost not yet built, and any refund that would exceed the cap is lost — so power cannot be stored in unfinished builds. Cancelled research refunds nothing: the power was spent when it started.

**Armies are bounded.** A commander may field at most 200 devices and 300 structures at once (a lobby setting: 100, 200 or 300 devices), and a factory whose commander is at the cap pauses. Eight commanders make 1,600 devices and 2,400 structures, inside the simulation budget `TechnicalDesign.md` §3 sizes for. There is no upkeep: the cap and the factory count are the brake, and the cap is what an AI that turtles runs into.

**Repair and demolition.** Repair costs power at a fraction of the build cost per hit point restored. Demolishing a structure refunds half its cost; a destroyed one refunds nothing.

---

## 5. Base building

**Structures are placed by the commander and built by builders.** The commander chooses a structure and a place; a builder device (§6) drives there and constructs it over the build time, and several builders shorten it. A structure under construction has hit points proportional to its progress and can be destroyed. Nothing is built instantly, including at the start of the match — the base level in the lobby decides what is already standing when the match begins.

**Placement rules.** A structure has a rectangular footprint in cells. It may be placed where the footprint holds no other structure, no feature and no water, and where the slope across the footprint is under 25%; when construction begins, the terrain under the footprint is flattened to its mean height, which is the Species flatten-under-buildings mechanic and the one terrain modification the game makes. There is no build radius: a structure may be placed anywhere the commander has explored, which is what makes forward bases and deposit-grabbing possible. A placed plan costs nothing and obstructs nothing until a builder begins it, and a commander may have at most 64 plans waiting.

**The structure catalogue** for the complete skirmish game (§12, M2). Footprints are in cells; strength is the class the damage model reads (§8).

| Structure | Footprint | Strength | Role |
|---|---|---|---|
| Command post | 3×3 | Hard | Unlocks the design screen; produces a trickle of power; a commander with no command post keeps the minimap and can still build one |
| Extractor | 1×1 | Soft | Produces power on a deposit when a generator within 48 cells serves it |
| Generator | 2×2 | Medium | Serves four extractors; takes one module, which adds two more |
| Factory | 3×3 | Medium | Builds devices from designs; takes up to two modules, each shortening build time |
| Research lab | 2×2 | Medium | Researches one item at a time; takes one module, shortening research time |
| Repair bay | 2×2 | Medium | Repairs devices that come to it; devices with a retreat threshold (§8) return here |
| Sensor tower | 1×1 | Soft | Sight 40 cells; spots for indirect-fire devices in range |
| Wall | 1×1 | Hard | Blocks movement and direct fire; joins to neighbouring walls |
| Hardpoint | 1×1 | Hard | A wall segment carrying a weapon module chosen from the researched set |
| Tower | 1×1 | Medium | A light weapon module on a tall mount with a sight bonus |
| Bunker | 2×2 | Bunker | An anti-personnel and anti-light weapon in a fortified housing |
| Uplink | 2×2 | Medium | Shows every structure on the landscape as a ghost and marks the clusters holding enemy devices, refreshed every ten seconds; grants no sight of devices; late research |

Air units are not before M4 (owner, 2026-09-17); they would add a lift-drive factory and a rearm pad.

**Modules** are upgrades built onto a standing structure by a builder, and they are researched like anything else. They are how a base grows without growing its footprint: a factory module shortens its factory's build times by 25% (two: 50%), a lab module shortens its lab's research by 30%, and a generator module serves two more extractors.

**Damage and repair.** Structures have hit points and an armour value, take damage by the model in §8, are repaired by builders, and leave a wreck when destroyed that is drawn for a while and blocks nothing.

---

## 6. Devices

A **device** is a unit the commander designed. This is the heart of the game (pillar 1), and it is taken from *Warzone 2100* nearly whole: a device is one **chassis**, one **drive** and one or more **modules**, each chosen from what the commander has researched, and its statistics are derived from the parts rather than authored per unit. Confirmed by the owner on 2026-09-17 as the reading of "devices".

**Chassis** sets hit points, armour against kinetic and thermal damage, base power cost, weight, and how many module mounts it carries. Three classes, each with successive marks unlocked by research:

| Class | Hit points | Kinetic armour | Thermal armour | Base speed (wu/s) | Sight (cells) | Cost | Mounts | Note |
|---|---|---|---|---|---|---|---|---|
| Light | 100 | 5 | 5 | 80 | 20 | 60 | 1 | Fast to build, fast to move, cheap to lose |
| Medium | 250 | 12 | 10 | 55 | 18 | 150 | 1 | The line unit |
| Heavy | 500 | 25 | 18 | 40 | 16 | 320 | 2 on Heavy II | Slow; the second mount is Heavy II's |

**Drive** sets speed as a function of terrain, the slope it can climb, whether it crosses water, and a hit-point multiplier. Six classes, of which four are in the first version:

| Drive | Speed factor | Max slope | Water | HP factor | Cost | Version |
|---|---|---|---|---|---|---|
| Wheels | 1.3 | 25% | No | 1.0 | 30 | 1 |
| Half-track | 1.1 | 35% | No | 1.15 | 45 | 1 |
| Tracks | 0.8 | 40% | No | 1.5 | 70 | 1 |
| Hover | 1.5 | 20% | Yes | 0.8 | 60 | 1 |
| Legs | 0.9 | 60% | No | 1.0 | 50 | 2 |
| Lift | 2.0 | ignores | Yes | 0.7 | 90 | M4 at the earliest |

**Derived statistics**, the formulas the tables carry and the design screen shows: speed = chassis base speed × drive speed factor × (100 − the sum of the modules' weight penalties) / 100; hit points = chassis hit points × drive HP factor × (100 + the class's hit-point upgrade) / 100; armour = chassis armour × (100 + the class's armour upgrade) / 100; cost = chassis + drive + modules; build time = cost ÷ 10 seconds, less the factory's modules; sight = the chassis's, or the sensor module's if larger. So a light on wheels with a machine gun is 104 world units per second (1.6 cells) for 130 power in 13 seconds, and a heavy on tracks with a cannon is 29 per second (0.45 cells) for 490 power in 49 seconds.

**A Mark is a component; an upgrade is a percentage.** Light II is a second chassis row with better numbers, unlocked by research, and a device keeps the chassis it was built with; a class upgrade is a percentage applied to every component of a class already in the field. The heavy's second mount belongs to Heavy II, so it exists only on devices designed with it, and a fielded Heavy I never sprouts an empty one.

**Modules** are what the device does. A weapon module has a damage class, damage, rate of fire, a short and a long range with a hit chance at each, and whether it fires direct or indirect. A system module does something other than shoot. Module costs run from about 40 (machine gun) to 250 (artillery).

| Module | Class | Damage | Rate (shots/s) | Range short / long (cells) | Hit % short / long | Splash (cells) | Weight | Cost | Note |
|---|---|---|---|---|---|---|---|---|---|
| Machine gun | Anti-light | 8 | 4 | 8 / 12 | 80 / 50 | — | 0% | 40 | Cheap, fast-firing, weak against armour |
| Cannon | Anti-tank | 60 | 0.5 | 10 / 16 | 70 / 45 | — | 10% | 100 | The line weapon; direct fire |
| Rocket pod | Anti-tank | 30, bursts of 4 | 0.2 | 12 / 20 | 60 / 40 | — | 10% | 120 | Burst fire, long range, slow reload |
| Mortar | Artillery | 80 | 0.25 | 6 minimum / 28 | indirect | 2 | 15% | 110 | Needs a spotter (§8); splash |
| Flamer | Flame | 20 | 3 | 4 / 6 | 90 / 70 | 1 | 5% | 60 | Short range, strong against light drives and soft structures |
| Artillery | Artillery | 200 | 0.1 | 12 minimum / 48 | indirect | 3 | 30% | 250 | Heavy chassis only; late research |
| Laser | Energy | 90 | 0.5 | 12 / 18 | 85 / 60 | — | 10% | 180 | Late research; armour counts half |
| Builder | System | builds 10 power/s | — | 2 | — | — | 10% | 50 | Constructs and repairs structures; the first device every commander owns |
| Sensor | System | — | — | sight 40 | — | — | 0% | 60 | Extended sight; spots for indirect fire in its sight |
| Repair | System | 15 hit points/s | — | 3 | — | — | 10% | 80 | Repairs devices in the field |
| Command | System | — | — | — | — | — | 5% | 120 | Leads an attached group and shares its experience; M4 |

**A design** is a named combination of chassis, drive and modules. Designs are per commander, made in the design screen (which needs a command post), and saved between matches in the user's directory (`TechnicalDesign.md` §9). A design's cost, build time, speed, hit points and armour are computed from its parts by formulas in the component tables and displayed in the design screen before the commander commits, and a design whose parts are later upgraded by research improves in the field: upgrades apply to the class, not to the instance.

**"Moddable" means the data files.** Every chassis, drive and module above is a row in a JSON file under `Content\` beside the executable (owner, 2026-09-17: R13 withdrawn, JSON chosen), with an id, a class, its numbers, the research item that unlocks it and the model that draws it; the derivation formulas and the damage matrix (§8) are files too. Adding a component is adding a row and a model; rebalancing is editing numbers. A mod is a directory under `Mods\` whose files override the game's at the same path, enabled by name in the lobby; the host hashes what it loaded, mods included, and refuses a client whose content differs. The loader validates every file — every prerequisite exists, the research tree has no cycle, every model referenced is present — and names the file and line that is wrong; the same rules run in CI. `TechnicalDesign.md` §8 has the layout.

**Experience.** A device that destroys things gains experience through eight ranks, each adding a small percentage to accuracy and damage. Ranks are visible on the unit, and they are what make a veteran worth retreating and repairing.

---

## 7. Research

**Research unlocks parts and improves classes.** A research item has a power cost, a research time, up to three prerequisites, and one of two effects: it makes a component or structure available, or it applies an upgrade — a percentage to the armour of every chassis, to the damage of every cannon, to the rate of every extractor — to a class, retroactively, to every device and structure already in the field.

**Labs research in parallel, one item each.** A commander with three labs researches three items at once; a lab module shortens that lab's time. Research cost is taken when the item starts, and a lab destroyed mid-item loses the progress but not the power, which is already spent.

**The tree** is authored as a table with the same properties as the component tables: id, prerequisites, cost, time, effect. It is validated at compile time — no missing prerequisite, no cycle, every unlock names a real component. Proposed sizes: 30 items in the vertical slice, 150 in the complete skirmish game (M2), against roughly 400 in *Warzone 2100*; the slice's tree is the minimum that lets a commander reach every module class in §6 once.

**Auto-research** is a lobby option: a commander who turns it on has each idle lab pick the cheapest available item. It exists for players who would rather fight than manage, and it is what the AI uses.

---

## 8. Combat

**Damage.** When a weapon hits, the damage dealt is the weapon's damage scaled by a modifier for the weapon's class against the target's class, less the target's armour of the matching kind, and never less than a third of the scaled damage:

```
scaled = damage × modifier[weaponClass][targetClass] / 100
dealt  = max(scaled − armour × armorFactor[weaponClass] / 100, scaled / 3)
```

Target classes are the six drive classes for devices and the four strength classes for structures — Soft, Medium, Hard and Bunker (§5). The modifier matrix is a table in the component data, one row per weapon class and ten target columns of integer percentages, and it is the whole of the rock-paper-scissors. Each weapon class also says how much of the target's armour counts against it — `armorFactor` above: 100 for anti-light, anti-tank and flame, 50 for energy, 0 for artillery, which is how artillery is indifferent to armour and the laser ignores half of it — and which armour it meets: kinetic for anti-light, anti-tank, artillery and energy, thermal for flame. The version-1 matrix, for tuning to start from:

| Weapon class | Wheels | Half-track | Tracks | Hover | Legs | Lift | Soft | Medium | Hard | Bunker |
|---|---|---|---|---|---|---|---|---|---|---|
| Anti-light | 120 | 100 | 50 | 110 | 130 | 100 | 120 | 60 | 30 | 20 |
| Anti-tank | 90 | 100 | 120 | 90 | 70 | 60 | 80 | 100 | 110 | 60 |
| Flame | 130 | 110 | 70 | 120 | 140 | 40 | 150 | 80 | 40 | 10 |
| Artillery | 100 | 100 | 100 | 100 | 100 | 20 | 130 | 120 | 100 | 60 |
| Energy | 100 | 100 | 100 | 100 | 100 | 100 | 100 | 100 | 100 | 80 |

Anti-light is the machine gun's class: strong against light drives and soft structures, weak against tracks and bunkers; anti-tank the reverse; flame strong against everything light and useless against bunkers; artillery indifferent to armour and poor against anything that walks out from under it. **The tables are checked before the design screen exists**: a headless script under `Tools/` runs every design against every design per tier and per power spent, and a design that dominates its tier is a table bug to fix first.

**Hitting.** A weapon has a hit chance at short range and a lower one at long range, both percentages, both modified by the device's rank and by research; the simulation rolls once per shot from its own random stream. Direct-fire weapons decide the hit at the moment of firing, and the projectile is a visual that arrives when it arrives. Indirect-fire weapons decide at impact against whatever is in the splash radius at the predicted landing cell, so a moving target can walk out from under a mortar.

**Sight and range.** Every device and structure has a sight radius; a target must be visible to someone on the commander's side to be fired on, and indirect-fire weapons additionally need a spotter — a sensor tower, a sensor device, or any device with the target in its own sight — because their range exceeds their sight. Height extends sight: a unit sees a distance scaled by its height above the target, and a ridge between them blocks it. Terrain occlusion is tested on the heightfield at cell resolution. Sight radii: light chassis 20 cells, medium 18, heavy 16, the sensor module 40, structures 12, the sensor tower 40, the tower 20; height adds a cell of sight for every 32 world units a viewer stands above its target.

**Orders.** A device carries a primary order and a standing set of stances:

| Primary orders | Stances |
|---|---|
| Move, attack-move, attack target, patrol between points, guard a position or a unit, return to repair, stop | Fire at will / return fire / hold fire; engage at optimal range / at long range; retreat at 50% / at 25% / never; pursue / hold position |

Structures with weapons take a target-priority stance only. Orders are given to selections and to numbered groups, and every order is validated by the simulation against what the commander can see and owns — an order to attack an unseen unit becomes an attack-move to its last known position.

**An enemy is legible.** Selecting or hovering a visible enemy device shows its chassis, drive and modules, and a structure its kind and health; designs are not secrets, because counter-design is the game (pillar 1). The host sends a design's parts with the first device of that design a client sees (`TechnicalDesign.md` §5.3).

**Retreat and repair** is what the stances exist for. A device whose hit points fall under its retreat threshold breaks off, returns to the nearest repair bay or repair device, waits to be repaired and returns to its guard position. It is the mechanic that makes experience matter and that makes the repair bay a structure worth defending. A device retreats only if a repair bay or a repair device is within 60 cells; otherwise it holds and fights, so that a heavy on a large landscape does not spend the match commuting — the answer to distance is a forward repair bay, and there is no build radius to stop one.

**Commanders**, in version 2, are devices with a command module: units attached to one follow it, share its target and inherit its rank. They exist so that late-game armies can be handled as a handful of groups rather than a hundred units.

---

## 9. AI

**An AI commander plays through the same orders a human does.** It sees what its units see, spends the same power, and its orders enter the simulation through the same validation. It runs inside the simulation, deterministically (`TechnicalDesign.md` §7), which is what makes an AI game replayable and an AI bug reproducible from a seed.

**It is a set of personalities over one planner.** The planner keeps a build order, an economy target, a research plan and an army composition, and chooses among them by weights; a personality is a set of weights (rusher, turtle, artillery, expander). Difficulty changes decision quality and reaction time, not information: an AI on any difficulty sees only what it has scouted. The highest difficulty additionally takes a power bonus, never vision, and the lobby says so in plain words next to the setting (owner, 2026-09-17), because an opponent that cheats silently is a bug report waiting to happen.

**The AI designs from the same tables.** For every combination of unlocked chassis, drive and module it scores expected damage per power against the enemy composition it has seen, through the damage matrix, plus a personality weight for speed, range or armour, and keeps the best two or three designs per role — line, raider, artillery, builder — re-scoring whenever research completes or the enemy's mix changes. Difficulty sets how often the planner runs (easy every 10 seconds, medium every 3, hard every second) and how many options it scores; the highest difficulty also takes the power bonus.

**A neutral faction** — hostile devices and nests scattered over the landscape, owned by no commander, the Species virus as it would look in this game — comes in M4, designed fresh (owner, 2026-09-17). It would make a large landscape dangerous to cross alone, which pillar 2 wants; it is also a second AI to write.

---

## 10. Multiplayer

**The host runs the match; each player sees the part of it their commander can see.** One simulation runs, on the host — a player's own game hosting in-process, or the headless host executable — and every other machine holds a replica of what its commander is entitled to know, kept current by the host (owner, 2026-09-17: host-authoritative state replication; `TechnicalDesign.md` §5). A client is never sent an object its commander cannot see, so the fog of war is enforced by the host rather than trusted to the client, and a modified client sees nothing an honest one does not. Orders travel to the host, are validated there against what the commander owns, sees and can afford, and take effect on the next tick; a unit moves when the host says it has.

**Lobby.** A host opens a match; players join by address, over a LAN or by direct IP. There is no master server in the first version and no NAT traversal, because both need a service somewhere. The lobby sets the landscape (a seed and a size class, or a stamped landscape by name), the base, power and technology levels, the victory condition, alliances, which seats are AI with which personality and difficulty, and which mods are enabled. The host hashes the content it loaded, mods included, and refuses a client whose content differs.

**In the match.** A player who drops is kept as a seat under AI control for a grace period and may rejoin, receiving their commander's view afresh; the host may pause; the match may be saved and resumed by the same players later. A long-running host that keeps a match open for weeks is the same machinery left running (owner, 2026-09-17: match-based, server-ready). Chat is text, to all or to allies.

**Replays.** A match is its seed, its settings and the stream of orders the host applied, which is small (`TechnicalDesign.md` §5.7 has the arithmetic), so the host records every match and any recorded match can be watched by hosting it locally, at any speed, from any commander's point of view — including the AI's. Replays are also the bug-report format: an odd AI decision or a simulation fault is reproduced by replaying the file.

---

## 11. Presentation

**The look is Species, pinned to five things**, confirmed by the owner on 2026-09-17 with the sprite population added. The numbers behind each — the lights, materials, fog, sky and camera; the terrain generator, palette and water; the window chrome, fonts and overlay — are read from the Species source in [`SpeciesLook.md`](SpeciesLook.md), [`SpeciesTerrain.md`](SpeciesTerrain.md) and [`SpeciesCanvas.md`](SpeciesCanvas.md), and the models themselves will be new (owner, 2026-09-17).

1. **Geometry is flat-shaded and vertex-coloured.** Models carry a colour per vertex and no texture, and are lit per face by one or two directional lights — the Species `.shp` format with its `Colours:` table and no normals in any of its 106 files, which is how Darwinia's models have always read. Polygon budgets follow the Species models: structures run from 60 to 2,400 triangles, vehicles from 24 to 300, the one soldier is 1,036, and the largest set-piece (`Rocket.shp`) is 4,992 (`SpeciesLineage.md` §4). Team colour is a designated colour slot in each model, replaced by the commander's colour at draw time.
2. **The landscape is coloured by its shape.** Each terrain vertex takes its colour from a 64×64 palette bitmap indexed by slope raised to the power 0.4 on one axis and normalised height on the other, with a little noise — the Species `GetLandscapeColour` formula, ported as is. Water is a flat plane at the water level drawn with the Species wave texture; the sky is a dark gradient. Eight terrain palettes and three water palettes come across (`SpeciesLineage.md` §4), and a palette is a landscape's biome.
3. **Small things are sprites.** Anything infantry-sized — crews, the neutral faction's swarm if it comes, the population that walks between structures (confirmed by the owner, 2026-09-17) — is a billboarded 32×32 sprite, the way Citizens are. Devices and structures are geometry.
4. **The interface is a terminal.** Flat rectangles with one-pixel borders, a pixel font, monochrome icons with one accent colour, windows that open over the world rather than a fixed HUD strip: the Species *Eclipse* toolkit as it looks, rewritten for this renderer. The design screen, the research screen and the minimap are windows the operator opens. The font is the Species Spectrum font (owner, 2026-09-17), because the typeface is as much a part of the Species look as the terrain palette.
5. **The camera flies.** Free yaw, pitch and height with edge scrolling and a minimum height above the terrain: the Species camera, which is an RTS camera already. No cinematic modes.

**Where the look and pillar 3 disagree, pillar 3 wins (owner, 2026-09-17), and three points were ruled on that basis**: zero ambient stays, because black in shadow is the look, and the terrain mottling stays with it; team-colour slots are drawn unlit, because lights of up to (5.0, 2.35, 0.77) would tint every team colour orange; and the black fog from 1,000 to 4,000 units, sized for maps of 5,400, either scales with the landscape or becomes distance desaturation, which ADR-002 decides with a frame to look at.

**Sound** uses the Species effect library where an effect fits — weapons, explosions, engines, construction, interface — as WAV files under `Content\Sounds` (`SpeciesLineage.md` §5), positioned in 3D by XAudio2 as Species does. There is no soundtrack in the first version: the Species music is licensed to Introversion from third-party artists and is not available to this game.

**Rendering is Direct3D 12 at an authored resolution presented scaled** (`AGENTS.md` §5). The authored resolution is the first client ADR; this design assumes 1920×1080 and a pixel font drawn 1:1 at that resolution, which is what pillar 3 needs and what the scaling rule exists to protect.

---

## 12. Scope by milestone

Each milestone is a playable state, not a subsystem list; a milestone is done when its "proves" column is true on a running build that the owner has run. Content counts are proposed.

| Milestone | Proves | Content |
|---|---|---|
| **M0 — Foundation** | A window presents a scene target, and CI captures the same scene headless on WARP as an artefact (`TechnicalDesign.md` §10); the simulation ticks deterministically and hashes; every library has a test suite; every checker `AGENTS.md` names exists and runs in CI; the landscape tool measures the generator's guarantees (§3) | No game content. The generator produces heights and the renderer draws them |
| **M1 — Vertical slice** | Two commanders (one human, one scripted) on a Small landscape build extractors, generators, a factory and a lab, design a device and fight to annihilation, with a host thread and the loopback transport in one process and the client a replica from the first slice (owner, 2026-09-17) | 1 landscape; 5 structures and one defence; 3 chassis; 3 drives; 4 modules (machine gun, cannon, mortar, builder); 30 research items; fixed-panel interface per `Interface.md`; `Net` and `Replica` over loopback; 1 scripted AI; the damage and visibility models complete; the M1 tables checked by the cost-efficiency script |
| **M2 — Skirmish** | Up to four commanders on a Medium landscape with the full structure catalogue, the full component set, fog of war, retreat and repair, ranks, and an AI with personalities; a match saves and resumes; the look completed | The full §5 catalogue; the §6 tables at version 1; 150 research items; 3 personalities × 3 difficulties; 8 biomes, the sky and clouds, the sprite population; a stamp library; the Eclipse-shaped windows |
| **M3 — Multiplayer** | Eight commanders over LAN and direct IP on a headless host; a client is never sent what its commander cannot see; a dropped player rejoins; Large landscapes and the dominance victory; mods and the content hash; replays record and play | Lobby; the UDP transport; headless host; replay viewer |
| **M4 — Frontier** | Frontier-class landscapes at full performance; the neutral faction; commanders; legs, and lift if the owner decides so then | Version 2 of the tables |

Revised on 2026-09-17 after the external review: Large landscapes, the dominance victory, mods and the content hash move to M3; M1 uses fixed panels; the look's extras come with M2. The review had also proposed deferring `Net` and `Replica` to M3, and the owner kept them in M1 (2026-09-17): replication is exercised over loopback from the first playable build.

A campaign, if there is one, follows M4 and gets its own design document.

---

## 13. Open questions

All answered by the owner on 2026-09-17. [`OpenQuestions.md`](OpenQuestions.md) keeps the record of each answer, whether it followed the recommendation, and where it is written in.

---

## 14. Vocabulary

| Term | Meaning |
|---|---|
| **Commander** | A player, human or AI, in a match |
| **Match** | One landscape, its commanders, and the simulation from the first tick to victory |
| **Landscape** | The heightfield, water, deposits and features of a match; generated from a seed and stamps |
| **World unit** | The Species unit, in which a soldier is 14 and a tank 49 |
| **Cell** | The landscape's grid unit, 64 world units square: one tank |
| **Deposit** | A point on the landscape where an extractor may be built |
| **Power** | The one currency |
| **Structure** | A built, immobile thing: command post, extractor, generator, factory, lab, repair bay, sensor tower, wall, hardpoint, tower, bunker, uplink |
| **Module (structure)** | An upgrade built onto a structure |
| **Device** | A mobile unit designed by a commander |
| **Component** | A chassis, drive or module — the parts a device is made of |
| **Chassis** | The component that sets a device's hit points, armour, mounts and base cost |
| **Drive** | The component that sets a device's speed, climb and water crossing |
| **Module (device)** | A weapon or system mounted on a chassis |
| **Design** | A named chassis + drive + modules combination a factory can build |
| **Research item** | A node in the research tree; unlocks a component or structure, or upgrades a class |
| **Order** | The only input to the simulation: what a commander tells a structure or device to do |
| **Stance** | A standing rule a device follows between orders |
| **Tick** | One step of the simulation; the simulation's only clock |
| **Host** | The machine that runs the one simulation and sends each client what its commander can see |
| **Replica** | A client's copy of the part of the match its commander can see, kept current by the host |
| **Stamp** | An authored patch of terrain and features the generator places into a landscape |
| **Plan** | A placed structure awaiting a builder; it costs and obstructs nothing until construction begins |
