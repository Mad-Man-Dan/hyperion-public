# Survival

The first game-mode mod: `mod.json` says `"kind": "mode"`, so the skirmish set-up lists it in the
Game Mode tab beside the map. One home inside a perimeter read from the map, a wave every gap
until the count runs out, then the perimeter lifts and the enemy base is yours to take.

Nothing here is Survival's alone: every record, pool and script is something another mode mod
could write. The enemies are rolled from the run's seed, so no two runs send the same army.

## The files

| Path | What |
|---|---|
| `mod.json` | the mod, and the `mode` block: seven settings a player sets, one a tool does (`start_wave`), two presets, what it needs, one to four players |
| `types.json` | the three race libraries (spells, statuses), the seven elite bodies a run rolls its elites onto, the two units the spells make, and the hooks `generate`, `game.start`, `game.tick` and `game.on_death` |
| `looks.json` | the effects: 12 looks a race and the Hybrid Reaver's own, some a repaint and some an aura alone |
| `readout.json` | the one unit's panel: classic's, with the name in the rank's colour, the rank's pips, a line for each status with its time left, the rolled change on each icon, and the effects |
| `lang/enUS.json` | every name and line a player reads, 220 strings, the source list a translation is made from |
| `scripts/generate.luau` | the whole run rolled from the seed: the waves, the perimeter, the generated kinds and their spells |
| `scripts/pools/tiers.luau` | the run's shape: the tiers a wave draws from, the bounds a rolled unit's own numbers fall between, and the ranks from a line unit up to a boss |
| `scripts/pools/terran.luau`, `zerg.luau`, `protoss.luau` | the archetypes and affixes each race draws on, with their weights, tags, costs and looks |
| `scripts/pools/spellparts.luau` | the carriers, payloads and modifiers a generated spell is built from, the name parts, and the fairness rules |
| `scripts/mode.luau` | the mode in a game: the waves sent, the perimeter held, the HUD, the ending |
| `scripts/spells.luau` | what the action blocks cannot say, as Luau: the cone, the pull, the chain, the split and the rest |
| `scripts/rolled.luau` | what the unit in front of you rolled, looked up by its type: the run's own notes, read only |
| `scripts/readout.luau` | `readout(unit)`: what the panel shows of it -- the name, the rank, the lines, the changes and where each number came from |
| `scripts/settings.luau` | what the Game Mode tab says under each setting, what the map read gives for a choice, and which settings the tab does not show |
| `scripts/ai/lib.luau` | threat, cluster, weakest, nearest, retreat, over `hx.units` and `hx.distance` |
| `scripts/ai/decide.luau` | the three decisions every cast makes, and the generated spells' own finder |
| `scripts/ai/casts_terran.luau`, `casts_zerg.luau`, `casts_protoss.luau` | one function a spell: when to cast it and at what |
| `scripts/ai/brains.luau` | the brains by role: bruiser, ranged, caster, support, summoner, assassin, siege |
| `scripts/ai/bosses.luau` | the mid-boss and boss brains, their phases, summons, enrage and arena |
| `API-NEEDS.md` | every call, field and hook used that `docs/mod-data.md` does not have yet |

## What is in it

