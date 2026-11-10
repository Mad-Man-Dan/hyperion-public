"""An MCP server for Hyperion's live channel (src117/port/tools/port_control.cpp), so a
Claude session can change what the running game shows and look at the result.

Registered in the repository's .mcp.json as "hyperion". It connects to 127.0.0.1:27117 (or
HYPERION_CONTROL_PORT) when a tool is first used, and again after Hyperion restarts. The
channel is open while Hyperion runs with --control, while Debug Tools is the active mod,
and on the canvas (python engine/tools/play.py --renderer direct3d12 --canvas).

snapshot returns the frame as an image as well as its path. The frames show the player's
game art: they stay on this machine, in the game's data folder.

Registered again as "hyperion-design" on 27118 for the design engine, HyperionDesign.exe
(docs/modules/design.md, python engine/tools/design.py): call sends any of the channel's
commands, frame returns the window's picture, and window, view, panel_click, scenario, again,
start_here, style, hud, paths, route and the fx tools are its own. The connection is dropped after a minute without a call.

Registered a third time as "hyperion-studio" on 27119 for Hyperion Studio, HyperionStudio.exe
(docs/modules/studio.md, python engine/tools/studio.py): the studio_* tools read and edit the mod
open in it through the same commands its panels use, each edit named as Claude's in its history.
"""
import base64
import json
import os
import sys
import threading
import time

from mcp.server.fastmcp import FastMCP, Image
from websockets.exceptions import ConnectionClosed

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hyperion_ctl import PORT, UNITS, Client, unit_id  # noqa: E402

mcp = FastMCP("hyperion")
_client = None
_lock = threading.Lock()
_idle = None
IDLE_S = 60.0


def _port():
    return int(os.environ.get("HYPERION_CONTROL_PORT", PORT))


def _unreachable(port, e):
    if port == 27118:
        return {"ok": False, "error": f"the design engine is not reachable on 127.0.0.1:{port}: {e}. Start it with "
                                      "python engine/tools/design.py."}
    return {"ok": False, "error": f"Hyperion is not reachable on 127.0.0.1:{port}: {e}. Start it with "
                                  "--control, with Debug Tools active, or on the canvas."}


def _drop():
    """The connection closed after a minute without a call."""
    global _client
    with _lock:
        if _client is not None:
            try:
                _client.close()
            except Exception:  # noqa: BLE001
                pass
            _client = None


def _call(cmd, timeout=10.0, **args):
    """The game's answer to cmd. A connection found closed is made again and the command sent
    once more, since the game never had it; after a timeout it is not sent again."""
    global _client, _idle
    port = _port()
    with _lock:
        if _idle is not None:
            _idle.cancel()
        try:
            for attempt in range(2):
                fresh = _client is None
                if fresh:
                    try:
                        _client = Client(port)
                    except Exception as e:  # noqa: BLE001  (refused, no key)
                        return _unreachable(port, e)
                try:
                    return _client.call(cmd, timeout=timeout, **args)
                except ConnectionClosed as e:
                    _client = None
                    if fresh or attempt == 1:
                        return _unreachable(port, e)
                except Exception as e:  # noqa: BLE001  (a timeout: the command may have run)
                    _client = None
                    return {"ok": False, "error": f"no answer to {cmd}: {e}"}
        finally:
            _idle = threading.Timer(IDLE_S, _drop)
            _idle.daemon = True
            _idle.start()


@mcp.tool()
def status() -> dict:
    """Where the game is: "front end", "menus" or "game"; the frame; paused; whether this is
    Debug Tools' canvas; whether a sandbox command changed it; the map's size in pixels."""
    return _call("status")


@mcp.tool()
def list_values() -> dict:
    """The values that can be changed live, each with its value, range, default and help."""
    return _call("list")


@mcp.tool()
def set_value(name: str, value: float) -> dict:
    """Sets a live value by name (list_values names them). Presentation only."""
    return _call("set", name=name, value=value)


@mcp.tool()
def snapshot(name: str = "") -> list:
    """Takes the next frame Hyperion shows and returns it as an image, with its path: the
    game at its own size, or the front end at the window's size."""
    answer = _call("snapshot", name=name or None)
    if not answer.get("ok"):
        return [str(answer)]
    return [f"{answer['path']} ({answer.get('width')}x{answer.get('height')})", Image(path=answer["path"])]


@mcp.tool()
def pause() -> dict:
    """Pauses a single-player game."""
    return _call("pause")


@mcp.tool()
def resume() -> dict:
    """Resumes a paused single-player game."""
    return _call("resume")


@mcp.tool()
def step(frames: int = 1) -> dict:
    """Runs a paused game on by a number of frames (1 to 10000) and pauses it again."""
    return _call("step", frames=frames, timeout=10.0 + frames / 10.0)


@mcp.tool()
def camera(x: int, y: int) -> dict:
    """Centres the view on a point of the map, in pixels (status gives the map's size)."""
    return _call("camera", x=x, y=y)


@mcp.tool()
def canvas(tileset: str = "jungle") -> dict:
    """From the front end's home screen, starts Debug Tools' canvas on a tileset: jungle (its
    grass, the default), badlands, platform, install, ashworld, desert, ice or twilight."""
    return _call("canvas", tileset=tileset)


SPAWN_HELP = ("On the canvas only: makes units at a point (map pixels) for a player (0 is the west camp, the "
              "player's; 1 the east camp, the computer's), and with attack_x/attack_y sends them to attack-move "
              "there. unit is a units.dat number or a name: " + ", ".join(sorted(UNITS)) + ". This changes the "
              "game, which is then marked as changed.")


@mcp.tool(description=SPAWN_HELP)
def spawn(unit: str, x: int, y: int, player: int = 0, count: int = 1, attack_x: int = -1, attack_y: int = -1) -> dict:
    try:
        n = unit_id(unit)
    except ValueError as e:
        return {"ok": False, "error": str(e)}
    attack = [attack_x, attack_y] if attack_x >= 0 and attack_y >= 0 else None
    return _call("spawn", unit=n, x=x, y=y, player=player, count=count, attack=attack)


@mcp.tool()
def select(x: int, y: int, radius: int = 96, player: int = 0) -> dict:
    """In a single-player game: selects a player's units in the square radius game pixels round a
    point (map pixels) by a drag box's rules (its own units without its buildings when there are
    units, else one unit), to see what a selection draws (health bars, circles) without the
    mouse."""
    return _call("select", x=x, y=y, radius=radius, player=player)


@mcp.tool()
def lights() -> dict:
    """The 2D lights' table: each image of images.dat that gives light (by its place), its
    name, colour, radius in game pixels, intensity and flicker; where the table came from;
    and how many lights the last map pass lit. The lights show with the values "effects"
    and "lights" on and the GPU scene running."""
    return _call("lights")


@mcp.tool()
def light(image: int, color: str = "", radius: float = -1, intensity: float = -1, flicker: float = -1,
          name: str = "", remove: bool = False) -> dict:
    """Changes one entry of the lights' table, or adds it: color as "#rrggbb", radius in
    game pixels (1 to 512), intensity (0 to 8), flicker (0 to 1); a field left out stays as
    it is. remove takes the image out of the table. Seen at once; lights_save keeps it."""
    args = {"image": image, "color": color or None, "name": name or None,
            "radius": radius if radius >= 0 else None, "intensity": intensity if intensity >= 0 else None,
            "flicker": flicker if flicker >= 0 else None, "remove": True if remove else None}
    return _call("light", **args)


@mcp.tool()
def dialogs() -> dict:
    """The open dialogs, each with its name when it is one of the console's or the
    screen edges' (minimap, statdata, statport, statcmd, textbox, statres, statlb), its text
    and its rectangle on the screen in game pixels (left, top, right, bottom)."""
    return _call("dialogs")


@mcp.tool()
def lights_save() -> dict:
    """Writes the lights' table to lights.json in the game's data folder, which Hyperion
    reads in place of its built-in table from then on."""
    return _call("lights_save")


@mcp.tool()
def looks() -> dict:
    """The looks: each one's name, the palette entries it takes (rule: blues, greens or all),
    what it is drawn as in full colour (material: crystal or gas), its ramp of six colours,
    the images it covers (none for all of a sprite's), the unit types that have it, its
    clarity, shine, glow and sparkle, its plume (height, density, rise, the images it
    replaces, its vents) and its light on the ground; where they came from, and the
    looks.json that looks_save writes. rich-gold and rich-violet are built in."""
    return _call("looks")


@mcp.tool()
def look(name: str, rule: str = "", material: str = "", ramp: list[str] | None = None,
         images: list[int] | None = None, units: list[int] | None = None, clarity: float = -1,
         shine: float = -1, glow: float = -1, sparkle: float = -1, plume: dict | None = None,
         light: dict | None = None, aura: dict | None = None, zones: list[dict] | None = None,
         circle: bool | None = None) -> dict:
    """Changes the look of that name, or adds it (the gold one's ramp and values, rule "all",
    material "crystal", no images, so all of a sprite's, and no units):
    rule "blues", "greens" or "all"; material "crystal" or "gas"; ramp six "#rrggbb", dark to
    light; images and units as places in images.dat and units.dat; clarity, shine, glow and
    sparkle 0 to 1; plume {height, density, rise, replaces, vents: [{image, x, y, strength,
    width, frame}]}, x and y in the image's frame, frame left out for any; light {color,
    radius, strength, x, y}. A field left out stays as it is. Seen at once; looks_save keeps it.

    The design engine has more. rule also "none" (the image keeps its colours), "reds", "golds",
    "greys", "skins", "hue" or "entries"; material also "paint" (the ramp as the art is shaded) or
    "glow" (the ramp added as light). zones: one to four, the first being the rule, material and
    ramp above, each {rule, material, ramp or color (one "#rrggbb" made a ramp), hue {from, to
    (degrees), chroma, lightMin, lightMax} for rule "hue", entries [palette entries] for rule
    "entries", except [entries], mix 0 to 1 over the image's own colours, fit "spread" or "keep"};
    an entry goes to the first zone that takes it. circle: whether a selection's circle takes the
    look too (false unless given). aura: {reach 0 to 8 (0 none), width, gap, falloff, inner,
    color, mid, core, mode "fixed" | "ramp" | "team" | "cycle", strength, opacity, blend "add" |
    "over", speed, pulse, pulseRate, flicker, crackle, crackleSize, sparks, embers, bands,
    bandSpeed, steps 2 to 8, dither, block 1 or 2, images "all" | "main", overUnits}: a line of
    light round the unit and a glow past it, in the game's pixels. A unit's paint and its aura
    are apart: a look given by hand that only has an aura (rule "none") leaves the paint it has.
    The templates in design/looks/templates/ hold looks of each kind."""
    args = {"name": name, "rule": rule or None, "material": material or None, "ramp": ramp, "images": images,
            "units": units, "clarity": clarity if clarity >= 0 else None, "shine": shine if shine >= 0 else None,
            "glow": glow if glow >= 0 else None, "sparkle": sparkle if sparkle >= 0 else None, "plume": plume,
            "light": light, "aura": aura, "zones": zones, "circle": circle}
    return _call("look", **args)


