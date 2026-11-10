"""Captures of Hyperion for its website (PRD 2026-10-24): clips and stills of Hyperion running,
recorded from its window and encoded as the site asks (hyperion-site's RECORDING.md).

    python tools/capture/capture.py list                  the recipes in shots.json
    python tools/capture/capture.py run <id> [<id> ...]   records each one
    python tools/capture/capture.py encode <raw> <id>     encodes a raw capture again
    python tools/capture/capture.py scene <name>          records a game the recipes play

A recipe (shots.json's `recipes`) says how to start Hyperion (its command line, where `{replays}` stands for
the replay suite's folder and `{scenes}` for recordings/raw/scenes/, as in a step's arguments, where
`{data}` is the data folder too), what to do through the live channel before recording (`steps`), and
for a clip how long to record and what to do while it records (`during`, each at a time from the
start). A step is a channel command (`{"cmd": ...}`), a key pressed a number of times
(`{"press": "U", "times": 6}`), a wait for a game's frame (`{"until_frame": 12000}`), the game
paused at an exact frame (`{"at_frame": 18000}`: played on to 300 before it, paused, and stepped to
it), or a pause (`{"sleep": 2}`). `settings` are merged into the data folder's settings.json for that run
(`{"hud": {"design": "classic"}}`), `music` over the music player's state, which each run writes
afresh as music.json (`{"windows": {"scale": 200}}`), and a still's `crop` ([x, y, w, h], at the capture's own
pixels) is the part a still or a clip keeps. Hyperion runs from testrun/play/ with a data
folder of its own, testrun/capture/, made from testrun/play/'s settings with no active mod, the
release notes' notices closed, the sound off, no recent maps and a profile named Commander (what
a capture shows is published), in a 1920x1080 window on the right screen, with HYPERION_CAPTURE
set so Debug Tools' opener stays off the game, and SDL's dummy sound driver, so nothing is heard
while the game and the music player play as they would.

The window is recorded through ffmpeg's gfxcapture (Windows Graphics Capture), which takes the
frames the presenter hands Windows, by the window's handle, so other windows over it do not
matter. A clip is recorded at 60 frames a second with the GPU's H.264 encoder at a high quality,
into recordings/raw/ (a new file each run), then encoded for the site into recordings/site/:
<id>.mp4 (H.264, faststart, no sound), <id>.webm (VP9) and <id>.jpg (its first frame). A still is
<id>.webp at its frame (the crop, or the whole window), and <id>-960.webp 960 wide when it is the
whole window; its PNG master stays in recordings/raw/. recordings/site/manifest.json lists every
file, as the site's `npm run sync:media` reads it: `{"files": [{"id", "file", "kind", "width",
"height", "seconds", "bytes", "version", "date", "notes"}]}`.

A scene is a game recorded for the recipes to play, so what they show is a game Hyperion played
and not a replay of another version's (an old ladder replay can play differently in 1.17 itself).
shots.json's `scenes` give each one's map, length and seats' races; `scene <name>` plays it as a
benchmark game, every seat a computer and the local player an observer who cannot lose, at a fixed
seed, unshown at full speed, and keeps its run.rep as recordings/raw/scenes/<name>.rep. The same
build makes the same file again. Zerg seats are left out while a replay's creep tiles can differ
from its game's (the tracker's creep tiles bug). A replay selects one unit at a time, so the
HUD's shots load a save instead, a game staged live by stage_fight.py (its comment); such a
scene names what made it (`made_by`).

The captures show the game's art: they may be published on the site (the user's decision, PRD
2026-10-24), and stay out of git (recordings/ is gitignored).
"""
import ctypes
import ctypes.wintypes as wt
import datetime
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
ENGINE = HERE.parent.parent
REPO = ENGINE.parent
PLAY = ENGINE / "testrun" / "play"
REPLAYS = ENGINE / "testrun" / "rt_replays"
DATA = ENGINE / "testrun" / "capture"
RAW = REPO / "recordings" / "raw"
SITE = REPO / "recordings" / "site"
SHOTS = HERE / "shots.json"
SCENES = RAW / "scenes"
VERSION_H = ENGINE / "hyperion" / "version.h"
NOTES = ENGINE / "notes" / "release-notes.json"
SIZE = (1920, 1080)