| | Terran | Zerg | Protoss | Shared |
|---|---|---|---|---|
| Spells | 12 | 12 | 12 | |
| Statuses | 12 | 12 | 12 | 6 (what a unit is born with) |
| Effects (looks) | 12 | 12 | 12 | 1 (the Hybrid Reaver's) |
| Affixes | 12 | 12 | 12 | |
| Archetypes | 12 | 12 | 12 | |
| Spell carriers | 1 | 1 | 1 | 8 |
| Spell payloads | 1 | 1 | 1 | 10 |
| Spell modifiers | 1 | 1 | 1 | 10 |

Beside them: seven elite bodies a run rolls onto, two units the spells make, and 37 weapons, 36 techs and 36
orders carrying the spells.

## The spells

Each is Psionic Storm's order with a weapon and tech of its own and `blocks` in place of firing.
The energy is the tech's.

| Terran | Energy | Zerg | Energy | Protoss | Energy |
|---|---|---|---|---|---|
| Scorch Round | 50 | Acid Bloom | 55 | Mind Lash | 50 |
| Null Pulse | 70 | Bile Spray | 60 | Void Rend | 75 |
| Sentry Drone | 60 | Spore Cloud | 50 | Psi Nova | 70 |
| Barrage Call | 75 | Root Snare | 45 | Warp Step | 40 |
| Bulwark Field | 60 | Spawn Brood | 70 | Summon Echo | 65 |
| Flare Mark | 40 | Infest Husk | 75 | Shield Weave | 55 |
| Thermite Spray | 45 | Carapace Knit | 50 | Khala Surge | 60 |
| Repair Swarm | 55 | Molt | 40 | Time Dilation | 65 |
| Shock Clamp | 50 | Frenzy Pheromone | 55 | Chrono Rift | 70 |
| Overclock | 40 | Devour | 45 | Null Field | 60 |
| Smoke Veil | 50 | Burrow Strike | 50 | Phase Veil | 45 |
| Siege Anchor | 35 | Hive Mind | 60 | Void Tether | 55 |

Six of them need a Luau function for what the blocks cannot say: Null Pulse (shields to zero),
Flare Mark (the ground shown), Thermite Spray (a cone), Infest Husk (a kill turned into a husk),
Devour (the caster fed), Shield Weave (shields back) and Void Tether (the pull).

## The statuses

Seconds at Fastest; speed, cooldown and damage are percentages; armour is points.

| Terran | What it does | Zerg | What it does | Protoss | What it does |
|---|---|---|---|---|---|
| Scorched | 4s, 10 life a second | Corroded | 8s, -4 armour, 4 deep | Mind-burned | 6s, 8 life a second |
| Slagged | 6s, -35 speed, 4 a second | Bile rot | 8s, 7 a second, 3 deep | Rent | 8s, -3 armour, -20 damage |
| Jammed | 5s, +60 cooldown | Spored | 7s, -30 speed, -2 armour | Dilated | 6s, -45 speed, +50 cooldown |
| Blinded | 3s, cannot attack | Hobbled | 5s, -55 speed | Rifted | 2s, cannot move |
| Clamped | 3s, cannot move | Rooted | 2.5s, cannot move | Tethered | 4s, -70 speed |
| Marked | 8s, -3 armour, 3 deep | Brood-touched | 10s, -15 speed, 3 a second | Nulled | 4s, cannot attack |
| Braced | 8s, +3 armour | Knit | 8s, +10 life a second | Void-marked | 10s, 5 a second, -2 armour |
| Veiled | 6s, +2 armour, +15 speed | Hardened carapace | 10s, +4 armour | Shield-woven | 10s, +3 armour, +6 a second |
| Mending | 6s, +8 life a second | Frenzied | 8s, -40 cooldown, +25 speed | Phased | 6s, +35 speed |
| Overclocked | 10s, -35 cooldown, +20 damage | Gorged | 12s, +25 damage, +5 a second | Khala-lit | 8s, +20 damage, +4 a second |
| Anchored | 8s, cannot move, +50 damage | Hive-linked | 6s, +15 damage, +1 armour | Warp lag | 5s, +45 cooldown |
| Drone-warded | 12s, +2 armour, +3 a second | Molting | 3s, +30 a second, cannot attack | Echo-bound | 8s, -30 damage |

Six more are nobody's spell: they are what a unit may be born with, in pairs, one for the unit
and one against it. They carry no overlay and no icon, so a marked unit still looks like the rest
of its wave, and they run for as long as a status may, which makes them its life.

| For it | | Against it | |
|---|---|---|---|
| Tough | +1 armour, -6 damage taken | Frail | +8 damage taken |
| Quick | +10 speed, -6 cooldown | Slow | -8 speed |
| Savage | +10 damage | Blunt | -8 damage |

## The effects

`looks.json`. A look with a ramp repaints the unit; one with the rule `none` is an aura alone, so
it goes over whatever paint the unit already has.

| Terran | Zerg | Protoss |
|---|---|---|
| `ls-t-ember` burning, embers | `ls-z-acid` wet green, running | `ls-p-psi` psionic gold |
| `ls-t-slag` crust over heat | `ls-z-bile` sick yellow film | `ls-p-void` violet, a violet light |
| `ls-t-ironclad` pale steel | `ls-z-spore` ochre dust | `ls-p-rift` white at the core |
| `ls-t-arc` jagged electricity | `ls-z-tendril` roots on the ground | `ls-p-dilation` pale blue, almost still |
| `ls-t-drone-lume` a lamp of its own | `ls-z-brood` brood violet | `ls-p-tether` a line dragging one way |
| `ls-t-rangefinder` an amber line | `ls-z-infested` grey gone soft | `ls-p-null` the glow gone out |
| `ls-t-thermite` white-hot | `ls-z-knit` green that breathes | `ls-p-khala` the shield's blue |
| `ls-t-weldglow` flares along the hull | `ls-z-chitin` pale as bone | `ls-p-shieldweave` three turning bands |
| `ls-t-clamp-spark` a broken line | `ls-z-frenzy` a fast red line | `ls-p-phase` faint, and quick |
| `ls-t-overclock` wisps off the outline | `ls-z-gorged` swollen and dark | `ls-p-warplag` an outline a step behind |
| `ls-t-smokeveil` a grey haze | `ls-z-hive` a shared line in time | `ls-p-echo` a copy, one shade toward white |
| `ls-t-anchorplate` plates settling | `ls-z-molt` flakes coming away | `ls-p-storm` storm blue, charged |

And `ls-hybrid`, Zerg brown under Protoss gold, which the Hybrid Reaver wears.

## The affixes

A unit carries one, an elite two, a mid-boss three and a boss four. Each has a weight, tags, a
cost in budget, a look and the name parts a generated name is built from.

| Terran | Zerg | Protoss |
|---|---|---|
| Searing (hits burn) | Corroding (-4 armour, stacks) | of the Void (blinks when hurt) |
| Slagging (slower, burning) | Bilious (rot, 3 deep) | Psionic (hits burn the mind) |
| Jamming (fires half as often) | Sporing (an aura that slows) | Rending (-3 armour, -20 damage) |
| Ironclad (+45% life, +3 armour) | Rooting (hits root) | Dilating (an aura: half speed) |
| Warded (an aura: +3 armour) | Splitting (two copies on death) | Tethering (drags units in) |
| Mending (an aura: +8 a second) | Brooding (spawns brood) | Nulling (a field nothing fires in) |
| Overclocked (faster, thinner) | Infesting (kills become husks) | Warded (+50% shields, an aura) |
| Droning (sends sentry drones) | Knitting (an aura: +10 a second) | Khala-lit (an aura: +20 damage) |
| Ranging (+2 tiles reach) | Chitinous (+50% life, +4 armour) | Phasing (+35% speed, warps out) |
| Veiled (an aura: a haze) | Frenzied (much faster, thinner) | Lagging (hits add cooldown) |
| Thermal (sprays thermite) | Gorging (feeds on what it hurts) | Echoing (warps in copies) |
| Anchoring (digs in, hits harder) | Molting (sheds and heals at half) | Rifting (holds, then breaks) |

## The archetypes

A classic unit with a role, which decides its brain. `tier` is the wave third it may first come
in; `cost` is supply.

| Terran | Role | Zerg | Role | Protoss | Role |
|---|---|---|---|---|---|
| Trooper (Marine) | ranged | Swarmling (Zergling) | bruiser | Blade (Zealot) | bruiser |
| Burner (Firebat) | bruiser | Spitter (Hydralisk) | ranged | Lance (Dragoon) | ranged |
| Raider (Vulture) | assassin | Flyer (Mutalisk) | ranged | Adept (High Templar) | caster |
| Walker (Goliath) | ranged | Digger (Lurker) | artillery | Shade (Dark Templar) | assassin |
| Breacher (Siege Tank) | artillery | Rampart (Ultralisk) | bruiser | Bastion (Archon) | bruiser |
| Corpsman (Medic) | support | Shaper (Defiler) | caster | Severer (Dark Archon) | caster |
| Stalker (Ghost) | assassin | Matron (Queen) | summoner | Glider (Scout) | ranged |
| Skyhook (Wraith) | ranged | Shrieker (Guardian) | artillery | Corsair | ranged |
| Tender (Science Vessel) | support | Gorger (Devourer) | ranged | Ark (Carrier) | summoner |
| Siren (Valkyrie) | ranged | Cutter (Scourge) | assassin | Weaver (Arbiter) | support |
| Warden (Battlecruiser) | bruiser | Husk (Infested Terran) | assassin | Thrower (Reaver) | artillery |
| Handler (SCV) | summoner | Brooder (Broodling) | bruiser | Eye (Observer) | support |

## The spell parts

A generated spell is one carrier, one payload and up to three modifiers, Noita's way. Each part
costs budget and a modifier multiplies it, so a boss can afford a wild spell and a mid-boss a
plain one.

- **Carriers** (8 shared): bolt, lance, nova, beam, storm, mine, lob, summon. Race-specific:
  barrage (Terran), burrow (Zerg), warp step (Protoss).
- **Payloads** (10 shared): fire, acid, void, shock, root, silence, slow, drain, pull, brood.
  Race-specific: incendiary, parasite, unmaking.
- **Modifiers** (10 shared): multicast, wider, heavier, lingering, delayed, chaining, splitting,
  homing, bouncing, on death. Race-specific: ranging, feeding, prismatic.

A carrier says where the cast lands and how it is paced; a payload what happens there; a modifier
changes the numbers, or, where the blocks cannot say it, adds a script block.

## The fairness rules

In `scripts/pools/spellparts.luau`. Tags that may not meet on one unit (cloaked with long range,
cloaked with artillery, suicide with tanky, no attack with assassin, two summoners, root with
pull, silence with root), and the most of a tag a wave may hold, from one crowd-control unit at
Recruit to six at Insane. A wave over a cap is drawn again from the same stream, so the run is the
same on every machine. Over the six difficulties and three races a roll redraws about nine times
in a run of twenty waves.

## The run

Rolled whole in `generate` before frame 0, from the seed and the settings alone. One seed gives
one run on every machine, so a replay or a save keeps only the seed. What comes back:

- the records: the generated unit kinds (cloned from their archetype, with the affixes' numbers as
  real columns: life and shields on the unit, damage and cooldown on a cloned weapon, speed on a
  cloned flingy), their generated spells as orders with blocks, their cards and their looks;
- `run`: the waves with their timing, supply, gates, kinds, elites and names; the perimeter and
  its gates as the map read gives them; the difficulty's curve; and each generated spell's numbers,
  which the modifiers' script helpers read back.

A twenty-wave Hardened run rolls about 20 generated kinds and 3 spells; forty waves at Insane
roll about 60 kinds and 7 spells.

## The mode in a game

`scripts/mode.luau`, once a second:

- **Start.** Read the settings, set the perimeter from the run, say the difficulty and the seed,
  ping each home.
- **Waves.** A wave every two minutes after the build-up, whether the last is cleared or not,
  out of the enemy base, or through two gates on an assault (every fifth, a quarter again as big).
  A wave is bought by its count of units, half its supply budget, so it climbs steadily wave on
  wave whichever archetypes it draws; each elite riding with it takes two of that count. The units
  are made for the enemy slot and then taken with `hx.own`, so classic's AI leaves them to the
  brains, which only send a unit at what it can hit and walk to (`hx.can_target`, `hx.can_reach`).
- **Perimeter.** Inside: nothing. The four tiles just outside: marked and counted, no damage. Past
  them: 5% of the unit's own most life a second, 5 more points every four tiles further out, to
  25%, shields first, and the death is nobody's kill. The edge grows four tiles every fifth wave
  cleared, never past 45% of the way to the enemy. While a boss lives its arena holds it in.
- **HUD.** The wave, the next in, and the next wave's spec when the setting is on, through
  `hx.hud`.
- **Ending.** The last wave cleared lifts the perimeter: with the push on, the enemy base's units
  go back to its own computer and its last building ends the game; with the push off, the win comes
  ten seconds after the lift. A home with no buildings left is out, and the last one out loses.

## The readout

The panel that describes the one unit you have selected is classic's, and `readout.json` changes
the parts a run needs; `scripts/readout.luau` fills them in. One file serves both consoles, so
the same readout shows whichever HUD the player chose.

- **The name** is the name the kind was rolled with, drawn in its rank's colour: a unique group
  teal, an elite gold, a mid-boss orange, a boss red. A line unit keeps classic's colour.
- **The rank's line** sits under it, with the rank as pips before its words: one for an elite,
  two for a mid-boss, three for a boss, while a unique group is told by its teal name alone.
  The group view marks every rank, a unique one included, since a crowd of twelve shows no
  names: a mid-boss in one is still a mid-boss. The line reads as the rank, what the unit is,
  and the mark it was born with: `Elite Spitter - Quick`.
- **A line for each status** on it, in turn, with its name, its time left and what it does, and a
  bar for the time. Where the room is no wider than classic's panel the line is the name and the
  time alone, since the whole line is half as long again as the row. A status with under two
  seconds left blinks the row it is on. The player's own units get those lines too, so a marine
  knows how long it is spored, and the rest of their panel stays classic's. A HUD design with the
  room reads them first, above the rank's line and the rolled figures, since a status is the part
  that changes while you watch.
- **The panel itself carries each change** against the row the kind clones, as a chevron and a
  figure: the armour, the shields and the attack's damage in their own icons' corners, the
  attack's cooldown as a second figure on the weapon's icon, and the life at the right end of its
  own row. A stat the roll left alone says nothing, and a weapon whose damage the roll left alone
  still shows its cooldown. The move speed is the one change with no place of its own.
- **Hovering says where every number came from**, in the order it was taken: the rank, the roll,
  then each affix by name. Each icon answers for what it carries, the weapon's for the cooldown
  and for any status that hastens or jams the unit, the wireframe for the life, and the name for
  the whole roll, the move speed with it: "Rolled from Hydralisk", then every change one to a
  line. A HUD design's own room names all six in its rolled-changes line.
- **The effects are light, never movement of the panel**: the wireframe flashes when the unit is
  hit and beats while it burns, what it lost or gained rises off the life line as a number, a heal
  lights that line, a new status slides in, an icon lights up when a status moves its stat, a kill
  lights the kills, and a ranked unit's wireframe is scanned as it is selected. The player's
  Reduce motion leaves out the effects that move text and keeps the light each one carries, so a
  hit, a heal and a status landing all still read; a blink is capped at three a second whatever
  the file says. An elite and up glows in its rank's colour and a mid-boss and up has light run
  across its name.

The effects are the design's full set, with three departures of our own. A heal gets a light of
its own, which the set leaves out: without it a heal said nothing a hit did not, and nothing at
all to a player who has turned motion off. Both floating numbers carry their sign, since the
engine hands the pattern a bare amount and a red "24" does not say which way it went. And the
rolled stats a run leans on, the life and the speed, are read on the wireframe and the name
rather than on icons of their own, because the panel has four icon places and neither stat owns
one.

Nothing of it reaches the game: the hook reads and never sets, the file is outside the mod's
fingerprint, and an enemy's rolls read the same for a player, a watcher and a replay.

## Checking it

`generate` runs under a stand-in host without the engine: it needs only `math`, `hx.isqrt`,
`hx.id` and the `ctx` it is handed. Two rolls of one seed give one run, which is the check the
mode's own tests should keep.

## The tiers, and what a rolled unit is

No unit a wave sends is a row off the table. Every group is rolled, and what it is rolled from
is `scripts/pools/tiers.luau` -- the one file to change to make a run harder, stranger or
longer in reach.

**A tier** is how deep a run has gone. Each one says where it opens, as a share of the run's
waves, and a wave buys from its own tier and every one below it, so the early archetypes never
stop coming and the late ones arrive on top of them. Each archetype's `tier` in the race pools
is its place here.

**The bounds** are what makes two Troopers different Troopers. A tier gives each stat a low and
a high, as a percentage of the row the unit clones -- life, shields, damage, speed, cooldown and
armour -- and every rolled kind draws its own number between them. A deeper tier strays further,
so a late wave is less predictable than an early one. `cooldown` below 100 is a faster attack,
so its bounds lean the other way from the rest.

**The ranks** are how strong a thing is for its tier, weakest first: a line unit, a unique group,
an elite, a mid-boss, a boss. Each says what the base row's life is multiplied by, how many of
the mod's affixes it may carry, and what a rolled spell of its own may cost. The order is the
point and it is written in the file rather than worked out anywhere, so a unique group can never
be read as stronger than an elite by accident. Three ranks are also capped, once the rank, the
roll and the affixes are all on, so stacked affixes cannot make a wall:

| Rank | Shields, damage, speed, cooldown | Life | Armour |
|---|---|---|---|
| Unique | within 25% of the base row | within 25% of the base row | at most +1 |
| Mid-boss | within 35% of the base row | within 50% of the rank's 340% | at most +2 |
| Boss | within 50% of the base row | within 75% of the rank's 700% | at most +3 |

The tooltip names what the cap took. Elites are left to their roll.

**A group shares its roll.** The first time a wave buys a Trooper it rolls what this wave's
Troopers are; buying more buys more of those. So the five that walk in together have the same
numbers and the same effects, and a player who learns one has learned the group.

**A unit may be born with a mark.** The one roll the run does not make before the first frame:
as a unit walks in it may take a single status of its own -- Tough or Frail, Quick or Slow,
Savage or Blunt -- and the tier says how likely that is (`born`, a fifth of the units at the
first tier and a little over a third at the last). The marks come in pairs and the numbers are
small, so the group is still the group and the wave's strength stays where its rolls put it; what
it buys is that the odd unit of a group is not quite its neighbour. This roll is `hx.random`, the
game's own draw, which the save carries and the state hash covers, so every machine and the
replay draw the same marks in the same order. Everything else is rolled from the seed because
`hx.random` is refused before a game has started, and `generate` runs before it.

**Every number is whole, and the run's seed is what it is rolled from.** The bounds are
percentages rather than fractions so that the roll comes out the same on every machine, which is
what lets a replay of a run send the run's own units rather than a fresh guess at them.

**The tables are checked before a run is rolled.** `LS.dataFault` reads them as the generator
starts: the ranks weakest first, every bound the right way round, each tier opening at a share of
the run, and every archetype naming a tier that is there. Anything wrong refuses the run and says
what it is in `hyperion_mods.log`, so an edit to this file that does not add up is a sentence to
read rather than a strange run.
