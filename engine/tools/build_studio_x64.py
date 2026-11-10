"""Build Hyperion Studio, HyperionStudio.exe, from the design engine's objects.

    python tools/build_studio_x64.py           the studio's changed files compiled, then linked
    python tools/build_studio_x64.py --clean   every studio file compiled again

The studio is the program modders make their mods in (docs/modules/studio.md). It is made from the
design engine's base, src117/design/, and src117/studio/, so it is built after
tools/build_design_x64.py and from its objects: every one of them but design_main.obj, the design
engine's own identity and panels, which the studio's studio_main.cpp stands in for. Hyperion.exe and
HyperionDesign.exe are unchanged by it.

Only the studio's files are compiled here, and only those older than their source, a header of the
studio or the design base, or the design build: a change to the studio is a few files and a link.
A lock in the build folder keeps two builds from linking at once; one started while another runs
waits for it.
"""
import glob
import os
import shutil
import sys
import time

ENGINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ENGINE, "tools"))
import build_design_x64 as bd  # noqa: E402  (the design build's paths, libraries and steps)
import build_engine_x64 as bx  # noqa: E402

STUDIO_SRC = os.path.join(bx.SRC, "studio")
STUDIO_DATA = os.path.join(ENGINE, "studio")  # what the studio reads beside it: fields.json and the rest
OUT = os.path.join(bx.SRC, "build", "studio")
EXE = os.path.join(OUT, "HyperionStudio.exe")
LOCK = os.path.join(OUT, "build.lock")
LOCK_WAIT_S = 600
# the design engine's own: its identity and panel set, which studio_main.cpp stands in for
DESIGN_ONLY = {"design_main.obj"}


def say(what):
    print("[build_studio_x64]", what, flush=True)


def design_ready():
    """The design build's objects, once it has linked HyperionDesign.exe from them."""
    exe = os.path.join(bd.OUT, "HyperionDesign.exe")
    rsp = os.path.join(bd.OUT, "link.rsp")
    if not (os.path.isfile(exe) and os.path.isfile(rsp)) or os.path.getmtime(exe) < os.path.getmtime(rsp):
        sys.exit("[build_studio_x64] no finished design build in %s: run python tools/build_design_x64.py first"
                 % bd.OUT)
    objs = [o for o in sorted(glob.glob(os.path.join(bd.OUT, "*.obj"))) if os.path.basename(o) not in DESIGN_ONLY]
    if not objs:
        sys.exit("[build_studio_x64] the design build has no objects: run python tools/build_design_x64.py")
    return exe, objs


