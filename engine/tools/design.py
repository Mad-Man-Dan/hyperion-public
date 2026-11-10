"""Start the design engine, HyperionDesign.exe, from a game folder of its own, testrun/design/.

    python tools/design.py                    build it if stale, start it in a maximized window
    python tools/design.py --size 1600x900    a window of that size instead
    python tools/design.py --fullscreen       full screen
    python tools/design.py --background       a window that takes no focus, for a tool's own runs
    python tools/design.py --folder design-2  from testrun/design-2/, while another runs from design/
    python tools/design.py --build            run build_engine_x64.py first, then build_design_x64.py
    python tools/design.py --stop             stop the design engine on the port (27118 unless --port)

    python tools/design.py status             the running engine's state, as JSON
    python tools/design.py call <cmd> [json]  any channel command, its answer printed; exit 1 when refused
    python tools/design.py frame <out.png>    the window's picture saved as PNG (--panels with them)

The design engine plays Debug Tools' canvas with a fixed seed, with panels round the game and its
live channel on port 27118, one past Hyperion's, so both run side by side
(docs/modules/design.md). Closing its window ends it. Any option the script does not know goes to
the exe.
"""
import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import play  # noqa: E402  (the game folder's setup and refresh)

ROOT = play.ROOT
BUILD = ROOT / "src117" / "build" / "design"
EXE = BUILD / "HyperionDesign.exe"
BUILT = [(EXE, "HyperionDesign.exe"), (BUILD / "SDL3.dll", "SDL3.dll")]
SYMBOLS = [(EXE.with_suffix(".pdb"), "HyperionDesign.pdb")]
PORT = 27118


def key_file(port: int) -> Path:
    return Path(os.environ["LOCALAPPDATA"]) / "Hyperion" / f"control-{port}.key"


def client(port: int, timeout: float = 5.0):
    """A connection to the running design engine's channel."""
    sys.path.insert(0, str(Path(__file__).resolve().parent / "control"))
    from hyperion_ctl import Client  # noqa: E402
    return Client(port=port, timeout=timeout)


def grab(c, out: Path, panels: bool = False, timeout: float = 10.0) -> dict:
    """The window's picture as a PNG at out, as design_grab takes it of the next frame."""
    answer = c.call("design_grab", path=str(out.resolve()), panels=panels, timeout=timeout)
    if not answer.get("ok"):
        raise RuntimeError(answer.get("error", "design_grab refused"))
    deadline = time.time() + timeout
    while time.time() < deadline:
        state = c.call("design_grabbed", timeout=timeout)
        if state.get("state") == "saved":
            return state
        if state.get("state") == "failed":
            raise RuntimeError("the grab could not be taken")
        time.sleep(0.05)
    raise TimeoutError("no frame was drawn for the grab")


def command(argv, port: int) -> int:
    """status, call and frame: the running engine driven from the command line."""
    what = argv[0]
    if what == "frame":
        panels = "--panels" in argv
        argv = [a for a in argv if a != "--panels"]
        if len(argv) < 2:
            sys.exit("design.py frame <out.png> [--panels]")
        with client(port) as c:
            print(json.dumps(grab(c, Path(argv[1]), panels)))
        return 0
    cmd = "design_status" if what == "status" else (argv[1] if len(argv) > 1 else None)
    if cmd is None:
        sys.exit("design.py call <cmd> [json]")
    args = json.loads(argv[2]) if what == "call" and len(argv) > 2 else {}
    with client(port) as c:
        answer = c.call(cmd, timeout=30.0, **args)
    print(json.dumps(answer, indent=1))
    return 0 if answer.get("ok") else 1


def listener(port: int) -> int:
    """The process listening on 127.0.0.1:port, or 0."""
    r = subprocess.run(["netstat", "-ano", "-p", "TCP"], capture_output=True, text=True)
    for line in r.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 5 and parts[1] == f"127.0.0.1:{port}" and parts[3] == "LISTENING":
            return int(parts[4])
    return 0


