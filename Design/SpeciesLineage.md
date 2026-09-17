# Species Lineage — what comes across, and what does not

**Status: DRAFT (2026-09-17).** An inventory of the Species repository as read on 2026-09-17 at commit `d1add55`, with a disposition for each part: port, take the design, take the data, or leave. Paths in this document are relative to the Species repository unless they start with `Design/`. Figures marked *measured* were counted in that tree with the commands noted; nothing was built or run. `AGENTS.md` is explicit that a decision taken in a sibling tree binds nothing here — this document is about what is worth carrying, not what is inherited by default.

---

## 1. Provenance, first

**Species is Darwinia.** Its `README.md` says so in terms: the codebase began as the Darwinia source by Introversion Software, and *"the licence covering the original source has not been established, so treat the provenance as unresolved rather than permissive."* Its `LICENSE` file, deleted in commit `eec3992`, described Species as an internal research project, not licensed for commercial use and not for distribution, and carried a *Provenance* section saying the same thing the README still says; commit `6fb1246` removed the README's licence section that pointed at it. The situation is not that the licence is permissive but that the project's own terms are gone and the original's were never established. The shapes, textures, sprites, sounds and level files under `GameData/` are Darwinia's art, and the code in `GameLogic/` is Darwinia's game.

`AGENTS.md` R14 already has the rule this falls under: third-party content compiled in is the owner's question, needs the owner's approval before it lands, and the licence text travels with the bytes. This document therefore sorts the content into three bins and asks the owner to decide the middle one (Q5):

- **Never**, whatever the owner decides about the rest, because it is licensed to Introversion from someone else or is Introversion's identity: the six soundtrack tracks (Tresk, Trash80, DMA-SC — 126.9 MB, *measured*), the Introversion and publisher logos and splash screens (`IvLogo.bmp`, `MsnOberonComboSplash.bmp`, `DmaCrew.bmp`, `ProgramDarwinia.bmp`, `DarwinResearchAssociates.bmp`), and the Sepulveda narration.
- **The owner's call**: Darwinia's models, effect sounds, terrain palettes, sprites, icons and fonts, and the Darwinia-derived code. The recommendation is to use them as **placeholders with a replacement plan** — they are exactly the right shape for the vertical slice, and a game that is ever distributed needs its own — and to record that in ADR-006 with the plan. The fonts are the one item in this bin whose provenance is known rather than unresolved, and §4 says what it is.
- **Clean**: the engineering Species added on top — the input event system, the network transport, the XAudio2 backend, the slot maps, the checkers, the documents — which is the owner's own work.

---

## 2. The tree, measured

Lines of C++ per project, *measured* with `find <dir> -name '*.cpp' -o -name '*.h' | xargs cat | wc -l`:

| Project | Lines | What it is |
|---|---|---|
| `NeuronCore` | 5,048 | Sockets, transport, wire protocol, byte streams, slot maps, PRNGs, assertions, preferences |
| `NeuronClient` | 23,019 | OpenGL renderer, `.shp` models, XAudio2 sound, the input system, the Eclipse UI toolkit, resources |
| `NeuronServer` | 700 | The lockstep host: client registry, sequence counter, sync values |
| `GameLogic` | 65,808 | Darwinia's entities, buildings, weapons, the world model, the landscape, the in-game windows |
| `Species` (exe) | 14,604 | Application, main loop, camera, renderer entry, task manager interface, editor |
| `Server` (exe) | 110 | Headless host, ticks at 10 Hz |
| `Tests` | 5,984 | Four suites |

Content under `GameData/`, *measured* with `ls`, `du`, `file` and a Python pass over the files:

| Content | Count | Size | Format |
|---|---|---|---|
| `Shapes/` | 106 files, 301 fragments, 29,637 positions, 32,253 triangles; 75 files carry markers, none carries normals | 2.4 MB as text | Text `.shp`: per fragment a transform, a position table, a colour table, vertices as (position, colour) pairs, triangles; named markers |
| `Textures/` | 37 | 3.7 MB, of which 2.5 MB is two splash screens | 24-bit and 8-bit BMP; ten 8-bit font bitmaps at 256×224 (one at 256×208) |
| `Terrain/` | 18 | 276 KB | Eight 64×64×24 landscape palettes, three 128×128×8 water textures, seven wave textures |
| `Sprites/` | 7 | 44 KB | 32×32×24 BMP |
| `Icons/` | 45 | 940 KB | 128×128×8 BMP |
| `Sounds/` | 1,332 | 500.9 MB (481 MiB) | Every one 16-bit mono 44.1 kHz PCM WAV |
| `Levels/` | 29 | 228 KB | 12 map files and 17 mission files, text |
| `Language/` | 6 | 728 KB | Phrase tables; English is 126 KB |
| `Sounds.txt` | 16 entity, 29 building and 10 other blocks; 109 sample groups; 163 distinct sound names | | The event-to-sample model |
| `Stats.txt` | 19 rows | | Health, speed and rate of fire per entity type |

