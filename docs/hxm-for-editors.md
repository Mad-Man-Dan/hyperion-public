# Hyperion maps for map editors

This guide is for the developer of a map editor who wants the editor to open, save and test
Hyperion's maps. It assumes you know StarCraft's map format (the CHK inside an `.scm` or
`.scx`) and describes Hyperion's format as a set of changes to it: for each area, how
StarCraft does it, what Hyperion changed, and what an editor has to write.

Two reference documents come with it:

- `hxm-format.md`: every field of the format, with its type and default.
- `live-channel-for-editors.md`: the protocol for loading a map into a Hyperion that is
  already running.

This guide describes Hyperion v0.7.3-beta (2026-11-10) and format version 1. Where
v0.7.2-beta behaves differently, the text says "from v0.7.3-beta".

## Terms

- **CHK**: the `scenario.chk` file inside an `.scm` or `.scx`, made of four-letter sections
  (`DIM`, `OWNR`, `UNIT`, `TRIG` and so on).
- **Player index**: a player's number as the CHK stores it, counted from 0. Index 0 is the
  player StarEdit calls Player 1, index 7 is Player 8, and index 11 is Player 12, the neutral
  player. This guide and the format use the index, and give StarEdit's name beside it where
  that helps.
- **Controller**: who controls a player, which the CHK stores in `OWNR` (human, computer,
  rescuable, neutral and so on). In `map.json` this is a player's `slot` field.
- **Classic map**: a map within StarCraft's limits, which can be saved as `.scm` or `.scx`
  and which StarCraft itself plays.
- **Hyperion map**: a map past those limits. Only Hyperion plays it.

## What Hyperion is

Hyperion is a 64-bit engine for StarCraft: Brood War. It runs on Windows, Mac (Apple silicon)
and Linux (x64 and ARM64).

It plays StarCraft's maps exactly as the original game does, frame for frame:

- `.scm` and `.scx` files;
- maps saved by StarEdit, ScmDraft 2 and StarCraft: Remastered;
- the campaign maps.

For maps past StarCraft's limits it has its own format, `.hxm`. An `.hxm` map can be larger,
have more players, give each player any RGB colour, and place units that a mod adds.

Hyperion ships none of StarCraft's files. It reads them from the player's own install, which
is one of two kinds:

- **StarCraft: Remastered from Battle.net** (free). Hyperion reads its CASC storage, the same
  data StarDraft reads.
- **A classic install's MPQ files:** `StarDat.mpq`, `BrooDat.mpq`, `patch_rt.mpq`, the CDs'
  `StarCraft.mpq` and `BroodWar.mpq`, and `Local.dll`.

Hyperion runs from its own folder. It reads the game's folders and never writes to them.

## Where Hyperion looks for maps

The in-game map list shows the maps in these folders, and the `--map` option finds a map in
them by file name:

- StarCraft's own `Maps` folder, in either kind of install. A map your editor saves there
  needs no other set-up.
