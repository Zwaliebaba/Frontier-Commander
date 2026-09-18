# ADR-003 — The snapshot and replay formats

**Status:** Accepted; the refusal of compression is superseded, for the fog grid alone, by [`ADR-008`](ADR-008-fog-grid-encoding.md) (2026-09-18), on the measurement this ADR asked for. Everything else here stands.
**Date:** 2026-09-17
**Owner:** the author, on `Design/TechnicalDesign.md` §4.9 and §9

## Context

The determinism tests of `TechnicalDesign.md` §10 need a snapshot from M0: a `Sim` written whole and read back indistinguishable, at tick 5,000 of a 10,000-tick match. The save file of M2 and the replay file of M3 are these formats with a file around them, so the layout is fixed now and cited then rather than invented twice (`ImplementationPlan.md` §6, item 5). Both go through the one versioned byte stream of `Core` (`ByteWriter`, `ByteReader`; `m0-foundation/T10`): little-endian integers, length-prefixed spans with a caller's bound, and a reader that stays failed.

## Decision

**A snapshot** is one stream, written by `Frontier::Snapshot::Write` and read by `Snapshot::Read` (`Sim/Snapshot.h`), in this order and nothing else:

| Field | Bytes | Note |
|---|---|---|
| magic | 4 | `"FCSP"`, `SNAPSHOT_MAGIC` = 0x50534346 |
| version | 2 | `SNAPSHOT_VERSION`: 2 with the landscape section (`m0-foundation/T17`), 3 with a tile's palette (`OpenQuestions.md` Q18), 4 with the object maps and the seat's own state (`m1-vertical-slice/S1`) |
| settings | 34 | seed (8), size class, seat count, base level, power level, technology tiers, victory (1 each), survival ticks (4), then eight seats' kind and alliance (2 each): the lobby, verbatim |
| tick | 4 | |
| Random state | 16 | the four xoshiro128\*\* words |
| seat count, then per seat | 1 + 44 each, plus what the seat holds | kind, alliance, power and stockpile cap in hundredths (4 each); the counts of completed research (4 each), research in progress (4 + 4 each) and designs (chassis, drive, eight mount slots and a count, 41 each); device and structure counts and caps (4 each); the two fog grids, run-length encoded (ADR-008); the ghost count and each ghost (22); defeated and surrendered |
| landscape | 1, then the definition and the deltas when there is one | a created flag; the definition's version (4), size class (1), cells per side (4), seed (8), palette as a length-prefixed span, the tile count (4) and each tile's ten fields (37), the start and deposit counts and positions (4 + 8 each); then the delta count (4) and each delta's rectangle (16) and `int16` heights. The samples are never written: the definition and the deltas reproduce every one of them (`TechnicalDesign.md` §4.4) |
| object maps | 24, plus the records | one count and its records per kind, in ascending id order, then the creation counter (4). The counter is written because it is state: two worlds holding the same records would otherwise issue different ids from the next tick on |
| — devices (`S1`) | 77 each | id (4), seat, design (4), position (12), facing (2), hit points (4), experience (4), target (5), destination (8), moving, eight reload counters (32) |
| — structures (`S1`) | 56 each | id (4), seat, design (4), footprint cell (8), height (4), state, hit points (4), build progress (4), four module slots (16) and a count, what it is working on (5) and the ticks left (4) |
| — projectiles (`S1`) | 42 each | id (4), seat, shooter (5), module (4), position (12), impact (12), ticks to impact (4) |
| — features (`S1`) | 22 each | id (4), design (4), cell (8), height (4), facing (2) |
| — wrecks (`S1`) | 32 each | id (4), seat, origin (5), design (4), position (12), facing (2), decay ticks (4) |
| last targeting roll | 4 | stage 8's draw, so that the hash reads back |
| applied and dropped order counts | 8 | |
| finished, winning alliance, publish due | 3 | |
| hash | 8 | the stage-13 hash of the last tick, so that `Hash()` reads back |
| next arrival, pending count, then per pending order | 8 + 26 each | the arrival number (4) and the order (22, below); arrival numbers are what keep a reloaded queue in the original's order |
| digest | 8 | FNV-1a 64 over every byte before it |

The reader builds nothing until it has refused nothing: a different magic or version, a value outside its enumeration, a seat count outside `MIN_SEATS`..`MAX_SEATS` or different from the settings', a landscape the generator refuses or a delta outside it, more than 4,096 tiles or positions, a palette over 256 bytes, a pending count over 2^20, a stream that ends early or runs on, or a digest that differs, and `Read` returns nothing. A snapshot is host-side only (§4.9): a client never holds one.

**An order in a stream** (`Sim/Order.h`, `WriteOrder`/`ReadOrder`): the tick (4), four operands (4 each), the seat (1) and the kind (1), 22 bytes, `ORDER_STREAM_BYTES`. The same bytes in the snapshot's queue, in a replay and on the wire (the network ADR cites this one); a kind outside the twenty is refused.

**A replay** is the settings, the seed and the order stream in submission order: magic `"FCRP"`, version, the settings as above, an order count, the orders, then (tick, hash) checkpoints every 100 ticks and at the final tick, so that a replay is checked against the match it records rather than merely played. The in-memory form is what `SimTests::DeterminismTests` replays today: the whole stream is submitted up front, which is sound because the hash leaves the pending queue out (ADR-002). The file's writer and player arrive with M3 (`m3-multiplayer`), and the save file of M2 is the snapshot with the replay so far beside it, so that a saved match can still be replayed from its start.

**Versioning.** One version number for the whole stream, and any change to the layout bumps it; a field is never reinterpreted under an old number. Until the first save file ships (M2), the reader accepts exactly the version it was built with, because nothing older exists to keep. From M2 a reader keeps every version it can migrate forward and refuses the rest by number, saying which.

## Consequences

- A save, a replay and a test snapshot are one code path, so the determinism tests exercise the save file from M0.
- A truncated or altered file is refused whole rather than read past; there is no partial load.
- Every object kind added to `Sim` is a version bump and a row in the table above, in the same commit. `S1` added all five at once and the table carries a row apiece; a field added to one of them is the same obligation.
- The sizes hold the estimate of §4.9: 5,000 objects at about 64 bytes each is under 2 MB for a Large landscape, and the five records `S1` added average 46 bytes, so the estimate holds as written. Compression, or per-section versions, would reopen this ADR, and only a measured snapshot over that estimate would justify either. **That happened on 2026-09-18**: the fog grid a seat gained with `S1` is the first O(area) state a snapshot has to carry, 16.8 MB on a Frontier landscape with eight seats, and [`ADR-008`](ADR-008-fog-grid-encoding.md) run-length encodes that one section on exactly the measurement this line demands. Every other section stays uncompressed and the bar is unchanged.

## Measurements

The snapshot of the three-seat match `SimTests::SnapshotTests` builds was 200 bytes at version 2, when the match held no objects and a seat held four fields; arithmetic on the table as it then stood gave the same: 6 + 34 + 4 + 16 + 1 + 21 + 1 + 4 + 8 + 3 + 8 + 8 + 78 + 8. At version 4 the same test builds two devices, a structure, a projectile, a feature, a removed wreck and three seats with research, designs, caps, fog and ghosts, and its snapshot is **1,251 bytes**, as `Snapshot::Write` returns it and the test prints it. With a Small landscape and one flatten delta of 25 samples the snapshot stays under 4 KB where the samples alone would be 526 KB, which `SimTests::LandscapeTests` asserts. Nothing rests on the round-trip time and it is not measured.
