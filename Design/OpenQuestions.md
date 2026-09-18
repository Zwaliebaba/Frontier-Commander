# Open questions — answered

**Status: one open — Q22, the rank numbers. Answered: the twenty-two of 2026-09-17, the six the external review raised included, and Q17 through Q21 of 2026-09-18.** Every question the drafts left open was put to the owner on 2026-09-17 with the options and a recommendation, and every answer is written into the document it belongs to, dated. This file keeps the record: the question, the answer, whether it followed the recommendation, and where it now lives. Of the original sixteen, nothing is open; the engineering choices deferred to ADRs — hierarchical A\* against flow fields, the fog and sky scaling, per-triangle normals, the authored resolution — are listed in `TechnicalDesign.md` §12 and are decided by measurement, not by the owner. One of those, the sky's scaling, turned out to carry a look decision the owner should take rather than a measurement, and it is Q17 below, answered 2026-09-18; ADR-005 settled the fog half of that pair on 2026-09-17 and left the sky untouched. A new question is added in the form the old ones had — the question, why it blocks, the options, a recommendation — and put to the owner; the six the external review raised are recorded below in the same form.

| # | Question | Answer (owner, 2026-09-17) | Followed the recommendation | Recorded in |
|---|---|---|---|---|
| Q1 | Match or world? | Match-based, designed so that a long-running host is reachable later without a rewrite | Yes | `GameDesign.md` §1, §10 |
| Q2 | Network model | **Host-authoritative state replication**: only the host simulates; each client holds a replica of what its commander can see | **No** — the draft recommended lockstep; `TechnicalDesign.md` §5.1 records what the choice buys and what it costs | `TechnicalDesign.md` §2, §3, §5, §10, §11; `GameDesign.md` §10 |
| Q3 | What "moddable" means against R13 | **R13 withdrawn as obsolete**; game data lives in files beside the executable and a mod is a directory that overrides them by path | Beyond the recommendation, which kept R13 and added a mod directory by ADR | `AGENTS.md` (R13 deleted by the owner), `TechnicalDesign.md` §1, §8; `GameDesign.md` §6 |
| Q4 | Content format, reframed once R13 went | JSON, with a reader written into `Core` under R14 | No — text tables in the Species tradition were recommended | `TechnicalDesign.md` §8 |
| Q5 | The Species-derived content other than models | Used, with the provenance risk accepted as a private project's; the soundtrack, branding and narration stay excluded | No — placeholders with a replacement plan were recommended | `SpeciesLineage.md` §1; the provenance ADR when written |
| Q6 | The reading of the Species look | Confirmed, with the sprite population added as a visual | Yes | `GameDesign.md` §11 |
| Q7 | Devices as the *Warzone 2100* design system | Confirmed: chassis + drive + modules | Yes | `GameDesign.md` §6 |
| Q8 | Terrain mutability | Flatten under structures only, through M3 | Yes | `GameDesign.md` §3; `TechnicalDesign.md` §4.4 |
| Q9 | Hand-authored maps | Seeds and stamps only | Yes | `GameDesign.md` §3 |
| Q10 | Air units | Not before M4 | Yes | `GameDesign.md` §5, §6, §12 |
| Q11 | A neutral faction | M4, designed fresh | Yes | `GameDesign.md` §9, §12 |
| Q12 | Names | Projects as drafted; `Frontier` for the game, `Neuron` for the engine | Yes | `TechnicalDesign.md` §2 |
| Q13 | Where files live, reframed once R13 went | Content and mods beside the executable; everything written under `%LOCALAPPDATA%\FrontierCommander` | Yes | `TechnicalDesign.md` §9 |
| Q14 | Tick rate | 20 Hz, recorded in ADR-002 with the empty tick measured and the full tick to follow at M1 | Yes | `TechnicalDesign.md` §3 |
| Q15 | The AI at the highest difficulty | A power bonus stated in the lobby; never vision | Yes | `GameDesign.md` §9; `TechnicalDesign.md` §7 |
| Q16 | Font | The Spectrum font | Yes | `GameDesign.md` §11; `TechnicalDesign.md` §6.4; `SpeciesLineage.md` §4 |
| — | Promotion | `GameDesign.md` and `TechnicalDesign.md` are the design `AGENTS.md` refers to; the two `AGENTS.md` sentences that said it did not exist are updated | Yes | `README.md`; `AGENTS.md` |

