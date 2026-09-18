# ADR-010 — The Species-derived content: what came across, and the accepted risk

**Status:** Accepted (2026-09-18). Written by `m1-vertical-slice/C4`, which is the task that first
moved a Species file into this tree. It records a decision the owner made on 2026-09-17
(`OpenQuestions.md` Q5 and R5) rather than making one.

## Context

`SpeciesLineage.md` §1 states the provenance plainly and it is not comfortable: Species is Darwinia,
its own licence file was deleted, and the original's terms were never established. Its README says
to *"treat the provenance as unresolved rather than permissive."* The art under Species's
`GameData/` is Darwinia's art.

`AGENTS.md` R14 makes third-party content the owner's question, to be approved before it lands, with
the licence text travelling with the bytes. There is no licence text to travel, which is the whole
difficulty.

Two questions were put to the owner and both were answered against the recommendation:

- **Q5 (2026-09-17)**: the recommendation was placeholders with a replacement plan. The owner chose
  to use the Species-derived content and to carry the provenance risk as a private project's.
- **R5 (2026-09-17)**: the recommendation was replacement before M3, when a match first hands
  content to another player. The owner confirmed that the acceptance **covers distribution to other
  players in M3 and inside mods**.

## Decision

**The Species-derived content listed below is used, and the owner accepts the provenance risk,
including handing it to other players in M3 and inside mods.** That is the owner's decision, taken
twice, in those terms.

**What is excluded stays excluded, whatever else is decided**, because it is licensed to
Introversion from a third party or is Introversion's own identity rather than Darwinia's art:

- the six soundtrack tracks (Tresk, Trash80, DMA-SC — 126.9 MB measured);
- the Introversion and publisher logos and splash screens: `IvLogo.bmp`,
  `MsnOberonComboSplash.bmp`, `DmaCrew.bmp`, `ProgramDarwinia.bmp`,
  `DarwinResearchAssociates.bmp`;
- the Sepulveda narration.

`Tools/ImportSounds.py` carries that list as a refusal rather than as a note: a name matching
`music`, `theme`, `intro`, `narrat`, `speech`, `logo`, `darwinia` or `introversion` is refused even
when an event asks for it, so the exclusion is a property of the tool and not of the operator's
memory.

## What has come across, file by file

Every Species-derived file in this tree, with the Species path it came from and the tool that
converted it. Anything added later is added to this table in the same commit.

| In this tree | From Species | By | What it is |
|---|---|---|---|
| `GameData/Terrain/LandscapeDefault.dds` | `GameData/Terrain/LandscapeDefault.bmp` | `Tools/MakeTerrainPalette.py` (C1) | The landscape colour ramp: slope along x, height along y (`SpeciesTerrain.md` §6) |
| `GameData/Terrain/LandscapeDesert.dds` | `GameData/Terrain/LandscapeDesert.bmp` | `Tools/MakeTerrainPalette.py` (C1) | The same, for a desert biome |
| `GameData/Terrain/LandscapeEarth.dds` | `GameData/Terrain/LandscapeEarth.bmp` | `Tools/MakeTerrainPalette.py` (C1) | The same, for an earth biome |
| `GameData/Terrain/LandscapeIcecaps.dds` | `GameData/Terrain/LandscapeIcecaps.bmp` | `Tools/MakeTerrainPalette.py` (C1) | The same, for an icecaps biome |
| `GameData/Textures/SpectrumFont.dds` | `GameData/Textures/SpeccyFontNormal.bmp` | `Tools/ImportTextures.py` (C4) | The Spectrum font atlas, 256×224, black keyed to alpha zero |
| `GameData/Textures/Icons.dds` | `GameData/Icons/Banner{Goto,Follow,Absorb,Deploy,Unload,None}.bmp`, `Gesture{Armour,Officer}.bmp` | `Tools/ImportTextures.py` (C4) | Eight 64×64 order icons in a 4×2 atlas, unkeyed: the dark-blue field is part of the icon |

**The light pairs, the fog ranges, the palettes' numbers, the camera limits and the particle types**
are also Species's, read out of its level files and its source into `SpeciesLook.md` and
`SpeciesTerrain.md` and re-authored as rows in `GameData/Biomes.json` and as constants. They are
measurements of a running program rather than copies of its files, and they are listed here because
the distinction is worth stating rather than because it changes the decision.

**No Species geometry has come across.** `Tools/ImportShp.py` reads all 106 shapes and is committed
for reference imports; the models the game ships are `Tools/MakePlaceholderModels.py`'s primitives
until the owner authors his own (`SpeciesLineage.md` §5). **No Species sound has come across.**
`GameData/Sounds.json` is empty and `Tools/ImportSounds.py` copies nothing; `m2-skirmish/T9`
authors the events and decides what it needs.

## Consequences

- **The risk is the owner's and it is real.** Nothing here makes the provenance resolved. A
  distribution that reaches beyond the owner is the point at which it matters, and M3 is that
  point; the owner has said so and said yes.
- **The table above is the inventory**, and it is only useful if it stays complete. A task that
  moves a Species file into this tree adds its row in the same commit, as `AGENTS.md` §6 asks of
  the ADR a change implements.
- **The exclusions are enforced by a tool**, not by a reader remembering them. That is the one part
  of this decision that is mechanical, and it is mechanical because it is the part that must not
  fail quietly.
- **Replacing the content later is cheap by construction.** Everything in the table is data behind
  a path: a font atlas, an icon atlas, four palettes. Nothing in the code names Darwinia, and the
  models — the largest body of art a game like this has — were never taken.
