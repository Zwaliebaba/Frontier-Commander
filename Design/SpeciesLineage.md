# Species Lineage — what comes across, and what does not

**Status: ACCEPTED (2026-09-17).** An inventory of the Species repository as read on 2026-09-17 at commit `d1add55`, with a disposition for each part: port, take the design, take the data, or leave. **Decisions (owner, 2026-09-17): the models will be made new; the rest of the Species-derived content is used, with the provenance risk accepted, distribution to other players and inside mods included (§1).** The Species meshes are therefore reference for scale and style only, and what carries is the configuration around them, written up in [`SpeciesLook.md`](SpeciesLook.md), [`SpeciesTerrain.md`](SpeciesTerrain.md) and [`SpeciesCanvas.md`](SpeciesCanvas.md). Paths in this document are relative to the Species repository unless they start with `Design/`. Figures marked *measured* were counted in that tree with the commands noted; nothing was built or run. `AGENTS.md` is explicit that a decision taken in a sibling tree binds nothing here — this document is about what is worth carrying, not what is inherited by default.

---

## 1. Provenance, first

**Species is Darwinia.** Its `README.md` says so in terms: the codebase began as the Darwinia source by Introversion Software, and *"the licence covering the original source has not been established, so treat the provenance as unresolved rather than permissive."* Its `LICENSE` file, deleted in commit `eec3992`, described Species as an internal research project, not licensed for commercial use and not for distribution, and carried a *Provenance* section saying the same thing the README still says; commit `6fb1246` removed the README's licence section that pointed at it. The situation is not that the licence is permissive but that the project's own terms are gone and the original's were never established. The shapes, textures, sprites, sounds and level files under `GameData/` are Darwinia's art, and the code in `GameLogic/` is Darwinia's game.

`AGENTS.md` R14 already has the rule this falls under: third-party content compiled in is the owner's question, needs the owner's approval before it lands, and the licence text travels with the bytes. This document therefore sorts the content into three bins; the owner decided the middle one on 2026-09-17:

- **Never**, whatever the owner decides about the rest, because it is licensed to Introversion from someone else or is Introversion's identity: the six soundtrack tracks (Tresk, Trash80, DMA-SC — 126.9 MB, *measured*), the Introversion and publisher logos and splash screens (`IvLogo.bmp`, `MsnOberonComboSplash.bmp`, `DmaCrew.bmp`, `ProgramDarwinia.bmp`, `DarwinResearchAssociates.bmp`), and the Sepulveda narration.
- **Used, with the risk accepted (owner, 2026-09-17)**: Darwinia's effect sounds, terrain palettes, sprites, icons and fonts, and the Darwinia-derived code. The recommendation was to treat them as placeholders with a replacement plan; the owner chose to use them and to carry the provenance risk, and confirmed the same day that the acceptance covers handing them to other players in M3 and inside mods, against a recommendation to replace them before M3. ADR-006 records the decision in those terms and lists what came across. The fonts are the one item in this bin whose provenance is known rather than unresolved, and §4 says what it is.
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
| `Shapes/` | 106 files, 301 fragments, 29,637 positions, 53,193 triangles; 75 files carry markers, none carries normals; 40 files encode their triangles as strips | 2.4 MB as text | Text `.shp`: per fragment a transform, a position table, a colour table, vertices as (position, colour) pairs, triangles; named markers |
| `Textures/` | 37 | 3.7 MB, of which 2.5 MB is two splash screens | 24-bit and 8-bit BMP; ten 8-bit font bitmaps at 256×224 (one at 256×208) |
| `Terrain/` | 18 | 276 KB | Eight 64×64×24 landscape palettes, three 128×128×8 water textures, seven wave textures |
| `Sprites/` | 7 | 44 KB | 32×32×24 BMP |
| `Icons/` | 45 | 940 KB | 128×128×8 BMP |
| `Sounds/` | 1,332 | 500.9 MB (481 MiB) | Every one 16-bit mono 44.1 kHz PCM WAV |
| `Levels/` | 29 | 228 KB | 12 map files and 17 mission files, text |
| `Language/` | 6 | 728 KB | Phrase tables; English is 126 KB |
| `Sounds.txt` | 16 entity, 29 building and 10 other blocks; 109 sample groups; 163 distinct sound names | | The event-to-sample model |
| `Stats.txt` | 19 rows | | Health, speed and rate of fire per entity type |