@mcp.tool()
def look_player(player: int, look: str = "") -> dict:
    """The design engine's: gives each of a player's units a look (player 0 to 11, a look's name,
    or "" to take it away), after a look given by hand or by rich bases and before a type's. It
    stays from game to game. The Painter panel paints a player this way, its look painter-p<slot>
    with the player's colour set by the colour command. look_takes {name} gives what a look takes
    of the palette, look_colours {unit | player | type} the palette entries units draw with, and
    looks_palette the palette, through call."""
    return _call("look_player", player=player, look=look)


@mcp.tool()
def look_unit(unit: int, look: str = "") -> dict:
    """Gives one unit a look by hand (unit is its id, as units gives it): a look's name,
    "none" for none, or "" to leave it to the game (rich bases, or its type's look). It goes
    with the unit until another unit takes its place, and ends with the game."""
    return _call("look_unit", unit=unit, look=look)


@mcp.tool()
def looks_save() -> dict:
    """Writes the looks to looks.json in the active mod's folder, or in the game's data folder
    when no mod is active, which Hyperion reads over its built-in looks as each game begins."""
    return _call("looks_save")


@mcp.tool()
def call(cmd: str, args: dict | None = None, timeout: float = 30.0) -> dict:
    """Sends any of the channel's commands with its arguments and returns the answer. An unknown
    command's error lists every command there is. On the design engine nothing is held back:
    units, players, spawn, select, click, press, design_status, design_window, design_view,
    design_click, design_grab, design_hud, design_paths, paths, fx_list, fx_load, fx_play, fx_stop,
    design_scenario, design_launch, design_again, design_start_here, design_style, design_quit and
    the rest. "id" is the channel's own, numbering the request, so no command takes
    an argument of that name."""
    return _call(cmd, timeout=timeout, **(args or {}))


@mcp.tool()
def frame(panels: bool = False) -> list:
    """The design engine's window as it shows it: the game with its HUD, path lines and effects,
    and with panels its panels too. Saved as a PNG in its data folder's grabs, which stay on
    this machine, and returned as an image with its size."""
    port = _port()
    answer = _call("design_grab", panels=True if panels else None)
    if not answer.get("ok"):
        return [json.dumps(answer)]
    deadline = time.time() + 10.0
    while time.time() < deadline:
        state = _call("design_grabbed")
        if state.get("state") == "saved":
            with open(state["path"], "rb") as f:
                data = f.read()
            meta = {k: state[k] for k in ("path", "width", "height")}
            return [json.dumps(meta), Image(data=data, format="png")]
        if state.get("state") == "failed" or not state.get("ok"):
            return [json.dumps(state)]
        time.sleep(0.05)
    return [str(_unreachable(port, "no frame was drawn for the grab in 10 s"))]


@mcp.tool()
def record(around: str = "", unit: int = -1, x: int = -1, y: int = -1, seconds: float = 4.0, fps: float = 25.0,
           loop: bool = True, fade: float = 0.5, size: int = 192, scale: float = 0.0, format: str = "gif",
           marks: bool = False, name: str = "clip", timeout: float = 120.0) -> dict:
    """A clip of the design engine's game to share, made and waited for: a square of size map
    pixels round a unit (around "unit": unit by its index, else the first selected) or a point
    (x, y), or the whole view (around "view"), without the HUD, the console, the cursor or the
    selection's marks (marks: true keeps the marks). It loops: the last fade seconds are taken past
    its end and faded into its start. scale is the clip's pixels to a map pixel, 0 as the window
    shows it. format is gif, webm, mp4 (by ffmpeg on the PATH) or png (the frames). The clip is
    saved in the engine's data folder's clips/, on this machine; its path is in the answer."""
    fields = {"seconds": seconds, "fps": fps, "loop": loop, "fade": fade, "size": size, "scale": scale,
              "format": format, "marks": marks, "name": name}
    if around:
        fields["around"] = around
    if unit >= 0:
        fields["unit"] = unit
    if x >= 0 and y >= 0:
        fields["x"], fields["y"] = x, y
    answer = _call("design_record", **fields)
    if not answer.get("ok"):
        return answer
    deadline = time.time() + timeout
    while time.time() < deadline:
        state = _call("design_recorded")
        if state.get("state") in ("done", "failed", "none") or not state.get("ok"):
            return state
        time.sleep(0.25)
    return _call("design_recorded")


@mcp.tool()
def window(w: int = 0, h: int = 0, fullscreen: bool | None = None, maximized: bool = False) -> dict:
    """The design engine's window: a size in pixels (w x h), full screen or not, or maximized;
    nothing given, how it is now."""
    return _call("design_window", w=w or None, h=h or None, fullscreen=fullscreen,
                 maximized=True if maximized else None)


@mcp.tool()
def view(mode: str = "", zoom: float = 0, zoom_max: float = 0) -> dict:
    """How the game and the panels share the window: "beside" (panels docked round the game),
    "over" (the game fills the window, the panels over it) or "game" (the game alone, F11);
    the camera's zoom (1 is the game's own scale, more shows more of the map), and how far it
    may zoom out (1 to 8). Nothing given, how it is now."""
    return _call("design_view", mode=mode or None, zoom=zoom or None, zoomMax=zoom_max or None)


@mcp.tool()
def panel_click(x: int, y: int, right: bool = False, wheel: float = 0) -> dict:
    """Clicks at a point of the design engine's window, in its pixels (frame gives its size),
    as the mouse does, so the panels see it: a panel's button, a list, the map. wheel turns the
    wheel there instead."""
    return _call("design_click", x=x, y=y, button=3 if right else None, wheel=wheel or None)


@mcp.tool()
def hud(design: str = "", layout: str = "", size: str = "", material: str = "", light: str = "",
        hidden: bool | None = None) -> dict:
    """The design engine's HUD: design ("classic", "mech" or a user design's id, as the answer
    lists them), layout ("span", "flank", "flank-right"), size ("full", "min"), material
    ("gunmetal", "graphite", "blackout"), light, and hidden to hide it with the classic console.
    A field left out stays as it is; with none, the current picks and the designs."""
    return _call("design_hud", design=design or None, layout=layout or None, size=size or None,
                 material=material or None, light=light or None, hidden=hidden)


@mcp.tool()
def scenario(load: str = "", save: str = "", game: dict | None = None, units: list | None = None,
             effects: list | None = None, clear: bool = False, reveal: bool | None = None,
             paused: bool | None = None, style: str = "", launch: bool = False, list_them: bool = False) -> dict:
    """The design engine's scenarios (design/scenarios/): a game set up the same way each time.
    load starts one: its game, then its units with their orders, reveal, camera, effects and
    style, then its start point, which again goes back to. Or build one: game is {canvas:
    tileset, seed?} or {map: path, type?: "Melee", sub?, speed?, hyperion?, seed?, watch?, seats?:
    [{race, level?, team?, start?, handicap?}], mutators?: {startMinerals, build, ...}}; units are
    spawn's fields, [{unit, x, y, player?, count?, attack?: [x, y] | move?: [x, y]}], unit a number
    or a name (marine, or a map name such as terran_marine); effects are fx_play's, [{effect, at:
    [x, y], to?, loop?, params?}]. clear empties its units and effects first; launch starts its game
    now; save writes it. list_them lists the saved ones. With nothing, its state."""
    fields = {}
    if list_them:
        fields["list"] = True
    if load:
        fields["load"] = load
    if game is not None:
        fields["game"] = game
    if clear:
        fields["clear_units"] = True
        fields["clear_effects"] = True
    if units:
        fields["units"] = [dict(u, unit=unit_id(u["unit"])) if "unit" in u else u for u in units]
    if effects:
        fields["effects"] = effects
    if reveal is not None:
        fields["reveal"] = reveal
    if paused is not None:
        fields["paused"] = paused
    if style:
        fields["style"] = style
    if launch:
        fields["launch"] = True
    if save:
        fields["save"] = save
    return _call("design_scenario", **fields)


@mcp.tool()
def again() -> dict:
    """Puts the game back at the scenario's start point (F9): the same fight plays again, under
    whatever was changed since (a style, live values, looks, lights). Without a start point in this
    game, the scenario's game is made again."""
    return _call("design_again")


@mcp.tool()
def give(minerals: int = 0, gas: int = 0, player: int = 0) -> dict:
    """Adds minerals and gas to a player's in the game being tried (player 0, the one playing, when
    not given): a canvas starts its players with none. Marks the game as changed, as a spawn does."""
    return _call("design_give", player=player, minerals=minerals, gas=gas)


@mcp.tool()
def start_here() -> dict:
    """The start point again goes back to, taken at the frame now (Shift+F9)."""
    return _call("design_start_here")


@mcp.tool()
def style(apply: str = "", save: str = "", a: str = "", b: str = "", flip: bool = False,
          list_them: bool = False) -> dict:
    """The design engine's styles (design/styles/): how the game is drawn, as one named set: the
    picture's Settings rows, the live values that differ from their defaults (post effects,
    lights, HD, looks' tuning), the lights, the looks and the HUD. save takes the picture as it is
    now; apply puts one back; a and b set the two flip swaps between (F8). With nothing, the
    state."""
    fields = {}
    if list_them:
        fields["list"] = True
    if save:
        fields["save"] = save
    if apply:
        fields["apply"] = apply
    if a:
        fields["a"] = a
    if b:
        fields["b"] = b
    if flip:
        fields["flip"] = True
    return _call("design_style", **fields)


@mcp.tool()
def paths(on: bool = True, units: str = "selected", player: int = -1) -> dict:
    """Draws each unit's way in the design engine's window: where it steers, its waypoints, the
    regions after them (dashed), its goal and its order's target. units is "selected", "all"
    or a JSON list of unit ids; player limits it to one player's."""
    which = json.loads(units) if units.startswith("[") else units
    return _call("design_paths", on=on, units=which, player=player if player >= 0 else None)


