"""Start Hyperion Studio, HyperionStudio.exe, from a game folder of its own, testrun/studio/.

    python tools/studio.py                    build it if stale, start it in a maximized window
    python tools/studio.py --size 2560x1440   a window of that size instead
    python tools/studio.py --fullscreen       full screen
    python tools/studio.py --background       a window that takes no focus, for a tool's own runs
    python tools/studio.py --folder studio-2  from testrun/studio-2/, while another runs from studio/
    python tools/studio.py --build            build Hyperion's x64 objects and the design engine first
    python tools/studio.py --stop             stop the studio on the port (27119 unless --port)

    python tools/studio.py status             the running studio's state, as JSON
    python tools/studio.py call <cmd> [json]  any channel command, its answer printed; exit 1 when refused
    python tools/studio.py frame <out.png>    the window's picture saved as PNG (--panels with them)

The studio is the program modders make their mods in, on the design engine's base, with its live
channel on port 27119, so it runs beside Hyperion (27117) and the design engine (27118)
(docs/modules/studio.md). Claude's own runs take --background --port 27132 --folder studio-test, so
the user's open studio and its files are left alone. Any option the script does not know goes to
the exe.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import design  # noqa: E402  (the channel's client, the grab, the listener)
import play  # noqa: E402  (the game folder's setup and refresh)

ROOT = play.ROOT
BUILD = ROOT / "src117" / "build" / "studio"
EXE = BUILD / "HyperionStudio.exe"
BUILT = [(EXE, "HyperionStudio.exe"), (BUILD / "SDL3.dll", "SDL3.dll")]
SYMBOLS = [(EXE.with_suffix(".pdb"), "HyperionStudio.pdb")]
DATA = ROOT / "studio"
PORT = 27119


def command(argv, port: int) -> int:
    """status, call and frame: the running studio driven from the command line."""
    what = argv[0]
    if what == "frame":
        panels = "--panels" in argv
        argv = [a for a in argv if a != "--panels"]
        if len(argv) < 2:
            sys.exit("studio.py frame <out.png> [--panels]")
        with design.client(port) as c:
            print(json.dumps(design.grab(c, Path(argv[1]), panels)))
        return 0
    cmd = "design_status" if what == "status" else (argv[1] if len(argv) > 1 else None)
    if cmd is None:
        sys.exit("studio.py call <cmd> [json]")
    args = json.loads(argv[2]) if what == "call" and len(argv) > 2 else {}
    with design.client(port) as c:
        answer = c.call(cmd, timeout=30.0, **args)
    print(json.dumps(answer, indent=1))
    return 0 if answer.get("ok") else 1


def stop(port: int) -> None:
    """The studio on this port asked to leave, and stopped if it does not."""
    pid = design.listener(port)
    if not pid:
        print(f"no studio listens on {port}")
        return
    try:
        with design.client(port, timeout=2.0) as c:
            c.call("design_quit", timeout=2.0)
    except Exception as e:  # noqa: BLE001  (no answer: stopped below)
        print(f"the channel did not answer ({e})")
    for _ in range(30):
        time.sleep(0.1)
        if design.listener(port) != pid:
            print("the studio left")
            return
    subprocess.run(["taskkill", "/PID", str(pid), "/T", "/F"])


def stale() -> bool:
    """Whether HyperionStudio.exe is older than the design engine or the studio's sources."""
    if not EXE.exists():
        return True
    newest = (ROOT / "src117" / "build" / "x64" / "Hyperion.exe").stat().st_mtime
    for folder in (ROOT / "src117" / "design", ROOT / "src117" / "studio"):
        for f in folder.rglob("*"):
            newest = max(newest, f.stat().st_mtime)
    return EXE.stat().st_mtime < newest


def refresh_data(game: Path) -> None:
    """The studio's data folder (engine/studio/) into the game folder's studio\\."""
    if not DATA.is_dir():
        return
    for src in sorted(DATA.rglob("*")):
        if not src.is_file():
            continue
        dst = game / "studio" / src.relative_to(DATA)
        if dst.exists() and dst.stat().st_mtime >= src.stat().st_mtime and dst.stat().st_size == src.stat().st_size:
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        print(f"  studio/{src.relative_to(DATA).as_posix()} <- {src}")


