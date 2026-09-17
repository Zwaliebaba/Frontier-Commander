# Frontier Commander — Technical Design

**Status: DESIGN (accepted by the owner on 2026-09-17, after the questions in [`OpenQuestions.md`](OpenQuestions.md) were answered; revised through ADRs).** It assumes [`AGENTS.md`](../AGENTS.md) has been read and cites its rules by number rather than repeating them. Decisions carry the date they were taken. Figures are arithmetic on stated assumptions, not measurements, and are labelled so: nothing in this repository has been built or run, and this document was written on Linux where nothing could be.

---

## 1. What the rules already decide

Four `AGENTS.md` rules shape everything below, and this document does not re-argue them:

- **R12** — Direct3D 12 only, COM lifetimes RAII, one scene target presented scaled.
- **R14** — the Windows SDK and the MSVC standard library, and nothing else. No Agility SDK, no `d3dx12.h`, no DirectXTK, no shader compiler at runtime, and no parser library: JSON, BMP and WAV are read by code in this tree.
- **R16** — the simulation is deterministic and holds no floats: integers and fixed point, a pinned PRNG, the tick as the only clock. Floats live in the renderer and in the client's replica.
- **§2** — flat project directories, one-way edges, project files as source.

**R13 was removed by the owner on 2026-09-17** (its number is not reused; `AGENTS.md` §5 says R18 and up are reserved without renumbering). The executable no longer ships alone: game data lives in files under `Content\` beside it and mods override them (§8), and what the game writes goes under the user's profile (§9). Two things the old rule also said hold here as design: shaders are compiled at build time, never at runtime (`AGENTS.md` §2 still says so), and a path resolves from the executable's directory or the user's profile, never from the working directory (§8, §9).

What the rules leave open, and this document decides: the projects, the simulation's representation, the network model, the renderer's shape, the content layout, and where files live.

---

## 2. Projects and layers

Eight projects, one solution at the root, `x64` only, toolset `v145`, as `AGENTS.md` §3 requires. The names are decided (owner, 2026-09-17). Arrows point at what a project is built on; every arrow points downward and none points sideways.

```
            ┌────────────────────────────────────────────────┐
            │  FrontierCommander (exe)      FrontierHost (exe)│
            └───────┬──────────┬──────────────────────┬──────┘
                    │          │                      │
            ┌───────▼───┐  ┌───▼──────┐               │
            │  Client   │  │ Replica  │               │
            └───────┬───┘  └───┬──────┘               │
                    │          │                      │
                    │      ┌───▼────┐                 │
                    │      │  Net   │◄────────────────┘      FrontierHost builds on
                    │      └───┬────┘                        Net, Sim, Content, Core
                    │          │                             and never on Client or Replica
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

| Project | Kind | Namespace | Holds | Built on |
|---|---|---|---|---|
| `Core` | static lib | `Neuron` | Fixed-point math, binary angles, integer geometry, the PRNG, hashing, slot maps, byte-stream reader and writer, the JSON reader and writer, the BMP and WAV readers, the UDP transport and its loopback twin, path resolution (executable directory, user profile), assertions, the one header that owns the Windows macro family | nothing |
| `Content` | static lib | `Frontier` | The loaders and the in-memory tables: components, research, structures, the damage matrix, sound events, biomes, landscape definitions, stamps, models; the content hash; validation with file-and-line diagnostics | `Core` |
| `Sim` | static lib | `Frontier` | The landscape, the world, orders and their validation, economy, research, production, movement and pathing, visibility, combat, the AI, the tick, the hash, snapshots | `Core`, `Content` |
| `Net` | static lib | `Frontier` | The protocol: message records, the host endpoint that publishes each client's view of `Sim`, the client endpoint that receives it | `Core`, `Sim` |
| `Replica` | static lib | `Frontier` | The client's world: the objects its commander can see, built from `Net`'s records, interpolated between frames, and turned into the render view | `Core`, `Content`, `Net` |
| `Client` | static lib | `Neuron` | The Direct3D 12 renderer, the window, the input event system, XAudio2, the UI toolkit | `Core`, `Content` |
| `FrontierCommander` | exe | `Frontier` | The game: application, main loop, camera, HUD, the local host for single-player and hosted matches | everything |
| `FrontierHost` | exe | `Frontier` | The headless host: opens a match, runs the one simulation, publishes to clients | `Core`, `Content`, `Sim`, `Net` |

**The edges, and why each runs the way it does.**

