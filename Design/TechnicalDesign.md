# Frontier Commander — Technical Design

**Status: DRAFT (2026-09-17).** A proposal, not authority (see [`README.md`](README.md)). It assumes [`AGENTS.md`](../AGENTS.md) has been read and cites its rules by number rather than repeating them. Where it proposes something an `AGENTS.md` rule forbids or leaves open, it says so, and the decision goes to the owner through [`OpenQuestions.md`](OpenQuestions.md) and then into an ADR. Figures here are arithmetic on stated assumptions, not measurements, and are labelled so: nothing in this repository has been built or run, and this document was written on Linux where nothing could be.

---

## 1. What the rules already decide

Five `AGENTS.md` rules shape everything below, and this document does not re-argue them:

- **R12** — Direct3D 12 only, COM lifetimes RAII, one scene target presented scaled.
- **R13** — the executable ships alone; content is compiled in; every file the host writes is an ADR.
- **R14** — the Windows SDK and the MSVC standard library, and nothing else. No Agility SDK, no `d3dx12.h`, no DirectXTK, no shader compiler at runtime.
- **R16** — the simulation is deterministic and holds no floats: integers and fixed point, a pinned PRNG, the tick as the only clock. Floats live in the renderer.
- **§2** — flat project directories, one-way edges, project files as source.

What they leave open, and this document proposes: the projects, the simulation's representation, the network model, the renderer's shape, the content pipeline, and which files the game may write.

---

## 2. Projects and layers

Seven projects, one solution at the root, `x64` only, toolset `v145`, as `AGENTS.md` §3 requires. The names are proposals (Q12). Arrows point at what a project is built on; every arrow points downward and none points sideways.

```
            ┌────────────────────────────────────────────────┐
            │  FrontierCommander (exe)      FrontierHost (exe)│
            └───────┬──────────┬──────────────────────┬───────┘
                    │          │                      │
            ┌───────▼───┐  ┌───▼────┐                 │
            │  Client   │  │  Net   │◄────────────────┘      FrontierHost builds on
            └───────┬───┘  └───┬────┘                        Net, Sim, Content, Core
                    │          │                             and never on Client
                    │      ┌───▼────┐
                    │      │  Sim   │
                    │      └───┬────┘
                    │          │
            ┌───────▼──────────▼────┐
            │        Content        │
            └───────────┬───────────┘
                        │
            ┌───────────▼───────────┐
            │         Core          │
            └───────────────────────┘
```

| Project | Kind | Holds | Built on |
|---|---|---|---|
| `Core` | static lib | Fixed-point math, binary angles, integer geometry, the PRNG, hashing, slot maps, byte-stream reader and writer, assertions, the one header that owns the Windows macro family | nothing |
| `Content` | static lib | The component tables, the research tree, the structure catalogue, the damage matrix, the sound-event table, and the baked models, palettes, sprites, icons, font, sounds and stamps | `Core` |
| `Sim` | static lib | The landscape, the world, orders and their validation, economy, research, production, movement and pathing, visibility, combat, the AI, the tick, the hash, snapshots | `Core`, `Content` |
| `Net` | static lib | The UDP transport and its loopback twin, the lockstep protocol, the host and client endpoints, snapshot transfer | `Core`, `Sim` |
| `Client` | static lib | The Direct3D 12 renderer, the window, the input event system, XAudio2, the UI toolkit | `Core`, `Content` |
| `FrontierCommander` | exe | The game: application, main loop, camera, HUD, the translation from simulation state to render view | everything |
| `FrontierHost` | exe | The headless host: opens a match, sequences orders, runs the authoritative simulation | `Core`, `Content`, `Sim`, `Net` |

**The edges, and why each runs the way it does.**