- Hyperion's data folder, `Data\maps\` (`Data/maps/` on Mac and Linux). The maps that come
  with Hyperion are in `Data\maps\Hyperion\`.
- up to eight more folders, each given with `--maps <folder>`.

**Hyperion compiles an `.hxm` to a CHK before it plays it**, and caches the result in
`Data\cache\hxm\`. The cache entry is keyed by the map's path, the compiler's revision and
the loaded mod's data, and it is rebuilt when the `.hxm` file's modification time is newer.
So when your editor saves, it must write the file with a new modification time. A save that
preserves the old time makes Hyperion play the cached, older map.

## The file format in short

An `.hxm` file is a zip archive containing:

    map.json              the whole map except per-tile data, as one JSON document
    layers/<name>.bin     per-tile data, binary, little-endian
    files/<path>          the map's other files (its sounds), at their path in the map

The same tree as a plain folder is the unpacked form. Hyperion plays the zip; the folder
form is for hand editing and version control.

How `map.json` is written:

- It uses names for StarCraft's numeric ids (`terran_marine`, `jungle`, `at_least`). Every
  field that accepts a name also accepts the number, so an id with no name is never lost.
- Text is UTF-8. StarCraft's colour codes are the same control characters as in a CHK.
- There is no string table. Each string is written in the field that uses it, where a CHK
  would hold an index into `STR `.

Every CHK converts to `.hxm` and back without loss. If your editor already has a model of a
CHK, an `.hxm` is that model in another container:

| CHK section | `map.json` |
|---|---|
| `DIM`, `ERA`, `VER` | `size`, `tileset`, `expansion` |
| `SPRP` | `name`, `description` |
| `OWNR`, `SIDE`, `COLR`, `CRGB`, `IOWN` | `players`: each player's `slot`, `race`, `color`, `color_mode`, `rgb`, `editor_slot` |
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
| `STR `, `STRx` | the strings, written in place |
| `TYPE`, `IVER`, `IVE2`, `VCOD`, sections with unusual sizes | `compat` |
| any other section | `sections`, kept byte for byte |

## What Hyperion changes, at a glance

| | StarCraft | Hyperion |
|---|---|---|
| Map size | `DIM` up to 256 by 256 tiles | up to 1,024 by 1,024; 512 is the supported target for now |
| Players | 12 entries in `OWNR` and `SIDE`: Players 1 to 8 can be played, Players 9 to 11 can only own units, Player 12 is neutral | up to 64 players that can be played (32 is the supported target for now): indices 0 to 10 and 12 to 64. Index 11 stays neutral |
| Player colours | `COLR`, one palette colour for each of Players 1 to 8; Remastered adds `CRGB` | any RGB colour for any player |
| Triggers | run for Players 1 to 8 and the player groups | run for any player index, and can name any player |
| Unit types | 228 | a mod adds more, up to 4,096 in a game |
| Units alive at once | 1,700 | 262,144 in a game by Hyperion's rules |
| Forces | 4 | 4 |
| Locations | 64 (`VER` 59) or 255 | 255 |

### How a compiled Hyperion map is marked: `VER`

StarCraft identifies a CHK's format by `VER`: 59 for original StarCraft, 63 for a hybrid
map, 205 for Brood War, and 64 and 206 for the Remastered forms.

A classic map compiled from `.hxm` keeps one of those values.

A Hyperion map compiles to a CHK with a `VER` no Blizzard version uses:

- **1206**, or
- **1064** when the map does not need Brood War (`"expansion": false`).

A map is a Hyperion map when any of these is true:

- it is more than 256 tiles wide or high;
- a player at index 8 or above can be played (see "More players");
- a trigger runs for, or names, a player index above 11;
- it places a unit that a mod adds.

Because the value is unique, a map site or a tool can tell a Hyperion map from a classic one
by `VER` alone, whatever the file is called. If your editor writes a Hyperion map directly as
a CHK, alone or inside an `.scx`, write the same `VER`.

Hyperion reads `VER` 1206 with the same section layout as Remastered's 206, and 1064 as 64.
So such a CHK has `MRGN` with 255 locations and its strings in `STR ` or `STRx`. StarCraft
rejects a `VER` it does not know, so it can never load such a map and misread it.

The data that does not fit StarCraft's fixed-size sections goes in three extra sections.
Each starts with a version number, and Hyperion rejects a version it does not know. Their
byte layouts are in `hxm-format.md`, "Hyperion's sections in a compiled map".

| Section | Why it exists | Contents |
|---|---|---|
| `HXPL` | `OWNR`, `SIDE`, `FORC` and `COLR` have room for 12, 12, 8 and 8 players | the controller, race, force and colour of every player from index 8 up |
| `HXRG` | `CRGB` has room for 8 players | the RGB colour of each player from index 8 up |
| `HXTE` | a trigger's list of who it runs for has entries only for Players 1 to 12 and the groups | for each trigger, the player indices above 11 it runs for |

## Supporting the format, step by step

Each step is useful without the ones after it.

### 1. Open and save `.hxm` for a classic map

This covers a map of up to 256 by 256 tiles with Players 1 to 8. Such a map plays in Hyperion
exactly as the same map saved as `.scx`, so this step is only about reading and writing the
file.

- Read `map.json`, each layer it lists under `layers`, and each file it lists under `files`.
- When writing, you may omit any field that has its default value. Every field has a default
  (`hxm-format.md`).
- If the map was converted from a CHK, write `compat` and `sections` back unchanged. They
  hold what is needed to convert the map back to the same CHK bytes. A map created new has
  neither.
- Keep the `isom` layer. It is the CHK's `ISOM` section at the map's size. StarCraft and
  Hyperion ignore it, but an editor needs it to paint isometric terrain.
- Write `"limits": "classic"` for a map you intend to save as `.scm` or `.scx` as well (see
  "Declaring a map classic", below).

### 2. Larger maps

**StarCraft:** `DIM` holds the width and height in tiles. StarEdit offers 64, 96, 128, 192
and 256 for each, chosen independently. The engine cannot go past 256 in either direction.

**Hyperion:** the engine plays up to 1,024 tiles in each direction. **Maps up to 512 are the
supported target for now.** Nothing in the format changes for a larger map except the numbers
in `size` and the length of each layer.

- **Size.** Width and height are still independent, and each can be any whole number of
  tiles from 1 up. An editor that offers StarEdit's sizes can add 384 and 512 to the list and
  allow any pairing, for example 512 by 256, 128 by 512 or 384 by 192. Maps of 512 by 256,
  256 by 512, 384 by 128 and 128 by 512 were played to confirm this (2026-11-09).
- **Tiles.** The `tiles` layer is `MTXM`: width times height u16 values, row by row from the
  top. A 512 by 512 map has 262,144 tiles, which is why layers are binary files and not JSON.
- **Coordinates** are pixels from the top-left corner, as in `UNIT` and `MRGN`, up to the
  size in tiles times 32.
- **Unit count.** StarCraft holds 1,700 units at once, and the units a map preplaces count
  toward that. A game by Hyperion's rules holds 262,144, and a Hyperion map is always played
  by Hyperion's rules (see "Rule sets").
- **Pathfinding.** Like StarCraft, Hyperion divides the map into pathfinding regions when a
  game starts.
  - Above 256 tiles the regions are coarser.
  - If the terrain breaks into too many separate pieces, the map fails to start with
    StarCraft's "too many obstructions" error.
  - This can only be found by starting a game. "Testing" below has a quick way to do that.

### 3. More players

The supported target for now is 32 players. The engine plays 64.

#### Player indices

**StarCraft:** `OWNR` and `SIDE` have 12 entries.

| Index | StarEdit's name | Role |
|---|---|---|
| 0 to 7 | Players 1 to 8 | the players a lobby can seat |
| 8 to 10 | Players 9 to 11 | can own units and be named in triggers; no lobby ever seats them |
| 11 | Player 12 | the neutral player: mineral fields, geysers, critters |

**Hyperion:** indices 0 to 11 keep exactly those meanings on a classic map. A Hyperion map
adds player indices 12 to 64, and can turn indices 8 to 10 into real players. Index 11 is
never a player: it stays the neutral player, so every unit owner value that means "neutral"
in StarCraft still does.

That gives 64 players that can be played: indices 0 to 10 (11 players) and 12 to 64 (53
players).

In `map.json`:

- The neutral player is not in the `players` list. It is the separate field `neutral`, and a
  unit owned by it has `"owner": "neutral"`.
- The `players` list is in index order with index 11 left out. **So list entry 11 is player
  index 12**, entry 12 is index 13, and so on.
- A unit's or doodad's `owner` is the player index itself (0 to 10, or 12 and up), or
  `"neutral"`.

#### Controllers

A player's `slot` field is its `OWNR` value, by name:

| `slot` | `OWNR` | Meaning in StarCraft |
|---|---|---|
| `inactive` (default) | 0 | not in the game |
| `rescuable` | 3 | units wait to be rescued |
| `unused` | 4 | no controller |
| `computer` | 5 | a computer player |
| `human` | 6 | an open slot for a person |
| `neutral` | 7 | neutral units |
| `closed` | 8 | a closed slot |

`OWNR` values 1 and 2, which StarCraft uses only while a game runs, are `computer_game` and
`human_occupied`. A converted map that has them keeps them.

Hyperion does not add controller values. On a Hyperion map, a player at index 8 or above
whose `slot` is `human` or `computer` is a real player. Any other value means the same as in
StarCraft: a player that owns units and is never played.

If a map keeps some indices for unit owners of that kind, it has that many fewer players.
A map with all 64 players has every index except 11 as `human` or `computer`.

#### What makes a map a Hyperion map

For players, the rule is: **a player at list entry 8 or above whose `slot` is `human` or
`computer`.** Such a map cannot be written as a CHK that StarCraft plays, so its compiled CHK
gets Hyperion's `VER`. A map with no such player, and within the other limits, is a classic
map.

#### Declaring a map classic: `"limits": "classic"`

Some existing StarCraft maps give Players 9 to 11 a `computer` controller in `OWNR`, even
though StarCraft never seats them. By the rule above those would be taken for Hyperion maps.

`"limits": "classic"` at the top level of `map.json` settles it:

- Indices 8 to 10 are unit owners that no lobby seats, whatever their `slot` says, as in
  StarCraft.
- The compiled CHK keeps a classic `VER`.
- Hyperion rejects the map if it exceeds StarCraft's limits in another way: more than 256
  tiles in either direction, a player index above 10, or a mod's unit.

Write it for every map meant to be saved as `.scm` or `.scx`. Hyperion's converter adds it
when it converts such a CHK to `.hxm`. Without the field, the map's contents decide, by the
rule above. (From v0.7.3-beta; v0.7.2-beta ignores the field.)

#### Start locations, melee and Use Map Settings

**StarCraft:** a Start Location unit (unit 214) owned by a player marks where that player
starts. In Use Map Settings, `OWNR` decides who plays. In melee, StarCraft shuffles the
players of the game into Players 1 to 8, and the shuffle is loose about `OWNR`: it can seat
a player in one whose controller is `rescuable`, `unused` or `neutral` as well as `human` or
`computer`, whether or not it owns a Start Location.

**Hyperion:**

- Place one `start_location` unit for each player that a person or a computer can take,
  owned by that player. This is unchanged.
- **Use Map Settings** works as in StarCraft for every index. Players that are not `human`
  or `computer` keep their preplaced units and nobody is seated in them. This was checked
  with indices 9, 10 and 12 (2026-11-09).
- **Melee, indices 0 to 7:** StarCraft's rule is kept unchanged, so classic maps play as
  they always have. On a map meant for melee, set any of Players 1 to 8 that should not be
  played to `inactive`, which is the default.
- **Melee, index 8 and above:** a player is seated only where `slot` is `human` or `computer`
  and the player owns a Start Location. Other indices keep their units and are never seated.
  (From v0.7.3-beta. In v0.7.2-beta the shuffle could seat a player in an index with no Start
  Location, and that player was eliminated as the game started.)

#### People and computers

- In a single-player skirmish, the person takes the first `human` player and every other
  open player gets a computer of the race the map gives it.
- A LAN room seats up to 8 people for now, in Players 1 to 8. More than 8 people in one game
  comes with Hyperion's online play.

#### Forces

**StarCraft:** `FORC` assigns each of Players 1 to 8 to one of 4 forces.

**Hyperion:** still 4 forces. Every player, at any index, has a `force` from 0 to 3.

#### Colours

**StarCraft:** `COLR` gives each of Players 1 to 8 a palette colour. Remastered adds `CRGB`,
which can give each of them an RGB colour instead.

**Hyperion:**

- `color` accepts StarCraft's and Remastered's colour names and numbers (0 to 22), and
  Hyperion's own from 23 up (`hxm-format.md`, "Player").
- `"rgb": [r, g, b]` gives a player any colour. For indices 0 to 7 also set
  `"color_mode": "rgb"`, which is how `CRGB` marks an RGB colour. For index 8 and above `rgb`
  alone is enough.
- Hyperion's full-colour renderer draws the exact colour. Its 8-bit palette renderer uses the
  nearest of StarCraft's colour ramps.

#### Triggers

**StarCraft:** a trigger has a list of the players and groups it runs for, and its conditions
and actions name a player or a group by number: 0 to 11 for Players 1 to 12, then the groups
(Current Player, Foes, Allies, All Players, Force 1 to 4 and the rest).

**Hyperion:**

- A trigger can run for a player index above 11, up to 63.
- A condition or an action can name any player index. Give Units to Player can give to one.
- The groups (`all_players`, `foes`, `allies`, `force_1` to `force_4`, and the others listed
  in `hxm-format.md`) include every player index.
- Leaderboards, mission objectives, and the Most and Least conditions count every player.
- Actions that address one person's screen and speakers (Display Text, Play WAV, Center View
  and similar) still act only for people, who are always in Players 1 to 8.

In `map.json` a player is written as its index in all of these places. How the compiled CHK
stores an index above 11 is in `hxm-format.md`.

### 4. Units that a mod adds

**StarCraft:** a unit's type is an index into `units.dat`, 0 to 227. `UNIT` stores it as a
u16.

**Hyperion:** a mod can add unit types after StarCraft's 228, each with its own weapons,
sprites, images, orders and techs, and it can change StarCraft's own units. A game holds up
to 4,096 unit types. `mod-data.md` describes the mod format.

**Placing a mod's unit.** Write the name the mod gives the unit in the unit's `type`, as you
would for one of StarCraft's units:

    {"type": "hybrid_reaver", "x": 1024, "y": 960, "owner": 0}

**Use the name, never a number.** StarCraft's units keep their names and their numbers. A
mod's unit has no fixed number: the mod's data assigns it one from 256 up when it loads, and
another mod, or a later version of the same mod, may assign a different one. Hyperion
resolves the name to the number when it compiles the map, which is why the compile cache is
keyed by the mod's data.

**Loading.** The map loads only in a game that has the mod's data: one started with
`--mod <id>`, or with the mod selected in Hyperion's Mods menu. Without the mod, Hyperion
rejects the map with "unknown unit hybrid_reaver".

**Where a mod's unit name is accepted:** a unit's `type`, a doodad's `unit`, and a trigger's
`unit`.

**Where it is not:** the map's `settings` and `availability`. They compile to the CHK
sections `UNIx`, `UPGx`, `TECx`, `PUNI`, `PUPx` and `PTEx`, which have fixed room for
StarCraft's 228 units, 61 upgrades and 44 techs. A mod's unit named there is dropped without
a message, and a mod's upgrade or tech is rejected as unknown. A mod sets the costs and stats
of its own units in its own data.

**What else a mod can change.**

- StarCraft's own units. In a game with a mod, a unit's stats may differ from StarCraft's.
- The eight tilesets: their palette, their tile graphics, and which tiles units can walk and
  build on (from v0.7.3-beta). A map still names one of the eight tilesets in `tileset`. With
  such a mod its terrain looks and plays as the mod defines. An editor that wants to show the
  map as the game will draw it needs the mod's `tilesets` data (`mod-data.md`).
- Races. A mod can add playable races. A player picks one when the game starts
  (`--race <name>`). A map's `race` field accepts only StarCraft's races for now.

**The map does not record which mod it needs, for now.** An editor that offers a mod's units
has to be told which mod the map is for (see "Planned").

### 5. Testing in Hyperion

Hyperion's command line gives an editor three tools. Pass full paths. On Mac the program is
`Hyperion.app/Contents/MacOS/Hyperion` and on Linux `./Hyperion.x86_64` (`./Hyperion.aarch64`
on ARM); the options are the same.

#### Validate a map without starting the game

    Hyperion.exe --hxm-check <map.hxm> [<report.txt>]

This reads the map exactly as a game would and writes one line for each problem: errors,
which stop the map from playing, and warnings, which an editor should show its user. It
checks for:

- a format version newer than this build reads;
- a width or height outside 1 to 1,024;
- `"limits": "classic"` on a map that exceeds StarCraft's limits;
- a unit outside the map, or owned by a player index the map does not have;
- a `link` to a unit that does not exist;
- a `human` or `computer` player with no Start Location;
- more preplaced units than a game holds;
- a location defined twice;
- a trigger that names a location the map does not have;
- a `tiles` layer shorter than width times height;
- a size or a player count above the supported targets (a warning).

The last line is `plays: ...` or `refused: ...`, with counts.

- The report goes to the named file, or to standard output if none is given.
- The exit code is 0 if the map plays, 1 if it is refused, and 2 if the file cannot be read.
- The check needs no StarCraft files and takes well under a second.
- It knows only StarCraft's own units, so it reports a mod's unit as unknown. To test a map
  that places a mod's units, start a game with `--mod` as below.

#### Start a game on the map

    Hyperion.exe --map <map.hxm> [--race zerg|terran|protoss|random] [--computers <n>]
    Hyperion.exe --map <map.hxm> --ums
    Hyperion.exe --map <map.hxm> --rules classic|hyperion
    Hyperion.exe --map <map.hxm> --teams <t,t,...> [--starts <s,s,...>]
    Hyperion.exe --map <map.hxm> --mode <id>
    Hyperion.exe --mod <id> --map <map.hxm>

- **`--map`** starts a melee game immediately, the user against computers. By default each
  of the remaining Start Locations gets a computer, as StarCraft's Play Custom screen fills
  them, and on a map with players at index 8 and above, so does every open one of those.
  `--computers` sets the number, 1 to 63.
- **`--ums`** starts the map as Use Map Settings, so its triggers run. The user takes the
  first `human` player and every `computer` player gets a computer.
- **`--rules`** selects the rule set (see "Rule sets").
- **`--teams`** assigns teams, and `--starts` assigns Start Locations.
- **`--mode`** plays a game mode that a mod provides (from v0.7.3-beta). A mode uses only the
  map's Start Locations and its groups of mineral fields and geysers, so a map needs no
  extra field to support one.
- **`--mod`** loads a mod's data for the game.
- `.scm` and `.scx` files work with all of these.
- When the game ends, Hyperion goes to its menus.
- If the map cannot be played, Hyperion goes straight to its menus and writes the reason to
  `hyperion_hxm.log` in its `Data` folder.

#### Run a game unattended

    Hyperion.exe --map <map.hxm> --record=600,<out.rep>

This plays the game with no input until frame 600 (about 25 seconds of game time at Fastest),
writes the replay and exits. It is the quickest way to confirm that the pathfinding regions
build and the game starts.

#### Reload the map in a running game

Start Hyperion with `--control`. An editor can then switch the running game to a map without
restarting:

- `map_load` loads a saved `.scm`, `.scx` or `.hxm` file.
- `map_push` sends only the terrain, for maps up to 256 by 256 tiles.

Each switch fades out and in, and can play a scripted cutscene on the map.

- `live-channel-for-editors.md` documents the messages.
- While Hyperion runs with `--control`, `http://127.0.0.1:27117/guide` shows each message,
  runs it against the game, and gives sample code. `live-guide.html` is the same page for
  offline reading.