Three answers went against the recommendation, and the documents say so where they record them rather than smoothing it over. Replication (Q2) costs a second world model and a protocol the lockstep draft did not need, and the design carries that cost in `TechnicalDesign.md` §5 and §11. JSON (Q4) costs a reader under R14, which is three hundred lines and a conformance test. The accepted provenance risk (Q5) is the owner's to carry, and the provenance ADR will say so in terms. Two answers went further than the recommendation: R13 withdrawn outright (Q3), which simplified the content pipeline more than the mod-directory ADR would have, and promotion now.

## Raised by the external review (2026-09-17)

An external review of the eight documents, read without `AGENTS.md`, found the game half under-specified relative to the engineering half and challenged the premise that scale produces decisions rather than dead time. Its corrections are applied in the documents; the six decisions it raised were put to the owner with recommendations and answered the same day. Three went against the recommendation, and the documents say so where they record them.

| # | Question | Answer (owner, 2026-09-17) | Followed the recommendation | Recorded in |
|---|---|---|---|---|
| R1 | Where does Claude Code run for this project — can it invoke MSBuild and vstest, and can it ever see a rendered frame? | Linux sessions that cannot build. CI is the agent's compiler: every push builds and tests Debug\|x64 on the Windows runner, and the client gets a capture mode — a WARP device replaying a scripted match and writing BMPs as CI artefacts — with Direct3D debug-layer messages counted as failures | Yes | `TechnicalDesign.md` §6.1, §10; `GameDesign.md` §12 (M0) |
| R2 | May M1 and M2 run single-player without `Net` and `Replica`, with the view built from `Sim` through the render-view seam? | **No**: the host thread and the loopback transport are in the vertical slice, and the client is a replica from M1 | **No** — deferring them to M3 was recommended and had been applied; the milestones and `TechnicalDesign.md` §2 and §3 are restored | `GameDesign.md` §12; `TechnicalDesign.md` §2, §3, §11 |
| R3 | What time to first contact and map-crossing time should Small target? | Four to six minutes to first contact and under two minutes for a light device to cross; the halved size classes stand, and the *Warzone 2100* mod test is run before M1 | Yes | `GameDesign.md` §3 |
| R4 | Pillar 3 outranks the look by the game design's own rule. Do the black fog, the zero ambient and the over-bright tinting lights go, or is pillar 3 reworded? | Pillar 3 wins: zero ambient stays, team-colour slots are drawn unlit, the fog scales with the landscape or becomes distance desaturation — ADR-005 chose the desaturation on two captured frames (2026-09-17), the owner confirming or overriding at `m0-foundation/T22` — and pillar 3 forbids texture detail rather than vertex noise, so the mottling stays | Yes | `GameDesign.md` §1, §11; `TechnicalDesign.md` §6.2, §6.4, §12; `SpeciesLook.md` §5 |
| R5 | The accepted provenance risk (Q5): does it knowingly cover handing the Species-derived content to other players in M3 and inside mods? | **Yes**: the acceptance covers M3 and mods, and the provenance ADR says so in terms | **No** — placeholders through M2 and replacement before M3 were recommended | `SpeciesLineage.md` §1; `TechnicalDesign.md` §11, §12 |
| R6 | R14 forbids `d3dx12.h`, a single MIT-licensed header, and applies to tools and tests that never ship. Reconsider? | **`d3dx12.h` is admitted as the one exception to R14**: vendored under `Client/` as a single pinned file with its licence beside it, named in R14 and in the renderer ADR; nothing else | **No** — keeping R14 as written was recommended | `AGENTS.md` R14 and §2; `TechnicalDesign.md` §6.1, §11, §12 |

Three answers went against the recommendation. Replication in the vertical slice (R2) puts `Net` and `Replica` on M1's critical path, which is the cost the recommendation wanted to defer; the owner preferred to meet replication's bugs where they are cheap. Distribution under the accepted risk (R5) is the owner's to carry, and the provenance ADR will say in terms that it covers M3 and mods. The helper header (R6) is the first exception to a rule that had none, and R14 now names it so that it stays the only one.

## Raised later

| # | Question | Answer (owner, 2026-09-18) | Followed the recommendation | Recorded in |
|---|---|---|---|---|
| Q17 | The Species sky and cloud layers are absolute squares sized for maps up to 5,400 units, and a Frontier landscape is 65,536. Do they scale with the map or follow the camera? | **Camera-relative extent with the noise sampled in world space**: the layers always cover the view, cloud features keep their authored size on any landscape, and the pattern stays pinned to the ground | Yes | `SpeciesLook.md` §6; `m2-skirmish/T8`, which measures the layer heights and the world-space repeat period and records them in its ADR |
| Q18 | Species applied one palette to a whole map, so a landscape carries one terrain type. How does one landscape carry more than one? | **A palette per tile, blended by the edge falloff the tiles' heights already merge by**, with the palette out of the state hash because it colours and never generates. One water colour for the map, and palettes that share a landscape ramp their bottom rows into it, so any two blend without a seam | Yes | `GameDesign.md` §3; `SpeciesTerrain.md` §6; ADR-002 (amended) and ADR-006; `Content/LandscapeDefinition.h`; `m2-skirmish/T8` draws the blend |

