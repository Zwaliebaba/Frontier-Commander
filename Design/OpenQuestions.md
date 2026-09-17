# Open questions — answered

**Status: ANSWERED (2026-09-17).** Every question the drafts left open was put to the owner on 2026-09-17 with the options and a recommendation, and every answer is written into the document it belongs to, dated. This file keeps the record: the question, the answer, whether it followed the recommendation, and where it now lives. **Nothing is open.** A new question is added below the table in the form the old ones had — the question, why it blocks, the options, a recommendation — and put to the owner.

| # | Question | Answer (owner, 2026-09-17) | Followed the recommendation | Recorded in |
|---|---|---|---|---|
| Q1 | Match or world? | Match-based, designed so that a long-running host is reachable later without a rewrite | Yes | `GameDesign.md` §1, §10 |
| Q2 | Network model | **Host-authoritative state replication**: only the host simulates; each client holds a replica of what its commander can see | **No** — the draft recommended lockstep; `TechnicalDesign.md` §5.1 records what the choice buys and what it costs | `TechnicalDesign.md` §2, §3, §5, §10, §11; `GameDesign.md` §10 |
| Q3 | What "moddable" means against R13 | **R13 withdrawn as obsolete**; game data lives in files beside the executable and a mod is a directory that overrides them by path | Beyond the recommendation, which kept R13 and added a mod directory by ADR | `AGENTS.md` (R13 deleted by the owner), `TechnicalDesign.md` §1, §8; `GameDesign.md` §6 |
| Q4 | Content format, reframed once R13 went | JSON, with a reader written into `Core` under R14 | No — text tables in the Species tradition were recommended | `TechnicalDesign.md` §8 |
| Q5 | The Species-derived content other than models | Used, with the provenance risk accepted as a private project's; the soundtrack, branding and narration stay excluded | No — placeholders with a replacement plan were recommended | `SpeciesLineage.md` §1; ADR-006 when written |
| Q6 | The reading of the Species look | Confirmed, with the sprite population added as a visual | Yes | `GameDesign.md` §11 |
| Q7 | Devices as the *Warzone 2100* design system | Confirmed: chassis + drive + modules | Yes | `GameDesign.md` §6 |
| Q8 | Terrain mutability | Flatten under structures only, through M3 | Yes | `GameDesign.md` §3; `TechnicalDesign.md` §4.4 |
| Q9 | Hand-authored maps | Seeds and stamps only | Yes | `GameDesign.md` §3 |
| Q10 | Air units | Not before M4 | Yes | `GameDesign.md` §5, §6, §12 |
| Q11 | A neutral faction | M4, designed fresh | Yes | `GameDesign.md` §9, §12 |
| Q12 | Names | Projects as drafted; `Frontier` for the game, `Neuron` for the engine | Yes | `TechnicalDesign.md` §2 |
| Q13 | Where files live, reframed once R13 went | Content and mods beside the executable; everything written under `%LOCALAPPDATA%\FrontierCommander` | Yes | `TechnicalDesign.md` §9 |
| Q14 | Tick rate | 20 Hz, confirmed by measurement in ADR-003 | Yes | `TechnicalDesign.md` §3 |
| Q15 | The AI at the highest difficulty | A power bonus stated in the lobby; never vision | Yes | `GameDesign.md` §9; `TechnicalDesign.md` §7 |
| Q16 | Font | The Spectrum font | Yes | `GameDesign.md` §11; `TechnicalDesign.md` §6.4; `SpeciesLineage.md` §4 |
| — | Promotion | `GameDesign.md` and `TechnicalDesign.md` are the design `AGENTS.md` refers to; the two `AGENTS.md` sentences that said it did not exist are updated | Yes | `README.md`; `AGENTS.md` |

Three answers went against the recommendation, and the documents say so where they record them rather than smoothing it over. Replication (Q2) costs a second world model and a protocol the lockstep draft did not need, and the design carries that cost in `TechnicalDesign.md` §5 and §11. JSON (Q4) costs a reader under R14, which is three hundred lines and a conformance test. The accepted provenance risk (Q5) is the owner's to carry, and ADR-006 will say so in terms. Two answers went further than the recommendation: R13 withdrawn outright (Q3), which simplified the content pipeline more than the mod-directory ADR would have, and promotion now.

## Adding a question

The form the answered ones had: the question in a paragraph; why it blocks, naming the sections that cannot proceed without it; the options; a recommendation with its reason. Put it to the owner, and when it is answered write the answer into the owning document with the date and add a row above.