sys.path.insert(0, str(ENGINE / "tools" / "control"))
sys.path.insert(0, str(ENGINE / "tools"))
from hyperion_ctl import Client  # noqa: E402

user32 = ctypes.windll.user32


def version() -> str:
    m = re.search(r'HYPERION_FILE_VERSION_STR "([^"]+)"', VERSION_H.read_text())
    return m.group(1) if m else "?"


PROFILE = "Commander"


MUSIC = {"source": "game", "gameTrack": 0, "mineTrack": 0, "volume": 0.7, "balance": 0.5, "shuffle": False,
         "repeat": True, "eqOn": False, "eqAuto": False, "preamp": 0, "bands": [0] * 10, "vis": "spectrum",
         "inGames": "player", "duck": True, "pauseCine": True, "onTop": True, "follow": True, "sessionApp": "",
         "windows": {"main": [-1, -1], "eq": [-1, -1], "pl": [-1, -1], "scale": 100, "double": False,
                     "shade": False, "eqShade": False, "plShade": False, "eqShown": True, "plShown": True,
                     "snap": True},
         "mine": []}


def data_folder(extra: dict, music: dict = None) -> None:
    """testrun/capture/ for a run (the module's comment), with a recipe's own settings over it."""
    DATA.mkdir(parents=True, exist_ok=True)
    settings = json.loads((PLAY / "settings.json").read_text(encoding="utf-8"))
    hx = settings.setdefault("hyperion", {})
    hx["mod"] = ""
    hx["display"] = "window"
    newest = [e["id"] for e in json.loads(NOTES.read_text(encoding="utf-8"))["entries"][:3]]
    hx["notesDismissed"] = " ".join(newest)
    hx["musicLevel"] = 0
    hx["profile"] = PROFILE
    sc = settings.setdefault("Starcraft", {})
    sc["music"] = 0
    sc["sfx"] = 0
    sc.pop("Recent Maps", None)
    for section, values in extra.items():
        settings.setdefault(section, {}).update(values)
    (DATA / "settings.json").write_text(json.dumps(settings, indent=2), encoding="utf-8")
    # the music player's state, fresh each run: the recipe's over the player's defaults
    state = json.loads(json.dumps(MUSIC))
    for key, value in (music or {}).items():
        if isinstance(value, dict) and isinstance(state.get(key), dict):
            state[key].update(value)
        else:
            state[key] = value
    (DATA / "music.json").write_text(json.dumps(state, indent=2), encoding="utf-8")
    # the profile: a copy of the play folder's first, under the name a capture may show
    chars = DATA / "characters"
    chars.mkdir(exist_ok=True)
    mine = chars / f"{PROFILE}.spc"
    if not mine.exists():
        first = sorted((PLAY / "characters").glob("*.spc"))
        if first:
            shutil.copyfile(first[0], mine)


def hyperion_pids() -> set:
    out = subprocess.run(["powershell", "-NoProfile", "-Command",
                          "(Get-CimInstance Win32_Process -Filter \"Name='Hyperion.exe'\").ProcessId"],
                         capture_output=True, text=True).stdout.split()
    return {int(x) for x in out}


def window_of(pid: int):
    """The process's largest visible window: its handle and client size."""
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def each(h, _):
        p = wt.DWORD()
        user32.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and user32.IsWindowVisible(h):
            r = wt.RECT()
            user32.GetClientRect(h, ctypes.byref(r))
            if r.right > 100:
                found.append((h, r.right, r.bottom))
        return True
    user32.EnumWindows(each, 0)
    return max(found, key=lambda f: f[1] * f[2]) if found else None


