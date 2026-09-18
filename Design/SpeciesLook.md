# Species Look — the presentation, as configuration

**Status: REFERENCE (2026-09-17, §2 re-read 2026-09-18 for `OpenQuestions.md` Q21).** Read from the Species repository at commit `d1add55`: every light, material, fog, sky, camera and effect setting that makes a Species frame look the way it does, with the source line each value came from. Nothing was run; the numbers are the code's, and where a consequence is stated ("faces the light does not reach are black") it is derived from the fixed-function OpenGL rules the code sets, not observed on a screen. Paths are relative to the Species repository.

**Why this document exists.** The owner will make new models (2026-09-17), so what carries from Species into *Frontier Commander* is not the meshes but the configuration around them: where the lights are, what colour they are, how far the fog reaches, how high the sky is, how the camera moves. Everything here is presentation-side and float, which means none of it touches `AGENTS.md` R16. [`SpeciesTerrain.md`](SpeciesTerrain.md) covers the landscape and water in the same way; [`SpeciesCanvas.md`](SpeciesCanvas.md) covers the windows and the overlay.

---

## 1. The frame

**Coordinate frame.** Y is up, the ground is the X–Z plane, the world origin is a map corner and the map runs from 0 to its world size on both axes. **Sea level is y = 0** (`Species/Camera.cpp:65`, the comment on `MIN_HEIGHT`). The unit is the Species world unit, in which a soldier is 14 tall ([`SpeciesLineage.md`](SpeciesLineage.md) §4).

**One frame, in order** (`Species/Renderer.cpp:341` `RenderFrame`, `GameLogic/Location.cpp:1011` `Location::Render`):

```
1  SetOpenGLState              the baseline of §4, then the location's lights (§2) and fog (§5)
2  near/far planes             5 / 15,000 in a location; 50 / 10,000,000 on the campaign sphere
3  projection                  gluPerspective(vertical FOV from the camera, aspect = W/H, near, far)
4  clear                       colour = g_backgroundColour: black (0,0,0,0); white in negative mode;
                               (0.05, 0, 0.05) when no location is loaded
5  [pixel effect pre-pass]     if RenderPixelShader > 0 (§9)
6  Location::Render:
     clouds and sky            Clouds::Render — sky grid, blobby clouds, flat clouds (§6)
     landscape                 main pass, then the outline overlay (SpeciesTerrain.md §6)
     water                     flat plane, then dynamic waves (SpeciesTerrain.md §7)
     buildings                 lit, fogged (§3)
     teams                     entities: shapes and sprites
     building alphas           glows, ownership lights, additive things (§7)
     particles, weapons, spirits
7  [pixel effect apply]        (§9)
8  explosions, particle system, user-input overlays, cursor, task manager interface, camera effects
```

**Window and resolution** (`Species/Renderer.cpp:64` `Initialise`): the preferences file asks for `ScreenWidth`/`ScreenHeight` (1280×768 in `GameData/DefaultPreferences.txt`), colour depth 32, refresh 60 (75 by code default), Z depth 24, `WaitVerticalRetrace` 0 (1 by code default), windowed 0. If the mode fails, it falls back to 640×480 windowed 16-bit.

---

## 2. Lights

**Definition.** Each map carries its own lights in a `Lights_StartDefinition` block, one per line: `fx fy fz r g b` (`GameLogic/LevelFile.cpp:621` `ParseLights`). The three-vector is the **direction toward the light**, stored with a fourth component of 0 so OpenGL treats it as a directional light at infinity (`GameLogic/WorldObject.h:71` `Light`). The colour is three floats with no upper bound, and every map but two uses values above 1.0. A mission file may override the map's lights; the editor (`GameLogic/LightsWindow.cpp`) edits them in place.

**How they are applied** (`GameLogic/Location.cpp:2149` `SetupLights`, `Species/Renderer.cpp:827` `SetObjectLighting`):