class Lock:
    """The build folder's lock: made with O_EXCL, waited for while another build holds it, and
    taken over when its holder has been gone longer than any build takes."""

    def __enter__(self):
        os.makedirs(OUT, exist_ok=True)
        start = time.time()
        while True:
            try:
                fd = os.open(LOCK, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
                os.write(fd, str(os.getpid()).encode())
                os.close(fd)
                return self
            except FileExistsError:
                try:
                    age = time.time() - os.path.getmtime(LOCK)
                except OSError:
                    continue
                if age > LOCK_WAIT_S:
                    say("a lock %d s old taken over" % age)
                    os.remove(LOCK)
                    continue
                if time.time() - start > LOCK_WAIT_S:
                    sys.exit("[build_studio_x64] another build holds %s" % LOCK)
                time.sleep(1.0)

    def __exit__(self, *exc):
        try:
            os.remove(LOCK)
        except OSError:
            pass


def stale_sources(design_exe, clean):
    """The studio's .cpp files whose objects are missing or older than what they are made from."""
    srcs = sorted(glob.glob(os.path.join(STUDIO_SRC, "*.cpp")))
    headers = glob.glob(os.path.join(STUDIO_SRC, "*.h")) + glob.glob(os.path.join(bd.DESIGN_SRC, "*.h"))
    newest_shared = max([os.path.getmtime(design_exe)] + [os.path.getmtime(h) for h in headers])
    out = []
    for src in srcs:
        obj = os.path.join(OUT, os.path.splitext(os.path.basename(src))[0] + ".obj")
        if clean or not os.path.isfile(obj):
            out.append(src)
            continue
        made = os.path.getmtime(obj)
        if made < os.path.getmtime(src) or made < newest_shared:
            out.append(src)
    return srcs, out


def copy_data(dest):
    """The studio's data folder (engine/studio/) beside the exe, as studio\\."""
    if not os.path.isdir(STUDIO_DATA):
        return
    for src in glob.glob(os.path.join(STUDIO_DATA, "**", "*"), recursive=True):
        if os.path.isfile(src):
            dst = os.path.join(dest, "studio", os.path.relpath(src, STUDIO_DATA))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(src, dst)


def main():
    clean = "--clean" in sys.argv[1:]
    design_exe, design_objs = design_ready()
    with Lock():
        srcs, stale = stale_sources(design_exe, clean)
        if not srcs:
            sys.exit("[build_studio_x64] no studio sources in %s" % STUDIO_SRC)
        # objects of studio files no longer there would link a part that is gone
        names = {os.path.splitext(os.path.basename(s))[0] + ".obj" for s in srcs}
        for o in glob.glob(os.path.join(OUT, "*.obj")):
            if os.path.basename(o) not in names:
                os.remove(o)
        env = bx.vs_env()
        if stale:
            flags = bx.PORT + ["/DHX_DESIGN", "/Fo" + OUT + "\\"]
            includes = [bd.DESIGN_SRC, bd.DESIGN_SHADERS] + bd.IMGUI_INCLUDES
            bd.run(["cl"] + flags + ["/MP"] + ["/I" + i for i in includes] + stale, env,
                   "cl /c %d of %d studio TUs" % (len(stale), len(srcs)))
        else:
            say("the studio's objects are current")
        studio_objs = sorted(glob.glob(os.path.join(OUT, "*.obj")))
        if not stale and os.path.isfile(EXE) and os.path.getmtime(EXE) >= max(
                [os.path.getmtime(design_exe)] + [os.path.getmtime(o) for o in studio_objs]):
            say("HyperionStudio.exe is current")
        else:
            link(env, design_objs, studio_objs)
        copy_data(OUT)
        bx.copy_web(OUT)
        bx.copy_huds(OUT)
        shutil.copyfile(os.path.join(bx.SDL3, "lib", "x64", "SDL3.dll"), os.path.join(OUT, "SDL3.dll"))
    say("OK -> src117\\build\\studio\\HyperionStudio.exe")


def link(env, design_objs, studio_objs):
    hyp = os.path.join(ENGINE, "hyperion")
    lrsp = os.path.join(OUT, "link.rsp")
    with open(lrsp, "w", encoding="utf-8") as f:
        f.write("\n".join('"%s"' % o for o in design_objs + studio_objs + [os.path.join(bx.OUT, "hyperion.o")]) + "\n")
    libs = [os.path.join(bx.OUT, "stormlib", "stormlib.lib"), os.path.join(bx.OUT, "casclib", "casclib.lib"),
            os.path.join(bx.OUT, "webm", "webm.lib"), os.path.join(bx.OUT, "image", "image.lib"),
            os.path.join(bx.OUT, "dav1d", "dav1d.lib")]
    luau = os.path.join(bd.OUT, "luau", "luau.lib")
    imgui = os.path.join(bd.OUT, "imgui", "imgui.lib")
    bd.run(["link", "/nologo", "/MACHINE:X64", "/SUBSYSTEM:WINDOWS", "/ENTRY:wWinMainCRTStartup", "/DEBUG",
            "/MANIFEST:EMBED", "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' "
            "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'",
            "/OPT:REF", "/OPT:ICF", "/DEF:" + os.path.join(hyp, "hyperion.def"),
            "/OUT:" + EXE, "/MAP:" + os.path.join(OUT, "HyperionStudio.map"),
            "@" + lrsp, os.path.join(bx.OUT, "hyperion.res"), os.path.join(bx.OUT, "fonts.res")] + libs +
           [os.path.join(bx.SDL3, "lib", "x64", "SDL3.lib"), "/DELAYLOAD:SDL3.dll", "delayimp.lib", "bcrypt.lib"] +
           [os.path.join(bx.WEBRTC, "lib", l) for l in bx.WEBRTC_LIBS] + ["iphlpapi.lib"] + bx.LIBS + [luau, imgui],
           env, "link HyperionStudio.exe")


if __name__ == "__main__":
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from buildlock import BuildLock
    with BuildLock("build_studio_x64"):
        main()
