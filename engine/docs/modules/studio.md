# studio: Hyperion Studio, the program modders make mods in

Hyperion Studio is `HyperionStudio.exe`, a program of its own for modders. It is built from the
design engine's base (`src117/design/`, `docs/modules/design.md`) and its own folder,
`src117/studio/`, in place of the design engine's panels (`design_main.cpp`). It opens a mod,
shows every record of its `types.json` with each value and where the value comes from, and changes
the mod through edits that can be undone. A canvas game runs beside it to try the mod in.
`Hyperion.exe` carries none of it.

The mod's format is `docs/mod-data.md`; the studio writes only what the loader
(`port/data/port_moddata.cpp`) reads, and the loader stays the judge of what a value means
(`studio_verify`).

## Running and building

| | |
|---|---|
| `python tools/build_studio_x64.py` | compiles the changed files of `src117/studio/` and links `src117/build/studio/HyperionStudio.exe` from the design build's objects; a lock in the build folder keeps two builds from linking at once |
| `python tools/build_design_x64.py` | needed first, and again only when the design base or a hooked file changes |
| `python tools/studio.py` | starts it maximized from `testrun/studio/`, building when stale |
| `python tools/studio.py --background --port 27132 --folder studio-test --size 1920x1080` | a tool's run, apart from the user's; it takes the channel's `click` (`HYPERION_TEST_MOUSE`), in window pixels, so a check can aim a spell or place a building |
| `python tools/studio.py call <cmd> '<json>' --port N` · `frame <png> --panels --port N` · `--stop --port N` | driving it from the command line |
| `python tools/studio_test.py --port 27132 --folder studio-test` | the model checked end to end over the channel |

The channel's port is 27119; the MCP server `hyperion-studio` in `.mcp.json` speaks to it with the
`studio_*` tools of `tools/control/hyperion_mcp.py`, each edit named as Claude's.