- Light *i* becomes `GL_LIGHT0 + i` with `GL_POSITION` = the normalised direction, `GL_DIFFUSE` = `GL_SPECULAR` = the colour, `GL_AMBIENT` = black.
- **Only lights 0 and 1 are ever enabled.** `SetObjectLighting` enables exactly those two; a third light in a file would be set and ignored. Every map defines exactly two.
- **The scene ambient is zero** (`GL_LIGHT_MODEL_AMBIENT` = (0,0,0,0), `Renderer.cpp:792`) and every material's ambient is zero. A face neither light reaches is black. That is the single most important fact in this document: the Species look has no fill light, and its shadows are not dark, they are absent.
- The object material is diffuse 1.0, specular 0, ambient 0, shininess 127 — pure Lambert. With colour-material on (§4) the vertex colour multiplies in, so a lit face is `vertexColour × Σ lightColour × max(0, N·L)`, clamped to 1.0 per channel by OpenGL after summing.
- **Colours above 1.0 are the point.** A light of (5.00, 2.35, 0.77) saturates red at `N·L` = 0.2, green at 0.43 and blue never: faces turned even slightly toward it are full red and orange, and the falloff toward the terminator is compressed into a narrow band. That is the hard, saturated rim the Darwinia maps have.

**Every light in every map**, read from `GameData/Levels/Map*.txt`. Elevation is the angle above the horizon; azimuth is measured from +Z toward +X.

| Map | Light | Direction toward the light | Elevation | Azimuth | Colour (r, g, b) | Peak |
|---|---|---|---|---|---|---|
| Biosphere | 1 | (−0.85, 0.52, 0.04) | 31° | −87° | (0.66, 0.66, 0.66) | 0.66 |
| Biosphere | 2 | (−0.41, 0.30, 0.86) | 17° | −25° | (0.59, 0.59, 0.59) | 0.59 |
| Containment | 1 | (0.07, 0.35, 0.93) | 21° | 4° | (1.00, 1.00, 1.00) | 1.00 |
| Containment | 2 | (−0.04, 0.43, 0.90) | 26° | −3° | (2.77, 1.07, 0.00) | 2.77 |
| Escort | 1 | (−0.24, 0.49, 0.84) | 29° | −16° | (1.39, 1.09, 1.00) | 1.39 |
| Escort | 2 | (0.58, 0.00, 0.82) | 0° | 35° | (3.00, 1.68, 1.04) | 3.00 |
| Garden | 1 | (0.04, 0.39, −0.92) | 23° | 178° | (1.06, 0.96, 0.72) | 1.06 |
| Garden | 2 | (0.57, 0.00, −0.82) | 0° | 145° | (3.58, 0.79, 0.14) | 3.58 |
| Generator | 1 | (0.76, 0.13, 0.64) | 7° | 50° | (0.71, 0.71, 0.71) | 0.71 |
| Generator | 2 | (−0.99, 0.00, 0.11) | 0° | −84° | (5.00, 2.35, 0.77) | 5.00 |
| Launchpad | 1 | (0.07, 0.35, 0.93) | 21° | 4° | (1.00, 1.00, 1.00) | 1.00 |
| Launchpad | 2 | (−0.04, 0.43, 0.90) | 26° | −3° | (2.00, 1.00, 0.40) | 2.00 |
| Mine | 1 | (0.79, 0.49, 0.38) | 29° | 64° | (1.60, 0.80, 0.36) | 1.60 |
| Mine | 2 | (−0.93, 0.00, 0.36) | 0° | −69° | (5.00, 2.42, 1.15) | 5.00 |
| PatternBuffer | 1 | (−0.52, 0.40, 0.75) | 24° | −35° | (1.25, 1.25, 1.25) | 1.25 |
| PatternBuffer | 2 | (0.62, 0.36, 0.70) | 21° | 42° | (1.03, 1.03, 1.03) | 1.03 |
| Receiver | 1 | (−0.88, 0.47, 0.03) | 28° | −88° | (2.52, 1.41, 0.31) | 2.52 |
| Receiver | 2 | (−0.97, 0.00, 0.24) | 0° | −76° | (3.28, 2.21, 1.38) | 3.28 |
| Sandbox | 1 | (0.85, 0.52, 0.09) | 31° | 84° | (1.24, 1.16, 1.04) | 1.24 |
| Sandbox | 2 | (−0.66, 0.30, −0.69) | 17° | −136° | (1.04, 1.16, 1.24) | 1.24 |
| Temple | 1 | (0.57, 0.39, −0.72) | 23° | 142° | (1.05, 0.81, 0.81) | 1.05 |
| Temple | 2 | (0.27, 0.00, −0.96) | 0° | 164° | (1.05, 1.02, 0.72) | 1.05 |
| Yard | 1 | (0.48, 0.87, −0.15) | 60° | 107° | (1.05, 1.03, 0.93) | 1.05 |
| Yard | 2 | (0.99, 0.00, −0.14) | 0° | 98° | (5.00, 2.16, 0.62) | 5.00 |