@mcp.tool()
def route(units: str = "selected", player: int = -1) -> dict:
    """The units' ways as points in map pixels, each [x, y, kind] with kind unit, steer,
    waypoint, region (an estimate), goal or target: what paths draws."""
    which = json.loads(units) if units.startswith("[") else units
    return _call("paths", units=which, player=player if player >= 0 else None)


@mcp.tool()
def fx_list() -> dict:
    """The effects loaded from design/fx/luau/, each with its parameters, and any errors."""
    return _call("fx_list")


@mcp.tool()
def fx_load() -> dict:
    """Loads the effects from design/fx/luau/ again, after one has been changed."""
    return _call("fx_load")


@mcp.tool()
def fx_play(effect: str, x: float = -1, y: float = -1, unit: int = -1, to_x: float = -1, to_y: float = -1,
            to_unit: int = -1, radius: float = -1, params: dict | None = None, loop: bool = False,
            seed: int = -1, team: int = -1) -> dict:
    """Plays an effect in a single-player game: at a map point (x, y) or a unit, the view's
    middle when neither is given; to a point or unit for the effects that go somewhere; with
    its params (fx_list names them), looping or once. Answers the playing effect's number."""
    args = {"effect": effect, "params": params, "loop": True if loop else None}
    if x >= 0 and y >= 0:
        args["at"] = [x, y]
    if to_x >= 0 and to_y >= 0:
        args["to"] = [to_x, to_y]
    for k, v in (("unit", unit), ("toUnit", to_unit), ("seed", seed), ("team", team)):
        if v >= 0:
            args[k] = v
    if radius >= 0:
        args["radius"] = radius
    return _call("fx_play", **args)


@mcp.tool()
def fx_stop(playing: int = -1) -> dict:
    """Stops a playing effect by the number fx_play gave, or every one."""
    return _call("fx_stop", playing=playing if playing >= 0 else None)


@mcp.tool()
def click(x: int, y: int, right: bool = False, double: bool = False) -> dict:
    """Clicks at a point of the game's frame, in game pixels (design_status gives the view's
    size), as the game takes a click: the left button, or the right with right. The panels never
    see it; panel_click is the mouse's own way."""
    return _call("click", x=x, y=y, right=True if right else None, double=True if double else None)


@mcp.tool()
def press(key: str = "", vk: int = 0, char: str = "") -> dict:
    """Presses a key as the keyboard would: a letter or digit, or "enter", "escape", "tab",
    "up", "down", "left" or "right"; or a Windows virtual key's number as vk; char is typed with
    it (a chat line's letters)."""
    return _call("press", key=key or None, vk=vk or None, char=char or None)


# ---- Hyperion Studio (hyperion-studio, 27119) ----------------------------------------------------------
@mcp.tool()
def studio_status() -> dict:
    """The mod open in Hyperion Studio: its id, folders, unsaved edits, the history's length, and an
    autosave waiting to be taken back (studio_recover) after a crash."""
    return _call("studio_status")


@mcp.tool()
def studio_new_mod(name: str, id: str = "", by: str = "", line: str = "") -> dict:
    """Makes a new mod among the installed ones, with a mod.json, a data folder and no records yet,
    and opens it: its id from its name ("Void Clans" gives void-clans) unless given."""
    return _call("studio_new_mod", name=name, id=id or None, by=by or None, line=line or None)


@mcp.tool()
def studio_open(mod: str = "", folder: str = "") -> dict:
    """Opens a mod in the studio: an installed mod by its id, or a folder that holds a mod.json. The
    mods it depends on open with it, read only. Unsaved edits of the mod open before are dropped
    (an autosave keeps them)."""
    return _call("studio_open", mod=mod or None, folder=folder or None)


@mcp.tool()
def studio_list(table: str = "units", classic: bool = False) -> dict:
    """A table's records in the open mod (units, weapons, flingy, sprites, images, orders, techs,
    upgrades, sounds or portraits): its own, its edits of
    classic rows, and its dependencies', each with its row and its base; with classic, the classic
    rows' names too."""
    return _call("studio_list", table=table, classic=True if classic else None)


@mcp.tool()
def studio_get(table: str, name: str) -> dict:
    """A record: every field with its value as the loader would build it, and where the value comes
    from (own, base, edit, classic, with the record or classic row that gives it). A classic row's
    types.json name (terran_marine) works too."""
    return _call("studio_get", table=table, name=name)


@mcp.tool()
def studio_set(table: str, name: str, field: str, value: int | float | str | bool | list | dict | None = None,
               revert: bool = False, merge: str = "") -> dict:
    """Sets one field of the mod's record, as an undoable edit named as Claude's. The value is what
    types.json takes: a number, a name of another record or classic row, a list. revert takes the
    field back to its base's. merge joins edits of one gesture into one undo step."""
    if revert:
        return _call("studio_set", table=table, name=name, field=field, revert=True, sender="claude")
    return _call("studio_set", table=table, name=name, field=field, value=value, merge=merge or None,
                 sender="claude")


@mcp.tool()
def studio_create(table: str, name: str, base: str = "", edit: str = "", record: dict | None = None) -> dict:
    """Makes a record in the open mod: from a base (a classic row's name or one of the mod's), as an
    edit of a classic row (edit), or from a whole record. The name takes letters, digits and _."""
    return _call("studio_create", table=table, name=name, base=base or None, edit=edit or None, record=record,
                 sender="claude")


@mcp.tool()
def studio_delete(table: str, name: str) -> dict:
    """Takes a record of the open mod away, as an undoable edit."""
    return _call("studio_delete", table=table, name=name, sender="claude")


@mcp.tool()
def studio_raw(table: str, name: str, text: str) -> dict:
    """Replaces a record of the open mod with JSON text, as its Raw view does; a parse error is
    refused with its line."""
    return _call("studio_raw", table=table, name=name, text=text, sender="claude")


@mcp.tool()
def studio_undo() -> dict:
    """Undoes the last group of edits, whoever made them."""
    return _call("studio_undo")


@mcp.tool()
def studio_redo() -> dict:
    """Does the last undone group of edits again."""
    return _call("studio_redo")


@mcp.tool()
def studio_history(count: int = 50) -> dict:
    """The edits so far, newest last: who made each (you, claude, a tool), what it did, and whether
    it is done or undone."""
    return _call("studio_history", count=count)


@mcp.tool()
def studio_save() -> dict:
    """Writes the open mod's files that changed (types.json, mod.json, looks.json, iscript.txt)."""
    return _call("studio_save")


@mcp.tool()
def studio_push(replace: str = "all") -> dict:
    """Pushes the open mod's working data into the studio's canvas game (mod_push), so a change is
    tried at once: replace "all" turns the units of earlier pushes into the newest, "last" those of
    the push before, "" none. The answer comes later: studio_pushes gives each push's."""
    return _call("studio_push", replace=replace or None)


@mcp.tool()
def studio_pushes() -> dict:
    """Each push and the game's answer: pushed, with the records it made, or refused, with why."""
    return _call("studio_pushes")


@mcp.tool()
def studio_verify() -> dict:
    """Checks the studio's values for each of the mod's records against the rows the loader built
    in the last push: same, or the differences."""
    return _call("studio_verify")



@mcp.tool()
def studio_show(table: str = "", name: str = "", view: str = "", level: str = "", workspace: str = "",
                dock: str = "", game: str = "", filter: str = "") -> dict:
    """Points the studio's window at something, as the modder would click it: a record opened (table,
    name, view such as "Numbers" or "Card and tech", level simple, detailed or raw), a workspace
    (make, tables, art, looks, effects, cards, test, checks, mod), a table for Tables without a name,
    the dock's tab (game, problems, history, output, closed), where the game shows (dock, preview,
    large), the fields' filter."""
    args = {"table": table, "name": name, "view": view, "level": level, "workspace": workspace, "dock": dock,
            "game": game, "filter": filter}
    return _call("studio_show", **{k: v for k, v in args.items() if v})


@mcp.tool()
def studio_put(doc: str, path: list, value=None) -> dict:
    """Sets a member of one of the mod's documents ("types", "mod", "looks", "hud"), its parents made
    as needed, or takes it away when value is null: for what is no record of a table, such as
    types.json's "cards" and "races", mod.json's fields, or hud.json's console and minimap."""
    return _call("studio_put", doc=doc, path=path, value=value, sender="claude")


@mcp.tool()
def studio_files(ext: str = "") -> dict:
    """The files of the open mod's data folder, those ending in one of ext ("png,grp") when given."""
    return _call("studio_files", ext=ext or None)


@mcp.tool()
def studio_import(path: str, folder: str = "") -> dict:
    """Copies a file from anywhere into the mod's data folder (into folder, made as needed) and gives
    its path in the mod, to name in a record. A file already inside is named where it is."""
    return _call("studio_import", **{"from": path, "folder": folder or None})


@mcp.tool()
def studio_damage(weapon: str, target: str, level: int = 0) -> dict:
    """What a weapon does to a unit by the classic rules: damage an attack after armour and size, the
    attacks that kill it, the seconds that takes and the damage a second, at an upgrade level."""
    return _call("studio_damage", weapon=weapon, target=target, level=level)


@mcp.tool()
def studio_make_unit(name: str, base: str, label: str = "", race: str = "", at: str = "", copy_weapon: bool = True,
                     values: dict | None = None, button: dict | None = None) -> dict:
    """Makes a unit of the mod from a classic one, as one edit: name its key, label its shown name,
    at the unit that makes it, copy_weapon its own copy of the base's weapon, values fields to set,
    button {card, position 1-9, hotkey, text} its button on a card."""
    return _call("studio_make_unit", name=name, base=base, label=label or None, race=race or None, at=at or None,
                 copy_weapon=copy_weapon, values=values, button=button, sender="claude")


@mcp.tool()
def studio_actions(unit: str = "") -> dict:
    """The mod's actions (spells: an order, a tech and a weapon together) with their casters, and the
    classic spells a new one can start from."""
    return _call("studio_actions", unit=unit or None)


@mcp.tool()
def studio_make_action(unit: str, start_from: str, label: str = "", name: str = "", values: dict | None = None) -> dict:
    """Makes an ability for a unit from a classic spell (start_from, such as psionic_storm, lockdown,
    yamato_gun), as one edit: its order, tech, weapon, the cast button and the research button.
    values: energy, damage, max_range, inner/middle/outer_splash, damage_type, explosion, at (where it
    is researched), minerals, gas, research_time, hotkey, position, text, ai, target, give_energy."""
    return _call("studio_make_action", unit=unit, **{"from": start_from}, label=label or None, name=name or None,
                 values=values, sender="claude")


