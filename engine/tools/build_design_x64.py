"""Build the design engine, HyperionDesign.exe, from the x64 build's objects.

    python tools/build_design_x64.py

The design engine is Hyperion's engine as a program of its own for trying ideas out: a window of
any size with panels round the game (docs/modules/design.md). It is built after
tools/build_engine_x64.py and from its objects, so Hyperion.exe carries none of it: this script
compiles only src117/design/ and the few shared files whose design-engine hooks sit under
#ifdef HX_DESIGN (HOOKED), and links src117/build/design/HyperionDesign.exe beside SDL3.dll,
web/ and huds/. The game's globals stay bound by the x64 build's abs_globals.obj: the design
engine's own files may name any game global that the engine's code already names, through the
same headers, and a link error says when one is not bound. The normal build, its gates and its
packages are unchanged. Run tools/design.py to start it.

The design engine's own third-party code is Dear ImGui, its panels (tools/fetch_imgui.py), and
Luau, the language its effects are written in (tools/fetch_luau.py), each built here into its
own library once per version; its shaders (src117/design/shaders/) are compiled with the x64
build's dxc. Dear ImGui's SDL_GPU backend carries its own compiled shaders.
"""
import glob
import os
import shutil
import subprocess
import sys

ENGINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ENGINE, "tools"))
import build_engine_x64 as bx  # noqa: E402  (the x64 build's flags, paths and steps)
from port_files import port_path  # noqa: E402

DESIGN_SRC = os.path.join(bx.SRC, "design")
OUT = os.path.join(bx.SRC, "build", "design")
# the shared files with hooks under HX_DESIGN (src117/design/design_hooks.h), compiled again here;
# port_gpu_scene.cpp has none of its own but takes the design engine's copies of the shaders
HOOKED = ["port_paths.cpp", "port_present.cpp", "port_gpu.cpp", "port_control.cpp", "port_console.cpp",
          "port_winmsg.cpp", "port_gpu_ui.cpp", "port_front.cpp", "port_observe.cpp", "port_looks.cpp",
          "port_scene.cpp", "port_gpu_scene.cpp", "port_moddata.cpp", "port_debugview.cpp",
          "port_camera.cpp"]
LUAU = os.path.join(ENGINE, "third_party", "luau-0.742")
# the parts of Luau the design engine runs: the parser, the compiler to bytecode and the VM
LUAU_PARTS = ["Common", "Ast", "Bytecode", "Compiler", "VM"]
LUAU_INCLUDES = [os.path.join(LUAU, part, "include") for part in LUAU_PARTS]
IMGUI = os.path.join(ENGINE, "third_party", "imgui-1.93.0-docking")
IMGUI_SOURCES = ["imgui.cpp", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp", "imgui_demo.cpp",
                 os.path.join("backends", "imgui_impl_sdl3.cpp"), os.path.join("backends", "imgui_impl_sdlgpu3.cpp")]
IMGUI_INCLUDES = [IMGUI, os.path.join(IMGUI, "backends")]
DESIGN_SHADERS = os.path.join(OUT, "shaders")
DESIGN_SHADER_ENTRIES = [("fx.hlsl", "VSFx", "vs_6_0"), ("fx.hlsl", "PSFx", "ps_6_0")]
# the x64 build's shaders the design engine builds again for itself; it keeps its own copies of
# the headers the hooked files include
HOOKED_SHADERS = ["scene.hlsl"]


def say(what):
    print("[build_design_x64]", what, flush=True)


def run(cmd, env, what):
    say(what)
    exe = shutil.which(cmd[0], path=env["PATH"]) or cmd[0]
    r = subprocess.run([exe] + cmd[1:], env=env, cwd=OUT)
    if r.returncode:
        sys.exit("[build_design_x64] %s FAILED" % what)