- **`Sim` includes no Windows header, no D3D header and no socket.** That is what makes a headless host possible and the simulation testable without a window, and it is the edge the layering checker must guard hardest, because it is the one a convenience include breaks first.
- **`Content` holds data, not behaviour.** The row types (`ChassisDesc`, `ResearchItemDesc`, …) are plain aggregates (R8), the tables are `constexpr` arrays of them, and the `consteval` checks that validate the tables live beside them (§8). `Sim` reads `Content`; `Content` knows nothing of `Sim`.
- **`Net` builds on `Sim`** for the order and snapshot types it carries, and never on `Client`. That is the edge Species had to fight for over eighteen tasks; this tree starts with it.
- **`Client` does not build on `Sim`.** The executable builds a **render view** from the simulation each frame — a plain list of what to draw — and `Client` draws it, so nothing in `Client` names a `Device` or a `Structure`. That is R9's "the engine does not know the game" made structural rather than aspirational, and it is what lets the renderer be tested and replaced on its own.
- **The executables hold what is genuinely theirs**: the frame loop, the camera, the HUD, the render-view translation. Code in an executable cannot be linked into a test DLL, so anything in one that deserves a test is code that belongs in a library.

**Namespaces**, per R9: `Frontier` for the game (`Sim`, `Content`, both executables) and one engine namespace for `Core`, `Client` and `Net`. The engine name is Q12; the case for reusing `Neuron` is that the engine code this tree ports from Species then moves without a rename, which `AGENTS.md` names as the reason the formatter settings were carried over in the first place.

**Tests** are one `Tests/<Name>Tests` project per static library on the Microsoft Native Unit Test Framework, each sitting directly above its library with the same edges. `Sim` tests are the bulk of the suite (§10).

**`Build/` and `Tools/`** hold Python that never ships: the three checkers `AGENTS.md` §6 names, a layering checker in the Species mould (an upward include fails, and there is no allowlist), and the bakers of §8.

---

## 3. The frame and the tick

**The simulation ticks at a fixed rate; the renderer runs at the display's.** Proposed: **20 ticks per second**, 50 ms each, settled by ADR-003 once there is a build to measure. Species inherited 10 Hz from Darwinia and spread heavy work over ten slices. Twenty is proposed because orders in a strategy game feel late at 100 ms of tick quantisation on top of network delay, and because a 50 ms budget on a 2026 desktop for the object counts below is generous.

**The main loop** on the client, per frame:

```
1  pump the window's messages into the input event queue     once per frame, nowhere else
2  drain the network: orders and acks in, our orders out
3  while simulation time trails wall time by a tick:
       Sim::Advance(tick)                                   0, 1 or several per frame
4  build the render view: positions interpolated between
   the last two ticks                                       the first float, renderer-side
5  route input events: UI first, then camera and selection
6  record, draw, present
```

Step 3 is the only place wall time and ticks meet (R16), and it is where the lockstep input delay is enforced: the simulation may not advance tick *t* until every commander's orders for *t* are in hand (§5). Step 4 is where floats begin. The headless host runs steps 2 and 3 and nothing else.

**Budget arithmetic.** Eight commanders at 300 devices is 2,400 devices; with structures, projectiles and a neutral faction, call it 4,000 simulated objects. At 50 ms per tick that is 12.5 µs per object per tick, which is thousands of instructions. Pathing and visibility are the systems that can spend it (§4.5, §4.6); everything else is inside it by orders of magnitude.

---

## 4. The simulation

Everything in `Sim`, in the order R16 forces.

### 4.1 Numbers

| Quantity | Representation | Range | Note |
|---|---|---|---|
| Position (x, z) | `std::int32_t` in 1/256 world unit | ±8.3 million world units | A Frontier landscape is 131,072 across; the range is not the constraint, the arithmetic is |
| Height (y) | `std::int32_t`, same unit | | Terrain samples and object heights share it |
| Velocity | position units per tick | | No per-second quantity ever enters the simulation; the tick is the unit |
| Angle | `std::uint16_t` binary angle | 65,536 per turn | `sin` and `cos` from a 1,024-entry integer table with linear interpolation, in `Core` |
| Distance | integer square root of the squared distance | | Compare squared distances wherever possible; take the root only where a range check needs it |
| Percentages, multipliers | `std::int32_t` hundredths | | `armorPercent`, `speedFactorHundredths` — the unit in the name (R6) |
| Time | ticks, `std::uint32_t` | 6.8 years at 20 Hz | Build times, cooldowns and research durations are tick counts in the tables |

The 1/256 subunit is chosen so that a cell of 64 world units is 16,384 subunits and cell arithmetic is a shift, and so that the slowest interesting speed — a heavy on tracks at 20 world units per second, a third of the Species `Armour` — is 256 subunits per tick, with room to scale by hundredths without rounding to zero. **A product of two positions overflows `int32`**: every such product is done in `std::int64_t`, and `Core` provides the fixed-point helpers (`MulDiv`, `Sqrt`, `Dot`, `LengthSquared`) so nobody writes the widening by hand twice.

