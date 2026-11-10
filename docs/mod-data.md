# A mod's data: new units, weapons and the rest

A mod can bring types of its own: units, weapons, flingy, sprites, images, orders, techs,
upgrades, sounds and portraits, with their image scripts and art. They come after the original game's
rows, so a game without the mod cannot see them and plays as it did. A mod can also edit any
row already there, the original's or a mod's it depends on (Edits, below), and build on other
mods (`"depends"`, below). The loader is `engine/src117/port/data/port_moddata.cpp`; the test
mod is `engine/mods/hybrid-test/`.

A mod can also change how the game looks without changing what it does: its console, the
minimap's dots and the selection's marks (`hud.json`, below), and the looks its units are drawn
with (`looks.json`, below). The seams the engine has for
modules as well as mods are listed in `engine/docs/modules/overview.md`.

## Where it goes

A mod's `mod.json` names a folder of its own with `"data"`:

    {
      "id": "hybrid-test",
      "name": "Hybrid Test",
      ...
      "data": "data/"
    }

The folder holds `types.json` (the records), and may hold `iscript.txt` (the image scripts)
and the files the records name: GRPs, sounds, portrait videos, icons and wireframes, and its
own cinematics (Cinematics, below). A mod's art is the player's own business: an `art/` folder anywhere in a mod (`data/art/`, say) is
left out of git and out of the
package (`engine/tools/package.py`).

A game has the mod's types when the run is started with `--mod <id>`, or the `"hyperion"`
`"mod"` setting names it. The data loads at start-up; errors go to `hyperion_mods.log` and
a message box, and the run goes on without the mod. `--mod-check <id>` reports the same
errors before anyone plays, and every file that would not play (Checking a mod, below).

A mod may build on others: `"depends"` names them, each by its id, or with the least version
it needs.

    "depends": [{"id": "hybrid-test", "version": "0.2"}]

Their data loads first, in order, each mod after the mods it depends on, then the mod's own
(`PortModChain`, `port_mods.cpp`). The mod's records name theirs as they name the original's,
take ids after theirs, and may edit them. A dependency that is not installed, is older than the
version named, or leads back to the mod itself stops the load, and the message names it.
Versions compare part by part, so `1.10` is after `1.9`. The fingerprint covers the whole
chain, and a replay or save records the mods under it (In a game, below).

## types.json

One object for each table, `units`, `weapons`, `flingy`, `sprites`, `images`, `orders`,
`techs`, `upgrades`, `sounds`, `portraits`, each a map from a record's name to the record
(and beside them `cards`, `races` and `statuses`, below):

    {
      "units": {
        "hybrid_reaver": {
          "base": "zerg_ultralisk",
          "name": "Hybrid Reaver",
          "flingy": "hybrid_reaver",
          "ground_weapon": "hybrid_slash",
          "hit_points": 500,
          "shields_enabled": 1,
          "shields": 200,
          "armor": 2
        }
      },
      "weapons": {
        "hybrid_slash": {"base": 40, "damage": 30, "damage_bonus": 3}
      },
      "flingy": {"hybrid_reaver": {"base": 14, "sprite": "hybrid_reaver"}},
      "sprites": {"hybrid_reaver": {"base": 157, "image": "hybrid_reaver"}},
      "images": {"hybrid_reaver": {"base": 50}}
    }

`base` is the row a record starts from: one of the original's rows by its number or its
name, or another of the mod's records in the same table by its name. Every column the record
does not give is the base's, as the mod's edits leave it. A field that names another type (a
unit's `flingy`, a weapon's `flingy`, an image's `script`, and the others marked below) takes
a number or a name the same way. A number must be a row that exists: one of the original's,
the table's "none" (the row after the original's, for units, weapons, orders, techs and
upgrades), or one of the mod's ids; any other fails the load and the log names the field.

Every original row has a name. Units, techs and upgrades have the names `.hxm` maps use
(`zerg_ultralisk`, `stim_packs`, `terran_infantry_armor`; `docs/hxm-format.md`), and orders
names given by hand (`attack_move`, `cast_psionic_storm`); all are in
`tools/hxmap/names.py`. Weapons, flingy, sprites and images have names made from the
original's own files by `tools/hxmap/gen_typenames.py`, kept in `tools/hxmap/typenames.py`:

- a weapon is its label as the game shows it (`gauss_rifle`); where two share a label, the
  later adds the first unit that fires it (`gauss_rifle_jim_raynor_marine`);
- a flingy, a sprite and its image are named after the unit that moves with them
  (`terran_marine`), else the weapon that fires them (`psionic_storm`);
- a unit's building image is the unit's name with `_construction`;
- a sound is its file's path (`terran_marine_tmayes00`, `misc_buzz`), sound 0 being `none`;
- a portrait is the folder its videos are in (`tmarine`, `tghost`);
- any other image is its file's path (`terran_tmashad`, `thingy_tscglow`), with `_shadow` for
  a shadow drawn from another image's frames;
- a name already taken in its table gets `_2`, `_3` and so on.

The engine reads them from `engine/src117/port/data/port_typenames117.h` (`port_typenames.cpp`),
which `tools/hxmap/gen_cpp.py` writes with the other names, so the engine and the tools agree.

Each record gets the next free id of its table, in the order of the records' names, so a
given version of a mod always has the same ids:

| table | 1.17's rows | a mod's first id | the most ids |
|---|---|---|---|
| units | 0-227 | 256 | 4095 |
| weapons | 0-129 | 131 | 2047 |
| flingy | 0-208 | 209 | 2047 |
| sprites | 0-516 | 517 | 8191 |
| images | 0-998 | 999 | 16383 |
| orders | 0-188 | 190 | 1023 |
| techs | 0-43 | 45 | 1023 |
| upgrades | 0-60 | 62 | 1023 |
| sounds | 0-1143 | 1144 | 16383 |
| portraits | 0-109 | 110 | 4095 |

Units start at 256 because 228 to 232 already mean something to 1.17 (no unit, the
trigger groups), and weapons, orders, techs and upgrades after the row that means none
(130, 189, 44 and 61). A mod's techs and upgrades no longer use 1.17's unused rows, which
an EUD map may use. The limits are in `engine/src117/h/typelimits.h`.