Five shape files (`Ark`, `Armour`, `BattleCannonBase`, `BattleCannonFull`, `BoxKite`) parse to zero triangles by the grep used here; `Ark.shp` has fragments with positions, so the count is a quirk of the grep and not of the files. Verify by reading before counting on any of the five.

---

## 3. Code

Disposition per module. *Port* means the code moves, renamed to `AGENTS.md` §1 and reformatted by `.clang-format`, and it is chosen only where the code is Species' own work and the design here is the same. *Design* means the shape and the rules come across and the code is rewritten. *Leave* means leave.

### NeuronCore

| Module | Disposition | Why |
|---|---|---|
| `SlotMap`, `FastSlotMap` | Design | The narrow handle-in, reference-out API is right; the two flavours exist to reproduce Darwinia's legacy index assignment, which this game does not need. One slot map with generation-checked handles, and ids that are not indices (`Design/TechnicalDesign.md` §4.3) |
| `SliceWalker` | Leave | Slicing a frame into ten was Darwinia's way of spreading work; budgeted systems (`Design/TechnicalDesign.md` §4.5, §4.6) replace it |
| `Transport`, `UdpTransport`, `LoopbackTransport`, `UdpSocket` | Port | Species' own work from `network-transport` T7–T11: the seam, the bounded reads, the polled socket, the loopback for tests. The best-tested networking in that tree |
| `NetworkUpdate`, `ServerToClientLetter`, `ByteStream`, `ProtocolLimits`, `TeamControls` | Design, partly | The framing, versioning and sequence discipline come across as design; the 42-byte fixed packet, the thirteen update kinds, `NUM_TEAMS 4` and the Darwinia vocabulary (`RunProgram`, `AimBuilding`) do not. `Design/TechnicalDesign.md` §5.2 is the replacement |
| `MathUtils` (`syncrand`, a Mersenne Twister), `Random` (an LCG) | Design | The two-streams rule, named and enforced. The generators are replaced (`Design/TechnicalDesign.md` §4.2) |
| `NeuronMath` and the DirectXMath conventions | Port, renderer only | DirectXMath is SDK content and R14 allows it; it belongs on the render side and never in `Sim` |
| `Debug` (`ASSERT`, `DebugTrace`, `Fatal`), `NeuronHelper` (`NonCopyable`, `ScopedHandle`) | Port | Small, Species-style, and exactly what `Core` needs first |
| `FileSys`, `Preferences`, `Profiler`, `HiResTime`, `GameTime` | Leave | R13 removes the file system as a runtime concern; preferences are an ADR of their own; timing is the tick |
| `LookupTable`, `VectorUtils`, `2dArray` | Port where used | Utility |
| `WorldObjectId` | Leave | A slot index on the wire is the design this game is explicitly not repeating |

### NeuronClient

| Module | Disposition | Why |
|---|---|---|
| The input system: `Input`, `InputDriverWin32`, `InputEvents`, `InputRouter`, the `InputDriver*` binding stack, `TargetCursor`, `KeyDefs`, `KeyNames` | Port | Species' own work (`input-native-events`); `Design/TechnicalDesign.md` §6.5 takes its rules as the specification. The rebinding stack and its preferences syntax are worth keeping |
| `SoundSystem`, `SoundInstance`, `SoundParameter`, `SoundLibrary3d`, `SoundLibraryXAudio2`, `SoundStreamDecoder`, `SampleCache` | Port | Species' own `sound-xaudio2` work: one XAudio2 backend, X3DAudio, device-loss recovery. The WAV loader becomes a memory reader over embedded data |
| `Eclipse`, `EclWindow`, `EclButton`, `InputField`, `ScrollBar`, `DropDownMenu` | Design | The windowing model and the input-first routing are right; the drawing is OpenGL immediate mode and Darwinia-derived |
| `Shape`, `ShapeFragment`, `ShapeMarker` | Leave; the *format* moves to the baker | Runtime parsing of text models is what R13 forbids; `Tools/BakeModels.py` reads `.shp` and writes headers. The marker concept — a named attachment point with a transform in the fragment tree — is kept in the baked output |
| `TextRenderer` | Design | Bitmap-font quads; rewritten for D3D12 and a new atlas |
| `Bitmap`, `Texture`, `Resource`, `OGLExtensions`, `RenderUtils`, `SphereRenderer`, `3dSprite`, `GlVertex`, `2dSurfaceMap` | Leave | OpenGL and runtime loading |
| `WindowManagerWin32`, `Win32EventHandler` | Design | A borderless window that owns Escape and Alt+F4 (`AGENTS.md` §5) is a different window; the message-pump-once rule comes across |
| `ClientToServer` | Design | The client endpoint of the lockstep conversation, reshaped around the protocol of `Design/TechnicalDesign.md` §5.2 |
| `LanguageTable` and the `_kbd` phrase mechanism | Design, later | A phrase keyed by whether it names a binding is a good idea; localisation is not in the first version |
| `SystemInfo`, `UserInfo`, `FilePaths`, `FilesysUtils`, `FileWriter` | Leave | |
| The `*Access` interfaces (`RendererAccess`, `CameraAccess`, `LocationAccess`, …) | Leave | The dependency inversions Species needed to unpick Darwinia's single binary; this tree's layering never has the problem |

