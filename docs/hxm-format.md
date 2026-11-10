# The Hyperion map format (.hxm)

This is the field-by-field reference. `hxm-for-editors.md` is the guide: it explains each
change from StarCraft, how to support the format step by step, and what is planned.

A Hyperion map is a small tree of files: one JSON document, `map.json`, which holds
everything about the map except its per-tile data, and binary layers that hold the per-tile
data. It carries the same information as StarCraft's CHK (the `scenario.chk` inside an
`.scm` or `.scx`), as named fields in place of four-letter sections, so it can be written by
hand, by an editor or by a generator as easily as it is read.

Every StarCraft map converts to it and back: `.scm` and `.scx` files, maps saved by
StarCraft: Remastered and ScmDraft 2, the campaign maps, and the map inside a replay.

The format's own limits are only those of its number types: a side can be up to 65,535 tiles
and `players` is a list of any length. A CHK stops at 256 tiles a side and 12 players. The
engine decides what it plays (`engine/docs/maps.md`): 1,024 tiles a side and 64 players, with
512 and 32 the supported target for now.

`tools/hxmap` reads and writes the format and converts:

    python -m tools.hxmap convert "(8)Big Game Hunters.scm" bgh.hxm
    python -m tools.hxmap convert bgh.hxm bgh/            # unpacked, to edit
    python -m tools.hxmap convert bgh/ bgh.scx            # back to a classic map
    python -m tools.hxmap info bgh.hxm
    python -m tools.hxmap roundtrip <maps or folders>     # check the conversion

## Terms

- **Player index:** a player's number as the CHK stores it, counted from 0. Index 0 is the
  player StarEdit calls Player 1, and index 11 is Player 12, the neutral player.
- **Controller:** who controls a player, the CHK's `OWNR` value. In `map.json` it is a
  player's `slot` field.
- **Classic map:** a map within StarCraft's limits, which StarCraft itself plays.
- **Hyperion map:** a map past those limits, which only Hyperion plays.
- **Compiled map:** the CHK that Hyperion builds from an `.hxm` to play it.

## The package

    map.json              the document
    layers/<name>.bin     each layer map.json lists
    files/<path>          the map's other files (its sounds), at their path in the map,
                          with slashes for backslashes

An `.hxm` file is a zip of that tree (deflated or stored). The same tree in a folder is the
unpacked form, for editing and version control. Both read the same.

## Conventions

- Names are lower case with underscores (`terran_marine`, `jungle`, `at_least`). Every field
  that accepts a name for one of StarCraft's numeric ids also accepts the number, so an id
  with no name is never lost. `tools/hxmap/names.py` has every list of names; the engine has
  the same lists (`engine/src117/port/data/port_hxm_names.h`).
- Coordinates are pixels from the map's top-left corner. A tile is 32 by 32 pixels.
- Text is plain JSON text. StarCraft's colour and formatting codes are the same control
  characters as in a CHK (`\u0003` and so on).
- A field left out takes its default, given below. The converter writes only fields that
  differ from their default.

## Players

### Player indices

| Index | In StarCraft | In `map.json` |
|---|---|---|
| 0 to 7 | Players 1 to 8, the players a lobby can seat | entries 0 to 7 of `players` |
| 8 to 10 | Players 9 to 11: they can own units, and no lobby seats them | entries 8 to 10 of `players`, written only when the map uses them |
| 11 | Player 12, the neutral player, which owns resources and critters | not in `players`: the `neutral` field, and the name `"neutral"` wherever a player is named |
| 12 to 64 | do not exist | entries 11 to 63 of `players`, on a Hyperion map only |

So `players` is in index order with index 11 left out: entry 11 is index 12, entry 12 is
index 13, and so on. A unit's or doodad's `owner` is the index itself (0 to 10, or 12 and
up) or `"neutral"`. Triggers name a player by its index too.

### Classic maps and Hyperion maps

A map is a Hyperion map when a player at list entry 8 or above has the controller `human` or
`computer`. (The other conditions are a side above 256 tiles, a trigger for a player index
above 11, and a unit that a mod adds.)

- On a classic map, indices 8 to 10 are what they are in StarCraft: owners a map can give
  units to, whom no lobby seats.