An id past 255 is kept in 16 bits where 1.17 has a byte (a unit's order, a building's
research, a bullet's weapon, units.dat's and orders.dat's columns of them), and the commands
that carry one have wide forms a game sends only for such an id. An EUD map still reads
1.17's byte: the low one.

An image whose `grp` the player does not have is drawn as its base, with its base's script.
Where that script has no animation the unit plays (a spell cast on a unit that never casts),
the image keeps the animation it has.

### The fields

The names are those of the DAT files' columns; the values are the files' own, but for a
unit's `hit_points`, which is in whole points (the file keeps 256 times that).

units: `flingy`*, `subunit`*, `subunit2`*, `infestation`*, `construction_image`*,
`direction`, `shields_enabled`, `shields`, `hit_points`, `elevation`, `movement_flags`,
`rank`, `ai_idle_order`*, `idle_order`*, `return_to_idle_order`*, `attack_unit_order`*,
`attack_move_order`*, `ground_weapon`*, `max_ground_hits`, `air_weapon`*, `max_air_hits`,
`ai_internal`, `flags`, `target_range`, `sight_range`, `armor_upgrade`*, `size`, `armor`,
`right_click`, `ready_sound`*, `what_sound_first`*, `what_sound_last`*, `pissed_sound_first`*,
`pissed_sound_last`*, `yes_sound_first`*, `yes_sound_last`*, `placement_box`,
`addon_position`, `bounds`, `portrait`*, `minerals`, `gas`, `build_time`, `requirements`,
`group_flags`, `supply_provided`, `supply_required`, `space_required`, `space_provided`,
`build_score`, `destroy_score`, `map_string`, `broodwar`, `editor_flags`.

weapons: `label`, `flingy`*, `unused`, `target_flags`, `min_range`, `max_range`,
`damage_upgrade`*, `damage_type`, `behavior`, `remove_after`, `explosion`, `inner_splash`,
`middle_splash`, `outer_splash`, `damage`, `damage_bonus`, `cooldown`, `damage_factor`,
`attack_angle`, `launch_spin`, `forward_offset`, `upward_offset`, `target_error`, `icon`.

flingy: `sprite`*, `top_speed`, `acceleration`, `halt_distance`, `turn_radius`, `unused`,
`movement_control`.

sprites: `image`*, `health_bar`, `unknown`, `visible`, `selection_circle`,
`selection_offset`.

images: `grp`, `turns`, `clickable`, `use_full_iscript`, `draw_if_cloaked`,
`draw_function`, `remapping`, `script`*, `shield_overlay`, `attack_overlay`,
`damage_overlay`, `special_overlay`, `landing_dust_overlay`, `liftoff_dust_overlay`.

orders: `label`, `use_weapon_targeting`, `secondary`, `non_subunit`,
`subunit_inheritance`, `subunit_can_use`, `interruptible`, `stop_moving`, `can_be_queued`,
`keep_target_disabled`, `can_be_obstructed`, `fleeable`, `requires_movable`, `weapon`*,
`tech`*, `animation`, `highlight`, `requirements`, `obscured`*.

techs: `minerals`, `gas`, `research_time`, `energy`, `requirements`, `use_requirements`,
`icon`, `label`, `race`, `researched`, `broodwar`.

upgrades: `minerals`, `minerals_factor`, `gas`, `gas_factor`, `time`, `time_factor` (a
level costs the base and the factor times the levels done), `requirements`, `icon`,
`label`, `race`, `max_level`, `broodwar`.

sounds: `file`, `priority`, `flags`, `length_adjust`, `min_volume`.

portraits: `idle`, `talking`, `idle_chances`.

(* names another type.) A field with an array gives a column of more than one value, as
`bounds` and `placement_box` are. A unit's `flags` may be a number or an array of the flags'
names, bit 0 first: `building`, `addon`, `flyer`, `worker`, `subunit`, `flying_building`,
`hero`, `regenerate`, `animated_idle`, `cloakable`, `two_in_egg`, `single_entity`,
`resource_depot`, `resource_container`, `robotic`, `detector`, `organic`,
`requires_creep`, `unused`, `requires_psi`, `burrowable`, `spellcaster`,
`permanent_cloak`, `pickup_item`, `ignores_supply`, `medium_overlays`, `large_overlays`,
`battle_reactions`, `auto_attack`, `invincible`, `mechanical`, `produces_units`.

Fields of the mod's own, not columns:

- `name` (units): the name the game shows; without it, the record's name.
- `name` (weapons, techs, upgrades): the name the game shows, in the status panel's weapon and
  upgrade tooltips and while a tech researches, in place of the game's string `label` numbers;
  without it, that string.
- `looks` (units): the 1.17 unit whose looks the game borrows for what is not in the DAT
  files: the status panel's layout, and the wireframes, command icon, voices and portrait the
  record does not give itself. Without it, the base's.
- `icon`, `wireframe` and `transport_wireframe` (units): the unit's own command icon and
  wireframes, PNG or GRP files, or a unit whose own it shows (Icons and wireframes, below).
- `buttons` (units): the 1.17 unit whose command card the unit has; without it, its
  looks'.
- `grp` (images): the image's frames, a GRP file in the data folder, or a picture of them
  (Art sheets, below). Without it, the image
  draws its base's frames with its base's overlays. With it, the overlays the base has are
  at the frame's centre, and the image has no damage overlay. A `grp` the folder does not
  have (a player who has not made the art) leaves the image its base's frames and its
  base's script, which those frames fit, and the log says so.
- `requires` (units, techs and upgrades): what a player needs to train the unit, research the tech or upgrade the upgrade, in order, each one met:
  `"broodwar"` (an expansion game), a unit's name (one finished), `{"at": unit}` (the
  unit that trains it) or `{"tech": tech}` (researched); a list among them is
  alternatives. The Hybrid Reaver's is `[{"at": "protoss_gateway"},
  "protoss_templar_archives"]`, as 1.17 writes the Dark Templar's. A unit without it is
  trained nowhere. A tech with it is researched where its `{"at": building}` says, for its
  `minerals`, `gas` and `research_time` (`ResearchAllowed` reads it through
  `ModTechResearchTree`); a new game starts it available to every player and researched by
  none, whatever the map says of the row it took (`ModTechsForGame`). Psionic Lance's is
  `[{"at": "protoss_templar_archives"}]`, 150 minerals, 150 gas and 1,800 frames. An
  upgrade with it is upgraded where it says, a level at a time to its `max_level`
  (`UpgradeAllowed` reads it through `ModUpgradeTree`), and a new game starts it at level
  0 of its `max_level` for every player (`ModTechsForGame`). A weapon's `damage_upgrade` or
  a unit's `armor_upgrade` may name it, and its levels then add the weapon's
  `damage_bonus` or a point of armour each, as 1.17's do. Hybrid Claws is
  `[{"at": "protoss_forge"}]`, three levels, and the Hybrid Reaver's weapon names it.
- `use_requires` (techs): what a unit needs to cast the tech, in the same form, with
  `{"caster": unit}` (the unit casting) and `"researched"` (the tech itself). A mod's tech
  takes one of 1.17's unused tech rows and is available to every player; Psionic
  Shockwave's is `[{"caster": "hybrid_reaver"}]`, so no research is needed, and Psionic
  Lance's `[{"caster": "hybrid_reaver"}, "researched"]`, so its button is greyed until
  the research is done.
- `comment`: anything; the loader skips it.

### Edits

A record with `"edit"` changes a row that is there already, in place of adding one. It names
the row as `base` does, and gives the fields it changes; every other column stays as the row
has it.

    "units": {
      "tougher_marine": {"edit": "terran_marine", "hit_points": 60, "name": "Veteran Marine"}
    },
    "weapons": {
      "heavier_rifle": {"edit": "gauss_rifle", "damage": 8}
    }

The record's own name is only a label. Any column of any table can be edited, as can a
unit's `name`, `icon`, `wireframe` and `transport_wireframe`, and the `requires` and
`use_requires` of units, techs and upgrades. An edit has no `base`, `grp`, `looks` or
`buttons`: those make a new row, and a new record has them.

- An edit of one of the original's rows is written for each game that has the mod, after the
  game reads its tables and before the map's own settings, so a map that sets a unit's hit
  points or an upgrade's levels still has its way. A game without the mod has the original's
  rows: images.dat, read once at start-up, is put back for it, and the other tables are read
  again for each game. An edited upgrade's `max_level` becomes each player's most at the
  game's start, as the original's is.
- An edit's `requires` and `use_requires` take the place of the original's tree only; the
  original's other checks stay, a tech the map leaves unavailable among them. A unit's
  `requires` that starts with `{"at": unit}` also moves an add-on to that building.
- An edit's `name` is the name the game shows for the unit, under a name the map gives it.
- An edit of a record of a mod it depends on changes that record, for every record after it
  that names it. A record of the same `types.json` gives its fields itself, so editing one is
  refused.
- A new record based on an edited row starts from the edit. Two edits of a row apply in turn,
  in the order of the mods and, within one, of their names.

### Cards

`cards`, beside the tables, adds buttons to command cards, by the card's unit, in a game
with the mod. A 1.17 unit's card gets the buttons among its own; a mod's unit named here
gets a card of its own, its looks' card with the buttons, so a 1.17 unit that looks the
same keeps its card.

    "cards": {
      "protoss_forge": [
        {"position": 4, "upgrade": "hybrid_claws", "hotkey": "c", "text": "Upgrade Hybrid <C>laws"}
      ],
      "protoss_templar_archives": [
        {"position": 7, "research": "psionic_lance", "hotkey": "l", "text": "Research Psionic <L>ance"}
      ],
      "protoss_gateway": [
        {"position": 5, "train": "hybrid_reaver", "hotkey": "r",
         "text": "Warp in Hybrid <R>eaver",
         "disabled": "Hybrid Reaver Requires:
   Templar Archives"}
      ]
    }

    "cards": {
      "hybrid_reaver": [
        {"position": 7, "cast": "psionic_shockwave", "ai": "enemy_cluster", "hotkey": "c", "text": "Psionic Sho<c>kwave"},
        {"position": 8, "cast": "psionic_lance", "target": true, "ai": "enemy_cluster", "hotkey": "l", "text": "Psionic <L>ance"}
      ]
    }

`position` is the card's slot, 1 to 9, left to right and top to bottom; `train` the mod's
unit to train, `upgrade` the mod's upgrade to upgrade (shown while its next level is allowed),
`research` the mod's tech to research (one with `requires`; the button
shows while the research is allowed, and goes once it is done, as 1.17's do), or `cast` the
mod's order to give, which the button gives at once at the
caster's own place (the button shows while the order's tech's `use_requires` are met), or
with `"target": true` once the player has aimed it at a unit or a place, as 1.17's spell
buttons do (`order_mod_target`, after the energy is checked as `order_tech` checks it);
`ai` on a cast, how a computer's caster casts it (below);
`hotkey` its key, and `text` the button's text with the key's letter written `<R>`,
drawn highlighted; `disabled` the text while the requirements are not met; `icon` a
command icon, the trained unit's own or its looks', or the tech's, by default. The button is put among the card's by position.

A worker's build menu is a card too, by its name: `zerg_basic_structures`,
`terran_basic_structures`, `protoss_basic_structures` and the three `_advanced_structures`
(the Drone's, the SCV's and the Probe's). Its buttons each `build` one of the mod's units,
which the worker places as its race places its own (`order_place`, `order_place_protoss`,
`order_zerg_place`) and builds once placed, while the unit's `requires` are met; only a build
menu's buttons build.

    "cards": {
      "terran_advanced_structures": [
        {"position": 5, "build": "test_hall", "hotkey": "h", "text": "Build Test <H>all"}
      ]
    }

A button goes after the card's own at its position, and a card shows the first at each
position that is not hidden: a mod's button at a place 1.17's uses shows when a race of the
mod goes `without` the unit 1.17's builds there.

An entry with `classic` adds no button: it changes the words of the card's own button at
its position, which stays where it is and does what it did. `classic` names what that
button makes, researches or upgrades, or is `true` for the first button there; the entry
gives `text`, `hotkey` or `disabled`, and nothing else. The text keeps the classic button's
kind of cost; a hotkey alone keeps its text.

    "cards": {
      "terran_basic_structures": [
        {"position": 7, "classic": "terran_academy", "disabled": "Academy Requires:\n   Barracks or Iron Forge"}
      ]
    }

The presentation is all it changes: what a button does, and so a replay, is the same.

### What a mod's type does not do yet

A mod's order is a spell: 1.17's spell order runs it (`DispatchClickAbility`), which
takes the energy of the order's tech, waits until the target is in its weapon's range,
plays the order's animation, and fires the weapon where the script says `castspell`.
Psionic Shockwave's order starts from Psionic Storm's and names its own weapon and tech;
its weapon appears on the caster (fire type 5) and has the map's range, so the cast
happens where the unit stands. Psionic Lance's is the same order aimed: its weapon appears
at the target's place (fire type 4) with a range of 224, so the caster walks into range of
what it was aimed at and casts there, and its explosion 3 splashes enemies only. A mod's
tech is let through `sub_434130`'s target checks whatever it is aimed at; its weapon's
target flags choose what it hits.

A mod's order whose `base`, or its base's base, is one of the classic spell orders that do
their own work runs that order's own handler in place of the spell order: `cast_recall`,
`cast_scanner_sweep`, `cast_defensive_matrix`, `cast_hallucination`, `cast_mind_control`
and `cast_feedback`. It fires no weapon. The handler checks the target as the classic spell
does and works with the classic numbers; the energy is the mod's order's tech's, whose
`use_requires` say who may cast it. Stim Packs has no order: a mod's order whose tech is
`stim_packs` or made from it, and whose order is none of those six, stims the caster as the
classic button does. It pays the tech's energy, and a caster with more than 10 life gives
10 for the classic stim time. The rule is `ModOrderHandler` (`port_moddata.cpp`), read by
the order dispatch (`CUnitUpdate2.cpp`); the classic game's own orders are untouched. A mod's order with `blocks` runs them in place
of firing its weapon (Action blocks, below).

A mod's weapon may use explosion 25, which only a mod reaches: the enemy splash of
explosion 3, then a stun of 8 of the paralysis timer's ticks (Maelstrom's) on every
enemy within its outer splash that is on the ground and is no building.

A computer's unit of a mod casts the casts of its card that have an `ai`, tried in the
card's order where 1.17 tries its units' spells (`ai_update_unit_spells`), when it has the
energy and the tech's `use_requires` are met. The `ai` names how it finds a target: one of
1.17's finders by the spell it serves, `psionic_storm`, `yamato_gun`, `plague`,
`dark_swarm`, `ensnare`, `spawn_broodlings`, `parasite`, `stasis_field`, `emp_shockwave`,
`irradiate`, `defensive_matrix`, `disruption_web`, `restoration`, `optical_flare`,
`feedback`, `mind_control`, `maelstrom`, `lockdown`, `hallucination`; or `enemy_cluster`,
Psionic Storm's finder with the spell's own weapon and without its count of the caster's
side, for a spell whose splash spares it (Psionic Storm's never casts beside its own
melee units). An aimed cast goes at the target found; a cast where the caster stands is
given when a target is inside its weapon's outer splash, where `enemy_cluster` searches.
Both of the test mod's casts use `enemy_cluster`.

A mod's unit has its looks' voices, portrait, command icon and wireframes where its record
gives none of its own (below). A computer trains a mod's unit only as a stand-in its race's `ai` names (Races,
below), and researches no mod tech or upgrade.

### Statuses

`statuses`, beside the tables, is the mod's statuses by name: a thing a unit has for a while,
slowed, poisoned, stunned, hastened. A weapon's `status` puts one on every unit the weapon
damages, a unit's `aura` on the units near it every so often, and an order's blocks where the
cast says (Action blocks, below). Only a game that has the mod has them.

    "statuses": {
      "venom": {"name": "Venom", "duration": 6, "move_speed": -50, "damage_per_second": 4,
                "overlay": "thingy_ensgoos", "overlay_by_size": true},
      "mending": {"name": "Mending", "duration": 1.5, "life_per_second": 6}
    },
    "weapons": {"venom_spit": {"base": "needle_spines", "status": "venom"}},
    "units": {"void_mender": {"base": "terran_medic",
              "aura": {"status": "mending", "radius": 160, "targets": "allies", "every": 1}}}

A status's fields, each one optional but `duration`:

| field | |
|---|---|
| `name` | what a player reads; the record's name without it |
| `icon` | a frame of the command icons, shown with it in the studio (a game does not show it) |
| `side` | `good` or `bad`: which side the status is on, for what shows it. Without one the side is worked out from the fields, which is right for nearly every status: each field adds its own weight, armour, speed, damage dealt and life given counting good, and damage taken, a longer cooldown, life lost, and being held still or kept from attacking counting bad, so a status that gives with one hand and takes with the other comes out on the side it leans. Set it where a status sums to nearly nothing and one balance edit would flip it |
| `look` | how the status shows itself on the unit readout, where what is worked out from its fields reads wrong. The keys are `kind` (one of the fourteen below), `icon` (a command icon of its own), `colour` or `hue` (`#rrggbb`, or `for` or `against` to take its side's colour), `pattern` (the one laid over a wireframe, `none` for none), `show` (a list of `readout`, `wire` and `group`: where it shows at all) and `priority` (0 to 1000: how strongly it asks for the one mark a wireframe has room for). Every key may be left out and what is left out is its kind's own |
| `duration` | seconds at Fastest (24 frames a second), from when it is put on |
| `stacking` | what a new one does while it lasts: `refresh` (the default) restarts it, `stack` adds a stack and restarts it, `ignore` does nothing |
| `max_stacks` | the most stacks, 1 to 255, 5 by default; each stack adds what the status changes once more |
| `move_speed` | percent of the unit's speed, -100 to 400: -50 is half its speed |
| `attack_cooldown` | percent of the time between its attacks, -90 to 400: 25 attacks a quarter slower |
| `damage_dealt` | percent of the damage its weapons deal, -100 to 400 |
| `damage_taken` | percent of the damage it takes, -100 to 400: -25 takes a quarter less, from any source, before armour |
| `armor` | points of armour, -100 to 100; a unit's armour stays 0 or more |
| `cannot_move`, `cannot_attack` | true holds it still, or keeps its weapons from firing |
| `damage_per_second` | life lost a second, which may kill; given every 8 frames |
| `life_per_second` | life gained a second, up to its most; below 0 life lost, never below 1 (as Plague's) |
| `overlay` | an image over the unit while it lasts, by name or number. It wants an image meant to sit on a unit, like the Ensnare goo or one of the glows, and not a weapon's own image: a weapon's image damages in its death animation, and a unit's death plays that animation on every image it carries, so the overlay would be asked to damage with a weapon that is not there. Nothing is damaged when it is -- the engine does nothing where there is no weapon behind the image -- but the look is wrong as well, since it is a shot's picture rather than a mark on a unit. A load says so for this field, for `start_image` and `end_image` and for a `show` block's sprite, naming the animation that damages, in `hyperion_mods.log` and among `--mod-check`'s warnings; it reads the picture's own script rather than its name, so a picture that shares a weapon's name and is not one passes. A picture named by `hx.show` or `hx.overlay` is not read, since a script picks it as it runs |
| `overlay_by_size` | true takes the image after `overlay` for a medium unit and the next for a large one, as Ensnare's goo is (`thingy_ensgoos`) |
| `start_image`, `end_image` | an image played on the unit as it starts and as it ends; one whose script ends |
| `on_end` | a script called as it runs out or is taken off, with `e.unit` and `e.status`; a unit that dies with it takes its statuses with it, so it is not called then |

A unit holds six statuses at once; a seventh is passed over. The speed, cooldown, damage and
armour are worked out where the classic game works out its own (`CUnit::ModifyGivenMovementSpeed`,
`CUnit::GetWeaponCD`, `CUnit::Damage`), after Stim Packs and Ensnare, so the classic statuses and
a mod's add up. A subunit (a turret) has the statuses of the unit it is part of. A unit hidden in a
transport keeps counting its statuses down and takes no life from them.

A unit's `aura` is one aura, or a list of up to three each with its own fields: `status`, the
status it gives; `radius` in pixels, 160 by default; `targets`, whose units: `allies` (the
default, its own among them), `enemies`, `own` or `all`; `every` seconds, 1 by default; `self`,
false to leave itself out; `buildings`, true to reach buildings too. A unit gives each while it is
finished and not hidden, the first time on the frame it is.

A weapon's `status`, a unit's `aura` and an order's `blocks` are the record's own: a record based
on it does not have them, and an edit of a classic row may give them (`{"edit": "gauss_rifle",
"status": "venom"}`).

### Action blocks

An order of the mod's may have `blocks`, what its cast does in place of firing its weapon: a
list run in turn as the cast goes off (where the caster's script says `castspell`), each block an
object whose `do` names it. The spell order still walks the caster into its weapon's range, takes
its tech's energy and plays its animation; the weapon gives the range and the cooldown only. An
order that runs a classic spell's own work (Recall and the others above) runs no blocks.

    "orders": {
      "void_step": {"base": "cast_psionic_storm", "weapon": "void_step", "tech": "void_step",
        "blocks": [
          {"do": "teleport"},
          {"do": "spawn", "unit": "zerg_zergling", "count": 2, "seconds": 20},
          {"do": "status", "status": "venom", "radius": 96, "targets": "enemies"},
          {"do": "repeat", "times": 3, "blocks": [
            {"do": "wait", "seconds": 1},
            {"do": "damage", "amount": 10, "radius": 64}
          ]}
        ]}
    }

| `do` | its fields |
|---|---|
| `fire` | `weapon`, the order's own without it: fired as the cast fires it |
| `damage` | `amount`, before armour and size, and `share`, a percent (1 to 100) of each unit's most life added to it; `damage_type`, `normal` (the default), `explosive`, `concussive` or `independent` |
| `heal` | `amount` of life, up to the unit's most |
| `energy` | `amount` of energy given to a spellcaster, up to its most; below 0 taken away |
| `status` | `status`, put on the units it reaches |
| `remove_statuses` | `status`, taken off; without it every status of the mod's |
| `spawn` | `unit`, `count` (1 to 64), `seconds` they live (0 for good); at the place aimed at, or the caster with `"at": "caster"`, as Create Unit makes them |
| `teleport` | none: the caster goes to the nearest free place to where it was aimed, with Recall's flash |
| `sound` | `sound`, played where the block is |
| `show` | `sprite`, shown where the block is, or on the caster with `"at": "caster"`. It wants a sprite meant to be shown on its own, not a weapon's, for the reason a status's `overlay` does: a weapon's picture damages in an animation of its own, and shown with no shot behind it there is nothing to damage with. Two of the original's even damage in their first animation, so they would ask for it the moment they appear |
| `wait` | `seconds`; the blocks after it go on that much later; `blocks` inside it, run after the wait |
| `repeat` | `times` (1 to 1000) and `blocks`, run that many times in turn; a wait inside paces them |

`damage`, `heal`, `energy`, `status` and `remove_statuses` reach the unit aimed at (the caster for
a cast where it stands) or, with `radius` in pixels, the units within it of the place aimed at
(`"at": "caster"`, the caster): `targets` says whose, `enemies` for damage and statuses, `allies`
for the others, `own` or `all`; buildings only with `"buildings": true`. A block runs on the frame
the cast goes off and the program after a wait on its frame, in the order the casts were made; a
program holds 256 blocks at most, nested ones counted, and a repeat nests 8 deep. A block the
engine does not run, or a field it does not read, stops the load: `orders void_step.blocks[2]
does "blink", which is no block the engine runs`.

In a save the statuses on each unit and the programs running are a section of their own (HXSU),
so a game loaded goes on as it was; a replay plays them again from the casts and hits it records.
`statuses` on the live channel lists the units that have any, with each one's frames left and
stacks.

### Races

`races`, beside the tables, adds races a player can choose. A race plays one of the three
races' ways, its `base`: their way of building, supply, console, sounds and the number a
replay's header keeps for the player. What it has of its own:

    "races": {
      "hybrid": {
        "name": "Hybrid",
        "base": "protoss",
        "start": [{"unit": "protoss_nexus"}, {"unit": "protoss_probe", "count": 4}, {"unit": "hybrid_reaver"}],
        "units": ["hybrid_reaver"],
        "without": ["protoss_dark_templar"],
        "ai": {"protoss_dark_templar": "hybrid_reaver", "psionic_storm": "psionic_lance",
               "protoss_ground_weapons": "hybrid_claws"}
      }
    }

`name` is the name a player sees and `--race` takes, as does the record's own name.
`start` is what a player of it starts a melee game with, in place of its base's town hall
and workers: a building goes where the town hall would, the rest at the start location,
`count` of each (1 by default); without `start` the race starts as its base does. `units`
are the mod's units that are this race's alone: no other player trains them, while a mod
unit no race names is any player's. `without` are 1.17's units its players do not have,
taken out as a map takes units out, so their buttons are not shown. `ai` is for a computer
of the race, which plays 1.17's AI scripts for its base: where a script names a unit of the
left, the computer makes, waits for and attacks with the unit on the right
(`ModRaceAiUnit`, read by each script command that names a unit of the player's own). Without
it, a script that asks for a unit the race goes without waits for it for ever, and so it does
when the unit on the right cannot be made where the script asks: what it needs must be
built by then, and the gas there. 1.17's Protoss scripts open with Zealots and ask for
Dragoons only after the Cybernetics Core, so the test race keeps its Zealots, and its
Hybrid, which needs the Templar Archives and gas, stands in for the Dark Templar, which come
after both.

`console` and `music`, each `zerg`, `terran` or `protoss`, are the console a player of the
race sees and the music the game plays for them, another race's in place of its base's
(`ModRaceLook`, read where the game loads its console and where it starts its music). They
change what is seen and heard only; the advisor is `advisor`, below.

`rules` is how the race builds, as blocks it picks in place of its base's:

    "rules": {"build": "warp", "power": true, "power_from": ["iron_pylon"]},
    "advisor": {"race": "protoss", "lines": {"supply": "iron_more_pylons"}}

`build` is the way its workers build, `morph` (the worker becomes the building, as a
Drone does), `construct` (the worker stays and builds it, as an SCV does) or `warp` (the
worker starts it and goes on, and the building warps in by itself, as a Probe's does); a
worker builds its base's way without it. The rule reaches the worker whose looks are its
base's worker's, the Rig of a Terran-based race as the SCV: its build buttons place the
building with that way's order, the computer's workers build that way too, and a building
whose image script has no animation for the way (a Terran building warped in has no warp
animation) finishes without it (`ModRulesWorkerOrder`, `ModRulesOrderAllowed`,
`ModRulesNoAnimation` in `port_modrules.cpp`). A building warped in or morphed is not
resumed by a worker's right click, as a Protoss one is not.

The other blocks are carried by the units' own fields, which the game reads for any race,
and `rules` records what the race wants of them for the studio, which sets the fields:

- `power`: its buildings need power, `requires_psi` in their `flags`, as Protoss ones do.
  A unit gives power when its `idle_order` is `init_psi_provider`, as the Pylon's is
  (`power_from` names them): the power field and the grid count it, the placement shows its
  field and refuses a building out of it, a building left without power stops, and saves
  and replays keep it (`ModRulesPowerSource`, `ModRulesPowered`). A computer of the race
  builds its power maker first when its script asks for a building that needs power and it
  has none, where its base's supply building would be (`ModRulesAiPowerFirst`).
- `creep`: its buildings need creep, `requires_creep` in their `flags`. A unit makes creep
  when its `idle_order` is `init_creep_growth`, as the Hatchery's is (`creep_from`): one that
  is a town hall (`resource_depot`) spreads the Hatchery's creep and makes larva, another the
  Creep Colony's (`ModRulesCreepType`).
- `shields`: its units have shields, `shields_enabled` and `shields`, which come back as
  Protoss shields do.
- `regenerate`: its units heal by themselves, `regenerate` in their `flags`, as Zerg do.

A mod's gas building, one that looks like the Refinery, the Assimilator or the Extractor (its
`base`), or any building of the mod's with the `resource_container` flag that is no mineral
field, goes on a geyser and is gathered from as they are, whichever way its race builds, and
turns back into the geyser when it dies (`ModRulesGasType`). A mod's add-on, and a mod's
building that lands, take their add-on position from the classic unit they look like, since
the game keeps that column for the classic buildings only (`ModRulesAddOnOffset`): so a mod's
unit with the `addon` flag is made from a classic add-on or looks like one, else the loader
refuses it. A card's `train` button for an add-on places it beside the building, as the
classic add-ons' buttons do, and the building builds it there. A unit of the mod's keeps its
base's tests of the building that makes it, after the `at` that names that building: one
made from the Marine or the SCV is not trained while its building is in the air or busy with
an add-on, one made from the Comsat Station is not built while an add-on is attached or the
building is busy (`BuilderTests` in `port_moddata.cpp`). A mod's building that looks like a
classic one that lifts off lands as itself, and takes back an add-on of its own where it
left it, as the classic buildings do.

What stays its base's: supply (each unit's `group_flags` say which race's supply it
counts in), Terran buildings burning when hurt (by the same group flags), add-ons (the
`addon` flag and the cards' buttons), a building cancelled while it morphs giving back the
worker only to a Zerg-based race, and the AI scripts, which are its base's. A power maker
cannot train units: the game keeps its power links where a trainer keeps its rally point.

`advisor` is the race whose advisor a player of the race hears and reads, `zerg`, `terran`
or `protoss`, in place of its base's: "You must construct additional Pylons" and the rest
(`ModRulesAdvisorSound`, `ModRulesAdvisorString`, read where the game plays a sound for the
player and shows an error). As an object, `race` is that race and `lines` gives sounds of
the mod's own for lines by name: `minerals`, `gas`, `supply`, `energy`, `base_attacked`,
`forces_attacked`, `research_done`, `upgrade_done`, `unpowered`, `error`, `cannot`,
`cannot_place`, `cannot_land`, `cannot_build_here`, `build_error`. Like `console` and
`music` it changes what is heard only, and only for the player of the race on this
machine.

`ai` maps techs and upgrades the same way: where a script researches one of 1.17's techs
or upgrades on the left, the computer researches the mod's on the right
(`ModRaceAiTech`, `ModRaceAiUpgrade`, read by the scripts' `tech` and `upgrade`), at the
building its `requires` names, as a player would. 1.17's techs and upgrades go by their
names in lower case with underscores, `psionic_storm`, `protoss_ground_weapons` and so on
(`kTechs117`, `kUpgrades117` in `port_moddata.cpp`). The test race researches Psionic
Lance where its scripts research Psionic Storm, which its Hybrids then cast (U9), and
Hybrid Claws in place of the ground weapons.

A race is chosen with `--race <name>` (`engine/docs/hyperion-exe.md`) or in the front
end's skirmish set-up, for the player and for computers (`--computer-race <name>` gives
every computer one). A computer of a mod's race plays 1.17's AI scripts for its base,
with the race's start units and the stand-ins its `ai` names; the race goes to the computer
of the row that chose it, wherever the game seats it (`PlayersSeatedComputer`). Its AI takes a
stand-in for the classic unit it replaces wherever it decides a unit's part, worker, town hall,
gas building, supplier, soldier, and where a building goes (`ModRaceAiClassic`, the `ai` map read
backwards), and makes the stand-ins where it would make its base's worker, town hall, gas
building and supplier; so a race whose `start` replaces the town hall and workers mines,
builds and attacks as its base does. A worker of any player takes its load to a mod's town
hall as to a classic one. A team game, which only
multiplayer offers (single player lists no team types, as 1.17's did not), has the three
races only for now. The replay records each slot's race in a section of its own
(`RACE`) and the save in `HXRA`, and the game played back or loaded has them.

## Tilesets

`tilesets`, beside the tables, changes the classic tilesets a map's terrain is drawn with:
their colours, the pictures of their megatiles and the flags the game plays by. Each member is
a tileset by its name (badlands, platform, install, ashworld, jungle, desert, ice, twilight),
and what it gives replaces classic's in a game with the mod; the rest stays classic.

    "tilesets": {
      "badlands": {
        "palette": {"219": "#3e0f06", "211": [53, 20, 13]},
        "groups": {"2": {"buildable": false, "comment": "Dirt (Mars red, no building)"}},
        "megatiles": {"9": {"image": "tilesets/badlands/megatiles/9.png",
                            "walkable": ["####", "#..#", "#..#", "####"], "height": "mid"}}
      }
    }

- `palette`: entries 0 to 255, each `"#rrggbb"` or `[r, g, b]`. The ranges the game cycles
  (water, lava, lights) keep cycling, through the colours the mod gives them.
- `groups`, by the group's number in the tileset's `.cv5`: `buildable` and `creep` (the group's
  own flags), `walkable`, `blocks_view` (true or false) and `height` (`"low"`, `"mid"` or
  `"high"`) set on every minitile of the group's 16 megatiles, and `image`, a PNG 512 by 32 (or
  a multiple) of the 16 side by side.
- `megatiles`, by the megatile's number in `.vx4`: `walkable` and `blocks_view`, true, false
  or four rows of four of `#` (set) and `.` (not), one a minitile; `height`, a word or four
  rows of `l`, `m` and `h`; and `image`, a PNG 32 by 32 (or a multiple, averaged down). A
  megatile's own fields go over its group's.
- `comment`, in a group, a megatile or the tileset, is a note the game does not read.

A picture is mapped into the tileset's palette, as the mod has it, each pixel to the nearest
colour the game does not cycle; a pixel more than half clear keeps classic's. Its minitiles
are added to the tileset's, so a picture changes the megatile wherever the tileset uses it and
nothing else. The studio puts pictures at `tilesets/<tileset>/groups/<n>.png` and
`tilesets/<tileset>/megatiles/<n>.png`; any path in the data folder reads. A mod's tilesets go
over those of the mods it depends on, entry by entry.

The flags change how a game plays (where units walk and see, what is built where, the
ground's height for sight and hits), so they are part of the mod's data: in its fingerprint,
in `types.json`, and only in a game with the mod. A classic game, its replay and a game
without the mod have the classic tilesets. The changes are made as the map loads, so a push
while a game runs does not change its terrain. A tileset the mod recolours or draws anew is
drawn in classic art: Remastered's terrain, which has its own pictures, is left out for it. A
map's picture in the menus (the lobby's, the live channel's `map_info`) shows the tileset as a
game with the loaded mod has it.

A tileset of the mod's own, one a map names, is not made: a map names its tileset by a number
of eight (`ERA`), so a ninth needs a change to the map format, and maps are made in StarDraft 2.
`--mod-check` refuses a tileset no game has, a group or megatile past the tileset's, a field
no group or megatile has, and a picture that does not read or is the wrong size; and lists
each tileset's changes in a note.

## Scripts

A mod's data folder may hold `scripts/*.lua` (or `.luau`), Luau compiled as the mod loads; a
script that does not parse or compile refuses the mod with its file and line, and `--mod-check`
says so. Their functions are named by the mod's records, and the game calls them at fixed points
of its frame, in mod games only (`port_modlua.cpp`):

- an order's block `{"do": "script", "call": "<function>"}` among its `blocks` (Statuses and
  action blocks), called where the cast's program reaches it;
- a unit's `"scripts"`: `on_hit`, `on_damaged` (it may change the damage as it comes),
  `on_death`, `on_made` and a `tick` with `every` seconds, each for every unit of the type.
  `on_death` is called at the frame's end, when the unit is gone, but `hx.get` still reads the
  values it kept;
- an order's `"toggle"` `{on, off, drain}` (on and off called, energy drained a second while on),
  `"channel"` `{tick, every, stop, seconds}` (called while the caster stands; a new order stops
  it), `"autocast"` `{choose, every}` (the function picks the target), and `"cooldown"` and
  `"charges"` for any order, which classic spells lack;
- types.json's `"game"` `{start, tick, every}`, and its `director` `{call, every}` for a mode
  that plays a player itself (A game mode, below).

A script acts through the `hx` table: units found in an area or on the map (`hx.units`), their
life, shields, energy, place and owner read and set, damage through the game's own damage
(`hx.damage`), `hx.heal`, `hx.kill`, `hx.move` (to the free place nearest), `hx.order`, units made
(`hx.make`, for a time if given), the mod's statuses put on and taken off (`hx.status`,
`hx.unstatus`), a sound, sprite or overlay shown, toggles, charges, autocast and channels, and
values kept on a unit or on the game (`hx.set`, `hx.game_set`). The studio's script editor lists
every name with its arguments.

`hx.status` holds one of the unit's six slots for as long as the status lasts, and is passed over
on a unit already holding six (Statuses, above). So a status a unit keeps for its life spends a
slot for its life: a unit marked that way takes one fewer of whatever else is put on it later.

`hx.units` takes a table of what to look for, and gives a list of unit numbers:

    hx.units({ x = px, y = py, radius = 224, enemies_of = hx.owner(u), max = 64 })

| Field | What it is |
|---|---|
| `x` · `y` · `radius` | the area to look in, in map pixels; with no radius the whole map |
| `type` | one type only, by its name or number |
| `owner` | that player's units only. **A player number, 0 to the game's last slot** |
| `enemies_of` · `allies_of` | the units an enemy or an ally of that player owns. **A player number, not a unit**: `hx.owner(u)` gives a unit's player |
| `buildings` | false leaves the buildings out; they are in the list otherwise |
| `max` | how many at most, 1700 at most and 1700 by default |

A player number, a type number and a table's name are checked as they are read, and a value
outside the game raises an error naming the call and what was passed, which the game writes to
`hyperion_mods.log` with the script's line. A unit number passed where a player belongs is the
mistake this catches: the two are both numbers, and the game would otherwise read its player
tables with it.

A game with scripts plays the same on every machine and in its replay and save:
- a script reads the game and the game's own random only (`hx.random`); there is no clock, no
  `os`, no `io`, no file but the mod's scripts;
- `hx.random` has the three forms Lua's `math.random` has -- `hx.random()` a part of 1,
  `hx.random(n)` a whole number from 1 to n, `hx.random(a, b)` one from a to b -- and it draws
  from the game's own random, which the save and the state hash carry, so every machine and the
  replay draw the same numbers in the same order. `math.random` is that same function, so a
  script written the way Lua is written gets the game's random rather than the host's, and
  `math.randomseed` is refused: the stream is the game's and not a script's to set. It is for a
  game only, so a script's top, a mode's `generate` and the set-up's own calls raise rather than
  draw -- a mode that wants a roll before there is a game rolls it from its run seed itself,
  which is what `generate` is given one for;
- a function keeps nothing between calls but what it gives the game to keep: numbers and true or
  false on a unit or on the game, saved in the save's `HXLU` section;
- numbers are whole where they meet the game (pixels, life in points); `hx.isqrt`, `hx.pow` and
  `hx.distance` are the game's own, and the host's sine and cosine are replaced by its own;
- every call has the same budget of the VM's steps (100,000), and a mode's `generate` ten
  million of its own; a call that runs past it, or raises an error, stops alone and is logged the
  same way on every machine;
- a function may not change a global or a local of its script, which would outlive the call and
  is in no save: the load refuses a script that does, with the file and line.

What the scripts do can be followed while the game runs: the live channel's `scripts` gives
the state they keep and the lines they said (`hx.log`), and
`script_trace {on?, since?, ai_since?, run?}` gives a row for each act they make on the game -- a unit taken or let go, an order, a cast, a
unit made, damage, a status, a win or a loss -- with its frame, the unit it was for and the hook
it was made in, the last 4,000 of them. Each row's `at` is its number, and `since` reads from a
number on, so a reader follows the trace by asking again past the last row it has.
`--mode-trace` has it on from the first frame.

The same answer's `ai` is the mode's own decisions: one row for each `brain` call and each
card `ai` call, with the frame, the unit, the function, whether it decided anything and what
it ordered -- the order, the unit it was given on or the place it was given at -- or why it
decided nothing. The last 512 are kept, read from `ai_since` on. All of this is read only: it
goes in no save and in no hash, and a game plays the same whether anything is watching or not.

### Marks on the map

`hx.area` marks ground, so a mod can show an area the game's own rules say nothing about: a
perimeter to hold, a landing zone, the reach of something it runs itself. A mark is placed in map
pixels and sits on the ground, moving and scaling with the map as the player scrolls and zooms.

    hx.area("perimeter", {shape = "ring", x = cx, y = cy, radius = r, color = 2,
                          edge = 0.9, edge_width = 6, soft = 1,
                          fade = {near = 320, most = 0.9, glow = true}})
    hx.area("drop", {shape = "circle", x = x, y = y, radius = 96, color = "#ffcc33",
                     fill = 0.18, edge = 0.7, dash = 24, pulse = 2})
    hx.area("perimeter")   -- nothing in place of the table clears that mark

Each call takes the mark's own name first. Setting a mark of the same name again replaces it,
which is how one is moved or grown, so a script may set it every tick: its pulse keeps its beat
through the change, and its fade is measured where it stands now. A mark cleared fades away over
half a second rather than vanishing, so a perimeter lifted leaves the screen as a perimeter
would, and one set again while it fades comes back. Two marks on the same centre are two marks,
each with its own name, which is how a band outside an edge is drawn. A game shows 32 at once.

| Field | What it is |
|---|---|
| `shape` | `circle` (the default), `ring`, `rect`, `polygon` or `line` |
| `x` · `y` | a circle's and a ring's centre, a rectangle's top left, a line's first point, in map pixels |
| `radius` | a circle's and a ring's |
| `w` · `h` | a rectangle's size |
| `x2` · `y2` | a line's second point |
| `points` | a polygon's corners, x and y in turn as one flat list, 64 corners at most; the shape is closed |
| `color` | a player's number for that player's own colour, or `"#rrggbb"`; white without one (`colour` is taken as well) |
| `players` | who sees it: a player's number or a list of them; every player without one. A mark of one player's own ground is drawn on that player's screen alone, and a viewer of no side sees every mark |
| `layer` | `ground`, the default, paints the mark into the map itself, so units and buildings stand on it and it takes the ground's own light. `over` draws it above everything on the map, which reads at any zoom and over any ground: it is for a mark a player must not lose behind a base, and it is what a mark has to say to sit above the units, as every mark did before this field existed. A mark painted on the ground wants the full colour the game draws by default, so a player who has set Settings' player colours to Classic, or whose game is on the renderer that paints no scene, sees that mark drawn over the game instead: it is never lost, but it is not the same look, and the setting is the player's and not the mod's |
| `fill` | the inside's opacity, 0 to 1; none without one. A ring and a line have no inside |
| `edge` | the edge's opacity, 1 without one; 0 draws no edge |
| `edge_width` | the edge's width in map pixels, 2 without one, so it thins as the player zooms out |
| `dash` | a dash and its gap, in map pixels; the edge is drawn whole without one |
| `soft` | a glow under the edge, this many of its widths wide; none without one |
| `pulse` | seconds a beat, over which the opacity swells and falls; steady without one |
| `pulse_depth` | how deep that swell goes, 0 to 1 and 0.3 without one: at the beat's lowest the mark is drawn at this much less than its own opacity, and 0 leaves it steady. The swell travels along the edge, so a long edge shows it as a wave running round and a short one swells all together |
| `fade` | `true` for the fade with every default, or the table below; without it the mark is always drawn at the opacities above |
| `outside` | what is drawn under one of the player's own units standing outside a closed mark: `{style = "tint", band = <map pixels>}` puts a beating light on the ground beneath it, in a warning colour within `band` of the edge and a danger colour past it, so a player sees at a glance which of their units are out and how far. Nothing without it. `"ring"` and `"tether"` are taken and draw as nothing for now. It is the player's own units only, and a viewer of no side is shown none. How that light is drawn -- how far it reaches, its two colours, its beat -- is the look's `outside` block below |
| `look` | the look the mark wears, by the name the mod's own types.json gives it under `marks` (The looks a mark wears, below). Without one the mark is its edge and its inside and nothing more. A look is blocks of light with their own values, and it may ask for the mark's own values as well, which the fields here win over |
| `crossing` | what the mark does when a unit of those that light it crosses its edge: `"ripple"` sends a wave running away along the edge from the place, brightest where it happened and gone in under two seconds, and a unit on its way out turns that stretch of the edge toward the look's warning colour. `"none"` without one. `"flash"`, `"stamp"` and `"arc"` are taken and draw as `none` for now. Only the edge itself counts, not any band a mod has drawn outside it, and only units the player watching can see, so a crossing in the fog goes unanswered |
| `minimap` | whether the mark shows on the minimap as well, and how: `true` for the look's own minimap block, `false` for none, or a table of `on`, `fill`, `edge`, `width`, `dash` and `color`, which takes a player's number for that player's colour or a `#rrggbb` of its own. Nothing without it or a look that carries one. The minimap's picture has no alpha and redraws on a timer of its own, so a mark there is drawn in the palette entry nearest its colour, `width` and `dash` are the minimap's own pixels rather than the map's, and nothing of the fade or the pulse reaches it: a perimeter shows on the minimap whole, whatever the ground's light is doing. It is drawn on every renderer, SDL's included, where a mark on the ground does not show at all. `hx.minimap()` gives the picture's `width`, `height` and `scale` -- the map pixels one of its pixels covers -- or false before there is one |

A name `hx.area` has no field for is a mistake in the mod rather than something to pass over:
the call fails and says which name it was, and the tables inside `fade` and `outside` are held
to their own fields the same way. A mark drawn without a field the mod meant to set would be
drawn wrong with nothing to say why.

The fade leaves a mark mostly hidden and lights the stretch of edge a unit comes near, rather
than the whole mark, unless the mod turns `glow` off. Its own
`players` is not the mark's: `players` above says who sees the mark at all, and the fade's says
whose units light it up. A mark drawn for everyone whose fade answers to one player's units is
the usual thing -- an ally can see where your perimeter is, and it brightens where you stand on
it.

| `fade` field | What it is |
|---|---|
| `players` | whose units reveal it: `"me"` (the default, the player watching), `"allies"`, `"all"`, or a list of player numbers |
| `near` | how far off a unit begins to reveal it, in map pixels; 192 without one, about a third of the classic view |
| `most` | the opacity it reaches with a unit on it, as a part of the mark's own, 1 without one |
| `least` | and the one it rests at with nothing near, 0 without one |
| `glow` | true (the default): only the part of the edge near the unit shows, so the mark lights up where it is met. False: the whole mark fades together |
| `curve` | how the light falls off over that distance: `"smooth"` (the default) eases away, `"even"` falls in a straight line, and `"sharp"` holds full past half the way and then drops, which keeps a mark dark until a unit is nearly on it |

Only units the player watching can see reveal a mark, so nothing shows through the fog, and each
player's screen fades its own: a mark is the same for everyone, what is drawn of it is not.

Marks are presentation and nothing else: the simulation never reads one, none of them is in the
save, the replay or what the other players are sent, and a game plays the same with them or
without. A replay and a watched seat draw them because the mod's own scripts run there and set
them again. A mark is drawn over the game's picture, so it is over the units rather than under
them. Under SDL's renderer, the fallback where the card cannot run the GPU presenter, nothing of
the overlay is drawn and marks do not show.

### The looks a mark wears

A mark's edge and its inside are all the engine draws of its own. Anything past that is a look:
blocks of light stacked on the mark, each with its own values, which the mod names in its
types.json under `marks`. The engine knows no look by name, so what a mark wears is the mod's
and two mods' walls of light need look nothing alike.

    "marks": {
      "ward": {
        "about": "A low wall of light standing on the perimeter.",
        "warn": "#ffb347", "edge_width": 3, "pulse": 2.2, "pulse_depth": 0.3,
        "curve": "sharp", "crossing": "ripple",
        "field": {"height": 26, "bands": 6, "alpha": 0.42, "fade": 1},
        "cells": {"spacing": 13, "rows": [0.30, 0.72], "stagger": 0.5, "radius": 4.2,
                  "squash": 0.8, "width": 1, "alpha": 0.7, "least": 0.12, "base": 0.55,
                  "swing": 0.45, "rate": 3.1, "along": 0.37, "row": 1.7, "warm": 0.2},
        "foot": {"glow": 4, "alpha": 0.22},
        "tear": {"life": 0.9, "width": 10, "grow": 30, "height": 1.6, "alpha": 0.9},
        "ripple": {"speed": 384, "width": 46, "life": 1.6, "flash": 50, "flash_life": 0.5},
        "outside": {"style": "tint", "band": 128, "radius": 26, "alpha": 0.55, "base": 0.65,
                    "swing": 0.35, "beat": 4, "past": 9, "color": "#ffb347",
                    "far_color": "#e0453a"}
      }
    }

A script puts that look on a mark with `look = "ward"`. A mode names its looks in its own
types.json the same way; a look of a name already loaded replaces it, the mode's over the mod's.

A block the look leaves out is not drawn at all, and a field a block leaves out takes the value
the example gives. Every length is in map pixels, which the camera scales; every opacity is a
part of the light at that point of the edge, so a block fades with the mark and the fade's
`least` holds the whole look down; and a rate is by the second. A block or a field this build
has no name for stops the mod as it loads, with the line of types.json it is on: nothing of a
look is passed over quietly, since a look drawn without a value would be drawn wrong and
nothing would say why.

The values beside the blocks are the mark's own, as the field table above gives them, which the
look asks for and `hx.area` wins over: `fill`, `edge`, `edge_width`, `dash`, `soft`, `pulse`,
`pulse_depth`, `curve` and `crossing`. `warn` is the colour the blocks answer a unit on its way
out in.

| Block | What it draws |
|---|---|
| `field` | light standing up the screen from the edge: `height` how far it stands (26), `bands` the bands it is drawn in (6), `alpha` the band at the foot's opacity (0.42) and `fade` how much of that the band at the top has lost (1). The height is the look's own, which the cells' rows and the tear's height are measured in. `add` (true without one) adds the field's light to what is under it rather than covering it, so it brightens the ground it stands on and two stretches of it piling up where an edge doubles back are brighter than one; false draws it over what is there, which is flatter and hides more. `lean` and `edge_on` are the face's, below (0.35, 1.2) |
| `cells` | outlines drifting in the field along the edge: `spacing` the edge between one set of cells and the next (13), `rows` each row's height as a part of the field's (`[0.30, 0.72]`), `stagger` how far along the spacing each row after the first is moved (0.5), `radius` and `squash` the outline's radius and how flat it is drawn (4.2, 0.8), `width` the line (1), `alpha` its opacity (0.7), `least` the light a stretch of edge needs before its cells are drawn at all (0.12), `base` and `swing` the flicker's middle and how far it swings (0.55, 0.45), `rate`, `along` and `row` its beat by the second, by the edge gone along and by the row (3.1, 0.37, 1.7), and `warm` the warmth past which a cell takes the warning colour (0.2). `shape` is `"hex"`, the only cell this build draws |
| `foot` | the mark's own edge with a glow spilling onto the ground under it: `glow` how many of the edge's widths the glow reaches (4) and `alpha` its opacity, as a part of the edge's (0.22). A look with a foot has no use for `soft`, which the foot stands in place of |
| `tear` | a column of light in the field where a unit came through, which wants `crossing`: `life` the seconds it lasts (0.9), `width` and `grow` how wide it begins and how much wider it is at the end (10, 30), `height` how tall it is, in the field's heights (1.6), `alpha` its opacity at the foot, falling to nothing at the top (0.9), and `lean` how far it leans the way the unit was going, in its own heights (0.5): a unit leaving leans the tear out over the ground outside and a unit arriving leans it in, so the two read apart wherever on the shape it happened. 0 stands it straight up and tells them apart by the warning colour alone |
| `ripple` | the wave a crossing sends along the edge: `speed` how fast it travels (384), `width` and `life` how wide it is and the seconds it lasts (46, 1.6), and `flash` and `flash_life` the flash at the place itself, which dies sooner (50, 0.5). `side` leans that flash toward the stretch of edge the unit was heading for, 0 to 1 and 0.5 without one, so a crossing taken at an angle shows which way it went; the wave itself runs away both ways alike. Nothing draws it of itself -- it is light the edge and the other blocks take -- so it has no layer of its own |
| `outside` | the light under one of the player's own units standing outside the mark: `style` and `band` as `hx.area`'s own field gives them, `radius` how far the light reaches (26), `alpha` its opacity at the middle (0.55), `base` and `swing` the beat's middle and how far it swings (0.65, 0.35), `beat` and `past` its rate within the band and past it (4, 9), and `color` and `far_color` the colours of the two (`#ffb347`, `#e0453a`) |
| `minimap` | the mark on the minimap's own picture, which `hx.area`'s `minimap` field turns on and off: `on` (true in a block that is there), `fill` and `edge` their opacities as parts of the pixels drawn (0, 1), `width` the edge's width and `dash` its dash, both in minimap pixels (1, 0), and the colour it is drawn in: `player` a player's number, whose colour it takes, or `color` a `#rrggbb` of its own, the mark's own colour without either and `player` winning where a block gives both. A recipe splits those two keys where `hx.area`'s one `color` takes either, since a recipe's colours are written `#rrggbb` everywhere else. It is not light on the ground but a mark on a palettized picture that redraws on its own timer, so it neither fades nor beats |

The blocks that stand are drawn on a wall standing on the edge, and an edge may run at any angle
across the map. The game's view is oblique and never turns, so a stretch of edge running across
the screen shows that wall's whole face and one running up the screen shows it edge-on. Up the
screen is both north and tall in this view, which leaves a face seen exactly edge-on nothing to
be drawn with at all: `field.lean` leans the top of the wall out along the edge's own normal,
across the screen, which gives that face its width back and stands a closed mark's wall round it
like the sides of a bowl. It is in the field's heights, 0.35 without one, and 0 stands the wall
straight up, where a stretch of edge running up the screen is drawn as a line again.
`field.edge_on` is what a face turned that far away is worth to look at, as a part of a face-on
one's brightness: 1.2 without one, since light seen along its own length piles up, and under 1
dims a turned face instead. The cells sit on that face, so they lean, squash and shade with it
rather than facing the viewer; the tear is turned by the crossing instead, as its own row says.

A block's `layer` says where it is drawn, `ground` or `over`, as a mark's own `layer` does. The
field, the cells and the tear stand up the screen from the edge, which the ground cannot be
painted with, so they take `over` and a look that asks the ground for one is refused as it
loads. The light outside lies flat on the ground and takes either. The foot is the mark's own
edge and is drawn where the mark's `layer` says, so a wall of light has its foot painted under
the units while the field stands over them, and a unit inside it stands behind the light.

A look is presentation like the mark it is on: the simulation never reads one, nothing of it is
in the save, the replay or what the other players are sent, and a game plays the same whatever
a look says. It lives in types.json, so the mod's fingerprint covers it as it covers the rest of
that file.


`sounds` are sfxdata.dat's rows, the game's sounds by number, and `portraits` portdata.dat's,
the talking heads. Both are heard and seen only: no game's state holds them, so neither a
replay nor the fingerprint has their files.

    "sounds": {
      "trooper_ready": {"file": "sounds/ready.ogg"},
      "trooper_yes": {"base": "terran_marine_tmayes00", "file": "sounds/yes.opus"},
      "louder_ready": {"edit": "terran_marine_tmardy00", "file": "sounds/hello.ogg"}
    },
    "portraits": {
      "trooper": {"idle": "portraits/trooper/idle", "talking": "portraits/trooper/talk",
                  "idle_chances": [85, 5, 5, 5]}
    },
    "units": {
      "trooper": {"base": "terran_marine", "portrait": "trooper", "ready_sound": "trooper_ready",
                  "yes_sound_first": "trooper_yes", "yes_sound_last": "trooper_yes",
                  "requires": [{"at": "terran_barracks"}]}
    }

- A sound's `file` is a path in the data folder: WAV, Ogg Vorbis, Opus, MP3 or FLAC, known by
  its first bytes. A WAV of 8 or 16 bits and one or two channels plays as it is; any other file is decoded
  to one as it loads, more than two channels mixed into two. The browser build plays no Opus.
  A file the folder does not have is named in the log and plays as a missing file of the
  original's does: not at all.
- A sound's `priority`, `length_adjust` (milliseconds the talking portrait runs past it) and
  `min_volume` are sfxdata.dat's. Its `flags` are a number or their names: `preload`,
  `one_per_unit` (a unit type says one of its sounds at a time), `one_at_a_time` (the sound
  plays once at a time), `uninterrupted`. A sound with no `base` needs a `file`.
- The game plays eight sounds at once. With eight playing, a new sound stops the playing one of
  the lowest `priority` below its own, if there is one not marked `uninterrupted`, and is not
  heard otherwise. The classic voices have priority 1 and are `one_per_unit` and
  `uninterrupted`.
- `min_interval` is the least milliseconds between two plays of the sound: asked again sooner,
  it is not heard. The classic game waits 80 for every sound, which is the value without one.
- `subtitle` is the words of the line, shown on the game's message line (where "Not enough
  minerals" shows) with the speaker's name as a unit of the player's says it: selected,
  acknowledging an order, annoyed, ready, or an advisor's line, the lines that move the talking
  portrait. Only a player with Settings' Unit subtitles on (`"unitSubtitles"`) sees it, in the
  player's language through the mod's `lang/` files (key `sound/<record>`, Languages). It is
  the record's own or its edit's: a sound based on another does not take its subtitle.
- The subtitle and the gap are what a player sees and hears only: a game, a replay and the
  other players are the same with them or without.
- A portrait's `idle` and `talking` are paths without their end: the game adds the video's
  number and one of `.webm`, `.mkv`, `.mp4`, `.m4v`, `.mov` or `.smk`
  (`portraits/trooper/idle0.webm`), the first the folder has in that order. It plays idle video 0 to 3 by
  `idle_chances`, the chance in 100 of each, and talking video 0 to 2 at random while the unit
  speaks; a video the folder does not have falls to the one numbered below it, so `idle0` and
  `talk0` are enough. Without `talking`, it talks with its idle videos.
- A portrait's video is Smacker, or VP8, VP9, AV1, H.264 or H.265 in WebM, Matroska, MP4 or
  QuickTime, with Vorbis, Opus or AAC sound if any. H.264, H.265 and AAC play through the
  player's system: on Windows always for H.264 and AAC (except Windows' N editions without the
  Media Feature Pack), and for H.265 with Microsoft's HEVC Video Extensions; on a Mac always;
  on Linux when FFmpeg is installed. Where they cannot, the video is not shown and
  `hyperion_video.log` says what is missing, so AV1 or VP9 is the choice that plays
  everywhere. One larger than the
  console's box (60 by 56) is drawn smaller to fit, its shape kept. On the GPU presenter a
  portrait that is not Smacker plays in full colour at its own size, fitted to the box with its
  shape kept (to the HD console's window when Remastered's art is drawn). The browser build
  plays Smacker portraits only.
- A unit's sound fields and `portrait` name the mod's sounds and portraits or the original's
  (`terran_marine_tmayes00`, `tmarine`). An edit of an original sound or portrait changes it
  for every unit that has it, in a game with the mod.
- `iscript.txt`'s `playsnd`, `playsndbtwn` and `playsndrand` take a sound's number or name.

## A game mode

A mod whose `mod.json` says `"kind": "mode"` is a game mode: a way of playing that the player
picks for a game, with settings of its own, and whose scripts run the game from the first frame.
`engine/mods/survival/` is one. A mode mod may hold its records, scripts, looks and language
files in its own folder beside `mod.json` with `"data": "./"`, so nothing is nested.

    {
      "id": "survival",
      "name": "Survival",
      "kind": "mode",
      "data": "./",
      "mode": { ... }
    }

Its `"mode"` block says what the player chooses and what the mode needs
(`port_modedecl.cpp`):

| Field | What it is |
|---|---|
| `line` · `about` · `notes` | one line for the list, a paragraph for the panel, and a note a line |
| `game_type` | how the set-up's Game type list shows it: `{"name", "tag", "plays"}` |
| `sim` | whether it changes the simulation; true when it is left out |
| `enemy_player` | true: the mode plays an enemy player itself, and no classic AI starts for it |
| `players` | `[least, most]`, the players it is played with |
| `needs` | `"any race"`, or the id of the mod it must be played with |
| `settings` | what the player sets, below |
| `presets` | named sets of those values: `{"id", "name", "line", "values": {...}}` |

A mode that brings its own enemy says `"enemy_player": true`, and the player it plays is the
one its `generate` is given as `ctx.enemy`: the first computer the first person plays against,
by the teams the set-up gave them. No classic AI script starts for it, so it builds nothing, trains nothing and mounts
no attack of its own, and every unit of it is outside the computer's town, captains and guard
posts, as a unit the mode takes with `hx.own` is. What that player does is the mode's scripts' to
say, through their units' brains and their orders. Everything else about the player is the
game's: its colour, its race, its alliances, its vision and its start units from the map.
Nothing about the other players changes, and a game with no mode is untouched. Until the set-up
has a seat of its own for this, a mode that wants it asks for a game with a computer in it.

What that player does is a hook of its own, the director, named in types.json's `"game"` block
and called every so many seconds:

    "game": { "start": "LsStart", "tick": "LsTick", "every": 1,
              "director": { "call": "LsDirector", "every": 2 } }

| `e` | What it is |
|---|---|
| `e.frame` · `e.seconds` | the game's frame, and it in seconds at Fastest |
| `e.player` | the player the mode plays, or -1 when it plays none |
| `e.units` · `e.buildings` | how many that player has, counted by the game rather than in the script |
| `e.minerals` · `e.gas` | what it holds |

The director is where a mode decides what to make, when and where, what its waves walk at, and
what it hands to its units' brains; it makes units with `hx.make`, orders them with `hx.order`
and owns them with `hx.own`, as any hook does. It runs on the game's own frames and is part of
the simulation, so it plays back and loads with the rest of the mode: its beat is the frame
itself, which a loaded game takes up where the save left it. A mode may have a director with no
player of its own, as a game master for whatever it drives; `e.player` is -1 then.

A mode is picked in the set-up's Game type list, after the classic types, and `game_type` says
how it appears there: `name` is what the list calls it, `tag` the mod it comes from, which the
row says as "from `<tag>` mod", and `plays` the classic type a game of it is launched as. With no
block at all the list shows the mod's name as both, and plays Melee. A mode sets `tag` only to
have its mod named differently -- shorter, or in the player's language -- and not to name a kind
of game, which the row would read as a mod that does not exist. `plays` takes a classic type's
name as the set-up writes it, in any case: `"melee"`, `"free for all"`, `"one on one"`,
`"capture the flag"`, `"greed"`, `"slaughter"`, `"sudden death"`, `"ladder"`,
`"use map settings"`, `"team melee"`, `"team free for all"`, `"team capture the flag"` or
`"top vs bottom"`. A name there is none of says so in `hyperion_mods.log` and is played as
Melee. `name` and `tag` are language keys like the rest of the mode's words,
`mode/<id>/game_type/name` and `/tag`.

    "game_type": { "name": "Survival", "tag": "Survival", "plays": "melee" }

A setting is a `choice`, a `toggle`, a `number`, a `list` of things to turn off, or a `seed`:

    {"id": "difficulty", "kind": "choice", "name": "Difficulty",
     "choices": [{"id": "recruit", "name": "Recruit"}, {"id": "brutal", "name": "Brutal"}],
     "base": "recruit", "effect": "LsDifficultyEffect"}

`name`, `group` and `gist` are its words; `base` is its value when the player sets nothing (the
first choice, false, the `min`, nothing turned off, 0); a number takes `min`, `max`, `step` and
`unit`. Three fields name functions of the mode's scripts, which the set-up calls as the player
changes things: `effect(value, values)` gives a line about what the value does, `read(value, map)`
one about the map chosen, and `shown(values)` whether the setting is shown at all. They run apart
from any game and may read nothing but their arguments. A setting whose `shown` is always false
is one no player sees and a tool sets, as Survival's `start_wave` is: the command line
(`--mode-settings start_wave=12`, or `--mode-settings @<file.json>` for a whole object) and the
mode record carry it like any other, so a game that starts part-way still plays and replays the
same.

A mode may also draw what it makes of the map, beside its settings in the set-up, with
`preview`. It is a function the mode block names rather than a setting's:

    "preview": "LsPreview"

It is given the settings and the map, as `read` is given a value and the map, and gives back a
list of shapes in the map's own pixels:

    function LsPreview(values, map)
      local shapes = {}
      for _, s in ipairs(map.starts) do
        shapes[#shapes + 1] = {shape = "ring", x = s.x, y = s.y, r = values.radius * 32,
                               colour = 0xffb347, edge = 0.9, width = 48}
      end
      return shapes
    end

| Field | What it is |
|---|---|
| `shape` | `circle`, `ring`, `rect`, `polygon` or `line`, the shapes a mark on the map takes |
| `x` · `y` | a circle's and a ring's centre, a rectangle's top left, a line's first point |
| `r` | a circle's and a ring's radius |
| `w` · `h` | a rectangle's size |
| `x2` · `y2` | a line's second point |
| `points` | a polygon's corners, `{x1, y1, x2, y2, ...}` |
| `colour` · `color` | `0xrrggbb`; white without one. Either spelling is taken, as a mark's is |
| `fill` · `edge` | how solid the inside and the edge are, 0 to 1; no fill and a whole edge without them |
| `width` | the edge's width, never drawn thinner than one pixel of the picture |
| `dash` | the dash's length; 0 draws the edge whole |

Every length is in map pixels, which is 32 to the tile, and the picture brings them down with
it. The shapes are drawn over the map under "Drawn as `<type>` reads this map", so a player sees
where the mode's own ground falls before the game starts, and sees it move as they change the
settings that decide it. A mode that names no `preview`, or whose call gives nothing back, shows
the map alone and leaves the caption out. Sixty-four shapes are drawn and the rest are left out.
A shape of a kind, or with a field, that the set-up has none of says so in `hyperion_mods.log`,
naming the mode and the function, rather than being passed over in silence.

The map it is given is the map read, the same table `read` is given: `starts` and `bases`, each
with `x` and `y` in map pixels and `tx` and `ty` in tiles, and the map's `w` and `h`. A start's
`slot` is the number the map itself gives that start and not a seat of this game's: the set-up
has no seating to offer, so a mode cannot tell from here which start a player or its own enemy
will hold, and a preview that wants to mark one has to choose it by the map alone.

`preview` runs apart from any game, as `effect` and `read` do, and what it gives back is drawn
and read by nothing else: the simulation never sees it, it is not in the save or the mode's
record, and a mode whose preview is wrong plays and replays correctly all the same. It is the
set-up's picture of the mode's ground and not the ground itself, which the mode's scripts set in
a game with `hx.area`.

The player's own words for all of this are the mode's language keys (Languages, below).

### What generate makes

`types.json` may name a function with `"generate"`. It runs once as the game starts, before the
map is loaded, and its records are that game's alone:

    "generate": "LsGenerate"

It is given one table:

| Field | What it is |
|---|---|
| `ctx.seed` | the run's seed: the `seed` setting, or the game's own when that is 0 |
| `ctx.settings` | every setting's value by its id |
| `ctx.players` | the slots that play: `{player, race, kind, start = {x, y}}`, `kind` `"human"` or `"computer"` |
| `ctx.enemy` | the first computer the first person plays against: `{player, race, x, y}`, or nil |
| `ctx.map` | the map read: `starts`, `bases`, `walk(x1, y1, x2, y2)` in tiles or -1, and `walkable(tx, ty)` |
| `ctx.row(table, name)` | the row a record would clone, by its record or classic name: every column at the record's own scale, and the name of each type a column points at (`ground_weapon_name`) |

It gives back a table in types.json's own shape, with any of the record tables, `cards` and
`looks`, plus `run`. Every record it makes is named `gen_<something>`, so a roll never stands in
for one of the mode's own records or a row of the game's; the records take ids after the mode's,
in the order of their names. `looks` is an array of `looks.json` entries, read after the mode's
own file, so an entry changes the look of its name. `run` is any tree of numbers, strings, true
or false and tables; the scripts read it as `hx.run`, which is read-only, and it is made again
from the seed when a save is loaded, so it is not in the save.

A roll has a budget of ten million of the VM's steps, and an error names the function and the
line. It must read nothing but `ctx`: the same seed and settings give the same records on every
machine, in the game's replay and after a load, and `--mod-check` holds a mode to that.

### What the mode's scripts have

A mode's scripts are `scripts/*.luau` as any mod's are, and may be in folders of their own
(`scripts/ai/brains.luau`), loaded in the order of their names into one environment. Beside
everything under Scripts, above, a mode has:

| Call | What it does |
|---|---|
| `hx.setting(id)` | the setting's value for this game |
| `hx.run` | what `generate` returned, read-only |
| `hx.own(units)` | the mode takes the units off their owner's computer, which then leaves them alone: it does not think for them, no town, guard post or attacking force takes them on, and one it already held it lets go of. They stay that player's for colour, score, supply and alliance, and the mark goes with a save and is in the state hash. A unit the mode owns is the mode's to order: with no brain and no orders it stands still |
| `hx.release(units)` | gives them back to the computer |
| `hx.cast(u, order, target)` · `hx.cast(u, order, x, y)` | the unit casts the order as its card's button would: the energy is taken, the animation plays, the blocks run. False when it cannot |
| `hx.win(players)` · `hx.lose(players)` | those players win or lose, as a trigger's Victory and Defeat do |
| `hx.message(text)` · `hx.message(text, players)` | a line on the message line, for everyone or for those players |
| `hx.hud(key, value)` | a value a HUD design draws as `mode.<key>` |
| `hx.timer(id, part)` | a countdown or a timer over the game (What a mode shows, below) |
| `hx.banner(id, part)` | a title with lines over the game, which goes by itself |
| `hx.counter(id, part)` · `hx.counter(id, n)` | a count over the game, as the wave out of the run's |
| `hx.ping(x, y)` | a minimap ping, as an ally's is |
| `hx.minimap()` | the minimap's picture: `width` and `height` in its own pixels, and `scale`, the map pixels one of them covers (16 on a 64-tile map, 32 at 128, 64 at 256). False before a game's minimap is made. It is what a length in `hx.area`'s `minimap` block is measured in |
| `hx.reveal(x, y, radius, seconds)` | the ground there shown to the mode's players for that long |
| `hx.order(units, ...)` | `hx.order` takes one unit or a list, and `"attack_move"` as well as the other orders |

`types.json`'s `"game"` block gained `on_death`, called for every unit that dies with
`e.unit`, `e.player`, `e.x`, `e.y` and `e.killer`, the player whose damage killed it. A unit's
`"scripts"` gained `brain` with `every`, called for each of its units the mode owns or a computer
plays, and a card's button may name `"ai": {"call": "<function>", "every": <seconds>}`, which is
called for those units too, so a mode can play a unit the game's own AI does not know.

`hx.message`, `hx.hud`, `hx.ping` and the three parts below are presentation: the game, its
replay and the other players are the same with them or without. Everything else a mode does is
simulation, so a game with a mode plays by Hyperion's rules and its replay is Hyperion's.

A tech with no `use_requires` may be cast by any unit whose command card carries the button,
which is then the gate: a mode rolls its casters as the game loads and cannot name them when the
mod is written.

### What a mode shows

`hx.hud` sets a value for a HUD design to draw, so only a design that names `mode.<key>` shows
it and a player on the classic console sees nothing of it. A mode's own parts are drawn over the
game whatever console it is played with, and its scripts set and clear them: a countdown or
timer, a banner with lines, and a counter. They are the GPU presenter's, as the rest of the
overlay is, and the end of this section says what that leaves out.

    hx.timer("next", {label = "mode/survival/hud/next_wave", ends = at, style = "both"})
    hx.counter("wave", {label = "mode/survival/hud/wave", value = n, of = hx.run.waves})
    hx.banner("cleared", {title = "mode/survival/hud/cleared", values = {n}, seconds = 4})
    hx.timer("next")          -- nothing in place of the table clears that part
    hx.counter("wave", n)     -- a number alone sets the count of one already there

Each call takes the part's own name first. Setting a part of the same name again replaces it,
which is how a part is updated, and a timer whose end falls on the frame the old one ends at
goes on counting rather than starting over, so a mode may set the same timer every tick. A mode
shows sixteen parts at once.

| Field | What it is |
|---|---|
| `label` | the words over a timer's clock or before a count; a language key, or its own text |
| `title` · `lines` | a banner's heading and the lines under it, eight at most. A line is a key or its text, or `{text = <key>, values = {...}}` for a line with places of its own |
| `values` | what the places in `label` or `title` take, in order: `%s` and `%d` as the mod's own text has them |
| `ends` | a timer's: the frame it reaches zero at, as `hx.frame() + hx.seconds(45)` gives one |
| `seconds` | a timer's span from now, in place of `ends`; a banner's is how long it is drawn, five without one |
| `up` | true for a timer that counts up from now rather than down |
| `full` | a timer's bar empties over this many frames, the span it was given without one |
| `value` · `of` | a counter's count, and what it is out of; `of` left out counts on its own |
| `shows` | the seconds a timer or a counter is drawn for before it goes by itself; without one it stays until the scripts clear it |
| `anchor` | where it sits: `top` (the default, `top_right` for a counter), `top_left`, `top_right`, `left`, `right`, `middle` |
| `style` | a timer: `clock` (the default), `bar` or `both`; a counter: `plain` (the default) or `bar`, which wants `of` |
| `tone` | `plain` (the default), `good`, `warn` or `bad`, which colours the count and the bar |

The words are the mode's language keys, so `mode/<mod>/hud/next_wave` in `lang/` is shown in the
player's language and a key no file has is drawn as it is, which lets a mod keep its English in
the scripts. Parts of one anchor stack under each other in the order they were set. There is no
bottom anchor: the console and a HUD design own the bottom of the screen, and `top_left` sits
under the message line, which the game draws there.

The parts are presentation and nothing else: the simulation never reads one, none of them is in
the save, the replay or what the other players are sent, and a game plays the same with them or
without. A replay and a watched seat draw them because the mode's own scripts run there and set
them again; a game that is loaded draws none until the scripts set them, which a mode's tick
does of itself. They are drawn over the game by the GPU presenter; under SDL's renderer, the
fallback where no graphics driver starts, Hyperion draws no overlay at all, so a mode's parts do
not show there and `hx.message` is what reaches that player.

### A mode in a game

The set-up's Game type list picks the mode, and its preset and settings are set with the game's
options; the command line takes them too (`engine/docs/hyperion-exe.md`):

    Hyperion.exe --map <file> --mode survival --mode-settings difficulty=insane --mode-seed 7

A game with a mode plays with the mode's own option (`HX_OPT_MODE`), and its replay and save hold
the mode's id, version, a fingerprint of the data it loaded, every setting's value and the seed
(the replay's `MODE` section, the save's `HXMD`). Both roll the run again from the seed, so a
replay plays and a save goes on as the game did. A run without the mode installed, or with
another version of it, refuses them and says which. A mode's data loads over the active mod's as
the game's tables are written, and both are put away when the game ends, as a push is, so the
menus and the next game have the data loaded at start-up.

`--mod-check <id>` of a mode also reads its block, rolls its `generate` on the first of the
install's maps for every choice of its `difficulty` and for each race, says what each roll
returned, and rolls one seed twice to hold the records to the byte.

    Hyperion.exe --mode-roll <id> [<out.json>] [--map <file>] [--mode-settings <...>]
                 [--mode-seed <n>] [--race <race>] [--players <n>]

rolls that mode's `generate` once and writes what it returned, so a tool can see the run a
seed gives before a game is played. It plays no game and opens no window, it reads the
game's files as a run does (`--install`, `--data`), and it may run while Hyperion is open:
its logs go to the data folder's `mode-roll\`. Without `--map` it rolls on the first of the
install's maps by name, as the check does; `--players` is how many seats play, the first a
person and the rest computers of `--race`, the last of them the mode's enemy base. The
object it writes to the file, or to standard output without one, names the mode, its
version, the map, the players, the race, the seed and every setting's value, and then
either `run` and `records`, what `generate` returned, or `error`, why it did not roll. It
ends with 0 when it rolled, 1 when it did not and 2 when no mode has the id.

## Cinematics

A mod replaces a cinematic with a file at the cinematic's path in its data folder:
`smk/Terran1.mkv` in place of the first Terran mission's. The names are the classic game's:
`smk/blizzard`, `smk/starintr`, `smk/starXintr`, `smk/PreTerran_1` to `_4`, `smk/Terran1` to
`4`, `smk/PreZerg_2` to `_4`, `smk/Zerg1` to `4`, `smk/PreProtoss_1` to `_3`, `smk/Protoss1`
to `4`, `smk/xProtoss`, `smk/xTerran` and `smk/xZerg`. It takes the first of `.webm`, `.mkv`,
`.mp4`, `.m4v`, `.mov` and `.smk` there, in the mod's folder first and then in those of the mods
it depends on. The video may be anything a portrait's may be (Sounds and portraits, above), at
any size; one the player's system cannot decode is passed over, and the cinematic plays as it
would without the mod.

With the GPU presenter it plays in full colour at its own depth, up to 16 bits, fitted to the
whole window with its shape kept, so a 4K video is shown at 4K on a 4K screen. A video 320
columns wide or less is shown twice as wide, and twice as tall where 480 rows fit that too, as
the classic player shows its own. On SDL's renderer it is put in 256 colours, fitted to the 640
by 480 screen.

The subtitles are Remastered's for the cinematic of that name, when Settings' Cinematic
subtitles is on. A mod whose video says other words puts its own beside it, as the same path
with `.srt` (`smk/Terran1.srt`), in UTF-8; an empty file shows none. Subtitles in another
language go beside it with the language's code, `smk/Terran1.deDE.srt` (Languages, below).

## Icons and wireframes

A unit's `icon` is its command icon (its button, the selection panel's queues) and its
`wireframe` and `transport_wireframe` are what the status panel and a transport's cargo show of
it. Each is a PNG or GRP file in the data folder, or a unit whose own it shows
(`"wireframe": "zerg_zergling"`). An edit's replace the original unit's.

    "trooper": {"base": "terran_marine", "icon": "art/trooper_icon.png",
                "wireframe": "art/trooper_wire.png", "transport_wireframe": "art/trooper_cargo.grp"}