| Q19 | A landscape ten times the largest is a design target. Is the ten on the side or on the area? | **Ten on the side**: 10,240 cells, 655,360 world units, a hundred times Frontier's area | Yes | `GameDesign.md` §3; `TechnicalDesign.md` §4.4 and §4.6; ADR-007; `m4-frontier/T1` measures it |

| Q20 | S3 cannot be built: nothing wires the content tables into Sim, and no task owns it. How does the simulation read content, and what binds a snapshot to the tables it was taken against? | **A `const ContentTree&` at construction, with the digest of the tables in the snapshot and a refusal on mismatch** | Yes | ADR-009; `Content/ContentHash.h`; `Sim/Sim.h`; `Sim/Snapshot.h` at version 6 |

| Q21 | The built-in light pair is the Garden's, whose sun lies on the horizon and reaches no surface facing up; the owner's frame of 2026-09-18 had a median value of 39/255. Which pair is the built-in? | **Species' Sandbox pair**, asked for after reading how Species itself does it: two elevated near-white lights from opposite azimuths, the pair Species put on its own level 1. The Garden stays as a biome | Yes | `Client/Lighting.h`; `GameData/Biomes.json`; `SpeciesLook.md` §2 and §11; `m0-foundation/T22` rules on the fog against a frame drawn under it |

The question as it was put, kept for the reasoning it weighed.

### Q17 — Does the sky scale with the landscape, or follow the camera? (raised 2026-09-18, answered 2026-09-18)

The Species sky is the black clear colour with three additive layers over it: a grid plane at height 1,200 over a square 14,000 units on a side, and blobby and flat cloud layers over a square of 17,000 (`SpeciesLook.md` §6). Those sizes are absolute and were chosen for maps up to 5,400 units across. A Frontier landscape is 8,192 units at Small and 65,536 at Frontier class, so on anything but the smallest map the layers end inside the view and the sky has a visible edge. §6 says as much and leaves the answer open: the layers scale with the map, or they become camera-relative.

**Why it blocks.** `m2-skirmish/T8` completes the look and cannot draw the sky without it; its acceptance currently says the layers land "at the scale the fog ADR chose", and no ADR chose one. ADR-005 settled the fog's form, the far plane and the depth mapping, and says nothing about the sky. The question also reaches backwards: ADR-005 chose distance desaturation over the Species fog on two captured frames whose evidence was the horizon band, 99.4% near-black against 89.7%. Both frames had a black sky and nothing drawn in it, because nothing draws a sky yet. Three additive layers over that black, each setting its own fog to black while the biome's fog is a desaturation, change exactly the part of the frame that decision rested on.

**The options.**

1. **Scale with the landscape**, as the fog does: the grid and cloud squares become fractions of the extent. The Species geometry is kept whole and the sky is a bounded ceiling over the map. On a Frontier landscape the cloud square is about 170,000 units, twelve times Species's, and with the texture repeat counts unchanged every cloud feature is twelve times larger; keeping the feature size means scaling the repeats with the square, which is the same arithmetic in a different place.
2. **Camera-relative**: the layers follow the camera at a fixed size, so cloud features keep their authored scale on any landscape and the sky has no edge at all. The cost is the fixed relationship between the clouds and the ground: clouds no longer drift over a fixed point of the map, and a shadow or a reflection could not be derived from them later.
3. **Camera-relative extent, world-locked texture coordinates**: the layer always covers the view, and the noise is sampled in world space so the pattern stays pinned to the landscape. Cloud features keep their authored size, the sky never runs out, and the clouds still sit over a place.

**Answered: option 3 (owner, 2026-09-18).** Recommendation: option 3. Species already animates the clouds by adding to the texture offset each tick, so sampling in world space rather than in layer space is a small change to the same mechanism. It is the only option that is independent of landscape size, which is the actual problem; the other two trade one of authored cloud scale or a fixed relationship with the ground to get there.

**Whichever is chosen, ADR-005's comparison is re-made with the sky drawn**, and the fog ruling is confirmed or superseded on those frames rather than on the black-sky ones. That is cheap: the capture already draws frames 100 and 200 from one vantage under the two fog modes, and it would draw them again with the sky in place.

