"""Hyperion Studio's model checked end to end over its channel, against a studio started for it.

    python tools/studio.py --background --port 27132 --folder studio-test
    python tools/studio_test.py [--port 27132] [--folder studio-test] [--no-game]

A copy of the hybrid-test mod (mods/studio-check in the studio's game folder) is opened, read, changed,
undone, saved and pushed into the canvas game, and the studio's values are checked against the
loader's own rows (studio_verify). Each step prints ok or what went wrong; the exit code is the
number of steps that failed. --no-game leaves out the steps that need the canvas game.
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
import design  # noqa: E402  (the channel's client)
import play  # noqa: E402

ROOT = play.ROOT

failed = 0


def step(name, ok, detail=""):
    global failed
    print(("ok    " if ok else "FAIL  ") + name + ("" if ok else ": " + str(detail)[:400]), flush=True)
    if not ok:
        failed += 1
    return ok


def field(answer, key):
    for f in answer.get("fields", []):
        if f["key"] == key:
            return f
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--port", type=int, default=27132)
    ap.add_argument("--folder", default="studio-test")
    ap.add_argument("--no-game", action="store_true")
    a = ap.parse_args()
    game = play.rt.TESTRUN / a.folder
    src = game / "mods" / "hybrid-test"
    mod = game / "mods" / "studio-check"
    if mod.exists():
        shutil.rmtree(mod)
    shutil.copytree(src, mod)
    mj = json.loads((mod / "mod.json").read_text(encoding="utf-8"))
    mj["id"], mj["name"] = "studio-check", "Studio check"
    (mod / "mod.json").write_text(json.dumps(mj, indent=2) + "\n", encoding="utf-8")
    # an animation whose conditional jumps branch three ways, one only on a branch, for the player's lanes
    isc = mod / "data" / "iscript.txt"
    text = isc.read_bytes().decode("ascii")
    text = text.replace("SpecialState1 [NONE]", "SpecialState1 HybridReaverBranch")
    text += "\r\n".join(["", "HybridReaverBranch:", "        playfram      0x00", "        wait          2",
                          "        randcondjmp   128 HybridReaverBranchB", "        liftoffcondjmp HybridReaverBranchC",
                          "        playfram      0x11", "        wait          3", "        end",
                          "HybridReaverBranchB:", "        playfram      0x22", "        wait          2",
                          "        trgtrangecondjmp 64 HybridReaverBranchC", "        playfram      0x33",
                          "        wait          2", "        end",
                          "HybridReaverBranchC:", "        playfram      0x44", "        wait          4", "        end", ""])
    isc.write_bytes(text.encode("ascii"))

    with design.client(a.port, timeout=10.0) as c:
        def call(cmd, **kw):
            return c.call(cmd, timeout=30.0, **kw)

        # a mod started in the studio: its folder, mod.json and empty types.json made, and opened
        fresh = game / "mods" / "studio-new"
        if fresh.exists():
            shutil.rmtree(fresh)
        r = call("studio_new_mod", name="Studio New", id="studio-new")
        step("a new mod made and opened", r.get("ok") and (fresh / "mod.json").exists()
             and (fresh / "data" / "types.json").exists(), r)
        r = call("studio_new_mod", name="Studio New", id="studio-new")
        step("a second with the same id is refused", not r.get("ok"), r)

        r = call("studio_open", folder=str(mod))
        step("open a mod by its folder", r.get("ok") and r.get("mod") == "studio-check", r)
        # the animation player: a lane for each other way a condition goes, and one only a lane comes to
        r = call("studio_art_anim", image="hybrid_reaver", anim="SpecialState1")
        lanes = r.get("lanes", [])
        step("an animation's branches each have a lane", r.get("ok") and len(lanes) >= 3, r)
        step("a condition only a branch comes to has its lane", any(", " in l["name"] for l in lanes), lanes)
        # overlays that overlays make, playing on once the image itself has ended
        r = call("studio_art_anim", image="zerg_overmind_with_shell", anim="Death")
        made = [o for o in r.get("overlay_runs", []) if o.get("made_by", -1) >= 0]
        step("an overlay's own overlays are shown", r.get("ok") and made, r.get("overlay_runs"))
        step("its overlays play on after the image ends", r.get("played", 0) > len(r.get("ticks", [])),
             (r.get("played"), len(r.get("ticks", []))))

        # a classic button's words changed by the mod: its text and its text while it cannot
        r = call("studio_card_set", unit="protoss_gateway",
                 button={"position": 1, "classic": "protoss_zealot", "text": "Warp in <Z>ealot Guard",
                         "disabled": "Zealot Guard Requires:\n   Gateway"})
        step("a classic button's words given", r.get("ok"), r)
        btn = [b for b in call("studio_card", unit="protoss_gateway").get("buttons", [])
               if b.get("target") == "protoss_zealot"]
        step("the card shows the mod's words over the classic ones",
             btn and btn[0]["reworded"] and btn[0]["text"] == "Warp in <Z>ealot Guard"
             and btn[0]["classic_text"] != btn[0]["text"], btn)
        r = call("studio_card_set", unit="protoss_gateway",
                 button={"position": 1, "classic": "protoss_zealot", "train": "hybrid_reaver"})
        step("words that also train are refused", not r.get("ok"), r)
        call("studio_undo")
        btn = [b for b in call("studio_card", unit="protoss_gateway").get("buttons", [])
               if b.get("target") == "protoss_zealot"]
        step("undo gives the classic words back", btn and not btn[0]["reworded"], btn)

        # a race's own console and music, another race's; a word that names none refused
        r = call("studio_race_set", race="hybrid", field="console", value="zerg")
        step("a race's console set", r.get("ok"), r)
        r = call("studio_race_set", race="hybrid", field="music", value="elves")
        step("a music that is no race's is refused", not r.get("ok"), r)
        call("studio_undo")
        races = {x["race"]: x["record"] for x in call("studio_races").get("races", [])}
        step("undo takes the console back", "console" not in races.get("hybrid", {"console": 1}), races.get("hybrid"))

        # a stand-in let into the casters of what the unit it replaces cast: the High Templar's Psionic
        # Storm, a classic tech, given an edit whose casters take the Hybrid Reaver
        rec = races.get("hybrid", {})
        call("studio_race_set", race="hybrid", field="without",
             value=list(rec.get("without", [])) + ["protoss_high_templar"])
        ai = dict(rec.get("ai", {}))
        ai["protoss_high_templar"] = "hybrid_reaver"
        call("studio_race_set", race="hybrid", field="ai", value=ai)
        r = call("studio_race_fix", race="hybrid", unit="protoss_high_templar")
        storm = [m for m in r.get("mended", []) if m.get("name") == "psionic_storm" and m.get("casters")]
        step("the race fix lets a stand-in cast what the unit it replaces cast", r.get("ok") and storm, r)
        if storm:
            use = field(call("studio_get", table="techs", name=storm[0]["record"]), "use_requires")
            step("the classic casters kept, the stand-in beside them",
                 use and "protoss_high_templar" in json.dumps(use) and "hybrid_reaver" in json.dumps(use), use)
        for _ in range(3):
            call("studio_undo")
        races = {x["race"]: x["record"] for x in call("studio_races").get("races", [])}
        step("undo takes the fix and the race's change back",
             "protoss_high_templar" not in json.dumps(races.get("hybrid", {})), races.get("hybrid"))

        # ---- the Strings workspace (studio_strings.cpp) ----
        # every text a player sees, each with where it is and where it comes from
        def strings(**kw):
            return {s["id"]: s for s in call("studio_strings", **kw).get("strings", [])}
        r = call("studio_strings")
        ids = {s["id"]: s for s in r.get("strings", [])}
        step("the strings listed", r.get("ok") and r.get("count", 0) > 500, r.get("count"))
        hr = ids.get("unit/hybrid_reaver")
        step("a unit's name is the mod's", hr and hr["text"] == "Hybrid Reaver" and hr["source"] == "mod", hr)
        warp = ids.get("card/protoss_gateway/m0/text")
        step("a button's text has its hotkey marked", warp and warp["text"] == "Warp in Hybrid <R>eaver"
             and warp["hotkey"] == "r", warp)
        zid = "card/protoss_gateway/c1-protoss_zealot/text"
        step("a classic button's words are classic and editable", zid in ids and ids[zid]["source"] == "classic"
             and ids[zid]["editable"], ids.get(zid))
        step("the mod's page and its race are there", ids.get("mod/name", {}).get("source") == "mod"
             and ids.get("race/hybrid", {}).get("text") == "Hybrid", (ids.get("mod/name"), ids.get("race/hybrid")))
        r = call("studio_strings", kind="button", mine=True)
        step("the filters leave the mod's buttons", r.get("ok") and r.get("count", 0) >= 4
             and all(s["kind"] == "button" and s["source"] == "mod" for s in r.get("strings", [])), r.get("count"))
        step("a search finds a marked text by its words",
             any("<L>ance" in s["text"] for s in strings(query="psionic lance", kind="button").values()))
        r = call("studio_string_set", id=zid, text="Warp in Zealot <G>uard")
        s = r.get("string") or {}
        step("a classic button's text given", r.get("ok") and s.get("source") == "mod" and s.get("hotkey") == "g"
             and s.get("classic"), r)
        btn = [b for b in call("studio_card", unit="protoss_gateway").get("buttons", [])
               if b.get("target") == "protoss_zealot"]
        step("the card has the mod's words and hotkey", btn and btn[0]["reworded"]
             and btn[0]["text"] == "Warp in Zealot <G>uard" and btn[0]["hotkey"] == "g", btn)
        r = call("studio_string_set", id=zid, text="")
        btn = [b for b in call("studio_card", unit="protoss_gateway").get("buttons", [])
               if b.get("target") == "protoss_zealot"]
        step("no text gives the classic words back", r.get("ok") and btn and not btn[0]["reworded"]
             and "classic" not in json.dumps(call("studio_card", unit="protoss_gateway").get("mod_buttons", [])), btn)
        r = call("studio_string_set", id="unit/protoss_zealot", text="Zealot Guard")
        step("a classic unit's name makes an edit of it", r.get("ok")
             and (r.get("string") or {}).get("text") == "Zealot Guard"
             and call("studio_get", table="units", name="protoss_zealot_edit").get("ok"), r)
        call("studio_undo")
        step("undo takes the edit away", not call("studio_get", table="units", name="protoss_zealot_edit").get("ok"))
        r = call("studio_string_set", id="label/weapons/hybrid_slash", text="Slash")
        step("a weapon's name given in Strings is the record's own", r.get("ok")
             and call("studio_get", table="weapons", name="hybrid_slash").get("record", {}).get("name") == "Slash", r)
        call("studio_undo")
        # find and replace: previewed, made as one edit, undone as one
        at = call("studio_history").get("at")
        r = call("studio_strings_replace", find="hybrid", replace="Chimera", mine=True, preview=True)
        step("a replace previewed changes nothing", r.get("ok") and r.get("count", 0) >= 5
             and call("studio_history").get("at") == at, r.get("count"))
        w = [c for c in r.get("changes", []) if c["id"] == "card/protoss_gateway/m0/text"]
        step("the hotkey stays on its letter", w and w[0]["after"] == "Warp in Chimera <R>eaver", w)
        n = r.get("count", 0)
        r = call("studio_strings_replace", find="hybrid", replace="Chimera", mine=True)
        groups = {e.get("group") for e in call("studio_history", count=n).get("edits", [])}
        now = strings(mine=True)
        step("a replace is one edit", r.get("ok") and r.get("done") and len(groups) == 1
             and now.get("unit/hybrid_reaver", {}).get("text") == "Chimera Reaver"
             and now.get("race/hybrid", {}).get("text") == "Chimera", (r.get("error"), groups))
        call("studio_undo")
        now = strings(mine=True)
        step("one undo takes the replace back", now.get("unit/hybrid_reaver", {}).get("text") == "Hybrid Reaver"
             and now.get("card/protoss_gateway/m0/text", {}).get("text") == "Warp in Hybrid <R>eaver",
             now.get("unit/hybrid_reaver"))

        # ---- the Balance workspace (studio_balance.cpp) ----
        shown = call("studio_show").get("workspace")

        def cell(r, unit, against):
            for c in r.get("cells", []):
                if c["unit"] == unit and c["against"] == against:
                    return c
            return None

        r = call("studio_matchup", units=["protoss_zealot", "terran_marine"],
                 against=["terran_marine", "zerg_mutalisk", "zerg_zergling"])
        zm = cell(r, "protoss_zealot", "terran_marine")
        step("a Zealot kills a Marine in 3 attacks, a Marine a Zealot in 30", r.get("ok") and zm
             and zm["attacks"] == 3 and zm["against_attacks"] == 30 and zm["winner"] == "unit"
             and abs(zm["time_to_kill"] - 44 / 24) < 0.01 and abs(zm["against_time_to_kill"] - 29 * 15 / 24) < 0.01, zm)
        zu = cell(r, "protoss_zealot", "zerg_mutalisk")
        step("a ground weapon cannot reach a flyer", zu and zu["attacks"] == 0 and zu["winner"] == "against", zu)
        mm = cell(r, "terran_marine", "terran_marine")
        step("a unit against itself falls with it", mm and mm["winner"] == "both" and mm["margin"] == 0, mm)
        r = call("studio_matchup")
        rows = [x["unit"] for x in r.get("rows", [])]
        cols = [x["unit"] for x in r.get("columns", [])]
        step("the mod's unit is a row and a column, with the classic columns", r.get("ok") and "hybrid_reaver" in rows
             and "hybrid_reaver" in cols and "protoss_zealot" in cols and "zerg_hydralisk" in cols, (rows, cols))
        before = cell(r, "hybrid_reaver", "protoss_zealot")
        r = call("studio_set", table="units", name="hybrid_reaver", field="hit_points", value=50)
        after = cell(call("studio_matchup"), "hybrid_reaver", "protoss_zealot")
        step("the numbers follow an edit", r.get("ok") and before and after
             and after["against_attacks"] < before["against_attacks"], (before, after))
        call("studio_undo")
        again = cell(call("studio_matchup"), "hybrid_reaver", "protoss_zealot")
        step("and its undo", again and again["against_attacks"] == before["against_attacks"], (before, again))
        r = call("studio_matchup", units=["protoss_zealot"], against=["terran_marine", "zerg_zergling"], per_cost=True,
                 budget=1200)
        zm, zz = cell(r, "protoss_zealot", "terran_marine"), cell(r, "protoss_zealot", "zerg_zergling")
        step("for the same cost, as many of each as the budget buys", zm and zz and zm["count"] == 12
             and zm["against_count"] == 24 and zz["against_count"] == 48, (zm, zz))
        r = call("studio_matchup_against", add="zerg_ultralisk")
        step("a column picked", r.get("ok") and "zerg_ultralisk" in r.get("columns", []), r)
        r = call("studio_matchup_against", remove="zerg_ultralisk")
        step("and taken away", r.get("ok") and "zerg_ultralisk" not in r.get("columns", []), r)
        step("a unit that is not there is refused", not call("studio_matchup_against", add="no_such_unit").get("ok"))
        r = call("studio_matchup_against", add="protoss_archon", rows=True)
        step("a row picked", r.get("ok") and "protoss_archon" in r.get("rows", []), r)
        call("studio_matchup_against", reset=True, rows=True)
        step("the Balance workspace shows", call("studio_show", workspace="balance").get("workspace") == "balance")
        call("studio_show", workspace=shown)  # the workspace the steps after expect


        # ---- a race on a real map, and scenarios of one's own (studio_racetest.cpp) ----
        # a scenario of the mod's own, saved at once as its file, listed, tried and taken away
        sc_file = mod / "scenarios" / "reaver-test.json"
        r = call("studio_scenario_save", name="Reaver test",
                 sides=[{"units": [{"unit": "protoss_zealot", "count": 1}]},
                        {"units": [{"unit": "terran_marine", "count": 3}, {"unit": "zerg_zergling", "count": 4}],
                         "order": "attack"}])
        step("a scenario saved as a file of the mod", r.get("ok") and sc_file.exists(), r)
        step("a scenario naming no unit is refused", not call("studio_scenario_save", name="Bad",
             sides=[{"units": [{"unit": "no_such_unit", "count": 1}]}]).get("ok"))
        r = call("studio_scenarios")
        mine = [x for x in r.get("scenarios", []) if x["name"] == "Reaver test"]
        step("it is listed with its sides", mine and len(mine[0]["json"]["sides"]) == 2
             and mine[0]["json"]["sides"][1].get("order") == "attack", r)
        if not a.no_game:
            for _ in range(240):
                if call("design_status").get("game", {}).get("on"):
                    break
                time.sleep(0.5)
            r = call("studio_try", unit="hybrid_reaver", against="Reaver test")
            step("Try it takes the scenario by its name", r.get("ok") and "Reaver test" in r.get("try", {}).get("label", ""), r)
            theirs, t = set(), {}
            for _ in range(100):
                t = call("studio_test")
                theirs = {u["type"] for u in call("units", player=1).get("units", [])}
                if t.get("try", {}).get("state") == "playing" and len(theirs) == 2:
                    break
                time.sleep(0.1)
            step("its units are on the field", t.get("try", {}).get("state") == "playing" and len(theirs) == 2, (t.get("try"), theirs))
        r = call("studio_scenario_delete", name="Reaver test")
        step("a scenario taken away", r.get("ok") and not sc_file.exists()
             and not [x for x in call("studio_scenarios").get("scenarios", []) if x["name"] == "Reaver test"], r)

        # the race played on a real map: refusals first, then a game started, read and stopped by its process
        step("a race that is none of the mod's is refused", not call("studio_race_play", race="no_such_race",
             map="(2)Boxer.scm").get("ok"))
        step("a map that is none of the studio's is refused", not call("studio_race_play", race="hybrid",
             map="no such map.scm").get("ok"))
        maps = [Path(m) for m in [r"E:\StarCraft\Maps\(2)Boxer.scm"] if Path(m).exists()]
        if not a.no_game and maps:
            pid = 0
            try:
                r = call("studio_race_play", race="hybrid", map=str(maps[0]), computers=1)
                pid = r.get("pid", 0)
                step("a race's game started", r.get("ok") and pid and 27140 <= r.get("port", 0) <= 27149, r)
                g = {}
                for _ in range(120):
                    g = call("studio_race_game", resume=True)
                    you = [p for p in g.get("players", []) if p["who"] == "you"]
                    if g.get("state") == "game" and you and you[0]["count"]:
                        break
                    time.sleep(0.25)
                you = [p for p in g.get("players", []) if p["who"] == "you"]
                units = {u["name"]: u["count"] for u in (you[0]["units"] if you else [])}
                step("it starts with the race's start units", units.get("hybrid_reaver") == 1
                     and units.get("protoss_probe") == 4 and units.get("protoss_nexus") == 1, g)
            finally:
                r = call("studio_race_stop")
                step("Stop ends it", r.get("ok") and r.get("state") == "ended", r)
            if pid:
                alive = subprocess.run(["tasklist", "/FI", f"PID eq {pid}", "/NH"], capture_output=True, text=True).stdout
                step("its process is gone", str(pid) not in alive, alive)

        r = call("studio_list", table="units")
        names = [x["name"] for x in r.get("records", [])]
        step("its units listed", "hybrid_reaver" in names, names)
        r = call("studio_get", table="units", name="hybrid_reaver")
        hp = field(r, "hit_points")
        step("a unit resolves", r.get("ok") and r.get("resolves"), r.get("why"))
        step("its own life is its own", hp and hp["source"] == "own", hp)
        inherited = [f for f in r.get("fields", []) if f["source"] == "classic"]
        step("an inherited field names its classic base", inherited and all(f["from"] == "zerg_ultralisk"
                                                                             for f in inherited), inherited[:2])

        r = call("studio_create", table="units", name="test_zealot", base="protoss_zealot")
        step("a unit made from a classic one", r.get("ok"), r)
        r = call("studio_get", table="units", name="test_zealot")
        hp = field(r, "hit_points")
        step("it starts with the classic life (100)", hp and hp["value"] == [100] and hp["source"] == "classic", hp)
        r = call("studio_set", table="units", name="test_zealot", field="hit_points", value=150)
        step("a field set", r.get("ok"), r)
        hp = field(call("studio_get", table="units", name="test_zealot"), "hit_points")
        step("it reads back as its own", hp and hp["value"] == [150] and hp["source"] == "own", hp)
        call("studio_undo")
        hp = field(call("studio_get", table="units", name="test_zealot"), "hit_points")
        step("undo gives the classic life back", hp and hp["value"] == [100] and hp["source"] == "classic", hp)
        call("studio_redo")
        hp = field(call("studio_get", table="units", name="test_zealot"), "hit_points")
        step("redo sets it again", hp and hp["value"] == [150], hp)
        r = call("studio_raw", table="units", name="test_zealot", text='{"base": "protoss_zealot",\n "hit_points": }')
        step("a record written as text that does not parse is refused with its line",
             not r.get("ok") and "line 2" in r.get("error", ""), r)
        r = call("studio_history")
        senders = {e["sender"] for e in r.get("edits", [])}
        step("the history names who made each edit", senders == {"claude"}, senders)

        # a unit made with what it comes with, as one edit
        r = call("studio_make_unit", name="test_lancer", label="Test Lancer", base="protoss_dragoon", race="protoss",
                 at="protoss_gateway", copy_weapon=True,
                 button={"card": "protoss_gateway", "position": 9, "hotkey": "l", "text": "Warp in Test <L>ancer"})
        step("a unit made with its weapon and a button", r.get("ok"), r)
        gw = field(call("studio_get", table="units", name="test_lancer"), "ground_weapon")
        step("it fires its own copy of the weapon", gw and gw.get("own") == "test_lancer_weapon", gw)
        groups = {e.get("group") for e in call("studio_history", count=3).get("edits", [])}
        step("its parts are one edit", len(groups) == 1, groups)
        call("studio_undo")
        step("one undo takes the whole of it back", not call("studio_get", table="units", name="test_lancer").get("ok")
             and not call("studio_get", table="weapons", name="test_lancer_weapon").get("ok"))
        r = call("studio_make_unit", name="protoss_zealot", base="protoss_zealot", copy_weapon=True)
        step("a refused make is refused whole", not r.get("ok")
             and not call("studio_get", table="weapons", name="protoss_zealot_weapon").get("ok"), r)
        # the classic rules' numbers: 30 normal against a Zealot (60 shields, 100 life, armour 1) takes
        # two attacks for the shields and four at 29
        r = call("studio_damage", weapon="hybrid_slash", target="protoss_zealot")
        step("the Hybrid Slash kills a Zealot in 6 attacks", r.get("attacks") == 6, r)
        r = call("studio_damage", weapon="hybrid_slash", target="zerg_ultralisk")
        step("and an Ultralisk in 14", r.get("attacks") == 14, r)

        # a member of another document: the minimap's own dot set in hud.json, undone, set again
        r = call("studio_put", doc="hud", path=["minimap", "dots", "own"], value=117)
        step("a hud.json member set", r.get("ok"), r)
        call("studio_undo")
        r = call("studio_put", doc="hud", path=["minimap", "dots", "own"], value=117)
        # a file brought into the data folder, then listed
        art = game / "studio-test-art.png"
        art.write_bytes(bytes.fromhex("89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c489"
                                      "0000000d49444154789c6360000002000005000155a4a1d70000000049454e44ae426082"))
        r = call("studio_import", **{"from": str(art), "folder": "art/test"})
        step("a file brought into the mod", r.get("ok") and r.get("path") == "art/test/studio-test-art.png", r)
        r = call("studio_files", ext="png")
        step("and listed among its files", "art/test/studio-test-art.png" in r.get("files", []), r.get("files"))

        r = call("studio_save")
        step("saved", r.get("ok") and r.get("unsaved") == 0, r)
        hud = json.loads((mod / "data" / "hud.json").read_text(encoding="utf-8")) if (mod / "data" / "hud.json").exists()             else {}
        step("hud.json has the dot", hud.get("minimap", {}).get("dots", {}).get("own") == 117, hud)
        types = json.loads((mod / "data" / "types.json").read_text(encoding="utf-8"))
        step("types.json has the new unit", types.get("units", {}).get("test_zealot", {}).get("hit_points") == 150,
             types.get("units", {}).get("test_zealot"))

        exe = ROOT / "src117" / "build" / "x64" / "Hyperion.exe"
        if exe.exists():
            # the classic install, as the game folder has it, so no first start asks which
            env = dict(os.environ, HYPERION_QUIET="1", HYPERION_WINDOW="background")
            p = subprocess.run([str(exe), "--mod-check", "studio-check", f"--data={game}", "--install", "classic",
                                "--install-folder", str(game)], cwd=game, capture_output=True, text=True,
                               timeout=300, env=env)
            report = (game / "mod-check").glob("*.txt")
            step("--mod-check passes the saved mod", p.returncode == 0, (p.returncode, p.stdout[-300:], p.stderr[-300:],
                                                                         [str(x) for x in report]))

        if not a.no_game:
            for _ in range(240):
                if call("design_status").get("game", {}).get("on"):
                    break
                time.sleep(0.5)
            on = call("design_status").get("game", {}).get("on")
            if step("the canvas game is on", on):
                def push(**kw):
                    r = call("studio_push", **kw)
                    if not r.get("ok"):
                        return r
                    n = r["push"]
                    for _ in range(100):
                        ps = call("studio_pushes")["pushes"]
                        if ps[n - 1]["state"] != "asked":
                            return ps[n - 1]
                        time.sleep(0.1)
                    return {"state": "no answer"}

                p = push(replace="all")
                step("pushed into the game", p.get("state") == "pushed", p)
                v = call("studio_verify")
                step("the studio's values are the loader's", v.get("ok") and v.get("same"), v)
                units = (p.get("answer") or {}).get("units", [])
                row = next((u["id"] for u in units if u["name"] == "test_zealot"), None)
                if step("the push made test_zealot", row is not None, units):
                    s = call("spawn", unit=row, x=1024, y=1024, player=0, count=1)
                    step("it spawns", s.get("ok"), s)
                    us = call("units").get("units", [])
                    mine = [u for u in us if u.get("type") == row]
                    life = mine[0].get("hp") if mine else None
                    step("in the game it has 150 life", mine and life in (150, 150 * 256), mine[:1])
                call("studio_set", table="units", name="test_zealot", field="ground_weapon", value="no_such_weapon")
                p = push()
                step("a push naming no weapon is refused, naming the field",
                     p.get("state") == "refused" and "ground_weapon" in json.dumps(p.get("answer")), p)
                call("studio_undo")
                # the autosave a push makes, taken back after the working state is dropped
                call("studio_set", table="units", name="test_zealot", field="armor", value=3)
                push()
                call("studio_open", folder=str(mod))
                st = call("studio_status")
                step("an autosave is waiting after reopening", st.get("recover") is not None, st)
                call("studio_recover")
                ar = field(call("studio_get", table="units", name="test_zealot"), "armor")
                step("the autosave gives the edit back", ar and ar["value"] == [3], ar)

        # ---- statuses and action blocks (studio_statuses.cpp) ----
        # a status made from a kind, changed, put on a weapon and given as an aura; its sentence and uses
        def status_of(name):
            return next((s for s in call("studio_statuses").get("statuses", []) if s["name"] == name), None)
        r = call("studio_status_new", name="venom", label="Venom", **{"from": "poison"})
        step("a status made from the poison kind", r.get("ok") and r["status"]["fields"].get("damage_per_second", 0) > 0, r)
        r = call("studio_status_set", name="venom", values={"duration": 6, "move_speed": -50})
        step("its duration and speed set as one edit", r.get("ok") and r["status"]["fields"].get("move_speed") == -50, r)
        s = status_of("venom")
        step("its sentence says what it does", s and "50 %" in s["sentence"] and "6 s" in s["sentence"], s)
        step("a field that is no status's is refused",
             not call("studio_status_set", name="venom", field="speed", value=3).get("ok"))
        step("a move speed below -100 is refused",
             not call("studio_status_set", name="venom", field="move_speed", value=-150).get("ok"))
        r = call("studio_weapon_status", weapon="hybrid_slash", status="venom")
        step("the status put on a weapon", r.get("ok") and r.get("status") == "venom", r)
        r = call("studio_unit_aura", unit="hybrid_reaver", aura={"status": "venom", "targets": "enemies", "radius": 96})
        step("the status given as a unit's aura", r.get("ok") and r.get("aura", {}).get("radius") == 96, r)
        used = {(u["table"], u["name"]) for u in (status_of("venom") or {}).get("used_by", [])}
        step("where it is used names the weapon and the aura",
             ("weapons", "hybrid_slash") in used and ("units", "hybrid_reaver") in used, used)
        # an action's blocks: set, added to inside a repeat, changed, moved; an unknown block refused
        r = call("studio_action_blocks", name="psionic_lance",
                 blocks=[{"do": "teleport"}, {"do": "spawn", "unit": "zerg_zergling", "count": 2, "seconds": 20},
                         {"do": "repeat", "times": 2, "blocks": [{"do": "wait", "seconds": 1}]}])
        step("an action's blocks set", r.get("ok") and len(r["action"]["blocks"]) == 3, r)
        step("its sentence comes from its blocks", r.get("ok") and "moves the caster" in r["action"]["sentence"]
             and "2 zergling" in r["action"]["sentence"].lower(), r.get("action", {}).get("sentence"))
        r = call("studio_block_add", name="psionic_lance", path=[2],
                 block={"do": "status", "status": "venom", "radius": 64, "targets": "enemies"})
        step("a block added inside a repeat", r.get("ok") and len(r["action"]["blocks"][2]["blocks"]) == 2, r)
        r = call("studio_block_set", name="psionic_lance", path=[1], field="count", value=3)
        step("a block's field set", r.get("ok") and r["action"]["blocks"][1]["count"] == 3, r)
        r = call("studio_block_move", name="psionic_lance", path=[0], by=1)
        step("a block moved", r.get("ok") and r["action"]["blocks"][1]["do"] == "teleport", r)
        r = call("studio_block_add", name="psionic_lance", block={"do": "blink"})
        step("a block the engine does not run is refused", not r.get("ok") and "blink" in str(r), r)
        # a rename follows the status's uses; a delete takes them off and undo puts them back
        r = call("studio_status_set", name="venom", rename="venin")
        w = call("studio_get", table="weapons", name="hybrid_slash")
        st = field(w, "status")
        step("a renamed status's weapon follows it", r.get("ok") and st and "venin" in json.dumps(st), (r, st))
        r = call("studio_status_delete", name="venin")
        step("a deleted status's uses are taken off", r.get("ok") and len(r.get("uses_taken_off", [])) >= 3, r)
        call("studio_undo")
        step("undo puts the status and its uses back", status_of("venin") is not None
             and len(status_of("venin")["used_by"]) >= 3, status_of("venin"))
        call("studio_save")
        saved = json.loads((mod / "data" / "types.json").read_text(encoding="utf-8"))
        step("the status, the weapon's status, the aura and the blocks saved",
             "venin" in saved.get("statuses", {}) and saved["weapons"]["hybrid_slash"].get("status") == "venin"
             and saved["units"]["hybrid_reaver"].get("aura", {}).get("status") == "venin"
             and saved["orders"]["psionic_lance"].get("blocks"), saved.get("statuses"))
        r = call("studio_check", run=True)
        for _ in range(240):
            if r.get("game", {}).get("state") == "done":
                break
            time.sleep(0.5)
            r = call("studio_check")
        game_check = r.get("game", {})
        step("the loader takes them (--mod-check)", game_check.get("state") == "done"
             and game_check.get("exit") == 0 and game_check.get("verdict", "").startswith("plays"), game_check)
        # ---- end of statuses and action blocks ----

        # ---- languages (studio_languages.cpp) ----
        r = call("studio_languages", add="de")
        langs = {x["language"]: x for x in r.get("languages", [])}
        step("a language added by its two letters", r.get("ok") and "deDE" in langs
             and langs["deDE"]["missing"] == langs["deDE"]["strings"] > 0, r)
        step("the mod's own language is refused", not call("studio_languages", add="enUS").get("ok"))
        r = call("studio_translation_set", language="deDE", id="unit/hybrid_reaver", text="Hybrid-H\u00e4scher")
        step("a unit's name translated by its Strings id", r.get("ok") and r["row"]["state"] == "done"
             and r["row"]["key"] == "unit/hybrid_reaver", r)
        r = call("studio_translation_set", language="deDE", key="unit/no_such_unit", text="Nichts")
        step("a key that names no string is refused", not r.get("ok"), r)
        miss = call("studio_strings", missing="deDE")
        ids = [x["id"] for x in miss.get("strings", [])]
        step("missing in deDE leaves the translated one out", miss.get("ok") and miss.get("count", 0) > 0
             and "unit/hybrid_reaver" not in ids, (miss.get("count"), ids[:5]))
        call("studio_string_set", id="unit/hybrid_reaver", text="Hybrid Reaper")
        row = [x for x in call("studio_translations", language="deDE").get("rows", []) if x["key"] == "unit/hybrid_reaver"]
        step("a translation whose source changed is stale", row and row[0]["state"] == "stale"
             and row[0]["made_from"] == "Hybrid Reaver", row)
        call("studio_undo")
        row = [x for x in call("studio_translations", language="deDE").get("rows", []) if x["key"] == "unit/hybrid_reaver"]
        step("undo makes it current again", row and row[0]["state"] == "done", row)
        r = call("studio_translation_set", language="deDE", id="card/protoss_gateway/m0/text", text="Hybrid-H\u00e4scher rufen")
        step("a button's translation has the mod's hotkey letter marked", r.get("ok") and "<r>" in r["row"]["text"], r)
        exp = call("studio_translations_export", language="deDE")
        csv = Path(exp.get("file", ""))
        step("a translator's CSV written, in .studio", exp.get("ok") and csv.exists() and exp.get("rows", 0) > 2
             and csv.parent.parent.name == ".studio", exp)
        if csv.exists():
            import csv as csvmod
            rows = list(csvmod.reader(csv.read_text(encoding="utf-8-sig").splitlines(keepends=True)))
            for row in rows:
                if row and row[0] == "race/hybrid":
                    row[3] = "Hybride"
            rows.append(["unit/nobody", "somewhere", "Nobody", "Niemand", "missing"])
            with open(csv, "w", encoding="utf-8", newline="") as f:
                csvmod.writer(f, lineterminator="\n").writerows(rows)
            out = csv.read_text(encoding="utf-8").split("\n")[:-1]
            r = call("studio_translations_import", language="deDE", file=str(csv))
            refused = r.get("refused", [])
            step("an import refuses the line naming no string, and says which",
                 r.get("ok") and len(refused) == 1 and refused[0]["key"] == "unit/nobody" and refused[0]["line"] == len(out), r)
            step("the rest is imported", r.get("imported") == 1, r)
            call("studio_undo")
            row = [x for x in call("studio_translations", language="deDE").get("rows", []) if x["key"] == "race/hybrid"]
            step("one undo takes the import back", row and row[0]["state"] == "missing", row)
        po = call("studio_translations_export", language="deDE", format="po")
        r = call("studio_translations_import", language="deDE", file=po.get("file", ""))
        step("a PO file read back as it was changes nothing", po.get("ok") and r.get("ok") and r.get("imported") == 0
             and r.get("unchanged") == 2 and not r.get("refused"), (po, r))
        call("studio_set", table="upgrades", name="hybrid_claws", field="name", value="Hybrid Claws")
        r = call("studio_translation_set", language="deDE", key="upgrade/hybrid_claws", text="Hybridklauen")
        step("an upgrade's own name translated by its key", r.get("ok") and r["row"]["state"] == "done", r)
        r = call("studio_translation_set", language="deDE", key="tech/psionic_lance", text="Lanze")
        step("a tech without a name of its own has none to translate", not r.get("ok"), r)
        call("studio_undo")
        call("studio_undo")
        call("studio_save")
        lf = mod / "lang" / "deDE.json"
        saved = json.loads(lf.read_text(encoding="utf-8")) if lf.exists() else {}
        step("the language saved as lang/deDE.json", saved.get("strings", {}).get("unit/hybrid_reaver", {}).get("text")
             == "Hybrid-H\u00e4scher", saved)
        call("studio_languages", remove="deDE")
        call("studio_save")
        step("a language taken away deletes its file at the save", not lf.exists())
        call("studio_undo")
        call("studio_save")
        step("undo and save write it again", lf.exists())
        # ---- end of languages ----

        # ---- tilesets (studio_tilesets.cpp) ----
        def png(path, w, h, rgb):
            import struct
            import zlib
            raw = b"".join(b"\0" + bytes(rgb) * w for _ in range(h))
            chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
            Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                                   + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))

        r = call("studio_tilesets")
        step("the eight classic tilesets listed, none changed", r.get("ok") and len(r.get("tilesets", [])) == 8
             and not any(t["changed"] for t in r["tilesets"]), r)
        g2 = call("studio_tileset", tileset="badlands", group=2)
        mega = next((m for m in g2.get("megatiles", []) if m > 0), 0)
        step("a group read from the player's archives", g2.get("ok") and g2.get("buildable") and mega > 0, g2)
        r = call("studio_tile_set", tileset="badlands", group=2, field="buildable", value=False, merge="tiles-test")
        g2 = call("studio_tileset", tileset="badlands", group=2)
        step("a group made unbuildable", r.get("ok") and not g2.get("buildable") and g2["flags"] & 0x80, (r, g2))
        r = call("studio_tile_set", tileset="badlands", group=2, field="height", value="very high")
        step("a height that is no height is refused", not r.get("ok"), r)
        r = call("studio_tile_set", tileset="badlands", group=99999, field="creep", value=True)
        step("a group past the tileset's is refused", not r.get("ok"), r)
        rows = ["####", "#..#", "#..#", "####"]
        r = call("studio_tile_set", tileset="badlands", megatile=mega, field="walkable", value=rows)
        m = call("studio_tileset", tileset="badlands", megatile=mega)
        step("a megatile's minitiles made walkable or not", r.get("ok") and m.get("walkable") == rows, (r, m.get("walkable")))
        r = call("studio_tile_set", tileset="badlands", colour=219, value="#ff0000")
        pal = {e["index"]: e for e in call("studio_tileset", tileset="badlands", palette=True).get("palette", [])}
        step("a colour of the palette changed", r.get("ok") and pal.get(219, {}).get("now") == "#ff0000"
             and pal[219]["classic"] != "#ff0000", pal.get(219))
        tmp = game / "tiles-check"
        tmp.mkdir(exist_ok=True)
        png(tmp / "plate.png", 32, 32, (240, 200, 30))
        png(tmp / "wrong.png", 30, 30, (240, 200, 30))
        r = call("studio_tile_image", tileset="badlands", megatile=mega, **{"from": str(tmp / "wrong.png")})
        step("a picture of the wrong size is refused", not r.get("ok"), r)
        before = call("studio_tilesets", tileset="badlands").get("minitiles_now", 0)
        r = call("studio_tile_image", tileset="badlands", megatile=mega, **{"from": str(tmp / "plate.png")})
        after = call("studio_tilesets", tileset="badlands")
        step("a megatile's picture brought in, its minitiles added", r.get("ok") and r.get("pictures") == 1
             and after.get("minitiles_now", 0) > before
             and (mod / "data" / "tilesets" / "badlands" / "megatiles" / ("%d.png" % mega)).exists(), (r, before, after))
        r = call("studio_tile_export", tileset="badlands", megatile=mega, to=str(tmp / "out.png"))
        step("a megatile saved out as PNG", r.get("ok") and (tmp / "out.png").exists(), r)
        call("studio_save")
        types = json.loads((mod / "data" / "types.json").read_text(encoding="utf-8"))
        bl = types.get("tilesets", {}).get("badlands", {})
        step("the changes saved in types.json's tilesets", bl.get("groups", {}).get("2", {}).get("buildable") is False
             and bl.get("palette", {}).get("219") == "#ff0000"
             and bl.get("megatiles", {}).get(str(mega), {}).get("walkable") == rows, bl)
        call("studio_undo")
        m = call("studio_tileset", tileset="badlands", megatile=mega)
        step("undo takes the picture away", m.get("ok") and m.get("image") is None, m.get("image"))
        for _ in range(3):
            call("studio_undo")
        call("studio_save")
        types = json.loads((mod / "data" / "types.json").read_text(encoding="utf-8"))
        step("undone to the start, types.json has no tilesets", "tilesets" not in types, types.get("tilesets"))
        shutil.rmtree(tmp, ignore_errors=True)
        # ---- end of tilesets ----

        # ---- building rules (studio_buildrules.cpp) ----
        # a race's way of building and its rules; the fix to its units as one edit; its advisor
        r = call("studio_race_rules", race="hybrid", build="construct")
        step("a race's way of building set", r.get("ok") and r.get("build") == "construct"
             and r.get("rules", {}).get("build") == "construct" and "protoss_probe" in r.get("workers", []), r)
        step("a way that is none is refused", not call("studio_race_rules", race="hybrid", build="fly").get("ok"))
        step("a rule that is none is refused", not call("studio_race_rules", race="hybrid", wings=True).get("ok"))
        r = call("studio_race_rules", race="hybrid", power=True)
        step("power with no power maker is named", r.get("ok") and r.get("power") and any(
            c["error"] and "power maker" in c["text"] for c in r.get("conflicts", [])), r.get("conflicts"))
        r = call("studio_race_rules", race="hybrid", power=None, shields=False)
        shields = [c for c in r.get("changes", []) if c["unit"] == "hybrid_reaver" and c["field"] == "shields_enabled"]
        step("no shields asks its units to have none", r.get("ok") and not r.get("shields") and shields
             and not shields[0]["to"], r.get("changes"))
        def reaver_flags():
            f = field(call("studio_get", table="units", name="hybrid_reaver"), "shields_enabled")
            return f and (f["value"], f["source"])
        flags0 = reaver_flags()
        r = call("studio_race_rules", race="hybrid", fix=True)
        flags1 = reaver_flags()
        step("the fix sets them, nothing left to change", r.get("ok") and not r.get("changes")
             and flags1 and flags1 != flags0, (r.get("changes"), flags0, flags1))
        call("studio_undo")
        step("undo takes the fix back in one", reaver_flags() == flags0, (flags0, reaver_flags()))
        r = call("studio_race_advisor", race="hybrid", advisor="zerg")
        supply = [x for x in r.get("lines", []) if x["line"] == "supply"]
        step("a race's advisor set", r.get("ok") and r.get("advisor") == "zerg" and supply
             and supply[0]["sound"] and not supply[0]["own"], r.get("lines"))
        step("an advisor that is none is refused", not call("studio_race_advisor", race="hybrid", advisor="elves").get("ok"))
        step("a line that is none is refused", not call("studio_race_advisor", race="hybrid", line="nope",
             revert=True).get("ok"))
        import wave
        wav = game / "advisor-check.wav"
        with wave.open(str(wav), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(22050)
            w.writeframes(b"\0\0" * 2205)
        r = call("studio_race_advisor", race="hybrid", line="supply", **{"from": str(wav)})
        supply = [x for x in r.get("lines", []) if x["line"] == "supply"]
        step("a line said by a file of the mod's", r.get("ok") and r.get("made") == "hybrid_advisor_supply"
             and supply and supply[0]["own"] and supply[0]["sound"] == "hybrid_advisor_supply", r)
        r = call("studio_race_advisor", race="hybrid", line="supply", revert=True)
        supply = [x for x in r.get("lines", []) if x["line"] == "supply"]
        step("the advisor's line again", r.get("ok") and supply and not supply[0]["own"], r.get("lines"))
        hy = {}
        for _ in range(8):  # the six edits above, each one undo
            hy = {x["race"]: x["record"] for x in call("studio_races").get("races", [])}.get("hybrid", {})
            if "rules" not in hy and "advisor" not in hy:
                break
            call("studio_undo")
        step("undo takes the rules and advisor back", "rules" not in hy and "advisor" not in hy, hy)
        wav.unlink()
        # ---- end of building rules ----

        # ---- scripts in Lua (studio_scripts.cpp) ----
        # a script made and checked as the loader checks it, named by a unit's hook, a script block and an
        # order's cooldown, saved, taken by --mod-check, taken away and brought back
        r = call("studio_script_api")
        step("the API lists hx.damage and the hooks", any(x["name"] == "hx.damage" for x in r.get("api", []))
             and any(h["hook"] == "on_hit" for h in r.get("hooks", [])), r.get("hooks"))
        lua = ('function reaver_hit(e)\n  hx.game_set("hits", (hx.game_get("hits") or 0) + 1)\nend\n\n'
               'function lance(e)\n  if e.target then\n    hx.damage(e.target, 20, {source = e.caster})\n  end\nend\n')

        def script_file(name):
            return (call("studio_scripts", file=name).get("files") or [{}])[0]
        r = call("studio_script_new", file="test", text=lua)
        step("a script made", r.get("ok") and r.get("made") == "test.lua", r)
        f = script_file("test.lua")
        step("its functions read, with no problem", [x["name"] for x in f.get("functions", [])] == ["reaver_hit", "lance"]
             and f.get("problems") == [], f)
        r = call("studio_script_set", file="test.lua", text=lua + "\nlocal n = 2 ^ 3\n")
        step("a power is a problem on its line", r.get("ok") and any(p["line"] == 11 for p in r.get("problems", [])), r)
        r = call("studio_script_set", file="test.lua", text="function lance(e\n")
        step("a parse error is a problem", r.get("ok") and len(r.get("problems", [])) > 0, r)
        call("studio_undo")
        call("studio_undo")
        f = script_file("test.lua")
        step("undo gives the text back", f.get("problems") == [] and len(f.get("functions", [])) == 2, f)
        step("a name that is no script's is refused", not call("studio_script_set", file="../x.txt", text="").get("ok"))
        r = call("studio_set", table="units", name="hybrid_reaver", field="scripts", value={"on_hit": "reaver_hit"})
        step("a unit's hook set", r.get("ok"), r)
        r = call("studio_block_add", name="psionic_lance", block={"do": "script", "call": "lance"})
        step("a script block added", r.get("ok") and "calls lance" in r.get("action", {}).get("sentence", ""),
             r.get("action", {}).get("sentence"))
        r = call("studio_set", table="orders", name="psionic_lance", field="cooldown", value=6)
        step("a cooldown on an order", r.get("ok"), r)
        uses = {x["name"]: x["used_by"] for x in script_file("test.lua").get("functions", [])}
        step("where each function is named", uses.get("reaver_hit") == ["units hybrid_reaver: on_hit"]
             and any("psionic_lance" in u for u in uses.get("lance", [])), uses)
        r = call("studio_script_rename", file="test.lua", to="lances")
        step("a script renamed", r.get("ok") and r.get("renamed") == "lances.lua", r)
        call("studio_undo")
        r = call("studio_script_show", function="lance")
        step("a function shown at its line", r.get("ok") and r.get("shown") == "test.lua" and r.get("line") == 5, r)
        call("studio_save")
        saved = mod / "data" / "scripts" / "test.lua"
        types = json.loads((mod / "data" / "types.json").read_text(encoding="utf-8"))
        step("the script, the hook and the cooldown saved", saved.exists() and saved.read_text(encoding="utf-8") == lua
             and types["units"]["hybrid_reaver"].get("scripts") == {"on_hit": "reaver_hit"}
             and types["orders"]["psionic_lance"].get("cooldown") == 6, types["orders"].get("psionic_lance"))
        r = call("studio_check", run=True)
        for _ in range(240):
            if r.get("game", {}).get("state") == "done":
                break
            time.sleep(0.5)
            r = call("studio_check")
        game_check = r.get("game", {})
        step("the loader takes the scripts (--mod-check)", game_check.get("state") == "done"
             and game_check.get("exit") == 0 and game_check.get("verdict", "").startswith("plays"), game_check)
        r = call("studio_script_delete", file="test.lua")
        step("a script taken away names what the mod still calls", r.get("ok")
             and set(r.get("still_named", [])) == {"reaver_hit", "lance"}, r)
        call("studio_undo")
        step("undo brings it back", script_file("test.lua").get("file") == "test.lua")
        # ---- end of scripts in Lua ----

        # ---- the Claude panel (studio_claude.cpp) ----
        # a stand-in for Claude Code (tools/studio_claude_fake.py), so these steps spend nothing
        def claude_wait():
            s = {}
            for _ in range(60):
                s = call("studio_claude_status")
                if not s.get("running"):
                    break
                time.sleep(0.25)
            return s

        def reaver():
            return call("studio_get", table="units", name="hybrid_reaver").get("record", {})

        call("studio_claude_use", program=str(ROOT / "tools" / "studio_claude_fake.py"))
        call("studio_claude_mode", mode="show")
        was = reaver()
        r = call("studio_claude_ask", new=True, text="fake: say Cheaper and tougher.|set units hybrid_reaver minerals 77|"
                 "set units hybrid_reaver hit_points 88|deny studio_save|set units terran_marine minerals 1")
        step("Claude asked, through the stand-in", r.get("ok") and r.get("running"), r)
        claude_wait()
        items = call("studio_claude_transcript").get("items", [])
        tools = [(x["tool"], x["state"]) for x in items if x["kind"] == "tool"]
        step("each tool call a line, with its state", tools == [("studio_set", "done"), ("studio_set", "done"),
             ("studio_save", "not given"), ("studio_set", "refused")], items)
        refused = [x for x in items if x["kind"] == "tool" and x["state"] == "refused"]
        step("a refused call says why", refused and "not the mod's" in refused[0].get("detail", ""), refused)
        p = call("studio_claude_pending")
        rows = [(x["field"], x["after"]) for x in p.get("rows", [])]
        step("Show me first: the edits wait as a proposal", p.get("waiting") and rows == [("minerals", 77), ("hit points", 88)], p)
        r = call("studio_set", table="units", name="hybrid_reaver", field="armor", value=3, sender="you")
        step("the modder's own edit waits for it", not r.get("ok") and "proposal" in r.get("error", ""), r)
        r = call("studio_claude_accept", row=0)
        step("one row accepted, the other still waits", r.get("ok") and r.get("waiting")
             and [x["field"] for x in r.get("rows", [])] == ["hit points"] and reaver().get("minerals") == 77, r)
        r = call("studio_claude_undo")
        rec = reaver()
        step("Undo all takes the rest back", r.get("ok") and not r.get("waiting") and rec.get("hit_points") == was.get("hit_points")
             and rec.get("minerals") == 77, rec)
        call("studio_undo")
        step("the accepted row undone as an edit of its own", reaver().get("minerals") == was.get("minerals"), reaver())
        call("studio_claude_mode", mode="edit")
        call("studio_claude_ask", text="fake: set units hybrid_reaver minerals 66|set units hybrid_reaver hit_points 99")
        claude_wait()
        p = call("studio_claude_pending")
        step("Let it edit: made at once, nothing waits", not p.get("waiting") and reaver().get("minerals") == 66
             and reaver().get("hit_points") == 99 and len(p.get("rows", [])) == 2, p)
        call("studio_undo")
        rec = reaver()
        step("one undo takes the ask's edits back", rec.get("minerals") == was.get("minerals")
             and rec.get("hit_points") == was.get("hit_points"), rec)
        call("studio_claude_ask", text="fake: say Thinking about it.|sleep 30")
        time.sleep(1.0)
        r = call("studio_claude_stop")
        items = call("studio_claude_transcript").get("items", [])
        step("Stop ends the process", r.get("ok") and not r.get("running") and items and items[-1]["kind"] == "note"
             and items[-1]["text"].startswith("Stopped"), (r, items[-2:]))
        convs = call("studio_claude_conversations").get("conversations", [])
        step("the conversation kept in the mod's .studio/claude", len(convs) >= 1 and convs[0]["asks"] >= 3
             and (mod / ".studio" / "claude" / (convs[0]["id"] + ".json")).exists(), convs)
        r = call("studio_claude_conversations", all=True)
        step("conversations cleared", r.get("ok") and r.get("conversations") == []
             and not (mod / ".studio" / "claude" / (convs[0]["id"] + ".json")).exists() if convs else False, r)
        # a proposal that adds a language: no save, autosave or push carries it until it is accepted
        call("studio_claude_mode", mode="show")
        langs = sorted(call("studio_claude_pending").get("pushed_languages", []))
        fr = mod / "lang" / "frFR.json"
        call("studio_claude_ask", new=True, text='fake: call studio_languages {"add": "frFR"}')
        claude_wait()
        p = call("studio_claude_pending")
        step("a language Claude adds waits in the proposal", p.get("waiting") and [x["doc"] for x in p.get("rows", [])] == ["lang"]
             and sorted(p.get("pushed_languages", [])) == langs, p)
        call("studio_save")
        step("a save leaves the waiting language out", not fr.exists(), fr)
        r = call("studio_claude_accept")
        call("studio_save")
        step("accepted, a push and a save carry it", "frFR" in r.get("pushed_languages", []) and fr.exists(), r)
        call("studio_undo")
        call("studio_save")
        step("undone and saved, its file is gone", not fr.exists(), fr)
        s = call("studio_claude_use", program="none")
        r = call("studio_claude_ask", text="anything")
        step("no Claude Code: said, and nothing runs", s.get("missing") == "Claude Code was not found" and not r.get("ok"), (s, r))
        call("studio_claude_use", program="")
        call("studio_claude_mode", mode="show")
        # ---- end of the Claude panel ----

        # ---- A and B fights, sound priority and subtitles (studio_ab.cpp) ----
        at0 = call("studio_history").get("at", 0)
        r = call("studio_ab_variant", unit="hybrid_reaver", clear=True, table="weapons", name="hybrid_slash", field="damage",
                 value=60)
        ch = r.get("changes", [])
        step("B: a weapon's damage changed, A's beside it", r.get("ok") and len(ch) == 1 and ch[0]["b"] == 60
             and ch[0]["a"] == 30, r)
        step("B refuses a record that is not the unit's", not call("studio_ab_variant", unit="hybrid_reaver", table="weapons",
                                                                     name="gauss_rifle", field="damage", value=9).get("ok"))
        step("and a field that is no number", not call("studio_ab_variant", unit="hybrid_reaver", field="name", value=1).get("ok"))
        step("B is not the mod's", call("studio_history").get("at", 0) == at0)

        def played(**kw):
            r = call("studio_ab_run", **kw)
            if not r.get("ok"):
                return r
            t0 = time.time()
            while time.time() - t0 < 120:
                time.sleep(1.0)
                r = call("studio_ab")
                if r.get("state") not in ("starting", "pushing", "playing"):
                    break
            return r

        if not a.no_game:
            run = dict(unit="hybrid_reaver", against="zerg_zergling", count=6, yours=1, seeds=2)
            r1 = played(**run)
            step("A and B played out: each seed twice", r1.get("state") == "done" and len(r1.get("fights", [])) == 4
                 and r1["sums"]["A"]["fights"] == 2 and r1["sums"]["B"]["fights"] == 2, r1)
            step("B fought as its own record", r1.get("spawned") == ["hybrid_reaver", "hybrid_reaver__b"], r1.get("spawned"))
            r2 = played(**run)
            step("the same seeds play the same fights", r2.get("fights") == r1.get("fights") and r2.get("fights"),
                 (r1.get("fights"), r2.get("fights")))
            r = played(unit="hybrid_reaver", against="zerg_zergling", count=6, seeds=1, b=False)
            step("A alone played out", r.get("state") == "done" and len(r.get("fights", [])) == 1 and "B" not in r.get("sums", {}), r)
        r = call("studio_ab_keep", unit="hybrid_reaver")
        dmg = field(call("studio_get", table="weapons", name="hybrid_slash"), "damage")
        step("Keep B writes its changes into the mod", r.get("ok") and dmg and dmg["value"] == [60], (r, dmg))
        step("B is empty once kept", call("studio_ab_variant", unit="hybrid_reaver").get("changes") == [])
        call("studio_undo")
        dmg = field(call("studio_get", table="weapons", name="hybrid_slash"), "damage")
        step("one undo takes Keep B back", dmg and dmg["value"] == [30], dmg)
        # a sound's fields: a classic one changed through an edit of it
        r = call("studio_sound_set", sound="terran_marine_tmawht00", field="subtitle", value="Ready to roll.")
        step("a classic sound's subtitle makes an edit of it", r.get("ok") and r.get("record") == "terran_marine_tmawht00_edit"
             and r.get("subtitle") == "Ready to roll.", r)
        r = call("studio_sound_set", sound="terran_marine_tmawht00", field="min_interval", value=2000)
        step("its least gap", r.get("ok") and r.get("min_interval") == 2000, r)
        r = call("studio_sound_set", sound="terran_marine_tmawht00", field="priority", value=40)
        step("its priority", r.get("ok") and r.get("priority") == 40, r)
        step("a subtitle is a text", not call("studio_sound_set", sound="terran_marine_tmawht00", field="subtitle", value=3).get("ok"))
        r = call("studio_strings", kind="subtitle")
        sub = [s for s in r.get("strings", []) if s.get("key") == "sound/terran_marine_tmawht00_edit"]
        step("the subtitle is a string of the mod's, with its key", sub and sub[0]["text"] == "Ready to roll.", r.get("strings"))
        if not a.no_game:
            r = call("studio_push")
            n = r.get("push", 0)
            t0 = time.time()
            while time.time() - t0 < 30:
                p = [x for x in call("studio_pushes").get("pushes", []) if x["push"] == n]
                if p and p[0]["state"] != "asked":
                    break
                time.sleep(0.5)
            step("the loader takes a subtitle and a gap", p and p[0]["state"] == "pushed", p)
        while call("studio_history").get("at", 0) > at0 and call("studio_undo").get("ok"):
            pass
        step("the sound's edit undone", "terran_marine_tmawht00_edit" not in
             [x["name"] for x in call("studio_list", table="sounds").get("records", [])])
        # ---- end of A and B fights, sound priority and subtitles ----

        r = call("studio_delete", table="units", name="test_zealot")
        step("a unit taken away", r.get("ok"), r)
        call("studio_undo")
        names = [x["name"] for x in call("studio_list", table="units").get("records", [])]
        step("undo puts it back", "test_zealot" in names, names)
        call("studio_undo")  # the armor, or the delete's predecessor
    print("%d failed" % failed)
    return failed


if __name__ == "__main__":
    sys.exit(main())