@mcp.tool()
def studio_delete_action(name: str) -> dict:
    """Takes an action of the mod away, its order, tech, unused weapon and buttons, as one edit."""
    return _call("studio_delete_action", name=name, sender="claude")


@mcp.tool()
def studio_action_research(name: str, at: str = "", minerals: int = 0, gas: int = 0, research_time: int = 0) -> dict:
    """Sets where an action of the mod is researched, as one edit: with at (a building, such as
    protoss_templar_archives), its tech needs that research before a caster can use it, costs
    minerals and gas and takes research_time frames (24 a second at Fastest), and a Research button
    goes on the building's card; with no at, it needs no research and its Research buttons go."""
    return _call("studio_action_research", name=name, at=at or None, minerals=minerals or None, gas=gas or None,
                 research_time=research_time or None, sender="claude")


@mcp.tool()
def studio_statuses(query: str = "", show: str = "", section: str = "") -> dict:
    """The mod's statuses (slowed, poisoned, stunned and the like: types.json "statuses") with what
    each does in a sentence and where it is used: weapons, actions' blocks and units' auras. show
    opens one in the Statuses workspace, section (changes, life, look or uses) scrolls to it."""
    return _call("studio_statuses", query=query or None, show=show or None, section=section or None)


@mcp.tool()
def studio_status_new(name: str = "", label: str = "", start_from: str = "", values: dict | None = None) -> dict:
    """Makes a status, as one edit: start_from blank, slow, poison, stun, haste, weaken, mending or a
    status of the mod to copy; values are its fields (duration seconds, stacking refresh/stack/ignore,
    max_stacks, move_speed %, armor, damage_dealt %, attack_cooldown %, cannot_move, cannot_attack,
    damage_per_second, life_per_second, overlay, overlay_by_size, start_image, end_image, icon)."""
    return _call("studio_status_new", name=name or None, label=label or None, **{"from": start_from or None},
                 values=values, sender="claude")


@mcp.tool()
def studio_status_set(name: str, field: str = "", value=None, values: dict | None = None, rename: str = "",
                      merge: str = "") -> dict:
    """Sets a status's field (value null takes it off) or several (values), or renames it (rename,
    its uses following), as one edit; merge joins edits of the same key into one undo step."""
    return _call("studio_status_set", name=name, field=field or None, value=value, values=values,
                 rename=rename or None, merge=merge or None, sender="claude")


@mcp.tool()
def studio_status_delete(name: str) -> dict:
    """Takes a status away with its uses (weapons' status, units' auras, blocks naming it), as one edit."""
    return _call("studio_status_delete", name=name, sender="claude")


@mcp.tool()
def studio_scripts(file: str = "") -> dict:
    """The mod's scripts (scripts/*.lua and *.luau of its data folder, docs/mod-data.md "Scripts"):
    each file's problems as the loader finds them (line, column, what) and its functions, each with
    where the mod names it (a unit's hook, an order's toggle, channel or autocast, a script block);
    the hooks and when the game calls them; the last run in the test game and the game's log."""
    return _call("studio_scripts", file=file or None)


@mcp.tool()
def studio_script_api() -> dict:
    """The names a script has (hx.units, hx.damage, hx.set and the rest, math, string, table), each
    with its arguments and a sentence, and the hooks with where each is named and when it is called."""
    return _call("studio_script_api")


@mcp.tool()
def studio_script_set(file: str, text: str, merge: bool = False) -> dict:
    """Sets a script's whole text, as one edit (merge joins it to the last few seconds' edit of the
    same file); the answer gives its problems as the loader finds them. file is a name in scripts/
    ending in .lua or .luau."""
    return _call("studio_script_set", file=file, text=text, merge=merge or None, sender="claude")


@mcp.tool()
def studio_script_new(file: str, text: str = "") -> dict:
    """Makes a script (.lua added when the name has no extension), from a short start unless text
    is given, as one edit."""
    return _call("studio_script_new", file=file, text=text or None, sender="claude")


@mcp.tool()
def studio_script_delete(file: str) -> dict:
    """Takes a script away, as one edit; the answer names its functions the mod still calls."""
    return _call("studio_script_delete", file=file, sender="claude")


@mcp.tool()
def studio_script_rename(file: str, to: str) -> dict:
    """Renames a script, as one edit."""
    return _call("studio_script_rename", file=file, to=to, sender="claude")


@mcp.tool()
def studio_script_show(file: str = "", function: str = "", line: int = 0, find: str = "") -> dict:
    """Shows the Scripts workspace at a file, at the line of a function, or at a line, with Find
    opened on a word when find is given."""
    return _call("studio_script_show", file=file or None, function=function or None, line=line or None,
                 find=find or None)


@mcp.tool()
def studio_script_run(function: str, unit: str = "selected") -> dict:
    """Pushes the mod into the test game and calls a function of its scripts there once, with
    e.unit and e.caster the unit selected in the game and e.target the second (unit "none" for
    neither). The result, the function's log lines and any error come in studio_scripts' run."""
    return _call("studio_script_run", function=function, unit=unit, sender="claude")


@mcp.tool()
def studio_weapon_status(weapon: str, status: str = "") -> dict:
    """Puts a status on every unit the weapon damages; an empty status takes it off. A classic
    weapon gets an edit record of the mod's."""
    return _call("studio_weapon_status", weapon=weapon, status=status or None, sender="claude")


@mcp.tool()
def studio_unit_aura(unit: str, aura: dict | None = None, remove: bool = False) -> dict:
    """Gives a unit a passive aura, {status, radius px, targets allies/enemies/own/all, every s,
    self, buildings}, or takes it off (remove)."""
    return _call("studio_unit_aura", unit=unit, aura=aura, remove=remove or None, sender="claude")


@mcp.tool()
def studio_action_blocks(name: str, blocks: list | None = None) -> dict:
    """Sets an action's blocks, the whole list, what its cast does in place of firing its weapon
    (docs/mod-data.md, "Action blocks"): fire, damage, heal, energy, status, remove_statuses, spawn,
    teleport, sound, show, wait, repeat. None or [] takes them off."""
    return _call("studio_action_blocks", name=name, blocks=blocks if blocks is not None else [], sender="claude")


@mcp.tool()
def studio_block_add(name: str, block: dict, path: list | None = None, at: int = -1) -> dict:
    """Adds a block ({"do": ...}) to an action's blocks, or inside the repeat or wait path names
    (indexes, [2] or [2, 0]), at a place (the end by default)."""
    return _call("studio_block_add", name=name, block=block, path=path, at=at if at >= 0 else None,
                 sender="claude")


@mcp.tool()
def studio_block_set(name: str, path: list, field: str, value=None) -> dict:
    """Sets one field of the block path names; value null takes it off."""
    return _call("studio_block_set", name=name, path=path, field=field, value=value, sender="claude")


@mcp.tool()
def studio_block_remove(name: str, path: list) -> dict:
    """Takes the block path names away, with the blocks inside it."""
    return _call("studio_block_remove", name=name, path=path, sender="claude")


@mcp.tool()
def studio_block_move(name: str, path: list, by: int) -> dict:
    """Moves the block path names among its list's, by places (-1 earlier, 1 later)."""
    return _call("studio_block_move", name=name, path=path, by=by, sender="claude")


@mcp.tool()
def studio_try(unit: str, against: str = "zealots", count: int = 0, yours: int = 0, units: list | None = None) -> dict:
    """Tries a unit of the mod in the canvas game: the mod pushed, the field cleared, the unit (yours
    of it) against zealots, marines, ultralisk or a unit's name (count of them), or units
    [{unit, count, player}]; a start point taken so studio_test again puts the fight back."""
    return _call("studio_try", unit=unit, against=against, count=count or None, yours=yours or None, units=units)


@mcp.tool()
def studio_test(again: bool = False, pause: bool = False, resume: bool = False, step: int = 0) -> dict:
    """The try's state and the units last seen; again puts it back at its start, pause, resume, or
    step frames while paused."""
    return _call("studio_test", again=again or None, pause=pause or None, resume=resume or None, step=step or None)


@mcp.tool()
def studio_matchup(units: list | None = None, against: list | None = None, per_cost: bool | None = None, budget: int = 0,
                   level: int = -1, closing: bool | None = None, show: bool = False) -> dict:
    """The Balance workspace's matchup grid, worked out by the classic damage rules: each row unit (the
    mod's units that fight and the picked rows, or units) against each column (those and the picked
    classic units, or against). Each cell: winner (unit, against, both, neither), margin (the winner's
    life and shields left, 0 to 1), time, counts and those left, attacks and time_to_kill alone each
    way, damage a second. per_cost: as many of each as budget (600, 1200, 2400...) buys; level 0 or 3
    upgrades both sides; closing: the shorter reach walks in first. show puts the workspace up with
    these settings. The numbers follow the mod's edits."""
    return _call("studio_matchup", units=units, against=against, per_cost=per_cost, budget=budget or None,
                 level=level if level >= 0 else None, closing=closing, show=show or None)


@mcp.tool()
def studio_matchup_against(add: str | list | None = None, remove: str | list | None = None, reset: bool = False,
                           rows: bool = False) -> dict:
    """The Balance grid's picked columns (classic units to fight against): a unit or list added or
    taken away, or reset to the defaults (two of each race); with rows, the picked rows instead. The
    answer has the picked lists and the grid's rows and columns."""
    return _call("studio_matchup_against", add=add, remove=remove, reset=reset or None, rows=rows or None)


@mcp.tool()
def studio_scenarios() -> dict:
    """The mod's own scenarios for Try it (its scenarios folder), each as its file holds it: its
    sides' units and counts, the side the tried unit joins (tried) and each side's order."""
    return _call("studio_scenarios")


@mcp.tool()
def studio_scenario_save(name: str, sides: list | None = None, tried: int = 0) -> dict:
    """Saves a scenario of the mod's at once, in place of one of the same name: sides, one or two of
    {units: [{unit, count}], order?: "attack"}, tried the place of the side the tried unit joins;
    without sides, the last try's. studio_try takes it by name as against."""
    return _call("studio_scenario_save", name=name, sides=sides, tried=tried if sides else None)


@mcp.tool()
def studio_scenario_delete(name: str) -> dict:
    """Takes a scenario of the mod's away (its file)."""
    return _call("studio_scenario_delete", name=name)


