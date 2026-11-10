"""Fetch dr_libs' audio decoders into engine/third_party.

A mod's sounds and music in MP3, FLAC and WAV (src117/port/data/port_audiodecode.cpp) are read
through dr_mp3, dr_flac and dr_wav; Ogg
Vorbis goes through stb_vorbis (tools/fetch_stb.py). The files land in
engine/third_party/dr_libs-<commit>/, which is gitignored: the repository carries the commit and
the checksums, not the files. dr_libs is public domain, or MIT-0 where that is not recognised.

    python tools/fetch_drlibs.py

dr_libs has no releases; the files are pinned at a commit, each checked against the SHA-256 it
had when it was pinned. A file that is already there is left alone.
"""

import hashlib
import sys
import urllib.request
from pathlib import Path

VERSION = "b6a9189"  # dr_mp3 0.7.5, dr_flac 0.13.5, dr_wav 0.14.7 (2026-11-09)
COMMIT = "b6a91899fd5ee9508aab6a41b9f51ff8b4987317"
FILES = {
    "dr_mp3.h": "e5e8cc2a449ad312fbb2e97bd8b085aab9362bf99ddd8e34684f317ef048381f",
    "dr_flac.h": "3f010d3142a5b9df80a0bd8b325de54317468b1aff85abb257c86dbfefe9bd2a",
    "dr_wav.h": "7c9660d75361ad6175e6217c63a1075c9512445c06bc951197fc479179f76b39",
}

ENGINE = Path(__file__).resolve().parents[1]
DEST = ENGINE / "third_party" / f"dr_libs-{VERSION}"


def main() -> int:
    for name, sha256 in FILES.items():
        path = DEST / name
        if path.exists():
            print(f"{name} {VERSION} already in {DEST}")
            continue
        url = f"https://raw.githubusercontent.com/mackron/dr_libs/{COMMIT}/{name}"
        print(f"downloading {url}")
        with urllib.request.urlopen(url) as r:
            data = r.read()
        digest = hashlib.sha256(data).hexdigest()
        if digest != sha256:
            print(f"checksum mismatch: {digest}, want {sha256}")
            return 1
        DEST.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        print(f"{name} {VERSION} -> {DEST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
