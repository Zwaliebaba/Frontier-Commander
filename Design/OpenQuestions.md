# Open questions for the owner

**Status: OPEN (2026-09-17).** Each question here is one the design documents cannot settle, with the options and a recommendation. An answer is written into the document that owns the question, dated and owned, and the question is removed from here. Questions are ordered by how much of the design they block, not by how hard they are.

Where a document proceeds on an assumption pending an answer, it names the question and the assumption; nothing has been assumed silently.

---

### Q1 — Match or world?

**The question.** Is *Frontier Commander* a match-based strategy game — a landscape, two to eight commanders, a victory condition, an hour or two — or a persistent world where bases live between sessions and players come and go, as the Species direction (`tasks/_openworld-prompt.md` in that repository) describes?

**Why it blocks.** It decides the network model (Q2), whether there is persistence at all, whether a landscape has edges, what "large" means, and whether the AI is an opponent or an ecology. Almost every section of `GameDesign.md` reads differently under the second answer.

**Options.** (a) Match-based, as drafted. (b) A persistent world. (c) Match-based first, designed so that a long-running server — one landscape, matches that resume for weeks — is reachable without a rewrite.

**Recommendation: (c).** The brief — building bases, fighting AI and other players on a large landscape, moddable devices, research — describes a match game with a large map, and a match game is buildable by one developer in the milestones of `GameDesign.md` §12. The snapshot format and the headless host in `TechnicalDesign.md` are what (c) needs and (a) has anyway. A persistent world is a different project; if that is the intent, the technical design is rewritten before anything else is.

### Q2 — Lockstep with an authoritative host, or state replication?

**The question.** `TechnicalDesign.md` §5.1 proposes deterministic lockstep on the order stream, with the host's simulation authoritative and snapshot recovery for a diverged client. The alternative is a host-only simulation with state replicated to clients.

**Why it blocks.** The whole of `Net`, the size of `Client` (a replicating client does not run `Sim`), and the cheating posture.

**Recommendation: lockstep,** for the reasons in §5.1 — it spends what R16 already requires and costs a fraction of replication — unless Q1 answers "persistent world", in which case replication. The host-authoritative hybrid, where a diverged client resyncs from the host rather than ending the match, is the drafted design and is what makes the desync class survivable.

### Q3 — What does "moddable" mean against R13?

**The question.** `AGENTS.md` R13 forbids a runtime file dependency and a loose-file path. A mod loaded at runtime is exactly that. The design makes every stat a compiled-in table (`GameDesign.md` §6), which is the precondition for any modding, but a mod that is "edit the tables and rebuild" is a fork, not a mod.

**Options.** (a) Tables only, in the first version: "moddable" means the game is data-driven and a modder builds it. (b) An ADR that adds one opt-in mod directory beside the executable, host-declared and hashed into the match handshake, with R13 unchanged for the base game — nothing is required to start. (c) Reopen R13.

**Recommendation: (a) now, (b) as the ADR when M2 is playable.** The tables and their compile-time validation are the work either way; the loader is small once they exist. A mod directory that is optional and beside the executable keeps R13's purpose — the shipped executable needs nothing — and bends only its letter, which the owner can decide with the loader in hand.

### Q4 — How is content embedded, and what is the sound budget?

**The question.** R13 says `constexpr` arrays in headers. Models, palettes, sprites, icons and the font fit — under 2 MB in total by the arithmetic in `TechnicalDesign.md` §8. Sound does not: 150–250 effects are 20–40 MB of PCM, or 3–6 MB compressed, and a multi-megabyte `constexpr` array is tens of megabytes of source that MSVC compiles slowly.

**Options.** (a) Compress to MS-ADPCM at 22.05 kHz, which XAudio2 plays natively, and embed as `constexpr` anyway. (b) Embed compressed audio as `RCDATA` resources, which are inside the executable and are read through Windows SDK calls — R13's purpose intact, its letter bent. (c) Both: compress, and use resources for audio only.

**Recommendation: (c),** as ADR-005. If `#embed` is available on the pinned toolset it changes the answer toward (a), and the ADR should say whether that was checked.

### Q5 — What happens to the Species-derived art, sound and code?

**The question.** The models are settled: the owner will make new ones (2026-09-17). For the rest — `SpeciesLineage.md` §1: the effects, palettes, sprites, icons and the Darwinia-derived code have no established licence; the Species repository's own licence file, which said "not for distribution", has been deleted. R14 makes this the owner's decision and requires the licence text to travel with the bytes.

**Options.** (a) Use them as placeholders through the vertical slice and M2, with a replacement plan before anything is distributed. (b) Use them and accept the provenance as a private project's risk. (c) Replace from the start and take the content cost now.

**Recommendation: (a),** recorded in ADR-006 with the list of what came across. The soundtrack, the branding and the narration are excluded under every option; the fonts are Q16. The Species-added engineering — input, transport, XAudio2, slot maps, checkers — is the owner's own and is not in question.

### Q6 — Is this the right reading of "the look and feel of Species"?