@mcp.tool()
def studio_ab_variant(unit: str, field: str = "", value: float | None = None, table: str = "", name: str = "",
                      revert: bool = False, clear: bool = False) -> dict:
    """Version B of a unit, kept in the studio and not in the mod until studio_ab_keep: a number
    field of the unit (table "units", the default) or of a weapon it fires (table "weapons", name
    the weapon, its ground weapon when not given) set to value, or revert to take that change out;
    clear empties B first. With no field, B as it is. The answer has each change with A's value and
    B's, and the records B may change."""
    return _call("studio_ab_variant", unit=unit, field=field or None, value=value, table=table or None, name=name or None,
                 revert=revert or None, clear=clear or None)


@mcp.tool()
def studio_ab_run(unit: str, against: str = "zerg_zergling", count: int = 6, yours: int = 1, units: list | None = None,
                  seeds: int = 10, first_seed: int = 1, seconds: int = 60, attack: bool = False, theirs_attack: bool = True,
                  b: bool | None = None) -> dict:
    """Plays a fight out in the canvas game, seed after seed: yours of the unit against count of
    against (or units, [{unit, count, player}], player 0 joining the unit's side). The canvas game
    starts again, paused on its first frame, the working mod and B are pushed (B as <unit>__b), and
    each fight starts from the same snapshot with the game's generator seeded, A then B for each
    seed; the same seeds play the same fights. attack sends the unit's side at the others; theirs
    come at it unless theirs_attack is false. b: A and B (the default when B has changes) or A
    alone. A fight ends when a side has nothing left or after seconds. Answers at once;
    studio_ab gives the progress and the results."""
    return _call("studio_ab_run", unit=unit, against=against, count=count, yours=yours, units=units, seeds=seeds,
                 first_seed=first_seed, seconds=seconds, attack=attack or None, theirs_attack=None if theirs_attack else False, b=b)


@mcp.tool()
def studio_ab(show: bool = False) -> dict:
    """The last fights played out: state (starting, pushing, playing, done, stopped, refused) and
    why, each fight (version A or B, seed, winner yours, theirs or neither, seconds, units and life
    left, damage dealt and taken), and the sums for A and B (win_share, time_to_win, life_left as a
    share of the start, damage_dealt and damage_taken a fight). show puts Balance's Played out page up."""
    return _call("studio_ab", show=show or None)


@mcp.tool()
def studio_ab_stop() -> dict:
    """Stops the fights being played out, keeping those played."""
    return _call("studio_ab_stop")


@mcp.tool()
def studio_ab_keep(unit: str) -> dict:
    """Writes B's changes for the unit into the mod's records as one edit (one undo takes it back);
    a classic weapon B changed gets an edit record. B is then empty."""
    return _call("studio_ab_keep", unit=unit)


@mcp.tool()
def studio_sound_set(sound: str, field: str, value: str | float | None = None, revert: bool = False) -> dict:
    """One field of a sound: subtitle (the words shown as a unit says it), priority (0 to 255: with
    eight sounds playing, a sound stops the lowest below it), flags (2 one per unit type, 16 once at a
    time, 32 not cut off), min_interval (milliseconds before it plays again; 80 when not given),
    length_adjust or min_volume. A classic sound is changed through an edit of it, made in the same
    undo. revert takes the field back to its base's."""
    return _call("studio_sound_set", sound=sound, field=field, value=value, revert=revert or None)


@mcp.tool()
def sounds(play: str | int | None = None) -> dict:
    """In a game: the eight sound slots playing, each one's sound and priority (-1 free), after
    playing a sound first by its number or a mod's name, as the interface plays one (started says
    whether it was heard: a full set of slots of higher priority, or the sound's least gap, keep it out)."""
    return _call("sounds", play=play)


@mcp.tool()
def studio_race_play(race: str, map: str, computers: int = 1, computer_race: str = "") -> dict:
    """Plays a race on a real map: the mod saved, then Hyperion.exe started as a game of its own,
    the player of the race (the mod's, by its record's name or the name players see, or a classic
    one) against computers of computer_race (a classic race or one of the mod's; the game's pick
    when empty). map is a map's path or its file's name among the studio's maps. Answers the
    game's process and control port; studio_race_game follows it."""
    return _call("studio_race_play", race=race, map=map, computers=computers, computer_race=computer_race or None)


@mcp.tool()
def studio_race_game(resume: bool = False) -> dict:
    """The game studio_race_play started, as its live channel last said: running, state, frame,
    and each player's resources and units by type (type, name, count). resume lets a game go on
    that holds while its window has no focus."""
    return _call("studio_race_game", resume=resume or None)


@mcp.tool()
def studio_race_stop() -> dict:
    """Ends the game studio_race_play started, by its process alone."""
    return _call("studio_race_stop")

@mcp.tool()
def studio_play(name: str = "", file: str = "") -> dict:
    """Plays a sound once in the studio, as the game decodes it: a sound record by name (a classic
    one from the game's files, a mod's from its data folder), or a file of the data folder. The
    answer says where it came from, its kind, seconds, rate and channels."""
    return _call("studio_play", name=name or None, file=file or None)


@mcp.tool()
def studio_unit_sounds(unit: str, from_unit: str, portrait: bool = True) -> dict:
    """A unit of the mod speaks as another unit: its ready, selected, annoyed and acknowledged lines
    (and its portrait, unless portrait is false) set to that unit's, in one edit."""
    return _call("studio_unit_sounds", unit=unit, **{"from": from_unit}, portrait=portrait, sender="claude")


@mcp.tool()
def studio_unit_sound_add(unit: str, line: str, path: str) -> dict:
    """A sound file (WAV, Ogg, Opus, MP3 or FLAC) brought into the data folder's sounds/<unit>/ and made
    a sound record <unit>_<line>_NN at the end of the unit's line: ready, what, pissed or yes. The
    ready line says one sound, so a new one takes the old one's place. One edit."""
    return _call("studio_unit_sound_add", unit=unit, line=line, **{"from": path}, sender="claude")


@mcp.tool()
def studio_unit_sound_remove(unit: str, line: str, sound: str) -> dict:
    """One of the unit's own sounds taken out of a line, its record deleted and the line's first and
    last set again (back to its base's when none of its own is left). One edit."""
    return _call("studio_unit_sound_remove", unit=unit, line=line, sound=sound, sender="claude")


@mcp.tool()
def studio_unit_portrait(unit: str) -> dict:
    """A portrait of the unit's own, named after it (videos under portraits/<unit>/idle and talk),
    made and given to the unit, in one edit. Bring its videos in with studio_portrait_video."""
    return _call("studio_unit_portrait", unit=unit, sender="claude")


@mcp.tool()
def studio_portrait_video(portrait: str, kind: str, n: int, path: str) -> dict:
    """A video copied in as a portrait's idle (n 0 to 3) or talking (n 0 to 2) video, under the name
    the loader looks for (<path><n>.webm and so on); the portrait given a path when it has none."""
    return _call("studio_portrait_video", portrait=portrait, kind=kind, n=n, **{"from": path}, sender="claude")


@mcp.tool()
def studio_look_set(name: str, field: str = "", value=None, remove: bool = False, merge: str = "") -> dict:
    """A look of looks.json changed: a field (rule, material, ramp, units, images, clarity, shine,
    glow, sparkle, plume.height, plume.vents, light.color and so on; units and images by record name,
    such as "terran_marine", or by number) set to value, or back to its
    default with no value; the whole look with value an object and no field; taken out with
    remove (a built-in one goes back to the engine's). The look is made when it is not there, and
    the canvas game shows it at once. Fields: engine/docs/modules/render.md, "Looks"."""
    return _call("studio_look_set", name=name, field=field or None, value=value, remove=True if remove else None,
                 merge=merge or None, sender="claude")


@mcp.tool()
def studio_look_unit(unit: str, look: str = "") -> dict:
    """The look a unit's type is drawn with (rich-gold, rich-violet or one of the mod's), "" for none,
    in looks.json's units by its name, one edit."""
    return _call("studio_look_unit", unit=unit, look=look, sender="claude")


@mcp.tool()
def studio_hud_set(path: str, value=None, merge: str = "") -> dict:
    """A member of hud.json set by a dotted path: console.art.terran, console.toEdges, console.fullMap,
    minimap.dots.own (allied, enemy, neutral: a palette entry), selection.circles (teamCircles,
    healthBars); back to the game's with no value."""
    return _call("studio_hud_set", path=path, value=value, merge=merge or None, sender="claude")


@mcp.tool()
def studio_looks_show(look: str = "", page: str = "") -> dict:
    """The Looks workspace shown at a look, or at the console's page: console, minimap or selection."""
    return _call("studio_looks_show", look=look or None, page=page or None)


@mcp.tool()
def studio_cinematic(name: str, path: str) -> dict:
    """A video (or .srt subtitles) copied in as a cinematic of the classic game's name, smk/<name><end>:
    blizzard, starintr, starXintr, PreTerran_1 to _4, Terran1 to 4, PreZerg_2 to _4, Zerg1 to 4,
    PreProtoss_1 to _3, Protoss1 to 4, xProtoss, xTerran, xZerg."""
    return _call("studio_cinematic", name=name, **{"from": path})


@mcp.tool()
def studio_mod_summary() -> dict:
    """The open mod's mod.json, its new and edited records by table, its files, those nothing names,
    its cinematics and its looks."""
    return _call("studio_mod_summary")


@mcp.tool()
def studio_mod_show(section: str = "") -> dict:
    """The mod's page shown, at a section: about, depends, brings, cinematics or files."""
    return _call("studio_mod_show", section=section or None)


@mcp.tool()
def studio_check(run: bool = False, save: bool = False) -> dict:
    """The mod checked: the studio's own problems as it is now, and the game's check (Hyperion.exe
    --mod-check on the saved mod) as it stands. run starts the game's check, save saves first; its
    state is "running" until it ends, so call again to read its problems, each with the record and
    field it is about."""
    return _call("studio_check", run=True if run else None, save=True if save else None)


@mcp.tool()
def studio_races() -> dict:
    """The open mod's races: each one's name, base race, start units, units, what it goes without,
    the AI's mapping, and the problems the loader would refuse or warn of."""
    return _call("studio_races")