The `.shp` format has two triangle encodings, `Triangles:` lists and `Strips:` triangle strips, and 40 of the 106 files use strips only. An earlier draft of this table counted only the lists and reported 32,253 triangles and five geometry-free files; the figure above comes from a parser that handles both, as `Shape.cpp` does, and the one genuinely geometry-free file is `BattleCannonBase.shp`: eight markers laying out the ports and status lights of the assembled cannon.

---

## 3. Code

Disposition per module. *Port* means the code moves, renamed to `AGENTS.md` §1 and reformatted by `.clang-format`, and it is chosen only where the code is Species' own work and the design here is the same. *Design* means the shape and the rules come across and the code is rewritten. *Leave* means leave.

### NeuronCore

| Module | Disposition | Why |
|---|---|---|
| `SlotMap`, `FastSlotMap` | Design | The narrow handle-in, reference-out API is right; the two flavours exist to reproduce Darwinia's legacy index assignment, which this game does not need. One slot map with generation-checked handles, and ids that are not indices (`Design/TechnicalDesign.md` §4.3) |
| `SliceWalker` | Leave | Slicing a frame into ten was Darwinia's way of spreading work; budgeted systems (`Design/TechnicalDesign.md` §4.5, §4.6) replace it |
| `Transport`, `UdpTransport`, `LoopbackTransport`, `UdpSocket` | Port | Species' own work from `network-transport` T7–T11: the seam, the bounded reads, the polled socket, the loopback for tests. The best-tested networking in that tree |
| `NetworkUpdate`, `ServerToClientLetter`, `ByteStream`, `ProtocolLimits`, `TeamControls` | Design, partly | The framing, versioning and sequence discipline come across as design; the 42-byte fixed packet, the thirteen update kinds, `NUM_TEAMS 4` and the Darwinia vocabulary (`RunProgram`, `AimBuilding`) do not. `Design/TechnicalDesign.md` §5.3 is the replacement |
| `MathUtils` (`syncrand`, a Mersenne Twister), `Random` (an LCG) | Design | The two-streams rule, named and enforced. The generators are replaced (`Design/TechnicalDesign.md` §4.2) |
| `NeuronMath` and the DirectXMath conventions | Port, renderer only | DirectXMath is SDK content and R14 allows it; it belongs on the render side and never in `Sim` |
| `Debug` (`ASSERT`, `DebugTrace`, `Fatal`), `NeuronHelper` (`NonCopyable`, `ScopedHandle`) | Port | Small, Species-style, and exactly what `Core` needs first |
| `FileSys`, `Preferences`, `Profiler`, `HiResTime`, `GameTime` | Design | Content and user files live in directories resolved from the executable's path and the user's profile (`Design/TechnicalDesign.md` §8, §9), which is `FileSys`'s job reshaped; preferences become a JSON file; timing is the tick |
| `LookupTable`, `VectorUtils`, `2dArray` | Port where used | Utility |
| `WorldObjectId` | Leave | A slot index on the wire is the design this game is explicitly not repeating |

### NeuronClient