### 4.2 Randomness

Two streams, named, in the Species manner, because Species spent a task finding six places that drew simulation state from the cosmetic one:

- **The simulation stream**, seeded from the match seed and advanced only by the simulation, in a fixed order. One generator per match, not per system; a per-system generator would be deterministic too, but it would make "how many draws happened this tick" a question nobody can answer.
- **The cosmetic stream**, for particles, jitter and sound variation, seeded from anything and never read by `Sim` — enforced by the layering: it lives in `Client`.

The generator is a small, well-known algorithm written into `Core` from its specification. xoshiro128\*\* is the recommendation: sixteen bytes of state, and its reference implementation is public domain, so R14's "the licence travels with the bytes" has nothing to carry. Mersenne Twister, which Species carries, is 2.5 KB of state that would go into every snapshot for no benefit.

### 4.3 Identity and storage

**An object's identity is a generated id, never a container index.** Species' owner recorded exactly this decision for Species' own future protocol on 2026-08-02 (the Species repository's `docs/ARCHITECTURE.md`, *Runtime model*): a slot index is reused, so a stale reference silently aliases whatever occupies the slot later; a counter incremented in creation order assigns the same ids on every machine and can never alias. Here it is the rule from the first line. An `ObjectId` is a `std::uint32_t` creation counter paired with a kind tag; a slot map in `Core` resolves it to storage in O(1) through a generation-checked handle, so a dead id resolves to nothing rather than to a stranger.

**Storage is a slot map per object kind** — devices, structures, projectiles, features, wrecks — with stable handles and **iteration in id order**. Standard containers by default (R15): a `std::vector` behind the slot map, and never a `std::unordered_map` in a loop whose order reaches the outcome (R16). Per-commander state — power, research, designs, visibility — is an array indexed by seat.

### 4.4 The landscape

**Heights are generated, integer, and identical everywhere.** The Species generator — diamond-square tiles with a fractal dimension, height scale and desired height each, merged into one map and smoothed under a guide grid — is ported from the Species repository's `GameLogic/Landscape.cpp` into integer arithmetic on the simulation stream. The Species code draws from the cosmetic LCG and computes its noise as `sfrand(powf(length × 10, fractalDimension))` in `float`; that is exactly what R16 forbids, and it is why Species records a landscape that changed shape across a compiler migration. The port replaces `powf` with a fixed-point table over the handful of fractal dimensions a landscape may use, and the LCG with the simulation stream keyed by tile seed. **The heightfield is simulation state**: pathing, slope, water and line of sight all read it, so it is under R16 without exception. The palette lookup that colours it (§6.4) is not, and stays in float on the renderer side.

**Storage.** Heights are sampled every 16 world units — four samples per cell edge, close to the Species spacing of 10.66 — and stored as `std::int16_t` whole units, which covers the height range of any Species landscape many times over. A Large landscape is 4,097 × 4,097 samples, 33.6 MB; a Frontier one 8,193 × 8,193, 134 MB, which is memory rather than a problem on the machines this game targets, and it is one of the numbers that make Frontier-class landscapes M4 work rather than M1. Derived grids are per cell, not per sample, each one byte or one bit: slope class, water, obstruction (a structure or feature occupies the cell), and per-commander visibility at two bits for its three states. Eight commanders on a Frontier landscape is 8 × 4.2 million cells × 2 bits = 8.4 MB of visibility. Arithmetic, not measurement.

**Stamps** are authored patches: a rectangle of relative heights and a list of features, applied after generation at a position the generator chooses. Their format is the same as a snapshot's landscape section, so the tool that authors one is the game with an editor window. **Terrain deltas at runtime** — flatten under a structure today, terraforming if Q8 ever says yes — are recorded as a list of rectangular height edits applied over the generated base, so a snapshot carries the seed plus the deltas rather than the heights, and the answer to Q8 can change without a new snapshot format.

### 4.5 Pathing

The system that decides whether a Large landscape works. The proposal is **hierarchical A\* over clusters with local steering**:

