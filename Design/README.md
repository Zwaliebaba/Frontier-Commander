# Design — what *Frontier Commander* is

The design documents for *Frontier Commander*. [`AGENTS.md`](../AGENTS.md) says how code is written here; this directory says what is being built. The two sit alongside each other: a design document never overrides an engineering rule, and an engineering rule never decides a game mechanic. Where a design decision has to constrain how code is *shaped*, it becomes an `R18`+ rule in `AGENTS.md` §5 citing the section here that is its source — the design is the source and `AGENTS.md` is the rule, in that order.

**Status: DRAFT, not yet design authority.** `AGENTS.md` states that no design authority exists and that a task needing a design answer asks the owner. That remains true until the owner promotes a document here; a draft is a proposal to react to, not a decision to build against. Each document carries its status at the top, and the questions the owner has to answer before promotion are collected in one place, [`OpenQuestions.md`](OpenQuestions.md). When a document is promoted, the two sentences in `AGENTS.md` that say the design does not exist are the owner's to update.

## The documents

| Document | What it settles | Status |
|---|---|---|
| [`GameDesign.md`](GameDesign.md) | The game: vision and pillars, the session, the landscape, economy, base building, devices and their components, research, combat, AI, multiplayer, presentation, scope per milestone | Draft |
| [`TechnicalDesign.md`](TechnicalDesign.md) | How the game is built inside the rules of `AGENTS.md`: projects and layers, the deterministic simulation, the network model, the Direct3D 12 renderer, the content pipeline, the files the executable may write, testing, the first ADRs | Draft |
| [`SpeciesLineage.md`](SpeciesLineage.md) | What comes across from the Species repository and what does not — code, art, sound, data, and the lessons it paid for — with measured figures and the provenance caveat | Draft |
| [`OpenQuestions.md`](OpenQuestions.md) | The decisions only the owner can take, each with the options and a recommendation | Open |
| [`ADR/`](ADR/README.md) | Engineering decisions taken while building, one file per decision, numbered from `ADR-001` | None yet |

Read them in that order. `GameDesign.md` is written to stand alone for a reader who knows real-time strategy games; `TechnicalDesign.md` assumes `AGENTS.md` has been read first, because it cites its rules by number rather than restating them; `SpeciesLineage.md` assumes both.

## How a design changes

- **A change to what the game is** edits the relevant document in a pull request the owner approves. The document is the record; there is no separate changelog.
- **A decision taken while building** — a format, a protocol, a subsystem's shape, an exception to a rule — is an ADR under `ADR/`, in the same commit as the code (`AGENTS.md` §6). When an ADR settles something a design document left open, the document is updated in the same commit to point at the ADR.
- **An open question** is answered by the owner and then written into the document it belongs to, dated and owned, in the manner `AGENTS.md` records its own decisions. Once written in, it leaves `OpenQuestions.md`.

## What is deliberately not here yet

A map and stamp content plan, an art bible beyond the presentation section of the game design, an audio design, a user-interface specification, and any campaign writing. Each of those is worth doing only against a running vertical slice, and the milestones in `GameDesign.md` §12 say when that is.
