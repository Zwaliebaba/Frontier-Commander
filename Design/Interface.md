# Interface — what the operator sees and clicks in M1

The M1 interface, as fixed panels at the authored resolution. It exists because the vertical slice cannot run without a specification of the screen: `m1-vertical-slice/K3` builds the toolkit from it, `K4` builds these panels from it, `G1` wires the orders, and neither is meant to invent anything. `Design/README.md` said this document is written before M1 starts; this is it.

**Status: DESIGN.** Written by `m1-vertical-slice/D1` on 2026-09-17. The owner's merge of its pull request is its acceptance, and §12 lists the rulings it takes that a reader may want to overturn before merging.

**M2 restyles this, it does not replace it.** The Eclipse-shaped windows of `SpeciesCanvas.md` are M2's, and they carry the same content in movable windows. Everything here is written so that a panel's *content* survives that change and only its frame is redrawn.

---

## 1. The rules this document works under

- **Everything is authored at 1920×1080 and unconditional** (ADR-004). Every rectangle, margin and glyph in this document is in authored pixels. Exactly one place reads the window's size, `FitAuthored`, and the present pass scales the finished frame.
- **Flat rectangles, one-pixel borders, a pixel font, monochrome icons with one accent colour** (`GameDesign.md` §11.4). No gradient fills except the two the chrome palette names, no drop shadows on panels, no animation except the two this document allows.
- **Readable at a glance outranks the Species look** (`GameDesign.md` pillar 3). Where `SpeciesCanvas.md` and pillar 3 disagree, pillar 3 wins, and §12 records each time it did.
- **The interface reads the replica and emits orders. It never reads the simulation.** Every panel below names what it reads from the render view or the replica and what orders it emits. A panel that needs a number nobody replicates is a defect in this document or in `N1`, not something a panel computes from the simulation.
- **The interface is the router's first sink** (`Client/InputRouter.h`). A widget that acts on an event consumes it; a panel under the cursor consumes clicks so they never reach selection.

---

## 2. The frame

```
 0                                                                          1920
 0 ┌────────────────────────┬──────────────────────────────┬────────────────┐
   │ POWER READOUT          │                              │ MATCH STATE    │ 40
   ├────────────────────────┴──────────────────────────────┴────────────────┤
   │                                                                        │
   │                                                                        │
   │                         THE WORLD VIEW                                 │
   │                    (the scene, drawn full frame,                       │
   │                     the panels sitting over it)                        │
   │                                                                        │
   │                                                                        │
792├───────────┬───────────────────┬─────────────────────────┬──────────────┤
   │           │                   │                         │              │
   │  MINIMAP  │    SELECTION      │        COMMAND          │    ORDERS    │
   │           │                   │   (five tabs, §7)       │              │
   │           │                   │                         │              │
1080└───────────┴───────────────────┴─────────────────────────┴──────────────┘
    0         288                 800                      1568           1920
```

| Panel | Rectangle (x, y, width, height) | Always present |
|---|---|---|
| Power readout | 0, 0, 400, 40 | Yes |
| Match state | 1520, 0, 400, 40 | Yes |
| Minimap | 0, 792, 288, 288 | Yes |
| Selection | 288, 792, 512, 288 | Yes |
| Command | 800, 792, 768, 288 | Yes |
| Orders | 1568, 792, 352, 288 | Yes |
| Match-end overlay | 560, 380, 800, 320 | On the match ending (§10) |
| Pause menu | 760, 420, 400, 240 | On Escape (§10) |

**The world view is the whole frame.** The scene is drawn at 1920×1080 and the panels are drawn over it, so the camera never has a smaller viewport than the screen and a unit is never hidden behind a panel without the player being able to scroll to it. The consequence is that the bottom 288 rows and the two 40-row corners are obscured; picking refuses a ray whose origin is inside a panel rectangle (§5), so nothing is ever selected through a panel.

