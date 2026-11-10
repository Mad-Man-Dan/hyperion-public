# Hosting a LAN

## Lede

Any Hyperion can host a room on your network. Players join it from Hyperion on Windows, Linux or
a Mac, or from a browser with nothing installed, and the host runs the evening from a console:
the room, its matches, the devices in it, and what each one costs the host's machine. Every match
can be watched live, a few seconds behind, with a camera that finds the fighting.

## How to use it

### Hosting

1. Home, **Multiplayer**, **Host a Room**. Name the room, and set a password if you want one.
2. The host's console opens with its tabs:
   - **Room**: the matches and who sits where, as floor, rack or wall;
   - **Devices**: every device in the room, its system, how it plays (installed, in a browser
     tab, or streamed) and its load on the host;
   - **Settings**: the room's name and password, the watching delay, who may watch, whether the
     host offers its StarCraft files to browsers, whether the room keeps its own copy of each
     match, and what happens when a player drops.
3. Make a match: pick the map, the game type, the speed, the rules (Hyperion's or the classic
   ones), the mutators, and the seats: open, closed, a computer (with its level and handicap) or
   a player.
4. Start it once the players say Ready. Each seat's game starts as a skirmish does, with
   Hyperion's countdown.

The room keeps going between matches, and players keep their seats and handicaps from match to
match.

### Joining

- **From Hyperion**: Home, **Multiplayer**, **Join a Room**. Rooms on your network are listed by themselves, each
  marked "Plays here" or with the reason it does not (another version, another mod, other game
  data). Pick one and join; sit, choose your race and handicap, chat, and say Ready.
- **By address**: a room on another subnet is joined by its address, or started with
  `Hyperion --join <address>[:<port>]`.
- **From a browser**: open the room's address on port 7117 ([Playing in a browser](@browser)).

People on StarCraft from Battle.net's files and people on a classic install play in the same
room: the room checks the game's rules, not the files.

### Watching

Anyone the room lets watch can follow a match live, 5 seconds, 10 seconds, 30 seconds or a minute
behind the players (the host's choice), from Hyperion or a browser. **L** goes live. The picture
cuts to a fight a couple of seconds before it reaches the watcher, from the room's own copy of
the match. When the match is over, its replay is written on the host into
`maps\replays\<room name>\`.

### What the room checks

- Every 120 frames each seat's game is hashed and compared; the first difference is written to
  `hyperion_lan.log` with the parts that differ.
- 1.17's own sync check runs as it always did.
- A match records a replay on every seat, and they play back identically.

## Limits and known issues

- A match has eight seats, each a person or a computer; bigger games are skirmishes for now.
- A player who drops: the match waits up to two minutes for them, then plays on without them.
  Pausing for everyone, losing them at once, saving and rejoining are not built yet.
- A seat cannot come back into a running match.
- Rooms are found by broadcast on one network; across networks, join by address.
- The room's ports: TCP 6112 (games), UDP 6111 (finding rooms), TCP 7117 (the page). A second
  room on one machine needs other ports (`HYPERION_LAN_PORT`, `HYPERION_LAN_WEB_PORT`).

## In progress and planned

- **The game from the host** (planned): the host hands out each system's install and the game
  files; the join page's install choice says "Not available yet" until then.
- **Tournaments** (planned): brackets run from the host's console; its Tournament tab is greyed.
- **Online play** over the same transport (parked; [Online play](@online)).

## Technical notes

- Lockstep, as 1.17's multiplayer always was: only commands cross the network, and every seat
  runs the whole game from the same commands. Hyperion replaced Storm's network providers with
  its own, listed under 1.17's LAN id, so 1.17's lobby, countdown and game loop run unchanged
  (`src117/port/lan/port_stormlan.cpp`).
- The room (`Hyperion --room`) is a process with no window and no game: devices, matches and
  their lobbies, a relay for each live match, the host's settings, the log, and the page
  (`port_lanroom*.cpp`). Each match's relay builds steps from every player's turns and compares
  the players' hashes (`port_lanrelay.cpp`).
- Finding rooms: a four-byte query broadcast twice a second and the rooms' answers, compared for
  the version, the simulation revision, the mod and the game data's fingerprint, a hash of the
  game's rules (the DATs and scripts), not of the archives.
- The feed: each seat sends the room its replay as it grows; watchers play it a delay behind,
  and the room's copy of a match, a watcher with no delay, gives the console's Wall its pictures
  and tells watchers where the fighting is (`port_lanfeed.cpp`).
- The host's settings: `lanroom.json` in the room's data folder.
- Module doc: `engine/docs/modules/lan.md`.