| Module | Disposition | Why |
|---|---|---|
| The input system: `Input`, `InputDriverWin32`, `InputEvents`, `InputRouter`, the `InputDriver*` binding stack, `TargetCursor`, `KeyDefs`, `KeyNames` | Port | Species' own work (`input-native-events`); `Design/TechnicalDesign.md` §6.5 takes its rules as the specification. The rebinding stack and its preferences syntax are worth keeping |
| `SoundSystem`, `SoundInstance`, `SoundParameter`, `SoundLibrary3d`, `SoundLibraryXAudio2`, `SoundStreamDecoder`, `SampleCache` | Port | Species' own `sound-xaudio2` work: one XAudio2 backend, X3DAudio, device-loss recovery. The WAV loader and the sample cache stay file loaders, reading `Content\Sounds` |
| `Eclipse`, `EclWindow`, `EclButton`, `InputField`, `ScrollBar`, `DropDownMenu` | Design | The windowing model and the input-first routing are right; the drawing is OpenGL immediate mode and Darwinia-derived |
| `Shape`, `ShapeFragment`, `ShapeMarker` | Design; the format is the reference | Models are JSON files under `Content\Models` holding the same records — positions, colours, vertices, triangles from either encoding, markers, a fragment tree — and `Tools/ImportShp.py` converts a `.shp` into one. The marker concept — a named attachment point with a transform in the fragment tree — is kept |
| `TextRenderer` | Design | Bitmap-font quads; rewritten for D3D12 and a new atlas |
| `Bitmap` (the 4-, 8- and 24-bit BMP reader), `Resource` (a name-keyed cache of loaded bitmaps, sounds and shapes) | Design | Content is files, and these are the two pieces of Species that load them; the BMP reader's knowledge goes into `Tools/ImportTextures.py`, because textures are DDS in this game (owner, 2026-09-17) and the runtime reads nothing else |
| `Texture`, `OGLExtensions`, `RenderUtils`, `SphereRenderer`, `3dSprite`, `GlVertex`, `2dSurfaceMap` | Leave | OpenGL |
| `WindowManagerWin32`, `Win32EventHandler` | Design | A borderless window that owns Escape and Alt+F4 (`AGENTS.md` §5) is a different window; the message-pump-once rule comes across |
| `ClientToServer` | Design | The client endpoint of Species' lockstep conversation; here the client endpoint receives state frames and sends orders (`Design/TechnicalDesign.md` §5) |
| `LanguageTable` and the `_kbd` phrase mechanism | Design, later | A phrase keyed by whether it names a binding is a good idea; localisation is not in the first version |
| `SystemInfo`, `UserInfo`, `FilePaths`, `FilesysUtils`, `FileWriter` | Leave | |
| The `*Access` interfaces (`RendererAccess`, `CameraAccess`, `LocationAccess`, …) | Leave | The dependency inversions Species needed to unpick Darwinia's single binary; this tree's layering never has the problem |

### NeuronServer

| Module | Disposition | Why |
|---|---|---|
| `Server`, `ServerToClient` | Design | Client registry, sequence counter, per-sequence sync values, history pruning, server-assigned ids, liveness: the host endpoint of `Design/TechnicalDesign.md` §5 keeps the registry, the ids and the liveness and replaces the sequenced-intent broadcast with per-client state frames, the owner having chosen replication over lockstep (2026-09-17) |

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
| Everything else — `Citizen`, `Engineer`, `Officer`, `Spirit*`, `Virii`, `Centipede`, `SoulDestroyer`, `Spider`, `ArmyAnt`, `AntHill`, `Triffid`, `Incubator`, `TrunkPort`, `TaskManager`, `GlobalWorld`, `Ai`, the in-game windows | Leave | Darwinia's mechanics, Darwinia's fiction, Darwinia's code. The neutral faction comes in M4 (owner, 2026-09-17), designed fresh with these as a mood board |

### Tests and tools

| Item | Disposition | Why |
|---|---|---|
| `Tests/NeuronCoreTests` (protocol encodings, the transport conversation over loopback), `Tests/NeuronClientTests` (input derivation) | Port with their subjects | Tests move with the code they cover |
| `tools/check_layering.py` | Port | An upward include fails, there is no allowlist, and a symbol declared low and defined high is caught too. `AGENTS.md` §6 names three checkers; this is the fourth, and it is the one that protects §2 of `Design/TechnicalDesign.md` |
| `tools/check_format.py`, `tools/check_project_files.py` | Design | `AGENTS.md` names their equivalents under `Build/`; the Species versions check changed lines only, which a tree formatted from the first line does not need |
| `tools/check_task_dag.py` and `docs/TASK_DAG.md` | Owner's choice | A plan-as-DAG discipline for agentic work; `AGENTS.md` here neither has it nor forbids it |

---

## 4. Art

Every model and bitmap below was rendered and looked at on 2026-09-17 with a review tool written for the purpose: a parser matching `NeuronClient/Shape.cpp` (both triangle encodings, the fragment hierarchy, the basis normalisation), a flat-shaded software rasteriser, and labelled contact sheets. Extents are the world-space bounding box of the rendered geometry, width × height × depth in Species world units, *measured*. Every item came across under the owner's decision of 2026-09-17 (§1), which ADR-006 records.

### Scale, before anything else

**The Species models are ten times larger than the first draft of `GameDesign.md` assumed.** A soldier (`Squad.shp`) is 14 units tall, a tank body 49 long, a wall segment 46 wide, a power station 82 × 74 × 103, a generator 192 across, and the largest set-piece (`ConstructionYard.shp`) 401 × 255 × 529. The draft had set a cell at 4 world units and structure footprints at one to three cells, which would have made a power station 25 cells wide. `GameDesign.md` §3 now defines the world unit as the Species unit and a cell as 64 of them — one tank, one wall segment — so the models import at their native scale, with a per-model factor in the content table for the few that need one. That is the single most useful thing the review found.