### NeuronServer

| Module | Disposition | Why |
|---|---|---|
| `Server`, `ServerToClient` | Design | Client registry, sequence counter, per-sequence sync values, history pruning, server-assigned ids, liveness: the host of `Design/TechnicalDesign.md` §5 is this with a snapshot path added and the Darwinia vocabulary removed |

### GameLogic

Darwinia's game, 65,808 lines, and almost none of it is this game. What is worth reading before writing the equivalent:

| Module | Disposition | Why |
|---|---|---|
| `Landscape`, `LandscapeTile` — diamond-square with guide grids, tile merging, flatten areas | Design; port the algorithm to integers | The landscape generator is the look and the shape of the land, and it is the one piece of `GameLogic` this game cannot do without. It draws from the cosmetic LCG in `float` and must be rewritten on the simulation stream in fixed point (`Design/TechnicalDesign.md` §4.4) |
| `LandscapeRenderer::GetLandscapeColour` | Port the formula | The terrain look in a dozen lines (`GameLogic/LandscapeRenderer.cpp:167–194`) |
| `Water`, `Clouds`, the sky | Design | The look; the drawing is OpenGL |
| `EntityGrid`, `ObstructionGrid` | Design | Spatial grids are needed; these are whole-map float grids sized for one team count |
| `RoutingSystem` | Leave | Waypoint routes for designer-authored paths, not a pathfinder |
| `Weapons`, `Explosion`, `ParticleSystem` | Read | Projectile kinds and their feel — laser, grenade, rocket, airstrike — are reference for the module table; the particle system's shape is worth copying on the render side |
| `GunTurret`, `Building` (markers as entrances, docks and ports) | Read | How a building uses `.shp` markers is the pattern for turrets and muzzles |
| `Camera` (in `Species/`) | Design | A free RTS camera with mounts; the control feel is the target |
| `LevelFile` | Leave; the map format is a reference for stamps | The `Landscape_StartDefinition` block — size, cell size, outside height, palette names, tile list — is a landscape definition already |
| Everything else — `Citizen`, `Engineer`, `Officer`, `Spirit*`, `Virii`, `Centipede`, `SoulDestroyer`, `Spider`, `ArmyAnt`, `AntHill`, `Triffid`, `Incubator`, `TrunkPort`, `TaskManager`, `GlobalWorld`, `Ai`, the in-game windows | Leave | Darwinia's mechanics, Darwinia's fiction, Darwinia's code. The neutral faction (Q11), if it comes, is designed fresh with these as a mood board |

### Tests and tools

| Item | Disposition | Why |
|---|---|---|
| `Tests/NeuronCoreTests` (protocol encodings, the transport conversation over loopback), `Tests/NeuronClientTests` (input derivation) | Port with their subjects | Tests move with the code they cover |
| `tools/check_layering.py` | Port | An upward include fails, there is no allowlist, and a symbol declared low and defined high is caught too. `AGENTS.md` §6 names three checkers; this is the fourth, and it is the one that protects §2 of `Design/TechnicalDesign.md` |
| `tools/check_format.py`, `tools/check_project_files.py` | Design | `AGENTS.md` names their equivalents under `Build/`; the Species versions check changed lines only, which a tree formatted from the first line does not need |
| `tools/check_task_dag.py` and `docs/TASK_DAG.md` | Owner's choice | A plan-as-DAG discipline for agentic work; `AGENTS.md` here neither has it nor forbids it |

---

## 4. Art

### Shapes

