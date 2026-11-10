"""A dozen tracks for the music player's My music shots (the recipe mp-mymusic), made here so
nothing on screen is anyone's library: chords of sines with a pulse, as Ogg Vorbis with plain
invented tags, into the capture data folder's music folder. Run once before the recipe.

    python tools/capture/make_tracks.py
"""
import pathlib
import subprocess

OUT = pathlib.Path(__file__).resolve().parents[2] / "testrun" / "capture" / "music"
TRACKS = [
    ("Low Orbit", "Night Shift", 196), ("Low Orbit", "Relay Station", 224), ("Low Orbit", "Static Bloom", 181),
    ("Marrow Lane", "Ash Fields", 247), ("Marrow Lane", "Outpost Lights", 203), ("Marrow Lane", "Dust Road", 215),
    ("The Quiet Engines", "Cold Start", 172), ("The Quiet Engines", "Harbour", 238), ("The Quiet Engines", "Long Haul", 263),
    ("Pale Signal", "First Contact", 199), ("Pale Signal", "Drift", 186), ("Pale Signal", "Homeward", 231),
]
ROOTS = [110.0, 123.47, 130.81, 146.83, 98.0, 87.31]


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    for i, (artist, title, seconds) in enumerate(TRACKS):
        f = ROOTS[i % len(ROOTS)]
        beat = (84 + 6 * i) / 60.0
        expr = ("0.22*sin(2*PI*{f}*t)+0.14*sin(2*PI*{f5}*t)+0.10*sin(2*PI*{f8}*t)*(0.5+0.5*sin(2*PI*0.25*t))"
                "+0.18*sin(2*PI*{fb}*t)*exp(-6*mod(t,{p}))+0.06*sin(2*PI*{fh}*t)*(0.5+0.5*sin(2*PI*{b2}*t))").format(
            f=f, f5=f * 1.5, f8=f * 2, fb=f / 2, p=1.0 / beat, fh=f * 4, b2=beat * 2)
        path = OUT / ("%02d %s - %s.ogg" % (i + 1, artist, title))
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                        "aevalsrc='%s|%s':s=44100:d=%d" % (expr, expr, seconds), "-c:a", "libvorbis", "-q:a", "2",
                        "-metadata", "artist=" + artist, "-metadata", "title=" + title, "-metadata", "album=" + artist,
                        "-metadata", "tracknumber=%d" % (i % 3 + 1), str(path)], check=True)
        print(path.name, path.stat().st_size)


if __name__ == "__main__":
    main()