def start(recipe: dict):
    """Hyperion started for a recipe: its process, its window and a channel client."""
    data_folder(recipe.get("settings", {}), recipe.get("music"))
    env = dict(os.environ, HYPERION_DISPLAY="right", HYPERION_WINDOW="background", HYPERION_CAPTURE="1",
               HYPERION_GAME_SIZE="%dx%d" % SIZE, HYPERION_FRONT_SIZE="%dx%d" % SIZE, SDL_AUDIO_DRIVER="dummy")
    env.update(recipe.get("env", {}))
    import play  # tools/play.py: its game folder brought up to the last build, and its map folders
    play.refresh()
    args = [a.replace("{replays}", str(REPLAYS)).replace("{scenes}", str(SCENES)) for a in recipe.get("args", [])]
    cmd = [str(PLAY / "Hyperion.exe"), f"--data={DATA}", "--control"] + args
    for folder in play.default_map_folders():
        cmd += ["--maps", str(folder)]
    proc = subprocess.Popen(cmd, cwd=PLAY, env=env)
    deadline = time.time() + 60
    client = None
    while time.time() < deadline and client is None:
        time.sleep(1)
        try:
            client = Client(timeout=2.0)
        except OSError:
            client = None
    if client is None:
        proc.kill()
        sys.exit("capture: Hyperion's channel did not open")
    time.sleep(recipe.get("wait", 3))
    win = window_of(proc.pid)
    if not win:
        proc.kill()
        sys.exit("capture: Hyperion has no window")
    return proc, win, client


def do(client, step) -> None:
    """One step of a recipe (the module's comment)."""
    if "sleep" in step:
        time.sleep(step["sleep"])
        return
    if "press" in step:
        # with its character too: 1.17's replay keys (U, D, P) read the character, not the key
        key = step["press"]
        char = step.get("char", key.lower() if len(key) == 1 else None)
        for _ in range(step.get("times", 1)):
            client.call("press", key=key, char=char)
            time.sleep(0.3)
        return
    if "until_frame" in step:
        deadline = time.time() + step.get("timeout", 120)
        while time.time() < deadline:
            if client.call("status").get("frame", 0) >= step["until_frame"]:
                return
            time.sleep(0.5)
        sys.exit("capture: the game did not reach frame %d" % step["until_frame"])
    if "at_frame" in step:
        target = step["at_frame"]
        do(client, {"until_frame": target - 300, "timeout": step.get("timeout", 300)})
        client.call("pause")
        frame = client.call("status").get("frame", 0)
        if frame < target:
            client.call("step", timeout=60.0, frames=target - frame)
        deadline = time.time() + 30
        while client.call("status").get("frame", 0) < target and time.time() < deadline:
            time.sleep(0.2)
        return
    args = {k: v.replace("{data}", str(DATA)).replace("{scenes}", str(SCENES)) if isinstance(v, str) else v
            for k, v in step.items() if k != "cmd"}
    answer = client.call(step["cmd"], timeout=30.0, **args)
    if not answer.get("ok", True):
        print("  %s: %s" % (step["cmd"], answer.get("error")))
    else:
        shown = {k: v for k, v in answer.items() if k not in ("id", "ok")}
        if shown:
            print("  %s: %s" % (step["cmd"], json.dumps(shown)[:200]))


def grab_args(hwnd: int) -> list:
    return ["-init_hw_device", "d3d11va", "-filter_complex",
            "gfxcapture=hwnd=%d:capture_cursor=0:max_framerate=60,hwdownload,format=bgra" % hwnd]


def record(hwnd: int, seconds: float, raw: pathlib.Path, client, during: list) -> None:
    """seconds of the window at 60 a second into raw, with the during steps at their times."""
    ff = subprocess.Popen(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y"] + grab_args(hwnd) +
                          ["-t", "%.2f" % seconds, "-fps_mode", "cfr", "-r", "60", "-c:v", "h264_nvenc",
                           "-preset", "p7", "-tune", "hq", "-rc", "constqp", "-qp", "14", "-pix_fmt", "yuv420p",
                           str(raw)], stdin=subprocess.DEVNULL)
    t0 = time.time()
    for at, step in sorted(during, key=lambda d: d[0]):
        time.sleep(max(0.0, at - (time.time() - t0)))
        do(client, step)
    if ff.wait() != 0:
        sys.exit("capture: ffmpeg could not record the window")