@mcp.tool()
def studio_race_set(race: str, value: dict | None = None, field: str = "", field_value=None, rename: str = "",
                    remove: bool = False) -> dict:
    """A race of the mod's changed in one edit: set whole (value, a race's record), one of its fields
    set (field: name, base, start, units, without, ai, comment; field_value, None to take it away),
    renamed (rename, its new key), or taken away (remove). A new race is made by setting value."""
    args = {"race": race, "sender": "claude"}
    if remove:
        args["remove"] = True
    elif rename:
        args["rename"] = rename
    elif field:
        args["field"] = field
        args["value"] = field_value
    else:
        args["value"] = value
    return _call("studio_race_set", **args)


@mcp.tool()
def studio_races_show(page: str = "", race: str = "", unit: str = "") -> dict:
    """The Cards and races workspace shown: page races, cards or tree; race a race of the mod's (or,
    with page tree, zerg, terran, protoss or one of the mod's); unit a unit's card or a build menu
    (terran_basic_structures). With page tree the answer holds the tree: each record's tier, what it
    needs and the link it misses."""
    return _call("studio_races_show", page=page or None, race=race or None, unit=unit or None)


@mcp.tool()
def studio_card(unit: str = "", card: int = -1) -> dict:
    """A unit's command card as the game deals it, or a worker's build menu by its name
    (zerg_basic_structures ... protoss_advanced_structures): each button's place, hotkey, text, what it
    does, whether it is classic or the mod's, and the card's hotkey and place clashes. card is a
    classic card by its number instead."""
    if card >= 0:
        return _call("studio_card", card=card)
    return _call("studio_card", unit=unit)


@mcp.tool()
def studio_card_set(unit: str, index: int = -1, button: dict | None = None, fields: dict | None = None,
                    remove: bool = False) -> dict:
    """One of the mod's buttons on a unit's card or a build menu, by index in the mod's list for it:
    set whole (button), fields merged (fields, a None taken away), or taken off (remove); without
    index, button is added. A button is {position 1-9, one of train/upgrade/research/cast/build, hotkey,
    text with the key as <K>, disabled?, icon?, target?, ai?}; only a build menu's buttons build."""
    args = {"unit": unit, "sender": "claude"}
    if index >= 0:
        args["index"] = index
    if remove:
        args["remove"] = True
    elif fields is not None:
        args["fields"] = fields
    else:
        args["button"] = button
    return _call("studio_card_set", **args)


@mcp.tool()
def studio_card_move(unit: str, index: int, position: int) -> dict:
    """One of the mod's buttons (index in the mod's list for the card) moved to a place 1-9; a button
    of the mod's at that place takes the place it left."""
    return _call("studio_card_move", unit=unit, index=index, position=position, sender="claude")


@mcp.tool()
def studio_card_train(unit: str, card: str, position: int = 0, hotkey: str = "", text: str = "") -> dict:
    """A button that trains one of the mod's units on a card (the unit whose card it is), at its first
    free place with a free hotkey unless given; where it is made goes first in what the unit needs
    when they name no place. One edit."""
    return _call("studio_card_train", unit=unit, card=card, position=position or None, hotkey=hotkey or None,
                 text=text or None, sender="claude")


@mcp.tool()
def studio_card_build(unit: str, menu: str = "advanced", position: int = 0, hotkey: str = "", text: str = "") -> dict:
    """A button that builds one of the mod's units on a worker's build menu: menu by its name
    (protoss_advanced_structures) or basic or advanced of the unit's race. At the menu's first free
    place unless position is given (at a classic place it shows for a race that goes without the
    classic building); the worker goes first in what the unit needs when they name no place. One edit."""
    return _call("studio_card_build", unit=unit, menu=menu, position=position or None, hotkey=hotkey or None,
                 text=text or None, sender="claude")



@mcp.tool()
def studio_strings(query: str = "", kind: str = "", mine: bool = False, changed: bool = False, show: bool = False,
                   missing: str = "") -> dict:
    """Every text a player sees that the open mod gives or could give: units' names, the game's
    strings that name its weapons, techs and upgrades (read only), races' names, each card button's
    text (hotkey letter as <K>) and its text while it cannot, and mod.json's name and line. Each has
    an id, its kind, where it is, its text, its source (classic, mod or dependency), the classic text
    it stands in for, whether it is editable and changed since the save. query searches the texts and
    where they are; kind is name, label, button, disabled, race or mod (or a list with commas); mine
    leaves the mod's own; changed those changed since the save; show puts the same on the Strings
    workspace and shows it. missing, one of the mod's languages, leaves the strings missing or stale
    in it; each translatable string has its key and its translation in each language."""
    return _call("studio_strings", query=query or None, kind=kind or None, mine=True if mine else None,
                 changed=True if changed else None, show=True if show else None, missing=missing or None)


@mcp.tool()
def studio_string_set(id: str, text: str) -> dict:
    """One string set by its id from studio_strings, as one edit: a unit's name (a classic unit's
    makes an edit of it), a race's name, a button's text or its text while it cannot (a classic
    button's through a "classic" entry of the card; the letter marked <K> becomes its hotkey), or
    mod.json's name or line. An empty text gives a classic string its classic words back."""
    return _call("studio_string_set", id=id, text=text, sender="claude")


@mcp.tool()
def studio_strings_replace(find: str, replace: str, case: bool = False, query: str = "", kind: str = "",
                           mine: bool = False, changed: bool = False, preview: bool = False) -> dict:
    """Every find replaced in the strings studio_strings would list with the same query, kind, mine
    and changed, as one edit that one undo takes back; a button's hotkey stays on its letter. case
    matches case; preview answers each change (before, after) and makes none. Read-only strings are
    left and counted."""
    return _call("studio_strings_replace", find=find, replace=replace, case=True if case else None,
                 query=query or None, kind=kind or None, mine=True if mine else None,
                 changed=True if changed else None, preview=True if preview else None, sender="claude")


@mcp.tool()
def studio_languages(add: str = "", remove: str = "", game: str | None = None) -> dict:
    """The open mod's languages (each a file lang/<code>.json, docs/mod-data.md Languages), each with
    how many of its strings are translated, missing and stale, and the language the mod's own text is
    in. add or remove a language (deDE, frFR, "de", "Deutsch"...) as one edit; game is the language
    the studio's test game and a race's Play it show the mod's text in ("" for the mod's own)."""
    return _call("studio_languages", add=add or None, remove=remove or None, game=game, sender="claude")


@mcp.tool()
def studio_translations(language: str, missing: bool = False, stale: bool = False) -> dict:
    """A language's rows: each translatable string's key, its Strings id, where it is, the mod's text,
    its translation and state (done, stale: made from a text the mod has changed since, missing, or
    empty: nothing to translate), and the mod that gives the translation. missing or stale leave
    those alone."""
    return _call("studio_translations", language=language, missing=True if missing else None,
                 stale=True if stale else None)


@mcp.tool()
def studio_translation_set(language: str, text: str, key: str = "", id: str = "") -> dict:
    """One string's translation into a language, as one edit (the language added when the mod has
    not got it). The string by its key (studio_translations) or its Strings id (studio_strings). A
    button's text marks its hotkey letter as <K>; one that marks none has the mod's letter marked
    when it has it. An empty text takes the translation away."""
    return _call("studio_translation_set", language=language, key=key or None, id=id or None,
                 text=text if text else None, sender="claude")


@mcp.tool()
def studio_translations_export(language: str, file: str = "", format: str = "") -> dict:
    """A translator's file for a language: CSV (key, where, source, the translation, state) or PO
    (msgctxt the key, msgid the mod's text, msgstr the translation), the mod's
    .studio/translations/<code>.csv unless a file is given; the format from format or the file's extension."""
    return _call("studio_translations_export", language=language, file=file or None, format=format or None)


@mcp.tool()
def studio_translations_import(language: str, file: str) -> dict:
    """A translator's CSV or PO file read back into a language as one edit. Lines whose key names no
    string of the mod are refused and listed (line and key); empty translations are left as they
    are; a translation made from a text the mod has changed since stays marked stale."""
    return _call("studio_translations_import", language=language, file=file, sender="claude")


@mcp.tool()
def studio_unit_art(unit: str) -> dict:
    """A unit of the mod given art of its own: an image, a sprite and a flingy of the mod's, based on
    the ones it shows now, linked in place of the classic ones, in one edit. Nothing is made for what
    is the mod's already. Its frames stay the classic ones until a sheet or GRP is brought in
    (studio_art_sheet)."""
    return _call("studio_unit_art", unit=unit, sender="claude")


@mcp.tool()
def studio_art_sheet(image: str, path: str = "", file: str = "", frame: list | None = None, origin: list | None = None,
                     directions: int = 0, frames: int = 0, scale: int = 0, team: str = "") -> dict:
    """A sheet (PNG) or GRP made an image's frames, in one edit: path brings a file into the data
    folder's art/, file names one there already. The slicing: frame [w, h] a cell, origin [x, y] the
    cell's point on the unit's position, directions 17 (mirrored) or 32, frames the count, scale 1 or
    2, team a sheet of the team colour's layer. Frame and directions are guessed from the sheet when
    not given, and the answer says what was guessed."""
    args = {"image": image, "sender": "claude"}
    if path:
        args["from"] = path
    if file:
        args["file"] = file
    for k, v in (("frame", frame), ("origin", origin), ("directions", directions), ("frames", frames),
                 ("scale", scale), ("team", team)):
        if v:
            args[k] = v
    return _call("studio_art_sheet", **args)


@mcp.tool()
def studio_art_frame(image: str = "", sprite: str = "", flingy: str = "", unit: str = "", frame: int = -1,
                     facing: int = -1, colour: int = -1) -> dict:
    """What an image (or the image a sprite, flingy or unit shows) draws: where its frames come from,
    their count and size, the frame set its script starts at, and, with frame or facing (0 to 31) and
    colour (a player 0 to 7), the frame drawn there and whether it is mirrored."""
    args = {k: v for k, v in (("image", image), ("sprite", sprite), ("flingy", flingy), ("unit", unit)) if v}
    for k, v in (("frame", frame), ("facing", facing), ("colour", colour)):
        if v >= 0:
            args[k] = v
    return _call("studio_art_frame", **args)