**Which pair Frontier uses** (`OpenQuestions.md` Q21, owner 2026-09-18): **Sandbox's**, as `Client\Lighting.h`'s `BUILT_IN_LIGHTING` and as the `Default` row of `GameData\Biomes.json`. The Garden's pair was the built-in and is kept beside it as the `Garden` biome. The reason is the measurement above the table: flat ground reaches 0.95 luminance under Sandbox against 0.38 under the Garden, and no sampled normal is left fully black against the Garden's 11.1%, because the Garden's sun lies on the horizon and contributes exactly nothing to a surface facing up. Sandbox rather than PatternBuffer or Biosphere, the other neutral pairs, because `GameData\Game.txt` makes `MapSandbox.txt` Species' level 1 — the first map a player sees, and the one that therefore had to read clearly. Sandbox is also one of the larger maps at 5,000 units a side, against the Garden's 2,002, though the Garden is the *smallest* of the twelve and not the largest, and size does not order the rigs: the largest map, the Generator at 5,372, carries the most extreme horizontal sun in the game. Nothing here is a default: `GameLogic\WorldObject.cpp:79` gives a new light a single horizontal white at 1.3, which is nobody's map, and `GameLogic\LightsWindow.cpp:60` is a button for multiplying a light's colour up and down by hand. **What this does not change:** the zero ambient (`OpenQuestions.md` R4) stays, and no fill term is added anywhere — the two lights simply reach further. Flat ground comes out at 0.95 luminance under Sandbox against 0.38 under the Garden, and no sampled normal is left fully black against the Garden's 11.1%.

**The recipe the table shows.** Nine of the twelve maps use the same arrangement: a near-white **key** at 20–30° elevation with a peak near 1.0, and a **horizontal sun** at exactly 0° elevation, saturated orange, with a peak of 2 to 5. The horizontal light is what paints cliff faces and the sides of every object in orange while leaving their tops to the pale key, and because there is no ambient, whichever side faces away from both is black. The three exceptions are Biosphere and PatternBuffer, lit by two soft neutral lights under 1.3 (the icecaps and earth palettes), and Sandbox, lit by a warm and a cool light of equal strength from opposite sides.

**Special lighting** (`RenderSpecialLighting` = 1, `Location.cpp:1071`): a single light from (0, 1, 0.5) normalised with colour (2.0, 1.5, 0.75), material specular 1.0 and shininess 10, light 1 off. A demo mode, kept for reference.

**Seasonal**: in the last two weeks of December (`ChristmasModEnabled`) both lights become (1.3, 1.2, 1.2) and the landscape, water and wave palettes switch to the icecaps and earth ones.

---

## 3. Materials and shading

**Shapes** (`NeuronClient/Shape.cpp:1478` `Shape::Render`, `:944` `RenderSlow`, `:181` the fragment parser):

