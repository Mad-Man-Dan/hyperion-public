# Windows, Linux and Mac

## Lede

Hyperion runs on 64-bit Windows, on Linux on ordinary PCs and on ARM boards, on Macs with Apple
silicon, and in a web browser. On every one it plays the same game: a replay plays frame for
frame the same on Windows, Linux and the Mac, and the test suite checks it.

## How to use it

### What each needs

| System | The program | Drawn through | Needs |
|---|---|---|---|
| Windows | `Hyperion.exe` | Vulkan, or Direct3D 12 | Windows 10 or 11, 64-bit, a graphics card with Direct3D 12 or Vulkan |
| Linux, x86-64 | `Hyperion.x86_64` | Vulkan | a 64-bit PC with a Vulkan driver |
| Linux, ARM64 | `Hyperion.aarch64` | Vulkan | a 64-bit ARM board with a Vulkan driver (tested on an NVIDIA Jetson Orin NX) |
| Mac | `Hyperion.app` | Metal | Apple silicon (M1 or later), macOS 13 Ventura or later |
| Browser | a page a LAN host serves | WebGL | a current browser; nothing installed |

Each also needs StarCraft's own files, one of:

- **StarCraft from Battle.net**, the free StarCraft: Remastered. The first start asks for its
  folder, filled in when Hyperion finds it (where Battle.net installed it on Windows,
  `/Applications/StarCraft` on a Mac, Lutris's and Wine's usual places on Linux); Settings,
  Data and mods, **StarCraft from Battle.net** changes it later.
- **A classic StarCraft folder** (1.16.1 and prior; Settings, Data and mods, **Classic StarCraft
  folder**) with its MPQs: `StarDat.mpq`, `BrooDat.mpq`
  and `patch_rt.mpq`, with `StarCraft.mpq` and `BroodWar.mpq` for the music and movies, and
  `Local.dll`. No `StarCraft.exe` is needed.

**Game files** on the same tab chooses between the two. Either way the game is drawn in its
classic art.

### Installing

- **Windows**: unzip the download into a folder of its own, anywhere, and run `Hyperion.exe`.
- **Linux**: unpack the tarball into a folder of its own and run `Hyperion.x86_64` or
  `Hyperion.aarch64`.
- **Mac**: unzip `Hyperion.app` into a folder. It is not signed by Apple, so the first start is
  allowed once in System Settings, Privacy & Security, **Open Anyway**; the README says how.

Hyperion keeps everything of its own in `Data` beside the program (beside the app on a Mac):
settings, profiles, replays, saved games, maps, mods, HUD designs, music and screenshots. The
documents are in `docs` beside it. Your StarCraft install is only read, never written.

### The browser

A LAN host's room serves a page; a phone or a PC joins from it with nothing installed
([Playing in a browser](@browser)).

## Limits and known issues

- On Linux and the Mac, the music player's "Playing on this computer" (other apps' music) says
  "Not available yet"; the rest of the player works.
- The Korean input method is Windows only; elsewhere SDL's own composition stands in.
- The Renderer choice (Vulkan or Direct3D 12) is Windows only.
- The browser build draws through the software path: no HUD designs, no zoom past what the
  software draws, 1.17's menus in place of Hyperion's, and no music player.
- The Mac with StarCraft from Battle.net in `/Applications/StarCraft` has not been tried with an
  install there yet.

## In progress and planned

- **Quick setup** (planned): one download for each system, with everything in it. Packages are
  built for testing today.
- **The game from the host** (planned): a LAN host hands out each system's install.
- **WebGPU in the browser** (parked): the GPU drawing, zoom, lights and effects in a browser.
- **Other apps' music on Linux**, through Linux's media controls (planned).

## Technical notes

- One window layer on every system: the window is SDL's, and `src117/port/platform/port_winmsg.cpp`
  makes Windows' messages out of SDL's events, so 1.17's window procedure and message pumps are
  unchanged. Off Windows, `src117/compat/` gives the Windows SDK's headers and `port_posix.cpp`
  the functions, over POSIX and SDL.
- The settings file `settings.json` stands in for the registry (`port_settings.cpp`).
- The builds: `engine/tools/build_engine_x64.py` (Windows), `build_engine_linux.py` (Linux x64,
  `--arch arm64` for ARM, and run on a Mac for the Mac), `build_engine_web.py` (64-bit
  WebAssembly with Emscripten; threads are fibers). The Mac's shaders are the same shaders turned
  into Metal Shading Language by SPIRV-Cross.
- Packages: `engine/tools/package.py` (`--linux x64|arm64` for a tarball, `--mac` on a Mac for
  `Hyperion.app` signed ad hoc). A package carries no Blizzard files.
- The gate: the replay suite plays every replay on each build and compares it with 1.17 frame
  for frame; Linux x64 and ARM64 play the quick suite on each merge
  (`engine/docs/replay-test.md`).
- On a Mac the app's folder is found through the Security framework even when macOS runs a
  downloaded app from a read-only copy (App Translocation), so the first start reads the folder
  you unzipped it in.
- Module docs: `engine/docs/modules/platform.md` and `engine/docs/modules/build.md`.
