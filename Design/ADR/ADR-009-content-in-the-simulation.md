# ADR-009 — The simulation reads the content tree, and a snapshot is bound to it

**Status:** Accepted
**Date:** 2026-09-18
**Owner:** the owner, answering `OpenQuestions.md` Q20 on 2026-09-18; supersedes ADR-003 on `Snapshot::Read`'s signature and adds one field to its layout, and nothing else in it

## Context

`m1-vertical-slice/S3` was the first task that could not be built. Its acceptance is precise — a generator serves the four nearest unserved extractors within 48 cells, the stockpile is capped at 1,000 plus 500 per completed generator, demolition refunds half — and every clause needs the simulation to know which structure row is a generator, an extractor or a command post, and what each costs. `Content/StructureDesc.h` already carries the right `StructureRole` enum, and `C2`'s own intent says every simulation task reads content rather than literals. **Nothing wired the `ContentTree` into `Sim`, and no task in any plan owned it**: `Sim`'s constructor took `MatchSettings` and nothing else. `S4`, `S5`, `S6` and `S10` — structures, devices, research and combat — wait on the same thing.

It reaches past the constructor. `Snapshot::Read` reconstructs a `Sim` from bytes alone, so a `Sim` that needs content changes a signature ADR-003 fixed. And it reaches determinism: a match reloaded against different tables is a different match, which today would diverge in silence. `m3-multiplayer/T3` builds a content hash so that two machines agree before a match; the exposure starts here, three milestones earlier, on one machine with a save file.

## Decision

**`Sim` takes the tree by reference at construction and never writes it.** `Sim(const MatchSettings&, const ContentTree&)`, held as a pointer, exposed as `Content()`. ADR-006 already has one process hold one tree that nothing writes to after loading, so this adds no ownership and no lifetime rule that was not already true. The rvalue overload is **deleted**, so binding a temporary is a compile error rather than a dangling pointer discovered at run time.

**A snapshot carries the digest of the tables it was written against, and `Read` refuses any other.** `Snapshot::Read(bytes, content)` computes `ContentHash(content)` and compares it to the one in the stream before it reads a single field, at snapshot version 6. This is the treatment altered bytes already get from ADR-003's digest, applied to the other half of what determines a match: the bytes say what the state is, the tables say what the rules are, and a reader that checks one and not the other is checking half.

**The digest covers what the simulation reads and nothing else** (`Content/ContentHash.h`): components, structures, research and the damage matrix. Biomes, models and sounds are the client's and change no outcome, so a player with different art plays the same match — and `m3-multiplayer/T3`'s join check may want a wider digest over the whole tree, which this deliberately is not. Every field of every row is fed in the tree's own order, with strings length-prefixed so that two rows whose ids run together cannot digest as one.

**What this forecloses is the self-describing snapshot.** The alternative the owner weighed was for `Sim` to derive its own rules and the snapshot to carry them, so that a file reloaded years later on a machine with no tables plays identically. That is genuinely better for a replay meant to outlive its tables, and it is refused here because every snapshot would then carry every row — chassis, drives, modules, structures, research, the damage matrix — which are identical between almost any two snapshots and which ADR-003's size estimate was written without. A replay is therefore only as durable as the tables beside it, and `m3-multiplayer/T6`, which builds the replay file, is where that is paid for if it turns out to matter.

**A process-wide tree was refused outright.** It is the cheapest to write and the only option that makes `DeterminismTests` impossible as written: the host and the replay checker live in one process and must be able to hold different tables.

## Consequences

- Every `Sim` construction and every `Snapshot::Read` gains an argument — 27 call sites, all in tests and `FrontierCommander`. A suite that exercises the simulation rather than the rules passes an empty tree, which is honest: no row is read, and the binding is still exercised because the snapshot carries that tree's digest and refuses any other.
- `FrontierCommander` passes an empty tree until `C2` authors the tables and the loader is wired to `GameData`. That is a real gap, visible in one function, rather than a literal buried in a system.
- **A field added to a content row that `ContentHash` does not feed is two different rule sets that digest alike** — a silent divergence, which is the failure this exists to prevent. `ContentHashTests` moves one field of every table and each mutation must move the digest; a new field arrives with a line in the digest and a line in that test, exactly as a new simulation field arrives with a line in the state hash.
- The state hash is untouched. The content digest is a precondition checked once at load, not part of the per-tick state: two hosts with the same tables and the same orders hash the same, and two with different tables never get as far as comparing.
- `S3`'s plan edge to `C2` is added, and a task for this wiring with it: the work existed and the plan did not say so.
- What would reopen it: a replay that has to outlive its tables, which is the self-describing snapshot above and a size question rather than a new argument.

## Measurements

- **The call sites**: 27, found by searching for `Sim` constructions and `Snapshot::Read` across `Tests/` and `FrontierCommander/`. All now compile and the suites pass: CoreTests 81, ContentTests 34, SimTests 44 on the session's Linux harness.
- **The snapshot's growth** is eight bytes, the digest, and nothing else: the three-seat match of `SnapshotTests` is 1,281 bytes at version 6 against 1,273 at version 5. ADR-003's estimate is unaffected.
- **A bug the digest found on its first run**, and the reason this section exists rather than being arithmetic: hashing the same fixture twice gave two answers. `DamageTable`'s three arrays had no initialisers, and it is the only member of `ContentTree` that is not a `std::vector`, so `ContentTree tree;` left the damage matrix holding whatever was in that memory. Every call site in the tree happens to write `{}`, so nothing had ever caught it and nothing would have until a match rolled damage against garbage. The arrays are now initialised where they are declared.