Of the 106 models, those that are the right kind of thing for this game, grouped by the `Design/GameDesign.md` catalogue entries they could stand in for. Every one is a placeholder until ADR-006 says otherwise.

| For | Species shapes |
|---|---|
| Devices: chassis and drives | `TankBody`, `Wheel`, `Armour`, `Squad`, `Tripod` (a legs reference), `Lander` (a lift reference) |
| Devices: modules | `TankTurret`, `TurretBase`, `TurretBarrel`, `TurretShell`, `BattleCannonBase`, `BattleCannonBarrel`, `BattleCannonTurret`, `BattleCannonFull`, `FieldGun`, `FieldGunShell`, `Missile`, `Rocket`, `RadarDish` (a sensor), `ControlTowerDish` |
| Structures | `Factory`, `Generator`, `PowerStation`, `SolarPanel`, `FuelGenerator`, `FuelGeneratorPump`, `FuelStation`, `FuelPipe`, `FuelPipeBase`, `Refinery`, `Mine`, `MineCart`, `Pylon`, `Wall`, `LaserFence`, `FenceSwitch`, `GunTurret`, `ControlTower`, `ControlPad`, `DisplayScreen`, `Library`, `BlueprintConsole`, `BlueprintRelay`, `BlueprintStore`, `ConstructionYard`, `ConstructionYardRung`, `ResearchItem`, `TrunkPort`, `BridgeEnd`, `BridgeTower`, `UpgradePort`, `PrimaryUpgradePort` |
| Features | `Temple1`–`Temple4`, `Cave`, `RockHead`, `Plant`, `TrackLink` |
| Infantry-scale, if sprites are not used | `Citizen`, `Engineer`, `Officer`, `LaserTroop` |
| Not for this game | `Spider*`, `Centipede*`, `SoulDestroyer*`, `TriffidEgg`, `TriffidHead`, `SporeGenerator`, `ArmyAnt*`, `AntHill`, `SpaceInvader`, `Ark`, `FlyingEgg`, `GarbageCollector`, `GodDish`, `GoldenScroll`, `Help`, `Camera`, `Throwable`, `BoxKite`, `SpawnPoint`, `SpawnLink`, `MasterSpawnPoint`, `ReceiverLink`, `SpiritProcessor`, `SpiritReceiver*`, `Incubator`, `Spam`, `FeedingTube`, `AiTarget`, `GlobalWorld*` |

The triangle budget of the reusable set is a few thousand in total; the largest, `Generator.shp`, is 2,376 triangles and `Refinery.shp` 1,152. Everything is well inside what an instanced flat-shaded pass draws without thought.

### Textures, sprites, icons

| Item | Disposition |
|---|---|
| `Terrain/Landscape*.bmp` (8 palettes), `Water*.bmp` (3), `Waves*.bmp` (7) | Take: they are the terrain look |
| `Textures/Particle`, `Glow`, `CloudyGlow`, `Starburst`, `MuzzleFlash`, `Laser`, `LaserFence*`, `RadarSignal`, `TriangleOutline`, `ShapeWireframe`, `SkyWireframe`, `Clouds`, `Deform*`, `GodRay`, `Fuel` | Take, as effect sprites and sky |
| `Textures/Interface*` | Take as reference for the UI look |
| `Textures/SpeccyFont*` (4), `EditorFont*` (6) | Owner's call, and Q16 puts it to the owner. Rendering the glyphs settles what they are: `SpeccyFont` is the Sinclair ZX Spectrum character set at twice its size, and `EditorFont` is a distinct, bolder pixel face with no sign of a third-party origin. Neither is a legal obstacle — a bitmap typeface design is not copyrightable in the United States, the United Kingdom's design right on a 1982 typeface expired decades ago, and Amstrad has long permitted redistribution of the Spectrum ROM for emulation — but the Spectrum font is the house face of every Introversion game, and with Darwinia's models and palettes beside it this game would read as one of theirs. That is a question of identity, not of law, and it is the owner's |
| `Textures/IvLogo`, `MsnOberonComboSplash`, `DmaCrew`, `ProgramDarwinia`, `Campaign`, `Prologue`, `SpeccyScreen` | **Never**: branding and Darwinia's campaign art |
| `Sprites/Citizen`, `LaserTrooper`, `Egg`, `Ghost`, `Virii`, `SantaHat`, `Sound` | `Citizen` and `LaserTrooper` are the infantry-scale sprite reference; the rest are not for this game |
| `Icons/Mouse*` (10 cursors), `Compass`, `ScrollBar`, `SelectionArrow`, `Background` | Take |
| `Icons/Gesture*`, `Icon*` (program icons), `Banner*`, `DarwinResearchAssociates` | Leave: Darwinia's task-manager vocabulary and branding |

