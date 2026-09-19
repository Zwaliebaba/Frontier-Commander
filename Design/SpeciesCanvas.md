# Species Canvas — the windows and the overlay

**Status: REFERENCE (2026-09-17).** Read from the Species repository at commit `d1add55`: the window toolkit, the game's window chrome, the fonts, the task-manager overlay and the cursor — what is drawn in two dimensions over the world, and the rules it follows. Nothing was run. Paths are relative to the Species repository.

**"Canvas"** is the name of the project-filter group in `NeuronClient/NeuronClient.vcxproj.filters` that holds the toolkit — `Eclipse.cpp`, `EclWindow.cpp`, `EclButton.cpp` and their headers — beside the Audio, Input and Network groups. The game's styled window (`GameLogic/SpeciesWindow.cpp`), the overlay (`Species/TaskManagerInterfaceIcons.cpp`) and the cursor (`Species/GameCursor.cpp`) build on it from the layers above. This document covers all four, because together they are the interface `GameDesign.md` §11 calls "a terminal".

---

## 1. The model

**A window** (`NeuronClient/EclWindow.h`) is a rectangle in screen pixels — `m_x`, `m_y`, `m_w`, `m_h` — with a **name that is its identity** (every lookup is by name), a title, two flags (`m_movable`, default true; `m_resizable`), a list of buttons, and the name of the button currently being text-edited. It has six virtuals: `Create`, `Remove`, `Update`, `Render(hasFocus)`, `Keypress`, `Char` and `MouseEvent`.

**A button** (`NeuronClient/EclButton.h`) is a rectangle **relative to its window**, with a name, a caption, a tooltip and a parent, and the virtuals `Render(realX, realY, highlighted, clicked)`, `MouseUp`, `MouseDown`, `MouseMove`, `Keypress`, `Char`. Everything in a window is a button: labels, input fields, drop-downs, scroll bars, close boxes, all derive from it.

**The toolkit state** (`NeuronClient/Eclipse.cpp`) is global: the list of windows — **index 0 is the front** — an optional popup window, an optional maximised window, the name of the focused window, the name of the button under the mouse, the mouse position, whether the left button is down, where it went down, and a tooltip timer.

---

## 2. The rules

Read from `Eclipse.cpp:57`–`:320`; every one of them is behaviour a player can feel.

- **Registration** (`EclRegisterWindow`, `:346`): the window is inserted **at the front**, clamped onto the screen (`MakeAllOnScreen`), and its `Create` is called. A duplicate name is checked for and then nothing is done about it (the branch is empty), so two windows of one name can coexist and every name lookup finds the front one. A child window registered at (0, 0) with a parent is placed at a random offset beside the parent, on the side that has more screen.
- **Focus and front**: a left press inside a window gives it focus and brings it to the front. A left press outside every window sets focus to none **and is not consumed** — the click goes to the game. **The right button never touches a window** (`EclMouseDown` returns false for it): right-click is the game's.
- **Press and release**: a press on a button calls its `MouseDown`; a press on window background calls the window's `MouseEvent` and, if the window is movable, records the drag offset. On release, the button **under the cursor at release time, in the window that was pressed**, gets `MouseUp`; a release on background removes the popup.
- **Drag and resize** (`EclMouseMove`, `:107`): with the left button held on background, a press within **4 pixels of the right or bottom edge** resizes (if resizable; minimum 60 × 40), anywhere else moves (if movable), both by the press offset.
- **Hover and tooltip**: with no button held, the button under the mouse in the **focused** window becomes current; when it changes, a timer is set 1,000 ms ahead, and once the mouse has rested on it that long the tooltip callback fires with the window and button. Leaving a button fires the callback with nulls. **Species registers no callback**, so window tooltips never appear; the overlay has its own (§5).
- **Keyboard**: key events go to the focused window's `Keypress`; typed characters (`WM_CHAR`) go to its `Char`, which returns whether it consumed them — the seam the input router uses to decide whether a keystroke was the interface's.
- **Render** (`EclRender`, `:269`): if a window is maximised, it alone; otherwise every window **from the back of the list to the front**, each told whether it has focus. A window's default render draws its buttons in reverse registration order, each with `highlighted` = the mouse is over it or it is the text-edit target, and `clicked` = the window has focus and the button is the one currently pressed.
- **Update** (`EclUpdate`): every window's `Update` each frame. `SpeciesWindow::Update` moves a keyboard cursor through `m_buttonOrder` with the menu-up and menu-down controls when the window has focus, and subscriptions to menu-activate and menu-close press the current button or close the window — the whole interface is navigable without a mouse.
- **Auto-size**: registering a button grows the window to fit it (`EclWindow.cpp:79`: height = button bottom + 10).
- **Popups** are windows registered through `EclRegisterPopup` and dismissed by any release outside a button.

---

