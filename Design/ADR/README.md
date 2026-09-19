# Architecture Decision Records

One file per engineering decision, numbered from `ADR-001` in this repository (`AGENTS.md` §6). The numbering does not continue any other tree's.

| ADR | Decision | Date |
|---|---|---|
| [`ADR-001`](ADR-001-solution-layout.md) | The solution and project layout: the eight projects, their edges and namespaces, the settings every project carries, the test-project shape | 2026-09-17 |
| [`ADR-002`](ADR-002-tick-and-numbers.md) | The 20 Hz tick, the 1/256 position unit and the fixed-point formats, the state hash's order, the order record; the empty tick measured | 2026-09-17 |
| [`ADR-003`](ADR-003-snapshot-and-replay-formats.md) | The snapshot and replay formats: the stream layout, its digest, and the one-version rule until the first save ships | 2026-09-17 |
| [`ADR-004`](ADR-004-renderer-foundation.md) | The renderer foundation: 1920×1080 authored, a borderless window over the primary monitor, a 4× multisampled scene target resolved and presented scaled; the `HRESULT` policy; `d3dx12.h` pinned at DirectX-Headers v1.606.3; shader model 6.0 through `FXCompile` and `dxc` | 2026-09-17 |
| [`ADR-005`](ADR-005-fog-and-lighting.md) | Fog and lighting: the Species lighting unchanged; team-colour slots neither lit nor fogged; distance desaturation over the Species fog scaled to the landscape, chosen on two captured frames, the owner confirming or overriding at T22; the far plane scaling with the landscape and the reversed depth that follow | 2026-09-17 |
| [`ADR-006`](ADR-006-content-format.md) | The content format: one plain row aggregate per table with integer fields in the unit their names say, a version per file refused by name, fail-fast loading and exhaustive validation, a line on every diagnostic including the ones that span files, `FrontierHost --validate` as CI's gate, and `DesignStats` as the one derivation | 2026-09-17 |
| [`ADR-007`](ADR-007-view-budget.md) | The view budget: the fog range absolute in world units with a ceiling on the desaturation, superseding ADR-005's fractions on the owner's frame; terrain chunk residency bounded by radius with the coarsest level resident everywhere, which holds for the four shipping size classes and is stated as O(area) that a quadtree must replace beyond them; the renderer's distances explicitly not line of sight | 2026-09-18 |
| [`ADR-008`](ADR-008-fog-grid-encoding.md) | The fog grid, the first O(area) state a snapshot carries, is run-length encoded; ADR-003's refusal of compression superseded for that one section on the measurement it demanded, every other section unchanged | 2026-09-18 |
| [`ADR-009`](ADR-009-content-in-the-simulation.md) | The simulation reads the content tree, taken by reference at construction and never written; a snapshot carries the digest of the tables it was written against and refuses any other, so reloading against different rules is a refusal rather than a silent divergence | 2026-09-18 |
| [`ADR-010`](ADR-010-species-content.md) | The Species-derived content: the owner's acceptance of the provenance risk, including distribution in M3 and inside mods; the inventory of every file that has come across and the tool that converted it; the exclusions enforced in `Tools/ImportSounds.py` rather than remembered | 2026-09-18 |
| [`ADR-011`](ADR-011-model-normals.md) | Model normals baked by the loader onto a split vertex rather than taken from the pixel shader's derivatives, because the colour already forces the split and the derivative's sign has no answer on a model; the outward normal is the SDK's `cross(c − a, b − a)`, pinned by a test and checked by a winding count; the captured frame K1 asked for is owed once G2 lands | 2026-09-18 |
| [`ADR-012`](ADR-012-replication-protocol.md) | The replication protocol: the interest set as a security boundary, delta frames against a 32-frame history, positions quantized to a quarter of a world unit by floor division, 1,200-byte fragments, and the bytes a frame takes at 100 and 600 visible objects, measured | 2026-09-19 |

## When to write one

A decision is anything a future reader would otherwise re-litigate: a file format, a wire protocol, a subsystem's shape, a project added, an exception to a rule in `AGENTS.md`, a figure the design left to measurement (the tick rate, the authored resolution). It is written **in the same commit as the change that implements it**, and a design document the decision settles is updated in that commit to cite it. `TechnicalDesign.md` §12 lists the ones the work will meet, numbered as they are written.

Not every choice is a decision. A local naming choice, a refactor that changes no boundary, and anything `AGENTS.md` already settles need none.

## The file

`ADR-<nnn>-<slug>.md`: three digits and a kebab-case slug of the decision, `ADR-001-solution-layout.md`. Written in the voice of `AGENTS.md`: dated, owned, and stating what it forecloses as plainly as what it chooses. Figures are measured, not estimated, and say how they were measured.

## Template

```markdown
# ADR-<nnn> — <title>

**Status:** Accepted | Superseded by ADR-<nnn>
**Date:** <YYYY-MM-DD>
**Owner:** <who decided>

## Context

What question the work met, and why it had to be answered now. Cite the design section
(`Design/<Document>.md §n`) or the `AGENTS.md` rule that raised it.

## Decision

What was decided, precisely enough to check code against it.

## Consequences

What this forecloses, what it costs, and what would reopen it.

## Measurements

The figures the decision rests on, each with how it was measured: the command, the build, the
machine. If a figure is arithmetic rather than a measurement, say what it is arithmetic on.
```

## Superseding

A decision is never edited into a different decision. A new ADR supersedes it, the old one's status line points forward, and the old file stays: the history of why is the point.