- A PNG is drawn on a clear background: a pixel of alpha under 128 is clear. An icon takes the
  console's shade nearest each pixel's brightness, as its colour by the button's state; a
  wireframe takes its edge as the outline, in the shields' colours, and its inside as the four
  parts damage colours, a quarter each.
- A GRP's first frame is used as it is, its numbers the console's: an icon's shades 1 to 15
  (1 the brightest), a wireframe's 0xC0 and 0xC1 for the outline and 0xD0 to 0xD3 for the
  parts, as 1.17's `cmdicons.grp` and `wirefram.grp` have them.
- An icon is up to 36 by 34 pixels, a wireframe up to 64 by 64 and a transport wireframe up to
  32 by 32; a larger picture is drawn smaller to fit, its shape kept.
- On the GPU presenter the icon and the wireframe are drawn from the picture at the HD scale
  (2x, or 4x with `HYPERION_HD_SCALE=4`): the icon on the command card and in the selection
  panel's queues, the wireframe in the status panel, each at its size above times the scale.
  A PNG at that size (72 by 68 for an icon at 2x, 128 by 128 for a wireframe) keeps all its
  detail; a smaller one is scaled up. The colours are the console's as above, an icon's by
  its brightness and a wireframe's parts in their damage colours by theirs, the outline as
  deep as the scale. A GRP is drawn as its pixels, each the scale's size. The transport's and
  the group's small wireframes stay as they are drawn above. The live value `hd.mods` 0 draws
  them all as above.
