"""Fetch Luau, the scripting language the design engine runs effects in, into engine/third_party.

The design engine (docs/modules/design.md) runs the effects of design/fx/ as Luau scripts: Lua with a
sandbox built in and a faster interpreter. Its source release lands in
engine/third_party/luau-<version>/, which is gitignored: the repository carries the version and
the checksum, not the files. Luau is under the MIT licence (Roblox, and Lua.org for the Lua it
came from). Only tools/build_design_x64.py compiles it, so Hyperion.exe carries none of it.

    python tools/fetch_luau.py

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

VERSION = "0.742"
URL = f"https://github.com/luau-lang/luau/archive/refs/tags/{VERSION}.zip"
SHA256 = "0d2906b02aa4fa0c0707fa212adec0e2cbd1adb3866fb1efa6d684e72b482855"

ENGINE = Path(__file__).resolve().parents[1]
DEST = ENGINE / "third_party" / f"luau-{VERSION}"
# what the design engine builds: the parser, the compiler to bytecode and the VM
PARTS = ["Common", "Ast", "Bytecode", "Compiler", "VM"]


def main() -> int:
    if (DEST / "VM" / "include" / "lua.h").exists():
        print(f"Luau {VERSION} already in {DEST}")
        return 0
    print(f"downloading {URL}")
    with urllib.request.urlopen(URL) as r:
        data = r.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SHA256:
        print(f"checksum mismatch: {digest}, want {SHA256}")
        return 1
    top = f"luau-{VERSION}/"
    DEST.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        for name in z.namelist():
            rel = name[len(top):] if name.startswith(top) else None
            if not rel or rel.endswith("/"):
                continue
            if rel.split("/")[0] not in PARTS and rel not in ("LICENSE.txt", "README.md"):
                continue
            out = DEST / rel
            out.parent.mkdir(parents=True, exist_ok=True)
            with z.open(name) as src, open(out, "wb") as dst:
                shutil.copyfileobj(src, dst)
    if not (DEST / "VM" / "include" / "lua.h").exists():
        print(f"unexpected package layout: no VM/include/lua.h under {DEST}")
        return 1
    print(f"Luau {VERSION} -> {DEST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
