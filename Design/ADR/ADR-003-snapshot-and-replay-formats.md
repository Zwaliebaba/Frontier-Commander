# ADR-003 — The snapshot and replay formats

**Status:** Accepted; `Snapshot::Read`'s signature and one field of the layout are superseded by [`ADR-009`](ADR-009-content-in-the-simulation.md) (2026-09-18), which binds a snapshot to the content it was written against; the refusal of compression is superseded, for the fog grid alone, by [`ADR-008`](ADR-008-fog-grid-encoding.md) (2026-09-18), on the measurement this ADR asked for. Everything else here stands.
**Date:** 2026-09-17
**Owner:** the author, on `Design/TechnicalDesign.md` §4.9 and §9

## Context

The determinism tests of `TechnicalDesign.md` §10 need a snapshot from M0: a `Sim` written whole and read back indistinguishable, at tick 5,000 of a 10,000-tick match. The save file of M2 and the replay file of M3 are these formats with a file around them, so the layout is fixed now and cited then rather than invented twice (`ImplementationPlan.md` §6, item 5). Both go through the one versioned byte stream of `Core` (`ByteWriter`, `ByteReader`; `m0-foundation/T10`): little-endian integers, length-prefixed spans with a caller's bound, and a reader that stays failed.

## Decision

**A snapshot** is one stream, written by `Frontier::Snapshot::Write` and read by `Snapshot::Read` (`Sim/Snapshot.h`), in this order and nothing else:

| Field | Bytes | Note |
|---|---|---|
| magic | 4 | `"FCSP"`, `SNAPSHOT_MAGIC` = 0x50534346 |
| version | 2 | `SNAPSHOT_VERSION`: 2 with the landscape section (`m0-foundation/T17`), 3 with a tile's palette (`OpenQuestions.md` Q18), 4 with the object maps and the seat's own state (`m1-vertical-slice/S1`), 5 with a device's primary order and stances and a seat's dropped orders (`S2`), 6 with the content digest (ADR-009), 7 with the lobby's device cap (`S3`), 8 with the fog grid's side and its wider viewer count and the visibility stamps (`S9`), 9 with a structure's module under construction (`S4`), 10 with a seat's class upgrades and production queues (`S5`), 11 with a lab's research and the lobby's auto-research (`S6`), 12 with a device's patrol anchor, route index and stall count and the planning queue (`S8`), 13 with a structure's weapon reload and the chance and damage a shell was fired with (`S10`) |
| content digest | 8 | `ContentHash` of the tables the match is played by (ADR-009). Read and compared before any field, so a stream written against other rules is refused rather than read |
| settings | 34 | seed (8), size class, seat count, base level, power level, technology tiers, victory (1 each), survival ticks (4), then eight seats' kind and alliance (2 each): the lobby, verbatim |
| tick | 4 | |
| Random state | 16 | the four xoshiro128\*\* words |
| seat count, then per seat | 1 + 148 each, plus what the seat holds | kind, alliance, power and stockpile cap in hundredths (4 each); the counts of completed research (4 each), research in progress (4, then 13 each: the lab, the row and the ticks left — `S6`) and designs (chassis, drive, eight mount slots and a count, 41 each); the class upgrades (`S5`, `S6`): three chassis-class armour and hit-point percentages, five weapon-class damage, rate and accuracy percentages, and the extractor-rate and structure-hit-point percentages, 92 in all; the production queues (4, then 13 each: the factory, the design and how many are left — `S5`); device and structure counts and caps (4 each); the two fog grids, run-length encoded (ADR-008); the ghost count and each ghost (22); this tick's dropped orders (4, then 2 each: the kind and the reason, which is what Net reports and the client displays); defeated and surrendered |
| landscape | 1, then the definition and the deltas when there is one | a created flag; the definition's version (4), size class (1), cells per side (4), seed (8), palette as a length-prefixed span, the tile count (4) and each tile's ten fields (37), the start and deposit counts and positions (4 + 8 each); then the delta count (4) and each delta's rectangle (16) and `int16` heights. The samples are never written: the definition and the deltas reproduce every one of them (`TechnicalDesign.md` §4.4) |
| object maps | 24, plus the records | one count and its records per kind, in ascending id order, then the creation counter (4). The counter is written because it is state: two worlds holding the same records would otherwise issue different ids from the next tick on |
| — devices (`S1`, `S2`, `S8`) | 98 each | id (4), seat, design (4), position (12), facing (2), hit points (4), experience (4), primary order, target (5), destination (8), the patrol anchor (8), how far along its route it has walked and how long it has been stuck (8 — `S8`), the four stances (4), control group, eight reload counters (32) |
| — structures (`S1`, `S4`, `S10`) | 68 each | id (4), seat, design (4), footprint cell (8), height (4), state, hit points (4), the builders' effort (4 — `S4` renamed it from a progress percentage that could never reach a hundred), the weapon reload (4 — `S10`), four module slots (16) and a count, the module under construction and what has gone into it (8 — `S4`), what it is working on (5) and the ticks left (4) |
| — projectiles (`S1`, `S10`) | 50 each | id (4), seat, shooter (5), module (4), position (12), impact (12), ticks to impact (4), and the chance and damage it was fired with (8 — `S10`: a shell outlives its shooter, so it carries what it was fired with rather than looking it up at the impact) |
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
- **That obligation was not kept between versions 7 and 12, and the table was brought current at version 13 (2026-09-19).** `S3` through `S9` each bumped `SNAPSHOT_VERSION` and updated its history comment in `Sim/Snapshot.h`, and none of them updated this ADR — so the record here said 6 while the code said 12, and the record shapes were `S1`'s. Worth stating rather than quietly fixing: the comment in the header is what each task reached for because it is next to the number, and this table is the one a reader outside the code finds. The two say the same thing again now, and the header comment cites the task for every version so the two can be diffed by eye.
- The sizes hold the estimate of §4.9: 5,000 objects at about 64 bytes each is under 2 MB for a Large landscape. The five records averaged 47 bytes at version 5 and average **54** at version 13, after `S4`, `S8` and `S10` widened three of them; the estimate still holds and the margin is now a fifth rather than a quarter, which is worth watching rather than acting on. Compression, or per-section versions, would reopen this ADR, and only a measured snapshot over that estimate would justify either. **That happened on 2026-09-18**: the fog grid a seat gained with `S1` is the first O(area) state a snapshot has to carry, 16.8 MB on a Frontier landscape with eight seats, and [`ADR-008`](ADR-008-fog-grid-encoding.md) run-length encodes that one section on exactly the measurement this line demands. Every other section stays uncompressed and the bar is unchanged.

## Measurements

The snapshot of the three-seat match `SimTests::SnapshotTests` builds was 200 bytes at version 2, when the match held no objects and a seat held four fields; arithmetic on the table as it then stood gave the same: 6 + 34 + 4 + 16 + 1 + 21 + 1 + 4 + 8 + 3 + 8 + 8 + 78 + 8. At version 4 the same test builds two devices, a structure, a projectile, a feature, a removed wreck and three seats with research, designs, caps, fog and ghosts, and its snapshot was **1,251 bytes**, as `Snapshot::Write` returns it and the test prints it. At **version 13** the same fixture is **1,689 bytes**: the growth is the seat's class upgrades and production queues (`S5`, `S6`), the wider records of `S4`, `S8` and `S10`, and the planning queue (`S8`), and the test prints the figure on every run so that the next widening is read off a run rather than recomputed here. With a Small landscape and one flatten delta of 25 samples the snapshot stays under 4 KB where the samples alone would be 526 KB, which `SimTests::LandscapeTests` asserts. Nothing rests on the round-trip time and it is not measured.