**Nothing moves.** No panel slides, collapses, or animates its size. The two animations this document allows are the tooltip's typewriter (§4) and the construction and research progress bars, which advance with their value and nothing else.

---

## 3. The chrome

One palette table, `GameData\Interface.json`, loaded by `Content` alongside the other tables, so that the look of a panel is data (`SpeciesCanvas.md` §7). Every colour below is that file's starting value, in 8-bit RGBA. The same file carries the eight commander colours of `GameDesign.md` §11, which are not chrome but are read by the same panels and the same minimap, and one file of eight rows is cheaper than a second loader (`C6`).

| Role | Colour | Where |
|---|---|---|
| `panelFill` | (24, 10, 12, 245) | Every panel's body |
| `panelBorder` | (199, 214, 220, 255) | The one-pixel border of every panel |
| `panelTitleFrom` | (199, 214, 220, 255) | The title strip's gradient, top |
| `panelTitleTo` | (112, 141, 168, 255) | The title strip's gradient, bottom |
| `titleText` | (255, 255, 150, 255) | A panel's title, uppercased |
| `bodyText` | (222, 226, 230, 255) | Labels and values |
| `dimText` | (128, 132, 136, 255) | A value that is unavailable or zero |
| `accent` | (255, 196, 64, 255) | The one accent: selection, the active tab, a ready item |
| `warning` | (232, 96, 72, 255) | A rejection, a losing state, damage under a quarter |
| `buttonFill` | (107, 37, 39, 255) | A button at rest |
| `buttonFillHover` | (140, 52, 54, 255) | Under the cursor |
| `buttonFillDown` | (199, 214, 220, 255) | Held |
| `buttonDisabled` | (60, 32, 34, 255) | Unavailable; its caption takes `dimText` |
| `barEmpty` | (40, 20, 22, 255) | A progress or health bar's track |
| `barBuild` | (120, 180, 220, 255) | Construction and production progress |
| `barHealth` | (96, 200, 108, 255) | Health at or over half |
| `barHealthLow` | (232, 96, 72, 255) | Health under a quarter |

**A panel** is a filled rectangle in `panelFill`, a one-pixel `panelBorder` on all four edges, and a 20-pixel title strip along the top filled with the vertical gradient `panelTitleFrom` → `panelTitleTo`, holding the panel's name in `titleText`, uppercased, at x + 8. The client area is inset 8 pixels left, right and bottom, and starts 24 pixels from the top.

**A button** is a filled rectangle with a one-pixel `panelBorder`, 24 pixels high, its caption in `bodyText` centred vertically and inset 8 pixels from the left, or centred when the button is square. Its fill is `buttonFill`, `buttonFillHover`, `buttonFillDown` or `buttonDisabled`; a down button's caption takes `panelFill` so that it stays readable on the light fill.

**A bar** is a filled `barEmpty` rectangle with a one-pixel `panelBorder`, 12 pixels high, filled from the left in its value's colour. A bar never animates toward its value; it is drawn at the value the frame has.

**An icon** is 32×32 authored pixels from `GameData\Textures\Icons.dds`, drawn as one quad in `bodyText` where it is available and `dimText` where it is not, on the panel fill. Monochrome with one accent (`GameDesign.md` §11.4): an icon is a single-channel mask tinted at draw time, never a coloured bitmap, so that the accent is the palette's and a mod recolours nothing by accident.

### Text

**Text is monospaced, 16 authored pixels a glyph box, with a 16-pixel advance and a 20-pixel line height.** The Spectrum atlas is 16 columns by 14 rows of 16×16 cells from ASCII 32 (`SpeciesCanvas.md` §4), and a glyph drawn at 16×16 is one texel to one pixel. A line of *n* characters is exactly 16*n* pixels wide, which is the arithmetic every layout in this document uses.

This is a ruling against `SpeciesCanvas.md` §7, which asks for sizes 12 and 13 drawn 1:1. At 1920×1080 with a 16-pixel cell, 12 and 13 are not 1:1 with anything: they resample every glyph, which is the single thing pillar 3 and ADR-004's authored resolution exist to prevent. Species drew at 12 and 13 because `GetMenuSize` scaled everything to the window; this game does not scale, so it can afford the atlas's own size. §12 records it as overturnable.

