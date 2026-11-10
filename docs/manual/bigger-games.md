# More players, more units

## Lede

Hyperion plays games far past 1.17's limits: up to 32 players, maps up to 512 tiles a side,
thousands of units, and selections of up to 255. Every map, replay and saved game of 1.17's
still plays exactly as it did, because a game takes the bigger sizes only when its map asks for
them.

## How to use it

### Big maps

Hyperion's own maps are in `Data\maps\Hyperion\`, in the skirmish map browser under Hyperion.
They are remade from 1.17's (the `.hxm` format, which holds more players and larger maps):

| Map | Players | Size (tiles) |
|---|---|---|
| Big Game Hunters 2x | 16 | 256 by 256 |
| Big Game Hunters 2x 24 | 24 | 256 by 256 |
| Elderlands 2x | 16 | 384 by 384 |
| Garden of Aiur 2x | 16 | 384 by 384 |
| Big Game Hunters 3x3 | 32 | 384 by 384 |
| Big Game Hunters XL | 16 | 512 by 512 |
| Big Game Hunters XL 1800 | 16 | 512 by 512, starting with 1,816 units placed |

1.17's largest map is 256 by 256 with 8 players.

### Games for 32

In Skirmish, pick a map for more than 8 players. You play one slot; the slots past 8 take
computers, each with its own race, level, team and handicap, as the first eight do. Team Match,
the computer levels, handicaps and mutators all work past 8. A LAN room's match seats up to 8
people, with computers in the rest.

### 64 players, experimental

`Data\maps\Hyperion\Experimental\` holds Big Game Hunters 4x4 (64 players, 512 by 512) and
Big Game Hunters 8x8 (64 players, 1,024 by 1,024). They play, but they are past what Hyperion
aims at for now, 32 players and 512 tiles, so they are kept apart from the rest.

### Units

A game holds 1.17's 1,700 units for every 8 players: 1,700 in a game of up to 8, 6,800 in a
game of 32, 13,600 in a game of 64, and sprites, images, bullets and orders in step. In a game
past 1.17's limits, 25,600 units can follow a path at once, where 1.17 has room for 1,024;
before, a big army's units past that number stood still. Big games draw and play much faster
than they did (v0.6.2).

### Selections of 255

In a game by Hyperion's rules (every new skirmish by default, and a room's match when the host
chooses them):

- select up to **255 units**, by a drag, a double click or Ctrl+click (the units of a type in
  what shows, at any zoom), and make control groups of up to 255;
- your own **buildings select together**. A command goes to one of them: Train to the building of
  that type with the shortest queue, Research and Upgrade to the first one free, Build to the
  nearest worker;
- **subgroups**: the selection is sorted by kind, the card is the active kind's, and **Tab**
  steps through the kinds (Shift+Tab back), as in StarCraft II. A spell is cast by the nearest
  unit that has the energy;
- a mixed group gets the shared command: Attack with Medics in the group attack-moves the rest
  while the Medics heal-move.

The HUD designs show a big selection in pages ([The HUD](@hud)).

### Saves and replays

Big games save and load, and record replays, like any other. A Hyperion replay checks itself as
it plays: it carries the game's state at intervals and compares each one, so a replay that
played differently from its game says so.

### Map makers

Triggers name the players past 8 (each slot from 12 up by its own number), and the trigger
lists, leaderboards, objectives, Give Units and Set Alliance work for every slot. See
[For map makers](@map-makers).

## Limits and known issues

- Scope is held at 32 players and 512 tiles a side for now; the pathing is tuned and tested to
  512. The 64-player maps are experimental.
- A game by the classic rules (a 1.17 replay or save, an EUD map, a room's match by the classic
  rules) selects 12.
- People: one in a skirmish, up to 8 in a LAN room's match; the other seats are computers.
- Online play is not here yet ([Online play](@online)).

## In progress and planned

- Past 32 players and 512 tiles for everyone: parked, by decision, until the rest is done.
- The game's own update on many threads, so the biggest games run faster still
  ([Cores, threads and the GPU](@performance)): planned, next.

## Technical notes

- The sizes: `engine/src117/h/playerlimits.h` (65 slots, `PLAYERS_MAX` 64) and `poollimits.h`
  (1.17's pools times the players over 8 rounded up, at most 8 for 64 players; the 64-bit build's
  storage holds 13 times, 22,100 units and 65,000 images, which only the benchmark's stress run
  takes, `--bench-pools`). Unit ids past 2,047 use 24 bits for the index.
- The bigger sizes are turned on only by a map's `HXPL` section naming players past 7, or by a
  Hyperion save; a 1.17 map with no player past 7 takes 1.17's sizes exactly. The sizes are not
  recorded: they follow from the map's slots and the simulation revision, which a replay and a
  save carry (`src117/h/hxsim.h`).
- Per-player globals keep 1.17's address for slots 0 to 11, with blocks of their own for the
  rest (`PlayerTable`, `PlayerGrid`, `PlayerMatrix`), so a game plays and hashes the same in a
  build with more slots.
- Maps past 256 tiles carry their pathing tables whole in a save, under a `PATH` version 1.17
  refuses. The map format: `engine/docs/maps.md`.
- Hyperion's selection is the rule option `HX_OPT_SELECTION` (`src117/h/hxrules.h`): selections
  sent as command 0x8C in chunks of up to 60 ids, rows of the port's own
  (`src117/port/gameplay/port_selwide.cpp`), buildings' commands picked by the receivers in
  `net_cmds.cpp`.
- The replays' checks: `hyperion_replay.log` in the Data folder ("checked 452 of 452: the game it
  recorded").
- Module docs: `engine/docs/modules/gameplay.md` (limits, slots past eight, triggers) and
  `engine/docs/modules/hud.md` (Hyperion's selection).
