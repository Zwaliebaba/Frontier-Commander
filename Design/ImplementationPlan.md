# Frontier Commander — Implementation Plan

**Status: PLAN (written 2026-09-17 from the design as accepted that day; revised as the plans under `tasks/` move).** This document says how the design becomes work an agent can execute: the loop a session runs, the format the work is planned in, what "done" means for every task, and how the milestones of [`GameDesign.md`](GameDesign.md) §12 map onto the plan files. The tasks themselves are in [`tasks/`](../tasks/), one plan per milestone, validated and queried by `Tools/CheckTaskDag.py`. This document does not repeat them.

It assumes [`AGENTS.md`](../AGENTS.md) has been read, because every rule there applies to every task here, and it cites [`TechnicalDesign.md`](TechnicalDesign.md) by section rather than restating it.

---

## 1. What this is, and what it is not

The design (`GameDesign.md`, `TechnicalDesign.md`) says what is built and, for the engineering, in what shape. `AGENTS.md` says how code is written. Neither says what to do on Monday. This plan does: it breaks the milestones into tasks small enough to be one pull request each, orders them by what genuinely cannot start until what else has landed, and states for each what an observer could check to call it done.

It is a plan for an **agent working in Linux sessions that cannot build** (owner, 2026-09-17; `OpenQuestions.md` R1). That single fact shapes everything in §2: the agent writes the code, the checkers and tools run in the session, and CI on the Windows runner is the compiler, the test runner, the linter and, through the capture job, the agent's eyes. A task is never "builds clean, not run" in a session; it is "CI green on the pull request" or it is not done.

It is not a schedule. Waves say what may run concurrently, not when. It is not a second design: where a task needed a decision the design did not give, §6 lists the decision taken and the design was updated in the same commit, so that the design stays the authority.

---

## 2. How an agent works a task

The loop, from a fresh session:

1. **Read** `AGENTS.md`, this document's §1 to §4, and `python3 Tools/CheckTaskDag.py --next tasks/<plan>.yaml` for the plan of the current milestone. A task listed as *held by another plan* waits on a task in an earlier milestone's plan; it is not yours yet.
2. **Pick one ready task.** If several are ready and share no `files`, they may be worked concurrently by different sessions; if they share files, they are worked one after the other.
3. **Read the task** in full: its `intent`, `files`, `acceptance` and `notes`, and then the design sections the intent cites. `ls` the file list before starting; a declared path is a prediction, not a promise.
4. **Branch** off `main`, set the task's `status` to `in_progress`, and commit that change first, alone, so that a parallel session does not pick up the same task.
5. **Do the work**, in the shape the acceptance lines require. Tests ship in the same commit as the behaviour (`TechnicalDesign.md` §10). Add every new file to the `.vcxproj` and the `.filters`. If the task is a decision, write the ADR in the same commit (`AGENTS.md` §6) and update the design section it settles.
6. **Run what runs in the session** (the table below) until every one is clean.
7. **Push and open the pull request** with the repository's template filled in honestly: what was verified in the session, what CI verified, what was not verified and why. One task per pull request; the PR title is the task's title.
8. **Read CI.** The Windows job's `build-log` and `test-results` artefacts and the `captures` artefact are what you have instead of a build. A red step is yours to fix: re-read the log, fix the cause, push again. Never lower a warning level, silence a diagnostic, skip a test or add a guard to get past a red build (`AGENTS.md` §3, §6).
9. **When CI is green and every acceptance line holds**, set the task's `status` to `done`, record anything surprising in its `notes` (a file list that was wrong, a decision beyond the letter of the acceptance, a measurement), and commit the plan update on the same branch. The owner merges.
10. **If the task turns out to be wrong** — the intent no longer holds, or the breakdown was mistaken — do not reshape it quietly. Set it `blocked` or `abandoned` with a note saying why, add the corrected tasks, and say so in the report.

**What runs where.** A session is Linux with Python 3, clang-format 18 and clang 18 available; CI is `.github/workflows/build.yml`.

| Command | Runs in the session | Runs in CI |
|---|---|---|
| `python3 Tools/CheckTaskDag.py` | yes | yes (Linux job) |
| `python3 Build/CheckFormat.py --clang-format clang-format-18` | yes | yes (Linux job, same version) |
| `python3 Build/CheckProjectFiles.py` | yes — it is written to run on both | yes (Windows job, before the build) |
| `python3 Tools/LandscapeTool.py`, `Tools/CheckBalance.py`, the importers | yes | where a task says so |
| `msbuild FrontierCommander.slnx …` | **no** | yes, Debug\|x64 only |
| `vstest.console.exe x64\Debug\*Tests.dll` | **no** | yes |
| `python Build\RunClangTidy.py` | **no** (needs the MSVC headers) | yes, after the build |
| `FrontierCommander --warp --capture …` | **no** | yes, frames uploaded as `captures` |