### Q18 — How does one landscape carry more than one terrain type? (raised 2026-09-18, answered 2026-09-18)

Species keeps eight 64×64 palettes under `Terrain/`, and a palette is a lookup table rather than a texture on the ground: slope runs along x, height up y, and every terrain vertex reads its colour from that square (`SpeciesTerrain.md` §6). Species applied exactly one to a map, so Earth, Desert and Icecaps are whole-level themes and never regions inside a level. This design inherited that unchanged: a landscape definition is a seed, a size class, a tile list and a palette, singular, and `ContentValidator` enforces that the palette names one biome.

**Why it blocks.** `m2-skirmish/T8` ships eight biomes and `C2` authors both `Biomes.json` and the landscapes that name them, so the shape of the answer decides their schemas. The fit is also worse for this game than it was for Species: the largest Species map is 5,372 world units across and a Frontier landscape is 65,536, twelve times, so one colour rule covers ground a heavy device takes most of an hour to cross. `GameDesign.md` §3 names distance as the point of the game and its biggest risk, and a landscape with no landmark and nowhere that looks different from anywhere else makes the dead-time failure more likely rather than less.

**Which palettes exist is not part of this question.** Q5 already ruled that the Species-derived content other than models is used. Sampling the eight on 2026-09-18 found four that are general terrain types — Default, Desert, Earth, Icecaps — and four that are recolours of Default dressing one Species location each (`SpeciesLineage.md` §5). So four of M2's eight biomes come across and four are authored, whichever option below is taken.

**The options.**

1. **One palette per landscape, as now.** Regions never exist and variety is between maps rather than within one. Costs nothing and changes nothing. It is the honest baseline: Species shipped this way and looked good doing it, on maps a thirty-third the size.
2. **A palette per tile, blended by the falloff the heights already merge by.** The definition is already a tile list, each tile with an origin, an extent and an edge falloff over which it is pulled to the plain. Give a tile a palette and blend two lookups per vertex with that same weight. Regions become authored rather than emergent, so a stamp library can place a desert pass deliberately.
3. **A second low-frequency noise field over the map**, selecting among palettes with a blend. More organic and independent of the tile layout, but it is a new generator stage that must be integer-deterministic to the sample, and it gives no authored control over where a region lands.

**Answered: option 2 (owner, 2026-09-18).** Recommendation: option 2, with the tile's palette excluded from the state hash. It reuses merge weights that already exist and costs nothing per frame, because vertex colour is computed when a chunk is built and baked into its buffer, so a second palette read and a lerp are build-time work. The cost that is not obvious: ADR-002's state hash covers every tile's field, so adding one regenerates every determinism fixture and golden hash. Excluding it is defensible, because a palette determines no simulation behaviour, but it is a deliberate exception that ADR-002 has to be amended to state rather than something to leave implied.