def main() -> int:
    if len(sys.argv) > 1 and sys.argv[1] in ("status", "call", "frame"):
        port = PORT
        argv = sys.argv[1:]
        if "--port" in argv:
            i = argv.index("--port")
            port = int(argv[i + 1])
            del argv[i:i + 2]
        return command(argv, port)
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--size", help="the window's size, WxH; maximized when not given")
    ap.add_argument("--fullscreen", action="store_true", help="start full screen")
    ap.add_argument("--background", action="store_true",
                    help="a window that takes no focus, and no crash window: a tool's run")
    ap.add_argument("--port", type=int, default=PORT, help="the live channel's port")
    ap.add_argument("--build", action="store_true", help="build Hyperion's x64 objects and the design engine first")
    ap.add_argument("--stop", action="store_true", help="stop the studio on the port and leave")
    ap.add_argument("--folder", default="studio",
                    help="its game folder under testrun/, studio unless given: a second studio runs from a folder "
                         "of its own while the first one's files are in use")
    args, rest = ap.parse_known_args()

    if args.stop:
        stop(args.port)
        return 0
    if design.listener(args.port):
        print(f"something listens on {args.port} already, a studio started before: "
              f"studio.py --stop stops it, --port <another> starts a second")
        return 0
    if args.build:
        if subprocess.run([sys.executable, str(ROOT / "tools" / "build_engine_x64.py"), "--no-install"]).returncode:
            sys.exit("build_engine_x64.py failed")
        if subprocess.run([sys.executable, str(ROOT / "tools" / "build_design_x64.py")]).returncode:
            sys.exit("build_design_x64.py failed")
    if args.build or stale():
        if design.stale() and not args.build:
            if subprocess.run([sys.executable, str(ROOT / "tools" / "build_design_x64.py")]).returncode:
                sys.exit("build_design_x64.py failed")
        if subprocess.run([sys.executable, str(ROOT / "tools" / "build_studio_x64.py")]).returncode:
            sys.exit("build_studio_x64.py failed")
    game = play.rt.TESTRUN / args.folder
    if not (game / "StarCraft.exe").exists():
        play.setup(game)
    play.refresh(game, BUILT, SYMBOLS)
    refresh_data(game)
    if not any(a == "--maps" or a.startswith("--maps=") for a in rest):
        for folder in play.default_map_folders():
            rest += ["--maps", str(folder)]

    env = dict(os.environ)
    env.pop("HYPERION_DLLS", None)
    env["HYPERION_WINDOW"] = "background" if args.background else "fullscreen" if args.fullscreen else "windowed"
    if args.background:
        env["HYPERION_QUIET"] = "1"  # a tool's run: a crash leaves its report, and no window
        env["HYPERION_TEST_MOUSE"] = "1"  # the channel's clicks reach the game in a window with no focus
    if args.size:
        env["HYPERION_DESIGN_SIZE"] = args.size
        env["HYPERION_GAME_SIZE"] = args.size  # a background window's size
    env["HYPERION_DESIGN_FOLDER"] = str(ROOT.parent / "design")
    cmd = [str(game / "HyperionStudio.exe"), f"--data={game}", f"--control={args.port}"] + rest
    print("starting: " + subprocess.list2cmdline(cmd))
    started = time.time()
    subprocess.Popen(cmd, cwd=game, env=env)

    # the channel writes its key once it listens
    kf = design.key_file(args.port)
    for _ in range(600):
        if kf.exists() and kf.stat().st_mtime >= started - 1:
            print(f"the studio listens on {args.port}")
            return 0
        time.sleep(0.1)
    sys.exit(f"the studio did not open its channel on {args.port}: see {game}\\hyperion_control.log")


if __name__ == "__main__":
    sys.exit(main())