- On a Hyperion map, a player at index 8 or above is a real player when its `slot` is
  `human` or `computer`. With any other controller it is an owner that nobody plays, as in
  StarCraft.
- Index 11 is the neutral player on every map.

The compiled map stores the players from index 8 up in `HXPL` and their RGB colours in
`HXRG` (see "Hyperion's sections in a compiled map").

**`"limits": "classic"`** declares that a map is a classic map whatever the controllers of
indices 8 to 10 are. Some StarCraft maps give Players 9 to 11 a `human` or `computer`
controller, which no lobby seats. Such a map converts to `.hxm` with this field, so it
converts back to the CHK it was. Added 2026-11-09 within version 1: a map without the field
means what it did before.

### Who is seated in a game

- **Use Map Settings:** as in StarCraft, for every index. A player whose controller is not
  `human` or `computer` keeps its preplaced units and nobody is seated in it.
- **Melee, indices 0 to 7:** StarCraft's rule, unchanged. Its start shuffle can seat a
  player in an index whose controller is `rescuable`, `unused` or `neutral` as well as
  `human` or `computer`. On a map meant for melee, give an index that nobody should play the
  controller `inactive`.
- **Melee, index 8 and above:** a player is seated only where the controller is `human` or
  `computer` and the player owns a Start Location.

### Player groups

Triggers name groups of players with these names:

| Name | Who |
|---|---|
| `current_player` | the player the trigger is running for |
| `foes`, `allies` | the current player's enemies, allies |
| `neutral_players` | players neutral to the current player |
| `all_players` | every player |
| `force_1` .. `force_4` | the players of a force |
| `non_allied_victory_players` | players not allied for victory |
| `neutral` | the neutral player |
| `unused_12`, `unused_22` .. `unused_25`, `group_N` | player codes StarCraft does not use; kept as they are |

On a Hyperion map the groups include the players at index 12 and above.

## The document

| Field | Type | Default | Meaning |
|---|---|---|---|
| `format` | `"hyperion-map"` | required | |
| `version` | 1 | required | the format version (see "Versions") |
| `name`, `description` | text | none | the map's title and description (`SPRP`) |
| `tileset` | name or 0-7 | required | `badlands`, `platform`, `installation`, `ashworld`, `jungle`, `desert`, `ice`, `twilight` (`ERA`) |
| `size` | `[width, height]` in tiles | required | (`DIM`) |
| `expansion` | bool | `true` | the map needs Brood War |
| `limits` | `classic` | none | `classic`: the map is a classic map. Indices 8 to 10 are owners no lobby seats whatever their `slot`, and Hyperion rejects the map if it exceeds StarCraft's limits in another way. Left out, the map's contents decide ("Classic maps and Hyperion maps", above) |
| `text_encoding` | `utf-8` or `cp1252` | `utf-8` | how strings are written into a compiled classic map: `cp1252` writes each string in StarCraft's Windows-1252 code page when it can, as the classic editors did |
| `players` | list of player | | the players, in index order without index 11 |
| `neutral` | player | inactive | the neutral player's record (index 11) |
| `forces` | list of force | | up to 4 |
| `units` | list of unit | `[]` | preplaced units (`UNIT`) |
| `doodads` | list of doodad | `[]` | the sprites and unit doodads the map places (`THG2`) |
| `locations` | list of location | `[]` | (`MRGN`) |
| `triggers` | list of trigger | `[]` | (`TRIG`) |
| `briefing` | list of trigger | none | the mission briefing's triggers (`MBRF`) |
| `unit_properties` | list | `[]` | the property sets that Create Units with Properties uses (`UPRP`) |
| `settings` | object | none | unit, upgrade and tech costs and stats |
| `availability` | object | none | what each player may build and research |
| `sounds` | object | none | the map's sound list: sound number to the sound's path in the map (`WAV `) |
| `switches` | object | none | switch number (from 1) to its name (`SWNM`) |
| `layers` | object | | written by the tools: each layer's file and element type |
| `files` | list | | written by the tools: the paths of the map's other files |
| `sections` | list | none | CHK sections of a converted map that this format has no field for, as `{"tag", "data"}` (base64). They are written back unchanged |
| `editor` | object | none | `tiles_same_as_game`: the editor's tile layer equals the game's. An editor's own data goes in an object under its name (`"stardraft": {...}`), which Hyperion never reads |
| `compat` | object | none | what a converted map needs to convert back to the same CHK: its `chk_version`, `type`, `iver`, `ive2`, `vcod` (`"standard"` or base64), `strx`, `string_count`, and the byte counts of sections that had an unusual size |