- The landscape is divided into 16×16-cell clusters; each cluster's boundary crossings are nodes, and the paths between them inside the cluster are precomputed edges, per drive class, because water and slope differ per class. A Large landscape has 4,096 clusters; a Frontier one 16,384.
- A move order plans over the cluster graph (thousands of nodes, not millions of cells), refines the first few clusters to a cell path, and refines further as the device advances. Planning is amortised over ticks with a budget per tick, and it is deterministic because the budget is in nodes expanded, not in milliseconds.
- Between cells, devices steer around each other with a separation rule in integer arithmetic; formations are a version-2 concern.
- A structure placed or destroyed invalidates the clusters it touches, which recompute their internal edges lazily.

Flow fields — one field per destination, shared by every device heading there — are the alternative and are better for hundred-unit blobs converging on one point; they are worse for a landscape this size because a field covers the map. The choice is an ADR when the slice has numbers; the interface `Sim` exposes (request a path, advance along it) is the same for both.

### 4.6 Visibility

Per commander, a grid of the three fog states. Each tick, a budgeted share of the devices and structures with sight recompute the cells they see: a disc of the sight radius, extended by the height difference, with each cell tested for occlusion by walking the heightfield from the viewer to the cell in integer steps. A viewer that has not moved and whose surroundings have not changed keeps its previous disc. The budget — proposed: every viewer refreshes at least once per second — is in viewers per tick, so it is deterministic.

The cost, as arithmetic: a sight radius of 24 cells is a disc of about 1,800 cells; an occlusion walk averages 12 steps; that is roughly 22,000 heightfield reads per viewer refresh, and 400 viewers refreshing once a second over 20 ticks is 440,000 reads per tick — well inside the budget of §3, and the reason the budget is in viewers rather than milliseconds.

### 4.7 Orders and validation

**Orders are the only input.** An order is a small, fixed-layout record: the commander's seat, the tick it is for, the kind, and up to four operands (ids, a position, a design id, a research id). Proposed kinds: `Move`, `AttackMove`, `Attack`, `Patrol`, `Guard`, `Stop`, `ReturnToRepair`, `SetStance`, `PlaceStructure`, `CancelStructure`, `Demolish`, `BuildModule`, `SetProduction`, `CancelProduction`, `SetResearch`, `CancelResearch`, `SaveDesign`, `Group`, `Surrender`, `Chat`. Twenty kinds, every one under 32 bytes.

**The simulation validates every order** against what the seat owns, can see and can afford, and drops what fails with a reason the client can display. Validation is inside `Sim`, not in `Net` or the executable, because in lockstep every machine applies every order and any one of them could be lying (§5).

### 4.8 Tick order

Fixed, and written down once:

```
1   apply this tick's orders, in seat order then arrival order
2   economy: extraction, stockpile, caps
3   research: advance, complete, apply upgrades
4   production: factories advance, spawn devices
5   construction: builders advance structures and modules
6   movement: paths advance, steering, collision with terrain and obstruction
7   visibility: the budgeted refresh
8   targeting and firing: acquire, roll, spawn projectiles, resolve direct hits
9   projectiles: advance, resolve indirect impacts, splash
10  damage, destruction, wrecks, experience
11  AI seats: observe, decide, enqueue orders for tick t + delay
12  victory check
13  hash the state
```

Step 11 is why the AI is in `Sim`: it emits orders like any commander, into the same queue, for a future tick, so a replay reproduces every AI decision without recording one.

### 4.9 Hash, snapshot, replay

- **The hash** in step 13 is a 64-bit digest over every object's simulation fields in id order plus every seat's state, each tick. FNV-1a is enough and is a few lines in `Core`. It is what the host and the clients compare (§5), and what a test compares between two runs.
- **A snapshot** is the full serialisation of `Sim` — seed, tick, landscape deltas, every slot map, every seat, the PRNG state — through one versioned byte-stream writer and reader pair in `Core`. It is the save file, the late-join payload and the desync-recovery payload: three features, one format. Estimated size: 4,000 objects at about 64 bytes, plus per-commander grids compressed as runs, under 2 MB for a Large landscape.
- **A replay** is the settings, the seed and the order stream. The stream is tiny (§5.2), so recording is always on.

---

## 5. Networking

### 5.1 The model, and the decision behind it