**What looked like a limit, and was not** (corrected 2026-09-18 on the owner's reading). This question first recorded that water is a single plane at one level, so regions would be land only and a desert coast and a temperate coast would share water they should not. That was wrong twice over. A desert coast and a grass coast share an ocean in the world too, so one water colour is right rather than a compromise; and the real mismatch was never the water but the palettes disagreeing about their own lowland rows, Species's Default putting dark blue there for shallows where Desert puts sand. The answer is an authoring rule and no mechanism at all: **a palette's bottom rows ramp into the water plane's colour**, so every palette meets the water the same way and any two blend cleanly (`SpeciesTerrain.md` §6). `Tools/MakeTerrainPalette.py` applies it and `GameData/Terrain/LandscapeDefault.dds` is the first written to it.

**What the answer cost, as built on 2026-09-18.** A tile carries a palette, absent meaning the landscape's; the loader reads it and the validator refuses one that names no biome; the snapshot carries it, at format version 3; the state hash does not, and ADR-002 is amended to say so rather than leave it implied. The blend itself is `m2-skirmish/T8`'s, because the terrain pass takes one palette today. The hash exclusion is not a new exception: the landscape's own palette was already outside it.

### Q19 — How much larger than Frontier, and is the target on the side or on the area? (raised 2026-09-18, answered 2026-09-18)

The owner stated on 2026-09-18 that a landscape ten times the current largest is a design target, not work for today. The structure that answers it is the same either way and ADR-007 records it: nothing resident may be O(area), so the chunk grid becomes a quadtree whose depth grows with the landscape, and the heightfield stops being a resident vector. What the reading changes is every figure by a factor of ten, and with it whether the answer is a paging scheme or a large machine.

**Why it blocks.** Nothing today, which is why it is a design target rather than a task. It blocks the *numbers*: `GameDesign.md` §3's size table, the cluster-graph sizing of `TechnicalDesign.md` §4.5, the visibility arithmetic of §4.6, and what `m4-frontier/T1` has to measure. A size class cannot be added to `SIZE_CLASS_CELLS` until it is answered, and the four that exist are unaffected either way.

**The options, as arithmetic** (a cell is 64 world units, a chunk 32 cells, heights `std::int16_t` at four samples per cell edge, visibility 1.25 bytes per cell per commander for eight commanders):

| | Frontier today | A: ten times the side | B: ten times the area |
|---|---|---|---|
| Cells per side | 1,024 | 10,240 | 3,232 |
| World units per side | 65,536 | 655,360 | 206,848 |
| Chunks | 1,024 | 102,400 | 10,201 |
| Heights, resident | 33.6 MB | **3.36 GB** | 334.3 MB |
| Cell grid and visibility | 14.7 MB | **1.47 GB** | 146.3 MB |
| ADR-007's coarse level everywhere | 10.8 MB | **1.08 GB** | 107.2 MB |
| Pathing clusters (§4.5 sizes 4,096) | 4,096 | 409,600 | 40,804 |
| Quadtree levels to one root | 5.0 | 8.3 | 6.7 |
| `int32` position headroom (ADR-002) | 128× | 12.8× | 40.6× |

1. **Ten times the side**, 655,360 world units. The natural reading of "ten times the size", and a hundred times the area. 4.82 GB of simulation state before a triangle, so the heightfield has to be generated per region or paged from disk rather than held; the renderer is the easier half.
2. **Ten times the area**, about 207,000 units a side. 481 MB of simulation state, which a large machine holds. The quadtree is still required, because 10,201 chunks is 10,201 draw calls under today's flat grid, but nothing has to be paged.

**Answered: option 1, ten on the side (owner, 2026-09-18).** Recommendation: option 1, and for the reason the owner's answer makes moot — the structural work is identical and only option 1 forces the heightfield question, which is the one that cannot be retrofitted cheaply.

**What the answer costs, and it is not the renderer.** At 655,360 world units the dense arrays of §4.4 and §4.6 are 3.36 GB of heights, 419.4 MB of cell grid and 1.05 GB of per-commander visibility for eight seats: **4.82 GB of simulation state before a triangle is drawn**, against 48.3 MB on Frontier. None of it survives as a `std::vector` over the whole map, and the shape of the answer is the same for all three — tiled and sparse, with ground no commander has reached costing nothing. A landscape is a seed and a tile list, so heights are a pure function of a few hundred bytes and a region can be produced rather than stored; visibility is the easier win, because most of a landscape that size is never explored by anyone, and a commander who has seen a twentieth of it needs 6.6 MB against 131 MB dense. What does not change is the per-tick cost: §4.6's refresh budget is in viewers, and viewers scale with the device cap rather than with the map.

**The consequence that is not engineering.** §3's own arithmetic, the arithmetic that halved the size classes on 2026-09-17: a light device crosses a Small landscape in 1.3 minutes and a Large one in 5.2, so at 655,360 units it is **105 minutes**, and a heavy device **378 minutes — 6.3 hours**. A map that cannot be crossed in a sitting is not a bigger version of the same game. It is coherent with what the project is called, and it makes transit a strategic decision rather than a tactical one — forward bases, production at the front, and a reason for transport to exist — but those are game-design answers that §3 and §6 owe, not engineering ones, and the crossing-time test §3 already schedules before M1 is where they should come from. `GameDesign.md` §3 also gives a reason to prefer knowing: it names distance as the point of the game and its biggest risk, and a heavy device already takes 19 minutes to cross a Large landscape. Ten times a Frontier side is a crossing measured in hours, so the answer is a gameplay question before it is an engineering one, and the crossing-time test that section already schedules before M1 is where it should be settled.

### Q20 — How does the simulation read content, and what binds a snapshot to the tables it was taken against? (raised 2026-09-18, answered 2026-09-18)

`m1-vertical-slice/S3` is the first task that cannot be built without this. Its acceptance is precise — "a generator serves the four nearest unserved extractors within 48 cells", a stockpile capped at "1,000 plus 500 per completed generator", demolition refunding half — and every clause needs the simulation to know which structure row is a generator, an extractor or a command post, and what each costs. `Content/StructureDesc.h` already carries exactly the right `StructureRole` enum, and `C2`'s own intent says "every simulation task reads content rather than literals". **But nothing wires the `ContentTree` into `Sim`, and no task in any plan lists that as its work.** `Sim`'s constructor takes `MatchSettings` and nothing else.

**Why it blocks.** `S3`, `S4`, `S5`, `S6` and `S10` — the economy, structures, devices, research and combat, which is most of M1's simulation — all read rows. It also reaches the save format: `Snapshot::Read` reconstructs a `Sim` from bytes alone, so if a `Sim` needs content, `Read` needs it too, and its signature is fixed by ADR-003. And it reaches determinism: a match reloaded against different tables is a different match, which today would diverge in silence. `m3-multiplayer/T3` builds a content hash for exactly that reason, but the exposure starts at M1, three milestones earlier.

**The options.**

1. **`Sim` holds a `const ContentTree*` given at construction, and the snapshot records the content's hash and refuses a tree that differs.** `Snapshot::Read` takes the tree beside the bytes. ADR-006 already says one process holds one tree that nothing writes to after loading, so the pointer is to something immutable and shared, and the refusal is the same shape as the digest refusal ADR-003 already makes for altered bytes.
2. **`Sim` derives its own plain rules from the tree at construction and the snapshot carries them.** A snapshot is then entirely self-describing: reload it on any machine, years later, with no tables on disk, and it plays identically. The cost is that every snapshot carries every row — chassis, drives, modules, structures, research, the damage matrix — which are identical between almost any two snapshots, and ADR-003's size estimate was written without them.
3. **A process-wide tree the simulation reaches without being given one.** Cheapest to write and worst for everything else: two `Sim`s in one process — the host and the replay checker `DeterminismTests` runs — could not be given different tables, and a global is the kind of hidden input `AGENTS.md` R16 exists to keep out of the simulation.

**Answered: option 1 (owner, 2026-09-18).** ADR-009 records it and what it cost. Recommendation: option 1, with the content hash in the snapshot and a superseding note on ADR-003 for the signature. It keeps the snapshot small, so ADR-003's estimate holds as written; it turns "reloaded against different tables" from a silent divergence into a refusal at load, which is the treatment altered bytes already get; and M3's content hash then extends the same value to the wire rather than inventing a second mechanism. Option 2 is genuinely attractive for a replay opened years later and is the better answer if replays are meant to outlive the tables — that is the question behind the question, and it is the owner's rather than mine.

**A smaller thing the same answer settles.** `S3`'s `depends_on` is `[S2]`, but nothing in M1 authors the tables it reads: `C2` does, and no edge says so. Whichever option is taken, `S3` gains an edge to `C2` and to whatever task does the wiring.

## Open

### Q21 — Which light pair is the built-in, now that the camera is not Species' camera? (raised 2026-09-18, answered 2026-09-18)

`Client/Lighting.h` carries the Garden map's pair as the built-in until `GameData/Biomes.json` carries one per biome: a near-white key at 23 degrees and a horizontal orange sun at three and a half times white (`SpeciesLook.md` §2). It is faithful, and on the owner's frame of 2026-09-18 it is also very dark — a median value of 39/255, with 819 pixels of 399,519 above 120.

**That darkness is not the fog.** The desaturation is exactly luminance-preserving, so none of it comes from ADR-005 or ADR-007. It is the rig meeting a camera it was not built for: Garden's sun sits at 0 degrees of elevation and contributes **exactly nothing** to any surface facing up, and its key contributes N·L = 0.39 to flat ground. Species looked along the ground, so cliff faces filled the screen and the horizontal sun did the work; an RTS camera looks down at ground that is mostly flat, and sees the key alone. Sampling 4,000 terrain normals up to 40 degrees of slope, from `SpeciesLook.md` §2's own table:

| Rig | Flat-ground luminance | Mean luminance | Normals fully black | Normals over 0.5 |
|---|---|---|---|---|
| Garden (in use) | 0.38 | 0.44 | 11.1% | 39.5% |
| Sandbox | 0.95 | 0.87 | 0.0% | 95.7% |
| PatternBuffer | 0.87 | 0.74 | 2.6% | 78.7% |
| Biosphere | 0.52 | 0.49 | 2.7% | 52.4% |

**Why it blocks.** Nothing, strictly — but `m0-foundation/T22` asks the owner to rule on the fog from a frame, and the frame's brightness is mostly this rather than the fog, so ruling on one without deciding the other reads the wrong cause. `m2-skirmish/T8` then authors eight biomes with a light pair each and would inherit whichever answer is implied rather than taken.

**What Species does, read from the source on 2026-09-18 at the owner's instruction.** The short answer is that *Species has no built-in rig at all*, so the question has no Species answer to copy — only a Species map to copy from. The longer answer is four findings, and one of them was the direction this question had been leaning away from.

- **The rig is per-map, world-fixed, and nothing ever touches it.** `GameLogic/Location.cpp:2149` `SetupLights` runs once when a location loads; the light is a direction and a colour and neither depends on the camera, the time or the frame. `GameLogic/WorldObject.cpp:79` `Light::Light` gives a *new* light a single horizontal white at 1.3 — which is nobody's map — so every pair in `SpeciesLook.md` §2's table was authored by hand, and the editor has a button (`LightsWindow.cpp:60` `LightGammaButton`) whose whole job is to multiply a light's colour up and down. There is no gamma, no brightness preference and no exposure anywhere in the renderer.
- **The landscape is lit by exactly that pair, on the same terms as everything else.** `GameLogic/LandscapeRenderer.cpp:337` enables `GL_LIGHT0`, `GL_LIGHT1`, `GL_COLOR_MATERIAL` and `GL_LIGHTING` and draws; the material is diffuse 1.0 and the scene ambient is zero. So the arithmetic this question rests on is the arithmetic Species runs.
- **The palette is not where the brightness hides — and this is the finding that matters, because it was the likeliest way for this question to be wrong.** Species colours a terrain vertex by slope and height (`GetLandscapeColour`, `LandscapeRenderer.cpp:167`: `u = (1 - normal.y)^0.4`, `v = 1 - height / highest` plus noise), which puts the *flat* ground in the palette's left column and the cliffs in its right. That is a mechanism which could have handed flat ground a bright, saturated colour precisely to make up for a rig that does not reach it. It does not: measured out of `GameData/Terrain/LandscapeDefault.bmp`, the flat column runs white at the summit through (22, 164, 126) at mid height to (13, 46, 101) at sea level — luminance 0.51 at mid height, 0.17 at the bottom. `TerrainPalette::BuiltIn()` already implements the identical formula and is 0.53 at mid height and 0.43 at sea level, so *Frontier's palette is as bright as Species' in the middle and two and a half times brighter at the bottom*. Garden's flat ground in Species is as dark as the owner's frame. The rig is the whole of the difference.
- **Species' own first map is lit the neutral way.** `GameData/Game.txt` line 4 makes level 1 `MapSandbox.txt`, and Sandbox is lit by two elevated near-white lights from opposite azimuths rather than by a horizontal sun. It is the first map a player sees and so the one that had to read clearly; it is one of the larger maps at 5,000 units a side, and its terrain is the smoothest at small scales, its two tiles carrying `heightScale` 1.00 and 1.12 against Garden's 4.31 to 4.66. **Size does not order the rigs, and the smoothness claim is weaker than it looks.** The largest Species map is the Generator at 5,372 and it carries the most extreme horizontal sun in the game; the Garden at 2,002 is the *smallest* of the twelve, not the largest, which is what `GameDesign.md` §3 said until this reading corrected it. And `heightScale` is the fractal's raw amplitude, which the merge then rescales to the tile's `desiredHeight` (`SpeciesTerrain.md` §4), so it sets the roughness *per octave* and not the relief: by relief against tile size Sandbox is the steeper of the two, 400 units over tiles of 1,834 and 2,224 against Garden's 107 over 564 to 884.

**The honest counter, which the correction above strengthens.** Sandbox is the tutorial, and a tutorial is lit to be read rather than to be admired; the maps that carry the look are the art-directed campaign maps, and their terrain was built around a horizontal sun that had cliffs to catch. With the size and steepness orderings removed, "Species lights a Frontier-shaped map this way" is no longer available as an argument — the twelve maps' shapes do not predict their rigs at all. What survives, and it is the whole of the case: the measurement (0.95 against 0.38 on flat ground, nothing black against 11.1% black), the fact that the pair was authored rather than defaulted, and the fact that Species put a neutral pair on the one map it needed a player to read.

**What this changes.** Option 3 gets weaker: Species tuned twelve pairs by hand and never needed a thirteenth shape, and raising Garden's sun off the horizon would make a rig no Species map has. Option 1 gets weaker as a *default*, not as a look — Garden belongs to Garden. Option 2 keeps the reason it started with, the measurement, and gains one precedent rather than an argument from shape: Species put a neutral pair on the map it needed read.

**The options.**

1. **Keep Garden.** It is what Species used and the zero-ambient black faces are the look `GameDesign.md` §11 and the owner's ruling of 2026-09-17 (R4) kept deliberately. The frame stays dark, and the answer to "too dark" is that the player learns to read it.
2. **Make Sandbox or PatternBuffer the built-in.** Both are from the same measured table, so nothing is invented: two soft neutral lights from opposite azimuths, elevated, with no face left unlit. Sandbox is 2.5× Garden's brightness on flat ground and leaves nothing black. The cost is the saturated orange rim on cliff faces, which is the most recognisable thing about the Species look.
3. **Author a Frontier pair**: keep Garden's warm key and raise the sun off the horizon far enough to reach flat ground, which is neither a Species rig nor a neutral one.

**Answer (owner, 2026-09-18): option 2, Sandbox.** `Client/Lighting.h` carries `SANDBOX_LIGHTING` as `BUILT_IN_LIGHTING`, `GARDEN_LIGHTING` stays beside it, and `GameData/Biomes.json` carries two rows: `Default` with the Sandbox pair and `Garden` with the Garden's, both on `LandscapeDefault.dds`, which is the ramp Species gives both maps. Nothing about the zero ambient changes — no fill term is added anywhere, the two lights simply reach further. `m0-foundation/T22` now rules on the fog against a frame drawn under this rig and under ADR-007's range, which was the reason the two questions could not be separated.

**Recommendation: option 2, Sandbox specifically, and option 1 kept as a biome.** The rig is per-biome data already (`Content/BiomeDesc.h`), so this is a question about the default rather than a foreclosure: making Sandbox the built-in gives an RTS camera a readable landscape now, and Garden stays available as the biome it actually belongs to, for the ground-level vantages the capture uses. Sandbox over PatternBuffer because of the reading above — it is the pair Species put on its own large, gentle map — rather than because it is the brighter of the two. Option 3 is what to reach for only if a frame under option 2 loses more of the look than the readability is worth, which is a frame to look at rather than an argument to have.

### Q22 — What does a rank add, and how much experience buys one? (raised 2026-09-18)

`GameDesign.md` §8 says "a device that destroys things gains experience through eight ranks, each adding a small percentage to accuracy and damage", and §12 puts ranks in M2. It gives the count and the word "small" and no numbers, and `m1-vertical-slice/S5`'s acceptance asks for "the per-rank accuracy and damage percentages **the design proposes**" — which the design does not.

**Why it blocks, and how much.** Not much, and that is worth saying rather than dressing it up: S5 ships the fields and a table, S10 applies them, and nothing before M2 reads a number a player would notice. What it does decide is whether a veteran is an edge or a second tier, which is a balance question the cost-efficiency script of `C3` cannot answer — it compares designs at equal power spent, and a rank is not bought with power. So the numbers are the owner's in the way the damage matrix is, and this asks for them before M2 tunes around whatever S5 happened to write.

**What S5 shipped, as the proposal.** `Sim/Design.h`, so that the code has an answer while the question is open:

| Rank | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| Experience at | 0 | 2 | 5 | 10 | 20 | 40 | 80 | 160 |
| Accuracy and damage | +0% | +2% | +4% | +7% | +10% | +14% | +18% | +24% |

The shape is the argument. The thresholds double, so a rank costs about as much as every rank before it put together: the eighth is 160 weighted kills, which is a device that survived a match rather than a device that had a good minute, and it is a thing a commander protects. The percentages reach 24, which is worth the retreat-and-repair loop `GameDesign.md` §8 says ranks exist to justify, and it is well under the 60 a class upgrade can reach, so a veteran light is still a light.

**The options.**

1. **Take the table above.** It is measured against nothing, which is its weakness, and it is at least internally argued.
2. **Flatter and cheaper**: eight ranks reaching +12%, thresholds 0, 1, 3, 6, 10, 15, 21, 28. Ranks then happen to most devices that fight twice, and the mechanic reads as texture rather than as an investment.
3. **Steeper and dearer**: reaching +40%, thresholds doubling from 3. A veteran becomes a unit worth building a repair bay for, and a lost one is a real setback — which is the *Warzone 2100* feel and also the one that punishes a player who is already losing.

**Recommendation: option 1, and re-open it in M2 against a measured match rather than against an argument.** A rank is the one number in the game that compounds with itself — a device that wins fights gets better at winning fights — so the safe direction is the modest one until `m2-skirmish` has AI-versus-AI matches to measure the spread on. The table is in one header with one reader, so changing it is an edit and not a migration.

## Adding a question

The form the answered ones had: the question in a paragraph; why it blocks, naming the sections that cannot proceed without it; the options; a recommendation with its reason. Put it to the owner, and when it is answered write the answer into the owning document with the date and add a row above.
