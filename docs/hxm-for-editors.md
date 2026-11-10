# Hyperion maps for map editors

This is for the author of a map editor who wants it to open, save and test Hyperion's
maps. It says:

- what Hyperion is and which game files it reads;
- what a Hyperion map is;
- how to support it, step by step;
- what Hyperion does with a map when a game starts;
- what is stable now and what is planned;
- how to test a map.

Two documents sit beside this file:

- `hxm-format.md`, the field-by-field reference;
- `live-channel-for-editors.md`, for testing a map in a Hyperion that is already running.

This covers Hyperion v0.7.3-beta (2026-11-10) and format version 1. What v0.7.2-beta does not
have says "from v0.7.3-beta".

## What Hyperion is

Hyperion is a 64-bit engine for StarCraft: Brood War, for Windows, Mac (Apple silicon) and
Linux (x64 and ARM64).

It plays the game's maps as the original does, to the frame:

- `.scm` and `.scx`;
- the maps StarEdit, ScmDraft 2 and Remastered save;
- the campaign's maps.

For maps past the original's limits it has a format of its own, `.hxm`. An `.hxm` map can be
bigger, hold more players, give each player a colour in full RGB, place units a mod adds, and
later have the features listed under "Planned".

Hyperion holds none of the game's files. It reads them from the player's own install, which
is one of two kinds:

- **StarCraft: Remastered, from Battle.net** (free). Hyperion reads its CASC storage, the same
  data StarDraft reads.
- **A classic install's MPQs:** `StarDat.mpq`, `BrooDat.mpq`, `patch_rt.mpq`, and the CDs'
  `StarCraft.mpq` and `BroodWar.mpq`, with `Local.dll`.

Hyperion runs from a folder of its own and only reads the game's folders. It never writes
into them.

## Where maps go

Hyperion's map list shows the maps in these folders, and `--map` finds a map in them by
name:

- the game's own `Maps` folder, whether the files are Battle.net's StarCraft or a classic
  install. A map an editor saves there needs nothing else.