**There is no second size and no second face.** Species's title and caption faces become one; a title is distinguished by its colour and its gradient strip, not by its font.

---

## 4. The pointer, the cursor and input

### Coordinates

`FrameInput::mouseX` and `mouseY` are **client pixels**. Every rectangle in this document is in **authored pixels**. `Client/ScaleMode.h` maps authored to client through `FitAuthored`; M1 adds its inverse, `AuthoredFromClient(const ScaledRectangle&, clientX, clientY)`, returning the authored position and whether it landed inside the letterbox at all. It is the one place the conversion happens, it lives beside `FitAuthored`, and `K3` writes it with the test that round-trips it against `FitAuthored` at all three scale modes.

A pointer outside the scaled rectangle — in the letterbox bars — is over no panel and over no world, and every hit test refuses it.

### The buttons

| Input | What it does | Where it is decided |
|---|---|---|
| Left click on a panel | The widget under it acts and consumes the event | `UiInputSink`, the router's first sink |
| Left click on the world | Selects the object under the ray; nothing under it clears the selection | §5 |
| Left drag on the world | Selects every own device inside the rectangle | §5 |
| **Right click on the world** | **Gives the selection its default order** (§6) | §6 |
| Right click on a panel | Nothing; the panel consumes it | `UiInputSink` |
| **Middle button held** | **Aims the camera**, at `AIM_RADIANS_PER_COUNT` | §12, ruling 3 |
| Wheel | Camera height; over the minimap, nothing | `CameraController` |
| W A S D, the arrows, the screen edges | Camera movement, as M0 built them | `CameraController` |
| Q E, R F, Page Up, Page Down | Camera yaw, pitch and height, as M0 built them | `CameraController` |
| Shift | Camera speed ×4; with an order, queues it behind the current one | §6 |

**The right button belongs to the game, and the camera gives it up.** `SpeciesCanvas.md` §2 is explicit that the right button never touches a window, and `G1` needs right-click for Move and Attack. M0's `CameraController` currently aims with the right button held; M1 moves aiming to the middle button held, and `G1` makes that change. The alternative, a drag threshold that delays every order by up to a quarter second, was refused: an order that arrives late is the one thing a real-time game cannot afford, and `TechnicalDesign.md` §3 already spends 250 to 330 milliseconds getting a click to visible movement.

### Hotkeys

Bindings live in `Preferences.json` (`TechnicalDesign.md` §9) as a control name to a key, and these are the defaults. `InputSubscription.h` already says the binding layer is M1's.

| Key | Control |
|---|---|
| 1 – 5 | The command panel's tabs: Construct, Produce, Research, Design, Match |
| Ctrl + 0 – 9 | Assign the selection to a numbered group (`Group`) |
| 0 – 9 | Select that group; twice in quick succession centres the camera on it |
| Space | Centre the camera on the selection |
| M, P, R, B | Move, Patrol, Attack-move, Build: arm that order for the next world click |
| S | Stop |
| H | Hold position stance |
| Tab | Cycle the selection through devices of the same design |
| Delete | Demolish the selected own structure, after a confirm click |
| Escape | Cancel an armed order, then a modal panel, then the pause menu (§10) |
| Enter | Open the chat line (§10) |
| F1 | Toggle the panel overlay off, for a clean look at the world |

**Escape is peeled, not swallowed.** It cancels the innermost thing that is open, one press at a time, and only opens the pause menu when nothing else is open. The window procedure owns Escape for closing the window (ADR-004), so `G1` moves that to the pause menu's Quit and the procedure keeps only Alt+F4.

### The cursor

The game draws its own cursor, Windows' being hidden (`SpeciesCanvas.md` §6), as a 32×32 quad from `Icons.dds` at the pointer. M1 has six:

| Cursor | When |
|---|---|
| Arrow | The default, over panels and over nothing |
| Select | Over an own device or structure |
| Move | An armed Move or the default order over open ground |
| Attack | Over a visible enemy, or an armed Attack |
| Build | Placing a structure, over legal ground |
| Refuse | Placing over illegal ground, or an order the selection cannot take |

### Tooltips

Hovering a widget for 1,000 milliseconds shows a tooltip: a panel-framed box at the pointer, offset 16 right and 16 down, flipped to the other side when it would leave the frame, holding one or two lines of `bodyText`. The text is revealed at 50 characters a second (`SpeciesCanvas.md` §5), which is the one piece of the overlay's character this document carries. Moving off the widget hides it at once.

---

## 5. Selection

**What can be selected.** Own devices and own structures, one at a time by click, or several devices by rectangle. A rectangle never selects structures and never selects another commander's anything. Clicking a visible enemy device or structure selects it as an *inspection*: the selection panel shows it (§8) and the orders panel is empty, because nothing it can be ordered to do exists.

**How it is picked.** `R2`'s picking functions, as pure functions over the replica and the camera matrices: the nearest object under a screen ray, and the objects inside a screen rectangle. A ray whose origin lands inside any panel rectangle of §2 is refused before it is cast.

**What it looks like.** A selected object carries a one-pixel `accent` outline in the world, drawn by the geometry pass from the render view's selection flag; there is no selection circle on the ground and no bracket. A rectangle drag draws a one-pixel `accent` rectangle over the world while the button is held.

**Groups.** Ten numbered groups, 0 to 9, held by the simulation through the `Group` order so that they survive a rejoin and appear in a replay. Assigning replaces the group. A group that has lost every member is empty and selecting it does nothing.

---

## 6. Orders on the world

**The default order** is what a right click gives, decided by what is under the cursor:

| Under the cursor | Selection | Order |
|---|---|---|
| Open ground | Devices | `Move` to that point |
| A visible enemy device or structure | Devices with a weapon | `Attack` that object |
| A visible enemy | Devices with no weapon | `Move` to the object's position |
| An own damaged structure | Devices with a builder | `Move` adjacent; repair is the builder's standing behaviour |
| An own structure plan | Devices with a builder | `Move` adjacent and begin it |
| Anything | A structure is selected | Nothing; a structure takes no primary order |
| The minimap | Devices | `Move` to the landscape point the minimap pixel names |

**An armed order** is one of Move, Patrol, Attack-move or Build, armed by its hotkey or by a button in the orders panel, which changes the cursor and makes the *next* left click on the world issue that order instead of selecting. Escape or a right click disarms it. Patrol takes two clicks, the second setting the far point.

**Shift queues.** An order given with Shift held is appended to the selection's order list rather than replacing it. The simulation's twenty kinds carry no queue (`Sim/Order.h`), so the client holds the queue and issues the next order when the replica reports the current one finished. This is stated plainly as a client-side convenience, not a simulation feature: a rejoining client does not recover a queue, and `TechnicalDesign.md` §4.7 is not changed for it.

**Every order is acknowledged locally at once** — a one-frame `accent` mark at the target point and a sound — and the unit moves when the replica says it has. There is no client-side prediction (`TechnicalDesign.md` §3).

**A rejection is shown.** The simulation records a reject reason per seat (`S2`: `NotOwned`, `NotVisible`, `CannotAfford`, `AtCap`, `InvalidTarget`, `InvalidPlacement`, `NotResearched`, `NoCommandPost`, `Malformed`). It reaches the client in the frame, is drawn as one line of `warning` text centred at y 720 for two seconds, and replaces the line already there. `SeatState` carries it (`N4`, 2026-09-19) as three fixed-size fields — a wrapping `rejectSequence`, the `OrderKind` and the `RejectReason` — rather than the list the simulation keeps, because this section draws one line at a time. **The client redraws on the sequence and not on the value**: two identical refusals are equal field for field, so without it a commander who asks twice for what he cannot afford would watch the line sit there and read it as not having been heard.