- Without `transport_wireframe`, a transport shows the wireframe at half its size. The
  selection of several units always shows it at half its size, as Hyperion does a building's.
- A command card's button that trains the unit and gives no `icon` shows the unit's.
- The files are read as each game with the mod begins (`ArtBuild`, into the console's groups
  through `ModConsoleGroup`); one that does not read is named in the log and the unit keeps
  its looks'. Like sounds they are not in the fingerprint. Over Remastered's HD console they
  are drawn at the HD scale, tinted as its own icons are.

## Art sheets

An image's `grp` may be a picture of its frames, a sheet, in PNG, WebP or TGA. Hyperion makes
the frames from it as the mod loads.

    "trooper": {"base": "terran_marine", "grp": "art/trooper.png", "team": "art/trooper_team.png",
                "frame": [64, 64], "origin": [32, 42.5], "script": "trooper"}

- `frame`: a frame's width and height in game pixels. Frames run left to right, top to
  bottom; a script's frame numbers are row × 17 + direction, as a GRP's.
- `origin`: where the unit stands in a frame, from its top left; the centre without it.
- `directions`: 17, from up round the right to down, the left side drawn mirrored as
  classic's are; or 32, each its own frame, 32 to a row. A script counts 17 to a row either
  way, so one script serves both.
- `team`: a picture of the same size whose bright, opaque pixels take the player's colour.
- `frames`: the count, when the last row is not full.
- `scale`: the picture is this many times the game's size, 1 to 8; `"scale": 4` with a
  `frame` of 96 is cells of 384 pixels.