- Hyperion's data folder, `Data\maps\` (`Data/maps/` on Mac and Linux). Hyperion's own maps
  are in `Data\maps\Hyperion\`.
- up to eight more folders, given with `--maps <folder>`.

Hyperion compiles a `.hxm` into the game's own form when it is played, and keeps the result
in `Data\cache\hxm\`. The cache is keyed by the map's path, the compiler's revision and the
data of the mod loaded, and redone whenever the file is newer. So an editor should write the
file afresh when it saves, giving it a new modification time; a save that keeps the old time
plays the old map.

## The format in short

A `.hxm` file is a zip of:

    map.json              the map as one JSON document
    layers/<name>.bin     the per-tile data, binary, little-endian
    files/<path>          the map's other files (its sounds), by their path in the map

The same tree in a folder is the unpacked form. Hyperion reads the zip; the folder is for
editing by hand and version control.

`map.json` uses names for the game's numbers (`terran_marine`, `jungle`, `at_least`).
Wherever it takes a name it also takes the number, so nothing outside the lists is lost.

- Text is plain UTF-8 JSON text.
- The game's colour codes are the control characters they always were.
- Strings are written where they are used: there is no string table to keep.

Every CHK converts to the format and back. An editor that already models a CHK can treat an
`.hxm` as that model in another form:

| CHK | `.hxm` |
|---|---|
| `DIM`, `ERA`, `VER` (`expansion`) | `size`, `tileset`, `expansion` |
| `SPRP` | `name`, `description` |
| `OWNR`, `SIDE`, `COLR`, `CRGB`, `IOWN` | `players` (`slot`, `race`, `color`, `color_mode`, `rgb`, `editor_slot`) |
| `FORC` | `forces`, and each player's `force` |
| `UNIT` | `units` |
| `THG2` | `doodads` |
| `MRGN` | `locations` |
| `TRIG`, `MBRF` | `triggers`, `briefing` |
| `UPRP`, `UPUS` | `unit_properties` |
| `UNIx`, `UPGx`, `TECx` (and `UNIS`, `UPGS`, `TECS`) | `settings` |
| `PUNI`, `PUPx`, `PTEx` (and `UPGR`, `PTEC`) | `availability` |
| `WAV `, `SWNM` | `sounds`, `switches` |
| `MTXM`, `TILE`, `ISOM`, `MASK`, `DD2 ` | the layers `tiles`, `editor_tiles`, `isom`, `fog`, `editor_doodads` |
| `STR `, `STRx` | the text in place |
| `TYPE`, `IVER`, `IVE2`, `VCOD`, odd section sizes | `compat` |
| any other section | `sections`, kept as they were |

**The CHK's `VER`.** A map past the original's limits (more than 256 tiles a side, players
past 7 who take part, triggers for slots past 11, or a mod's units) compiles to a CHK whose
`VER` is 1206, or 1064 when it does not need Brood War. No Blizzard version uses them, so map
sites and tools tell such a map from a classic one by `VER` alone, whatever the file is
called. An editor that writes a Hyperion map as a CHK, in an `.scx` or alone, writes the same
`VER`. A map within the limits keeps the original's (59, 63, 64, 205 or 206).

Hyperion reads 1206 as Remastered's 206 and 1064 as 64, so such a CHK has the sections those
versions have: `MRGN` with 255 locations, and `STR ` or `STRx`. The original refuses a `VER`
it does not know, so it never plays such a map wrongly. What a Hyperion map adds
is in three sections of its own, each with a version number first, and Hyperion refuses a
version it does not read (`hxm-format.md`, "Hyperion's sections in a compiled map"):

| Section | Holds |
|---|---|
| `HXPL` | the kind, race, force and colour of every slot from 8 on |
| `HXRG` | the RGB colours of the slots from 8 on |
| `HXTE` | the slots past 11 each trigger runs for |

## Supporting it, step by step

Each step is useful on its own.

### 1. Open and save `.hxm` within the original's limits

This step covers a map of up to 256 tiles a side and 8 players. It plays in Hyperion as the
same map saved as `.scx` would, so the step is about the file, not the game.

- Read `map.json`, the layers it lists under `layers`, and the files under `files`.
- Write only what differs from the defaults in `hxm-format.md`. Every field has one.
- When the map was converted from a CHK, keep `compat` and `sections` as they came, so it
  converts back to the same bytes. A new map has neither.
- Keep the `isom` layer. Hyperion does not read it, but an editor needs it to paint terrain,
  as with a CHK's ISOM. It has the CHK section's layout at the map's size.

### 2. Larger maps

Hyperion plays maps up to 1,024 tiles a side, wide or tall. **The target for now is 512.**
For a larger map only `size` and the layers' lengths change:

- **Width and height are chosen apart**, as in the original, and each is any whole number of
  tiles from 1 up. An editor that offers the original's sizes (64, 96, 128, 192, 256) can add
  384 and 512 to the same list and pair any width with any height: 512 by 256, 128 by 512,
  384 by 192. Maps of 512 by 256, 256 by 512, 384 by 128 and 128 by 512 were played to check
  this (2026-11-09).
- **Tiles.** `tiles` holds width times height u16 values, rows top to bottom. A 512 by 512
  map has 262,144 tiles, which is why the layers are binary.
- **Positions** are pixels from the top left, as in a CHK, up to the map's size times 32.
- **Units.**
  - A game on a classic map holds the original's 1,700 units, unless the player raises the
    limit in Game Options.
  - A game on a Hyperion map sizes its unit table for the units the map places and for its
    players, up to 262,144.
  - The map's preplaced units count.
- **Pathing.** Hyperion builds the map's pathing regions when the game starts, as the
  original does.
  - On a map past 256 tiles the regions are coarser.
  - A map whose terrain has too many separate pieces is refused with the original's "too
    many obstructions" message.
  - Only a game shows this: "Testing", below, has a quick way to start one.

### 3. More players

Up to 32 players. The engine plays 64; 32 is the target for now. The rules:

- **The slot numbers.**
  - Slots 0 to 7 are the original lobby's slots, and 8 to 10 the three no lobby offers.
  - Slot 11 is neutral, which the document keeps apart in `neutral`.
  - The new slots are 12 and up, to 64. So 64 players are slots 0 to 10 and 12 to 64.
  - The `players` list is in slot order without slot 11, so **entry 11 of the list is
    slot 12**, entry 12 is slot 13, and so on.
  - A unit's or doodad's `owner` is the slot number itself (0 to 10, then 12 and up), or
    `"neutral"`.
- **What makes it a Hyperion map:** a player from list entry 8 on whose `slot` is `human` or
  `computer`. Such a map has no form the original plays, and its CHK carries Hyperion's `VER`
  (above). A map without one is a classic map.
- **Saying it outright: `"limits": "classic"`.** With it the map is never a Hyperion map:
  slots 8 to 10 are owners no lobby seats whatever their `slot` says, and Hyperion refuses
  the map if it is past the original's limits in some other way (more than 256 tiles a side,
  a slot past 10, a mod's unit). Write it for every map meant to be saved as `.scm` or
  `.scx`. Some classic maps call players 9 to 11 computers; converted to `.hxm` they carry
  this field, which keeps them classic. Left out, the map's content decides, as above.
  In the compiled map the mark is the `VER`, which Hyperion writes from this.
- **Slots no one plays.** Hyperion keeps the original's and adds none of its own.
  - Slot 11, the original's player 12, is the neutral player: mineral fields, geysers,
    critters and whatever else a map gives `"neutral"`. No player ever takes it, on any map.
  - Slots 8 to 10 are the original's players 9 to 11: owners a map can give units to, whom no
    lobby seats. On a classic map they are exactly that.
  - On a Hyperion map every slot from 8 on (8 to 10, then 12 and up) is what its `slot` says:
    a player when it is `human` or `computer`, and otherwise an owner no one plays
    (`rescuable`, `neutral`, `unused`), as in the original. A map that keeps such owners has
    that many fewer players: with 64 players every slot but neutral's is a player's.
  - These owners are for Use Map Settings, as in the original. In such a game their units
    stay and no one is seated in them (checked with slots 9, 10 and 12, 2026-11-09).
  - In a melee game, of the slots from 8 on only the seats take a player: the slots that are
    `human` or `computer` and have a start location. The others keep their place and no one
    is put in them (from v0.7.3-beta; v0.7.2-beta could seat a player in one, who was
    eliminated as the game started).
  - Slots 0 to 7 keep the original's melee rule, which is looser: its start shuffle may seat
    a player in a slot that is `rescuable`, `unused` or `neutral` too, start location or not.
    So on a map meant for melee, give a slot from 0 to 7 that no one plays `inactive`, the
    default.
- **Humans and computers.**
  - In a skirmish, the person playing takes the first human slot, and every other open slot
    gets a computer of the race the map gives it.
  - A room on the local network seats up to 8 people for now. Seats for more people come
    with Hyperion's online play.
- **Forces:** four, as in the original. Each player's `force` is 0 to 3.
- **Colours.**
  - A player's `color` takes the original's and Remastered's names and numbers (0 to 22),
    and from 23 on Hyperion's own (`hxm-format.md`, Player).
  - `"rgb": [r, g, b]` gives a player any colour. Slots 0 to 7 need
    `"color_mode": "rgb"` with it, as Remastered maps have it. The slots past 7 take it on
    its own.
  - Hyperion draws the colour itself. Its 8-bit drawing takes the nearest palette ramp.
- **Triggers.**
  - A trigger names any slot by its number, the new ones too, and the groups
    (`all_players`, `foes`, `allies`, `force_1` to `force_4`, and the rest in
    `hxm-format.md`). The groups reach every slot.
  - A trigger can run for a slot past 11, a condition or action can name one, and Give Units
    can give to one.
  - The leaderboards, mission objectives, and the Most and Least conditions count every slot.
  - Text, sounds and the view still act only for the people playing, in slots 0 to 7.
- **Start locations:** one `start_location` unit for each slot a human or a computer can
  take, owned by that slot, as in the original.

### 4. Units a mod adds

A Hyperion mod can add unit types after the game's own: thousands of units, with their
weapons, sprites, images, orders and techs. It can also change the game's own units.
`mod-data.md`, beside this file in the hand-off, is the mod format.

**Placing a mod's unit.** A map places such a unit by the name the mod gives it, in a unit's
`type`, as it places the game's own units:

    {"type": "hybrid_reaver", "x": 1024, "y": 960, "owner": 0}

**When the map loads.** Only a game that has the mod's data loads it: a game started with
`--mod <id>`, or with the mod chosen in Hyperion's Mods menu. Without the mod, the map is
refused with "unknown unit hybrid_reaver".

**Names and numbers.** The game's own units keep their names, and a number still works for
any unit. A mod's unit has no fixed number, so use its name: the mod's data gives it one from
256 up when it loads (a game holds up to 4,096 unit types), and another mod, or a later
version of the same one, may give it another.

**Where a mod's name is read.** In a unit's `type`, a doodad's `unit` and a trigger's `unit`.
The map's `settings` and `availability` reach the game's own 228 units, 61 upgrades and 44
techs only, as the CHK sections they become do. A mod's unit named there is left out without
a message, and a mod's upgrade or tech is refused as unknown. A mod sets its own units' costs
and stats in its data.

**What a mod changes.** Mods can change the game's own units, so in a mod's game a unit's
stats may not be the original's. A mod can also change how the eight tilesets are drawn and
played: their colours, their tiles' pictures, and where units walk and build (from
v0.7.3-beta). A map still names one of the eight; with such a mod its terrain looks and plays as
the mod has it. An editor that shows a mod's map as the game does needs the mod's `tilesets`
(`mod-data.md`).

**A mod's races.** A mod can bring playable races. A player picks one when the game starts
(`--race <the race's name>`); a map's slot takes the original's races only, for now.