def ffmpeg(*args) -> None:
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y"] + [str(a) for a in args], check=True)


def still(hwnd: int, png: pathlib.Path) -> None:
    ffmpeg(*grab_args(hwnd), "-frames:v", "1", png)


def row(ident: str, path: pathlib.Path, kind: str, size, seconds=None) -> dict:
    r = {"id": ident, "file": path.name, "kind": kind, "width": size[0], "height": size[1],
         "bytes": path.stat().st_size, "version": version(), "date": datetime.date.today().isoformat()}
    if seconds:
        r["seconds"] = seconds
    return r


def stills(png: pathlib.Path, ident: str, crop) -> list:
    """The site's files from a still's PNG master: the crop at its own pixels, or the whole
    window and a copy 960 wide."""
    SITE.mkdir(parents=True, exist_ok=True)
    out = SITE / f"{ident}.webp"
    if crop:
        x, y, w, h = crop
        ffmpeg("-i", png, "-vf", "crop=%d:%d:%d:%d" % (w, h, x, y), "-c:v", "libwebp", "-quality", "90", out)
        return [row(ident, out, "still", (w, h))]
    small = SITE / f"{ident}-960.webp"
    ffmpeg("-i", png, "-c:v", "libwebp", "-quality", "90", out)
    ffmpeg("-i", png, "-vf", "scale=960:-2", "-c:v", "libwebp", "-quality", "88", small)
    return [row(ident, out, "still", SIZE), row(ident, small, "still", (960, 540))]


def encode(raw: pathlib.Path, ident: str, seconds=None, crop=None) -> list:
    """The site's three files from a raw clip (RECORDING.md, "Encode"), the whole window or a crop
    of it at its own pixels."""
    SITE.mkdir(parents=True, exist_ok=True)
    mp4, webm, jpg = (SITE / f"{ident}.{e}" for e in ("mp4", "webm", "jpg"))
    scale = "scale=%d:-2,fps=60" % SIZE[0]
    size = SIZE
    if crop:
        x, y, w, h = crop
        scale = "crop=%d:%d:%d:%d,fps=60" % (w, h, x, y)
        size = (w, h)
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(raw), "-an", "-c:v", "libx264",
                    "-preset", "slow", "-crf", "22", "-pix_fmt", "yuv420p", "-vf", scale, "-movflags", "+faststart",
                    str(mp4)], check=True)
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(raw), "-an", "-c:v", "libvpx-vp9",
                    "-b:v", "0", "-crf", "34", "-row-mt", "1", "-vf", scale, str(webm)], check=True)
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(mp4), "-frames:v", "1", "-q:v", "3",
                    str(jpg)], check=True)
    return [row(ident, mp4, "clip", size, seconds), row(ident, webm, "clip", size, seconds),
            row(ident, jpg, "still", size)]


def note(ident: str, rows: list, notes: str = "") -> None:
    """The manifest with an id's files replaced by these."""
    path = SITE / "manifest.json"
    manifest = json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}
    files = [f for f in manifest.get("files", []) if f["id"] != ident]
    for r in rows:
        if notes:
            r["notes"] = notes
        files.append(r)
    path.write_text(json.dumps({"files": sorted(files, key=lambda f: (f["id"], f["file"]))}, indent=2),
                    encoding="utf-8")