The Windows build carries it to players: `tools/build_engine_x64.py` builds the design base and
the studio after Hyperion (`--no-studio` leaves it), and the install and the package put
`HyperionStudio.exe` beside `Hyperion.exe` with `studio\fields.json` and the `tools\control\`
its Claude panel runs (`build.md`, `tools/package.py`). Started from an install with no options
it takes that folder's `Data\`, so it lists and opens the player's own mods, reads the game's
files as Hyperion does, and opens its channel on 27119. The Mac and Linux packages have
Hyperion alone, since the design engine it is built on is Windows only.

## The model (`studio_model.h`)

- **Documents.** `types`, `mod`, `looks` and `hud` are `PortJson` trees, and `iscript` is text; `lang`
  is the mod's languages, each `lang\<code>.json` (`docs/mod-data.md`, Languages) a member by its
  code, written a file each when it changed and deleted when its language is taken away. Each is
  read from the mod's data folder and written whole when it changed. `looks.json` and `hud.json`
  are written only once they have members or the file was there.
- **Records.** `StudioRecords(t)` gives a table's records: the mod's own, its edits of classic
  rows, and its dependencies' (read only). `StudioGet` and `StudioRow` resolve a field's value and
  its source (own, a base, an edit, classic), by the loader's rules (`studio_resolve.cpp`).
- **Fields.** The loader's columns and `engine/studio/fields.json`, which gives each field's
  category, editor, label and help (`studio_schema.cpp`). Editors: `int`, `hp`, `frames`, `px`,
  `half`, `bool`, `text`, `enum:<k>`, `flags:<k>`, `link:<table>`, `req`, `picture`, `sheetpath`,
  `grp`, `soundfile`, `video`, `pair`, `script`.
- **Edits.** Every change is a `StudioEdit` (document, path, before, after, sender). Edits made
  together are a group and are undone together; `StudioGroupBegin` nested inside an open group
  joins it. `StudioSetField`, `StudioCreate`, `StudioDelete`, `StudioSetRaw`, `StudioSetScript`
  and `StudioPut` (any member of a document, such as `cards`, `races` or a `hud.json` key) each
  make one group.
- **Claude's asks.** While the Claude panel's ask runs, the edits sent as "claude" are one group
  (`StudioClaudeJoin`). In Show me first that group is Claude's proposal (`StudioPending`): while it
  is the last group done, an edit of any other group is refused with why, and a save, the autosave
  and a push write the documents without it (`StudioGroupAside`; the translations the game's loader
  reads at a push's load through `StudioDocAccepted`), until it is accepted
  (`StudioPendingSet(0)`) or undone. Undo takes it back whole, Redo brings it back waiting.
- **Saving.** The stable writer (`StudioJsonText`) changes a file only when its content does.
  An autosave goes to `<mod>/.studio/autosave/` each minute and before each push, and
  `studio_recover` takes it back after a crash.
- **Files.** `StudioFiles(exts)` lists the data folder; `StudioImport` copies a file in. Files are
  not edits: undo leaves a file that was copied in.
- **Worked-out numbers** (`studio_calc.cpp`): damage a hit after armour and size, attacks to kill,
  damage a second and speed, by the classic damage rule (`CUnit::Damage`); `StudioStrikeNumbers`, one attack on a
  target's life and shields as they stand, for fights worked out frame by frame. Speed is the flingy's top speed, or, for a unit its image script walks (movement control 2), the
  Walking animation's `move` distances over its frames in the part that repeats (`StudioScriptWalkSpeed`,
  `studio_art_anim.cpp`).

## The commands (`studio_commands.cpp`)

`studio_status`, `studio_mods`, `studio_new_mod {name, id?, by?, line?}` (a mod made among the installed ones, its id from its name, and opened; `studio_newmod.cpp`, File, New mod), `studio_open {mod|folder}`, `studio_list {table, classic}`,
`studio_get {table, name}`, `studio_set {table, name, field, value|revert, merge}`,
`studio_create {table, name, base, edit, record}`, `studio_delete`, `studio_put {doc, path, value?}`,
`studio_files {ext?}`, `studio_import {from, folder?}`, `studio_damage {weapon, target, level?}`,
`studio_make_unit {name, label, base, race, at, copy_weapon, values, button}`, `studio_raw`,
`studio_script {text}`, `studio_undo`, `studio_redo`, `studio_history`, `studio_save`,
`studio_recover`, `studio_push`, `studio_pushes`, `studio_verify`; and the shell's `studio_show
{workspace, table, name, view, filter, dialog}`. The areas add their own: `studio_actions {unit?,
show?, blocks?}` (blocks true scrolls to the shown action's blocks), `studio_make_action {unit, from, label?, name?, values?}`, `studio_delete_action {name}`
(`studio_actions.cpp`); `studio_try {unit, against?, count?, yours?, units?}` and `studio_test
{again?, pause?, resume?, step?}`, `studio_scenarios`, `studio_scenario_save {name, sides?, tried?}` and
`studio_scenario_delete {name}` (`studio_testloop.cpp`); `studio_race_play {race, map, computers?,
computer_race?}`, `studio_race_game {resume?}` and `studio_race_stop` (`studio_racetest.cpp`); `studio_races`, `studio_race_set {race,
value | field+value | rename | remove}` (fields name, base, start, units, without, ai, console, music, comment; console and music are zerg, terran or protoss, null for its base's), `studio_races_show {page?, race?, unit?}` (page `races`, `cards` or `tree`, the tree also answered as JSON), `studio_card
{unit | card}`, `studio_card_set {unit, index?, button | fields | remove}`, `studio_card_move {unit,
index, position}`, `studio_card_train {unit, card, position?, hotkey?, text?}`, `studio_card_build {unit, menu?, position?, hotkey?, text?}`, `studio_race_fix {race, unit?}` (the race's stand-ins let into what needs the classic units it goes without, its records and edits of classic ones, and named beside them where the classic buttons say what they need, "Barracks or Iron Forge", as one edit, and let into the casters of the techs the units it goes without cast (a classic tech's use requirements, from the classic trees, given an edit; `casters` marks those); `words` counts the words changed) (a classic record's requirements and a classic tech's casters show as the classic trees have them, and a change starts from them) (a command icon, a tech's, an upgrade's, a weapon's or a status's, is picked from a grid of the classic icons, `StudioIconPick`) (a classic button's words, its text, hotkey and text while it cannot, are the mod's to change in its pane, as a `cards` entry with `classic`; `studio_card` gives `reworded`, `words_index` and the classic words) (a mod building on a worker's build menu, `"basic"`, `"advanced"` or the menu's name) (`studio_races.cpp`,
`studio_races_cards.cpp`); `studio_race_rules {race, build?, power?, power_from?, creep?, creep_from?, shields?, regenerate?, fix?}` (a race's `rules`, each null for its base's way; `fix` sets the fields of its units the rules ask for, as one edit; the answer has the rules, its `workers`, the `changes` still to make with why, the `conflicts`, a building that needs power with nothing to give it, and its advisor's `lines`) and `studio_race_advisor {race, advisor?, line?, from? | sound? | revert?}` (whose advisor its players hear, null for its base's; a line said by a file brought in under `sounds/advisor`, by a sound of the mod's, or by the advisor again) (`studio_buildrules.cpp`); `studio_unit_art {unit}`, `studio_art_sheet {image, from | file, frame?,
origin?, directions?, frames?, scale?, team?}`, `studio_art_frame {image | sprite | flingy | unit,
frame?, facing?, colour?}`, `studio_art_view {facing?, colour?, turning?, set?, tab?, select?,
classic?, anim?, tick?, playing?}`, `studio_art_anim {image | unit, anim?, facing?}` (an image script's animations and a run tick by tick: its overlays with `made_by`, the one that made each, -1 the image, as overlays make overlays in turn; `played`, its ticks with those its overlays play on after the image's script ends; `lanes`, a run for each other way a condition goes, then for a condition only a lane comes to, six at most) (`studio_art.cpp`, `studio_art_anim.cpp`); `studio_play {name | file}`, `studio_unit_sounds {unit, from,
portrait?}`, `studio_unit_sound_add {unit, line, from}`, `studio_unit_sound_remove {unit, line,
sound}`, `studio_unit_portrait {unit}`, `studio_portrait_video {portrait, kind, n, from}`
(`studio_sounds.cpp`); `studio_look_set {name, field?, value?, merge?, remove?}`, `studio_look_unit
{unit, look}`, `studio_hud_set {path, value?, merge?}`, `studio_looks_show {look | page}`
(`studio_looks.cpp`); `studio_cinematic {name, from}`, `studio_watch {name?, stop?, mute?}` (a cinematic played with its sound, stopped or muted), `studio_mod_summary`, `studio_mod_show
{section}` (`studio_modpage.cpp`); `studio_action_research {name, at?, minerals?, gas?, research_time?}`, `studio_action_blocks {name, blocks}` (an action's blocks, the whole list; [] for none), `studio_block_add {name, block, path?, at?}` (path the indexes of a repeat or wait to add inside), `studio_block_set {name, path, field, value}`, `studio_block_remove {name, path}` and `studio_block_move {name, path, by}` (`studio_actions.cpp`); `studio_statuses {query?, show?, section?}` (the statuses with each one's sentence and uses; section, changes, life, look or uses, scrolls the editor to it), `studio_status_new {name?, label?, from?, values?}` (from blank, slow, poison, stun, haste, weaken, mending or a status), `studio_status_set {name, field+value | values | rename}` (a rename repoints its uses), `studio_status_delete {name}` (its uses taken off with it), `studio_weapon_status {weapon, status}` and `studio_unit_aura {unit, aura | remove}` (a classic weapon or unit given an edit record) (`studio_statuses.cpp`); `studio_check {run?, save?}`, the studio's checks and, with run,
the loader's (`studio_checks.cpp`); `studio_strings {query?, kind?, mine?, changed?, missing?, show?}` (every text a player sees, each with an id, and the key a translation names it by with its translation in each language; `missing` a language, those missing or stale in it), `studio_string_set {id, text}` (one set, an empty text the classic words again) and `studio_strings_replace {find, replace, case?, query?, kind?, mine?, changed?, preview?}` (one edit for all) (`studio_strings.cpp`); `studio_languages {add? | remove? | game?}` (the mod's languages with each one's counts, a language added or taken away as one edit, `game` the language the studio's test game and a race's Play it show the mod's text in, "" for its own), `studio_translations {language, missing?, stale?}` (each string's key, id, where, the mod's text, its translation and state: done, stale, missing or empty), `studio_translation_set {language, id | key, text}` (one translation as one edit, made from the mod's text as it is now; "" or null takes it away; a button's text that marks no letter has the mod's hotkey letter marked), `studio_translations_export {language, file?, format?}` (a translator's file, CSV or PO, `.studio\translations\<code>.csv` in the mod's folder by default, which the game does not read and a package leaves out) and `studio_translations_import {language, file}` (read back as one edit: a line whose key is no string of the mod is refused and named, an empty translation left as it is, and one made from an older text kept stale) (`studio_languages.cpp`); `studio_matchup {units?, against?, per_cost?, budget?, level?, closing?, show?}` (the matchup grid: each cell's winner, margin, time, counts, attacks and time to kill each way) and `studio_matchup_against {add? | remove? | reset?, rows?}` (the picked columns, or rows) (`studio_balance.cpp`); `studio_ab_variant {unit, field?, value?, table?, name?, revert?, clear?}` (version B of a unit: a number field of the unit or of a weapon it fires, each change with A's value beside it, kept in the studio and not in the mod), `studio_ab_run {unit, against?, count?, yours?, units?, seeds?, first_seed?, seconds?, attack?, theirs_attack?, b?}` (the fight played out in the canvas game, A and B for each seed), `studio_ab {show?}` (its state, each fight, the sums: win share, time to win, life left, damage dealt and taken), `studio_ab_stop`, `studio_ab_keep {unit}` (B's changes written into the mod as one edit), and `studio_ab_mark` and `studio_ab_restore {seed}`, the run's own steps in the game (`studio_ab.cpp`); `studio_sound_set {sound, field, value | revert, merge?}` (a sound's subtitle, priority, flags, min_interval, length_adjust or min_volume; a classic sound changed through an edit of it in the same undo) (`studio_voice.cpp`); `studio_tilesets {tileset?}` (each classic tileset with the mod's changes counted, or one's listed), `studio_tileset {tileset, group | megatile | palette}` (a group's flags, megatiles and colours, a megatile's 16 minitiles as classic and as the mod has them, or each colour now and classic, whether units draw with it and whether it cycles), `studio_tile_set {tileset, group | megatile | colour | colours, field?, value | revert, merge?}` (a group's buildable, creep, walkable, height, blocks_view or comment, a megatile's walkable, blocks_view or height, a colour, or several colours as one edit; each checked by the loader's own reading before it is put), `studio_tile_image {tileset, group | megatile, from | revert}` (a PNG of the right size copied to `tilesets/<tileset>/groups/` or `megatiles/` and named), `studio_tile_export {tileset, group | megatile, to, classic?}` and `studio_tilesets_show {tileset?, view?, group?, slot?, megatile?, colour?, colours?, layer?, zoom?, overlay?, changed?}` (`studio_tilesets.cpp`). Every field may carry `sender`; the
panels send "you" through `StudioAsk`. The MCP server has a tool for each.

An area that needs a command of its own registers it with `PortControlCommand(name, handler)`
(`port/tools/port_control.h`) from its start function (`StudioRacesInit`, `StudioArtInit` and the
others in `studio_ui.h`), which `studio_main.cpp` calls once; the handler has the form of those in
`studio_commands.cpp`.

## The shell (`studio_shell.cpp`, `studio_kit.cpp`, `studio_theme.cpp`, `studio_draw.h`)

The menu bar, the workspace toolbar, the Explorer, the document area with its view tabs, the
Inspector, the bottom panes (Problems, History, Output, Claude) and the status bar. The look is the
design's tokens (`StudioCol(TK_*)`), Hyperion's faces (`StudioFont`), and the design's pieces:
buttons, segmented choices, chips, check boxes, pulldowns, inputs and number fields
(`studio_ui.h`). `studio_draw.h` holds the short helpers every area file uses (`P` for a length,
`C` for a colour, `T` for text).

Workspaces: 0 Make, 1 Tables, 2 Art, 3 Looks, 4 Effects, 5 Cards and races, 6 Test, 7 Checks,
8 the mod's page, 9 Strings, 10 Balance, 11 Statuses. A unit's document has the views Overview, Numbers, Actions, Art, Look,
Sounds and Card and tech, and Numbers has three levels: Simple, Detailed and Raw. Other records
have their head above their numbers. Tables shows any of the ten tables, its column sets the
table's categories in `fields.json`.

The field editors (`studio_editors.cpp`) draw any field for Numbers and for an area that wants
one: `StudioFieldEditor`, `StudioRecordPick`, `StudioFileField`, `StudioRequirements`, and the
system's file dialog through `StudioPickFile` and `StudioPickedFile`.

## The areas

Each area is a file of its own, behind the contract in `studio_ui.h`. The shell calls the
contract and nothing else of an area; an area reads the model, draws with the kit, and changes
the mod only through the commands (`StudioAsk`, or `DesignCall` when it needs the answer).

| File | Holds | Contract |
|---|---|---|
| `studio_races.cpp`, `studio_races_cards.cpp`, `studio_races_tree.cpp`, `studio_races.h` | the Cards and races workspace: races (name, base race, start units, units, what is without, the AI's mapping), every unit's card and the workers' build menus as a 3 x 3 grid with command icons, drag and hotkey clashes; a race's tech tree in tiers with its missing links; a unit's Card view; a tech's and an upgrade's head | `StudioRacesWorkspace`, `StudioUnitCard`, `StudioTechHead` |
| `studio_art.cpp`, `studio_art_script.cpp`, `studio_art_anim.cpp` | a unit's picture, its command icons and wireframes, its animations played from its script, the Art workspace (images, sprites, sheets brought in), an image's and a sprite's head with its frames, a unit's Art view (flingy, sprite, image), the image script | `StudioUnitPicture`, `StudioArtWorkspace`, `StudioArtHead`, `StudioUnitArt` |
| `studio_sounds.cpp` | a sound's head with play, a portrait's head, a unit's Sounds view | `StudioSoundHead`, `StudioUnitSounds` |
| `studio_voice.cpp`, `studio_voice.h` | under each line of a unit's Sounds view, a row for each of its sounds: the subtitle, the priority, the gap before it plays again, and its flags in words (not cut off, one per unit type, once at a time) | `StudioSoundRows` |
| `studio_looks.cpp` | the Looks workspace (looks.json and the console's hud.json), a unit's Look view | `StudioLooksWorkspace`, `StudioUnitLook` |
| `studio_readout.cpp`, `studio_readout.h` | the Looks workspace's Unit readout page: readout.json as a diff from classic's own panel, its seven parts with every parameter, the lines it adds, its ranks, its effects, and the panel's words as the game is showing them (below) | `StudioReadoutPage` |
| `studio_marks.cpp`, `studio_marks.h` | the Looks workspace's Marks pages: types.json's `marks`, the looks a mark on the ground wears, each block with the ranges the loader accepts, and the look put on a real mark in the canvas game (below) | `StudioMarksPage`, `StudioMarkNames` |
| `studio_actions.cpp` | the Effects workspace (orders, techs and weapons made from blocks), an action's blocks (fire, damage, heal, energy, status, remove statuses, spawn, teleport, sound, show, wait, repeat; repeat and wait nest) with the action's sentence from them, a unit's Actions view with its passive aura, an order's head | `StudioEffectsWorkspace`, `StudioUnitActions`, `StudioOrderHead` |
| `studio_statuses.cpp`, `studio_statuses.h` | the Statuses workspace: the mod's statuses with search, New (blank or from a kind), Duplicate and Delete; a status as plain sentences (how long, what it changes, the damage it takes, which side it is on, life over time, its look picked from images and played on a unit), where it is used (weapons, actions, auras) opening each, and a status put on a weapon or given as an aura; the status pick the actions area uses. The kind the readout draws a status with is shown as its framed icon, drawn from the engine's own table (`PortReadoutGlyph`, `PortReadoutKindHue`): a disc where the status helps the unit, a diamond where it hurts. Its `look` block overrides that a key at a time: the kind, its own icon, its colour, its pattern, where it shows at all, and how strongly it asks for the wireframe's one mark. The colour row says where that colour goes — the framed icon's edge and glyph, the pattern, the wireframe's tint, the status's row in a HUD's room and its mark in the group view — and that `for` and `against` name a side rather than a colour, so those take its green or red instead, as a status naming none does. The kind is the engine's own answer for that record (`ModStatusKindOfDef`), the call a game makes of the same rule, so the Studio and the game cannot differ about a status; a record the loader will not read says so instead of showing a kind. Sheet shows every status of the mod as a tile with that icon. A picture picked that is a weapon's own is marked there and then: its script damages in one of its animations, which worn as a status asks it to damage with no shot behind it, so nothing happens (`ImageScriptDamages117`, the same thing the loader says in the log) | `StudioStatusesWorkspace` |
| `studio_testloop.cpp` | the Test workspace: scenarios, the three fixed and the mod's own (below), the pushes, the game | `StudioTestWorkspace` |
| `studio_racetest.cpp` | a race played on a real map, from its page's Play it (below) | `StudioRaceTestPanel` |
| `studio_buildrules.cpp` | a race's page's How it builds and Its advisor: each rule a block in words (the way its workers build, power and what gives it, creep and what makes it, shields, healing, and what stays its base's), the fields of its units each rule asks for offered as one fix, what does not fit together named; whose advisor its players hear, each line played through the engine's sound and replaced by a file | `StudioBuildRulesPanel` |
| `studio_tilesets.cpp` | the Tilesets workspace: a classic tileset as the mod has it (classic read from the player's archives, the mods it depends on and its own `tilesets` over it, by the engine's `port_modtileset.cpp`); its groups in a zoomable grid with their flags as chips, the changed ones marked; a megatile large with its 16 minitiles' walkable, height, blocks view or the group's buildable over it, painted with a brush, one stroke one edit; a picture brought in for a megatile or a group, saved out to paint over; the group's flags, name and colours, recoloured at once; the palette with the cycling ranges moving as in a game, one colour or several edited (hue, saturation, brightness); a patch of terrain painted with groups; the changes, each put back alone | `StudioTilesetsWorkspace` |
| `studio_checks.cpp` | the Checks workspace: the mod checked by the loader, each problem linked to its field | `StudioChecksWorkspace` |
| `studio_media.h`, `studio_media_audio.cpp` | what the sounds, looks, mod page and checks areas share: a sound found, decoded and played, the game's palette, a dependency's data folder, a file copied in under a name, the pages' headings | |
| `studio_modpage.cpp` | the mod's page: mod.json, what it depends on, its cinematics | `StudioModWorkspace` |
| `studio_languages.cpp`, `studio_languages.h` | the mod's languages: each string's translation, what is missing or stale, the counts in Problems, the test game's language (`ModLangUse`, and `HYPERION_LANGUAGE` for a game Play it starts), the model's translations given to the loader for pushes before they are saved (`ModLangWorking`), a translator's CSV or PO file written and read | `StudioLanguagesProblems` |
| `studio_strings.cpp` | the Strings workspace: a column for each of the mod's languages beside its own text, a translation edited in place (Tab goes down the column), a missing one marked, a stale one chipped; the Languages row, each language a menu (show what is missing or stale in it, write or read a translator's file, take it away), Add a language, Test game in; and every text a player sees that the mod gives or could give (units' names, its weapons', techs' and upgrades' names, their own or the game's string their label numbers, races' names, each card button's text and its text while it cannot, mod.json's name and line, the subtitles of the mod's sounds), with where it is, where it comes from and the classic text; search, filters by kind and source, sort; a text edited in place through the area's own commands (`studio_set`, `studio_create`, `studio_card_set` with a `classic` entry for a classic button, `studio_race_set`, `studio_put`), the hotkey's letter marked; find and replace with a preview, one undo for all | `StudioStringsWorkspace` |
| `studio_balance.cpp` | the Balance workspace: the mod's units that fight (and picked rows) against themselves and picked classic units (two of each race by default), each cell fought out frame by frame by the classic damage rules (`StudioStrikeNumbers`): shields, armour, size, the weapon's factor times the swings of the unit's attack animation, cooldowns; one against one or as many of each as a budget buys (four melee units round each), upgrades at 0 or 3, closing in by range and speed; who wins and by how much, each side's attacks and time to kill; a row's wins, life for 100 spent and damage a second by size, a column's wins; a cell's pair side by side and Play this fight (`studio_try`) | `StudioBalanceWorkspace` |
| `studio_ab.cpp`, `studio_ab.h` | A and B fights (below): Balance's Played out page, opened from a cell's Play it out or the Test workspace's A and B | `StudioAbPages`, `StudioAbWorkspace`, `StudioAbOpen` |
| `studio_claude.cpp` | the dock's Claude tab: the modder's own Claude Code asked in words, its answer and tool calls streamed, Claude's edits as a proposal to accept or made at once, conversations kept per mod (below) | `StudioClaudeTab`, `StudioClaudeTick`, `StudioClaudeBadge` |
| `studio_modes.cpp`, `studio_modeshow.cpp`, `studio_modes.h` | the Mode workspace: a mod that is a game mode, its mode block written and its generate rolled; the mode's set-up tab as a player meets it and the picture its preview draws of a map (below) | `StudioModesWorkspace` |

A workspace is drawn in the rectangle `a` to `b` the shell gives it, in the shell's window; it
opens a child window of its own for anything that scrolls. A unit's view is drawn at the cursor
inside the document's scrolling region and ends with the cursor below what it drew.

## Scenarios of the mod's own

Beside the three fixed scenarios, Try it takes the mod's own: each a file,
`<mod folder>\scenarios\<name>.json`, written at once when saved (a file, as an art file brought in
is, not an edit to undo), read again as the folder changes. Picking one puts its rows in Of your
own, where they are changed and saved again; its x takes the file away. A file holds one or two
sides, `tried` the place of the one the unit tried joins, and each side's start order:

    {"name": "Trooper rush", "tried": 0,
     "sides": [{"units": [{"unit": "iron_trooper", "count": 2}], "order": "attack"},
               {"units": [{"unit": "zerg_zergling", "count": 8}]}]}

`studio_try {unit, against: "<name>"}` tries one; `studio_scenario_save` without `sides` saves the
last try.

## A and B fights (`studio_ab.cpp`)

Version B of a unit is a list of changes to its number fields and its weapons', kept in the
studio until Keep B writes them as one edit. A run plays B as records of its own in the same
game as A: the unit as `<unit>__b`, based on the unit with B's changes, and each weapon B
changed as `<weapon>__b`, both pushed with the working mod in one push. One game holding both
means one push and one snapshot serve every fight, so nothing but the version differs between
A's fight and B's.

A run starts the canvas game again, paused on its first frame, so the fights do not depend on
what the game did before: the units' slots and the frame count are the same each run. After
the push it takes a snapshot; each fight goes back to it, seeds the game's generator with the
fight's seed, spawns the two sides and steps the game fast until a side has nothing left or the
time is up. The same seeds give the same numbers on a re-run, after a restart of the studio,
and with other records of the mod changed. Life counts shields only for units that have them.
The Played out page sets the fight up (the unit and how many, the units against it, the seeds),
lists B's changes with A's value and the share each one moves it, and shows A's and B's sums
side by side and each seed's two fights.

## A race on a real map (`studio_racetest.cpp`)

A race's page ends its form with Play it: a map among the studio's (its data folder's `maps\`,
the game folder's and each `--maps` folder, searched by name, the players from the name's `(2)`
or the map itself), how many computers and their race (a classic one, or one of the mod's, so a
computer plays the race's stand-ins), and Play. Play saves the mod, then starts `Hyperion.exe`
(beside the studio, else the engine's x64 build above its game folder) as a process of its own:

    Hyperion.exe --data=<data folder>\studio-games --control=<27140 to 27149> --mod <id>
                 --map <file> --race "<race>" --computers N [--computer-race "<race>"] [--maps ...]

One data folder takes one copy of Hyperion (`port_instance.cpp`), so the game has its own,
`studio-games\`, whose `settings.json` the studio gives the studio's mods folder (`modsFolder`); it
reads the game's files from the studio's game folder (`HYPERION_GAME`, passed on), in a window
of its own (`HYPERION_WINDOW` windowed, or background for a tool's studio), without
`HYPERION_QUIET`, and with the channel's key the studio makes (`HYPERION_CONTROL_KEY`). A thread
reads the game through that channel twice a second (`status`, `players`, `units`): the panel and
`studio_race_game` show its state, frame and each player's resources and units by type, by the
mod's names. Stop ends that process alone, by its handle; the game is in a job that ends it when
the studio ends, closed or killed, so none is left to hold the replay gate. A game whose window
starts without focus can hold at its start: Go on, or `studio_race_game {resume: true}`.

## Scripts (`studio_scripts.cpp`)

Workspace 13, Scripts: the mod's Lua (`docs/mod-data.md`, Scripts), each file of its data folder's
`scripts\` a member of the model's "scripts" document. A code editor with the language's colours,
line numbers, the loader's own problems underlined where its parser puts them (`ModLuaCheckText`, so
what it underlines is what `--mod-check` refuses), completion of the API's names with their
arguments, Find, and every change an edit undo takes back; the functions each file gives and where
the mod names them; the hooks and what calls them; and a function called in the test game against
the unit selected there, with the scripts' log. Commands: `studio_scripts`, `studio_script_set`,
`studio_script_new`, `studio_script_delete`, `studio_script_rename`, `studio_script_show`,
`studio_script_run`, `studio_script_api`.

## Unit readout (`studio_readout.cpp`)

The Looks workspace's nav has Unit readout above Marks on the map: readout.json, the one unit's
panel as a mod changes it (`docs/mod-data.md`, "readout.json"). It is its own file rather than part
of hud.json, and the Studio's model reads and writes it as it does hud.json, so a save writes a
readout.json once there is something in it.

The template is classic's own panel and a mod changes the parameters it wants, so the page is
written as a diff: the seven parts as a row, the one picked written out a parameter a row, each
with its editor, classic's own value beside it, and an x that gives classic's back. A parameter
the mod does not set is drawn with classic's value as its hint, so what the panel will do is read
off the page without opening the file. A part with parameters set is marked in the row, and the
page's head counts them. The file's own keys (`from`, `in_huds`), how a status shows itself
(`status_look`: the framed icon, line 3 in turn, the wireframe's mark, the group view's) and the lines
it adds (`add.lines`) are written as three more parts, since they read the same way. Under them the ranks a
hook may give a unit, each with its colour and the words a player reads where the id is not
them, and the effects.

The loader is the judge. Rather than mirror its reading, the page writes the model's document out
as text and hands it to `PortReadoutCheck`, which is the call a game's console and `--mod-check`
make, so what the page shows is the file in force and the rows above the parts are the loader's own
words: why a whole file does not read, and each key it passes over with the line it was on. Nothing
here can drift from the loader.

The effects are listed a row each: what sets the effect off (its events, or the test it is held
while), the op, the part, and its own values as the file gives them. An op that moves text is
marked motion, which the player's Reduce motion leaves out; nothing shakes, bounces or changes
size, by the user's rule for the HUD. The events, the ops and the forms a `while` takes are listed
under the rows, so the names need not be looked up.

The panel in the game shows the real words. A readout draws no panel of its own: it writes its
words into the controls classic draws the panel with, so the panel keeps classic's font, places and
spacing. Read it now asks each part for its words the way the panel's own control asks
(`PortReadoutPartText`), asks each stat icon for its tooltip, and shows them beside the lines the
hook returned with their time left, their stacks and what they do, and the rolled changes among
them. It reads the readout the last push loaded, not the values being edited, so a push is needed
to see an edit; it needs a game running and one unit selected.

Above the words, which part of the panel comes out where: the words through classic's own
controls, and what the colour layer paints over them (`port_readoutdraw.cpp`) — each status's
framed icon at its line, the wireframe's mark, the rank's pips, and, where a design's slot
gives the room, the hook's lines and a row per status with its bar. A fourth row sets `roll` apart,
since it is words and not light: the line it counts is written through the panel's own control,
so it moves nothing, and it is the one op a player who turned motion off still sees and the one
that shows on the SDL renderer, where there is no overlay to paint light into. A fifth row names every other op, light by
light: `float` is the one that moves anything, its own text rising off the part, and `slide` is a
band of light in from its side on the panel with the status's row sliding in a slot's room. The split is worth saying
because the two halves answer differently: a part that is only words does not carry a colour a
mod asks for, and a part that is only painted does not show among the words. The layer wants an
overlay to paint into, which the GPU presenter gives; with the SDL renderer the readout is the
words alone.

The preview also names the status the kills' row is showing at that moment
(`PortReadoutRowLine`), which is the call the drawing reads, so the words here and the icon the
drawing puts at the line's start are always the same status, asks the wireframe and the name for their own tooltips
(`PortReadoutPartTip`), which carry the life, the speed and the rolled changes that have no stat
icon to sit on, says where the room the added lines go in
is and whether that room lists the statuses itself or leaves them to the panel's rotating row
(`PortReadoutRoomBox`, `PortReadoutRoomLists`), and lists the effects running on each part
(`PortReadoutRuns`): the op, whether an event set it off or a test holds it, how far
through it is, the status that set it off and its own words. The player's Reduce motion is
applied in that answer, so a motion op missing from the list is one their setting left out.

Commands: `studio_readout` (the file in force, every parameter set by part, its ranks and effects,
and the loader's reading of it), `studio_readout_set {part?, field, value?, revert?}`,
`studio_readout_fx {at?, field?, value?, new?, remove?}` and `studio_readout_show`.

## Marks on the map (`studio_marks.cpp`)

The Looks workspace's nav ends with Marks on the map: types.json's `marks`, the looks a mod's
scripts put on the ground marks `hx.area` draws (`docs/mod-data.md`, "The looks a mark wears"). A
look is made by its name beside the list, as a look of looks.json is.

A look's page is its line, the chips saying how many blocks it has and whether it stands over the
units, and then its blocks as a row: the mark's own values it asks for, then field, cells, foot,
tear, ripple, outside and minimap. The one picked is written out a field a row, each with its
editor, the range the loader accepts, the value the block draws with when the field is left out,
and a line of what it does; a field set has an x that takes it away again, which leaves the block
drawing with the engine's own. A block is added or taken away whole, since a block the look leaves
out is not drawn at all. The cells' rows are 1 to 8 heights, added and taken off at the end.

The ranges are the loader's own (`port_moddata.cpp`'s `MarkField` and the rest), written as one
table that the editor and the look the preview builds both read, and the editor holds every value
inside its range: a value outside stops the whole mod as it loads rather than being passed over.
A look whose file was edited elsewhere is checked by the same table, and what the loader would
refuse is drawn above the blocks in red, each row naming the path (`marks.ward.cells.spacing takes
a number from 0 to 1`); the loader's own refusal names the line of types.json instead
(`PortJsonLineAt`), which the model, holding a read tree, no longer has the text for.

See it in the game puts a real mark in the studio's canvas game, which runs in this process, so
the look is drawn as a game draws it: a channel command of its own (`studio_mark_show`) runs on
the game's thread and calls `PortMapMarkSet`, and the page sets the mark again each second while
it is open, so an edit shows at once and a game started since gets it back. The shape, the radius,
the fade with its distance, the crossing and the minimap are the mark's own values, which a script
says and a look only asks for; walking one of your own units near the edge shows the fade, across
it the tear and the ripple, and outside a closed mark the light outside. Under it the minimap's
own picture of the mark, drawn as that palettized bitmap draws it: the nearest palette entry, the
block's own minimap pixels, and nothing of the fade or the beat.

Commands: `studio_marks` (every look with its blocks and how many values would be refused),
`studio_mark {name}`, `studio_mark_set {name, block?, field?, value?, remove?}` (a look made, a
field or a whole block set, or a block or the look taken away; a number outside its range is
refused here rather than written) and `studio_mark_show {name?, off?, shape?, radius?, fade?,
crossing?, minimap?, at?}`. `studio_looks_show` takes `page: "marks"` and `mark`.

## Mode (`studio_modes.cpp`, `studio_modeshow.cpp`)

Workspace 14, Mode: the mod as a game mode (`docs/mod-data.md`, "A game mode"). A mod whose kind
is not `mode` is told what one is and offered Make this mod a game mode, which sets the kind and
starts an empty mode block. The rest is the views below.

Overview is the block's own words and shape: its line, its paragraph, what it needs, whether it
changes the simulation, whether it brings an enemy player of its own (`enemy_player`), the players
it is played with, and its notes, each one added or taken
away. Under them, In the Game type list is how the set-up's list shows it (`game_type`): the name
it is called there, the mod its row names, and the classic type a game of it is launched as,
picked from the thirteen the set-up writes. What it calls is the two functions a mode names past
its settings': its `preview`, and the director, which is `types.json`'s `game` `director` block
and not the mode's, so it is written through the types document; each opens its function in
Scripts. Settings is the list of what the player sets, each with its id and its kind as a chip, and
the one picked written out: its kind (choice, number, toggle, list, seed), its name, group and
gist, a number's least, most, step and unit, a choice's or a list's items each with an id and a
name, and the three functions the set-up calls as the player changes things (`effect`, `read`,
`shown`). Presets is the same for the named sets of values, each value typed as the mode's own
JSON and left out when it is cleared. Everything is written through `StudioPut` into mod.json's
`mode` block, so every change is one edit the undo takes back, and a line typed is one step and
not a step a letter.

Set-up and Preview (`studio_modeshow.cpp`) show what a player meets, from the mode's own Luau.
Both read the mod through `StudioModeDecl`, which makes the declaration the engine's own calls
take (`PortModeDecl`) out of the studio's model rather than out of the folder: so a word or a
number typed shows at once, while the Luau those calls run is the mod folder's and shows once a
script is saved. Read again drops what the calls have answered (`PortModeForget`), since each
answer is kept by the function, the settings and the map. The values a player would set are the
two views' own, shared between them and starting at the bases; neither view changes the mod.

Set-up is the mode's tab as the set-up draws it: its head with the name the Game type list calls
it and the mod its row names, a card of its paragraph with how many play it, whether it changes
the simulation, its own enemy and what it needs, its presets as buttons, and then the settings by
group with each one's control of its kind, its gist, and the sentences its `effect` and `read`
give for the value as it stands, each with the function it came from. Past what a game shows, it
lists the settings `shown` answers false for, which is the only place a modder sees one at all.
The map and the players seated are set above, since every call of the mode's is given them.

Preview draws what the mode's `preview` makes of a map: the map's own picture, its starts by slot
and its resource clusters, and the preview's shapes over them in the kinds the set-up's map box
draws (circle, ring, rect, polygon, line, each with its fill, edge, width and dash), in map pixels
brought down by the picture's scale. Under it the map's line, the caption the set-up puts there
("Starts are drawn at random, so a place here is not a player's", which the studio always says,
having no seating), and every shape's numbers in a line. A mode that names no preview, or whose
call gives nothing back, says so.

AI is what the mode plays its units with, read from the mod itself: every unit whose scripts name
a brain, every card button that names a call of its own, and the buttons that name one of the
game's own finders, each with how often it runs; a row opens its function in Scripts. Under them,
what they chose in the game Test started: while the view is open the studio asks that game's
channel for its trace (`script_trace`, which the Test game is started with `--mode-trace` so the
rows begin at the first frame), and each call is a line — the frame, the unit, the function, and
the order it gave or the reason it gave none. The last 400 are kept, newest first, and Clear
empties them. Reading them changes nothing in the game.

Test plays the mode's own game: the mod saved and Hyperion started as the race page's Play it
starts it (`StudioPlayWith`, `studio_racetest.cpp`), with `--mode`, the settings the view holds as
`--mode-settings` and the seed as `--mode-seed`. The settings start at their bases, a preset fills
them in one go, and each is the mode's own JSON. A mode that can start a game part way through
declares a wave to start at (`start_wave`, which it never shows a player); where it does, the
view draws it first, as Start at wave, and sends it with the rest.

Roll runs the mode's `generate` for a seed, a map and the settings as they stand
(`ModDataModeRoll`, which reads nothing but what it is given and plays no game), and shows what
came back: the run and the records it made, as trees. The seed is shown and kept, so a roll is
repeated by rolling again; Draw a seed takes a new one. The mode's scripts are the ones the push
loaded, so the mod is pushed into the studio's game first; without a push the roll says it has no
generate. Commands: `studio_mode` (the block, with its `game_type`, `enemy_player`, `players`,
`preview` and its director), `studio_mode_set` (a field of the block, or `director` and
`director_every`, which go into types.json's `game` block), `studio_mode_setting`,
`studio_mode_setting_delete`, `studio_mode_preset`, `studio_mode_preset_delete`,
`studio_mode_roll`, `studio_mode_ai`, `studio_mode_test`, `studio_modes_show` (its `view` takes
`set-up` and `preview` as well), `studio_mode_seen {setting?, value?, players?, map?, preset?,
bases?}` (every setting with its value, whether it is shown, and the two sentences the mode gives
for it) and `studio_mode_preview {map?, settings?}` (the shapes the preview gave back, each in a
line). The MCP server has a tool for each, and the Claude panel is given them all but
`studio_mode_test`, which starts a game.

## The Claude panel (`studio_claude.cpp`)

The dock's Claude tab (the toolbar's Claude, or View, Ask Claude) asks the modder's own Claude Code
in words; the tab says so beside the words box, and nothing runs until they ask. Each ask starts
`claude -p` as a child process of the studio, in the mod's `.studio\claude\`, with:

- `--output-format stream-json --verbose --include-partial-messages`, read line by line: Claude's
  words stream into the tab, and each tool call is a line in words ("set Iron Trooper's minerals to
  54") with its state: done, refused by the studio (with the studio's why), or not given;
- `--mcp-config .studio\claude\mcp.json --strict-mcp-config`: one MCP server, `hyperion-studio`
  (`tools/control/hyperion_mcp.py`, found from the studio's folder up, run by Python), on this
  studio's channel port, with `HYPERION_MCP_TOOLS` naming the tools it shows: the studio's tools that
  read the mod, edit it and point the window at something. `studio_save`, `studio_push`,
  `studio_undo` and `studio_redo` are shown but not given, so Claude says plainly that the modder does
  them; nothing that brings in or writes out a file, starts a game or opens a mod is shown;
- `--tools "" --allowedTools <the tools given> --permission-mode dontAsk`: no built-in tools and no
  questions; `--setting-sources project --disable-slash-commands`; `--system-prompt` saying what the
  studio is and how to edit through it; `--model` when one is picked (Sonnet, Opus, Haiku, or Claude
  Code's own);
- `--session-id <a new id>` for a new conversation, `--resume <id>` to go on with one.

The words go in on stdin after a line in brackets saying what the studio shows: the mod, the record
open, the workspace and the mode. Each of the first three is a chip above the words box, left out
when clicked.

**Show me first** (the default) and **Let it edit.** An ask's edits are one group (the model's
Claude's asks, above). In Show me first the group is a proposal, shown at the tab's right as rows:
record, field, before (the base's value, and whose, when the record gave none), after, and why the
record would no longer load when a change breaks it. Accept all makes it an ordinary group, one
Undo; a row's Accept makes that row a group of its own and the rest the proposal again; a row's Undo
takes it out of the proposal; Undo all undoes it. A follow-up ask adds to the proposal waiting. In
Let it edit the edits are made at once, one group, and the tab lists Claude's last edits with Undo
them (one undo, unless the modder edited between Claude's calls).

**Stop** ends the process and what it started (they are in a job, which the studio's end ends too);
what Claude changed before it stopped is kept, as one change.

**Conversations.** `.studio\claude\conversations.json` lists a mod's (id, title, when, asks) and
`<id>.json` keeps what the tab showed; the pulldown at the tab's head opens one, starts a new one,
clears one or all. Claude Code keeps its own record of each session as well. `.studio\` is the
studio's own folder in the mod, which the game does not read.

When Claude Code is not found (on the PATH, in `%USERPROFILE%\.local\bin` or `%APPDATA%\npm`), the
tab says how to install it and the rest of the studio works as before.

Commands: `studio_claude_status`, `studio_claude_ask {text, mode?, model?, new?}` (mode `show` or
`edit`), `studio_claude_stop`, `studio_claude_transcript`, `studio_claude_pending` (the rows, each as
JSON and in words), `studio_claude_accept {row?}`, `studio_claude_undo {row?}` (all, or one row),
`studio_claude_mode {mode?, model?}`, `studio_claude_conversations {open? | clear? | all? | new?}`, and
`studio_claude_use {program}`: a Claude Code run in place of the one found, "" for that one again,
"none" as if none were installed. `tools/studio_claude_fake.py` stands in for Claude Code in
`tools/studio_test.py`, so its steps spend nothing. The MCP server has a tool for each but the last.