---

## 5. Sound

**The library is 1,332 files and 500.9 MB, all 16-bit mono 44.1 kHz PCM** (*measured*). By size:

| Bucket | Files | Size |
|---|---|---|
| Under 64 KB | 394 | 14.4 MB |
| 64–256 KB | 498 | 73.7 MB |
| 256 KB–1 MB | 376 | 157.3 MB |
| 1–4 MB | 53 | 95.3 MB |
| 4 MB and over | 11 | 160.3 MB |

The eleven largest are the six soundtrack tracks (126.9 MB), the Spectrum tape loader, `Pang`, `TwoAtaris`, `Altitude1` and `Evil`, none of which is an effect. The 1–4 MB bucket is ambience loops, the `Theramin` and `High` drones, crate and spawn-point stingers — Darwinia set dressing.

**What this game would take** is the short effects: weapons (`ABlaster`, the laser and rocket sets), explosions, engine and hover loops (`TankHover`), construction and power-up stings (`GeneratorOnline` and `PowerStationOnline` are 2 MB each and would be cut down), damage and death sets, interface clicks. `Sounds.txt` references 163 distinct sample names across 109 sample groups, which is the natural first selection: the effects Darwinia actually wires to events. Estimate: 150–250 files, 20–40 MB of PCM, becoming 3–6 MB after MS-ADPCM at 22.05 kHz — the budget `Design/TechnicalDesign.md` §8 works from. Arithmetic, not measurement; the baker will measure.

**`Sounds.txt` itself is worth more than the samples.** Its model — an event per (object kind, event name) naming a sample group, a source type, a position type, an instance and a loop type, a minimum distance, and volume and frequency as parameter curves (`TypeFixedValue`, `TypeRangedRandom`, updated constantly or once per loop) — is a complete, proven design for a game's sound events, and it becomes a `constexpr` table in `Content`.

---

## 6. Data and documents

| Item | Disposition |
|---|---|
| `Stats.txt` | Reference only; nineteen rows of Darwinia numbers |
| The `Landscape_StartDefinition` and `LandscapeTiles_StartDefinition` blocks of `Levels/Map*.txt` | Reference for the landscape definition and the stamp format. The Garden's seven tiles are a worked example of how a designer shaped a fractal landscape |
| `Language/*.txt` | Leave |
| `Game.txt`, `GameUnlockAll.txt`, `Locations.txt`, the missions and scripts | Leave |
| `docs/ARCHITECTURE.md`, *Input* and *Runtime model* | Take as specification (`Design/TechnicalDesign.md` §6.5 and §5) |
| `docs/TESTING.md` | Take its rules: a test never reads `GameData/` in place, never writes into the source tree, and never opens a socket, a window or an audio device |
| `tasks/_openworld-prompt.md` | Read for the questions it asks, which are Q1's questions; its answers were for a persistent world, which this game is not unless Q1 says so |
| `AGENTS.md`, *What working looks like* (the Garden run) | Take the practice: presentation work is done when the owner has run it, not when CI is green — which `AGENTS.md` §3 here already says |

---

## 7. Lessons Species paid for

Recorded here so this tree does not pay for them again. Each is in the Species `AGENTS.md` or `docs/ARCHITECTURE.md` with the task that found it.

1. **Two random streams, named.** Six places drew simulation state from the cosmetic generator; `determinism` T5 found them. This tree names both from the first line, and `Sim` cannot include the cosmetic one.
2. **A slot index is not an identity.** `WorldObjectId::m_index` on the wire, aliasing after reuse; the owner's recorded decision to replace it. This tree starts with generated ids.
3. **Float terrain generation changed shape across a compiler migration** (`directxmath-migration` T13, unexplained). This tree generates in integers.
4. **The message pump ran twice a frame down two paths that did not know about each other.** One pump, one place.
5. **Consumption is decided by what the press did, not by a fresh lookup at release.** The router rules of `Design/TechnicalDesign.md` §6.5.
6. **A layering allowlist grows to 628 entries and then has to be deleted.** No allowlist, ever; an upward include fails.
7. **vstest reports "no tests found" as a pass.** `AGENTS.md` here already carries the `SuiteSmoke` rule.
8. **An enumerator nothing can produce holds up 1,400 lines.** `InputMode::GAMEPAD` and the whole control-help overlay behind it. A controller is an event source, not a mode.
9. **Renaming a name that content spells is a content change.** Species freezes its domain names until the game runs again. This tree has no content that spells a code name — tables are code — which is one more thing compiled-in content buys.