## 3. The chrome

`GameLogic/SpeciesWindow.cpp:529` `SpeciesWindow::Render`. A window, from back to front:

| Element | How it is drawn | Colour |
|---|---|---|
| Background | `Textures/InterfaceRed.bmp` (64×512, a vertical red gradient) repeated 8× across the width and once down the height, alpha 0.96 | the texture |
| Title bar | a gradient quad of height `GetClientRectY1() − 1` = 15 (scaled), top to bottom | (199, 214, 220) → (112, 141, 168) |
| Border | 2-pixel lines on all four edges | (199, 214, 220) |
| Outer line | a 1-pixel loop 2 pixels outside the window | (42, 56, 82) |
| Title | game font, size `GetMenuSize(12)`, centred, **uppercased in the constructor**, drawn twice with the shadow pass on | (255, 255, 150, 30) |
| Client area | inset 2 pixels left, right and bottom, 16 from the top | |

The title's colour has an alpha of 30 and is drawn twice with a shadow: the low alpha is what makes the yellow text glow softly against the red rather than sit on it, and it is repeated everywhere yellow text appears.

**Buttons** (`SpeciesButton::Render`, `:58`):

| State | Fill | Lines | Caption |
|---|---|---|---|
| Normal | (107, 37, 39, 64) | top and left (100, 34, 34, 200); right and bottom (0.1, 0, 0, 1.0) | editor font, white, or grey (0.5) when disabled; left-aligned at +5 or centred |
| Highlighted | gradient (199, 214, 220) → (112, 141, 168) | | (255, 255, 150, 30) twice with shadow; (128, 128, 75, 30) when disabled |
| Clicked | gradient white → (162, 191, 208) | | as highlighted |

A highlighted button also becomes the keyboard cursor's current button, so mouse and keyboard navigation never disagree. **Borderless buttons** (menu items) default to 7 pixels per character plus 9 wide and 15 high, draw only their lines when highlighted, their caption in white with a second 50%-white pass when highlighted, and the full gradient when clicked or when they are the keyboard's current button.

**Widgets**: the input field fills (0.1, 0, 0, 0.5) with a black and a (100, 34, 34, 150) edge; the drop-down draws its text in white; the scroll bar has a track of (85, 93, 78), a thumb of (187, 187, 187) and arrow buttons in three blues around (55, 60, 120).

**Menu scaling** (`GetMenuSize`, `:633`): with `g_largeMenus` on (the *Other* dialog's large-menus option set to 2, `App.cpp:167`) every size and font in a window is multiplied by `0.96 × screenHeight / 460`; otherwise sizes are literal pixels. Main-menu windows centre themselves on the screen and use font size 13 for their items.

---

## 4. Fonts

`NeuronClient/TextRenderer.cpp`. Two fonts, both bitmap:

| Global | Bitmap | Used for |
|---|---|---|
| `g_gameFont` | `Textures/SpeccyFont<Language>.bmp`, falling back to `SpeccyFontNormal.bmp` | window titles, the overlay, in-world text |
| `g_editorFont` | `Textures/EditorFont<Language>.bmp`, falling back to `EditorFontNormal.bmp` | button captions, the editor |

Both atlases are 256×224, **16 columns by 14 rows of 16×16 cells from ASCII 32**, and are **upscaled 2× with nearest sampling at load** to a 512×448 texture. A glyph is drawn as one textured quad: **`size` is the glyph height in pixels and the width is 0.6 × size** (`HORIZONTAL_SIZE`), the texture cell is inset by a margin (`TEX_STRETCH` = 1 − 26 × 0.003) and narrowed to 90%; text width is simply `characters × size × 0.6`. Options are a drop shadow and an outline, both off by default and turned on around individual calls. Sizes in use: 12 for titles and tooltips, 13 for menu items and the overlay's panel titles. There is no kerning, no proportional spacing and no scaling other than the quad size — the text is crisp only where `size` is a multiple of the atlas's 16-pixel cell, which is the case `AGENTS.md` §5 makes about the authored resolution.

---

## 5. The overlay

`Species/TaskManagerInterfaceIcons.cpp`, the interface drawn without windows: the task manager, research and objectives screens, the status messages, the tooltip and the bottom bar. Its two structural ideas are worth more than its content.

**A virtual screen.** `SetupRenderMatrices` (`:283`) sets an orthographic projection **600 units high and 600 × aspect wide** — 800 on a 4:3 display, 960 at 16:10, 1,067 at 16:9 — so every layout number in the file is in that space and the real resolution never appears. The task manager, research and objectives screens are stacked vertically in it and scrolled by `m_screenY` (task manager at 0, research above, objectives below), and the mouse is converted into the same space (`ConvertMousePosition`).

**Screen zones.** A `ScreenZone` (`TaskManagerInterface.h:15`) is a named rectangle in virtual space with a tooltip, a data value, and flags for pad scrolling and sub-icons; the overlay rebuilds its zone list every frame as it draws, and hit-testing is a search of that list. Keyboard shortcuts map controls onto the same names. It is an immediate-mode interface: the drawing code is the layout.

**What it draws**, in order (`:350`): target areas in the world; messages; if visible, the title bar, the screen for the current scroll position, the **bottom bar** — `InterfaceDivider.bmp` repeated 100× across the width in a 26-unit strip at the bottom — the tooltip and the zones; if hidden, the running tasks. **Panels** use the same gradient title (199, 214, 220) → (112, 141, 168) as windows, the same yellow (255, 255, 150, 30) title in the game font at 13, over `InterfaceRed.bmp` at (0.8, 0.8, 0.8, 0.8). **Icons** are the 128×128 `Icons/Icon*.bmp` drawn as **additive** quads (alpha 1.0, or 0.3 when unavailable) over an `IconShadow.bmp` quad 10 units larger; the task row is a column at x = 40 from y = 130, icons **50 units** with 10-unit gaps (70 and x = 80 in the larger mode), every value eased toward its target at 10 per second so the row animates open. The **tooltip** is a typewriter: the current zone's text revealed at **50 characters per second** at (20, 588) in the game font at 12, yellow twice with shadow.

---

## 6. The cursor

`Species/GameCursor.cpp`. Windows' own cursor is hidden; the game draws its own as **two-dimensional quads at the mouse position**: eight `MouseCursor` objects, each a main texture and a `shadow_`-prefixed twin, drawn shadow first (subtractive: src-alpha, one-minus-src-colour) then main (additive, in the cursor's colour), at a size in pixels:

| Cursor | Size | When |
|---|---|---|
| Main pointer | 25 | default |
| Placement | 40; pulses 20 → −20 over a placement; 60 over a valid target | placing a program or unit |
| Disabled | 60 | the action is not allowed |
| Move here | 30 | ordering a move |
| Highlight | 30 | over a selectable object |
| Selection | radius × 100 / √(camera distance) | scales with the selected object's apparent size |
| Turret target | 200 | aiming a turret |
| Missile target | | aiming a missile |

plus a **selection arrow** (`SelectionArrow.bmp` with its shadow) over the selected unit and **markers** placed in the world where an order was given, with a start time so they animate and fade.

**NOT ALL OF THEM ARE TWO-DIMENSIONAL, which this section said until 2026-09-19.** `MouseCursor` has a second entry point, `Render3D` (`GameCursor.cpp:1129`), and the cursors the game spends most of its time drawing go through it: the same four-vertex quad, placed at the point where the cursor's ray meets the landscape and **tilted to lie against the ground**, its up vector the terrain's interpolated normal. The plain ground disc, the placement ring, move-here and the fading order markers are all `Render3D`; the arrow, the selection ring and the turret target are the two-dimensional ones. `SpeciesLook.md` §7.1 has the mechanism, because the owner asked for this cursor in Frontier Commander and `Design/Interface.md` §4 now specifies it.

---

## 7. What this means for Frontier Commander

**Carry the model and the rules whole.** Windows that own buttons, identity by name, front-of-list is front-of-screen, left press focuses and raises, right button is the game's, release-on-button-under-cursor, the 4-pixel resize margin and the 60 × 40 minimum, the 1,000 ms tooltip, `Char` returning whether it consumed, keyboard navigation through a button order with activate and close, auto-size on registration. `TechnicalDesign.md` §6.4 already asks for a toolkit "in the Eclipse shape"; §2 above is that shape written down.

**Carry the chrome as a palette table.** Every RGBA value in §3, the two-pass yellow with shadow, the gradient title bar, the red gradient panel, the 2-pixel border and 1-pixel outer loop, the 16-pixel title height — one JSON file under `GameData\`, and the look of a window is data.

**Carry the two-font split**, a title face and a caption face, at 12 and 13 pixels, drawn 1:1. The atlas format (16 × 14 cells from ASCII 32, 0.6 width ratio) is simple enough to keep as the font file's format; the glyphs are the Spectrum font's (owner, 2026-09-17).

**Carry the overlay's ideas, not its coordinate system.** Screen zones rebuilt each frame with a tooltip and a shortcut each, a typewriter tooltip, animated icon rows, and panels that share the window chrome, all belong in the HUD. The 600-unit virtual screen does not: `AGENTS.md` §5 authors the whole frame at one resolution and scales it at present, which is the same idea applied once to everything instead of once to the overlay, and it makes the overlay's layout pixels like the windows'.

**Fix, do not carry**: the empty duplicate-name branch, the random placement of child windows, and the string lookups on every event (a handle is the C++23 answer, and `AGENTS.md` R2 names the type `Window`, not `EclWindow`).
