# Cores, threads and the GPU

## Lede

StarCraft 1.17 does everything on one thread of one core and draws with the processor. Hyperion
keeps the game itself on one thread, which is what makes every replay play the same game, and
moves the rest off it: drawing goes to the graphics card, checks and large games' lookups go to
a pool of worker threads, and the game rests between frames instead of keeping a core busy.
Moving the game's own update onto many threads is the next step.

## How to use it

### Settings, Performance

| Setting | Values | What it does |
|---|---|---|
| Processor threads | Auto (half your processor's threads, at most 16), Off, 2, 4, 8 or 16 | how many threads Hyperion spreads its checks and a large game's lookups over; a change takes effect after a restart |
| Rest between frames | on (default) or off | the game waits between frames instead of keeping a core busy |
| Game's cores | Auto, Large cache, Any | on a processor whose cores do not share one cache (a Ryzen X3D), puts a large game on the cores with the most cache. Auto does it from 2,000 units and lets go below 1,500 |

### Settings, Graphics

| Setting | What it does |
|---|---|
| Renderer | Vulkan (recommended) or Direct3D 12, on Windows. A change restarts Hyperion from there |
| Frame rate | Classic (1.17's), 60, Match the display (the default), 144, 240 or No limit |
| Vertical sync | with it on, a frame rate above the display's draws no frames the display cannot show |
| Frames ahead | how many frames the game may prepare before the card has drawn them: 1 shows each frame soonest, 3 keeps the card busiest |

### Settings, Game

**Game frame timing**: Even (the default) times each game frame exactly by the processor's
counter; Classic uses 1.17's timer, which steps every 15.6 ms.

### What it comes to

Measured on the PC it was built on (a Ryzen 7950X3D):

- **At normal speed**, Hyperion uses a seventh to a fifth of one core, where it used a whole one.
  A game left running in the background keeps its speed.
- **Drawing**: with 32 players' armies in view, a frame takes about 1 ms where it took 35.5 ms;
  6,700 images on screen at once take 1.5 ms; a 64-player game seen whole takes 1.6 ms.
- **The game's update** is one and a half to two times as fast in large games: a 64-player game
  plays 317 game frames a second where it played 211, and a battle of 19,400 units plays 38 a
  second where it played 18.5, faster than the game's Fastest speed (24).
- **Game's cores** makes large games 7 to 14% faster on an X3D processor; small games are
  unchanged.
- **Full screen** frames reach the display about 25 ms sooner, in one refresh.

All of this keeps 1.17's answers: every replay plays the same game it did.

## Limits and known issues

- The game's update itself still runs on one thread. The worker threads only read the game while
  it stands still between frames.
- In the browser build everything runs on one thread, and the drawing is the software path.
- Game's cores changes nothing on a processor whose cores share one cache, or off Windows.

## In progress and planned

- **The game's update on many threads** (planned, next): it starts by measuring how often a path
  search's inputs change between a frame's start and the search, which says whether searches
  answered ahead on other threads would hold. A way that keeps 1.17's answers stays exact for
  every replay; one that changes them would be a new rules revision, for Hyperion's own games
  only.

## Technical notes

- One pass of the loop (`engine/src117/lang/gameloop.cpp`, `RunGameLoop`): window messages, the
  clock, one game frame when it is due, the UI, a drawn frame when one is due
  (`PortFrameDue`), else a rest until input or the next thing due (`PortClockRest`).
- The worker pool (`src117/port/loop/port_workers.cpp`) takes work that only reads the game
  between frames: the state check's six domains and their parts, and the fog reads done ahead in
  games of 4,096 units or more. A batch wakes only as many threads as it has items.
- The state check (`port_synchash.cpp`): a 64-bit hash of each domain (units, objects, players,
  map, sprites, AI), every 24 frames, recorded into a Hyperion replay and compared on playback;
  a save holds the hash of its moment. Pointers are hashed as what they point at, so two runs
  hash alike wherever the program is loaded.
- The game runs faster in large games mainly by how it finds the units near a place: 1.17 keeps
  units in lists sorted across and down the map and walks long stretches of them; Hyperion also
  keeps them in a grid of squares 128 pixels (4 tiles) a side and answers the long walks from it,
  in the walk's order. The pathing's contour walks, a medic's search for someone to heal, the
  computer players' review of their forces and the removal of a unit from their attack groups
  ask far less. Each gives 1.17's answer to the bit; `HYPERION_FINDER_CHECK=1` runs both side by
  side, and over the whole replay suite they never differed.
- The drawing: the GPU presenter (Vulkan by default, Direct3D 12 on Windows) draws the map, the
  sprites, the fog's light (worked out on the card), cloaked units' shimmer in batches, and the
  HUD; work whose inputs have not changed since the last frame is kept.
- Measuring: `HYPERION_PERF=1` (`hyperion_perf.log`, ms per part each second),
  `HYPERION_PROFILE=1` (a sampling profiler), `HYPERION_FRAME_LOG` (each frame's time on the
  processor and the card), `engine/tools/hxrender.py` (a large game unwatched, zoom sweeps,
  profiles, comparisons). The benchmark measures a whole machine ([The benchmark](@benchmark)).
- Module docs: `engine/docs/modules/loop.md` and `engine/docs/modules/render.md`.