- **Flat shading**, `glShadeModel(GL_FLAT)`, set once in the baseline state. One normal per triangle, computed at load as `normalize((b − a) × (c − b))` (`Shape.cpp` `GenerateNormals`), never read from the file: no shape carries normals.
- **One colour per triangle in effect.** The file gives a colour per vertex and the renderer emits each, but under flat shading OpenGL takes the triangle's colour from its last vertex, so the middle-vertex and first-vertex colours never show. Alpha is always 255.
- `GL_COLOR_MATERIAL` is enabled for the duration of a shape with mode ambient-and-diffuse, so the vertex colour is the material. Specular stays 0 from the object material. No shape has a texture.
- Back faces are culled, front faces are counter-clockwise, polygons filled, normals not renormalised (`GL_NORMALIZE` off — the model transforms are rigid, so nothing scales).
- A fragment may carry an angular velocity and a velocity, applied to its basis rows for the prediction time before drawing (`Shape.cpp:889`): spinning parts spin between simulation ticks.

**The landscape** has its own two materials; they are in [`SpeciesTerrain.md`](SpeciesTerrain.md) §6.

**Where the team colour appears.** Not on shape geometry. It appears on sprites (citizens are tinted), on **ownership lights** — `Starburst.bmp` billboards of size 6 drawn additively at every `MarkerLight*` marker a building's shape carries, in the owning team's colour or (0.5, 0.5, 0.5) when unowned (`GameLogic/Building.cpp:365`) — and on effects such as the officer's glow (100, 250, 100). The team palette itself (`GameLogic/Location.cpp:248`):

| Team | Colour | Comment in source |
|---|---|---|
| 0 | (100, 255, 100) | "Normally Green AI" |
| 1 | (200, 50, 50) | "Normally Virii" |
| 2 | (200, 200, 30) | "Normally Player" |
| 3 | (120, 180, 255) | "Magenta" (it is blue) |
| 4–7 | (120,180,255), (50,255,50), (250,200,10), (150,150,150) | Commented out: blue, green, orange, grey |

---

## 4. The baseline state

`Species/Renderer.cpp:763` `SetOpenGLState`, applied at the top of every frame and restored by every pass that changes it. Written out because every pass below is a delta from it:

| Group | State |
|---|---|
| Geometry | cull back faces; front = CCW; fill; **flat shading**; no normal rescaling |
| Colour | colour-material off (shapes and the landscape turn it on themselves), mode ambient-and-diffuse |
| Lighting | off; lights 0–7 off; then the location sets lights 0 and 1 (§2); scene ambient (0, 0, 0, 0) |
| Blending | off; function src-alpha / one-minus-src-alpha; alpha test off, function greater than 0.01 |
| Fog | parameters set (§5), disabled; each pass enables it |
| Texture | 2D off; wrap clamp both axes; env mode modulate; env colour black |
| Depth | test on, write on, function less-or-equal |
| Hints | fog and polygon smoothing "don't care"; line width 1 |

A `CheckOpenGLState` that asserted all of this exists and is disabled; the comment on it says its eighty lines are the only written statement of the contract. This table is the second.

---

## 5. Fog

`GameLogic/Location.cpp:2124` `SetupFog`: **linear, from 1,000 to 4,000 units, in the background colour** (black; white in negative mode), density 1. The landscape, the water and the buildings enable it; the effect is that the world fades to nothing between 1,000 and 4,000 units from the camera, well inside the 15,000-unit far plane. On maps 2,000–5,400 units across, the far side of a map is fog. The sky and cloud layers set their own fog, always to black, from 2,000 to 4,000 (sky grid) or 5,000 (clouds), then restore the location's. The campaign sphere uses 0 to 19,000. **Frontier**: ADR-005 (2026-09-17) replaced this with distance desaturation, keeping the linear fog in the shader for the capture's comparison; the owner confirms or overrides at `m0-foundation/T22`. ADR-007 (2026-09-18) then fixed its range on the owner's frame: **absolute world units, 2,048 to 8,192, not fractions of the landscape's extent**, because the fog range and the camera's distance both scaled with the map and so never separated — measured, 63.9% of that frame's landscape was exactly gray. The desaturation also takes a **ceiling of 0.35**, which the linear mode does not: fading to the background is self-limiting, since a fully fogged surface has vanished, while desaturation at full strength leaves the terrain in full detail with its colour gone. Without the ceiling the far field loses precisely what the horizontal 3.58-peak sun above is for.

---

## 6. Sky and clouds