def stop(port: int) -> None:
    """The design engine on this port asked to leave, and stopped if it does not; any other one,
    such as one started on another port, is left alone."""
    pid = listener(port)
    if not pid:
        print(f"no design engine listens on {port}")
        return
    try:
        with client(port, timeout=2.0) as c:
            c.call("design_quit", timeout=2.0)
    except Exception as e:  # noqa: BLE001  (no answer: stopped below)
        print(f"the channel did not answer ({e})")
    for _ in range(30):
        time.sleep(0.1)
        if listener(port) != pid:
            print("the design engine left")
            return
    subprocess.run(["taskkill", "/PID", str(pid), "/T", "/F"])


def stale() -> bool:
    """Whether HyperionDesign.exe is older than the x64 build or the design engine's sources."""
    if not EXE.exists():
        return True
    newest = (ROOT / "src117" / "build" / "x64" / "Hyperion.exe").stat().st_mtime
    for f in (ROOT / "src117" / "design").rglob("*"):
        newest = max(newest, f.stat().st_mtime)
    return EXE.stat().st_mtime < newest


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
    ap.add_argument("--background", action="store_true", help="a window that takes no focus, and no crash window: a tool's run")
    ap.add_argument("--port", type=int, default=PORT, help="the live channel's port")
    ap.add_argument("--build", action="store_true", help="build Hyperion's x64 objects first")
    ap.add_argument("--stop", action="store_true", help="stop the design engine on the port and leave")
    ap.add_argument("--folder", default="design",
                    help="its game folder under testrun/, design unless given: a second engine runs from a folder "
                         "of its own while the first one's files are in use")
    args, rest = ap.parse_known_args()

    if args.stop:
        stop(args.port)
        return 0
    if listener(args.port):
        print(f"something listens on {args.port} already, a design engine started before: "
              f"design.py --stop stops it, --port <another> starts a second")
        return 0
    if args.build:
        if subprocess.run([sys.executable, str(ROOT / "tools" / "build_engine_x64.py"), "--no-install"]).returncode:
            sys.exit("build_engine_x64.py failed")
    if args.build or stale():
        if subprocess.run([sys.executable, str(ROOT / "tools" / "build_design_x64.py")]).returncode:
            sys.exit("build_design_x64.py failed")
    game = play.rt.TESTRUN / args.folder
    if not (game / "StarCraft.exe").exists():
        play.setup(game)
    play.refresh(game, BUILT, SYMBOLS)
    if not any(a == "--maps" or a.startswith("--maps=") for a in rest):
        for folder in play.default_map_folders():
            rest += ["--maps", str(folder)]

    env = dict(os.environ)
    env.pop("HYPERION_DLLS", None)
    env["HYPERION_WINDOW"] = "background" if args.background else "fullscreen" if args.fullscreen else "windowed"
    if args.background:
        env["HYPERION_QUIET"] = "1"  # a tool's run: a crash leaves its report, and no window
    if args.size:
        env["HYPERION_DESIGN_SIZE"] = args.size
        env["HYPERION_GAME_SIZE"] = args.size  # a background window's size
    env["HYPERION_DESIGN_FOLDER"] = str(ROOT.parent / "design")
    cmd = [str(game / "HyperionDesign.exe"), f"--data={game}", f"--control={args.port}"] + rest
    print("starting: " + subprocess.list2cmdline(cmd))
    started = time.time()
    subprocess.Popen(cmd, cwd=game, env=env)

    # the channel writes its key once it listens
    kf = key_file(args.port)
    for _ in range(600):
        if kf.exists() and kf.stat().st_mtime >= started - 1:
            print(f"the design engine listens on {args.port}")
            return 0
        time.sleep(0.1)
    sys.exit(f"the design engine did not open its channel on {args.port}: see {game}\\hyperion_control.log")


if __name__ == "__main__":
    sys.exit(main())
