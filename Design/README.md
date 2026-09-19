# Design — what *Frontier Commander* is

The design documents for *Frontier Commander*. [`AGENTS.md`](../AGENTS.md) says how code is written here; this directory says what is being built. The two sit alongside each other: a design document never overrides an engineering rule, and an engineering rule never decides a game mechanic. Where a design decision has to constrain how code is *shaped*, it becomes an `R18`+ rule in `AGENTS.md` §5 citing the section here that is its source — the design is the source and `AGENTS.md` is the rule, in that order.

**Status: DESIGN (promoted by the owner on 2026-09-17).** `GameDesign.md` and `TechnicalDesign.md` are the design `AGENTS.md` refers to: what is built, alongside its rules for how. The sixteen questions the drafts had left open, and the six an external review raised, were put to the owner on 2026-09-17, and every answer is written into the document it belongs to, dated; [`OpenQuestions.md`](OpenQuestions.md) keeps the record, including the three answers that went against the recommendation. From here the design changes through pull requests the owner approves and through ADRs, and a task that needs an answer the design does not give asks the owner and gets it written in before the code is.

## The documents

| Document | What it settles | Status |
|---|---|---|
| [`GameDesign.md`](GameDesign.md) | The game: vision and pillars, the session, the landscape, economy, base building, devices and their components, research, combat, AI, multiplayer, presentation, scope per milestone | Design |
| [`TechnicalDesign.md`](TechnicalDesign.md) | How the game is built inside the rules of `AGENTS.md`: projects and layers, the deterministic simulation on the host, state replication to clients, the Direct3D 12 renderer, the content files and mods, where files live, testing, the first ADRs | Design |
| [`SpeciesLineage.md`](SpeciesLineage.md) | What comes across from the Species repository and what does not — code, art, sound, data, and the lessons it paid for — with measured figures and the provenance decision | Accepted |
| [`SpeciesLook.md`](SpeciesLook.md) | The Species presentation as configuration: the frame, every light in every map, materials and shading, fog, sky and clouds, camera, sprites, particles and debris, the pixel effect, the render preferences | Reference |
| [`SpeciesTerrain.md`](SpeciesTerrain.md) | The Species landscape and water as a system: the definition format, tile generation step by step, merging and flattening, colouring and the outline overlay, the water lightmap, plane and waves, the queries | Reference |
| [`SpeciesCanvas.md`](SpeciesCanvas.md) | The Species windows and overlay: the Eclipse model and its rules, the window chrome as a palette, the fonts, the task-manager overlay's virtual screen and zones, the cursor | Reference |
| [`Interface.md`](Interface.md) | What the operator sees and clicks in M1: the frame at the authored resolution, the chrome and its palette, the pointer and the hotkeys, selection and orders, the five command tabs, the selection panel, the minimap and the readouts, the overlays | Design |
| [`OpenQuestions.md`](OpenQuestions.md) | The sixteen questions put to the owner on 2026-09-17, the six the external review raised and Q17 on the sky's scaling of 2026-09-18, each with the answer and where it is recorded; and Q18 on terrain regions in one landscape, also 2026-09-18; the form for adding one | Answered |
| [`ADR/`](ADR/README.md) | Engineering decisions taken while building, one file per decision, numbered from `ADR-001` | ADR-001 |
| [`ImplementationPlan.md`](ImplementationPlan.md) | How the design becomes work an agent executes: the session loop under CI as the compiler, the plan format, the definition of done, the map from milestones to the plans under [`tasks/`](../tasks/), and the decisions the plan took | Plan |

Read them in that order. `GameDesign.md` is written to stand alone for a reader who knows real-time strategy games; `TechnicalDesign.md` assumes `AGENTS.md` has been read first, because it cites its rules by number rather than restating them; `SpeciesLineage.md` assumes both. The three *Reference* documents are read from the Species source with a line pointer for every value; they are what the renderer, terrain and interface tasks build from, and "Reference" means they describe Species as it is rather than propose anything.

## How a design changes

- **A change to what the game is** edits the relevant document in a pull request the owner approves. The document is the record; there is no separate changelog.
- **A decision taken while building** — a format, a protocol, a subsystem's shape, an exception to a rule — is an ADR under `ADR/`, in the same commit as the code (`AGENTS.md` §6). When an ADR settles something a design document left open, the document is updated in the same commit to point at the ADR.
- **A question** is added to `OpenQuestions.md` with its options and a recommendation, put to the owner, and the answer is written into the document it belongs to, dated and owned, in the manner `AGENTS.md` records its own decisions; `OpenQuestions.md` keeps the row that says what was answered and where.

## What is deliberately not here yet

A map and stamp content plan, an art bible beyond the presentation section of the game design, an audio design and any campaign writing are worth doing only against the running slice, and the milestones in `GameDesign.md` §12 say when that is. The interface specification that stood here was written on 2026-09-17 as [`Interface.md`](Interface.md) (`m1-vertical-slice/D1`); its §11 lists what it found missing and which task owns each, and its §12 the rulings its merge accepts.
