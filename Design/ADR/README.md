# Architecture Decision Records

One file per engineering decision, numbered from `ADR-001` in this repository (`AGENTS.md` §6). The numbering does not continue any other tree's.

| ADR | Decision | Date |
|---|---|---|
| [`ADR-001`](ADR-001-solution-layout.md) | The solution and project layout: the eight projects, their edges and namespaces, the settings every project carries, the test-project shape | 2026-09-17 |
| [`ADR-002`](ADR-002-tick-and-numbers.md) | The 20 Hz tick, the 1/256 position unit and the fixed-point formats, the state hash's order, the order record; the empty tick measured | 2026-09-17 |
| [`ADR-003`](ADR-003-snapshot-and-replay-formats.md) | The snapshot and replay formats: the stream layout, its digest, and the one-version rule until the first save ships | 2026-09-17 |

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