A `verify` list in a plan names commands from both columns. The agent runs the first column before pushing and reads the second column's result off the pull request; a task is not done until both are clean.

**A private syntax check is allowed; a second build system is not.** `clang++ -std=c++2b -fsyntax-only` over a portable translation unit (`Core`, `Content`, `Sim`, `Net`, `Replica`) with `-I` for the project directories catches a typo or a missing include before CI does, and costs nothing. It is a local aid: nothing about it is committed, no makefile, no compile database, and a clean check is not a green build, because MSVC is the compiler of record and disagrees with clang in both directions (`AGENTS.md` §3 forbids a second build system; `OpenQuestions.md` R1 records why a Linux build was rejected).

**Write code that compiles the first time.** With no compiler in the session the agent's habits carry the cost: include what you use; spell SDK identifiers as the SDK does (R4); keep `Sim` free of platform headers and floats (T6 of M0 will fail the build if not); prefer the explicit over the clever; and read your own diff against `AGENTS.md` §7 before pushing. One validated push beats three speculative ones, and each red run costs the Windows runner's minutes and the owner's trust.

**Two things are always in scope and never optional**: the test that proves the acceptance line, and the plan-file update that records the state. Everything else that is not in the task is out of scope, however tempting (`AGENTS.md` §6, "Stay in scope").

---

## 3. The plan format

Work is planned as **directed acyclic graphs of tasks in YAML under `tasks/`**, one file per plan, in the format the Species repository's `docs/TASK_DAG.md` established and its agents used for 147 tasks. The graph is the plan: an agent reads it to decide what to work on, updates it as work completes, and commits it alongside the code. `Tools/CheckTaskDag.py` is a port of that repository's validator.

```
tasks/
  _template.yaml            copy this to start; files beginning with _ are not plans
  m1-vertical-slice.yaml    one plan per milestone, kebab-case, matching `plan:`
  m2-skirmish.yaml          M2 to M4 are coarse: units of scope, split into units of work
  m3-multiplayer.yaml       when the previous milestone closes and the slice has numbers
  m4-frontier.yaml
  Archive/                  plans with nothing left open; still loaded so blocked_by resolves
    m0-foundation.yaml      closed 2026-09-19 by the owner's run (T22)
```

**The schema**, per task: `id` (unique in the plan), `title` (imperative), `intent` (why, one or two sentences — the part that survives), `project`, `depends_on` (ids in this plan), `blocked_by` (tasks in another plan as `plan/Tn`), `files` (the expected touch set, used to spot collisions), `acceptance` (observable outcomes, at least one), `verify` (commands that prove them), `parallel_safe` (false when the task needs the tree to itself), `status`, `notes` (findings, appended as work proceeds; one key, once — the loader rejects a duplicate). Status is `todo`, `in_progress`, `blocked`, `done` or `abandoned`; the validator refuses `in_progress` or `done` while a dependency is not `done` or `abandoned`.

**The queries:**

```
python3 Tools/CheckTaskDag.py                                  # validate every plan (CI runs this)
python3 Tools/CheckTaskDag.py --next tasks/m1-vertical-slice.yaml  # what can start now
python3 Tools/CheckTaskDag.py --waves tasks/m1-vertical-slice.yaml # what may run concurrently
python3 Tools/CheckTaskDag.py --mermaid tasks/m1-vertical-slice.yaml
```

**The rules that make the format work**, carried over from the Species standard because each was learned there the hard way:

- **An edge means "cannot start until", not "reads better after".** An edge added for narrative tidiness serialises work needlessly. If the second task could technically start before the first finishes, there is no edge.
- **Acceptance criteria are observable.** "Cleaner" is not a criterion; "`Sim` includes no `<windows.h>`" is. Prefer a `verify` command to prose.
- **A task that adds behaviour states its tests in `acceptance` and runs them in `verify`**, never as a downstream node that will be marked done while the tests never arrive.
- **One reviewable change per task.** A diff too large to review in one sitting is two tasks, and the graph is where the split is expressed.
- **Declare `files`.** Two tasks in one wave that list the same file collide; two tasks in different plans that list the same file collide just the same.
- **A declared path is not checked against the disk.** `ls` the list before starting; the Species tree found eight declared lists wrong.
- **Run a closing node's grep the day you write it.** A criterion of the form "a grep over the tree returns nothing" is testable immediately and costs the most when it fails last.
- **An edge orders the work; it does not promise a buildable tree between.** When two tasks can only land together, land them in one commit and say so in both tasks' notes.
- **When a plan has nothing left open, move it to `tasks/Archive/`** and fix the paths that named it (`grep -rn 'tasks/<plan>.yaml'` over `*.md`, `*.py` and `.github/`).

