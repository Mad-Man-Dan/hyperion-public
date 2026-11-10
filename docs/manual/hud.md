# The HUD

## Lede

The HUD is the console you play from: the minimap, the selection, the portrait and the command
card. Besides the original's, Hyperion has two designs of its own, Mech and R9, drawn at your
screen's full resolution, and it plays designs you make yourself. You choose one in Settings,
on the HUD tab.

## How to use it

### Choosing a design

1. Open Settings and choose the **HUD** tab.
2. Pick a design: **Classic**, **Mech**, **R9**, or one of your own (listed after these).
3. Set its options. A live picture over the pane shows the design as it will look on your
   screen, at your screen's shape, over a map, and it changes with each pick before you accept.
4. Accept. The next game plays with it.

### The designs

| Design | What it is | Its options |
|---|---|---|
| Classic | 1.17's own console, by race, as it always was | Console layout and Console skin, Settings' own rows for the original console |
| Mech | dark metal plates with lit wells | Layout: **Full width** (across the foot of the screen), **Left column** (a column down the left), **Right column**; Size, with Full width: **Full** or **Minimized**; Material: **Gunmetal**, **Graphite** or **Blackout**; Width; Resources (right of centre or right edge); Resources backing (plate or none) |
| R9 | a Terran console of plates and glass | Width |

With a column layout (Left column, Right column), the battlefield is drawn beside the column, not
under it: the map's edge meets the column's.

### Making it yours

Each design keeps its own picks:

- **Light**: the design's light in a colour you choose, or the style's own.
- **Selection colour**: the selection boxes, the frames of one unit's armour, weapon and upgrade
  icons, its queue and its cargo, and the tooltip's light: the original's blue, the design's
  light, or a colour you choose.
- **Buttons**: leave out the ones you don't use: Actions per minute, Idle workers, Army,
  Control groups and Terrain. The menu, diplomacy and messaging always show.
- **The design's sounds**: on or off.
- **The design's tooltips**: the design's own, or the original's bubbles.

### Wide screens

- **Width**, **16:9**: on a screen wider than 16:9 the console stays the width of a 16:9 screen
  in the middle, so the minimap and the card stay where your hands know them, and plates fill
  the margins to the screen's edges. **Full screen** spreads it across.
- **Map raise**: 0 to 200, in pixels of a screen 1080 high, the same for every design. The map
  is drawn higher, so units along the foot of the screen come out from under a HUD that is tall
  at its sides; the view loses as much at its top, and the minimap's box shows what is left.
  Near the map's top edge the raise gives way so the top rows still come into sight. A column
  layout has no raise, and the row is greyed with the reason.

### The HUD's own buttons

- **Idle worker** (Shift+F11) selects the next idle worker; **Army** (Shift+F12) selects your
  army. Both keys can be changed in Settings, Hotkeys.
- **Control groups**: a press recalls a group. A right-press makes the group of your selection,
  and Shift with a right-press adds your selection to it, as Ctrl and Shift with the group's key
  do.
- **APM**: your actions per minute.

### Tooltips

A design's tooltip is drawn in its own materials and type: the button's name with its key
marked, the key, what it does or, for a greyed button, why, and its costs in the game's own
icons, with a cost you lack marked short. It sits over the button and shows at once, as the
original's does.

### Large selections

In a game by Hyperion's rules you select up to 255 units, and your own buildings select
together. A design shows two or more units as StarCraft II does: pages of 8 by 3 (5 by 3 in the
column layouts) with the page tabs at the left, every unit with its box in your selection colour,
the active subgroup's boxes lighter. Tab steps through the subgroups (Shift+Tab back) and the
page follows. A click on a unit selects it alone, Shift leaves it out, Ctrl takes its type.
[More players, more units](@bigger-games) has the rest.

## Limits and known issues

- Mech, R9 and your own designs need the GPU renderer (Vulkan or Direct3D 12). Under the SDL
  renderer, and in the browser build, the tab offers Classic alone and says why.
- A game by the classic rules selects 12 with any design: a replay of 1.17's, a LAN room's match
  by the classic rules, or a game on an EUD map.
- A replay plays with any design, or none, the same: the HUD is presentation only.
- Known: at a skirmish's start, where the HUD slides in, the map can show mirrored in the HUD's
  area for a moment (open).

## In progress and planned

- A design in StarCraft: Remastered's style: parked while Remastered's HD art is held.
- Designs in the browser build and under the SDL renderer: parked.
- HUD buttons for a mod's own actions: parked.
- The costs' icons drawn from upscaled art in a later round.

## Technical notes

- A design is a pair of files in Hyperion's HUD format, version 3: a layout (panels, layers,
  slots, bindings, timelines, options) and a style (materials, palette, looks, lights, sounds,
  the tooltip and the music player's skin). The format: `engine/docs/hud-format.md`.
- The built-in designs are in `engine/huds/` (`mech-span.json`, `mech-span-min.json`,
  `mech-flank.json`, `mech-flank-right.json`, `terran-r9.json`, and the styles in `styles/`).
  They are procedural and hold no game art; `tools/hud/import_builtins.py` takes them from the
  design center.
- 1.17's console stays underneath: its dialogs draw into their own bitmaps, the design's slots
  show those pixels (minimap, selection, portrait, card) and pass a press to the dialog under
  it, so every press ends in 1.17's own handler and sends the command 1.17's console sends.
- Drawn by the GPU presenter at the window's resolution (`engine/src117/port/render/port_gpu_hud.cpp`),
  the design played by `port_hudplay.cpp` and read by `port_hudfile.cpp`; the tab is
  `port_front_hud.cpp`.
- The map raise and the columns move the camera's sight (`CameraUpdate`, `port_view.cpp`), and
  every reader of the view follows: clicks, drags, the minimap's box, double clicks and the
  wheel's anchor. The game's own view, which commands and sounds read, does not move.
- Settings: the `hud` section of `settings.json` (`design`, `layout`, `size`, `material`,
  `<design>.light`, `<design>.selection`, `<design>.option.<id>`, `<design>.hide.<slot>`,
  `sound`, `tooltips`, `mapRaise`).
- Hyperion's selection is a rule of new games (`HX_OPT_SELECTION`): selections and groups of up
  to 255 sent in chunks, buildings together, subgroups by rank (`port_selwide.cpp`).
- The module doc: `engine/docs/modules/hud.md`.