**For now, the map does not name its mod.** An editor that offers a mod's units has to be
told which mod the map is for (see "Planned").

### 5. Testing in Hyperion

Hyperion's command line has three things for an editor. Pass full paths. On Mac and Linux the
program is `Hyperion.app/Contents/MacOS/Hyperion` or `./Hyperion.x86_64`
(`./Hyperion.aarch64` on ARM), with the same options.

**The check.**

    Hyperion.exe --hxm-check <map.hxm> [<report.txt>]

It reads the map as a game would and reports one line for each problem: the errors that stop
the map playing, and the warnings an editor should show. It checks for:

- a format version newer than the build;
- a size outside 1 to 1,024;
- a map whose `limits` are `classic` that is past the original's limits;
- a unit outside the map, or owned by a slot the map does not have;
- a link to no unit;
- a slot open to a human or a computer with no start location;
- more preplaced units than a game holds;
- a location defined twice;
- a trigger naming a location the map does not have;
- a short tiles layer;
- a size or player count past the targets.

The last line is `plays: ...` or `refused: ...`, with the counts.

- The report goes to the file named, or to standard output without one.
- The exit code is 0 when the map plays, 1 when it is refused, and 2 when the file cannot be
  read.
- The check does not start the game, needs no game files, and takes well under a second.
- It knows the game's own units only, so it reports a mod's unit as unknown. Test a map with
  a mod's units with `--mod`, below.