### Shapes

**The owner will make new models, so this table is reference, not a plan.** It stays because it records the scale and the polygon budget the new models should match, and because the fit column says which Species shapes are worth a look while the new ones are being made. Grouped by what they can stand in for, with the measured triangle count and extent. *Fit* is how well a model reads as the thing without rework: **direct** means it does; **with work** means it needs scaling, recolouring or a part removed; **weak** means the name promises more than the geometry delivers.

| For | Shape | Triangles | Extent (W×H×D) | Fit | Seen |
|---|---|---|---|---|---|
| Command post | `BlueprintConsole` | 460 | 69×79×69 | direct | A blue many-legged console with a dish on top; reads as a headquarters |
| | `ControlTower` | 308 | 32×49×26 | direct | A light-blue tower with an antenna; small; also a sensor tower |
| | `ControlPad` | 264 | 39×32×34 | with work | A sloped console with a hexagonal screen |
| | `DisplayScreen` | 84 | 94×43×84 | with work | A large sloped screen; base decoration |
| Extractor | `FuelPipeBase` | 96 | 48×97×41 | direct | A hexagonal base with a pipe rising from it: a wellhead |
| | `FuelGeneratorPump` | 126 | 28×230×28 | with work | A tall pump rod; the extractor's moving part |
| | `Mine` | 616 | 143×86×74 | with work | A cluster of sheds and cranes; needs scaling for a small footprint |
| Generator | `PowerStation` | 1,248 | 82×74×103 | direct | Cooling towers and red chimneys; the best structure in the set |
| | `Generator` | 2,376 | 192×151×178 | with work | A spiked machine with two rotors; large and busy |
| | `SolarPanel` | 648 | 92×120×42 | direct | A panel on a pole; a lighter alternative |
| | `FuelGenerator` | 252 | 129×178×123 | with work | A dark industrial block |
| Factory | `Refinery` | 1,152 | 273×140×155 | with work | A pink hall with cranes; a factory at half scale |
| | `Incubator` | 376 | 57×55×95 | with work | An angular grey building; usable at native scale |
| | `Factory` | 1,364 | 29×25×18 | weak | Two red pillars with blue rings and a yellow bud; does not read as a factory, and the name misleads |
| Research lab | `BlueprintStore` | 704 | 83×41×82 | direct | A hub with three dish petals |
| | `Library` | 800 | 70×23×70 | direct | A ring with spikes |
| | `ResearchItem` | 784 | 17×17×17 | direct | A lattice cube: a research crate |
| Repair bay | `UpgradePort` | 636 | 72×31×82 | direct | A wide platform with a spiral fixture: a pad |
| Sensor tower | `RadarDish` | 248 | 36×51×35 | direct | A dish on a blocky base |
| | `ControlTowerDish` | 156 | 14×12×6 | direct | The dish alone |
| Uplink | `BlueprintRelay` | 400 | 42×38×79 | direct | A satellite with two solar wings |
| | `TrunkPort` | 292 | 123×147×23 | with work | A blue ring portal; a gate |
| Hardpoint, tower | `GunTurret` | 292 | 17×11×8 | direct | A twin-barrel turret on a base; the classic |
| | `TurretBase`, `TurretBarrel` | 320, 132 | 45×27×42, 21×10×22 | direct | A crown base and a quad-barrel assembly; `TurretBase` is the same geometry as `SpiritReceiver` |
| | `FenceSwitch` | 240 | 49×60×47 | with work | A pointed blue tower with petals |
| Bunker, artillery | `FieldGun` | 288 | 79×58×91 | direct | A star-footed emplacement with a long barrel |
| | `BattleCannonFull`, `BattleCannonTurret`, `BattleCannonBarrel` | 168, 88, 80 | 31×33×27 | with work | A blocky heavy cannon in rough grey, unfinished-looking; `BattleCannonBase` is eight markers and no geometry |
| Wall | `Wall` | 132 | 46×54×6 | direct | One dark panel, one cell wide |
| | `LaserFence` | 54 | 10×77×22 | direct | A post with a bent head: a fence post or a light |
| Power lines, pipelines | `Pylon`, `FuelPipe`, `FuelStation`, `TrackLink`, `ReceiverLink`, `SpawnLink` | 96, 72, 344, 53, 72, 152 | 10×140×38 and similar | direct | Poles, pipes, a branching junction and a gantry: the generator-to-extractor link made visible |
| Device chassis | `TankBody` | 158 | 49×12×49 | direct | A flat angular hover tank in green, one cell long; the green is the team-colour slot |
| | `Armour` | 96 | 29×15×32 | direct | A boxy transport body |
| | `Lander` | 24 | 14×12×29 | with work | A plain wedge |
| | `Wheel` | 288 | 51×51×19 | with work | A spoked wheel at building scale; scaled down it is a wheel |
| Device modules | `TankTurret` | 174 | 14×15×36 | direct | A turret with a long barrel, matched to `TankBody` |
| Infantry | `Squad` | 1,036 | 8×14×8 | direct | A proper three-dimensional soldier with a rifle: the only humanoid model, and the reason infantry need not be sprites |
| Projectiles | `Missile`, `FieldGunShell`, `Throwable`, `TurretShell` | 120, 286, 286, 8 | 7×7×20 and smaller | direct | A finned missile, two bombs, an octahedral shell |
| Features | `Temple1`–`Temple4` | 156, 128, 236, 128 | 110×111×39 and smaller | direct | Arches and pillars: ruins |
| | `RockHead` | 540 | 60×156×63 | direct | A stone head |
| | `Cave` | 60 | 12×15×23 | direct | A cave mouth |
| | `Plant` | 192 | 31×53×19 | direct | A rose: biome dressing |
| | `Ark`, `Rocket` | 136, 4,992 | 110×202×105, 167×297×171 | direct | A lander tower and a rocket: stamp centrepieces |
| | `ConstructionYard`, `ConstructionYardRung`, `PrimaryUpgradePort`, `MasterSpawnPoint` | 270, 204, 636, 180 | up to 401×255×529 | direct | Platforms and rings at set-piece scale: stamp centrepieces |
| | `BridgeEnd`, `BridgeTower`, `FeedingTube`, `SpiritProcessor`, `SpawnPoint`, `AntHill` | 76, 20, 368, 432, 152, 396 | | with work | A gate, a bollard, a ring on a stand, a lamp on a rock, an angular pod, two termite mounds — the last a nest for the neutral faction |
| Markers | `AiTarget` | 84 | 16×30×2 | direct | A red flag on a pole: a rally point |
| Not for this game | `ArmyAnt*`, `Centipede*`, `SoulDestroyer*`, `Spider*`, `Tripod*`, `Triffid*`, `SporeGenerator`, `Spam`, `FlyingEgg`, `SpaceInvader` | | | | The creatures; a mood board for the M4 neutral faction |
| | `Citizen`, `Engineer`, `Officer`, `LaserTroop` | 76, 72, 58, 392 | 7×9×1 and similar | | Flat cross-shaped or wedge figures; the sprites do the same job better |
| | `GlobalWorldInner`, `GlobalWorldMiddle`, `GlobalWorldOuter`, `Camera`, `Help`, `GoldenScroll`, `GarbageCollector`, `BoxKite`, `MinePolygon1`, `MinePrimitive1`, `MineCart`, `GodDish`, `SpiritReceiver`, `SpiritReceiverHead` | | | | The campaign globe (21,654 triangles between them), editor gizmos and primitives, Darwinia's spirit machinery |