def x64_ready():
    """The x64 build's objects, once it has linked them: Hyperion.exe newer than its link list."""
    exe = os.path.join(bx.OUT, "Hyperion.exe")
    rsp = os.path.join(bx.OUT, "link.rsp")
    if not (os.path.isfile(exe) and os.path.isfile(rsp)) or os.path.getmtime(exe) < os.path.getmtime(rsp):
        sys.exit("[build_design_x64] no finished x64 build in %s: run python tools/build_engine_x64.py first" % bx.OUT)
    objs = sorted(glob.glob(os.path.join(bx.OUT, "*.obj")))
    if not objs:
        sys.exit("[build_design_x64] the x64 build has no objects: run python tools/build_engine_x64.py")
    return objs


def luau_lib(env):
    """Luau's library, built once for its version under OUT/luau."""
    lib = os.path.join(OUT, "luau", "luau.lib")
    if os.path.isfile(lib):
        return lib
    if not os.path.isfile(os.path.join(LUAU, "VM", "include", "lua.h")):
        sys.exit("[build_design_x64] no Luau in %s: run python tools/fetch_luau.py" % LUAU)
    objs = []
    for part in LUAU_PARTS:
        dest = os.path.join(OUT, "luau", part)
        os.makedirs(dest, exist_ok=True)
        srcs = sorted(glob.glob(os.path.join(LUAU, part, "src", "*.cpp")))
        run(["cl", "/nologo", "/c", "/MP", "/O2", "/DNDEBUG", "/MT", "/EHsc", "/std:c++17", "/bigobj", "/W0",
             "/Fo" + dest + "\\"] + ["/I" + i for i in LUAU_INCLUDES] + ["/I" + os.path.join(LUAU, part, "src")]
            + srcs, env, "cl /c Luau %s (%d)" % (part, len(srcs)))
        objs += sorted(glob.glob(os.path.join(dest, "*.obj")))
    run(["lib", "/nologo", "/OUT:" + lib] + objs, env, "lib luau.lib")
    return lib


def imgui_lib(env):
    """Dear ImGui's library with its SDL3 and SDL_GPU backends, built once for its version under
    OUT/imgui, where abs_globals never looks."""
    lib = os.path.join(OUT, "imgui", "imgui.lib")
    if os.path.isfile(lib):
        return lib
    if not os.path.isfile(os.path.join(IMGUI, "imgui.h")):
        sys.exit("[build_design_x64] no Dear ImGui in %s: run python tools/fetch_imgui.py" % IMGUI)
    dest = os.path.join(OUT, "imgui")
    os.makedirs(dest, exist_ok=True)
    srcs = [os.path.join(IMGUI, s) for s in IMGUI_SOURCES]
    run(["cl", "/nologo", "/c", "/MP", "/O2", "/DNDEBUG", "/MT", "/EHsc", "/W0", "/Z7", "/Fo" + dest + "\\"] +
        ["/I" + i for i in IMGUI_INCLUDES] + ["/I" + os.path.join(bx.SDL3, "include")] + srcs,
        env, "cl /c Dear ImGui (%d)" % len(srcs))
    run(["lib", "/nologo", "/OUT:" + lib] + sorted(glob.glob(os.path.join(dest, "*.obj"))), env, "lib imgui.lib")
    return lib


def shaders(env):
    """The design engine's shaders, and the x64 build's it keeps its own copies of, as DXIL
    for Direct3D 12 and SPIR-V for Vulkan, into headers the hooked files find before the x64's."""
    os.makedirs(DESIGN_SHADERS, exist_ok=True)
    entries = [(os.path.join(DESIGN_SRC, "shaders", src), entry, profile, [])
               for src, entry, profile in DESIGN_SHADER_ENTRIES]
    entries += [(os.path.join(bx.SHADER_SRC, src), entry, profile, ["-D", "HX_DESIGN"])
                for src, entry, profile in bx.SHADER_ENTRIES if src in HOOKED_SHADERS]
    for hlsl, entry, profile, defines in entries:
        src = os.path.basename(hlsl)
        run([bx.DXC, "-nologo", "-O3", "-Wno-ignored-attributes", "-T", profile, "-E", entry] + defines + [hlsl,
             "-Fh", os.path.join(DESIGN_SHADERS, entry + ".dxil.h"), "-Vn", "k%sDxil" % entry],
            env, "dxc %s %s DXIL" % (src, entry))
        run([bx.DXC, "-nologo", "-O3", "-T", profile, "-E", entry, "-spirv", "-fspv-target-env=vulkan1.0"] + defines +
            [hlsl, "-Fh", os.path.join(DESIGN_SHADERS, entry + ".spv.h"), "-Vn", "k%sSpv" % entry],
            env, "dxc %s %s SPIR-V" % (src, entry))


