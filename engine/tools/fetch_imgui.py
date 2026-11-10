"""Fetch Dear ImGui, the design engine's panels, into engine/third_party.

The design engine (docs/modules/design.md) draws its panels with Dear ImGui's docking branch,
through its SDL3 platform backend and its SDL_GPU renderer backend. Its source release lands in
engine/third_party/imgui-<version>/, which is gitignored: the repository carries the version and
the checksum, not the files. Dear ImGui is under the MIT licence (Omar Cornut). Only
tools/build_design_x64.py compiles it, so Hyperion.exe carries none of it.

    python tools/fetch_imgui.py

The download is checked against the SHA-256 recorded here. A folder that is already there is
left alone.
"""

import hashlib
import io
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path

VERSION = "1.93.0-docking"
URL = f"https://github.com/ocornut/imgui/archive/refs/tags/v{VERSION}.zip"
SHA256 = "302ccb19cf6ff803ebc970b4f96d59c95dd3a32169453ad5c503cab67c057992"

ENGINE = Path(__file__).resolve().parents[1]
DEST = ENGINE / "third_party" / f"imgui-{VERSION}"
# what the design engine builds: the library itself and the two SDL backends
BACKENDS = ["imgui_impl_sdl3", "imgui_impl_sdlgpu3"]


def wanted(rel: str) -> bool:
    if "/" not in rel:
        return rel.endswith((".cpp", ".h")) or rel in ("LICENSE.txt",)
    folder, name = rel.split("/", 1)
    return folder == "backends" and "/" not in name and name.split(".")[0].startswith(tuple(BACKENDS))


def main() -> int:
    if (DEST / "imgui.h").exists():
        print(f"Dear ImGui {VERSION} already in {DEST}")
        return 0
    print(f"downloading {URL}")
    with urllib.request.urlopen(URL) as r:
        data = r.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SHA256:
        print(f"checksum mismatch: {digest}, want {SHA256}")
        return 1
    top = f"imgui-{VERSION}/"
    DEST.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        for name in z.namelist():
            rel = name[len(top):] if name.startswith(top) else None
            if not rel or rel.endswith("/") or not wanted(rel):
                continue
            out = DEST / rel
            out.parent.mkdir(parents=True, exist_ok=True)
            with z.open(name) as src, open(out, "wb") as dst:
                shutil.copyfileobj(src, dst)
    if not (DEST / "imgui.h").exists() or not (DEST / "backends" / "imgui_impl_sdlgpu3.cpp").exists():
        print(f"unexpected package layout: no imgui.h or SDL_GPU backend under {DEST}")
        return 1
    print(f"Dear ImGui {VERSION} -> {DEST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