- A large sheet may be a list of files stacked top to bottom, `["art/a.png", "art/b.png"]`.
  Levels give one file or list for each scale: `{"1": "art/trooper.png", "4": "art/trooper_4x.png"}`.
- Each pixel takes the nearest of the colours every tileset has, and a team pixel the nearest
  step of the player's colour; alpha 128 or more is drawn. An image drawn as a shadow (draw
  function 10) takes every pixel with any alpha.
- A frame may be larger than 255 pixels a side. By Hyperion's rules sight, building sites and
  clicks use its whole size; classic rules hold them at 255.
- A picture is up to 16,384 pixels a side and 128 million pixels. Every file is in the
  fingerprint; a missing one leaves the image its base's frames and script.
- On the GPU presenter a sheet is drawn in full colour at the HD scale (2x, or 4x with
  `HYPERION_HD_SCALE=4`), from the smallest level at that scale or above, else the largest,
  whether or not Remastered's art is drawn. The team colour takes the place of the hue the team
  pixels were drawn in. Until it is ready, and on SDL's renderer, the image shows its classic
  frames. The live value `hd.mods` 0 shows the classic frames everywhere.

`engine/tools/control/directions.py` shows every direction of an image at once: it adds a
unit for each direction to the mod, places them in a ring on the canvas (wider than it is tall,
so it clears the console), and can make a test sheet whose frames carry their direction's
number.