---

## 7. The command panel

One panel, 768×288 at (800, 792), with five tabs along its title strip, selected by click or by the keys 1 to 5. The active tab's button is filled `accent` with its caption in `panelFill`.

### 7.1 Construct (tab 1)

A row of structure buttons, one per structure the seat has researched, each 128×64 holding a 32×32 icon, the name, and the cost in power. A button the seat cannot afford takes `buttonDisabled` and its cost takes `warning`. M1 has six structures: command post, extractor, generator, factory, research lab, tower.

Clicking one enters **placement**: the cursor becomes Build, and a footprint ghost follows the pointer on the terrain, a one-pixel outlined rectangle of the structure's footprint in cells, filled at a quarter alpha, `accent` where the placement is legal and `warning` where it is not. A left click places the plan and stays in placement so that a wall or a row of extractors is placed without returning to the panel; Escape or a right click leaves it.

**The ghost's legality is the client's guess and says so.** It is evaluated on the replica's copy of the landscape by the same rules as the simulation's placement — no structure, no feature, no water, slope under 25%, every cell explored, and a deposit under an extractor. The replica does not hold flatten deltas outside a structure's own record (`TechnicalDesign.md` §5.2), so ground flattened by a structure the commander cannot see can read as illegal when the host would allow it, or the reverse. The ghost is therefore advisory; the host decides, and an `InvalidPlacement` rejection is shown as §6 says. This is a known and accepted disagreement, not a defect to design around.

The panel also shows **the plan count**, *n* of 64, in `dimText`, and turns it `warning` at the cap.

### 7.2 Produce (tab 2)

Shown for a selected own factory; empty with a line of `dimText` otherwise.

- **The design list**, left half: every design the seat has saved, one row of 32 pixels each holding the name, the cost and the build time in seconds. Clicking one appends it to that factory's queue (`SetProduction`).
- **The queue**, right half: up to eight entries, the first with a `barBuild` progress bar and the seconds remaining, the rest as names. Clicking an entry removes it (`CancelProduction`).
- **The remaining time** is computed by the client as the ticks the replica reports remaining, divided by the tick rate. The host owns the arithmetic that turns builder rates into progress; the panel divides and prints.

A factory whose commander is at the device cap shows `AT THE DEVICE CAP` in `warning` where the progress bar would be, and the queue does not advance (`GameDesign.md` §4).

**There is no rally point in M1.** The twenty order kinds carry none (§11, gap for a later milestone), and a device appears beside its factory and holds.

### 7.3 Research (tab 3)

A scrolling list of the research items available to the seat, one row of 32 pixels each: the name, the cost, the time in seconds, and the effect in one line from the item's `description` field. A row whose prerequisites are unmet is not listed at all; a row the seat cannot afford takes `dimText` with its cost in `warning`.

Clicking a row starts it in the first idle lab (`SetResearch`). A lab that is researching shows above the list as a row with a `barBuild` bar and the seconds remaining; clicking that row cancels it (`CancelResearch`), which refunds nothing, and a confirm click is required.

**The auto-research toggle** sits in the panel's top right: a 24-pixel button that fills `accent` when on. It is a lobby setting per seat (`S6`), so toggling it mid-match is not in the twenty order kinds; in M1 the toggle is **read-only**, showing the lobby's value, and §11 hands the order kind to the milestone that adds a lobby.

### 7.4 Design (tab 4)

Requires a standing command post. Without one the tab shows `A COMMAND POST IS NEEDED` in `dimText` and nothing else.

Three columns of buttons — chassis, drive, modules — listing what the seat has researched, one selected in each of the first two and up to the chassis's mount count in the third. M1 offers three chassis, three drives and four modules, so the columns never scroll.

Under them, **the derived statistics**, from `Content/DesignStats`, recomputed on every change and shown before the commander commits:

```
SPEED        104 wu/s      HIT POINTS    100
ARMOUR      5 K / 5 T      SIGHT      20 cells
COST         130 power     BUILD TIME     13 s
```

A field that a research upgrade has raised is drawn in `accent` with the base value in `dimText` beside it. A design that cannot be built shows the fault from `DesignFault` as one line of `warning` — no modules, too many modules, or a module this chassis refuses — and the Save button is disabled.

A name field takes `WM_CHAR` characters, up to 24, and Save emits `SaveDesign`. Designs are per commander and saved between matches (`TechnicalDesign.md` §9). **There is no delete and no rename in M1**: the twenty kinds carry neither, and a seat holds at most sixteen designs, after which Save replaces the design of the same name or is refused.

### 7.5 Match (tab 5)

What the lobby fixed, so that a player can check it without leaving: the landscape's name and size class, the victory condition, the device cap, the starting power, the technology tiers, and each seat's kind and alliance with its commander colour. Read-only. It exists because M1 has no lobby and the defaults are otherwise invisible.

---

## 8. The selection panel

512×288 at (288, 792). What it shows depends on what is selected.

**One own device**: its design name, an icon, a `barHealth` bar with the hit points as `current / max`, the rank badge and rank name, the chassis, drive and modules as three lines, and the current order kind with its target. A device under half health draws its bar in `barHealth` until a quarter and `barHealthLow` under it.

**Several own devices**: a grid of up to 32 portraits, 48×48 each, each with a two-pixel health strip along its bottom edge and a one-pixel `accent` border on the one the panel's detail lines describe. Clicking a portrait narrows the selection to that device; Ctrl-clicking removes it from the selection. Over 32 selected, the grid shows the first 32 and a count.

**One own structure**: its name, an icon, a health bar, and its role's own line — a factory's current build, a lab's current item, a generator's served extractor count, an extractor's yield per second or `UNSERVED` in `warning`, a command post's trickle. A structure under construction shows a `barBuild` bar and the seconds remaining instead of health, because health follows progress (`GameDesign.md` §5).

**A structure plan** shows the structure's name, `PLAN`, its cost, and `NOT STARTED` until a builder begins it.

**A visible enemy device**: its chassis, drive and modules, its health bar, and its rank. Designs are not secrets (`GameDesign.md` §8). The parts arrive with the design record for the first device of that design a client sees; **until that record arrives the panel shows the health bar and `DESIGN UNKNOWN` in `dimText`**, and fills in when it arrives.

**A ghost structure** — one in an explored cell that the commander cannot currently see — shows its kind and the health it had when last seen, with the whole panel's body text in `dimText` and the word `REMEMBERED` in the title strip. It is never shown as live, because that is the one thing the fog exists to prevent.

**Nothing selected**: the panel body is empty but for a `dimText` line naming the two things a player who has just started needs, `LEFT CLICK TO SELECT` and `RIGHT CLICK TO ORDER`.

---

## 9. The orders panel, the minimap and the readouts

### 9.1 Orders (1568, 792, 352, 288)

Shown for a selection of own devices; empty for a structure or an enemy.

**Primary orders**, as a row of 48×48 icon buttons: Move, Attack-move, Patrol, Guard, Stop. An order that arms (§6) fills `accent` while armed. `ReturnToRepair` is listed and **disabled in M1**, because M1 has neither a repair bay nor a repair module (`S10`), with a tooltip saying so.

**Stances**, as four rows of small buttons, each row a mutually exclusive set, the active one filled `accent`:

| Row | Options | In M1 |
|---|---|---|
| Fire | At will / Return fire / Hold fire | All three |
| Range | Optimal / Long | Both |
| Retreat | 50% / 25% / Never | **Never only**; the other two are disabled with a tooltip, because nothing repairs |
| Movement | Pursue / Hold position | Both |

Each emits `SetStance`. A mixed selection shows no option filled in a row where its devices disagree, and clicking sets them all.

### 9.2 Minimap (0, 792, 288, 288)

