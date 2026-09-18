# ADR-006 — The content format

**Status:** Accepted
**Date:** 2026-09-17
**Owner:** the author, on `Design/TechnicalDesign.md` §8 and the owner's ruling of 2026-09-17 (JSON under `Content\`, textures DDS)

## Context

`TechnicalDesign.md` §8 fixes that game data is JSON files under `Content\` beside the executable, lists the files, and requires that validation happen at load and in CI **with one implementation**. It leaves to the first loader (`m1-vertical-slice/C1`) what the schemas actually are: what each row holds, in what unit, inside what range, and what a version field means when a reader meets a file it does not know. Every simulation task of M1 reads these tables, so the answer has to exist before `S1`.

## Decision

**One row aggregate per table, in `Content`, in the `Frontier` namespace**, a plain aggregate apiece (`AGENTS.md` R8): `ChassisDesc`, `DriveDesc`, `ModuleDesc` (`ComponentDesc.h`); `ResearchItemDesc`; `StructureDesc` and `StructureModuleDesc`; `DamageTable`; `BiomeDesc`; `LandscapeDefinition` (already there from M0); `StampDesc`; `ModelDesc`; `SoundEventDesc`. `ContentTree` holds them all, and one process holds one tree that nothing writes to after loading.

**Every number is an integer in the unit its name says** (`AGENTS.md` R6; ADR-002), and the name carries the unit: `costHundredths`, `buildTimeTicks`, `sightSubunits`, `speedFactorHundredths`, `weightPenaltyPercent`. Factors and percentages are hundredths, times are ticks, distances are subunits of 1/256 world unit, power is hundredths. Nothing in `Content` is a float — not because R16 reaches here, which it does not, but because a row is compared, hashed for the content digest of M3 and replicated, and a float is unreliable at all three. The renderer's own numbers, a light's direction and colour, are hundredths for the same reason and the client converts them once.

**A row that names a model also carries the scale it is drawn at**, `modelScaleHundredths`, absent meaning 100 and native. It sits on the row rather than on the model, so that one model serves two rows at two sizes: a placeholder primitive stands in for several things before the authored models exist, and the Species review found shapes needing 0.45 and 0.6 to fit the footprints they were given (`SpeciesLineage.md` §5). Scaling a model scales its markers with it, so a drive or a module attaches where the scaled chassis puts it; that is the consumer's arithmetic (`m1-vertical-slice/R2`), and this ADR only fixes where the number lives.

**A table authored in seconds is converted once, in `Content`.** `Neuron::TICKS_PER_SECOND` is declared in `Core/FixedPoint.h` beside the other units, which ADR-002 fixed at 20 and no code had yet needed.

**Every file carries a `version`, and a reader refuses a version it does not know by name** rather than reading it as best it can. The table files are at version 1 together; the landscape, stamp and model documents carry their own, declared beside their row, because a tool writes them one at a time.

**Loading is fail-fast and validation is exhaustive.** `LoadContent` reads the files in a fixed order — components, structures, research, damage, biomes, sounds, then the `Landscapes`, `Stamps` and `Models` directories sorted by name — and stops at the **first** file that fails, with one diagnostic naming the file, line and column; a tree that fails leaves the caller's `ContentTree` untouched, so there is never a half-read table. `ValidateContent` then reports **every** fault it finds, because a table edit usually breaks several rows and one pass should show them all.

**Every diagnostic names a line**, including the ones that span files. `Neuron::JsonValue` gained a line and column, set by the parser, so a fault in what a value *means* — a number out of range, an unknown enumeration name — points at the value rather than at the document. For a fault that spans files, `ContentTree::origins` records the file and line of every row as it is read, and the validator reports a missing prerequisite or a duplicate id at the line of the row that is wrong. Rows themselves carry no line: a row is compared, hashed and replicated, and a line is none of those things.

**The validator's rules**, in one place, are: no id is used twice across every table; every research prerequisite exists and is not the item itself; the research tree has no cycle; every unlock names a chassis, drive, module, structure or structure module; every `unlockedBy` names a research item; a structure's modules exist and fit its slots, and its weapon is a module; a weapon's long range is not under its short range and an indirect weapon's minimum range is under its long range; every landscape's palette names a biome and it has at least one start; and, when a directory is given, every model, texture and wave a row names is on disk.

**`FrontierHost --validate [directory]`** runs both with that same code and exits 0 or non-zero, printing every diagnostic in `file(line,column): message`, the form the build tools print. It defaults to `Paths::ContentDirectory()`. CI runs it as a step guarded on `Content\Components.json` existing, so that a table edit that would break the game breaks the build first, without a second implementation of the rules to keep in step.

**`DesignStats` is the one implementation of the derivation formulas** of `GameDesign.md` §6. The design screen shows what the factory will build because both call it. It is integer throughout, with one rounding — half up — at the end of each statistic's chain, which is what makes a light on wheels with a machine gun 104 world units a second rather than 103, and a heavy on tracks with a cannon 29 rather than 28. Research upgrades are passed in as class percentages rather than read from a seat, because `Content` knows nothing of seats.

**The directory layout is `TechnicalDesign.md` §8's, unchanged.** Mods (M3) overlay the same tree path for path; nothing in this ADR forecloses that, because the loader takes a directory.

## Consequences

- A table edit that breaks a rule fails CI with a file and a line, which is the whole point; a rule the validator does not have is a rule nobody enforces, so adding a cross-file rule means adding it here.
- The ranges live in the loader rather than in the schema files. A range that is wrong is a code change, not a data change. The alternative, a schema language, is a second thing to keep in step with the rows and was refused.
- `JsonValue` grew two integers. A content tree is tens of kilobytes, so the cost is nothing; a program parsing megabytes of JSON with this reader would notice, and none does.
- Rows keep the order their files declare, so a listing is a loop and the content hash of M3 has a stable order to digest. A lookup by id is a linear search, which is right for tables of tens of rows and wrong for thousands; if a table ever reaches thousands, this is the decision to revisit.
- Nothing yet loads `Content\Interface.json`, the chrome palette `Design/Interface.md` §3 specifies. It is a table like any other and `C2` adds it with its row aggregate.
- A structure module names a model like everything else, and the placeholder set of `C4` had none, which `C2` would have met as a validation failure. `C4`'s file list now carries one per M1 structure module.
- A landscape definition is validated against **this build's** constants — samples a side, sample spacing, the outside height — rather than reading them, so a definition written by a tool of another shape is refused rather than silently regenerated differently.

## Measurements

- **The two worked examples of `GameDesign.md` §6 reproduce exactly** from the formulas in `DesignStats.cpp`, against literal rows in `Tests/ContentTests` until `C2`'s tables exist: a light on wheels with a machine gun is 104 world units a second, 130 power, 13 seconds; a heavy on tracks with a cannon is 29, 490, 49. The second is the one that pins the rounding: 40 × 0.8 × 0.9 is 28.8, and the design says 29.
- **The diagnostics' lines are asserted by number**, not by presence: the loader tests write a document whose fault is on a known line and check that line, for a malformed file, a number out of range, a missing member and an unknown enumeration name; the validator tests do the same for a missing prerequisite, a cycle, an unknown unlock and a duplicate id, the last of which names both lines.
- **Twenty-seven tests** over the loader, the validator and the derivation, run on the CI runner with every other suite.