## Art from StarCraft II

`engine/tools/hxunit.py` makes a unit's GRPs from the sheets the local SC2 sprite tool
renders from a player's own StarCraft II install (`tools/local/sc2-sprites`, never
committed): the unit, frame row * 17 + direction, in the palette indexes every tileset
shares with the team colour on 8 to 15; its shadow; and a first script to finish by hand.
They go in the mod's `data/art/`, which stays on the player's machine:

    python engine/tools/hxunit.py <render folder> engine/mods/hybrid-test/data hybrid_reaver --team-glow 80,0.4 --check

`--team-glow` makes a model's bright, saturated pixels the team colour when it has no
team-colour area of its own, as StarCraft II's hybrids have none.

## iscript.txt

The image scripts, in the text iscript editors write, with a script named instead of
numbered (`port_iscript.cpp`):

    # a comment
    .headerstart
    Name          hybrid_reaver
    Type          12
    Init          HybridReaverInit
    Death         HybridReaverDeath
    Walking       HybridReaverWalk
    .headerend

    HybridReaverInit:
            imgul         hybrid_reaver_shadow 0 0
            playfram      0x00
            goto          HybridReaverLocal

An image's `script` names the header's `Name`. An animation the header leaves out, or names
`[NONE]`, has none. An opcode's image, sprite or flingy argument is a number or the name of
one of the mod's records; `useweapon`'s weapon is a number or a weapon's name, the
original's (`gauss_rifle`) or the mod's, and a sound's (`playsnd`) a number or a sound's name.
The scripts are added after 1.17's, with ids from
1000, and may be of any size: a jump, `call` and `useweapon` are written in wider forms of
Hyperion's own, so a mod's scripts go past the 64 KiB 1.17's offsets reach and name weapons
past 255. 1.17's own scripts keep their bytes and offsets.