- **`Sim` includes no Windows header, no D3D header and no socket.** That is what makes a headless host possible and the simulation testable without a window, and it is the edge the layering checker guards hardest, because it is the one a convenience include breaks first.
- **`Content` holds data and its loaders, not behaviour.** The row types (`ChassisDesc`, `ResearchItemDesc`, …) are plain aggregates (R8) filled from JSON; the validation that checks the tables lives beside them (§8). `Sim` reads `Content`; `Content` knows nothing of `Sim`.
- **`Net` builds on `Sim`** because the host endpoint reads simulation state to publish it, and never on `Client`, `Replica` or the executables. The message records — the wire form of a device, a structure, a projectile, an event — are defined here and are the only thing the two ends share.
- **`Replica` builds on `Net`** for those records and on nothing above. It is the second world model this design has, and it exists because only the host simulates (owner, 2026-09-17): a client draws what the host told it, not what it computed. It is a library rather than part of the executable so that "applying frames converges to the host's state" is a unit test (§10).
- **`Client` does not build on `Sim` or `Replica`.** The executable builds a **render view** from the replica each frame — a plain list of what to draw — and `Client` draws it, so nothing in `Client` names a `Device`. That is R9's "the engine does not know the game" made structural.
- **The executables hold what is genuinely theirs**: the frame loop, the camera, the HUD, the render-view translation, and in `FrontierCommander` the thread that hosts a match locally. Code in an executable cannot be linked into a test DLL, so anything in one that deserves a test is code that belongs in a library.

**Namespaces** (owner, 2026-09-17): `Frontier` for the game, as `AGENTS.md` §1 illustrates; **`Neuron` for the engine**, so that the input, transport and XAudio2 code ported from Species moves without a rename, which `AGENTS.md` names as the reason the formatter settings were carried over. The transport lives in `Core` and is engine code; the protocol above it is game code, which is why `Net` is `Frontier`.

**Tests** are one `Tests/<Name>Tests` project per static library on the Microsoft Native Unit Test Framework, each sitting directly above its library with the same edges. `Sim`, `Net` and `Replica` tests are the bulk of the suite (§10).

**`Build/` and `Tools/`** hold Python that never ships: the three checkers `AGENTS.md` §6 names, a layering checker in the Species mould (an upward include fails, and there is no allowlist), `CheckContent.py` (§8), and the importers of §8.

---

## 3. The frame and the tick

**The simulation ticks at 20 Hz** (owner, 2026-09-17), 50 ms each, with ADR-003 recording the measured confirmation once there is a build to measure. Species inherited 10 Hz from Darwinia and spread heavy work over ten slices; twenty is chosen because orders feel late at 100 ms of tick quantisation on top of the replication interval, and because a 50 ms budget on a 2026 desktop for the object counts below is generous.

**Only the host simulates.** There are two loops, and a single-player game runs both in one process:

```
host loop (FrontierHost, or the host thread inside FrontierCommander), driven by wall time:
1  drain the network: orders in, acks in
2  while simulation time trails wall time by a tick:
       apply this tick's orders; Sim::Advance(tick)              0, 1 or several per pass
3  every second tick: publish a frame to each client            each client's interest set, delta-encoded (§5)
4  heartbeat, timeouts, the host log

client loop (FrontierCommander), driven by the display:
1  pump the window's messages into the input event queue        once per frame, nowhere else
2  drain the network: frames in, orders and acks out
3  apply new frames to the replica
4  interpolate the replica 100 ms behind the newest frame       the first float: positions between two frames
5  build the render view from the replica
6  route input events: UI first, then camera and selection      selection and orders become messages
7  record, draw, present
```

Step 2 of the host loop is the only place wall time and ticks meet (R16). The client has no simulation clock; it has the replica's timeline, which is the host's tick numbers arriving late. **Local play is a host thread in the same process talking to the client through the loopback transport**, so single-player exercises the same code path as a match over the network; nothing is special-cased for one player.

**Budget arithmetic.** Eight commanders at 300 devices is 2,400 devices; with structures, projectiles and a neutral faction, call it 4,000 simulated objects. At 50 ms per tick that is 12.5 µs per object per tick, which is thousands of instructions. Pathing and visibility are the systems that can spend it (§4.5, §4.6); publishing (§5) is the third, and it runs every other tick.

---

## 4. The simulation

Everything in `Sim`, in the order R16 forces. It runs on the host only, and everything in it is exactly as deterministic as before the network model was decided: determinism now serves replays, saves, tests and the AI rather than agreement between clients, and R16 stands.

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

The generator is a small, well-known algorithm written into `Core` from its specification. xoshiro128\*\* is the choice: sixteen bytes of state, and its reference implementation is public domain, so R14's "the licence travels with the bytes" has nothing to carry. Mersenne Twister, which Species carries, is 2.5 KB of state that would go into every snapshot for no benefit.

### 4.3 Identity and storage

