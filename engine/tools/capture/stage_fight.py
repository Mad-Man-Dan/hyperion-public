"""The HUD moment staged, and saved for the recipes to load (recordings/raw/scenes/hudfight.snx).

    python tools/capture/stage_fight.py [<seed> [<frame> [<name>]]]     8202, 15000, hudfight

A replay selects one unit at a time, as 1.17's does, so the HUD's shots come from a live game: a
skirmish on Big Game Hunters by Hyperion's rules, the local Terran with three allied computers
against four, Protoss only (a Zerg game's creep tiles can differ from its replay's, the tracker's
creep tiles bug). The local player gets a base and a guard by spawns (a building where the search
finds it room, away from the minerals), and its SCVs are sent mining with a right click. The game
plays fast to the frame; then an army of 26 is spawned on the ground of the enemy with the most
fighters at home, just outside its defenders, attacks its start for 110 frames, is selected and
saved through the game menu (F10, Save Game). The pause lands a few frames after it is asked for,
so a second run makes a game like this one, not the same one; the recipes load the save, which
is the same every time. The army's place is printed for the recipes' camera and select steps.
"""
import json
import math
import shutil
import sys
import time

import capture

SEED = int(sys.argv[1]) if len(sys.argv) > 1 else 8202
FRAME = int(sys.argv[2]) if len(sys.argv) > 2 else 15000
NAME = sys.argv[3] if len(sys.argv) > 3 else "hudfight"
FIGHT = 110
MAP = "E:/StarCraft/Maps/BroodWar/WebMaps/(8)Big Game Hunters.scm"
ARGS = [f"--map={MAP}", f"--seed={SEED}", "--race=terran", "--computers=7", "--computer-race=protoss",
        "--rules=hyperion", "--teams=1,1,1,1,2,2,2,2", "--ai-levels=c6662222"]
NOT_FIGHTERS = {7, 13, 14, 35, 36, 41, 42, 64, 73, 85, 97}  # workers, larvae, eggs, mines, scarabs and the like
MINERALS = {176, 177, 178}
UNPATHABLE = 0x1FFD