**The question.** `GameDesign.md` §11 pins it to five things: flat-shaded vertex-coloured geometry, the height-and-slope palette landscape with the Species water and sky, sprites for small things, the terminal-style Eclipse interface, and the free camera. It excludes the Darwinia mechanics — spirits, citizens, the virus, the task manager as a mechanic.

**Options.** Confirm; or add (a population of sprite figures walking between structures as a visual, a narrator, the "inside a computer" fiction made explicit); or remove something. The reading now has a measured basis: `SpeciesLook.md`, `SpeciesTerrain.md` and `SpeciesCanvas.md` say in numbers what each of the five things is.

**Recommendation: confirm, and add the sprite population as a visual.** `GameDesign.md` §11 item 3 already leaves the door open; it is the most recognisable thing about Species, and it costs a sprite pass the game has anyway.

### Q7 — Are "devices" the *Warzone 2100* design system?

**The question.** The brief says *"devices which can be modded and researched to add more features."* The draft reads this as *Warzone*'s body–propulsion–turret design system — a device is a designed unit, its parts are components, research unlocks parts, "modded" is the data tables — and names the parts chassis, drive and module.

**Options.** Confirm; or "device" meant something else — a structure module system, an equipment system on fixed units, a programmable unit.

**Recommendation: confirm.** If it meant something else, `GameDesign.md` §6 and §7 are the sections to rewrite and the rest largely stands.

### Q8 — Terrain mutability

**The question.** The draft flattens terrain under structures and nothing else. Terraforming, cratering and destructible features change the delta format, the pathing invalidation and the griefing surface.

**Recommendation: flatten only, through M3.** The delta representation in `TechnicalDesign.md` §4.4 is a list of rectangular height edits so that the answer can change without a new snapshot format.

### Q9 — Hand-authored maps, or seeds and stamps only?

**Recommendation: seeds and stamps only.** A stamp library plus a seed gives a designer start bases, chokepoints and set-pieces at a fraction of the cost of whole maps, and one developer has no content budget for whole maps. The Species map format is the reference for a stamp's definition.

### Q10 — Air units?

**The question.** *Warzone* has VTOLs, rearm pads and anti-air; they are a third of its late game and a large share of its code.

**Recommendation: not before M4,** and decided then with the skirmish game in hand. The drive table carries a `Lift` row so that the component system does not preclude them.

### Q11 — A neutral faction?

**The question.** Hostile things on the landscape owned by nobody — the Species virus as it would look here — make a large map dangerous to cross and give the AI a second job.

**Recommendation: M4, designed fresh,** with the Darwinia creatures as a mood board and none of their code.

### Q12 — Names

**The question.** The project names in `TechnicalDesign.md` §2 (`Core`, `Content`, `Sim`, `Net`, `Client`, `FrontierHost`, `FrontierCommander`) and the engine namespace.

**Recommendation.** The project names as drafted; `Frontier` for the game namespace, as `AGENTS.md` §1 already illustrates; and **`Neuron` for the engine namespace**, because the engine code this tree ports from Species then moves without a rename, which is the stated reason the formatter settings were carried over. If the owner wants no Species name in this tree, any name will do and ADR-001 records it.

### Q13 — Which files may the game write, and when?

**The question.** `TechnicalDesign.md` §9 lists six: preferences, designs, saved matches, replays, stamp exports, the host log. R13 pre-approves none.

**Recommendation.** Approve the *category* now — files beside the executable, none required to start — so that each ADR argues the form and not the principle, and take them in the order §9 lists them. Designs are the one that cannot wait past M1: a design system whose designs vanish with the process is not one.

### Q14 — Tick rate

**Recommendation.** 20 Hz as drafted, settled by ADR-003 once M0 can measure a tick. The number matters less than that it is one number, fixed, everywhere.

### Q15 — Does the AI cheat at the highest difficulty?

**Recommendation.** Yes, by a power bonus stated in the lobby; never by vision. An AI that sees through fog is unfixable, and a bonus is one number.

### Q16 — Which font?

**The question.** Species carries two pixel fonts under `GameData/Textures/`, ten bitmaps in all: `SpeccyFont*`, which is the Sinclair ZX Spectrum character set at twice its size, and `EditorFont*`, a bolder face of no identifiable third-party origin (`SpeciesLineage.md` §4; both were identified by rendering the glyphs). A first draft of these documents excluded the Spectrum font outright on provenance, and that was too strong: a bitmap typeface design is not copyrightable in the United States, the United Kingdom's design right on a 1982 typeface has long expired, and Amstrad has permitted redistribution of the Spectrum ROM for emulation for decades. The question is not legal. It is that the Spectrum font is the house face of every Introversion game, and beside Darwinia's models and palettes it makes this game read as one of theirs.

**Options.** (a) The Spectrum font: the full Species look. (b) The editor font: Species' own, and less of a signature. (c) A new face drawn in the same spirit.

**Recommendation: (a),** because the brief asks for the look of Species and the typeface is part of it, with ADR-006 recording the provenance and the identity point beside the rest of the Species content. (b) is the fallback if the owner would rather not carry the Introversion signature, and it costs nothing to switch: the atlas is one baked header either way.
