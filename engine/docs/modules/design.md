# design: the design engine, a developer's build of the engine

The design engine is Hyperion's engine as a program of its own, `HyperionDesign.exe`, for trying
ideas live. It plays Debug Tools' canvas on the GPU presenter in a window that resizes, maximizes
or fills the screen, with Dear ImGui panels docked round the game, laid over it, or put away. The
panels spawn units, play effects, switch and hide the HUD, draw units' paths, edit lights, looks and
the live values, and show the frame times. A scenario sets a game up the same way each time, on
the canvas or a map with its game type, seats and mutators; Again (F9) puts it back at its start,
and a style holds how it is drawn, so two looks can be compared on the same fight. Every panel works through the live channel's own commands,
the same ones a Claude session sends through the `hyperion-design` MCP server or
`tools/design.py` from the command line.

`Hyperion.exe` carries none of it. The design engine is linked from the x64 build's objects plus
its own folder, `src117/design/`, and the few hooks it needs in shared files are under
`#ifdef HX_DESIGN`, which only its build defines. The normal build, its gates and its packages are
unchanged, and the design engine is never packaged or released.

## Status

Built: the program and its build; the canvas started with a fixed seed; the window maximized,
sized or full screen, the game's view widened to the room it has; three ways of sharing the
window (panels beside the game, over it, or the game alone); the panels (Game, Spawn, Inspector,
Values, Effects, HUD, Paths, Lights, Perf, Console, Scenario, Style, Looks, Painter, Record) and
the menu bar; units' ways; effects in Luau; scenarios on the canvas or a map, their start point and Again;
styles and their A/B flip; the effects in the game's pixels; looks in depth (up to four zones of a
unit's colours, an aura apart from the paint with its own settings, templates, a look for each of
a player's units); the army painter; the grab of the window with or without the panels; looping
clips of the game to share (GIF, WebM, MP4); the MCP tools and the command line.
Windows x64, on Vulkan (the default) and Direct3D 12.

Compiled without `HX_DESIGN`, the twelve hooked files give the same machine code as before,
function by function, apart from source line numbers passed as constants; and `scene.hlsl` gives
the same DXIL and SPIR-V, byte for byte.

## Key files