- Hyperion's packages include two clients: a single-header C++ one
  (`Data\tools\hxlive_cpp\hxlive.h`) and a Python one that is also a command-line tool
  (`Data\tools\hxlive.py`).

## Rule sets

Every game is played by one of two rule sets. A replay and a saved game record which one.

- **Classic:** StarCraft's rules exactly, frame for frame.
- **Hyperion:** StarCraft's rules with fixes, including Remastered's order fixes and
  selections of up to 255 units, and room for 262,144 units at once (from v0.7.3-beta no
  setting is needed for that).

How the rule set is chosen:

- A new game uses Hyperion's rules by default. A player can choose Classic in a skirmish's
  Game Options, or with `--rules classic`.
- These games always use Hyperion's rules:
  - a game on a Hyperion map;
  - a game with more than 8 players;
  - a game with a mod's data;
  - a game with teams, handicaps, mutators, computer difficulty levels or a game mode.

A map cannot select the rule set yet (see "Planned").

## Game options that a map does not store

The player chooses these when starting a game. A map has no field for them and an editor
needs no user interface for them:

- computer difficulty (Very Easy to Elite, and three Cheater levels);
- handicaps;
- mutators: starting workers, supply, fog of war, build time, tech and upgrades, and rich
  resources. Rich resources picks bases from the map's own mineral fields and geysers when
  the game starts, so a map needs nothing for it.