**Proposed: deterministic lockstep on the order stream, with the host's simulation authoritative.** Every machine runs `Sim` from the same seed; the host receives each commander's orders, stamps them for a future tick, and broadcasts the sequenced stream; every machine advances when it holds the orders for the next tick. Each tick's hash travels with the next batch of orders. A client whose hash disagrees with the host's has *diverged*, and instead of ending the match — the Darwinia and *Warzone* outcome — it requests a snapshot from the host, reloads, and continues from the host's state. Late join and rejoin are the same snapshot path.

This is the recommendation because it takes everything R16 already demands — integer simulation, pinned PRNG, tick clock, snapshotting — and spends it on the cheapest multiplayer a strategy game can have: bandwidth is orders, not state; the protocol is a handful of message kinds; there is no interest management, no prediction and no reconciliation for two thousand units. It is also the model *Warzone 2100* and Species both use, so the failure modes are known.

**The alternative** is a host-authoritative simulation with state replication: only the host simulates; clients receive object state within their interest and render it. It is what a persistent world with hundreds of players and untrusted clients needs, and it is what the Species direction documents design towards. It costs a replication protocol, interest management, client-side interpolation and prediction, and a client that does not run `Sim` at all — many months of one developer's time — and it buys cheat resistance and scale that a match of eight does not need. **Q2 puts this to the owner**, because the answer to Q1 (match or world) decides it, and the whole of `Net` is shaped by it.

What lockstep accepts, stated so nobody rediscovers it: every client holds the whole match state, so a modified client can see through fog; the slowest machine sets the pace; and the desync class of bugs exists — which is exactly why the hash, the snapshot recovery and the replay-as-bug-report are in the design rather than left for later.

### 5.2 The protocol

UDP, one socket per process, ported from the Species `network-transport` work rather than reinvented: a `Transport` seam with a `UdpTransport` and a `LoopbackTransport` so the whole conversation is testable in-process; every datagram framed with a protocol version and a length, every read bounded; server-assigned connection ids so two players behind one router are two players; liveness by heartbeat and timeout.

Message kinds, host-centred:

| Direction | Kind | Carries |
|---|---|---|
| client → host | `Join` | protocol version, content hash, player name, a join token |
| host → client | `Welcome` / `Refuse` | seat, match settings, current tick, a snapshot if the match is running |
| client → host | `Orders` | the client's orders for tick *t* + delay, plus its hash for the last tick it completed |
| host → client | `TickBatch` | every seat's orders for one tick, the host's hash for that tick, a sequence number |
| both | `Ack` | cumulative sequence acknowledgement |
| client → host | `RequestSnapshot` | after a hash mismatch or a gap it cannot fill |
| host → client | `Snapshot` | chunked, reliable |
| both | `Chat`, `Ping`, `Leave` | |