def run(ident: str, recipe: dict) -> None:
    before = hyperion_pids()
    if before:
        sys.exit("capture: a Hyperion is running (pid %s); close it first" % ", ".join(map(str, before)))
    proc, (hwnd, w, h), client = start(recipe)
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    try:
        for step in recipe.get("steps", []):
            do(client, step)
        if (w, h) != SIZE:
            print("capture: the window is %dx%d, not %dx%d" % (w, h) + SIZE)
        RAW.mkdir(parents=True, exist_ok=True)
        if recipe["kind"] == "clip":
            raw = RAW / f"{ident}_{stamp}.mkv"
            record(hwnd, recipe["seconds"], raw, client, recipe.get("during", []))
            rows = encode(raw, ident, recipe["seconds"], recipe.get("crop"))
        else:
            png = RAW / f"{ident}_{stamp}.png"
            still(hwnd, png)
            rows = stills(png, ident, recipe.get("crop"))
        note(ident, rows, recipe.get("notes", ""))
        print("capture: %s -> %s" % (ident, ", ".join(r["file"] for r in rows)))
    finally:
        client.close()
        proc.kill()
        proc.wait()


def scene(name: str, spec: dict) -> None:
    """A scene recorded (the module's comment)."""
    before = hyperion_pids()
    if before:
        sys.exit("capture: a Hyperion is running (pid %s); close it first" % ", ".join(map(str, before)))
    data_folder({})
    import play
    play.refresh()
    bench = DATA / "benchmarks"
    had = set(bench.iterdir()) if bench.exists() else set()
    env = dict(os.environ, HYPERION_DISPLAY="right", HYPERION_WINDOW="background", HYPERION_CAPTURE="1",
               SDL_AUDIO_DRIVER="dummy")
    races = spec["races"]
    cmd = [str(PLAY / "Hyperion.exe"), f"--data={DATA}", "--bench", f"--map={spec['map']}",
           f"--bench-frames={spec['frames']}", "--bench-speed=max", "--bench-shown=none",
           f"--bench-races={races}", f"--players={len(races)}"] + spec.get("args", [])
    for folder in play.default_map_folders():
        cmd += ["--maps", str(folder)]
    proc = subprocess.Popen(cmd, cwd=PLAY, env=env)
    try:
        proc.wait(timeout=spec.get("timeout", 1800))
    finally:
        if proc.poll() is None:
            proc.kill()
    runs = sorted(set(bench.iterdir()) - had) if bench.exists() else []
    reps = [r / "run.rep" for r in runs if (r / "run.rep").exists()]
    if not reps:
        sys.exit("capture: the scene's run left no run.rep")
    SCENES.mkdir(parents=True, exist_ok=True)
    out = SCENES / f"{name}.rep"
    shutil.copyfile(reps[-1], out)
    summary = json.loads((reps[-1].parent / "summary.json").read_text(encoding="utf-8"))
    print("capture: scene %s -> %s (%d bytes), %s frames, %s units at most, end state %s" % (
        name, out, out.stat().st_size, summary.get("end_frame"), summary.get("units_most"),
        summary.get("end_state")))


def main() -> int:
    shots = json.loads(SHOTS.read_text(encoding="utf-8"))
    recipes, scenes = shots["recipes"], shots.get("scenes", {})
    cmd = sys.argv[1] if len(sys.argv) > 1 else "list"
    if cmd == "list":
        for name, sc in scenes.items():
            print("%-16s scene %s" % (name, sc.get("title", "")))
        for ident, r in recipes.items():
            print("%-16s %-5s %s" % (ident, r["kind"], r.get("title", "")))
    elif cmd == "scene" and len(sys.argv) == 3:
        if sys.argv[2] not in scenes:
            sys.exit(f"capture: no scene {sys.argv[2]} in {SHOTS.name}")
        if "made_by" in scenes[sys.argv[2]]:
            sys.exit(f"capture: {sys.argv[2]} is made by {scenes[sys.argv[2]]['made_by']}")
        scene(sys.argv[2], scenes[sys.argv[2]])
    elif cmd == "run":
        for ident in sys.argv[2:]:
            if ident not in recipes:
                sys.exit(f"capture: no recipe {ident} in {SHOTS.name}")
            run(ident, recipes[ident])
    elif cmd == "encode" and len(sys.argv) == 4:
        raw, ident = pathlib.Path(sys.argv[2]), sys.argv[3]
        note(ident, encode(raw, ident))
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