## Terrain

- The `tiles` layer holds `MTXM` values: the tile group's index in the tileset's CV5 times
  16, plus the tile's index within the group.
- The `isom` layer is `ISOM`, the editor's isometric data.
- **Remastered's added tiles and doodads** (from Remastered 1.22) play when Hyperion reads a
  Remastered install. A classic install's tilesets do not contain them, so a map that uses
  them needs a Remastered install. StarDraft reads Remastered's data, so a StarDraft map that
  uses them plays on a Remastered install.

## EUD maps and text encoding

**EUD maps.** Triggers that read and write game memory, through Deaths and Set Deaths with a
player or unit number outside the normal range, behave as they do in StarCraft: Remastered.
Maps made with euddraft and EUD Editor rely on this.

- Hyperion emulates every address that Remastered emulates.
- An access to an address Hyperion does not emulate stops the map's triggers, as in
  Remastered.
- A game on a map whose triggers write memory cannot be saved.
- EUD needs no field in `.hxm`. It is entirely in the triggers.
- Native features to replace common uses of EUD are planned (below).

**Text encoding.** A map's strings may be UTF-8, and a Korean map's may be CP949. Hyperion
draws both.

## Format versions and stability

- **Version 1 has been frozen since 2026-10-13.** Every field in `hxm-format.md` keeps its
  meaning.