`GameLogic/Clouds.cpp`. There is no skybox and no sky colour: the sky is the clear colour (black) with three additive layers drawn over it, all with depth writes off and fog to black.

**The sky grid** (`RenderSky`, `Clouds.cpp:220`): a plane at **height 1,200** over a square from −6,000 to 8,000 on both axes (14,000 units), drawn as lines every **80 units** in both directions, each a quad 16 units wide textured with `Laser.bmp` (a soft beam), colour **(0.5, 0.5, 1.0, 0.3)**, additive, fog to black 2,000–4,000. It is the faint blue grid at the top of every Darwinia screenshot.

**Blobby clouds** (`RenderBlobby`): `Clouds.bmp` — a 16×16 noise mask — over a square from −8,000 to 9,000 (17,000 units), in up to three layers, colour **(0.7, 0.7, 0.9, 0.6)**, additive, magnification linear and minification nearest:

| Layer | Height | Texture repeats over the square | Drawn at cloud detail |
|---|---|---|---|
| 1 | 1,200 | 9 (one repeat per ~1,900 units) | 1, 2 |
| 2 | 1,000 | 4.5 | always |
| 3 | 800 | 1.8 | 1 |

**Flat clouds** (`RenderFlat`): the same texture with **nearest filtering both ways**, so the noise shows as hard 16×16 blocks — the pixelated cloud look — colour (0.7, 0.7, 0.9, 0.3), or alpha 0.5 at detail 3; a layer at 1,200 with 9 repeats and, at detail 1, one at 1,000 with 4.5. Depth test off.

**Drift**: `Clouds::Advance` adds (0.03, 0, −0.01) to the texture offset per 0.1-second server tick, which is 0.3 repeats per second on the first layer — about 570 world units per second across the 17,000-unit square, by arithmetic. Each quad is split 4×4 to keep per-vertex fog from banding.

**Sizes are absolute, not map-relative.** 14,000 and 17,000 units were chosen for maps up to 5,400 across. A *Frontier Commander* landscape of 65,536 units needs these to scale with the map, or to become a camera-relative sky. **Frontier takes neither as written: the layers follow the camera and their noise is sampled in world space** (owner, 2026-09-18, `OpenQuestions.md` Q17). Each layer is a quad covering the view at its own height, so it never runs out and never shows an edge, and its texture coordinates come from the world position under each vertex rather than from the quad, so a cloud feature keeps the size it was authored at on a landscape of any size and stays over the same piece of ground. Species's drift becomes an offset added to those world coordinates, the same animation in a different space. The layer heights, the world-space repeat period that replaces the per-square repeat counts, and the drift rate are measured on a frame by `m2-skirmish/T8` and recorded in its ADR. ADR-005 settled the fog's scaling and left the sky's; this is the sky's.

---

## 7. Camera

`Species/Camera.cpp`. The camera that gives the game its viewpoint:

| Property | Value | Where |
|---|---|---|
| Projection | vertical FOV **60°** (`m_fov`, `m_targetFov`), zoom by interpolating the FOV | `:1742`, `:1958` |
| Near, far | 5, 15,000 in a location | `Renderer.cpp:354` |
| Default orientation | front (0, −0.5, −1) normalised: pitched 26.6° down; start position (1000, 500, 1000) | `:1768` |
| Minimum height | 10 above sea level, and 10 (`MIN_GROUND_CLEARANCE`) above the highest ground sampled at the target and at ±10 units around it | `:64`, `:700` |
| Maximum height | 5,000 | `:66` |
| Tracking minimum | 200 × a height multiplier clamped to 0.25–2.0 | `:67`, `:1046` |
| Smoothing | position = lerp(position, target, 4 × dt) | `:714` |
| Blockage | a probe 100 units ahead; inside 100 the speed scales down to 0 at 30, and the camera climbs by the shortfall | `:679` |
| Free-movement speed | 250 units per second (`moveRate`), ×4 with the speed-up control; an older keyboard path uses the world size ÷ 30 per second with ×10 and ×0.1; mouse drag scales its input by 10 | `:609`, `:85`, `:645` |
| Modes | Replay, SphereWorld, **FreeMovement**, BuildingFocus, EntityTrack, RadarAim, FirstPerson, MoveToTarget, DoNothing, EntityFollow, TurretAim, and five sphere-world and menu modes | `NeuronClient/CameraAccess.h:52` |
| Camera shake | an intensity that decays; `CreateCameraShake(intensity)` takes the maximum | `:1777` |

