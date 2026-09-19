# ADR-008 — The fog grid is run-length encoded in the snapshot

**Status:** Accepted
**Date:** 2026-09-18
**Owner:** the author, on the measurement below; supersedes ADR-003 on compression and nothing else in it

## Context

ADR-003 fixed the snapshot's layout and closed with a condition: "Compression, or per-section versions, would reopen this ADR, and only a measured Large snapshot over that estimate would justify either." The estimate it was guarding is §4.9's — 5,000 objects at about 64 bytes each, under 2 MB on a Large landscape — and every section written until now is O(objects) or O(the landscape's definition), never O(the landscape's area). That is the property `SimTests::LandscapeTests::TheSnapshotCarriesTheDefinitionAndTheDeltasNotTheSamples` pins: a Small landscape's snapshot stays under 4 KB where its samples alone are 526 KB.

`m1-vertical-slice/S1` gave a seat the fog grid `TechnicalDesign.md` §4.6 specifies: per commander and per cell, a viewer count of one byte and a two-bit state derived from it. That is the **first O(area) state the simulation holds**, and the first that a snapshot has to carry, because it cannot be recomputed: a cell stays explored after the last viewer leaves, so the explored map is history rather than a function of the present. Written densely it is two bytes per cell per seat — 65,536 bytes on the Small landscape of that test, which the test caught at once, and **16.8 MB on a Frontier landscape with eight seats**, against an estimate of 2 MB for the whole file. ADR-003's condition is met on the measurement it asked for.

## Decision

**A fog grid is written as runs, not as cells.** Each of the two grids is a cell count, a run count, and then that many runs of a length (`std::uint32_t`) and a value (`std::uint8_t`). The reader refuses a run of zero length, a run that would carry the grid past the cell count it declared, a value outside the state's enumeration, and a pair of grids whose cell counts differ.

It is **run-length encoding and nothing more**: no entropy coder, no library, no dependency (`AGENTS.md` R14), and the runs are a pure function of the bytes, so two hosts write the same snapshot for the same state and `SnapshotTests::WritingTheSameSimTwiceGivesTheSameBytes` still holds. The state hash reads the grid as cells and is untouched, because the encoding is the file's and not the simulation's.

**It fits what a fog grid actually is.** A commander explores a fraction of a landscape and the rest stays one value for the whole match, so the cost tracks what has been seen rather than how large the map is: an untouched grid is one run, nine bytes. That is also why no other section takes this treatment — the object maps are O(objects) and already proportionate, and compressing them would be the speculative kind of compression ADR-003 was right to refuse.

**What this does not do is make the fog grid small enough to stop thinking about.** A fully explored Frontier landscape is worst case for the encoding, and a seat that has seen all of it pays close to the dense size. ADR-007 named the same shape of problem in the renderer and said the answer for a landscape ten times a Frontier side is tiles, with ground no commander has reached costing nothing; the same answer serves here, and `m1-vertical-slice/S9`, which fills these grids, is where it is decided on its own measurements rather than guessed at here.

## Consequences

- ADR-003's refusal of compression is superseded for this one section, on the condition ADR-003 itself set. Every other section stays uncompressed, and the next section that wants this has to meet the same bar: a measurement over the estimate, not an argument.
- A snapshot's size now depends on how much of the map has been explored, not only on what is in it. A save file grows through a match for a reason that is not the object count, which is worth knowing before anyone reads a size as a fault.
- The reader gained a way to be lied to that it did not have: a run count and lengths that disagree with the cell count. It refuses all three disagreements, and the bound `Snapshot::MAX_FOG_CELLS` (2^20, a Frontier landscape's cell count) caps what a hostile file can make it reserve.
- The encoding is per grid rather than per file, so the digest, the header, the version rule and every other section are exactly as ADR-003 left them.
- What would reopen it: `S9`'s measurement showing a played-out match's grids near their dense size often enough to matter, which is the tiling question above rather than a better coder.

## Measurements

- **The dense cost**, arithmetic on `TechnicalDesign.md` §4.6's byte per cell per grid: a Small landscape is 128 cells a side, so 16,384 cells, two grids, two seats — 65,536 bytes, which took the snapshot of `LandscapeTests::TheSnapshotCarriesTheDefinitionAndTheDeltasNotTheSamples` from 1,479 bytes to over 66 KB and failed its 4 KB assertion. A Frontier landscape is 1,048,576 cells: eight seats and two grids is 16.8 MB.
- **The encoded cost**, as built: an untouched grid is a cell count, a run count and one run — 13 bytes. The three seats of `SnapshotTests`, each with two grids of 64 cells holding two and three distinct values, cost 23 and 33 bytes a seat.
- **The snapshot of the three-seat match** is **1,251 bytes at version 4**, as `Snapshot::Write` returns it and `SnapshotTests` prints it, against 200 bytes at version 2 before that match carried any objects. The rise is the two devices, the structure, the projectile, the feature and the three furnished seats the test now builds, not the encoding.
- All of these are on the Linux harness of this session; the figures are byte counts and do not depend on the machine.