| File | What it holds | Key functions |
|---|---|---|
| `src117/design/design_hooks.h` | the calls the shared files make and the accessors they give, each under `#ifdef HX_DESIGN` | |
| `src117/design/design_engine.cpp`, `.h` | the start's defaults, the window's place and size, the grab, `design_status`, `design_window`, `design_grab`, `design_grabbed`, `design_quit` | `DesignDefaultOptions`, `DesignWindowPlace`, `DesignGrabTake` |
| `src117/design/design_imgui.cpp` | Dear ImGui in the window: its start, the events it takes, the dockspace and the room it leaves the game, the view modes, the overlay layer, `design_click`, `design_type`, `design_view` | `DesignEvent`, `DesignGameRoom`, `DesignPanelsDraw`, `DesignViewSet` |
| `src117/design/design_panels.cpp` | the menu bar, every panel, the default layout | `DesignMenuBar`, `DesignPanels`, `DesignDefaultLayout` |
| `src117/design/design_ui.h` | what the two above share | |
| `src117/design/design_bridge.cpp` | the panels' channel requests and their answers | `DesignCall`, `DesignBridgeDeliver` |
| `src117/design/design_scenario.cpp` | scenarios: their files, the game a scenario starts (`SkirmishGame`, the game types by name), its set-up, the start point and Again, the Scenario panel, F8 and F9 | `DesignLaunch`, `DesignSeed`, `DesignScenarioStep`, `SetUp`, `Again` |
| `src117/design/design_style.cpp` | styles: taken from and put back through the channel's commands, A and B, the Style panel | `SaveNow`, `Apply`, `CmdStyle` |
| `src117/design/design_looks.cpp` | the Looks panel: the looks edited live, their zones on the palette and on the selected unit's colours, their aura, the templates; given to the selection or a player | `DesignLooksPanel`, `ZoneEditor`, `Aura`, `TemplateFields` |
| `src117/design/design_painter.cpp` | the Painter panel: a scheme for a player's army, its presets and the race's zone map; `looks_palette` and `look_colours` | `DesignPainterPanel`, `Apply`, `CmdColours` |
| `src117/design/design_record.cpp` | clips: the Record panel, `design_record`, `design_recorded`, `design_record_stop`; a clip's part of the window, its frames, the loop's fade and ffmpeg | `DesignClipRegion`, `DesignClipTake`, `Make` |
| `src117/design/design_hud.cpp` | `design_hud`: the HUD's design, layout, size, material and light, and hiding it | `DesignHudHidden` |
| `src117/design/design_paths.cpp` | units' ways read from their paths, `design_paths` and `paths` | `RouteOf`, `DesignPathsBuild` |
| `src117/design/design_fx.cpp` | the effects' runtime in Luau, its host calls, `fx_list`, `fx_load`, `fx_play`, `fx_stop`, `fx_check` | `Load`, `DesignFxBuild` |
| `src117/design/design_draw.cpp` | the drawing over the game: the camera, the primitives' buffer, the console's mask, the passes | `DesignWorldDraw`, `DesignCamera` |
| `src117/design/shaders/fx.hlsl` | one quad a primitive, its shape worked out at each pixel | `VSFx`, `PSFx` |
| `tools/build_design_x64.py` | the build: Luau and Dear ImGui into libraries of their own, the shaders, the x64 build's objects copied, the hooked files and `src117/design/` compiled with `HX_DESIGN`, `scene.hlsl` built again with it into the design's shader headers (found before the x64 build's), the link | `HOOKED`, `HOOKED_SHADERS`, `luau_lib`, `imgui_lib`, `shaders` |
| `tools/fetch_luau.py`, `tools/fetch_imgui.py` | Luau's and Dear ImGui's source releases into `third_party/` (gitignored), each checked against its SHA-256 | |
| `tools/design.py` | the launcher and the command line: builds when stale, fills `testrun/design/`, starts the engine; `status`, `call`, `frame`, `--stop` | `command`, `grab`, `stop` |
| `tools/control/hyperion_mcp.py` | the MCP server, registered twice: `hyperion` for Hyperion, `hyperion-design` on 27118 | `call`, `frame`, `window`, `view`, `panel_click`, `hud`, `paths`, `route`, `fx_*`, `click`, `press` |
| `../../design/fx/luau/` | the effects the design engine plays, one `.luau` file each | |
| `../../design/looks/templates/` | the look templates, `{name, about, look}` each | |
| `../../design/painter/` | `zones/<race>.json`, each race's zones as palette entries; `presets/`, the schemes | |

The hooks in shared files:

| File | Hook |
|---|---|
| `port/platform/port_paths.cpp` | `DesignDefaultOptions` once the command line is read |
| `port/platform/port_winmsg.cpp` | `Pump` gives each SDL event to `DesignEvent` before `Translate`; one the panels take never reaches the game |
| `port/render/port_present.cpp` | a frame drawn whenever the panels are shown (`DesignWantsFrame`); the windowed place from `DesignWindowPlace` |
| `port/render/port_gpu.cpp` | `PortGpuGameBox` puts the game in the room `DesignGameRoom` gives; `GpuFrameWindow` draws `DesignWorldDraw` after the game, the HUD design unless `DesignHudHidden`, and `DesignPanelsDraw` last; the grab, the window's passes drawn again into a texture and downloaded; a clip's frame the same without the HUD design and the menus, only its part downloaded |
| `port/render/port_gpu_ui.cpp` | `DesignPanelsDraw` over Hyperion's menus |
| `port/render/port_console.cpp` | `DesignConsoleMask`, the classic console's mask; a dialog shown again when the console comes back is marked shown rather than given `ctrl_show`, whose event reads the parent a dialog does not have; the console built again when it comes back during a game, after a clip |
| `port/tools/port_control.cpp` | `Send` gives the answers to `DESIGN_CLIENT` to `DesignChannelAnswer`; `PortControlPoll` calls `DesignPoll` each pass, in a game or out; `DesignChannelPost`; `DesignInGame`, `DesignGameFrame`, `DesignGamePaused` |
| `port/front/port_front.cpp` | the front end leaves its screen for a scenario's game, and `PortFrontStartMode` starts it first (`DesignLaunchPending`, `DesignLaunch`) |
| `port/replays/port_observe.cpp` | `PortObserveSeed` takes a scenario's seed (`DesignSeed`) after the harness's pin |
| `port/render/port_looks.cpp`, `port_looks.h` | a look's zones, aura and circle (`ReadLook` and `LookJson` round the shared reader and writer, `Make`, `LookGpuDesign`, 1152 bytes a look for the GPU); a unit's paint and aura apart (`LookOfUnit`, `PortLookOfSprite` giving `paint \| aura << 8`, `PortLookImageBegin`, `PortLookAuraNow`); a turret given its base's; the selection's marks left out; `look_player`, `look_takes`, the live values `looks.aura` and `looks.aura.speed`; the channel's looks and each player's kept from game to game; the built-in `aura-green` |
| `port/render/port_scene.cpp` | a looked image's aura recorded as a look primitive round it, as a crystal's halo is, with the colour slot for an aura in the player's colour |
| `port/render/shaders/scene.hlsl` | `LookAura`; `LookColour` reading each entry's zone, its ramp, material and mix; `port_gpu_scene.cpp`, unchanged, is compiled again to take these shaders |

## How it works

**Starting.** With no map, replay or save named, the design engine plays the canvas
(`PORT_CANVAS_TILESET`). Its channel listens on 27118 unless `--control` says otherwise, one past
Hyperion's, so both run side by side. Its seed is 1117 unless `--seed` says otherwise. It always
uses the GPU presenter, Vulkan unless `HYPERION_RENDERER` names another GPU one. The window starts
windowed and maximized, or at `HYPERION_DESIGN_SIZE` (`WxH`, centred), or full screen with
`HYPERION_WINDOW=fullscreen`; Alt+Enter and the Window menu switch. Its settings are its own, in
`testrun/design/settings.json`, and the panels' places are in `imgui.ini` beside them; the
player's Hyperion settings are never touched.

**The window and the game's view.** The game's frame is always 480 pixels high and 640 to 1712
wide. The panels leave the game a room, the dockspace's central node; `PortGpuGameBox` draws the
game in that room and every mouse mapping goes through the same box, so the picture and the
pointer stay in step. Once the room's shape has held for 250 ms the view asks for a frame of that
shape, `480·w/h` wide, through `PortViewRequest`; a room taller than 4:3 letterboxes. Three modes
share the window, kept in the setting `design.view`:

| Mode | The window |
|---|---|
| beside | the menu bar and the panels docked round the game, which has the room they leave |
| over | the game fills the window, the panels over it, a little see-through |
| game | the game alone, no menu bar and no panels; F11 switches to it and back |

The camera's zoom goes out as far as the setting `hyperion.designZoomMax` allows, 8 unless
changed, from the Game panel or `design_view`. It is a key of its own because the settings file
is shared with the game, whose limit is `hyperion.zoomMax` and the player's to set: a design
engine or studio writing that one would zoom the player's game out with it.

**Events.** `Pump` is where every SDL event leaves the queue, and in the design engine each goes
to ImGui first. A press goes to the panels when a panel window is under it at the event's own
position, else to the game; its release goes to the same side, so no key or button sticks. Keys
and text go to the panels while one of their fields has the keyboard. The panels' overlay layer
is `PORT_OVERLAY_FREE_CURSOR` with an `Outside` hook, as Debug Tools' is, so over the panels the
game keeps its pointer, Windows' arrow shows and the map's edges do not scroll. ImGui never
changes the cursor, never captures the mouse and never stops text input; Hyperion does those.

**The panels' commands.** A panel calls a channel command with `DesignCall(cmd, fields, fn)`. The
request goes on the channel's inbox as client `DESIGN_CLIENT` and runs where `PortControlPoll`
runs every request, never inside a frame; its answer comes back to `fn` at the channel's next poll
(`DesignPoll`). So a panel's button and a tool's call run the same code. What a panel only reads (the
selection, the frame times) it reads directly each frame.

**The panels.**

| Panel | What it does |
|---|---|
| Game | the view mode, the zoom and how far it may go, pause, step, reveal, grab |
| Spawn | a unit list with a filter, the player and count; at the middle, by clicking on the map (a right click stops), a fight, clear every player |
| Inspector | the selected units: the first one's type, player, hit points, shields, energy, kills, armour, weapon, progress and resources, and the rest listed |
| Values | every live value, filtered, set from the panel |
| Effects | the Luau effects with their params as sliders; in the game's pixels, with the pixel's level (a quarter of a game pixel to four), the steps of light, the colour levels and the frames held; the screen flash's cap (off); play at the middle, on the selection or by clicking, loop, stop; files reloaded when they change |
| HUD | the HUD's design, layout, size, material and light, and hiding it |
| Paths | units' ways drawn, for the selection or all, one player or any |
| Lights | the lights listed, edited and saved |
| Perf | the frame times |
| Console | any channel command with JSON fields, and its answer |
| Scenario | the scenarios, load and save; Again (F9) and Start here (Shift+F9); keep the panels' spawns and effects; reveal, start paused, its style; the game (canvas and tileset, or a map found by name with its game type, sides, speed, rules, seats and mutators) and Start this game; the units and effects it places |
| Style | the styles, apply and save; A and B and the flip (F8); what a style holds |
| Looks | the looks, sent as they move: templates to apply or start from; the zones as tabs, each with the colours it takes (a rule, a band of hue, or entries clicked on the palette's grid or on the selected unit's own colours, each shown with what the look makes of it), its material, fit, mix and ramp or one colour; the aura in groups (shape, colour, motion, pixels, where, the selection circle); the material and the light; give the selection a look or its own back, give a player's units one or take it back; add one; save `looks.json` |
| Record | a looping clip of the game: round the selected unit, the view's middle or the whole view; its length, frame rate, the fade that makes it loop, the square's size and the scale; GIF, WebM, MP4 or PNG frames; the selection's marks kept or not; open the clip or show it in its folder |
| Painter | a player's army in a scheme: the player and the race's zone map, a preset, the primary colour, each zone's colour, finish (paint, metal, glow) and mix, an army aura from a template; apply to the player, as it changes or on a click, or take it off; randomize; save the scheme; the race's showroom; the zone map sampled from the player's units, suggested, put right on the grid and saved |

The menu bar holds the Window menu (full screen, windowed, maximized, common sizes, the view
modes, the default layout again), the Panels menu and the Game menu, and the game's state on the
right. Out of a game the panels wait, kept, and Hyperion's menus work as in Hyperion.

**The HUD.** `design_hud {design?, layout?, size?, material?, light?, hidden?}` picks a design from
`PortHudDesigns` (not one held back from the build), writes the picks to the design engine's
settings and reloads the HUD as Settings' HUD tab does. Hidden is an overlay layer that hides the
classic console (`PORT_OVERLAY_HIDES_CONSOLE`), so the map takes every row, and no design is
drawn.

**Paths.** `design_paths {on?, units?, player?}` draws the ways of the selected units, all of
them, or a list of ids, for one player or any; with nothing given it answers the state. A unit's
way is read from the game: its position, `nextWaypoint` where it steers, the waypoints left in its
path (`sai_path`'s packed points from `c1F`), then, when those stop short of the goal, the centres
of the regions the way goes on through, dashed as an estimate, and the goal, or `moveTarget` for
a unit with no path. A line to the order's target unit is drawn white. `paths` answers the same
points as JSON, `[x, y, kind]` in map pixels.

**Effects.** Effects are Luau scripts in `design/fx/luau/`, one each, in the contract of
`design/fx/API.md`: `effect({id, name, slot, ramp, length, loop, params}, function (f, p) ... end)`,
a pure function of its time and places. Places are Luau's vectors. The VM runs sandboxed
(`luaL_sandbox`, each file in a thread of its own) with 4 ms an effect a frame before it is
stopped. What runs many times a frame is the host's: `f.rand`, `f.noise` and `f.fbm` (the
studio's hash and noise, the same numbers, `fx_check`), the ramps, and every shape, which is a
call that appends plain numbers to one list. The clock is the game's frame count plus the time
within the frame, so pausing the game stops the effects. Drawn so far: spark, glow, light (a glow
on the ground), ring, disc, beam, bolt, trace, star, shard and flash; the others are counted in
`design_status`'s `leftOut` and not drawn. `fx_play {effect, at?, unit?, to?, toUnit?, from?,
radius?, params?, loop?, seed?, team?}` plays one (`unit: "selected"` for the selection; the
effect's id goes in `effect`, since the channel's own `id` numbers the request), `fx_stop
{playing?}` stops one or all, `fx_load` reads the files again, `fx_list` lists them with their
params.

**Drawing over the game.** Paths and effects become one list of primitives a frame (glow, dot,
ring, line, star, triangle, wash), each 128 bytes, in a storage buffer. `fx.hlsl` draws a quad a
primitive and works out its shape at each pixel; one draw for each run of the list in one blend,
added light or laid over. Map places go to the window through the camera, zoomed or not, and the
game's box. A primitive is clipped to the map's rows, and nothing is drawn where the classic
console is: its mask (`DesignConsoleMask`) is a texture the shader reads.

**Scenarios.** A scenario is a file in `design/scenarios/` (its `README.md` has the format): the
game, the units placed on it (`spawn`'s fields, their orders with them), reveal, the camera, the
effects (`fx_play`'s), start paused, and a style. The game is the canvas on a tileset, or a map
with its game type by the set-up's name (`IterateMainTemplates`), a sub type by its value (Team
Melee's teams, Top vs Bottom's split), speed, Hyperion's rules, a watched game, the seats (race,
computer level, team, start, handicap) and the mutators (`MutatorsJson`'s names), and a seed.

Loading one leaves the game on as Quit Mission leaves (`gameloop_init(4)`, once the game's loop
runs, and again 24 frames on if it plays on), and the front end starts the scenario's game before
anything else (`DesignLaunch`: `PortLaunchCanvas`, or a `SkirmishGame` as the skirmish set-up makes
it and `PortLaunchSkirmishGame`). Its seed is the game's (`DesignSeed`), restarts too. Classic's
pause outlives the game it was given in, so a game that begins paused is told to go on. Once the
new game's first frame has run, the set-up goes out as one batch of channel commands, which run in
one poll, so the game does not move between them: `pause`, `spawn` for each unit, `reveal`,
`camera`, `fx_stop` and each effect, `design_start_here`, `resume` unless it starts paused, and
the style. Two loads of a scenario play the same game: the same seed, the same set-up on the same
frame.

**Giving.** `design_give {player?, minerals?, gas?}` adds minerals and gas to a player's in the
game being tried (player 0 when not given) and marks the game as changed, as a spawn does: a
canvas starts its players with none, and a building's buttons are grey until they can pay.

**Again.** `design_start_here` takes a snapshot of the game (`PortSnapTake`, line `0xD5E1`, apart
from the trainer's and a replay's seek) at the frame now: about 3 to 8 ms and 30 to 70 MB.
`design_again` (F9) puts it back (`PortSnapRestore`), then does what the trainer's rewind does
(the console's selection and update, the cursor's target, the HUD, the map redrawn, sound
stopped), plays the scenario's effects again and resumes. What a snapshot holds is the game's, so
a style, live values, lights and looks changed since stay as they are: the same fight under the
new look. A new game drops the snapshot; Again in the scenario's game without one makes the game
again.

**Styles.** A style is a file in `design/styles/`: the Settings rows that change the picture by
name (art, art filtering, gamma, colour cycling, HD lighting, player colours, unit portraits,
console layout and skin, ally colours, colour-blind colours), the live values that differ from
their defaults (`zoom`, `camera.smooth` and `hd.debug` left out), the lights, the looks and the
HUD's picks. Saving takes them through `settings`, `list`, `lights`, `looks` and `design_hud`, and
writes the file once the answers are in. Applying sets each row (`setting`), each value, the values
it leaves out back to their defaults (`set`), its lights and none other (`light`, with `remove`),
each look (`look`; a look not in the style stays) and the HUD. A scenario's style is applied after
its game starts, since looks are made again each game. A and B are two styles; the flip (F8)
applies the other. The post effects are off unless the style turns `effects` on.

**Effects in the game's pixels.** With `fx.pixel` on (the default), `fx.hlsl` works each effect
out at the middle of the cell it falls in, so a cell is one colour, as the sprites' pixels are. A
cell is `fx.pixel.size` game pixels, taken as the nearest of 1, 2, 3 or 4, or of a half, a third
or a quarter of one, so its grid is always the game's: finer cells split each game pixel evenly,
coarser ones join whole game pixels, at any size of window; edges are hard, lines no thinner than a pixel; a glow is in
bands, and light and cover go in `fx.pixel.steps` steps with a 4x4 ordered dither between them,
counted from the map's corner so it stays on the map as the camera moves; each colour channel has
`fx.pixel.colours` levels (0 any). An effect's time steps by classic's frames, `fx.pixel.hold` of
them a step, as an image script's `wait` does. The paths stay smooth. Off, the effects are drawn
smooth at the window's pixels; a style keeps the switch, so the two compare with the flip.
A flash of the whole screen (`f.flash`) is held to `fx.flash`, as a player's comfort cap holds it,
and is off unless that is raised.

**Looks in depth.** A look has up to four zones of the colours it takes, in every build. A
zone takes palette entries by a rule (`blues`, `greens`, `all`, `none`, `reds`, `golds`, `greys`,
`skins`), by a band of hue in OKLab (`hue {from, to, chroma, lightMin, lightMax}`) or as a list
(`entries`), less those in `except`; an entry goes to the first zone that takes it, and the
transparent entry and the team colours never do. Each zone has a ramp of six colours (or one,
`color`, made a ramp by lightness), a material (`crystal`, `gas`, `paint`: the ramp at the tone
as the art is shaded; `glow`: the ramp added as light), a `mix` over the image's own colours, and
a `fit`: the zone's lightness spread over the whole ramp, or each entry kept at its own. The first
zone is the look's own `rule`, `material` and `ramp`, so a look of the game's reads as before.
`Make` fits each zone and makes the classic remap, the ramp's colour mixed into the entry's own
before the nearest palette entry is taken; `LookColour` reads each entry's zone from the GPU's
data. `look_takes {name}` gives what a look takes now.

A unit has a paint and up to three auras at once. The paint is the look given by hand unless
that look only has an aura (rule `none`), else the provider's (rich bases), else its player's
(`look_player {player, look}`), else its type's; the first aura is the hand's look's if it has
one, else the paint's. So an aura given by hand goes over an army's paint. The other auras are
those of the looks that name the unit's type before the one it is painted with, so three looks
that each bring an aura all show (`Reindex`). `PortLookOfSprite` gives
`paint | aura << 8 | another << 16 | another << 24`, which `CSprite::Draw` hands back unchanged;
`PortLookImageBegin` takes the paint's remap for the image and notes the auras
(`PortLookAuraAt`). A turret is not in the unit list: its base hands it its own looks, unless the
turret has a look by hand. A selection's circle
takes neither unless the look's `circle` is on. The looks the channel made or changed, and each
player's look, are kept from game to game, so a scheme stays through a scenario's new game.

**Auras.** An aura is a line of light hugging a unit's outline and a glow past it. Each image the
aura's look covers records a second look primitive round it after itself, its box widened by the
reach and raised where embers rise, carrying the image's frame as a crystal's halo does and the
colour slot; `LookAura` in `scene.hlsl` finds each pixel's distance to the frame's nearest drawn
pixel, ring by ring out to the reach. Its fields, in `looks.json`, the `look` command and the
Looks panel:

| Group | Fields |
|---|---|
| shape | `reach` 0 to 8 (0 none), `width`, `gap` (pixels between the unit and the line), `falloff`, `inner` (the unit's own edge lit) |
| colour | `color` (glow), `mid`, `core` (line); `mode`: `fixed`, `ramp` (line, middle, glow outward), `team` (the player's shades), `cycle` (the hue turning); `strength`, `opacity`; `blend` `add` or `over` (laid on, so a dark one outlines) |
| motion | `speed`, `pulse`, `pulseRate`, `flicker`, `crackle`, `crackleSize`, `sparks`, `embers` (pixels rising off the edges that face up), `bands` and `bandSpeed` (bright bands marching round the outline) |
| pixels | `steps` 2 to 8 with `dither`, `block` 1 or 2 game pixels |
| where | `images` `all` or `main` (no overlays such as muzzle flashes), `overUnits` |

All of it is at the game's pixels and steps with its frames. By default it draws only where no
sprite is drawn yet, so a turret's aura stays off its base. The aura is the GPU scene's, in full
colour, with the looks on (`looks`, `looks.aura`, `looks.aura.speed` over every aura's speed).

**Templates.** `design/looks/templates/` holds ten: `power-up`, `psionic`, `rage`, `frost`,
`void`, `hero-gold`, `team`, `prism` (auras that keep the unit's paint), `gilded` and `ghost`
(paint). The Looks panel applies one to a look, its aura over an aura as it starts so nothing of
the last one stays, or makes a new look from it.

**The army painter.** A scheme is a primary colour, the player's own (the channel's `colour`,
which sets the team colours' ramp), and for each zone of the race's zone map a colour, a finish
(`paint`; `metal`, a crystal that shows nothing through, with shine; `glow`) and a mix, with an
aura from a template if wanted. Applied, it becomes the look `painter-p<slot>`, its zones the zone
map's entries, and is given to the player with `look_player`. A zone map,
`design/painter/zones/<race>.json`, names palette entries: `armour`, `trim`, `lights` for terran,
`gold`, `hull`, `psi` for protoss, `carapace`, `flesh`, `bone` for zerg. The first maps were made
by sampling each race's showroom (`look_colours {player}`, the entries the player's units and
their turrets draw with, by pixels) and parting them by hue and lightness; the panel's grid puts
them right. A scenario `painter-<race>` stands one of each of the race's units and workers in two
rows on the canvas; protoss's arbiter stands apart, since its field would cloak the rest. Presets
in `design/painter/presets/`: "Azure & gold", "Desert", "Arctic", "Ivory & cyan", "Obsidian &
ember", "Night", "Primal", "Toxic", "Crimson & bone".

**The grab.** `design_grab {path?, panels?}` draws the next frame's passes again, at the window's
size, into a texture of its own, with the panels when asked, downloads it and saves a PNG
(`SDL_SavePNG`), by default in `testrun/design/grabs/`. `design_grabbed` answers its state:
`asked`, `drawing`, `saved` or `failed`, with the path and size. A grab shows the game's art, so it
stays on this machine.

**Clips.** `design_record` takes a clip of the game to share: a square of `size` map pixels round
a unit (the one given, else the first selected) or a point, or the whole view. While it takes, a
selection look leaves the marks undrawn and an overlay layer flagged to hide the cursor and the
console shows (the console comes back built again); a fifth of a second later the frames start.
Each frame due, at the clip's rate, is the grab's frame without the HUD design and the menus, and
only the clip's part of the window is downloaded, its square following the unit. The frames are
sized to the clip (as the window shows the map, or at `scale` pixels a map pixel) and held. A clip
loops: `fade` seconds more are taken past its end and faded over its start, so the last frame runs
on into the first. A thread then picks the frame nearest each of the clip's times, writes PNGs
and runs ffmpeg from the PATH for a GIF (its own palette, ordered dither, looping), a WebM (VP9)
or an MP4 (H.264); the clips go in `testrun/design/clips/`, which stay on this machine.

## Driving it from Claude

The `hyperion-design` MCP server in `.mcp.json` is `hyperion_mcp.py` on port 27118, allowed
without prompts in the repo's local settings. Its tools: `call` (any command; an unknown one's
error lists them all), `frame` (the window as an image, `panels` to include them), `window`,
`view`, `panel_click` (a click in the window's pixels, as the mouse gives it, so the panels see
it), `scenario`, `again`, `start_here`, `style`, `look` (with zones, circle and the whole aura),
`look_player`, `record` (a clip made and waited for), `hud`, `paths`, `route`, `fx_list`, `fx_load`,
`fx_play`, `fx_stop`, `click` and `press` (in
game pixels, straight to the game), and Hyperion's own (`status`, `spawn`, `select`, `camera`,
`snapshot` and the rest). A command that times out is not sent again. The server drops its
connection after a minute without a call.

A session's own test runs use `--background`, a port of their own and a game folder of their
own (`--folder design-test`), never 27118 while the user's design engine is open, whose files are
in use: a window that takes no focus, and no crash window (`HYPERION_QUIET=1`), the report left in
the folder's `crashes/`.

From the command line:

```
python engine/tools/design.py                    start it, maximized
python engine/tools/design.py --fullscreen       start it on the whole screen
python engine/tools/design.py status             design_status
python engine/tools/design.py call spawn '{"unit": 0, "x": 900, "y": 1000, "count": 6}'
python engine/tools/design.py frame out.png --panels
python engine/tools/design.py call design_scenario '{"load": "bgh-melee"}'
python engine/tools/design.py call design_again
python engine/tools/design.py --stop
```

## Commands and environment

| Name | Effect |
|---|---|
| `python tools/build_design_x64.py` | builds `src117/build/design/HyperionDesign.exe` after `build_engine_x64.py`, `fetch_luau.py` and `fetch_imgui.py` |
| `python tools/design.py [--size WxH] [--fullscreen] [--background] [--port <n>] [--folder <name>] [--build] [--stop]` | starts it from `testrun/<name>/` (`design` unless given), see the script's help |
| `design_status` | the program, its port, the design folder, the game (on, frame, paused, the view's size), `window`, `hud`, `paths`, `fx` (playing, shapes, ms a frame, left out, errors), `panels`, `scenario` (name, stage, its game, start, Agains, units, effects, the game), `style` (current, A, B, which is on) |
| `design_scenario {list? \| load? \| new? \| game? \| clear_units? \| clear_effects? \| units? \| effects? \| reveal? \| paused? \| style? \| save? \| launch?}` | a scenario: listed, loaded, built and saved, its game started; with nothing, its state |
| `design_launch {game}` | a game started as a scenario's, with nothing placed |
| `design_again`, `design_start_here` | back to the start point (F9); a start point at the frame now (Shift+F9) |
| `design_style {list? \| save? \| apply? \| a? \| b? \| flip?}` | styles: listed, taken, put back, A and B and the flip (F8) |
| `design_window {w?, h?, fullscreen?, maximized?}` | the window's size, full screen or maximized; with nothing, how it is |
| `design_view {mode?, zoom?, zoomMax?}` | `beside`, `over` or `game`; the camera's zoom; how far it may zoom out, 1 to 8, kept as `designZoomMax` |
| `design_click {x, y, button?, wheel?, to?}` | a click or a turn of the wheel at a point of the window, given to the panels and the game as the mouse's; with `to: [x, y]`, a drag from the point to there |
| `design_type {text?, key?, ctrl?, shift?}` | typing, given to the panels as the keyboard's: text as typed characters, or a key by name (enter, escape, tab, backspace, delete, home, end, the arrows, f5, f9, a letter) with Ctrl or Shift held |
| `design_grab {path?, panels?}`, `design_grabbed` | the window saved as a PNG, and the grab's state |
| `design_record {seconds?, fps?, loop?, fade?, around?, unit?, x?, y?, size?, scale?, marks?, format?, name?, centre?}` | a clip begun; `around` is `unit`, `point` or `view`; `format` gif, webm, mp4 or png |
| `design_recorded`, `design_record_stop` | the clip's state (`taking`, `making`, `done`, `failed`), frames taken and made, its size and path; the one being taken dropped |
| `design_hud {design?, layout?, size?, material?, light?, hidden?}` | the HUD; with nothing, the picks and the designs |
| `design_paths {on?, units?, player?}`, `paths {units?, player?}` | units' ways drawn, or answered |
| `fx_list`, `fx_load`, `fx_play {...}`, `fx_stop {playing?}`, `fx_check {seed}` | effects |
| `look_player {player, look}` | a look for each of a player's units; an empty name takes it away |
| `look_takes {name}` | what a look takes of the palette now: each entry's zone (-1 none) and its classic remap |
| `look_colours {unit \| player \| type, detail?}` | the palette entries units draw with in their frames now, `[entry, pixels]` most first; with `detail`, a unit's images (image, palette type, frame, hidden, frames) |
| `looks_palette` | the palette's 256 colours |
| `design_quit` | leaves |
| `HYPERION_DESIGN_FOLDER` | the repo's `design/` folder, where the effects are; otherwise the first `design\` with a `_shared\` going up from the program |
| `HYPERION_DESIGN_SIZE` | the window's first size, `WxH`; maximized when not set |

## Known limits

- `press` is a tap, down and up at once, so a key held to scroll does not scroll.
- A clip's frames are taken as the window is drawn, at the first frame drawn when each is due; a
  slow frame leaves the nearest one in its place. The loop's fade shows a moving unit twice for
  its length; a unit that stands, or a paused game, loops cleanly. GIF, WebM and MP4 need ffmpeg
  on the PATH; without it the clip is left as PNG frames.
- Paths and effects are drawn over the game's frame, after it: an effect does not sort with the
  sprites, light only adds glow, and the shapes keep their pixel size as the camera zooms only
  through the camera's scale. Drawing them in the scene's own pass is the way to all three.
- Effects' `f.each` is a loop in Luau, not instanced, and Luau runs interpreted (no native code).
- Design files may name a game global that some x64 file already uses (`sgpStatStrs`, through
  the same declaration): the x64 build's `abs_globals.obj` binds it. One no x64 file uses fails
  to link.
- One window: ImGui's viewports, panels dragged out as windows of their own, are off.
- Again puts back the game, not what was done after: orders given by hand since the start point
  are not given again. A scenario's own orders, given with its units, are in the snapshot.
- A pixel effect's faint glow is a visible dither mesh at few steps; more steps or the smooth
  look soften it.
- An aura shows on the classic art; Hyperion does not draw Remastered's HD art in this version, so
  the aura has not been seen over it.
- Zones, auras and the painter's finishes are the GPU scene's full colour; the classic 8-bit
  picture takes a zone's colours through the nearest palette entries and has no aura.
- A cloaked unit, a dark templar's or an observer's own included, is drawn as a distortion of
  what is behind it and keeps no paint.
- A zone map names palette entries; the units' entries are the same on every tileset, but a few
  entries a tileset changes could fall in a zone.
- The `perf` command's present and scene times are the CPU's; the shaders' time on the GPU is not
  measured. 182 painted marines with auras: 120 frames a second at the cap, the CPU times as with
  the looks off.
- Effects play where a scenario or a panel puts them; nothing yet ties one to a game's event (a
  weapon firing, a death, a spell).
- `spawn` knows a unit by its number or its map name (`terran_marine`); the MCP's `scenario`
  turns short names (`marine`) into numbers.
- A map's first game in a run loads its tileset's HD art on its first frame, which can take a
  minute and more; the set-up waits for it.
- The camera does not stop at the map's edge, as in Hyperion, so zoomed out past the canvas
  map's width the sides show nothing. Zooming in, or a larger map, fills them.