The free camera is a fly camera with a ground floor, not an RTS camera with a fixed pitch: yaw and pitch are free, height is what the mouse wheel and the vertical keys change, and the height rule keeps it out of the terrain.

---

## 8. Sprites and particles

**Citizen-scale sprites** (`GameLogic/Citizen.cpp:2246`): a billboard 3 units wide, scaled by 1 + 0.03 × ((index × uniqueId) mod 10) so no two are quite the same size, twice as tall as wide, standing 0.3 units above the ground, with a black shadow quad on the ground behind it.

**Particles** (`GameLogic/ParticleSystem.cpp:215` `InitialiseParticleTypes`, `:160` `Render`): camera-facing quads of size `m_size / 16`, drawn **additively** (src-alpha, one) at alpha 90/255, fading to 0 over the last quarter of their life; in negative mode subtractive. Colour is a random blend of the type's two colours at birth; gravity scales with the particle's size relative to the type's; friction is applied per tick.

| Type | Life (s) | Size | Gravity | Friction | Colour 1 | Colour 2 |
|---|---|---|---|---|---|---|
| RocketTrail | 2 | 15 | 15 | 0.6 | (128, 128, 128) | (200, 200, 200) |
| ExplosionCore | 2 | 150 | 10 | 0.2 | (200, 100, 100) | (255, 120, 120) |
| ExplosionDebris | 6 | 40 | 20 | 0.2 | (200, 128, 128) | (250, 200, 200) |
| MuzzleFlash | 1 | 10 | 0 | 0.2 | (255, 128, 128) | (200, 100, 100) |
| Fire | 4 | 50 | −4 | 0 | (150, 50, 50) | (150, 120, 50) |
| ControlFlash | 1 | 30 | −2 | 0 | (50, 50, 150) | (50, 50, 250) |
| Spark | 4 | 15 | 15 | 1.5 | (250, 200, 0) | (250, 200, 50) |
| BlueSpark | 4 | 15 | 15 | 1.5 | (50, 50, 200) | (50, 70, 255) |
| Brass | 2 | 4 | 30 | 0.5 | (250, 200, 0) | (250, 200, 50) |
| MissileTrail | 5 | 200 | 1 | 0 | (100, 100, 100) | (200, 200, 200) |
| MissileFire | 0.5 | 300 | 0 | 0 | (150, 50, 50) | (150, 120, 50) |
| CitizenFire | 1 | 25 | −10 | 0 | (150, 50, 50) | (150, 120, 50) |
| Leaf | 60 | 25 | | | | |

Negative gravity rises: fire and control flashes float up. The `Particle.bmp` texture is a 16×16 grey square; the softness comes from the additive blend and the count, not the sprite.

---

## 9. The pixel effect

`Species/Renderer.cpp:854` `PreRenderPixelEffect`, `:1121` `ApplyPixelEffect`, `:1050` `PaintPixels`; preference `RenderPixelShader` (1 full, 2 partial, 0 off). The blocky glow around the player's squad is this, and it is a two-pass trick rather than a shader:

1. **Before the scene**: reset a 16×16 grid of screen cells to "unused"; set the viewport to **256 × 256** (`m_pixelSize`, fixed); for every "pixelated" object within 1,000 units of the camera and in view — squad and centipede units, every other entity, every building — call its `RenderPixelEffect`, which draws the object normally into that small viewport **and marks the grid cells its bounding sphere covers with its distance** (`MarkUsedCells`, `RasteriseSphere`); copy the 256² framebuffer into a texture with nearest filtering; clear and draw the scene as usual.
2. **After the scene**: with the depth test on and depth writes off, draw each marked grid cell as a textured quad **placed at the recorded distance in eye space**, so nearer scenery occludes it, in four passes over the same quads: additive with nearest filtering at alpha 1.0; subtractive (src-alpha, one-minus-src-colour) with linear filtering at alpha 0 and again at 0.2; additive nearest at 0.9. The result is the object's own image, magnified from 256 pixels to the screen with hard pixel edges, added over itself with a darker linear-filtered halo.