**Test play.**

    Hyperion.exe --map <map.hxm> [--race zerg|terran|protoss|random] [--computers <n>]
    Hyperion.exe --map <map.hxm> --ums
    Hyperion.exe --map <map.hxm> --rules classic|hyperion
    Hyperion.exe --map <map.hxm> --teams <t,t,...> [--starts <s,s,...>]
    Hyperion.exe --map <map.hxm> --mode <id>
    Hyperion.exe --mod <id> --map <map.hxm>

- **`--map`** starts a melee game on the map at once, the player against computers. By
  default there is a computer in every other start, as Play Custom seats them, and on a map
  with players past 7 one in every open slot past 7. `--computers` takes 1 to 63.
- **`--ums`** starts it as Use Map Settings, so the map's triggers run. The player takes the
  first `human` slot and every `computer` slot gets a computer.
- **`--rules`** chooses the rule set (below).
- **`--teams`** puts the players in teams, and `--starts` gives them start locations.
- **`--mode`** plays a game mode a mod brings (from v0.7.3-beta). A mode reads the map's
  start locations and its clusters of mineral fields and geysers, and nothing else of the
  map's own, so a map needs no field for it.
- **`--mod`** plays with a mod's data.
- `.scm` and `.scx` work the same way.
- After the game, Hyperion shows its menus.
- When a map cannot be played, Hyperion shows its menus at once, and `hyperion_hxm.log` in
  the `Data` folder says why.