@mcp.tool()
def studio_art_view(facing: int = -1, colour: int = -1, turning: bool | None = None, set: int = -2, tab: str = "",
                    select: str = "", classic: bool | None = None, anim: str = "", tick: int = -1,
                    playing: bool | None = None, as_if: dict | None = None, to_anim: bool = False,
                    to_top: bool = False) -> dict:
    """How the studio's art pictures are shown: facing 0 to 31, colour a player 0 to 7, turning or
    still, set a frame set (-1 the script's start), the Art workspace's tab (images or sprites), the
    record it selects and whether it lists classic ones (tabs: images, sprites, flingy, scripts); the animation player's animation by name
    (Walking, GndAttkInit, Death...), its tick and whether it plays; as_if the state its conditional
    jumps go by, each a choice's place: random (0 by chance, 1 always, 2 never), carried (0 on its own,
    1 carried), range (0 in range, 1 out of range), arc and facing (0 as it faces, 1 in the arc, 2
    outside it), lifted (0 landed, 1 lifted off); to_anim or to_top scroll a
    unit's Art view to the player or to its top. Fields left out stay as they are."""
    args = {}
    for k, v in (("facing", facing), ("colour", colour), ("tick", tick)):
        if v >= 0:
            args[k] = v
    if set >= -1:
        args["set"] = set
    for k, v in (("tab", tab), ("select", select), ("anim", anim)):
        if v:
            args[k] = v
    for k, v in (("turning", turning), ("classic", classic), ("playing", playing)):
        if v is not None:
            args[k] = v
    if as_if:
        args["as_if"] = as_if
    if to_anim:
        args["to_anim"] = True
    if to_top:
        args["to_top"] = True
    return _call("studio_art_view", **args)


@mcp.tool()
def studio_art_anim(image: str = "", unit: str = "", anim: str = "", facing: int = 6) -> dict:
    """An image's (or a unit's image's) script played as the game plays it: its animations, and with
    anim (Init, Walking, GndAttkInit, Death...) each tick as [frame set, facing, events], events a for
    an attack, s a sound, c a spell, o a signal to the order; the tick it loops from, whether it ends,
    the conditional jumps it met ([tick, condition, jumped]) and the as_if choices they went by, each
    overlay with where and when it was made and its own run's frame sets, and notes on what it could
    not show."""
    args = {k: v for k, v in (("image", image), ("unit", unit), ("anim", anim)) if v}
    args["facing"] = facing
    return _call("studio_art_anim", **args)

@mcp.tool()
def studio_race_fix(race: str, unit: str = "") -> dict:
    """A race's stand-ins let into what needs the units it goes without: each record of its tech tree
    whose needs name a unit the race goes without, and that its "ai" gives a stand-in for, takes the
    stand-in too (an alternative beside it). The mod's own records are changed; a classic record gets
    an edit in the mod. Only unit's stand-in when given. One edit; the answer lists what changed and
    the tree's missing links after."""
    return _call("studio_race_fix", race=race, unit=unit or None, sender="claude")


@mcp.tool()
def studio_race_rules(race: str, rules: dict | None = None, fix: bool = False) -> dict:
    """A race's building rules (docs/mod-data.md, "Races"), set in one edit: rules is any of build
    ("morph", "construct" or "warp"), power, creep, shields, regenerate (true or false), power_from
    and creep_from (lists of units), each None for its base's way. fix also sets the fields of the
    race's units the rules ask for (requires_psi, requires_creep, the power and creep makers' idle
    orders, shields, regenerate). The answer has the rules, the race's workers, the changes still to
    make with why, what does not fit together (a building that needs power with nothing to give it),
    and its advisor's lines."""
    args = dict(rules or {})
    if fix:
        args["fix"] = True
    return _call("studio_race_rules", race=race, sender="claude", **args)


@mcp.tool()
def studio_race_advisor(race: str, advisor: str = "", line: str = "", from_file: str = "", sound: str = "",
                        revert: bool = False) -> dict:
    """Whose advisor a race's players hear: advisor "zerg", "terran" or "protoss", or "base" for its
    base's. With line (minerals, gas, supply, energy, base_attacked, forces_attacked, research_done,
    upgrade_done, unpowered, error, cannot, cannot_place, cannot_land, cannot_build_here,
    build_error), that line said by a sound file brought in (from_file, a path), by a sound of the
    mod's (sound), or by the advisor again (revert). The answer is the race's rules and lines."""
    args = {"race": race, "sender": "claude"}
    if advisor:
        args["advisor"] = None if advisor == "base" else advisor
    if line:
        args["line"] = line
        if from_file:
            args["from"] = from_file
        elif sound:
            args["sound"] = sound
        elif revert:
            args["revert"] = True
    return _call("studio_race_advisor", **args)


@mcp.tool()
def studio_watch(name: str = "", stop: bool = False, mute: bool | None = None) -> dict:
    """A cinematic watched on the mod's page with its sound (the mod's video at its name, else the
    classic one), stopped with stop, or the previews' sound muted with mute. The answer is the
    player as it was last drawn: its file, frame, and its sound with when frame 0 and the sound
    began (frame0At, soundAt, on ImGui's clock), so call again after a moment to read them."""
    return _call("studio_watch", name=name or None, stop=True if stop else None, mute=mute)


@mcp.tool()
def studio_tilesets(tileset: str = "") -> dict:
    """The classic tilesets and what the mod changes in each (its colours, groups' flags,
    megatiles' flags, pictures), or one tileset's (badlands, platform, install, ashworld, jungle,
    desert, ice, twilight) changes listed with their counts."""
    return _call("studio_tilesets", tileset=tileset or None)


@mcp.tool()
def studio_tileset(tileset: str, group: int = -1, megatile: int = -1, palette: bool = False) -> dict:
    """One group of a tileset (its flags, its 16 megatiles, its colours), one megatile (its 16
    minitiles' walkable, height and blocks view, as classic and as the mod has them), or the
    palette (each colour now and classic, whether units use it, whether it cycles)."""
    args: dict = {"tileset": tileset}
    if group >= 0:
        args["group"] = group
    if megatile >= 0:
        args["megatile"] = megatile
    if palette:
        args["palette"] = True
    return _call("studio_tileset", **args)


@mcp.tool()
def studio_tile_set(tileset: str, group: int = -1, megatile: int = -1, colour: int = -1, colours: dict | None = None,
                    field: str = "", value=None, revert: bool = False, merge: str = "") -> dict:
    """A change to a tileset in the mod, undoable. A group's field: buildable, creep, walkable,
    height (low, mid, high), blocks_view or comment; a megatile's: walkable, blocks_view (true,
    false or 4 strings of # and .) or height (a word or 4 strings of l, m, h); a colour's value
    #rrggbb; colours {index: #rrggbb} at once. revert puts the classic one back. Edits with the
    same merge string undo as one."""
    args: dict = {"tileset": tileset, "sender": "claude"}
    if group >= 0:
        args["group"] = group
    if megatile >= 0:
        args["megatile"] = megatile
    if colour >= 0:
        args["colour"] = colour
    if colours:
        args["colours"] = colours
    if field:
        args["field"] = field
    if revert:
        args["revert"] = True
    else:
        args["value"] = value
    if merge:
        args["merge"] = merge
    return _call("studio_tile_set", **args)


@mcp.tool()
def studio_tile_image(tileset: str, path: str = "", group: int = -1, megatile: int = -1, revert: bool = False) -> dict:
    """A PNG as a megatile's picture (32 by 32, or a multiple) or a group's 16 (512 by 32, or a
    multiple), copied into the mod and mapped into the tileset's palette, its clear pixels keeping
    the classic ones; revert takes the picture out."""
    args: dict = {"tileset": tileset, "sender": "claude"}
    if group >= 0:
        args["group"] = group
    if megatile >= 0:
        args["megatile"] = megatile
    if revert:
        args["revert"] = True
    else:
        args["from"] = path
    return _call("studio_tile_image", **args)


@mcp.tool()
def studio_tile_export(tileset: str, to: str, group: int = -1, megatile: int = -1, classic: bool = False) -> dict:
    """A megatile's or a group's picture saved as PNG, as the mod draws it, or classic's, to paint
    over and bring back with studio_tile_image."""
    args: dict = {"tileset": tileset, "to": to}
    if group >= 0:
        args["group"] = group
    if megatile >= 0:
        args["megatile"] = megatile
    if classic:
        args["classic"] = True
    return _call("studio_tile_export", **args)


@mcp.tool()
def studio_tilesets_show(tileset: str = "", view: str = "", group: int = -1, slot: int = -1, megatile: int = -1,
                         colour: int = -1, layer: str = "", zoom: int = 0, overlay: bool | None = None,
                         changed: bool | None = None) -> dict:
    """The Tilesets workspace brought up at a tileset, its view (tiles or palette), a group and
    place or a megatile, a colour, the layer painted (walkable, height, blocks_view, buildable),
    the grid's zoom (1 to 3), the flags over the tiles, and the changed groups only."""
    args: dict = {}
    for k, v in (("tileset", tileset), ("view", view), ("layer", layer)):
        if v:
            args[k] = v
    for k, v in (("group", group), ("slot", slot), ("megatile", megatile), ("colour", colour)):
        if v >= 0:
            args[k] = v
    if zoom:
        args["zoom"] = zoom
    if overlay is not None:
        args["overlay"] = overlay
    if changed is not None:
        args["changed"] = changed
    return _call("studio_tilesets_show", **args)


# ---- the looks a mark on the map wears (studio_marks.cpp) -------------------------------------


@mcp.tool()
def studio_marks() -> dict:
    """The looks the open mod names under types.json's "marks" (docs/mod-data.md, "The looks a mark
    wears"): what a mark the mod's scripts draw on the ground wears past its edge. Each with its
    line, which blocks of light it has (field, cells, foot, tear, ripple, outside, minimap), and how
    many of its values the loader would refuse."""
    return _call("studio_marks")


@mcp.tool()
def studio_readout() -> dict:
    """The mod's readout.json as it stands: whether it has one, how many parameters it sets past
    classic's, every parameter set by part, its ranks and its effects, and the loader's own reading of
    the file (whether it reads at all, why not, and each key it passes over). The readout is the one
    unit's panel: the wireframe, name, status line, kills, stat icons, life and energy."""
    return _call("studio_readout")


@mcp.tool()
def studio_readout_set(part: str = "", field: str = "", value=None, revert: bool = False) -> dict:
    """One parameter of the readout set, or given back to classic with revert. part is "" for the
    file's own keys (from, in_huds), "lines" for what it adds (each, bar, in_huds), or one of wire,
    name, status, kills, slots, life, energy; field is the parameter in it. A parameter not set is
    classic's own, so revert is how a part goes back rather than writing classic's value in."""
    args = {"part": part, "field": field, "value": value, "sender": "claude"}
    if revert:
        args["revert"] = True
    return _call("studio_readout_set", **args)