### Player

| Field | Default | Meaning |
|---|---|---|
| `slot` | `inactive` | the controller (`OWNR`): `human` (an open slot for a person), `computer`, `rescuable`, `neutral`, `closed`, `unused`, `inactive`, and the two values StarCraft uses only while a game runs, `human_occupied` and `computer_game` |
| `race` | `zerg` | (`SIDE`) `zerg`, `terran`, `protoss`, `user_select`, `random`, `independent`, `neutral`, `inactive` |
| `force` | 0 | the force's index, 0 to 3 |
| `color` | the index's own | a colour name or number. The names are StarCraft's (`red`, `blue`, `teal`, `purple`, `orange`, `brown`, `white`, `yellow`, `green`, `pale_yellow`, `tan`, `azure`), Remastered's eleven more, and from 23 up Hyperion's own, made for the players above index 7 (`tools/hxmap/names.py`). Indices 0 to 7 accept StarCraft's and Remastered's. Above index 7 any of them is accepted, and a player at index 12 or above with no `color` gets the next of Hyperion's |
| `color_mode`, `rgb` | none | Remastered's colour choice (`CRGB`): `random`, `player_choice`, `rgb` with `rgb: [r, g, b]`, or a predefined colour's number |
| `editor_slot` | `slot` | the controller the editor shows (`IOWN`), when it differs |

### Force

`{"name": text, "flags": [...]}` with flags from `random_start`, `allies`,
`allied_victory`, `shared_vision`.

### Unit

| Field | Default | Meaning |
|---|---|---|
| `type` | required | a unit's name or number, or the name of a unit that a mod adds ("Units that a mod adds", below) |
| `x`, `y` | required | the unit's centre, in pixels |
| `owner` | 0 | a player index or `"neutral"` |
| `id` | its position in the list | the unit's number, which `link` refers to |
| `link`, `relation` | 0 | a Nydus Canal's exit or an add-on's building: `link` is the other unit's `id`; `relation` is 0x200 for a Nydus link and 0x400 for an add-on |
| `hp`, `shields`, `energy` | 100 | percentages |
| `resources` | 0 | a mineral field's or geyser's amount |
| `hangar` | 0 | |
| `state` | 0 | bits: 1 cloaked, 2 burrowed, 4 lifted, 8 hallucinated, 16 invincible |
| `valid_state`, `valid_props` | 0x1F, 0x3F | which of the state bits and which properties the map sets |
| `unused` | 0 | |

A `start_location` unit owned by a player marks where that player starts.

#### Units that a mod adds

A unit's `type`, a doodad's `unit` and a trigger's `unit` can be the name a mod gives one of
its units, for example `"hybrid_reaver"`.

- **Loading.** Such a map loads only in a game that has the mod's data (`--mod <id>`, or the
  mod selected in the Mods menu). Without it, Hyperion rejects the map as naming an unknown
  unit.
- **Why a name.** A mod's unit has no fixed number. The mod's data assigns it one from 256
  up when it loads.
- **In the compiled map.** `UNIT`'s 16-bit unit type holds the number the loaded mod's data
  assigned. The compiled map is cached for that mod's data only, so a game with other data
  compiles the map again.
- **`settings` and `availability`** cover StarCraft's 228 units, 61 upgrades and 44 techs
  only. A mod's unit named there is dropped, and a mod's upgrade or tech is rejected.
- Added 2026-10-14 within version 1. Such names were rejected before, so no existing map
  changed meaning.
- Recording which mod a map needs is planned (`hxm-for-editors.md`).

### Doodad

`{"sprite": n, "x", "y", "owner"}` for a sprite, or `{"unit": name, "x", "y", "owner"}`
for a unit placed as a doodad, with optional `flags` (bit 0x8000 marks a disabled doodad)
and `pad`.

### Location