## hud.json

The data folder, or the mod's own folder when it names none, may hold `hud.json`, the look of
the console and what is drawn in it, read again as each game's console loads; a mod that only
changes these needs no `types.json`. Every section and key may be left out, and what is left
out or wrong stays as 1.17 has it (`hyperion_mods.log` says what was wrong). All of it is
presentation: neither a replay nor the game's state sees it.

### The console

`"console"` is the console's layout (`engine/src117/port/render/port_console.cpp`):

    {
      "console": {
        "art": {"terran": "art/console_terran.pcx", "replay": "art/console_replay.pcx"},
        "toEdges": true,
        "fullMap": false
      }
    }

- `art`: a console image of the mod's own for `zerg`, `terran`, `protoss` or `replay`, in place
  of 1.17's `game\<race>console.pcx`: a PCX of 640 by 480 at 8 bits a pixel, its numbers the
  game's palette's. Its empty pixels (0) are where the map shows and a click is the map's, so
  the image is the console's shape too. It shows with the HD art on as well, whole in the
  middle of a wide view: Remastered's HD console is cut for 1.17's images.
- `toEdges`: false keeps Remastered's HD console whole in the middle of a wide view, where it
  otherwise moves its left part toward the screen's left edge and its right part toward the
  right edge, the dialogs with them.
- `fullMap`: true draws the map to the screen's foot at 640 wide too, as a wider view always
  does; 1.17 draws its 400 rows, so a hole in an image below them shows black.

The dialogs keep the places 1.17's dialog files give them, and draw over the image there.

### The minimap

`"minimap"` is the minimap's look (`engine/src117/port/render/port_minimap.cpp`): the units'
dots in a colour of the mod's, a palette number, by their owner's relation to the player:

    {
      "minimap": {
        "dots": {"own": 117, "allied": 135, "enemy": 111, "neutral": 81}
      }
    }

A relation left out keeps the colour the game gives it, Settings' team or ally colours
included. In a replay, which has no player of its own, the dots keep theirs. The dots' places
and how many there are stay the game's.

### The selection

`"selection"` is the selection's look (`engine/src117/port/render/port_selection.cpp`): whether
the circles under the units the player selected, the circles under a teammate's selection and
the health bars over selected units are drawn:

    {
      "selection": {"circles": true, "teamCircles": true, "healthBars": true}
    }

A mark left undrawn is still there for the game: what is selected, and what a command does to
it, stay the game's.

## readout.json

The same folder may hold `readout.json`, the one unit's readout: the panel that shows the
selected unit's wireframe, name, status line, kills, icons, life and energy. The template is
classic's own panel, and a mod changes the parameters it wants and leaves the rest. With no
`readout.json` the panel is classic's, pixel for pixel. A game mode's `readout.json` is the game's
when it has one; otherwise the content mod's is.

One readout serves both consoles. It is drawn into the panel's own picture, which the classic
console puts on the screen where it always does and a HUD design's `selection` slot shows as its
pixels, so a mod's readout shows whichever console the player chose. Like `hud.json` it is
presentation only: outside the loader and the fingerprint, in no replay and no save, and the
same values for a player and for anyone watching.

    {
      "readout": {
        "from": "classic",
        "in_huds": true,
        "parts": {
          "name": {"text": "{name}", "colour": "rank"},
          "status": {"lines": "classic then mod", "rotate": 2.5, "rank_pips": true},
          "slots": {"delta": "on the icon"}
        },
        "add": {
          "lines": {"each": "{name} {left}s {effects}", "bar": true, "in_huds": "a line each"}
        },
        "ranks": {"unique": "#4fd2c2", "elite": "#f2c94c", "midboss": "#ff9a3c", "boss": "#ff5a4a"},
        "fx": [
          {"on": "damaged", "part": "wire", "do": "flash", "colour": "#ffffff", "time": 0.18},
          {"while": "life < 0.33", "part": "wire", "do": "beat", "colour": "#ff5a3a", "rate": 1.2}
        ]
      }
    }

`from` names the template, and `classic` is the only one. `in_huds` false keeps the readout to
the classic console, a HUD design's slot showing classic's panel instead.

### The parts

The seven parts are classic's own, in the order it draws them, and each is the rectangle of the
control that draws it (`rez\statdata.bin`, so a part lands where the console's art has a window
for it). The readout's own pixels are that dialog's: 270 by 92, at (138, 388) on the 640 by 480
console.

| Part | Where | What it is |
|---|---|---|
| `wire` | (30, 0) 64 × 64 | the wireframe, tinted part by part as the unit is hurt |
| `name` | (102, 0) 152 × 14 | the unit's name |
| `status` | (106, 16) 144 × 14 | classic's one line: a hallucination, disabled, acid spores, blind, a parasite, or the rank |
| `kills` | (106, 30) 144 × 14 | "Kills: n", for a unit that can attack or has killed |
| `slots` | (104, 51) four of 36 × 36, 39 apart | armour, shields, the ground and air weapons, then interceptors, mines or a nuke, each with its upgrade level |
| `life` | (22, 64) 80 × 12 | shields and life as now/most |
| `energy` | (22, 76) 80 × 12 | energy as now/most, or a hallucination's time left |

Each part's parameters, classic's value first:

- `wire`: `tint` (`life`, `rank`, `team`, `none`; life is classic's, green then yellow and red),
  `states` (the two percentages of life where it turns, the higher first, `[66, 33]`), `frame`
  (`none`, `rank`, `corners`), `frame_colour`, `art` (`unit`, or `mod` for the mod's own
  wireframes).
- `name`: `text`, `colour` (`classic`, `rank`, `team` or `#rrggbb`), `align` (`centre`, `left`).
- `status`: `lines` (`classic`, `classic then mod`, `mod`; classic's own statuses keep their
  place first, so a parasite is never hidden by a mod's line), `rotate` (with more than one line,
  how long each shows, in seconds), `rank_pips` (the rank as pips beside the line, and in the
  group view). A status that lands on the unit, or a kill, takes the place at once and the turns
  carry on from it, so a status of a few seconds is read rather than waiting out the lines before
  it. Where the readout adds lines (`add`), a status's own line is drawn there and not here, so
  the statuses are not in both: this line then carries what the hook wrote and the unit's rank.
- `kills`: `show` (`auto`, `always`, `never`), `text`.
- `slots`: `order` (the icons, from `armor`, `shields`, `weapon_ground`, `weapon_air`, `special`
  and a mod's own names; a mod may add its own after these, as many as the room takes), `level`,
  `delta` (`off`, `on the icon`, `in the tooltip`: what a rolled change shows; on the icon it is
  a tab in its top left corner, a chevron for the way the figure went and how far it moved,
  green where the unit gains by it and red where it loses, a faster cooldown counting as a gain
  though its number falls), `tooltip` (a line of text, or a list of them).

  The panel has four icons and they are the armour, the shields and the two weapons, so three of
  the six stats `deltas` may name have no icon of their own. Two of them get a tab all the same,
  in the same form: a weapon's icon carries two figures, its **damage** and that weapon's
  **cooldown**, and the **life** takes a tab at the right end of its own row, where classic
  centres its figures and leaves the ends clear. The **speed** has no tab, there being no part of
  the panel that is the speed's.

  All six are read in tooltips as well: the weapon's icon carries its cooldown in frames, the row
  it was rolled from, the change and what the statuses on the unit do to it; the **wireframe**
  carries the life and its change; and the **name** carries what the unit was rolled from, what
  it is, and every change it rolled one to a line, the speed among them. The hook's `tooltips`
  for those three stats feed those places, as the icons' feed theirs. Classic shows nothing for
  the wireframe or the name, so a mod that gives no readout changes nothing.
- `life`: `text`, `colour` (`damage`, `life`, `classic`; damage is classic's, yellow while it
  smoulders and red while it burns).
- `energy`: `text`, `show` (`classic`, `every caster`, `never`; classic shows it for your own
  casters only).

A colour is `#rrggbb` or one of the names its parameter takes. The panel's picture is
palettized, so an `#rrggbb` is drawn as the nearest entry of the game's palette.

Text is a pattern: a value in braces, and anything else as written. The values are `{name}`,
`{rank}`, `{kills}`, `{life}`, `{life_max}`, `{shields}`, `{shields_max}`, `{energy}`,
`{energy_max}`, `{armor}`, `{level}`, `{stat}`, `{value}` and `{title}`, and on a line that names
a status `{left}`, `{stacks}` and `{effects}`. On such a line `{name}` is the status's own name,
not the unit's, since the line is the status's.

### What a mod adds

`add.lines` is the lines a mod adds past classic's parts: `each` (the pattern for one line),
`each_short` (the same line where the room is short),
`bar` (a bar for a status's time left) and `in_huds` (`a line each`, or `in turn`). A line of
classic's own rows is 144 pixels, about 28 of its letters, so a line that names a status and
everything it does will clip there while the same line reads whole in a HUD design's room:
`each_short` is for the name and the time alone (`{name} {left}s`, since `{left}` is the bare
number), and is used wherever the readout has no room wider than classic's panel. With none the
line reads as `{name} {left}s` from the status's own words, which carry its stacks as well and
leave the time out altogether for a status that has none, so it can never read "Spored s";
whatever still does not fit is cut on a word's boundary rather than through a number. On the
classic console they take the kills' line in turn with it, since that is the room classic leaves
free; a HUD slot taller than 92 gives the readout spare rows under the classic parts, and there
each line can have its own.

A slot gives the readout that room beside the panel where it has 110 spare columns, since a
narrower column holds no line worth reading, and under the panel where it has 18 spare rows
instead. A slot with neither, and the classic console, give none.

In that room the lines read from the top down: a row for each status first, with its icon, its
words, its time left as a bar and the same time as an arc round the icon, since those are what
changes while the unit is watched; then the hook's own lines in the order it wrote them; then
what the unit rolled where the hook gave `deltas` -- each stat named with its change, green the
way the unit gains and red the way it loses. The rolled changes are named only there: on the
console the tabs carry them, in the icons' own corners and on the life row.

`in_huds` decides where the statuses are read, and they are never in two places. With `a line
each` the room lists them and the panel's kills row keeps to the kills; with `in turn` the room
keeps to its other lines and the kills row rotates the statuses as it does on the console. The
room's rows read `each`, or `each_short` where the room is narrower than 120 pixels, through the
same values as the panel's row, and a status takes its `look`'s own hue there where it has one.

`ranks` names each rank the hook may give a unit and the colour it is marked in, which is where
`"colour": "rank"` and `"while": "rank >= elite"` read from. A rank is a colour, or an object with
the colour and the words a player reads:

    "ranks": {"unique": "#4fd2c2", "elite": {"colour": "#f2c94c", "name": "Elite"}}

The order they are written in is the order they rank. That matters because the classic panel is
palettized and draws a colour as the nearest entry of the game's palette, where two ranks may land
on one; what tells them apart there is the rank's place, which the group view's pip shows by its
size.

### How a status shows itself

Every status falls into one of fourteen kinds, worked out from its own fields, the strongest
first: what stops the unit, then what hurts it, then the rest. Each kind carries the glyph drawn
for it, its own hue, the pattern it lays over a wireframe, and the classic command icon that says
the same thing, so a mod's status draws with an icon from the start and needs no art of its own.

| kind | what it is | from | the classic icon it stands for |
|---|---|---|---|
| `root` | can't move | `cannot_move` | Lockdown |
| `disarm` | can't attack | `cannot_attack` | Disruption Web |
| `dot` | damage over time | `damage_per_second` | Plague |
| `heal` | heals | `life_per_second` | Healing |
| `armor_down` | less armour | `armor` below 0 | Corrosive Acid |
| `armor_up` | more armour | `armor` above 0 | Infantry Armor |
| `haste` | attacks faster | `attack_cooldown` below 0 | Stim Packs |
| `jam` | attacks slower | `attack_cooldown` above 0 | EMP Shockwave |
| `slow` | slower | `move_speed` below 0 | Ensnare |
| `swift` | faster | `move_speed` above 0 | Metabolic Boost |
| `might` | deals more | `damage_dealt` above 0 | Infantry Weapons |
| `weak` | deals less | `damage_dealt` below 0 | Optical Flare |
| `frail` | takes more | `damage_taken` above 0 | Irradiate |
| `tough` | takes less | `damage_taken` below 0 | Defensive Matrix |

A status whose fields pull both ways takes the first kind it matches, so a status that is both
`+armour` and `cannot_move` is a root; a status whose `look` names a kind takes that one instead.

`status_look` says where and how a status shows:

    "status_look": {"icon": "framed", "line3": "in turn", "wire_mark": "corner", "group_mark": "glyphs"}

- `icon`: `framed` (the glyph in a frame: a disc for a status that helps the unit, a diamond for
  one that hurts, the time left running out round it and the stacks in a corner), `glyph` (the
  glyph alone, at the whole box's size), `game` (the status's own command icon), `none`. A frame
  is near-black whichever side it is, and its edge and its glyph take the colour the status named
  for itself -- a `hue` of its own `look` -- or its side's green or red where it named none, since
  `for` and `against` name a side rather than a colour. A kind's own hue is not drawn on an icon,
  the side being what an icon says first; it goes on the pattern and the tile's edge mark.
- `line3`: `in turn`, the statuses taking the third line in turn with the kills, which is what
  fits the classic panel; or `a row`, a line each where the room allows.
- `wire_mark`: `corner` (the most telling status's icon in the wireframe's top left, the next
  smaller under it, then "+n"), `tint`, `pattern` (the kind's own over the wireframe's pixels),
  `none`.
- `group_mark`: `glyphs` (up to three down a group tile's left), `dots`, `edge`, `none`.

Those four values are the defaults, so a file that leaves the block out gets them.

### The hook

A script of the mod may give `readout(unit)`: what the readout shows of a unit, or nil for
classic's own panel. It is called for a unit the viewer sees, when the selection changes and when
the unit's own values do, not every frame; it reads the game and never sets it, so what it
returns can differ between viewers without the game going its own way.

    function readout(unit)
      local r = rolled(unit)
      if r == nil then return nil end
      local out = {
        name = r.name,
        rank = r.rank,
        lines = { r.rankName .. " " .. r.archetype },
        deltas = { armor = unit.armor - unit.base.armor, damage = r.damage - 100 },
        tooltips = { life = lifeSources(r) },
      }
      for _, s in ipairs(hx.statuses(unit)) do
        table.insert(out.lines, { status = s.id })
      end
      return out
    end

- `name` and `rank`: the name the readout shows, and a rank of `ranks`, which gives its colour.
- `lines`: a line each, as text, as `{text = ..., colour = ...}`, or as `{status = "<id>"}`. A
  line that names a status takes the status's name, its side, its time left and its stacks from
  the status's own definition, and its timer runs between calls, so a status counting down costs
  no call.
- `deltas`: the change against the unit's base row, by stat (`armor`, `shields`, `damage`,
  `cooldown`, `life`, `speed`), which the slots' `delta` shows.
- `tooltips`: the lines of a stat's tooltip, by stat.

A key of the answer that nothing reads is said once a game in `hyperion_mods.log`, with where it
was (`readout(unit).badges`, `readout(unit).tooltips.armour`), as a key of the file itself is said
as the file is read. The answer is a table, so a misspelled key would otherwise be accepted and
do nothing, which reads exactly like one that is working. The keys read are the five above, a
line's `text`, `colour` and `status`, and, in `deltas` and `tooltips`, the six stats' own names.

### The effects

`fx` is a list of effects, each one op on one part, set off by an event or held while a test is
true. Every one is carried by light: no part shakes, bounces or changes size.

The events are `selected`, `damaged`, `healed`, `status_added`, `stat_changed` and `kill`, raised
for the readout on the game clock. An effect takes `on` (one event or a list of them) or `while`
(`"life < 0.33"`, `"rank >= elite"` or `"status.left < 2"`), never both.

The ops are `flash` (the part flashes a colour), `float` (text rises off the part and fades),
`roll` (the numbers count to their new value), `slide` (a line slides in), `light` (the part
lights up, with `glint`), `blink` (at a `rate`), `scan` (a line runs down the part), `type` (text
types itself in), `beat` (the part beats in light), `glow` (at a `size` and a `rate`) and
`shimmer` (light runs across text `every` so often). The rest of an effect's keys are `part`
(a part's name, `lines` wherever a status's line is drawn, or `slots.<stat>` for the icon of the
stat the event names), `if` (`rank` draws it only for a unit the hook gave a rank), `colour`,
`text`, `time`, `hold`, `rate`, `every`, `size`, `light`, `glint`, `glow` and `from` (`slide`'s
side). `lines` is the rows in a HUD's room, and on the panel the row that is showing the line,
which is the kills' row where the readout `add`s lines and the status's own row where it does
not. An effect on `lines` that came from a status reaches that status's line alone: a `slide`
runs on the row of the status that landed, and a `blink` dims the row of a status its own test is
holding for, never a row beside it.

The panel's words are classic's own, in its font and its palette, so most ops reach a part as
light laid over it rather than as a change to the words: a `blink` lights the line on its beat
instead of putting it out, a `type` fills the line with light as the words would land, and a
`slide` on the panel is a band of light in from its side -- in a HUD's room the status's own row
slides with it. A `float` moves its own text, which rises off the part and fades.

A `roll` is the one op that writes words: while it runs, the readout writes the line it is on
(`life` or `energy`) with the figures counted from what the line said before to what it says now,
in classic's own format and colours where the mod leaves that line classic's, and in the mod's
own pattern where it gives one. When the run is over the line is classic's again. The life line
carries the shields as well, so a hit that takes only shields counts that half. A roll moves no
text, so it is one of the ops a player who asked for no motion still sees.

A script may set an effect off itself, at a moment the engine has no event for:
`hx.readout.fx(unit, "flash", {part = "name", colour = "rank", time = 0.25})`. The same ops and
the same caps, and it draws for that viewer only.

Two things a mod cannot override. The player's **Reduce motion** leaves out the ops that move
text (`float`, `slide`, `scan`, `type`, `shimmer`) and halves a flash; and a blink is capped at
three a second whichever way the file sets it. An op left out keeps the light it carries: a
`slide` with `glow`, or any motion op with `glint`, runs that light on its part in the motion's
place, so an effect written with light beside its movement still says what it was for. An effect
that is the only thing announcing something and carries no light -- a bare `slide` on
`status_added` -- says nothing for that player, so write a light effect (`flash`, `light`,
`glow` or `beat`) on the same event beside it. A `while` effect takes the same rule as the rest,
so a `shimmer` held while a unit is of a rank does not run for a player who asked for no motion.

Every key may be left out. A key that is wrong leaves its parameter classic's and is named in
`hyperion_mods.log` with the line it was on, so a mod with one key wrong loses that key and not
its readout; a whole file that does not read leaves the panel classic's.

## looks.json

The same folder may hold `looks.json`, read as each game begins: looks, which draw a unit's
images in other colours and, in full colour, as a crystal or as gas, with a light on the ground
and a plume. Each look in the file changes the built-in look of its name
(`rich-gold`, `rich-violet`) or is added, and its `units` are the unit types drawn with it, by number or by name (a mod's unit by its
name, since its number moves when a record that sorts before it is added). Like
`hud.json` it is presentation only, outside the loader and its fingerprint. The fields and an
example are in `engine/docs/modules/render.md`, "Looks"; the live channel's `look` takes the
same fields and `looks_save` writes the file.

A look may also take its colours in up to four zones, each by its own rule (`none` keeps the
image's colours, and there are `reds`, `golds`, `greys`, `skins`, a band of `hue` and a list of
`entries`) and with its own material (`paint` lays the ramp on at the tone the art is shaded,
`glow` adds it as light), and it may carry an aura, a line of light round the unit's outline with
a glow past it (`engine/docs/modules/design.md`, "Looks in depth"). A unit draws the auras of up
to three of the looks that name its type, so three looks that each bring one all show. An aura is
drawn by the GPU presenter's scene.

## Languages

A mod's text can be given in other languages: a file for each in the mod's `lang/` folder, named
by the language's code as the Language setting writes it (`enUS`, `deDE`, `esES`, `esMX`, `frFR`,
`itIT`, `jaJP`, `koKR`, `plPL`, `ptBR`, `ruRU`, `zhCN`, `zhTW`), so `lang/deDE.json`. It maps
each of the mod's strings, by its key, to its text in that language:

    {
      "language": "deDE",
      "strings": {
        "unit/iron_trooper": {"text": "Eisensoldat", "source": "Iron Trooper"},
        "card/iron_forge/1/train/iron_trooper/text": {"text": "Eisen<s>oldat ausbilden", "source": "Train Iron <T>rooper"},
        "race/iron_legion": "Eiserne Legion"
      }
    }

| key | the string |
|---|---|
| `mod/name`, `mod/line` | mod.json's name and line, as the Mods screen shows them |
| `race/<race>` | a race's name (its record's name when it gives none) |
| `unit/<record>` | a unit's name: a mod's unit, or an edit that gives one (an edit without a name shows the game's own, which the game translates) |
| `weapon/<record>`, `tech/<record>`, `upgrade/<record>` | the `name` a weapon, a tech or an upgrade gives (one without a name shows the game's string, which the game translates) |
| `sound/<record>` | a sound's `subtitle`, the words of a voice line |
| `mode/<mod>/line`, `/about`, `/note/<n>` | a mode's own words (A game mode, above): its line, its paragraph and each note by its place |
| `mode/<mod>/game_type/name`, `/tag` | what the Game type list calls it, and the mod its row names |
| `mode/<mod>/setting/<id>/name`, `/group`, `/gist` | a setting's words |
| `mode/<mod>/setting/<id>/choice/<id>` · `/item/<id>/name`, `/line` | a choice's name, and a list item's name and line |
| `mode/<mod>/preset/<id>/name`, `/line` | a preset's words |
| `card/<card>/<position>/<what>/<target>/text` | a card button's text; `.../disabled` its text while it cannot. `<what>` is `train`, `build`, `cast`, `research` or `upgrade` and `<target>` what the button names; for an entry with `classic` it is `classic` and what `classic` names, or `first` for `true` |

Each string is its text, or an object with its `text` and the mod's text it was made from,
`source`, which Hyperion Studio uses to mark a translation stale once the mod's text changes;
the game shows a stale one all the same. A game shows the player's language's text (Settings'
Language, or `HYPERION_LANGUAGE` in a test) wherever the mod's own would be: the unit's name in
the status panel and its tooltips, a button's words, a race's name in the set-up and the mods
list's name and line. A string the file does not give stays the mod's own. A button's text that
marks a letter (`<s>`) takes that letter as its hotkey in the language; one that marks none
keeps the mod's hotkey. A button with no text of its own shows the name of the unit it trains,
so it follows that unit's translation.

mod.json's `"language"` names the language the mod's own text is in, English without one; a
file for that language is not read. A mod that depends on others may translate their strings
too, by the same keys, in its own files: its translation takes the place of theirs, and theirs
of the mod's text where it gives none. `--race` takes a race's own name as well as its
translation.

The languages change only what is shown: the fingerprint is of `types.json` as it is, so games,
replays and saves agree whatever language each player sees.

A mod's own cinematic may have subtitles in each language: `smk/Terran1.deDE.srt` beside
`smk/Terran1.srt` is shown in German, the other in any language without its own file.

A mode's scripts say their own lines and HUD values by key (`hx.message`, `hx.hud`), so any key
under `mode/<mod>/` is the mode's: the player's language's text is shown, then the mod's own, and
then the key itself, which lets a mod keep its English in the scripts and translate it later.

`--mod-check` warns of a translation whose key names no string of the mod or the mods it builds
on, of a file in `lang/` that is named by no language or does not read, and lists each language
in a note: how many of the mod's strings it translates, those missing (shown in the mod's own
text) and those made from a text the mod has changed since.

## In a game

- A game with a mod's data plays by Hyperion's rules. Its replay and save
  record the mod's id, a fingerprint of its data and that of the mods it depends on
  (`types.json`, `iscript.txt` and the GRPs, SHA-1, in load order; not the sounds, videos,
  icons and wireframes, which are heard and seen only), and those mods' ids and versions. A run without the mod refuses them ("the mod hybrid-test's data, which this
  run did not load (--mod hybrid-test)"), as does a run with another version of it; where
  the mods under it differ, the refusal names both ("the mod my-mod with hybrid-test 0.2,
  where this run has hybrid-test 0.3"), and the replays screen shows them beside the mod.
- A game without the mod has the original's rows only, whatever mod the run loaded.
- `.hxm` maps place a mod's unit by its record's name (`"type": "hybrid_reaver"`); such a
  map loads only with the mod. `tools/hxmap/hybridmap.py` makes the test map.
- The live channel's `spawn` takes a mod's unit by its number or its record's name.
- A game with a mode records the mode too (A game mode, above), and `h2_lt_survival` plays
  one.
- The replay test's `h3_hybrid` (`engine/tools/replaytest/hxsuite.json`) plays a game with
  the test mod: its `"mod"` field copies the mod into the test install and passes `--mod`
  to the game, its replay and its save's load. `h5_lt_hybrid_race` plays its Hybrid race
  on Lost Temple against a computer.

## Pushed while a game runs

In a Debug Tools canvas game a mod's data can be sent again while the game runs, to try a
change without restarting (the PRD's live mod editing; 2026-10-15). The live
channel's `mod_push` takes `mod`, one of the mods that bring data (`mods` lists them), and
sends its data folder's `types.json`, `iscript.txt` and art; or `types` (the object, or its
text) and `iscript` given in the message, with the art still from the mod's folder or the
`folder` given. Debug Tools' panel has a Mod data section that does either, the text edited
in the page.

- Each push is a submission. Its records take the next ids of their tables after every
  earlier submission's, so a unit on the map keeps the rows it was made from; a name (the
  channel's `spawn`, a command card's button, another record's field) finds the newest.
  A push resets the mod's techs and upgrades for every player.
- `replace` turns units of earlier submissions into the newest record of their name, as a
  morph turns a unit (`CUnit::TransformType`): `"last"` the submission before, `"all"` every
  earlier one, or a submission's number. Their hit points keep their share of the most.
- A push that does not load (a field no table has, a name nothing has, a full table) is
  refused with the reason, and everything stays as it was.
- The answer names the submission, its units' names and ids, and how many records of each
  table it brought; `mod_submissions` lists the game's pushes, and `units` gives each unit's
  submission.
- A push marks the game as changed, and its data is put away when the game ends, so the menus
  and the next game have the data loaded at start-up: what pushes brought never reaches another
  game, a replay or a save. A push that brings races keeps each player's race by its name. The scripts' ids go on
  from the last pushed, as the image scripts' table keeps every script appended.
- The canvas need not have a mod with data: the first push gives it the mod's types.

## Checking a mod

    Hyperion.exe --mod-check <id> [<report.txt>]

loads the mod's data as start-up does, with the mods it depends on, then reads each file its
records name as a game would: the sounds, the portraits' and the cinematics' videos (the first
frame decoded), the sheets' pictures at every level, the icons, wireframes and GRPs. It plays
no game and opens no window. It reads the game's files as a run does (`--install`, `--data`),
and it may run while Hyperion is open: its logs go to the data folder's `mod-check\`.

Each problem is a line, to the report or to standard output without one:

- `error:` what stops the load, with the message start-up shows, or a file that does not
  decode: a sound that is no WAV, Ogg, MP3 or FLAC, a video Hyperion does not read, a picture
  cut short, a GRP whose frames are past its end.
- `warning:` what a game passes over: a name a field gives that nothing has, where the load
  goes on without it (a unit's `looks`); a file a record names that the folder does not have;
  a file passed over for another of the same name (`.webm` is tried before `.mkv`, `.mp4`,
  `.m4v`, `.mov` and `.smk`); a file in `smk/` that is no cinematic's name; subtitles with no
  cue or no video beside them; a video this system does not decode (H.265 without Microsoft's
  HEVC Video Extensions, say). Then, once each, the videos that play through a system's own
  decoders (H.264, H.265, AAC) and those the browser's build does not play (any video but
  Smacker, and Ogg sounds).
- `note:` what loaded: the mod, the records each table gained (their ids, and the range the
  build holds) and edited, and each video's codec, size, depth and frames.

The last line is `plays: 0 errors, 2 warnings`, `plays with errors: ...`, `refused: its data
does not load; ...` or `refused: no such mod`, which lists the mods there are and the folders
left out with why. The exit code is 0 when there is no error, 1 when there is one, and 2 when
no mod has the id.