"Partial" is read only as "greater than 0" by the renderer; the water renderer additionally reads `== 1` to enable its own water effect, so partial mode is the pixel effect without the water's. What the water effect draws was not read for this document.

**Negative mode** (`RenderNegative` = 1): white background, and the landscape, water and overlay blend with one-minus-src-colour, inverting the image.

---

## 10. Preferences that shape the frame

`GameData/DefaultPreferences.txt` sets only the screen mode; the render preferences below take their code defaults when absent (a call without a default gets whatever `PrefsManager` returns for a missing key, which was not read).

| Preference | Values | Effect | Read at |
|---|---|---|---|
| `RenderLandscapeMode` | 2 vertex buffer objects (default), 1 display lists | How the landscape mesh is submitted; falls back to 1 without the extension | `LandscapeRenderer.cpp:260` |
| `RenderLandscapeDetail` | 1 high, 2, 3, 4 "upgrade" | Multiplies the terrain cell size by 1, 1.5, 2, 2.5 — **and regenerates the heightmap at that resolution**, so the simulation's terrain differs between settings; 4 also drops the outline overlay | `Landscape.cpp:542`, `LandscapeRenderer.cpp:505` |
| `RenderWaterDetail` | 1 high, 2, 3; 0 flat only | Dynamic water cell = detail % of the world size; 0 disables the waves | `Water.cpp:53` |
| `RenderCloudDetail` | 1, 2, 3 | Which cloud layers draw (§6) | `Clouds.cpp:103` |
| `RenderBuildingDetail`, `RenderEntityDetail`, `RenderTreeDetail` | 1, 2, 3 | Per-object level of detail, consulted by each building and entity | many |
| `RenderPixelShader` | 1 full, 2 partial, 0 off | §9 | `Renderer.cpp:343` |
| `RenderNegative` | 0, 1 | §9 | `App.cpp:88` |
| `RenderSpecialLighting` | 0, 1 | §2 | `Location.cpp:1071` |

---

## 11. What this means for Frontier Commander

**Carry as data.** One entry per biome in `GameData\Biomes.json` — palette, water and wave bitmaps (`SpeciesTerrain.md` §6, §7), the light pair, the fog range and colour — and one file of constants for the sky grid, the cloud layers, the camera limits, the team colours and the particle types. Every number above is a row. The file carries `Default` (Sandbox's pair) and `Garden` today; `m2-skirmish/T8` adds the other six.

**Carry as rules for the pixel shader.** Lambert only; no ambient; two directional lights whose colours may exceed 1.0, summed and clamped after the sum; one normal per triangle; one colour per triangle. That is a shader of a dozen lines, and it is the whole of the lighting.

**Decide, in the first renderer ADR.** The sky and cloud planes and the fog range are absolute distances sized for maps up to 5,400 units; a Frontier landscape is twelve times that. Either they scale with the landscape, or the fog becomes the edge of visibility and the sky follows the camera. The pixel effect is a post pass that D3D12 does more cheaply as a render target than the copy-to-texture trick, and whether the game wants it at all is a look decision to take with a running build. The owner ruled on 2026-09-17 (`OpenQuestions.md` R4): zero ambient stays, team-colour slots are drawn unlit, and the fog scales with the landscape or becomes distance desaturation, and ADR-005 chose the desaturation on two captured frames (2026-09-17), with the owner's confirmation or override due at `m0-foundation/T22`. ADR-007 then made the fog range absolute (2026-09-18) and Q21 made Sandbox's pair the built-in the same day, so the frame T22 rules on is drawn under both.

**Do not carry.** The fixed-function specifics (display lists, colour-material, the state-restoration dance every pass performs), `RenderLandscapeDetail` changing the simulation, and preferences read without defaults.