Two things the sheets make plain that the names did not. The set is strongest in industrial structures, defences and set-pieces and weakest exactly where the vertical slice needs it most: there is one tank, one turret and one soldier, and the model named `Factory` is not one. And the models were made for a game with no grid, so footprints are the content's problem: `PowerStation` sits in 2×2 cells at native scale, `Refinery` needs 0.6× to fit 3×3, `Mine` 0.45× to fit 1×1, and a per-model scale in `Structures.json` is the mechanism.

### Textures, sprites, icons

Magenta (255, 0, 255) is the colour key throughout; the texture loader turns it into alpha.

| Item | Seen | Disposition |
|---|---|---|
| `Terrain/Landscape*.bmp` (8, 64×64×24) | Colour ramps: Default is the blue-green-white Darwinia is known for, Desert sand, Earth green and white, Icecaps white and grey, Launchpad teal, Mine purple and blue, Mine2 dark green, Containment blue-green | Take: eight biomes |
| `Terrain/Water*.bmp` (3, 128×128×8) | Caustic patterns; Default is Darwinia's pink water, Icecaps and Launchpad blue | Take |
| `Terrain/Waves*.bmp` (7, 400×10×24) | Horizontal colour ramps for the shoreline | Take |
| `Textures/InterfaceGrey`, `InterfaceRed` (64×512×8), `InterfaceDivider` (16×32) | Vertical gradients: the Eclipse window background and its red variant | Take: they are the interface look |
| `Textures/Glow`, `CloudyGlow`, `Fuel`, `Starburst`, `MuzzleFlash`, `RadarSignal`, `Laser`, `LaserFence`, `LaserFence2`, `GodRay`, `Particle` | Soft blobs, a beam, a streak, a cloud, a 16×16 grey square | Take: effect sprites |
| `Textures/ShapeWireframe`, `SkyWireframe`, `TriangleOutline`, `Clouds` | A diagonal-line tile, a bordered black square, a triangle-outline tile, a 16×16 noise mask | Take: the wireframe overlay and the sky are part of the look |
| `Textures/Deform1c`, `Deform36c` | Distortion maps for shockwaves | Take |
| `Textures/EditorFont*` (6), `SpeccyFont*` (4) | Two pixel fonts, each with accented variants | The Spectrum font is the game's (owner, 2026-09-17); §4 below says what both are |
| `Textures/IvLogo`, `MsnOberonComboSplash`, `DmaCrew`, `SpeccyScreen`, `ProgramDarwinia`, `Campaign`, `Prologue` | Logos, a publisher splash, the Darwinia loading screen, campaign paintings | **Never**: branding and Darwinia's campaign art |
| `Sprites/Citizen`, `LaserTrooper` (32×32×24) | Stick figures, magenta-keyed, the trooper with a gun: the Species population | Take: the population is confirmed as a visual (owner, 2026-09-17) |
| `Sprites/Virii`, `Egg`, `Ghost`, `SantaHat`, `Sound` | A triangle, an egg, a ghost figure, a hat, an editor speaker icon | Leave |
| `Icons/Banner*` (6, 64×64) | Order glyphs on a blue field: gather, deploy, follow, go to, none, unload | Take: order icons, nearly as they are |
| `Icons/Icon*` (15, 128×128×8) | White glyphs on dark-blue discs: Darwinia's programs | Take the style and the generic glyphs (`Delete`, `NoTask`, `Rocket`, `Grenade`, `Laser`, `Shadow`); leave the rest |
| `Icons/Mouse*` (9, 128×128), `SelectionArrow`, `ScrollBar`, `Compass`, `Background` | Pointer, placement, selection corners, move-here, turret and missile reticles, disabled, highlight | Take |
| `Icons/Gesture*` (10), `DarwinResearchAssociates` | Mouse-gesture strokes; a Vitruvian-citizen logo | Leave |

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