The client area is exactly **256×256**, so a Small landscape of 128 cells is **two pixels a cell**, drawn without resampling — which is why the panel is this size and why M1 is a Small landscape. A Medium landscape would be one pixel a cell and a Large one pixel to four, and §11 hands the scale rule for those to the milestone that ships them.

Drawn back to front:

1. **The fog**, from the render view's fog grid: black where unexplored, the terrain colour at half brightness where explored, at full where visible. This is the same grid the fog pass reads (`K2`), so the minimap and the world can never disagree.
2. **Structures**, two pixels a cell of their footprint, in their commander's colour; a ghost structure at half brightness.
3. **Devices**, one pixel each, in their commander's colour. Only what the interest set holds, which is what the commander can see.
4. **The selection**, in `accent`, over whatever colour the object had.
5. **The camera frustum**, a one-pixel `panelBorder` quadrilateral where the frustum's four far corners meet the ground plane, clipped to the map.

A left click moves the camera to that point, keeping its height and orientation. A left drag scrubs the camera continuously. A right click issues the default order to that landscape point (§6). The wheel does nothing over the minimap, so that a player scrubbing does not fall out of the sky.

### 9.3 Power readout (0, 0, 400, 40)

One line, 16-pixel text, in `bodyText`:

```
POWER 1,240 / 2,000   +15/s
```

The stockpile and its cap, then income a second. The stockpile comes from `SeatState`. **The cap and the income do not exist on the wire** — the cap is 1,000 plus 500 a generator and the income is five a second for every served extractor plus the command post's trickle, and both depend on structures the fog may hide from the seat's own view of itself, which it does not. M1 derives both client-side from the seat's own structures in the replica, which are always in its interest set; §11 gives `N1` the option of replicating them instead, which is the better answer and is not needed for the slice.

The stockpile turns `warning` at the cap, because income is being lost.

### 9.4 Match state (1520, 0, 400, 40)

The elapsed match time as `mm:ss` from the replica's tick, and, when the victory condition is Survival, the time remaining. Nothing else: a frame-rate counter and a network readout are a debug overlay, not this panel, and `G2` adds them behind F3 if it wants them.

---

## 10. Overlays

**The match-end overlay** (560, 380, 800, 320) appears when the seat's victory state leaves `Playing`. It fills the panel frame, shows `VICTORY` in `accent` or `DEFEAT` in `warning` at 32 pixels — the one place text is drawn at a size other than 16, as a doubled 16-pixel glyph, which is exactly 1:1 at twice the scale and stays crisp — the match duration, and a Quit button. The simulation stops advancing once decided (`S11`), so the world behind it is frozen and the camera still moves, which is deliberate: a player wants to look at the field.

**The pause menu** (760, 420, 400, 240) appears on Escape when nothing else is open: Resume and Quit, stacked. It pauses the local host, which M1 may do because the host is a thread in the same process (`G1`); it is not a simulation state and no order carries it, so §11 notes it as the thing that must change before a match has a second human in it.

**The chat line** opens on Enter as a single-line field along the bottom of the world view, above the panels at y 760, taking `WM_CHAR` characters up to 128, and emits `Chat` on Enter. Messages appear as up to four lines of `bodyText` above it, each for eight seconds. M1 has one human, so this exists to prove the order kind travels rather than to be used.

---

## 11. What this document needs that does not exist yet

Each of these is a real gap found while writing this document, with the task that owns it. The ones marked **blocks K4** must land before the panels are finished.

**Rows 3, 4 and 5 changed owner on 2026-09-19.** They were given to `N1`, `C1` and `C2`, and those three tasks were marked `done` without them — the acceptance lines that would have caught it were never written, and `Tools/CheckTaskDag.py` reads the plan's YAML and not this table, so a row whose owner is `done` is invisible to the thing that schedules work. They are now `N4` and `C6` in `tasks/m1-vertical-slice.yaml`, where the checker can see them. Row 4 is also moved ahead of `K3`: this document previously said none of these blocks `K3`, which was true of the letter of `K3`'s acceptance and false of its intent, because a toolkit that hard-codes the palette leaves nobody owning the move to data that §3 promises.