def main() -> int:
    proc, (hwnd, w, h), c = capture.start({"args": ARGS, "wait": 8, "env": {"HYPERION_TEST_MOUSE": "1"}})
    call = lambda cmd, timeout=30.0, **kw: c.call(cmd, timeout=timeout, **kw)
    units = lambda p: call("units", player=p, max=800).get("units", [])
    try:
        call("pause")
        players = call("players")["players"]
        local = next(p["slot"] for p in players if p.get("type") == 2)
        cc = next(u for u in units(local) if u["type"] == 106)
        X, Y = cc["x"], cc["y"]
        fields = sorted((u for u in units(11) if u["type"] in MINERALS),
                        key=lambda u: math.hypot(u["x"] - X, u["y"] - Y))[:8]
        toward = math.atan2(sum(u["y"] for u in fields) / len(fields) - Y, sum(u["x"] for u in fields) / len(fields) - X)

        # the SCVs mining: selected by the channel, a right click on a field above the console
        call("spawn", unit=7, x=X, y=Y + 96, player=local, count=8)
        call("camera", x=X, y=Y - 60)
        call("resume")
        time.sleep(0.5)
        call("select", x=X, y=Y + 96, player=local, radius=160)
        v = call("status")["view"]
        for m in fields:
            sx, sy = int(w / 2 + (m["x"] - v["x"]) * v["zoom"]), int(h / 2 + (m["y"] - v["y"]) * v["zoom"])
            if 100 < sx < w - 100 and 100 < sy < 760:
                break
        call("click", x=sx, y=sy, right=True)
        time.sleep(0.5)
        call("pause")

        # the base: each building where a ring round the command centre has room, off the minerals' side
        def place(unit, want, r0, r1):
            made = 0
            for r in range(r0, r1 + 1, 2):
                for k in range(16):
                    a = 2 * math.pi * k / 16
                    if abs(math.remainder(a - toward, 2 * math.pi)) < 1.4:
                        continue
                    x, y = int(X + r * 32 * math.cos(a)), int(Y + r * 32 * math.sin(a))
                    if 64 < x < 4032 and 64 < y < 4032 and call("spawn", unit=unit, x=x, y=y, player=local, count=1).get("ids"):
                        made += 1
                        if made == want:
                            return made
            return made

        for unit, want, r0, r1 in [(109, 8, 6, 14), (111, 2, 6, 12), (113, 1, 6, 12), (112, 1, 6, 12),
                                   (122, 1, 6, 14), (125, 1, 6, 12), (124, 2, 5, 12)]:
            print("stage: building %d, %d of %d" % (unit, place(unit, want, r0, r1), want))
        gx, gy = X + (-256 if X > 2048 else 256), Y + (-256 if Y > 2048 else 256)
        for unit, n in [(0, 8), (5, 2), (34, 2)]:
            call("spawn", unit=unit, x=gx, y=gy, player=local, count=n)

        frame = call("status")["frame"]
        while frame < FRAME:
            call("step", timeout=120.0, frames=min(5000, FRAME - frame), fast=True)
            frame = call("status")["frame"]

        # the enemy with the most fighters at home; a walkable point of its ground 450-760 px from
        # its start and 400 or more from its defenders, the nearest them
        # --teams gives the computers their teams in slot order: the first three are the local player's
        computers = sorted(p["slot"] for p in players if p.get("type") == 1)
        best = None
        for p in call("players")["players"]:
            if p["slot"] not in computers[3:]:
                continue
            sx0, sy0 = p["start"]
            foes = [u for u in units(p["slot"]) if u.get("type", 999) < 106 and u["type"] not in NOT_FIGHTERS
                    and abs(u["x"] - sx0) < 900 and abs(u["y"] - sy0) < 900]
            if best is None or len(foes) > len(best[2]):
                best = (p["slot"], (sx0, sy0), foes)
        slot, (ex, ey), foes = best
        fx = sum(u["x"] for u in foes) // max(1, len(foes)) if foes else ex
        fy = sum(u["y"] for u in foes) // max(1, len(foes)) if foes else ey
        area = call("region", x=ex, y=ey).get("area")
        spots = []
        for r in range(320, 760, 60):
            for k in range(24):
                a = 2 * math.pi * k / 24
                x, y = int(fx + r * math.cos(a)), int(fy + r * math.sin(a))
                if not (96 < x < 4000 and 96 < y < 4000):
                    continue
                if not 450 <= math.hypot(x - ex, y - ey) <= 760 or math.hypot(x - fx, y - fy) < 400:
                    continue
                g = call("region", x=x, y=y)
                if g.get("group") != UNPATHABLE and g.get("area") == area:
                    spots.append((math.hypot(x - fx, y - fy), x, y))
        ax, ay = min(spots)[1:] if spots else (ex, ey)
        ids = []
        for unit, n in [(0, 12), (32, 4), (34, 4), (5, 2), (3, 2), (1, 2)]:
            ids += call("spawn", unit=unit, x=ax, y=ay, player=local, count=n, attack=[ex, ey]).get("ids", [])
        call("step", timeout=60.0, frames=FIGHT, fast=True)
        alive = [u for u in call("units", ids=ids).get("units", []) if u.get("alive")]
        mx, my = sum(u["x"] for u in alive) // len(alive), sum(u["y"] for u in alive) // len(alive)
        rad = max(max(abs(u["x"] - mx), abs(u["y"] - my)) for u in alive) + 32
        call("camera", x=mx, y=my + 80)
        call("select", x=mx, y=my, player=local, radius=rad)
        time.sleep(2)

        # saved: F10, Save Game, the name box cleared and a new name (an old one asks to overwrite)
        call("press", vk=0x79)
        time.sleep(1.0)
        call("press", key="S", char="s")
        time.sleep(1.0)
        for _ in range(24):
            call("press", vk=8)
        typed = "hf" + time.strftime("%H%M%S")
        for ch in typed:
            call("press", key=ch.upper(), char=ch)
            time.sleep(0.1)
        call("press", key="enter")
        time.sleep(2.0)
        saved = sorted(capture.DATA.rglob(typed + ".snx"))
        if not saved:
            print("stage: the game was not saved")
            return 1
        capture.SCENES.mkdir(parents=True, exist_ok=True)
        out = capture.SCENES / f"{NAME}.snx"
        shutil.copyfile(saved[-1], out)
        print("stage: %s, the local player %d, %d of the army alive against player %d" % (out, local, len(alive), slot))
        print("stage: the army " + json.dumps({"camera": [mx, my - 36], "select": {"x": mx, "y": my, "player": local,
                                                                                  "radius": rad}}))
        return 0
    finally:
        c.close()
        proc.kill()


if __name__ == "__main__":
    sys.exit(main())