**What this game would take** is the short effects: weapons (`ABlaster`, the laser and rocket sets), explosions, engine and hover loops (`TankHover`), construction and power-up stings (`GeneratorOnline` and `PowerStationOnline` are 2 MB each and would be cut down), damage and death sets, interface clicks. `Sounds.txt` references 163 distinct sample names across 109 sample groups, which is the natural first selection: the effects Darwinia actually wires to events. Estimate: 150–250 files and 20–40 MB of PCM as WAV files under `Content\Sounds`, a directory rather than a budget now that content is files (`Design/TechnicalDesign.md` §8); MS-ADPCM at 22.05 kHz would cut it to 3–6 MB if the install size ever matters. Arithmetic, not measurement.

**`Sounds.txt` itself is worth more than the samples.** Its model — an event per (object kind, event name) naming a sample group, a source type, a position type, an instance and a loop type, a minimum distance, and volume and frequency as parameter curves (`TypeFixedValue`, `TypeRangedRandom`, updated constantly or once per loop) — is a complete, proven design for a game's sound events, and it becomes `Content\Sounds.json`.

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
| `tasks/_openworld-prompt.md` | Read for the questions it asks; its answers were for a persistent world, which this game is not (owner, 2026-09-17: match-based) |
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
9. **Renaming a name that content spells is a content change.** Species freezes its domain names until the game runs again. This tree's content is files too, so the same rule applies from the first JSON file: a name a content file spells is renamed in one commit with the files that spell it, and `FrontierHost --validate` in CI is what catches the one that was missed.