**An object's identity is a generated id, never a container index.** Species' owner recorded exactly this decision for Species' own future protocol on 2026-08-02 (the Species repository's `docs/ARCHITECTURE.md`, *Runtime model*): a slot index is reused, so a stale reference silently aliases whatever occupies the slot later; a counter incremented in creation order can never alias. Here it is the rule from the first line, and it is also what the wire carries: an `ObjectId` is a `std::uint32_t` creation counter paired with a kind tag, the same on the host and in every replica. A slot map in `Core` resolves it to storage in O(1) through a generation-checked handle, so a dead id resolves to nothing rather than to a stranger.

**Storage is a slot map per object kind** — devices, structures, projectiles, features, wrecks — with stable handles and **iteration in id order**. Standard containers by default (R15): a `std::vector` behind the slot map, and never a `std::unordered_map` in a loop whose order reaches the outcome (R16). Per-commander state — power, research, designs, visibility — is an array indexed by seat.

### 4.4 The landscape

**Heights are generated, integer, and identical everywhere.** The Species generator — diamond-square tiles with a fractal dimension, height scale and desired height each, merged into one map and smoothed under a guide grid; [`SpeciesTerrain.md`](SpeciesTerrain.md) §4 has every step — is ported from the Species repository's `GameLogic/Landscape.cpp` into integer arithmetic on the simulation stream. The Species code draws from the cosmetic LCG and computes its noise as `sfrand(powf(length × 10, fractalDimension))` in `float`; that is exactly what R16 forbids, and it is why Species records a landscape that changed shape across a compiler migration. The port replaces `powf` with a fixed-point table over the handful of fractal dimensions a landscape may use, and the LCG with the simulation stream keyed by tile seed. **The heightfield is simulation state**: pathing, slope, water and line of sight all read it, so it is under R16 without exception. The palette lookup that colours it (§6.4) is not, and stays in float on the renderer side. **Generation depends on nothing but the definition**: the Species preference that changed the heightmap's resolution (`SpeciesTerrain.md` §3) is the bug this port does not carry.

The client generates the same landscape from the definition the host sends at join, with the same `Sim` code — the client executable links `Sim` because it hosts locally — and receives the flatten deltas as they happen. A landscape is therefore never replicated sample by sample.

**Storage.** Heights are sampled every 16 world units — four samples per cell edge, close to the Species spacing of 10.66 — and stored as `std::int16_t` whole units, which covers the height range of any Species landscape many times over. A Large landscape is 4,097 × 4,097 samples, 33.6 MB; a Frontier one 8,193 × 8,193, 134 MB, which is memory rather than a problem on the machines this game targets, and it is one of the numbers that make Frontier-class landscapes M4 work rather than M1. Derived grids are per cell, not per sample, each one byte or one bit: slope class, water, obstruction (a structure or feature occupies the cell), and per-commander visibility at two bits for its three states. Eight commanders on a Frontier landscape is 8 × 4.2 million cells × 2 bits = 8.4 MB of visibility. Arithmetic, not measurement.

**Stamps** are authored patches: a rectangle of relative heights and a list of features, applied after generation at a position the generator chooses. Their format is the same as a snapshot's landscape section, so the tool that authors one is the game with an editor window, and a stamp is a JSON file under `Content\Stamps` (§8). **Terrain deltas at runtime** — flatten under a structure, and nothing else through M3 (owner, 2026-09-17) — are recorded as a list of rectangular height edits applied over the generated base, so a snapshot carries the seed plus the deltas rather than the heights, and terraforming can come later without a new snapshot format.

### 4.5 Pathing

The system that decides whether a Large landscape works. The design is **hierarchical A\* over clusters with local steering**:

- The landscape is divided into 16×16-cell clusters; each cluster's boundary crossings are nodes, and the paths between them inside the cluster are precomputed edges, per drive class, because water and slope differ per class. A Large landscape has 4,096 clusters; a Frontier one 16,384.
- A move order plans over the cluster graph (thousands of nodes, not millions of cells), refines the first few clusters to a cell path, and refines further as the device advances. Planning is amortised over ticks with a budget per tick, and it is deterministic because the budget is in nodes expanded, not in milliseconds.
- Between cells, devices steer around each other with a separation rule in integer arithmetic; formations are a version-2 concern.
- A structure placed or destroyed invalidates the clusters it touches, which recompute their internal edges lazily.

Flow fields — one field per destination, shared by every device heading there — are the alternative and are better for hundred-unit blobs converging on one point; they are worse for a landscape this size because a field covers the map. The choice is an ADR when the slice has numbers; the interface `Sim` exposes (request a path, advance along it) is the same for both.

### 4.6 Visibility

