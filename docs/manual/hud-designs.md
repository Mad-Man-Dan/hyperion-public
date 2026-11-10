# Making a HUD

## Lede

A HUD design is a pair of JSON files: a layout, which says where the panels go and what they
show, and a style, which says what they are made of. Put a design's folder, or a `.hud` package,
in the `huds` folder of Hyperion's Data folder and it is listed on Settings' HUD tab after the
built-in designs. While a game plays it, saving one of its files shows the change within a
second.

## How to use it

### A design's files

A design is a folder in `huds\` in the Data folder:

| File | What it is |
|---|---|
| `hud.json` | the layout (required): panels, layers, the game's slots, bindings, timelines, options |
| `style.json` | the style it wears (optional): materials, palette, looks, lights, sounds, the tooltip, the music player's skin |
| `parts\*.json` | parts by name, pieces a layout uses more than once (optional) |
| `files\` | your own images (PNG, a sheet of frames, or WebM video) and sounds (Ogg) (optional) |

The tab lists it by the layout's `name`. A design without `hud.json` is not listed.

### What a layout holds

- **Panels**, anchored to the screen's edges, the band (the 16:9 middle on a wider screen) or
  each other, in units of a screen 1080 high.
- **Layers**: shapes drawn by Hyperion's shader (plates, wells, glass over the map, relief and
  patterns), text, images, and **slots**, where the game's own parts show: the minimap, the
  selection, the portrait, the command card, the resources, the control groups, the idle workers,
  the army, APM, the menu, diplomacy, messaging and terrain.
- **Buttons**: any layer but a slot can have an `action`, so you draw your own button out of
  shapes and text and it does what the game's button does (the menu, the idle worker, a group).
- **Bindings**: text and values from the game in braces, such as `{idle.count}`.
- **Timelines** that play on the game's events (a unit selected, an alert, a button pressed),
  lighting and animating layers. Hyperion's own designs use light, never shaking.
- **Options** the player picks on the HUD tab, each a value your file reads.

### What a style holds

The materials and palette the layers' looks name, the lights, the sounds for the HUD's moments,
the tooltip's look and type, and the `musicPlayer` block, which skins the music player while
your design plays.

### Trying it while you write it

1. Choose your design on the HUD tab and start a game (a skirmish, or a replay).
2. Edit and save any of its files. The game reads the design again within a second and shows
   it, with your option picks, light and colours over it. A design that fails to load is read
   again too, so your fix shows as soon as you save it.
3. Each load writes what it found to `hyperion_hud.log` in the Data folder: the files read,
   `plays` or `refused`, then each error and note.

To play a design straight from where you work on it, without choosing it in Settings, start
Hyperion with `HYPERION_HUD=<path to hud.json>[;<path to style.json>]` set.

### Checking a design

    Hyperion.exe --hud-check <hud.json | folder | package.hud> [--style <style.json>]
                 [--pick <option>=<value>]... [--light #rrggbb]

checks a design without starting a game. It prints JSON with `plays`, `errors` and `warnings`,
and exits 0 when the design plays, 1 when it has errors, 2 when a file does not read.

### Sharing a design

A `.hud` file is a zip of the folder's contents (`hud.json`, `style.json`, `parts\`, `files\`),
zipped from inside the folder. Put it in `huds\`; Hyperion unpacks it as it lists it, and again
whenever the package is newer. A package is data, nothing in it runs, so a shared HUD is as safe
to load as a shared map.

## Limits and known issues

- **No game art and no fonts in a design.** A design holds your own work. The game's art is
  named, not copied (`sd:` and `hd:` references read the player's own install), and a font file
  is refused.
- A package: at most 512 entries and 64 MB unpacked. A zip that holds the folder rather than its
  contents is left out, so zip from inside the folder.
- Hyperion reads one style for a design, `style.json`; the design center's packages may carry a
  style per race, of which Hyperion takes the one named `style.json`.
- Designs need the GPU renderer.
- A missing sound plays as silence and a missing image draws nothing, each with a note in the
  log; neither stops the design.

## In progress and planned

- A slot dressing in the style (the labels', APM's and clock's type), provisional until decided.
- HUD buttons for a mod's own actions (parked).

## Technical notes

- The format is Hyperion's copy of the design center's HUD format, version 3, with Hyperion's
  additions marked (Parts, Buttons, animated images, packages as the engine reads them, the
  checker, live reload): `engine/docs/hud-format.md`. A file the design center's player checks
  means the same in Hyperion.
- Read and checked by `engine/src117/port/hud/port_hudfile.cpp` (parts laid out by
  `ExpandParts` before the checks), watched and reloaded by `port_hud.cpp` (`Watch`, `Changed`),
  packages unpacked into `huds\.unpacked\<name>\` (`Unpack`), the checker `port_hudcheck.cpp`.
- Drawn by the GPU presenter (`port_gpu_hud.cpp`); shapes by the HUD's shape shader.
- The fonts the format names: `eurostile` and `blizzard` are drawn in Saira, `eurostile-ext` in
  Michroma, `chakra-petch` in its four weights; all OFL, built into Hyperion.
- The built-in designs, as examples to start from: `engine/huds/` (layouts) and
  `engine/huds/styles/` (styles), installed into `Data\huds\` by the package.