- **A field can be added within version 1** only if omitting it means what version 1 already
  meant. Three additions have been made since the freeze, and none changed the meaning of an
  existing map:
  - player indices above 11 in triggers (2026-10-13), which were rejected before;
  - a mod's unit name in a unit's `type` (2026-10-14), which was rejected before;
  - `limits` (2026-11-09, from v0.7.3-beta), which changes nothing when omitted.
- **Changing what an existing field means raises the version number.** Hyperion rejects a map
  with a version newer than it reads, with a message, and does not try to play the parts it
  understands. An editor should do the same.
- **Hyperion ignores fields it does not know.** Put an editor's own data (view settings,
  extra layers) under `editor`, in an object named after the editor:
  `"editor": {"stardraft": {...}}`. Hyperion never reads or writes it.
- **Hyperion never writes `.hxm` files.** Only editors and tools do.

## Planned, not in version 1

None of these is in the format yet, and their final form is not decided. Each entry says
what the feature will do so an editor can plan for it. Each will arrive either as new fields
with defaults or as a new version, as described above, and this guide will be updated.

- **The mod a map needs.** The map will name its mod, and the mods that mod depends on, so
  an editor knows which units to offer and Hyperion loads the mod with the map.
- **A mod's race for a player.** A map will be able to give a player a race that a mod adds.
- **A mod's own tileset.** `tileset` names one of StarCraft's eight by number. A ninth needs
  a field for its name.