Per commander, a grid of the three fog states. Each tick, a budgeted share of the devices and structures with sight recompute the cells they see: a disc of the sight radius, extended by the height difference, with each cell tested for occlusion by walking the heightfield from the viewer to the cell in integer steps. A viewer that has not moved and whose surroundings have not changed keeps its previous disc. The budget — every viewer refreshes at least once per second — is in viewers per tick, so it is deterministic.

The cost, as arithmetic: a sight radius of 24 cells is a disc of about 1,800 cells; an occlusion walk averages 12 steps; that is roughly 22,000 heightfield reads per viewer refresh, and 400 viewers refreshing once a second over 20 ticks is 440,000 reads per tick — well inside the budget of §3, and the reason the budget is in viewers rather than milliseconds.

**Visibility is also the replication filter.** What a commander's grid marks visible is what the host publishes to that commander's client (§5.2), so the fog of war is enforced by the host and a client is never sent what it should not see.

### 4.7 Orders and validation

**Orders are the only input.** An order is a small, fixed-layout record: the commander's seat, the tick it is for, the kind, and up to four operands (ids, a position, a design id, a research id). Kinds: `Move`, `AttackMove`, `Attack`, `Patrol`, `Guard`, `Stop`, `ReturnToRepair`, `SetStance`, `PlaceStructure`, `CancelStructure`, `Demolish`, `BuildModule`, `SetProduction`, `CancelProduction`, `SetResearch`, `CancelResearch`, `SaveDesign`, `Group`, `Surrender`, `Chat`. Twenty kinds, every one under 32 bytes.