**A quick run with no one watching.**

    Hyperion.exe --map <map.hxm> --record=600,<out.rep>

This plays the game by itself to frame 600 (about 25 seconds of game time at the fastest
speed), writes its replay and exits. It is the way to see that the pathing regions build and
the game starts.

**Live testing.** With `Hyperion.exe --control` running, an editor can switch the game to a
map without restarting Hyperion:

- `map_load` loads the saved file, any `.scm`, `.scx` or `.hxm`.
- `map_push` sends just the terrain, up to 256 tiles a side.

Each switch goes out and comes in with a fade, and can play a scripted cutscene on the map.

- `live-channel-for-editors.md` has the messages.
- `http://127.0.0.1:27117/guide`, while Hyperion runs that way, shows and tries each of
  them on the game, with the code for it. `live-guide.html` beside this file is the same, to
  read offline.
- Hyperion's packages include a client for C++ in one header (`Data\tools\hxlive_cpp\hxlive.h`)
  and one for Python and the command line (`Data\tools\hxlive.py`).

## The rules a game plays by

A game plays by a rule set, and its replay and saved game record which:

- **Classic:** the original's, exactly.
- **Hyperion's:** the original's with fixes, among them Remastered's order fixes and
  selections of up to 255 units.

Which set a game uses:

- **New games default to Hyperion's.** A player can choose Classic in a skirmish's Game
  Options, or for one game with `--rules classic`.
- **Some games always play by Hyperion's:**
  - a game on a map past the original's limits;
  - a game with more than 8 players;
  - a game with a mod's data;
  - a game with teams, handicaps, mutators, the computer levels or a game mode.

Nothing in a map chooses the set yet (see "Planned").

## Game options that are not in the map

The player sets these when a game starts, so a map does not carry them and an editor needs
no field for them:

- the computers' levels (Very Easy to Elite, and three Cheater levels);
- handicaps;
- mutators: starting workers, supply, fog of war, build time, tech and upgrades, and rich
  resources.

A game by Hyperion's rules holds up to 262,144 units at once, whatever the map; from v0.7.3-beta
no setting asks for it.

Rich resources choose bases from the map's own mineral fields and geysers when the game
starts. A map needs nothing for them.

## Terrain

- `tiles` holds the CHK's MTXM values: a tile group times 16 plus the tile's index in it,
  from the tileset's CV5. `isom` is the editor's isometric layer.
- **Remastered's newer tiles and doodads** (the ones added in Remastered 1.22) play when
  Hyperion reads a Remastered install. A classic install's tilesets do not have them, so a
  map that uses them needs a Remastered install. StarDraft reads Remastered's data, so its
  maps that use them play on a Remastered install.

## EUD maps and text

**EUD maps.** Triggers that read and write the game's memory play as they do in
StarCraft: Remastered. These are Deaths and Set Deaths on a player or unit past the game's
own, as euddraft and EUD Editor make them.