- **A mod's units in `settings` and `availability`.**
- **The rule set.** A map will be able to say which rule set it is made for. Later balance
  versions will be rule sets too.
- **Campaigns.** A campaign will be an ordered set of maps carried by a mod, with what one
  mission passes to the next: surviving units, resources, research and choices. A map will
  be able to declare what it receives and what it passes on.
- **Cutscenes in the map.** The cutscenes the live channel can play, stored in the map and
  started by its triggers, in the same step format.
- **A data fingerprint.** A map may record a hash of the game data it was made with, so a
  game with different data can warn about it.
- **More than 8 people in one game**, with Hyperion's online play.
- **Maps above 1,024 tiles.** On hold.

## Example maps

Hyperion's packages include these in `Data\maps\Hyperion\`. They are remakes of StarCraft's
maps at Hyperion's sizes, and each passes `--hxm-check`.

| Map | Tiles (width and height) | Players |
|---|---|---|
| Big Game Hunters 2x | 256 | 16 |
| Big Game Hunters 2x (24) | 256 | 24 |
| Big Game Hunters 3x3 | 384 | 32 |
| Big Game Hunters XL | 512 | 16 |
| Big Game Hunters XL 1800 | 512 | 16, with about 1,800 preplaced units |
| Elderlands 2x | 384 | 16 |
| Garden of Aiur 2x | 384 | 16 |
| Experimental: Big Game Hunters 4x4 | 512 | 64 |
| Experimental: Big Game Hunters 8x8 | 1,024 | 64 |

- The two Experimental maps are above the supported targets, and `--hxm-check` warns about
  it.
- Every map except the 24-player one has an `isom` layer, so an editor can paint its
  terrain.
- `tools\hxmap` in Hyperion's source converts any map to `.hxm` and back:
  `python -m tools.hxmap convert <map> <out.hxm>`.

## Questions

Send questions and requests to Hyperion's developer. If an editor needs something the format
or the tools do not provide, it can be added.