`TickBatch` is reliable and ordered: sequence numbers, retransmission on a gap, and a bounded history on the host (Species T10's history pruning is the reference). Everything else is best-effort.

**Input delay.** The host stamps orders for tick *t + d*, where *d* starts at 3 ticks (150 ms) and adapts between 2 and 8 from measured round trips. A client that has not delivered orders for a tick by the time the host must send the batch is sent an empty order set for that tick and told so; a client three seconds behind is dropped to AI control (`GameDesign.md` §10).

**Bandwidth, as arithmetic.** An order is at most 32 bytes; a busy commander issues perhaps two per second; eight commanders make 16 orders per second, 512 bytes, in 20 batches of about 40 bytes with headers — under 2 KB/s to each client. A snapshot of 2 MB over a 1 MB/s link is two seconds. A replay of a two-hour match is under 4 MB uncompressed.

**Content hash at join.** The executable hashes its compiled-in tables at startup, and the host refuses a client whose hash differs. That is what keeps a mismatched build — or, if Q3 permits mods, a mismatched mod — from becoming a desync a minute in.

---

## 6. The renderer

### 6.1 Shape

Direct3D 12 through the SDK headers alone (R14): device, command queue, one command allocator per frame in flight, a flip-model swap chain of three back buffers, descriptor heaps managed by hand, resource barriers written by hand, three frames in flight with a fence per frame. The scene target at the authored resolution (R12) and the present pass that scales it, with the 1:1, integer and bilinear cases `AGENTS.md` §5 lists.

**The first client ADR** settles the authored resolution, the window style and whether the scene target is multisampled. This design assumes 1920×1080, a borderless window covering the primary monitor with Escape and Alt+F4 owned by the game, and a 4× multisampled scene target — flat-shaded geometry with hard silhouettes is exactly the content that aliases worst and that multisampling fixes best, and the back buffer cannot be multisampled, which is the reason the scene target exists.

### 6.2 Passes

| Pass | Draws | Pipeline |
|---|---|---|
| Terrain | Chunked landscape meshes, 64×64 cells per chunk, vertex colour from the palette, two directional lights, per-face normals | One PSO; chunks culled by frustum; a coarser mesh per chunk beyond a distance |
| Water | One plane at the water level with the wave texture scrolling | One PSO, alpha blended |
| Geometry | Every device, structure, feature and wreck: baked models, per-vertex colour, team colour substituted, instanced per model | One PSO; one instance buffer per model per frame |
| Sprites | Billboards for infantry-sized things and particles | One PSO, instanced, alpha tested |
| Fog | A full-screen composite darkening explored-not-visible cells and blacking unexplored ones, from the commander's visibility grid uploaded as a texture | One PSO |
| UI | Windows, text, icons, the minimap | One PSO, orthographic, alpha blended |
| Present | The scene target into the back buffer, scaled | One PSO |

Seven pixel shaders and about as many vertex shaders, hand-written HLSL under `Client/Shaders/`, compiled by `FXCompile` into `Client/CompiledShaders/` (`AGENTS.md` §2). Shader model 6 through the SDK's `dxc` is the target; whether `FXCompile` drives it cleanly on the pinned toolset is one of the first things M0 finds out.

### 6.3 The render view

The executable builds, each frame, a plain list of what to draw: for each visible object a model id, a position and an orientation interpolated between the last two ticks (the first float conversion of a simulation number, and the only place it happens), a team colour and a rank badge; for the terrain, which chunks changed height since the last frame. `Client` draws the list. `Client` never sees a `Device`.

### 6.4 The look, mechanically

- **Models** are baked from the Species `.shp` format (§8): positions quantised to `std::int16_t`, one colour per vertex, triangles as `std::uint16_t` indices, no normals — the face normal is derived in the pixel shader from the screen-space derivatives of the world position, or baked per triangle by the tool with the vertices split; the ADR that lands the first model decides. Markers — the named attachment points 75 of the 106 Species shapes carry, for turrets, muzzles and build effects — come across from the `.shp` `Marker` records.
- **Terrain colour** is the Species formula, computed on the CPU when a chunk is built: `u = (1 − slope)^0.4`, `v = 1 − height / highest`, plus noise, indexed into a 64×64 palette. Floats, because it never reaches the simulation.
- **Text** is a bitmap font atlas compiled in, drawn as quads at 1:1 at the authored resolution: one of the two Species pixel fonts, or a new one drawn in the same spirit, per Q16 (`SpeciesLineage.md` §4 says what the Species ones are).
- **The UI toolkit** is a window-and-widget system in the Eclipse shape: windows own widgets, the input router offers events to the topmost window first, a widget that acts on an event consumes it, and nothing in it polls.

### 6.5 Input and audio

**Input** takes the Species `input-native-events` design as its specification, because it is the best-documented piece of engineering in that tree and every rule in it was learned the hard way: one message pump per frame; the window procedure enqueues events and does nothing else; a pure per-frame derivation with one edge per control per frame; Raw Input for camera aim with `WM_MOUSEMOVE` as the fallback, guarded on *a relative packet actually arrived*; text as `WM_CHAR` characters to the focused widget; a router that offers events UI-first and masks a consumed key until release; subscriptions that are move-only handles. And the rule that matters most here: **the simulation never subscribes to input.** Input reaches `Sim` as orders through `Net`, nowhere else.

**Audio** is XAudio2 with X3DAudio positioning, ported from the Species `SoundLibraryXAudio2` backend, with the Species `Sounds.txt` event model — an event per (object kind, event) naming a sample group, a position type, a loop type and parameter curves for volume and pitch — carried across as a compiled-in table. Device loss is handled as Species does: park silent, rebuild every few seconds until a device comes back.

---

## 7. AI

In `Sim`, deterministic, one planner and a table of personalities (`GameDesign.md` §9). Structurally: an AI seat observes the simulation through the same visibility grid as a human and holds a small blackboard — known enemy structures, a threat map at cluster resolution, its own economy and army composition; each tick, within its budget, it evaluates a fixed list of behaviours (expand, defend, research, build army, attack) with personality weights and emits orders for tick *t + delay* through the same queue as a client. No threads, no wall time, no floats. The scripted opponent of the vertical slice is the same structure with one behaviour list hard-coded.

---

## 8. Content and the pipeline

R13 says content is compiled in and a generated header is committed, not built. The pipeline is Python under `Tools/`, run by a developer, with its output reviewed and committed like source.

| Content | Source | Baker | Output | Size, estimated |
|---|---|---|---|---|
| Models | Species `.shp` text files, and new ones in the same format | `Tools/BakeModels.py` | One header per model: `constexpr` position, colour, index and marker arrays | The 106 Species shapes total 53,193 triangles and 29,637 positions; at 6 bytes each that is about 500 KB for all of them, and the game uses a subset |
| Terrain palettes, water, waves | Species 64×64 and 128×128 BMPs | `Tools/BakeImages.py` | `constexpr` RGB arrays | 8 palettes at 12 KB, 3 water and 7 wave textures: about 250 KB |
| Sprites, icons, cursors | 32×32 and 128×128 BMPs | the same | `constexpr` arrays, 8-bit indexed where the source is | Under 500 KB |
| Font | A Species font bitmap (256×224, 8-bit) or a new one | the same | One atlas | Under 100 KB |
| Sounds | Species 16-bit 44.1 kHz mono WAV | `Tools/BakeSounds.py` | See below | The budget question |
| Component tables, research tree, structure catalogue, damage matrix, sound events | Hand-written `constexpr` tables in `Content/` | none — they are source | `consteval` validated | Tens of KB |
| Stamps | Authored in the game's editor window, exported as a header | the game | `constexpr` height and feature arrays | A few KB each |

**Sounds are the one budget that does not fit the rule as written.** The Species effect library is 500 MB of PCM (`SpeciesLineage.md` §5 has the measurement); the effects this game would use are perhaps 150–250 files, and 200 files at an average of 200 KB is 40 MB of PCM — and a 40 MB `constexpr` array is roughly 200 MB of C++ source text that MSVC compiles slowly and every checker walks. Two ways out, both for an ADR (Q4):

1. **Compress and embed.** XAudio2 plays MS-ADPCM natively (`WAVEFORMATADPCM`; no decoder to write), which is 4:1; resampling to 22.05 kHz mono halves it again. 40 MB becomes about 5 MB, which is an embeddable array, if a slow-compiling one. The quality is adequate for effects and would not be for music, of which there is none.
2. **Embed as a Win32 resource.** An `RCDATA` resource is inside the executable — R13's purpose, that the executable ships alone and loads nothing from disk, holds exactly — and is read through `FindResource` and `LockResource`, which are Windows SDK calls. It is not a `constexpr` array, which is R13's letter. The `.editorconfig` and `.gitattributes` in this repository already carve out `.rc` files, which suggests resources were anticipated; whether for this is the owner's to say.

The recommendation is both: compress, and embed the compressed audio as `RCDATA` while everything else stays `constexpr`. The ADR would name the format, the resource ids, and the rule that a resource is content and is baked and committed like a header. If the pinned toolset supports `#embed`, the ADR should say whether that was checked, because it changes the answer.

**Compile-time validation is the point of compiled-in tables.** `consteval` functions in `Content` check, at build time, that every research prerequisite names an existing item and the tree has no cycle; that every component's unlock names an item; that every model a component names exists; that every sound event names a sample group; and that no id is duplicated. A table edit that breaks the game breaks the build instead, which is what makes "data over code" safe for one developer without a QA department.

---

## 9. What the executable may write

R13 pre-approves nothing; each file below is an ADR that names its form, its location beside the executable, and its lifetime. The design needs these, in this order of urgency:

| File | Written by | Why | Proposed form |
|---|---|---|---|
| Preferences | Client | Scaling mode, key bindings, audio volumes, player name | One UTF-8 text file, key = value, beside the executable; absent means defaults |
| Designs | Client | A commander's saved device designs (`GameDesign.md` §6) — the feature is meaningless if they vanish with the process | Part of the preferences file, or its own; a few KB |
| Saved match | Host | A snapshot (§4.9) plus settings | One binary file, versioned |
| Replay | Host and client | Settings, seed, order stream | One binary file, versioned, always recorded, oldest pruned |
| Stamp export | Client (editor window) | Authored terrain as a header for `Content/` | A `.h` file, committed by a human |
| Host log | Host | What the headless host did and why a client was refused | Text, beside the executable, rotated |

None of these is required to start, which is the distinction R13 draws; all of them resolve beside the executable, never against the working directory. Q13 asks the owner to approve the category so that each ADR argues the form and not the principle.

---

## 10. Testing

- **`Sim` is the bulk of the suite, and determinism is its first test.** Two matches from one seed and one order stream hash identically at every tick; a snapshot taken at tick *n* and reloaded continues to the same hashes as the original; a replay reproduces a recorded match hash for hash. These three tests exist from M0, and every later feature runs under them.
- **`Net` is tested over `LoopbackTransport`**, as Species does: join, welcome, tick batches, a dropped datagram retransmitted, a hash mismatch triggering a snapshot, a slow client dropped to AI — the conversation, not the encodings.
- **`Content` is tested by the compiler** (§8), and by a suite that instantiates every table row: every design that can be built is built.
- **`Core`** tests its arithmetic exhaustively where the domain is small (the binary-angle tables, the integer square root, the fixed-point multiply) and by property elsewhere.
- **`Client` and the executables** are tested by running them (`AGENTS.md` §3); the pure parts — the input derivation, the UI router — get unit tests, as the Species input work showed they can.

A `SuiteSmoke` placeholder in every test project until its first real test, as `AGENTS.md` §3 requires, because vstest reports an empty suite as a pass. Tests never open a socket, a window or an audio device, and never read content from disk — the Species `docs/TESTING.md` rules, which are the right rules.

---

## 11. Risks

| Risk | Why it is real | What the design does about it |
|---|---|---|
| **Scope** | This is a strategy game with a design system, a research tree, an AI, lockstep multiplayer and a large-map renderer, with no library for any of it, by one developer | Milestones that are playable states, a vertical slice that cuts everything not on the critical path, and tables so that content is not code |
| **Large landscapes** | Pathing, visibility and terrain rendering scale with the map; a Frontier landscape is over sixty times a *Warzone* map | Hierarchical pathing, budgeted visibility, chunked terrain — and M4, not M1, is where Frontier-class maps must perform |
| **Desyncs** | The bug class lockstep creates; Species and *Warzone* both carry the scars | Integer simulation from the first line, a per-tick hash, snapshot recovery instead of match death, replays as bug reports |
| **D3D12 without helpers** | R14 removes `d3dx12.h`, so barriers, heaps and root signatures are hand-written | Seven fixed pipelines, no material system, no generality: write the little the game needs, once |
| **Content under R13** | Sound does not fit as `constexpr` | The ADR of §8 |
| **Provenance of the Species content** | The art and sound derive from Darwinia and the licence is unresolved | `SpeciesLineage.md` §1 and Q5; nothing licensed to a third party (the soundtrack, the branding, the narration) comes across at all |
| **Text at scale** | A pixel font resampled is the cost of the scaled present | 1:1 at the authored resolution is the common path; the ADR settles the resolution with that in mind |

---

## 12. The first decisions, as ADRs

The ADRs the first tasks will write, in the order the work meets them:

| ADR | Decision | Written by |
|---|---|---|
| 001 | The solution and project layout of §2, with the names settled | The first project |
| 002 | Authored resolution, window style, scene target multisampling | The first renderer task |
| 003 | Tick rate, the position unit and the fixed-point formats of §4.1 | The first `Sim` task |
| 004 | The network model (§5.1) | Before `Net` has a second file |
| 005 | Content embedding: `constexpr` and resources, the sound format (§8) | The sound baker |
| 006 | The Species-derived content that comes across, and under what terms (Q5) | The model baker |
| 007 | The preferences file (§9) | The first task that needs a setting to persist |
| 008 | The snapshot and replay formats (§4.9) | The first save |