- Hyperion serves every address that Remastered serves.
- An address Hyperion does not serve stops the map's triggers, as in Remastered.
- A map whose triggers write memory cannot be saved during a game.
- EUD needs no field in `.hxm`: it is all in the triggers.
- Native features that do what EUD maps use memory for are planned (below).

**Text.** A map's text may be UTF-8, and a Korean map's may be CP949. Both are drawn.

## Versions and what stays stable

- **Version 1 has been frozen since 2026-10-13.** Every field in `hxm-format.md` keeps its
  meaning.
- **A field may be added in version 1** when leaving it out means what version 1 meant.
  Three things have been added since the freeze. The first two are values that were refused
  before, and the third is a field that changes nothing when left out, so no map changed
  meaning:
  - slots past 11 in triggers (2026-10-13);
  - a mod's unit by name in a unit's `type` (2026-10-14);
  - `limits` (2026-11-09, from v0.7.3-beta; v0.7.2-beta ignores it).
- **A change to what an existing field means raises the version.** Hyperion refuses a map of
  a version newer than it reads, and says so, rather than playing it without the parts it
  does not know. An editor should do the same.
- **Hyperion ignores fields it does not know.** An editor's own data, such as view settings
  or layers of its own, goes under `editor`, in an object named after the editor:
  `"editor": {"stardraft": {...}}`. Hyperion never reads or writes it.
- **Hyperion does not write `.hxm` files.** Only editors and tools do.

## Planned, not in version 1

None of these are in the format yet, and what they will look like is not fixed. Each note
says what the feature will do, so an editor can plan for it. Each will arrive as new fields
with defaults or as a new version, as above, and this document will say so.

- **The mod a map is made for.** The map will name its mod (and the mods that mod builds on),
  so an editor knows which units to offer and Hyperion loads the mod with the map.
- **A mod's race in a slot.** Mods bring playable races now, which a player picks as the game
  starts. A map will be able to give a slot such a race.
- **A tileset of a mod's own.** A map names one of the eight tilesets by number; a ninth
  needs a field for its name.
- **A mod's units in `settings` and `availability`**, which reach the game's own for now.
- **The rule set in the map.** A map will be able to say which rule set it is made for.
  Later versions of the game's balance will be rule sets too.
- **Campaigns.** A campaign will be a set of maps in order, carried by a mod, with what a
  mission carries into the next: surviving units, resources, research and choices. A map will
  be able to say what it takes in and what it passes on.
- **Scenes in the map.** The cutscenes the live channel plays, kept in a map and started by
  its triggers, in the same step format.
- **A data fingerprint.** A map may record a hash of the game data it was made against, so a
  game on other data can say so.
- **More people in a room**, past 8, with Hyperion's online play.
- **Maps past 1,024 tiles a side.** Paused for now.

## Example maps

Hyperion's packages carry these, in `Data\maps\Hyperion\`. They are remakes of the game's
own maps at Hyperion's sizes, and each passes `--hxm-check`:

| Map | Tiles a side | Players |
|---|---|---|
| Big Game Hunters 2x | 256 | 16 |
| Big Game Hunters 2x (24) | 256 | 24 |
| Big Game Hunters 3x3 | 384 | 32 |
| Big Game Hunters XL | 512 | 16 |
| Big Game Hunters XL 1800 | 512 | 16, about 1,800 units placed |
| Elderlands 2x | 384 | 16 |
| Garden of Aiur 2x | 384 | 16 |
| Experimental: Big Game Hunters 4x4 | 512 | 64 |
| Experimental: Big Game Hunters 8x8 | 1,024 | 64 |

- The two Experimental maps are past the targets, and the check says so.
- All but the 24-player map carry an `isom` layer, so an editor can paint their terrain.
- `tools\hxmap` in Hyperion's source converts any map to `.hxm` and back:
  `python -m tools.hxmap convert <map> <out.hxm>`.

## Questions

Questions go to Hyperion's developer, who adds what an editor needs.