@mcp.tool()
def studio_readout_fx(at: int = -1, field: str = "", value=None, new: bool = False,
                      remove: bool = False) -> dict:
    """An effect of the readout made (new), read (at alone), changed (at and field) or taken away
    (at and remove). An effect is one op on one part, set off by an event ("on") or held while a test
    is true ("while"), never both. A new one is a flash on the wireframe when the unit is damaged,
    which a field then changes. Every effect is carried by light: no part shakes, bounces or changes
    size, and the ops that move text are marked motion, which the player's Reduce motion leaves out."""
    args = {"at": at if at >= 0 else None, "field": field or None, "value": value, "sender": "claude"}
    if new:
        args["new"] = True
    if remove:
        args["remove"] = True
    return _call("studio_readout_fx", **args)


@mcp.tool()
def studio_readout_show() -> dict:
    """What the panel is showing now in the studio's canvas game, for the one unit selected: each
    part's words as the panel's own control asks for them (a part the mod leaves alone answers
    classic's own), each stat icon's tooltip, and the lines the hook returned with their time left
    and what they do. It reads the readout the last push loaded, not the values being edited, and
    needs a game running, which the Test workspace starts."""
    return _call("studio_readout_show")


@mcp.tool()
def studio_mark(name: str) -> dict:
    """One mark look as types.json has it, with its blocks counted and every value the loader would
    refuse named by its path."""
    return _call("studio_mark", name=name)


@mcp.tool()
def studio_mark_set(name: str, block: str = "", field: str = "", value=None, remove: bool = False) -> dict:
    """A mark look made (name alone), one of its values set, or a block or the whole look taken away
    with remove. block is "" for the mark's own values the look asks for (warn, fill, edge,
    edge_width, dash, soft, pulse, pulse_depth, curve, crossing) or one of field, cells, foot, tear,
    ripple, outside, minimap; field is the value in it. A number outside the range the loader accepts
    is refused rather than written, since the loader would refuse the whole mod for it."""
    args = {"name": name, "block": block, "field": field or None, "value": value, "sender": "claude"}
    if remove:
        args["remove"] = True
    return _call("studio_mark_set", **args)


@mcp.tool()
def studio_mark_show(name: str = "", off: bool = False, shape: str = "", radius: float = 0,
                     fade: bool | None = None, crossing: bool | None = None, minimap: bool | None = None,
                     at: list | None = None) -> dict:
    """Puts a real mark wearing that look in the studio's canvas game, so the look is drawn as a game
    draws it, or takes it out with off. The shape (ring, circle or rect), the radius in map pixels,
    the fade, the crossing and the minimap are the mark's own values, which a script says and a look
    only asks for; at is [x, y] in map pixels. It needs a game running, which the Test workspace
    starts, and changes nothing of the mod."""
    args = {"name": name or None, "shape": shape or None, "at": at}
    if off:
        args["off"] = True
    if radius > 0:
        args["radius"] = radius
    if fade is not None:
        args["fade"] = fade
    if crossing is not None:
        args["crossing"] = crossing
    if minimap is not None:
        args["minimap"] = minimap
    return _call("studio_mark_show", **args)


# ---- the Mode workspace (studio_modes.cpp, studio_modeshow.cpp) -------------------------------


@mcp.tool()
def studio_mode() -> dict:
    """The open mod's mode block (docs/mod-data.md, "A game mode"): whether the mod is a game mode,
    its line and paragraph, what it needs, whether it changes the simulation, whether it plays an
    enemy player of its own, the players it is played with, how the set-up's Game type list shows it
    (game_type), the function it draws a map with (preview), its director from types.json's "game"
    block, and every setting and preset."""
    return _call("studio_mode")


@mcp.tool()
def studio_mode_set(field: str, value=None) -> dict:
    """Sets a field of the mode block: line, about, needs, sim, enemy_player, players, notes, preview
    or game_type. "director" and "director_every" are types.json's "game" block instead, which is
    where a mode names the hook that plays its own player and how often it runs."""
    return _call("studio_mode_set", field=field, value=value, sender="claude")


@mcp.tool()
def studio_mode_setting(id: str, field: str = "", value=None, new: bool = False, kind: str = "",
                        name: str = "") -> dict:
    """One setting of the mode: read with id alone, made with new (kind choice, toggle, number, list
    or seed), or a field of it set (kind, name, group, gist, base, min, max, step, unit, choices,
    items, effect, read, shown)."""
    args = {"id": id, "field": field or None, "value": value, "kind": kind or None, "name": name or None,
            "sender": "claude"}
    if new:
        args["new"] = True
    return _call("studio_mode_setting", **args)


@mcp.tool()
def studio_mode_setting_delete(id: str) -> dict:
    """Takes a setting away from the mode block."""
    return _call("studio_mode_setting_delete", id=id, sender="claude")


@mcp.tool()
def studio_mode_preset(id: str, field: str = "", value=None, new: bool = False, values: dict | None = None,
                       name: str = "") -> dict:
    """One preset of the mode, a named set of its settings' values: read with id alone, made with new,
    or its id, name, line or whole values object set."""
    args = {"id": id, "field": field or None, "value": value, "values": values, "name": name or None,
            "sender": "claude"}
    if new:
        args["new"] = True
    return _call("studio_mode_preset", **args)


@mcp.tool()
def studio_mode_preset_delete(id: str) -> dict:
    """Takes a preset away from the mode block."""
    return _call("studio_mode_preset_delete", id=id, sender="claude")


@mcp.tool()
def studio_mode_roll(seed: int = -1, map: str = "", settings: dict | None = None, race: str = "") -> dict:
    """Runs the mode's generate for a seed, a map and the settings, and gives back what it returned:
    its run and how many records it made in each table. The mod must be pushed into the studio's game
    first, since the roll runs the scripts the push loaded."""
    args = {"map": map or None, "settings": settings, "race": race or None}
    if seed >= 0:
        args["seed"] = seed
    return _call("studio_mode_roll", timeout=30.0, **args)


@mcp.tool()
def studio_mode_ai() -> dict:
    """What the mode plays its units with: every unit whose scripts name a brain, every card button
    that names a call of its own, and the buttons that name one of the game's own finders, each with
    how often it runs; and what they chose in the game the Test view started, newest last."""
    return _call("studio_mode_ai")


@mcp.tool()
def studio_mode_seen(setting: str = "", value=None, players: int = 0, map: str = "", preset: str = "",
                     bases: bool = False) -> dict:
    """The mode's set-up tab as a player meets it: every setting with its value, whether the mode's
    own shown draws it, and the sentences its effect and read give for that value. A value, a preset
    or bases sets what the view holds; players and map are what every call of the mode's is given
    beside the values. It changes the view and never the mod."""
    args = {"setting": setting or None, "value": value, "map": map or None, "preset": preset or None}
    if players > 0:
        args["players"] = players
    if bases:
        args["bases"] = True
    return _call("studio_mode_seen", timeout=30.0, **args)


@mcp.tool()
def studio_mode_preview(map: str = "", settings: dict | None = None) -> dict:
    """The shapes the mode's preview draws of a map for the settings the set-up view holds, each in a
    line with its place, size, colour and opacities, and the map's tiles and starts. It changes
    nothing of the mod."""
    return _call("studio_mode_preview", timeout=30.0, map=map or None, settings=settings)


@mcp.tool()
def studio_modes_show(view: str = "", setting: str = "", preset: str = "") -> dict:
    """Shows the Mode workspace at a view: overview, settings, presets, set-up, preview, roll, ai or
    test. A setting or a preset names the one to show with it."""
    return _call("studio_modes_show", view=view or None, setting=setting or None, preset=preset or None)


@mcp.tool()
def studio_claude_status() -> dict:
    """The studio's Claude panel: whether an ask runs, its mode (show: Show me first, edit: Let it
    edit), the model, the Claude Code it runs and what is missing to run it, the conversation, and
    how many of Claude's changes wait to be accepted."""
    return _call("studio_claude_status")


@mcp.tool()
def studio_claude_ask(text: str, mode: str = "", model: str = "", new: bool = False) -> dict:
    """Asks the panel's Claude, the modder's own Claude Code that the studio runs, in words; it
    answers in the panel (studio_claude_transcript). mode show or edit; model sonnet, opus, haiku or
    "" for its own; new starts a new conversation, else the one shown goes on."""
    args: dict = {"text": text}
    if mode:
        args["mode"] = mode
    if model:
        args["model"] = model
    if new:
        args["new"] = True
    return _call("studio_claude_ask", **args)


@mcp.tool()
def studio_claude_stop() -> dict:
    """Stops the panel's Claude: its process ends; what it changed is kept, as one change."""
    return _call("studio_claude_stop")


@mcp.tool()
def studio_claude_transcript() -> dict:
    """The panel's conversation: the modder's words, Claude's, each tool call as a line with its state
    (done, refused, not given) and why, and how each ask ended."""
    return _call("studio_claude_transcript")


@mcp.tool()
def studio_claude_pending() -> dict:
    """Claude's proposal (Show me first) as rows: record, field, before and after, each as JSON and in
    words, and why a record would not load; or, once accepted or in Let it edit, its last edits."""
    return _call("studio_claude_pending")


@mcp.tool()
def studio_claude_accept(row: int = -1) -> dict:
    """Accepts Claude's proposal, all of it, or one row; the rest waits."""
    return _call("studio_claude_accept", **({"row": row} if row >= 0 else {}))


@mcp.tool()
def studio_claude_undo(row: int = -1) -> dict:
    """Undoes Claude's proposal, all of it, or one row; the rest waits."""
    return _call("studio_claude_undo", **({"row": row} if row >= 0 else {}))


@mcp.tool()
def studio_claude_conversations(open: str = "", clear: str = "", all: bool = False, new: bool = False) -> dict:
    """The panel's conversations in the open mod (kept in its .studio/claude/): one opened by its id,
    one cleared, all cleared, or a new one started."""
    args: dict = {}
    if open:
        args["open"] = open
    if clear:
        args["clear"] = clear
    if all:
        args["all"] = True
    if new:
        args["new"] = True
    return _call("studio_claude_conversations", **args)


# HYPERION_MCP_TOOLS: only the tools it names, with commas between. The studio's Claude panel
# gives the Claude Code it starts the studio's tools it may use and no others to read.
_only = os.environ.get("HYPERION_MCP_TOOLS")
if _only:
    _keep = {t.strip() for t in _only.split(",") if t.strip()}
    for _t in [t.name for t in mcp._tool_manager.list_tools()]:
        if _t not in _keep:
            mcp.remove_tool(_t)

if __name__ == "__main__":
    mcp.run()