**The simulation validates every order** against what the seat owns, can see and can afford, and drops what fails with a reason the client can display. Validation is inside `Sim`, on the host, and it is the whole of the trust model: a client's order is a request, and a client that lies about what it sees gains nothing, because the host decides visibility (§4.6) and applies orders against its own state.

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
14  every second tick: publish (§5)
```

Step 11 is why the AI is in `Sim`: it emits orders like any commander, into the same queue, for a future tick, so a replay reproduces every AI decision without recording one. Step 14 is outside the simulation proper — it reads the state and writes nothing — and lives in `Net`.

### 4.9 Hash, snapshot, replay

- **The hash** in step 13 is a 64-bit digest over every object's simulation fields in id order plus every seat's state, each tick. FNV-1a is enough and is a few lines in `Core`. It is what a test compares between two runs and what a replay is checked against; there is no cross-client comparison, because no client simulates.
- **A snapshot** is the full serialisation of `Sim` — seed, tick, landscape deltas, every slot map, every seat, the PRNG state — through one versioned byte-stream writer and reader pair in `Core`. It is the save file, and it is host-side only: a client never holds the whole match, and a joining or rejoining client receives its commander's view (§5.4), not a snapshot. Estimated size: 4,000 objects at about 64 bytes, plus per-commander grids compressed as runs, under 2 MB for a Large landscape.
- **A replay** is the settings, the seed and the order stream the host applied. The stream is tiny (§5.7), so the host records every match; a replay is watched by hosting it locally from the file, which is why the client executable links `Sim`.

---

## 5. Networking

### 5.1 The model, and the decision behind it

**Decided (owner, 2026-09-17): the host runs the only simulation and replicates state to clients.** Each client holds a replica of the objects its commander can see, kept current by frames the host publishes at 10 Hz; orders go to the host and take effect there. The draft had recommended deterministic lockstep, and the owner chose replication over it; both sides of that are recorded here so nobody re-litigates it.

**What it buys.** A client receives only what its commander can see, so the fog of war is enforced by the host and a modified client sees exactly what an honest one does — the map hack that every lockstep RTS lives with does not exist. Clients need not be deterministic, identical, or even the same build of the simulation, and a headless host on a machine nobody plays on is the natural shape rather than a special case, which is what "server-ready" (owner, 2026-09-17) needs. Late join and rejoin are the same thing as joining, and there is no desync class of bugs, because there is nothing for two simulations to disagree about.

**What it costs.** A protocol that publishes state rather than orders (§5.3); interest management, which is the visibility grid doing double duty (§5.2); a second world model, `Replica`, that has to stay a faithful reading of the host's (§2); bandwidth an order of magnitude above lockstep's, though still small (§5.7); and orders that take a round trip before a unit moves (§5.5). The lockstep draft had none of these and a desync class instead. The cost is accepted and the design carries it.

**What stays.** R16 stands: the host's simulation is deterministic so that replays reproduce, saves round-trip, tests compare hashes and the AI is reproducible. The Species transport work is ported as before (§5.6).

### 5.2 Interest: what a client is told

A client's **interest set** is decided by the host every publish, per client, from the visibility grid of §4.6:

- every object in a cell its commander, or an ally, currently sees;
- every object its commander owns, wherever it is;
- structures in explored cells at their **last-seen state** — sent once when they leave visibility, and updated only when seen again — which is how a client draws the ghost of a base it scouted;
- the landscape's definition and flatten deltas, and the match settings, which every client holds in full because they are not secrets.

An object entering the set is sent whole; an object leaving it is sent as a removal, and the replica keeps a ghost only for structures. Projectiles and effects inside the set are sent as short-lived events rather than objects. Interest is recomputed every publish from the grid, so the cost is a walk over the commander's visible cells' occupants, which the visibility pass already maintains.

### 5.3 Frames: how state travels

The host publishes a **frame** per client every second tick (10 Hz): a sequence number, the sequence of the **baseline** it is encoded against, and three lists — objects created since the baseline (full records), objects changed (a field mask and the changed fields), objects removed — plus the events of the interval. The baseline is the newest frame the client has acknowledged; the host keeps a short history of what it sent each client (32 frames, 3.2 seconds) and encodes against the acked one, so **a lost frame costs nothing but a larger next frame**: no retransmission, no ordering, just the next delta from an older baseline. A client whose acknowledged baseline has fallen out of the history receives a full frame — the same path a joining client takes.

Records are the plain aggregates of `Net`: `DeviceState` (id, design, seat, position, heading, hit points, rank, order kind, target, stance flags), `StructureState`, `WreckState`, `FeatureState`, `SeatState` (power, research in progress, victory state), and `Event` (kind, position, source, target, time). Positions on the wire are quantised to a quarter of a world unit and sent as deltas from the baseline; a device that did not move sends nothing. Every field is a fixed-width integer; there is no float on the wire.

Datagrams carry frames unreliably. Orders (§5.5) are the one reliable stream.

### 5.4 Joining, leaving, resuming

- **Join**: protocol version, content hash, player name, join token. The host answers with a seat, the settings, the landscape definition, the current tick and a full frame, or a refusal with a reason (version, content hash, no seat).
- **Rejoin** is a join with the same token; the seat is returned from AI control and the client receives a full frame.
- **Leave** and **timeout** put the seat under AI control for the grace period `GameDesign.md` §10 gives.
- **Save and resume** are host-side: the host writes a snapshot (§4.9) and later loads it, and the players join as they would any match.

### 5.5 Orders and their feel

Orders travel client to host on a **reliable stream**: sequence numbers, cumulative acknowledgements in every datagram, and resend of anything unacknowledged after a round-trip estimate. The host applies an order at the start of the next tick and it shows in the following frame. The round trip plus the publish interval is 120–180 ms at broadband latencies, which is comparable to the input delay the lockstep draft carried; the client acknowledges an order locally at once — a cursor mark, a sound — and the unit moves when the replica says it has. There is no client-side prediction of movement: a strategy game does not need it and the replica stays honest.

### 5.6 The transport

UDP, one socket per process, ported from the Species `network-transport` work rather than reinvented: a `Transport` seam with a `UdpTransport` and a `LoopbackTransport` so the whole conversation is testable in-process; every datagram framed with a protocol version and a length, every read bounded; server-assigned connection ids so two players behind one router are two players; liveness by heartbeat and timeout. It lives in `Core` (§2).

### 5.7 Bandwidth, as arithmetic

Take a client whose commander sees 600 objects in a large battle, 300 of which change between frames. A changed device record is 2 bytes of id, 6 of position delta, 1 of heading, 1 of hit points, 1 of flags: 11 bytes, so 3.3 KB per frame and 33 KB/s at 10 Hz. Creations are about 30 bytes each and events 8; a full frame of 600 objects is about 18 KB. Peak is therefore 20–60 KB/s per client and about 0.5 MB/s out of a host with eight, against under 2 KB/s per client for the lockstep draft: twenty times more, and still trivial on a LAN or a broadband link. A replay of a two-hour match is under 4 MB, unchanged, because it is orders. Arithmetic, not measurement; ADR-004 records the measured numbers.

### 5.8 Trust

The host is the truth; a client is a view and a source of requests. What a client can do is send orders, and the host validates every one (§4.7); what it can know is its interest set (§5.2), so there is nothing to extract from a client's memory that the host did not choose to send. The content hash at join covers everything the host loaded, mods included (§8), and a client whose content differs is refused rather than allowed to disagree a minute in.

---

## 6. The renderer

### 6.1 Shape

Direct3D 12 through the SDK headers alone (R14): device, command queue, one command allocator per frame in flight, a flip-model swap chain of three back buffers, descriptor heaps managed by hand, resource barriers written by hand, three frames in flight with a fence per frame. The scene target at the authored resolution (R12) and the present pass that scales it, with the 1:1, integer and bilinear cases `AGENTS.md` §5 lists.

**The first client ADR** settles the authored resolution, the window style and whether the scene target is multisampled. This design assumes 1920×1080, a borderless window covering the primary monitor with Escape and Alt+F4 owned by the game, and a 4× multisampled scene target — flat-shaded geometry with hard silhouettes is exactly the content that aliases worst and that multisampling fixes best, and the back buffer cannot be multisampled, which is the reason the scene target exists.

### 6.2 Passes

| Pass | Draws | Pipeline |
|---|---|---|
| Terrain | Chunked landscape meshes, 64×64 cells per chunk, vertex colour from the palette, two directional lights, per-face normals | One PSO; chunks culled by frustum; a coarser mesh per chunk beyond a distance |
| Water | One plane at the water level with the wave texture scrolling, and the shore band | One PSO, alpha blended |
| Geometry | Every device, structure, feature and wreck: models, per-vertex colour, team colour substituted, instanced per model | One PSO; one instance buffer per model per frame |
| Sprites | Billboards for infantry-sized things, the population, and particles | One PSO, instanced, alpha tested |
| Fog | A full-screen composite darkening explored-not-visible cells and blacking unexplored ones, from the commander's visibility as the replica knows it | One PSO |
| UI | Windows, text, icons, the minimap | One PSO, orthographic, alpha blended |
| Present | The scene target into the back buffer, scaled | One PSO |

Seven pixel shaders and about as many vertex shaders, hand-written HLSL under `Client/Shaders/`, compiled by `FXCompile` into `Client/CompiledShaders/` (`AGENTS.md` §2). Shader model 6 through the SDK's `dxc` is the target; whether `FXCompile` drives it cleanly on the pinned toolset is one of the first things M0 finds out.

### 6.3 The render view

The executable builds, each frame, a plain list of what to draw from the replica: for each object a model id, a position and an orientation interpolated between the last two frames (the first float conversion of a simulation number, and the only place it happens), a team colour and a rank badge; for the terrain, which chunks changed height since the last frame. `Client` draws the list. `Client` never sees a `Device`.

### 6.4 The look, mechanically

The values are in the three reference documents: [`SpeciesLook.md`](SpeciesLook.md) for lights, materials, fog, sky, camera, particles and the pixel effect; [`SpeciesTerrain.md`](SpeciesTerrain.md) for the generator, the palette lookup, the overlay and the water; [`SpeciesCanvas.md`](SpeciesCanvas.md) for the window toolkit's rules, the chrome palette and the fonts. This section says how each becomes a pass.

- **Models** are new (owner, 2026-09-17) and are JSON files under `Content\Models` (§8) holding what the Species `.shp` record holds: positions, one colour per vertex, triangles, named markers, a fragment tree. Loaded once into vertex and index buffers: positions as `float`, one colour per vertex, no normals — the face normal is derived in the pixel shader from the screen-space derivatives of the world position, or the loader splits vertices and bakes one per triangle; the ADR that lands the first model decides. Markers — attachment points for turrets, muzzles and build effects — come across as they are.
- **Lighting** is the Species model exactly: Lambert only, no ambient, two directional lights whose colours may exceed 1.0, summed and clamped after the sum, one normal and one colour per triangle. A shader of a dozen lines.
- **Terrain colour** is the Species formula, computed on the CPU when a chunk is built: `u = (1 − slope)^0.4`, `v = 1 − height / highest`, plus noise, indexed into a 64×64 palette. Floats, because it never reaches the simulation.
- **Text** is a bitmap font atlas drawn as quads at 1:1 at the authored resolution: the Species Spectrum font (owner, 2026-09-17), loaded from `Content\Textures`, in the 16-by-14-cell atlas format `SpeciesCanvas.md` §4 describes.
- **The UI toolkit** is a window-and-widget system in the Eclipse shape, with the rules `SpeciesCanvas.md` §2 writes down and the chrome its §3 tabulates: windows own widgets, the input router offers events to the topmost window first, a widget that acts on an event consumes it, and nothing in it polls.

### 6.5 Input and audio

**Input** takes the Species `input-native-events` design as its specification, because it is the best-documented piece of engineering in that tree and every rule in it was learned the hard way: one message pump per frame; the window procedure enqueues events and does nothing else; a pure per-frame derivation with one edge per control per frame; Raw Input for camera aim with `WM_MOUSEMOVE` as the fallback, guarded on *a relative packet actually arrived*; text as `WM_CHAR` characters to the focused widget; a router that offers events UI-first and masks a consumed key until release; subscriptions that are move-only handles. And the rule that matters most here: **the simulation never subscribes to input.** Input becomes orders through `Net`, nowhere else.

**Audio** is XAudio2 with X3DAudio positioning, ported from the Species `SoundLibraryXAudio2` backend, with the Species `Sounds.txt` event model — an event per (object kind, event) naming a sample group, a position type, a loop type and parameter curves for volume and pitch — carried across as `Content\Sounds.json`, and the samples as WAV files under `Content\Sounds`. Device loss is handled as Species does: park silent, rebuild every few seconds until a device comes back.

---

## 7. AI

In `Sim`, on the host, deterministic, one planner and a table of personalities (`GameDesign.md` §9). Structurally: an AI seat observes the simulation through the same visibility grid as a human and holds a small blackboard — known enemy structures, a threat map at cluster resolution, its own economy and army composition; each tick, within its budget, it evaluates a fixed list of behaviours (expand, defend, research, build army, attack) with personality weights and emits orders for tick *t + delay* through the same queue as a client. No threads, no wall time, no floats. The highest difficulty takes a power bonus and never vision (owner, 2026-09-17). The scripted opponent of the vertical slice is the same structure with one behaviour list hard-coded.

---

## 8. Content

**Game data is files** (owner, 2026-09-17: R13 withdrawn), **in JSON** (owner, 2026-09-17), under a `Content\` directory beside the executable, resolved from the executable's own path and never from the working directory. R14 still holds, so the JSON reader is written into `Core`: a strict parser of the standard grammar, about three hundred lines, with the file, line and column in every error. The layout:

```
Content\
  Components.json          chassis, drives and modules (GameDesign.md §6), with the derivation formulas
  Research.json            the research tree (GameDesign.md §7)
  Structures.json          the structure catalogue and modules (GameDesign.md §5)
  Damage.json              the weapon-class by target-class modifier matrix (GameDesign.md §8)
  Sounds.json              the sound-event table, the Species Sounds.txt model
  Biomes.json              per biome: palette, water and wave bitmaps, the light pair, fog, sky (SpeciesLook.md, SpeciesTerrain.md)
  Landscapes\*.json        a landscape definition: size class, seed, tiles, stamp placement (SpeciesTerrain.md §2 in JSON)
  Stamps\*.json            authored terrain patches
  Models\*.json            positions, colours, triangles, markers, fragments
  Textures\*.bmp           palettes, water, waves, sprites, icons, the font (24- and 8-bit BMP)
  Sounds\*.wav             16-bit PCM, which XAudio2 plays as it is