---

## 4. Definition of done

A task is done when all of the following hold, and the report says which were checked how:

- Every `acceptance` line is true, and the one that could not be checked in the session was checked by CI or by the owner, as the line says.
- CI is green on the pull request's head: the build shape, the build, every test suite, clang-tidy, the format job, the plan validation, and the capture job where it applies.
- The new behaviour has its tests in the same change, counted in the test results (`vstest` reports an empty suite as a pass; a `SuiteSmoke` is deleted only by the first real test in that project).
- Every new, moved or removed file is in the `.vcxproj` and the `.filters`; `CheckProjectFiles.py` says so.
- No rule of `AGENTS.md` was bent silently. A bend is named in the pull request's "Anything you had to bend".
- If the task was a decision, the ADR exists in the same commit, numbered next, and the design section it settles cites it.
- If the task changed what the design says, the design says the new thing, dated, in the same commit.
- The plan file records `done` and the notes record what was learned.

The milestone is done when its "proves" column in `GameDesign.md` §12 is true on a running build the owner has run. Every plan ends with that task, and it is the owner's.

---

## 5. From the design to the plans

| Milestone | Plan | Proves (`GameDesign.md` §12) | Tasks | Grain |
|---|---|---|---|---|
| M0 Foundation | `tasks/Archive/m0-foundation.yaml` | Window, scene target and a CI capture on WARP; a deterministic, hashing tick; a suite per library; every checker in CI; the landscape tool; heights generated and drawn | 22 | Units of work |
| M1 Vertical slice | `tasks/m1-vertical-slice.yaml` | Two commanders on a Small landscape build, design, research and fight to annihilation over loopback, the client a replica | 29 | Units of work |
| M2 Skirmish | `tasks/m2-skirmish.yaml` | Four commanders on Medium with the full catalogue and component set, personalities, save and resume, the look completed | 11 | Units of scope |
| M3 Multiplayer | `tasks/m3-multiplayer.yaml` | Eight commanders over LAN and direct IP on a headless host; rejoin; Large and dominance; mods; replays | 10 | Units of scope |
| M4 Frontier | `tasks/m4-frontier.yaml` | Frontier-class landscapes at full performance; the neutral faction; commanders; legs and possibly lift | 7 | Units of scope |

**M0 in one paragraph.** Three tasks start at once: the solution with `Core` and its tests plus ADR-001 for all eight projects (T1), the format checker (T3), and the landscape tool in Python (T16), which needs nothing and is the one piece of the simulation the agent can run and tune in the session. The other seven projects (T2), the build-shape checker (T4), clang-tidy's runner (T5) and the `Core` pieces — arithmetic, randomness and hashing, the slot map, the byte stream, JSON, bitmaps and waves, paths and logging, the transport seam with loopback — follow, each with its tests. The `Sim` skeleton (T15) puts the fourteen-stage tick, the hash, the snapshot and the three determinism tests in place before any system exists, and the landscape (T17) is the first system, tested bit for bit against the tool's golden fields. The `Client` foundation (T18) is the window, the device with WARP, the scene target presented scaled, the shader pipeline and the capture that writes BMPs, with ADR-002; input (T19) follows the Species design; the terrain pass and camera (T20) draw the landscape and write the fog-and-lighting ADR on captured frames; the capture job (T21) makes the frames CI artefacts. The owner's run (T22) closes it. **It closed on 2026-09-19** and the plan moved to `tasks/Archive/`; T22's notes carry the machine, the frame time and the WARP-against-hardware comparison, and name the one acceptance line left unmet.