def main():
    objs = x64_ready()
    os.makedirs(OUT, exist_ok=True)
    for f in glob.glob(os.path.join(OUT, "*.obj")):
        os.remove(f)
    env = bx.vs_env()
    luau = luau_lib(env)
    imgui = imgui_lib(env)
    shaders(env)

    # the x64 build's objects, its globals' block among them, less the hooked ones made here;
    # copies, so nothing done here can reach the x64 build's own
    hooked = {os.path.splitext(h)[0] + ".obj" for h in HOOKED}
    for o in objs:
        if os.path.basename(o) not in hooked:
            shutil.copy2(o, os.path.join(OUT, os.path.basename(o)))

    flags = bx.PORT + ["/DHX_DESIGN", "/Fo" + OUT + "\\"]
    run(["cl", "/I" + DESIGN_SHADERS] + flags + ["/MP"] + [port_path(h) for h in HOOKED], env,
        "cl /c %d hooked TUs" % len(HOOKED))
    design = sorted(glob.glob(os.path.join(DESIGN_SRC, "*.cpp")))
    run(["cl"] + flags + ["/MP", "/I" + DESIGN_SHADERS] + ["/I" + i for i in LUAU_INCLUDES + IMGUI_INCLUDES] + design,
        env, "cl /c %d design TUs" % len(design))

    hyp = os.path.join(ENGINE, "hyperion")
    lrsp = os.path.join(OUT, "link.rsp")
    with open(lrsp, "w", encoding="utf-8") as f:
        f.write("\n".join('"%s"' % o for o in sorted(glob.glob(os.path.join(OUT, "*.obj")))
                          + [os.path.join(bx.OUT, "hyperion.o")]) + "\n")
    libs = [os.path.join(bx.OUT, "stormlib", "stormlib.lib"), os.path.join(bx.OUT, "casclib", "casclib.lib"),
            os.path.join(bx.OUT, "webm", "webm.lib"), os.path.join(bx.OUT, "image", "image.lib"),
            os.path.join(bx.OUT, "dav1d", "dav1d.lib")]
    run(["link", "/nologo", "/MACHINE:X64", "/SUBSYSTEM:WINDOWS", "/ENTRY:wWinMainCRTStartup", "/DEBUG",
         "/MANIFEST:EMBED", "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' "
         "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'",
         "/OPT:REF", "/OPT:ICF", "/DEF:" + os.path.join(hyp, "hyperion.def"),
         "/OUT:" + os.path.join(OUT, "HyperionDesign.exe"), "/MAP:" + os.path.join(OUT, "HyperionDesign.map"),
         "@" + lrsp, os.path.join(bx.OUT, "hyperion.res"), os.path.join(bx.OUT, "fonts.res")] + libs +
        [os.path.join(bx.SDL3, "lib", "x64", "SDL3.lib"), "/DELAYLOAD:SDL3.dll", "delayimp.lib", "bcrypt.lib"] +
        [os.path.join(bx.WEBRTC, "lib", l) for l in bx.WEBRTC_LIBS] + ["iphlpapi.lib"] + bx.LIBS + [luau, imgui],
        env, "link HyperionDesign.exe")
    bx.copy_web(OUT)
    bx.copy_huds(OUT)
    shutil.copyfile(os.path.join(bx.SDL3, "lib", "x64", "SDL3.dll"), os.path.join(OUT, "SDL3.dll"))
    say("OK -> src117\\build\\design\\HyperionDesign.exe")


if __name__ == "__main__":
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from buildlock import BuildLock
    with BuildLock("build_design_x64"):
        main()