Mods\<name>\...            the same tree; a file here overrides the one at the same path under Content\
```

**Mods** are directories under `Mods\` beside the executable, enabled by name in the lobby; the loader reads `Content\` and then each enabled mod in order, and a file in a mod replaces the file at the same path. Nothing else is needed for a mod that changes numbers, adds a component or a model, or replaces a sound.

**Validation happens twice, with the same rules.** At load, `Content` checks that every research prerequisite names an existing item and the tree has no cycle; that every component's unlock names an item; that every model, texture and sound a table names exists; that no id is duplicated; that every number is in its range — and refuses to start on a failure, naming the file and line. In CI, `Build/CheckContent.py` runs the same checks over `Content\` on every push, so a table edit that would break the game breaks the build first. The two implementations are kept in step by a shared list of rules in the checker's header comment and a test that feeds the same broken files to both.

**The content hash** is a 64-bit digest over the bytes of every file loaded, in load order, mods included, computed at start and sent at join (§5.4). Two players with different files are refused each other's matches before the first tick.

**Importers**, under `Tools/`, never ship: `ImportShp.py` converts a Species `.shp` into a model JSON, keeping fragments, colours, triangles from both encodings and markers (the review tool that rendered the Species set is its prototype); `ImportSounds.py` selects and copies WAVs by the names `Sounds.txt` references. Textures are BMPs already and need no conversion; PNG, which would need an inflate implementation under R14, is an ADR for the day it matters.

**Shaders are the one thing still compiled in**: HLSL under `Client/Shaders/`, compiled at build time by `FXCompile` (`AGENTS.md` §2), never loaded at runtime.

---

## 9. What the executable reads and writes

Decided (owner, 2026-09-17):

| Where | What | Access |
|---|---|---|
| `<executable directory>\Content\` | the game's data (§8) | read; the install may be read-only |
| `<executable directory>\Mods\` | mods (§8) | read |
| `%LOCALAPPDATA%\FrontierCommander\` | everything the game writes, created on first use | read and write |

Under the user directory:

| File | Written by | Form |
|---|---|---|
| `Preferences.json` | client | scaling mode, key bindings, audio volumes, player name; absent means defaults |
| `Designs.json` | client | the commander's saved device designs (`GameDesign.md` §6) |
| `Saves\*.fcsave` | host | a snapshot (§4.9) plus settings, versioned |
| `Replays\*.fcreplay` | host | settings, seed, order stream, versioned; always recorded, the oldest pruned |
| `Logs\Host.log` | host | what the host did and why a client was refused; rotated |

The headless host, run as a service or by another user, uses the same layout under its own profile. Nothing resolves against the working directory. ADR-007 records the preferences schema and ADR-008 the save and replay formats.

---

## 10. Testing

- **`Sim` is the bulk of the suite, and determinism is its first test.** Two matches from one seed and one order stream hash identically at every tick; a snapshot taken at tick *n* and reloaded continues to the same hashes as the original; a replay reproduces a recorded match hash for hash. These three tests exist from M0, and every later feature runs under them.
- **`Net` is tested over `LoopbackTransport`**, as Species does: join, a full frame, deltas against an acked baseline, a lost frame recovered by the next delta, a full frame after the history is exhausted, orders delivered reliably through loss, a slow client dropped to AI — the conversation, not the encodings.
- **Interest is a security property and is tested as one**: for every publish in a scripted match, no record in a client's frame names an object outside its commander's visibility, own objects and ghosts. This is the test that makes the fog of war real.
- **`Replica` converges**: applying a host's frames, with and without loss, produces a replica equal to the host's interest set at every acked frame, and interpolation never places an object outside the segment between two frames.
- **`Content`** loads the shipped tree, refuses each of a set of deliberately broken files with the right file and line, and applies a mod overlay; `CheckContent.py` agrees with the loader on every broken file.
- **`Core`** tests the JSON reader against a conformance set (including the pathological documents), the BMP and WAV readers against small fixtures, and its arithmetic exhaustively where the domain is small (the binary-angle tables, the integer square root, the fixed-point multiply).
- **`Client` and the executables** are tested by running them (`AGENTS.md` §3); the pure parts — the input derivation, the UI router — get unit tests, as the Species input work showed they can.

A `SuiteSmoke` placeholder in every test project until its first real test, as `AGENTS.md` §3 requires, because vstest reports an empty suite as a pass. Tests never open a socket, a window or an audio device, and never read `Content\` in place — a test that needs files writes its own under a temporary directory — the Species `docs/TESTING.md` rules, which are the right rules.

---

## 11. Risks

| Risk | Why it is real | What the design does about it |
|---|---|---|
| **Scope** | This is a strategy game with a design system, a research tree, an AI, replicated multiplayer and a large-map renderer, with no library for any of it, by one developer | Milestones that are playable states, a vertical slice that cuts everything not on the critical path, and data files so that content is not code |
| **Replication** | A second world model that must stay a faithful reading of the host's; interest management that is also the fog-of-war security; bandwidth peaks in large battles; a view that is always 100–200 ms behind | `Replica` as a tested library, the interest test of §10, the arithmetic of §5.7 measured in ADR-004, and no prediction to get wrong |
| **Large landscapes** | Pathing, visibility and terrain rendering scale with the map; a Frontier landscape is over sixty times a *Warzone* map | Hierarchical pathing, budgeted visibility, chunked terrain — and M4, not M1, is where Frontier-class maps must perform |
| **Hand-written readers** | JSON, BMP and WAV readers under R14 are small and are exactly where a malformed file becomes a crash | Strict parsers with fixtures and conformance tests; content refused at load with a location, never partially loaded |
| **D3D12 without helpers** | R14 removes `d3dx12.h`, so barriers, heaps and root signatures are hand-written | Seven fixed pipelines, no material system, no generality: write the little the game needs, once |
| **Provenance of the Species content** | The effects, palettes, sprites, icons and the Darwinia-derived code have no established licence, and the owner has chosen to use them and carry the risk (2026-09-17) | ADR-006 records the decision and the list; the soundtrack, the branding and the narration are excluded regardless |
| **Text at scale** | A pixel font resampled is the cost of the scaled present | 1:1 at the authored resolution is the common path; the ADR settles the resolution with that in mind |

---

## 12. The first decisions, as ADRs

The ADRs the first tasks will write, in the order the work meets them. Where the owner has already decided, the ADR records the decision and adds the measurement.

| ADR | Decision | Written by |
|---|---|---|
| 001 | The solution and project layout of §2, the eight projects and their edges, the two namespaces | The first project |
| 002 | Authored resolution, window style, scene target multisampling | The first renderer task |
| 003 | The 20 Hz tick, measured; the position unit and the fixed-point formats of §4.1 | The first `Sim` task |
| 004 | The network model — host-authoritative replication, as decided — and the measured protocol numbers: publish rate, history length, quantisation, interest cost | The first `Net` task |
| 005 | The content directory, the JSON schemas and their versioning, the overlay rule for mods, the content hash | The first loader |
| 006 | The Species-derived content that came across and the accepted provenance risk; the Spectrum font | The first importer run |
| 007 | The user directory and the preferences schema | The first task that needs a setting to persist |
| 008 | The snapshot and replay formats | The first save |