**M1 in one paragraph.** The interface specification (D1) is the one design task and the owner accepts it by merging. `Content` gets its loaders, validation and `FrontierHost --validate` (C1), then the M1 tables (C2), the balance script (C3) and the importers with placeholder models and the provenance ADR (C4). `Sim` grows in the order the tick runs: objects and seats (S1), orders (S2), economy (S3), structures (S4), devices and production (S5), research (S6), pathing (S7), movement (S8), visibility (S9), combat (S10), victory (S11), and the scripted AI (S12), whose AI-against-AI test is the one that exercises everything at once. `Net` is the records (N1), the host with the interest test and the network ADR (N2) and the client endpoint (N3); `Replica` converges (R1) and builds the render view with composition at markers and picking (R2). `Client` gets the geometry, fog and UI passes (K1, K2, K3). The executable assembles the two loops over loopback with selection and orders (G1), the capture becomes a scripted match (G2), the panels and minimap land (K4), and the owner's run (G3) closes it with the tick measured.

**What a task reads.** The design is cited per task in its `intent`; the table below is the map for a reader who wants the whole of a family at once.

| Task family | Read first | Then |
|---|---|---|
| Solution, projects, checkers (M0 T1–T6) | `AGENTS.md` §1–§4, §6 | `TechnicalDesign.md` §2; `.clang-tidy`, `.clang-format`, `build.yml` in full |
| `Core` (M0 T7–T14) | `TechnicalDesign.md` §4.1–§4.3, §4.9, §5.6, §8 (the JSON reader), §9 | `AGENTS.md` R14–R17; the Species `network-transport` plan for the transport seam |
| Landscape (M0 T16–T17; M2 T7) | `SpeciesTerrain.md` §2–§5 | `GameDesign.md` §3; `TechnicalDesign.md` §4.4 |
| Renderer (M0 T18, T20; M1 K1–K2; M2 T8, T12) | `AGENTS.md` §5; `TechnicalDesign.md` §6 | `SpeciesLook.md`; `SpeciesTerrain.md` §6–§7; `GameDesign.md` §11 |
| Input and UI (M0 T19; M1 D1, K3, K4; M2 T10) | `TechnicalDesign.md` §6.4–§6.5 | `SpeciesCanvas.md`; the Species `input-native-events` plan; `Design/Interface.md` once written |
| Simulation (M0 T15; M1 S1–S12) | `TechnicalDesign.md` §3, §4, §7 | `GameDesign.md` §2–§9 for the numbers each system applies |
| Content and tools (M1 C1–C4) | `TechnicalDesign.md` §8 | `GameDesign.md` §4–§8 tables; `SpeciesLineage.md` §4–§5 |
| Networking and replica (M1 N1–N3, R1–R2; M3) | `TechnicalDesign.md` §5, §6.3, §10 | `GameDesign.md` §10; the Species `network-transport` plan |

---

## 6. Decisions this plan takes, and where the design changed to match

Writing tasks against the design found a handful of places where a task would have had to decide something the design did not say. Each was decided here, in the smallest way that let the task be written, and the design was updated in the same commit so it stays the authority. They are the author's and the owner may reverse any of them by editing the design.

1. **The render-view and height-view types live in `Core`.** `Replica` produces the render view and `Client` consumes it, and the edges of `TechnicalDesign.md` §2 forbid either from seeing the other, so the plain aggregates go below both. ADR-001 records it (M0 T1); `TechnicalDesign.md` §6.3 says so.
2. **The client receives its commander's fog grid as `FogDelta` records.** The fog pass and the minimap draw the commander's visibility, and nothing in §5.3's record list carried it. A run-length record of cells with their new state is added to the frame; it is the commander's own information and leaks nothing. `TechnicalDesign.md` §5.3 says so; M1 N1 implements it.
3. **`LandscapeDefinition` is a `Content` aggregate from M0**, with no loader until M1 C1, because `Sim` reads `Content` and a type that starts in `Sim` would have to move. The tool of M0 T16 fixes its JSON form.
4. **Power is held in hundredths**, so that 5 power per second at 20 Hz is an integer per tick (25). It follows the rule of `TechnicalDesign.md` §4.1 for percentages and is named in M1 S3.
5. **ADR numbering follows the order of writing.** `TechnicalDesign.md` §12's numbers are the order the design expected; the ADR a task writes takes the next free number, and §12 is updated in that commit. Two consequences: the snapshot-format ADR is written by M0 T15 with the tick ADR, because the format exists then (the save file of M2 cites it); and the fog-and-lighting ruling gets its own ADR at M0 T20 rather than riding on the renderer ADR, because it needs a captured landscape and the renderer ADR is written before one exists. So ADR-002 is the tick and ADR-003 the snapshot (2026-09-17), and the renderer's is numbered when T18 writes it; the design documents name an unwritten ADR by its subject, never by a number.
6. **The tick ADR carries a dated measurement section from M1**, because the first `Sim` task cannot measure a tick that has no systems in it; `AGENTS.md` §6 asks for measured figures, and M1 G3 adds them.
7. **The layering check lives in `CheckProjectFiles.py`** rather than a fourth script, so that the workflow needs no new step; `TechnicalDesign.md` §2 called it "a layering checker in the Species mould" and this is where it is.
8. **A `d3dx12.h` copy needs a pinned origin.** The renderer ADR (`m0-foundation/T18`) records the release and the SHA-256 of the vendored file, so that "pinned" (owner, 2026-09-17) is checkable.
9. **Content validation runs in CI from M1 C1** as `FrontierHost --validate`, guarded on the first table existing, added to the workflow by that task.
10. **The plan validation runs in CI's Linux job** from this commit, guarded on the checker existing, so that a plan with a cycle or an inconsistent status cannot merge.
11. **Textures are DDS and nothing else** (owner, 2026-09-17), so `Core` reads DDS rather than BMP, the importers write DDS, and the capture screenshots alone stay BMP because the agent has to be able to look at them. `TechnicalDesign.md` §8 says so; M0 T12 and M1 C4 carry it.