`{"id": n, "name": text, "left", "top", "right", "bottom", "elevation"}`. Ids count from
1. A StarCraft map has 64 locations (`VER` 59) or 255, and location 64 is "Anywhere".
`elevation` is the set of elevation levels the location excludes, as bits.

### Trigger

| Field | Meaning |
|---|---|
| `players` | the players and groups the trigger runs for |
| `conditions` | up to 16 |
| `actions` | up to 64 |
| `flags` | the CHK's trigger flags (bit 2 is disabled, among others), when not 0 |
| `player_bytes`, `current_action` | raw values of a converted map, when unusual |

A condition is `{"type": name, ...}` with any of:

- `location` (a location id);
- `player` (a player index or a group);
- `amount`;
- `unit`;
- `comparison` (`at_least`, `at_most`, `exactly`);
- `resource` (the resource, the score kind or the switch number, depending on the type);
- `flags`, `mask`.

An action is `{"type": name, ...}` with any of:

- `location`;
- `text`;
- `wav` (a sound's path);
- `time` (milliseconds);
- `player`;
- `number` (a second location, an amount, a state, or for `give_units_to_player` the player
  who receives the units);
- `unit`;
- `modifier` (a count, or how to change a value);
- `flags`, `pad`, `mask`.

The type names are StarCraft's, in `names.py`: `CONDITIONS`, `ACTIONS`, and
`BRIEFING_ACTIONS` for the briefing. A text field that pointed at no string in a converted
map is kept as `{"string_id": n}`.

**Player indices above 11.** On a Hyperion map, a trigger's `players`, a condition's or
action's `player`, and `give_units_to_player`'s `number` can be a player index of 12 or
more, written as the index, as for indices 0 to 10. A trigger's `players` accepts indices up
to 63. Briefings run only for indices 0 to 7. Added 2026-10-13 within version 1: such values
were rejected before, so no existing map changed meaning.

In the compiled map:

- a condition or an action stores an index above 11 as the player code 0x100 + index. No
  StarCraft map contains such a code, and only a Hyperion game reads it;
- the indices above 11 that each trigger runs for are in the section `HXTE` (below), because
  a `TRIG` entry's own list has room only for Players 1 to 12 and the groups.

### Unit properties

`[{"slot": 1-64, "valid_state", "valid_props", "owner", "hp", "shields", "energy",
"resources", "hangar", "state", "used": true}]`, each field 0 when left out. Here `slot` is
the property set's number in `UPRP`.

### Settings

- `settings.units` maps a unit to `{"hp", "shields", "armor", "build_time", "minerals",
  "gas", "name"}`.
- `settings.weapons` maps a weapon number to `[damage, bonus]`. The whole weapon table is
  written whenever any unit is listed.
- `settings.upgrades` maps an upgrade to `{"minerals", "minerals_factor", "gas",
  "gas_factor", "time", "time_factor"}`.
- `settings.techs` maps a tech to `{"minerals", "gas", "time", "energy"}`.

Only what the map overrides is listed. Everything else keeps the game's values. These are
the Brood War sections `UNIx`, `UPGx` and `TECx`. The forms `units_classic`,
`upgrades_classic` and `techs_classic` are the sections a map for original StarCraft
carries: `UNIS`, `UPGS` and `TECS`.

### Availability

- `availability.units`: `global` (unit to 0 or 1, listing only those that are not 1), and
  for each player `available` and `use_global`.
- `availability.upgrades`: `global_max`, `global_start`, and for each player `max`, `start`
  and `use_global`.
- `availability.techs`: `global_available`, `global_researched`, and for each player
  `available`, `researched` and `use_global`.

Values left out are the game's defaults: everything available, upgrade levels 0, nothing
researched, and every player using the global settings. `upgrades_classic` and
`techs_classic` are original StarCraft's `UPGR` and `PTEC`.

## Layers

| Layer | Element | Meaning |
|---|---|---|
| `tiles` | u16 per tile, row by row from the top | the terrain (`MTXM`): the tile group's index in the tileset's CV5 times 16, plus the tile's index within the group |
| `fog` | u8 per tile | the set of players who start with the tile unexplored, as bits (`MASK`) |
| `isom` | u16 | the editor's isometric terrain (`ISOM`), as StarEdit and ScmDraft keep it. The game does not read it |
| `editor_tiles` | u16 per tile | the editor's own copy of the terrain (`TILE`), when it differs from `tiles` |
| `editor_doodads` | bytes | the editor's doodad list (`DD2 `) |

All layers are little-endian. They are binary because a 512 by 512 map has 262,144 tiles. A
generator writes one with a few lines of code (`tools/hxmap` uses Python's `struct`).

## Versions

- Version 1 has been frozen since 2026-10-13: every field above keeps its meaning.
- A field can be added to version 1 only if omitting it means what version 1 already meant.
- Changing what an existing field means raises the version.
- Hyperion and `tools/hxmap` reject a document with a version newer than they read, with a
  message, and ignore fields they do not know.
- `Hyperion.exe --hxm-check <map.hxm>` validates a map that an editor wrote
  (`hxm-for-editors.md`).

## Conversion

**From a CHK.** `from_chk` reads a CHK the way the game does. The game runs each section's
handler over every copy of that section, in file order:

- for most sections the last copy wins;
- `MTXM` and `MASK` copies overlay one another;
- `UNIT`, `THG2`, `TRIG` and `MBRF` copies add up.

So a "protected" map, which relies on duplicate sections, converts to what the game actually
plays.

**To a CHK.** `to_chk` writes a CHK that any StarCraft tool reads when the map is within
StarCraft's limits: 256 tiles a side, 11 players and neutral, 255 locations, 4 forces.
String ids are renumbered on the way back. Nothing in the game depends on an id's value.

**`VER`.** A map within the limits keeps a classic `VER` (59, 63, 64, 205 or 206).

A map past them gets a `VER` of Hyperion's own: 1206, or 1064 when `expansion` is false. A
map is past the limits when it has more than 256 tiles a side, a player at list entry 8 or
above who can be played, a trigger for a player index above 11, or a unit that a mod adds.

- No Blizzard version uses 1206 or 1064, so a map site or an editor can tell such a CHK from
  a classic one by its `VER` alone, whatever the file is called.
- Hyperion reads 1206 with the section layout of Remastered's 206, and 1064 with that of 64,
  with strings in `STR ` or `STRx`. Their `MRGN` therefore holds 255 locations. A map of
  `VER` 59, which holds 64, gets the others as empty locations when its `VER` is raised.
- A map converted back from 1206 gets 205 or 206, and one from 1064 gets 63 or 64.
- StarCraft rejects a `VER` it does not know, so it never loads such a map and misreads it.

### Hyperion's sections in a compiled map

A compiled Hyperion map has up to three sections that no classic map has. They exist because
the CHK's own sections have a fixed number of entries: `OWNR` and `SIDE` 12, `FORC`, `COLR`
and `CRGB` 8, and a trigger's player list 27.

- Each begins with a DWORD version, which is 1 for all three today.
- Hyperion rejects the map if it does not know that version, or if the section's size does
  not match its counts.
- They can be anywhere in the file. Hyperion reads `HXPL` before the sections that need the
  players, wherever it is.

| Section | After the version | Written when |
|---|---|---|
| `HXPL` | a DWORD count of player indices, 9 to 65, which includes index 11 once it goes past 11; then four bytes for each index from 8 up: the controller (`OWNR`'s values), the race (`SIDE`'s values), the force, and the colour's number or 0xFF for none. Index 11's four bytes are 0 | a player at index 8 or above can be played |
| `HXRG` | the same count; then four bytes for each index from 8 up: 1 if it has an RGB colour, then red, green and blue | such a player has `rgb` |
| `HXTE` | a DWORD count of `TRIG`'s triggers; then a QWORD for each, with bit n set when the trigger runs for index n | a trigger runs for an index above 11 |

Elsewhere in the compiled map, a condition or an action names an index above 11 as the
player code 0x100 + index, and `UNIT`'s owner byte is the index itself.

## Checking the conversion

`python -m tools.hxmap roundtrip` checks every map it is given: the sections the game reads
come back byte for byte, and the sections with text come back as the same document. The
replay check in `tools/hxmap/check.py` goes further: it plays the test replays with their
maps converted to the format and back, and compares every frame with the original.