| # | What is missing | Owner |
|---|---|---|
| 1 | `AuthoredFromClient`, the inverse of `FitAuthored`, with its round-trip test | `K3` |
| 2 | `Core/RenderView.h` must carry a selection flag, construction progress, a commander colour index rather than a packed colour, the fog grid, and a wreck-or-projectile distinction | `R2` — **blocks K4** |
| 3 | ~~A wire record carrying the per-seat order rejections `S2` already records, so §6 can show them~~ | **Done 2026-09-19 by `N4`**: `SeatState` carries the seat's last refusal as a sequence, a kind and a reason |
| 4 | `GameData\Interface.json`, the chrome palette of §3, and its loader row | `C6` — **blocks K4** |
| 5 | The eight commander colours, as a content table | Ruled in `GameDesign.md` §11 (owner, 2026-09-19); `C6` carries the table, and `R2` reads an index into it |
| 6 | The eight ranks' names, badges and percentages. `GameDesign.md` §6 says "a small percentage" and nothing more, and `S5` defers to a design that proposes none | `GameDesign.md` §6, then `C2` |
| 7 | The icon list: six cursors and one icon per structure, module, order and stance, with `Icons.dds` laid out as a grid of 32×32 cells | `C4` — **blocks K4** |
| 8 | Whether a structure's own "target priority" stance has options at all; `GameDesign.md` §8 names it and gives no set | `GameDesign.md` §8 |
| 9 | A rally-point order kind, a design-delete order kind, and a pause that is part of the match rather than of the process. All three need a twenty-first order kind, which changes the wire | A later milestone; noted in `TechnicalDesign.md` §4.7 |
| 10 | The minimap's scale rule for Medium and Large landscapes | The milestone that ships them (M2, M3) |
| 11 | The survival clock's default duration | `GameDesign.md` §2 |

---

## 12. The rulings this document takes

Each is a decision this document made that a reader may reasonably want to overturn, and the owner's merge is the ruling. Overturning one is an edit here, not an ADR, because none of them is an engineering decision.

1. **Text is 16 pixels, monospaced, one face.** Against `SpeciesCanvas.md` §7, which asks for 12 and 13. The reason is in §3: at a fixed authored resolution, a size that is not the atlas's cell resamples every glyph, and pillar 3 is what the authored resolution exists to serve. Overturning this means accepting soft text or re-authoring the atlas at 12 and 13.
2. **One-pixel borders, no drop shadow on a panel.** Against `SpeciesCanvas.md` §3's 2-pixel border with a 1-pixel outer loop, and with `GameDesign.md` §11.4 and `K3`, which both say one pixel. The yellow-twice-with-shadow title treatment is dropped with it; the title's glow was Species's answer to text over a red gradient, and this document's panels are dark.
3. **The middle button aims the camera; the right button gives orders.** M0 bound aiming to the right button because no orders existed yet. The alternative considered and refused was a drag threshold on the right button, which delays every order.
4. **The world view is the whole frame and the panels sit over it.** The alternative, a world viewport above a HUD strip, wastes no pixels to occlusion but makes the scene 1920×792 and breaks the authored resolution ADR-004 fixed.
5. **Shift-queued orders are a client-side convenience.** The simulation has no queue and does not gain one for M1. A rejoining client loses its queue.
6. **The auto-research toggle is read-only in M1.** Changing it mid-match needs an order kind that does not exist.
7. **The footprint ghost is advisory.** The client can disagree with the host about legality over ground whose flatten deltas it has not been sent, and the rejection message is the correction.
8. **A ghost structure is visibly remembered**, in dim text under a `REMEMBERED` title, rather than drawn as live with a subtle difference.
9. **The minimap is 256×256 and M1's landscape is Small**, so the map is two pixels a cell with no resampling.