---

## 7. Placeholders, and what M1 leaves out on purpose

- **Models** are the owner's to make (owner, 2026-09-17). Until they exist, M1 C4 generates primitives with markers so that composition at markers is exercised rather than faked, and every model the tables name exists. The owner's OBJ files replace them through `ImportObj.py` one at a time, with no code change.
- **Audio, the sprite pass and particles, water with waves, the sky and clouds, Eclipse-shaped windows, save and resume, the lobby, hover, walls and hardpoints, and every module beyond the four** are M2. M1's projectiles are small geometry instances and an explosion is a wreck appearing; the explosion itself — the particle burst and the debris shatter — is `m2-skirmish/T12`, which the owner added on 2026-09-19 rather than reversing the cut. This is the review's cut list applied, with the owner's one reversal (replication stays in M1).
- **The palette in M0 is a generated gradient**; the Species palettes arrive with the provenance ADR in M1 C4, so that no Species-derived file is in the tree before the ADR that records it.
- **`Design/Interface.md` does not exist yet.** It is M1 D1, and the UI tasks depend on it.

---

## 8. Risks to the plan itself

| Risk | Why | What the plan does |
|---|---|---|
| **Compile-error round trips** | The agent cannot compile; every mistake costs a CI cycle of minutes | The syntax aid of §2, small tasks, tests as literals rather than files, and the habit of one validated push |
| **`FXCompile` and `dxc` on the pinned toolset** | Whether MSBuild's shader step drives dxc for shader model 6 is unknown (`TechnicalDesign.md` §6.2) | M0 T18 finds out first and records the answer in the renderer ADR; 5.1 through `fxc` is the fallback and the design does not depend on 6 |
| **WARP on the CI runner** | The capture job assumes a D3D12 WARP device on `windows-latest` | M0 T18's capture path is tried before the job is written (T21); if WARP is unavailable, the job records that and the owner's run is the only eyes |
| **The AI-against-AI test's cost** | A 36,000-tick match in a Debug unit test may be too slow for CI | M1 S12 records the duration and bounds the run; the full-length run moves to the capture job if needed |
| **Plan drift** | Files lists and acceptance lines rot as the code moves | `ls` before starting, notes on every task, and the rule that a wrong task is marked and replaced, not quietly reshaped |
| **The owner's runs** | Three of the five plans end in a task only the owner can do | The plan says so on each; nothing else waits on them except the archive move |

---

## 9. The first pull requests

The order that gets a frame on CI soonest, with concurrency where the graph allows:

1. `m0-foundation/T1` — the solution, `Core`, `CoreTests`, ADR-001. Alone, because everything hangs off it.
2. `T3` and `T16` in parallel with T1 — the format checker and the landscape tool need nothing.
3. `T2` — the other seven projects; then `T4` and `T5`, the two checkers, and in parallel `T7`, `T8`, `T9`, `T10`, `T11`, `T13` — the `Core` pieces, each its own PR.
4. `T6`, `T12`, `T14`, `T15`, `T19` — the layering check, DDS textures and waves, the transport seam, the `Sim` skeleton, input.
5. `T17` and `T18` — the landscape in C++ against the golden fields, and the D3D12 foundation with the capture.
6. `T20` and `T21` — the terrain on screen, and the capture job that shows it.
7. `T22` — the owner runs it.

Then `m1-vertical-slice` from `--next`, starting with `D1` and `C1`, which are the first two things every other M1 task waits on.
