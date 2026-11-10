// port_moddata.cpp -- a mod's data: type rows from types.json, image scripts from iscript.txt
// (port_iscript.cpp) and their art (port_moddata.h; docs/mod-data.md; docs/modules/data.md).
//
// A record names the original's row or mod record it starts from ("base") and gives the fields
// it changes, by the names below. Each gets the next id of its table past the original's
// (h/typelimits.h), in the order of the records' names. Rows are built in 1.17's layout and
// stored as the file's are (PortDataStore), at start-up for every table but techdata, whose
// rows are 1.17's unused ones, filled for each game that has the mod. A record with "edit"
// changes a row that is there already: an earlier submission's record in place, or
// one of the original's rows, written for each game that has the mod and put back for any
// other (ModDataForGame).
//
// Sounds and portraits are what is heard and seen only: a mod's rows are
// there from start-up, and their files are read from the mod's folder (ModDataFilePath). Their
// paths, sfxdata.tbl's and portdata.tbl's strings in the original, are held as the names the
// game reads, in the record's column, so a row is built, edited and stored as any other.
//
// The mods a mod depends on load first, each a submission of its own (port_mods.h's
// PortModChain). A push (ModDataPush) takes ids, and its scripts their place, after
// every earlier submission's, so a unit on the map keeps its rows and a name finds the newest;
// techs and upgrades, with few free rows, keep an earlier same-named record's row. Each game
// starts from the data loaded at start-up, so no push reaches another game, a replay or a save.
#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "../../h/typelimits.h"
#include "../platform/port_log.h"  // PortLogger
#include "../platform/port_util.h"  // PortReadFile
#include "../../h/playertables.h"
#include "../../h/net_cmds.h"  // PlayerData
#include "../../h/gamedata.h"  // array_data_t
#include "../../h/saveload.h"  // set_tech_available, set_tech_researched
#include "../../h/Storm.h"     // SMemAlloc, SFileLoadFile
#include "port_formats.h"  // PortGroupNative
#include "port_imagefile.h"  // PortImageDecode, a sheet's pictures
#include "port_modsheet.h"   // ModSheetGroup
#include "port_modtileset.h"  // ModTilesetLoad, a tileset's changes
#include "port_modlang.h"    // ModLangApply, the player's language's text
#include "../render/port_modart.h"  // PortModArtSet, PortModUiSet: art at its own resolution
#include "../render/port_mapmark.h"  // the looks types.json gives the marks a script draws
#include "../platform/port_paths.h"  // PortCommandOptions
#include "../platform/port_standalone.h"  // PortDataBytes, PortDataGrown, PortDataRow, PortDataStore
#include "port_typenames.h"  // TypeNameFind, TypeNameOf: the original's rows by name
#include "port_mode.h"       // the game's mode, its settings and seed
#include "port_modemap.h"    // the map read a mode's generate is given
#include "../render/port_looks.h"  // PortLooksModeFile, PortLooksModeLooks
#include "../../h/hxrules.h"       // RulesCampaignMission, the game's rule options
#include "../../h/Teams.h"          // IsTeamGame
#include "../../h/point16.h"       // point16_t, the start locations
#include "port_video.h"  // PortVideoExtensions, a portrait's videos
#include "port_iscript.h"
#include "../platform/port_json.h"
#include "port_moddata.h"
#include "port_mods.h"
#include "../gameplay/port_session.h"  // Session
#include "port_moddata_push.h"
#include "port_modcheck.h"  // ModCheckFile
#include "port_modstatus.h"  // ModStatusField, ModStatusLoad: statuses, auras, blocks
#include "../hud/port_readout.h"  // PortReadoutLoad: the mod's unit readout
#include "port_modlua.h"     // ModLuaField, ModLuaLoad: the scripts
#include "port_modrules.h"  // ModRulesRead: a race's building rules and its advisor
#include "port_modvoice.h"  // ModSoundSubtitle, ModSoundGap
#include "port_stormapi.h"  // SFileUnloadFile
#include "../platform/port_globals117.h"
#include <SDL3/SDL.h>  // SDL_LoadPNG_IO, a mod's icons and wireframes
#ifdef HX_DESIGN
#include "../../design/design_hooks.h"  // DesignModColumn
#endif

extern array_data_t gWEAPONSArrayDataTbl_g[25] OC_UNALIGNED;
extern array_data_t gFLINGYArrayDataTbl_g[8] OC_UNALIGNED;
extern array_data_t gSPRITESArrayDataTbl_g[7] OC_UNALIGNED;
extern array_data_t gIMAGESArrayDataTbl_g[15] OC_UNALIGNED;
extern array_data_t gORDERSArrayDataTbl_g[20] OC_UNALIGNED;
extern array_data_t gTECHDATAArrayDataTbl_g[12] OC_UNALIGNED;
extern array_data_t gSFXDATAArrayDataTbl_g[6] OC_UNALIGNED;
extern array_data_t gPORTDATAArrayDataTbl_g[7] OC_UNALIGNED;
extern WORD sgTree[546] OC_UNALIGNED;  // the classic units' requirements, TechTree.cpp
void snd_free_snd(int nSoundId);  // snd.cpp: a sound's buffer let go, read again when next played

// The images' frames and overlay offsets (CImage.cpp), in storage that holds a mod's.
struct GROUP;
struct lo_file_t;
extern GROUP *sgpImgGroups[IMAGE_TYPES_MAX] OC_UNALIGNED;
extern lo_file_t *sgpOverlayGroups[5 * 999];
extern const lo_file_t *sgpShieldOverlayGroups_g[999] OC_UNALIGNED;
extern const lo_file_t *g_hxModOverlays[6][IMAGE_TYPES_MAX - IMAGE_TYPES_117];
extern BYTE g_hxModImage32[IMAGE_TYPES_MAX - IMAGE_TYPES_117];  // CImage.cpp: an image of 32 directions
extern BYTE sgbNumDmgOverlays_g[IMAGE_TYPES_MAX] OC_UNALIGNED;

bool g_hxModGame = false;
int ModUnitNewest(unsigned nType);

// The players' tables a race reaches (TechTree.cpp, maphdr.cpp).
extern PlayerUnitsAvailableTable PlayerUnitsAvailable;
extern DWORD gPlayback;       // 0x67CC90
extern void *gpLoadGameFile;  // 0x6A5E34
extern DWORD g_LocalNationID;
extern PlayerTable<PlayerData, PlayerTagData> gPlayerData;  // 0x6726C8
extern PlayerTable<point16_t, PlayerTagStartLocations, 8> gStartLocations;  // 0x534A00

namespace {

struct Column {
  const char *psz;
  const char *pszRef;  // the table a name in it names, or null for a number
  int nScale;          // the file's value is this many times the record's (hit points)
  long long nMost;     // values from 0 to one short of this, or any when 0: a column the engine
                       // indexes a table of its own with
};

#define C(name) {name, nullptr, 1, 0}
#define R(name, ref) {name, ref, 1, 0}
#define N(name, most) {name, nullptr, 1, most}

const Column kUnitColumns[] = {
    R("flingy", "flingy"), R("subunit", "units"), R("subunit2", "units"), R("infestation", "units"),
    R("construction_image", "images"), C("direction"), C("shields_enabled"), C("shields"),
    {"hit_points", nullptr, 256, 0}, C("elevation"), C("movement_flags"), C("rank"),
    R("ai_idle_order", "orders"), R("idle_order", "orders"), R("return_to_idle_order", "orders"),
    R("attack_unit_order", "orders"), R("attack_move_order", "orders"), R("ground_weapon", "weapons"),
    C("max_ground_hits"), R("air_weapon", "weapons"), C("max_air_hits"), C("ai_internal"),
    C("flags"), C("target_range"), C("sight_range"), R("armor_upgrade", "upgrades"), C("size"), C("armor"),
    C("right_click"), R("ready_sound", "sounds"), R("what_sound_first", "sounds"), R("what_sound_last", "sounds"),
    R("pissed_sound_first", "sounds"), R("pissed_sound_last", "sounds"), R("yes_sound_first", "sounds"),
    R("yes_sound_last", "sounds"), C("placement_box"), C("addon_position"), C("bounds"), R("portrait", "portraits"),
    C("minerals"), C("gas"),
    C("build_time"), C("requirements"), C("group_flags"), C("supply_provided"),
    C("supply_required"), C("space_required"), C("space_provided"), C("build_score"),
    C("destroy_score"), C("map_string"), C("broodwar"), C("editor_flags"),
};
const Column kWeaponColumns[] = {
    C("label"), R("flingy", "flingy"), C("unused"), C("target_flags"), C("min_range"),
    C("max_range"), R("damage_upgrade", "upgrades"), C("damage_type"), C("behavior"), C("remove_after"),
    C("explosion"), C("inner_splash"), C("middle_splash"), C("outer_splash"), C("damage"),
    C("damage_bonus"), C("cooldown"), C("damage_factor"), C("attack_angle"), C("launch_spin"),
    C("forward_offset"), C("upward_offset"), C("target_error"), C("icon"),
};
const Column kFlingyColumns[] = {
    R("sprite", "sprites"), C("top_speed"), C("acceleration"), C("halt_distance"),
    C("turn_radius"), C("unused"), C("movement_control"),
};
const Column kSpriteColumns[] = {
    R("image", "images"), C("health_bar"), C("unknown"), C("visible"), C("selection_circle"),
    C("selection_offset"),
};
const Column kImageColumns[] = {
    C("grp"), C("turns"), C("clickable"), C("use_full_iscript"), C("draw_if_cloaked"),
    N("draw_function", 18), N("remapping", 8), R("script", "scripts"), C("shield_overlay"),
    C("attack_overlay"), C("damage_overlay"), C("special_overlay"), C("landing_dust_overlay"),
    C("liftoff_dust_overlay"),
};
const Column kOrderColumns[] = {
    C("label"), C("use_weapon_targeting"), C("secondary"), C("non_subunit"),
    C("subunit_inheritance"), C("subunit_can_use"), C("interruptible"), C("stop_moving"),
    C("can_be_queued"), C("keep_target_disabled"), C("can_be_obstructed"), C("fleeable"),
    C("requires_movable"), R("weapon", "weapons"), R("tech", "techs"), N("animation", 28),
    C("highlight"), C("requirements"), R("obscured", "orders"),
};
const Column kTechColumns[] = {
    C("minerals"), C("gas"), C("research_time"), C("energy"), C("requirements"),
    C("use_requirements"), C("icon"), C("label"), C("race"), C("researched"), C("broodwar"),
};
const Column kUpgradeColumns[] = {
    C("minerals"), C("minerals_factor"), C("gas"), C("gas_factor"), C("time"), C("time_factor"),
    C("requirements"), C("icon"), C("label"), C("race"), C("max_level"), C("broodwar"),
};
// sfxdata.dat: the file under sound\, and how the game plays it
const Column kSoundColumns[] = {
    C("file"), C("priority"), C("flags"), C("length_adjust"), C("min_volume"),
};
// portdata.dat: the idle and talking videos under portrait\, a name to which the game adds
// the variant's digit and .smk, and the chances in 100 of the four idle ones
const Column kPortraitColumns[] = {
    C("idle"), C("talking"), C("idle_chance_0"), C("idle_chance_1"), C("idle_chance_2"), C("idle_chance_3"),
};
#undef C
#undef R
#undef N

// The flags' names, bit 0 first (units.dat's special ability flags).
const char *const kUnitFlags[32] = {
    "building", "addon", "flyer", "worker", "subunit", "flying_building", "hero", "regenerate",
    "animated_idle", "cloakable", "two_in_egg", "single_entity", "resource_depot",
    "resource_container", "robotic", "detector", "organic", "requires_creep", "unused",
    "requires_psi", "burrowable", "spellcaster", "permanent_cloak", "pickup_item",
    "ignores_supply", "medium_overlays", "large_overlays", "battle_reactions", "auto_attack",
    "invincible", "mechanical", "produces_units",
};

// A sound's flags (sfxdata.dat's), bit 0 first: kept loaded, one at a time for a unit type,
// two bits the game sets as it plays, one at a time for the sound, and not cut short by a
// sound of higher priority.
const char *const kSoundFlags[6] = {
    "preload", "one_per_unit", "", "", "one_at_a_time", "uninterrupted",
};

struct Table {
  const char *psz;
  const char *pszFile;
  array_data_t *pDesc;
  const Column *pColumns;
  int nColumns;
  int nRows117;
  int nModFirst;
  int nModMax;  // the most rows the table's storage holds
};

#define T(name, file, desc, cols, rows, first, max) \
  {name, file, desc, cols, static_cast<int>(sizeof cols / sizeof cols[0]), rows, first, max}
const Table kTables[] = {
    T("units", "arr\\units.dat", gUnitsArrayDataTbl, kUnitColumns, UNIT_TYPES_117, UNIT_TYPE_MOD_FIRST, UNIT_TYPES_MAX),
    T("weapons", "arr\\weapons.dat", gWEAPONSArrayDataTbl_g, kWeaponColumns, WEAPON_TYPES_117, WEAPON_TYPE_MOD_FIRST, WEAPON_TYPES_MAX),
    T("flingy", "arr\\flingy.dat", gFLINGYArrayDataTbl_g, kFlingyColumns, FLINGY_TYPES_117, FLINGY_TYPE_MOD_FIRST, FLINGY_TYPES_MAX),
    T("sprites", "arr\\sprites.dat", gSPRITESArrayDataTbl_g, kSpriteColumns, SPRITE_TYPES_117, SPRITE_TYPE_MOD_FIRST, SPRITE_TYPES_MAX),
    T("images", "arr\\images.dat", gIMAGESArrayDataTbl_g, kImageColumns, IMAGE_TYPES_117, IMAGE_TYPE_MOD_FIRST, IMAGE_TYPES_MAX),
    T("orders", "arr\\orders.dat", gORDERSArrayDataTbl_g, kOrderColumns, ORDER_TYPES_117, ORDER_TYPE_MOD_FIRST, ORDER_TYPES_MAX),
    T("techs", "arr\\techdata.dat", gTECHDATAArrayDataTbl_g, kTechColumns, TECH_TYPES_117, TECH_TYPE_MOD_FIRST, TECH_TYPES_MAX),
    T("upgrades", "arr\\upgrades.dat", gUPGRADESArrayDataTbl, kUpgradeColumns, UPGRADE_TYPES_117, UPGRADE_TYPE_MOD_FIRST,
      UPGRADE_TYPES_MAX),
    T("sounds", "arr\\sfxdata.dat", gSFXDATAArrayDataTbl_g, kSoundColumns, SOUND_TYPES_117, SOUND_TYPE_MOD_FIRST,
      SOUND_TYPES_MAX),
    T("portraits", "arr\\portdata.dat", gPORTDATAArrayDataTbl_g, kPortraitColumns, PORTRAIT_TYPES_117,
      PORTRAIT_TYPE_MOD_FIRST, PORTRAIT_TYPES_MAX),
};
#undef T
enum { UNITS, WEAPONS, FLINGY, SPRITES, IMAGES, ORDERS, TECHS, UPGRADES, SOUNDS, PORTRAITS, TABLES };

// The columns that hold a path, the first of sfxdata.dat's and two of portdata.dat's: an
// index into its .tbl in the file, the path's text in the game.
int PathColumns(int t) {
  return t == SOUNDS ? 1 : t == PORTRAITS ? 2 : 0;
}
const char *TblOf(int t) {
  return t == SOUNDS ? "arr\\sfxdata.tbl" : t == PORTRAITS ? "arr\\portdata.tbl" : nullptr;
}

// Where a column sits in its file, and which rows it has: most have one a type; sprites.dat
// has columns from sprite 130 (the selectable ones), and units.dat columns for the units
// below 106 (sounds) and for the buildings from 106 (infestation, add-on positions).
struct Geometry {
  size_t offset;
  DWORD rowBytes;
  DWORD elemSize;
  int first;
  int rows;
  // a row's bytes and an element's in a record and in the build's column: the file's, or
  // more in a column of ids the build keeps wide (h/typelimits.h)
  DWORD recBytes;
  DWORD recElem;
};

struct Record {
  std::string name;
  const PortJson *pJson = nullptr;
  int id = -1;
  std::vector<std::vector<BYTE>> cols;  // the row, column by column, in the build's widths (recBytes)
  int state = 0;                        // 0 not built, 1 being built, 2 built
  int base117 = -1;                     // the 1.17 row its bases start from
  int sub = 1;                          // the submission it came in: 1 at start-up, then pushes
};

struct ModTable {
  std::vector<BYTE> file;
  std::vector<std::string> tbl;  // the strings of the .tbl its paths index (PathColumns)
  std::vector<Geometry> geo;
  std::vector<Record> recs;
  std::vector<Record> edits;  // the original's rows as edits change them, the newest last
};

// Requirements in 1.17's tree form (TechTree.cpp's sgTree), by the unit, tech or upgrade: a
// mod's own rows', from their records, and those an edit gives one of the original's rows,
// which keep the original's other checks (ModTreeEdit).
struct Trees {
  std::map<int, std::vector<WORD>> own[MOD_TREE_KINDS];
  std::map<int, std::vector<WORD>> edit[MOD_TREE_KINDS];
};

std::string g_id;
std::string g_name;     // its mod.json's name, ModDataName
std::string g_depends;  // the mods with data it depends on, "id version", in load order (ModDataDepends)
BYTE g_abFingerprint[20];
std::deque<PortJson> g_typesKept;  // each submission's types.json, which its records point into
PortJson *g_pTypes;                // the one being loaded
#define g_types (*g_pTypes)
ModTable g_tables[TABLES];
std::map<std::string, int> g_scripts;  // a script's name, its id
int g_nScripts;                        // the scripts loaded, whose ids run from 1000
std::vector<DWORD> g_scriptHeaders;    // their headers' offsets, by id from 1000
int g_nSubs;                           // the submissions loaded
HxTypeCounts g_counts;
std::string g_error;
Trees g_trees;
std::map<int, std::string> g_unitNames;    // the name an edit gives a unit, ModUnitName and ModUnitEditName
std::vector<std::string> g_strings;        // ModString
std::vector<ModCardButton> g_buttons;      // ModCardButtons
// A button's new words for an original button (ModCardButton.nWords): its hotkey and text as
// "cards" gives them, and the string made of them once the original's kind is known.
struct Words {
  std::string hotkey, text;
  WORD wStr = 0;
};
std::vector<Words> g_words;
std::vector<std::string> g_cardAiCalls;    // the functions a button's "ai" names (ModCardAiCall)
bool g_bAddCards;          // a submission's cards come beside the earlier ones, not in place of them
std::string g_loadKey;     // the submission being loaded: its mod's id, which its paths are named by
std::string g_loadFolder;  // and its data folder
std::map<std::string, std::string> g_mediaFolders;  // each mod's data folder by its id (ModDataFilePath)
std::mutex g_mediaLock;                             // which the game's file reads take on any thread

const char *Lasting(const std::string &text);

// The sounds and portraits, whatever the game: 1.17's and the mod's.
void MediaCountsSet() {
  g_hxSoundTypes = SOUND_TYPE_MOD_FIRST + static_cast<int>(g_tables[SOUNDS].recs.size());
  g_hxPortraitTypes = PORTRAIT_TYPE_MOD_FIRST + static_cast<int>(g_tables[PORTRAITS].recs.size());
}

// The game's types from the tables' records (g_hxTypes): a table without a mod's rows has
// 1.17's count, one with them its first mod row's plus theirs.
void CountsSet() {
  auto count = [](int t) {
    return g_tables[t].recs.empty() ? kTables[t].nRows117
                                    : kTables[t].nModFirst + static_cast<int>(g_tables[t].recs.size());
  };
  g_counts = {count(UNITS),  count(WEAPONS), count(FLINGY), count(SPRITES),
              count(IMAGES), count(ORDERS),  count(TECHS),  count(UPGRADES)};
  MediaCountsSet();
}

// "races": a race plays its base's way, with its own name, start units and units.
struct Race {
  std::string id;    // its record's name
  std::string name;  // the name a player sees
  int nBase;         // 0 zerg, 1 terran, 2 protoss
  bool bOwnStart;    // "start" given: these start units in place of the base's
  std::vector<std::pair<int, int>> start;  // a unit and how many
  std::vector<int> units;    // the mod's units that are this race's alone
  std::vector<int> without;  // 1.17's units its players do not have
  std::vector<std::pair<int, int>> ai;  // a unit 1.17's AI scripts name, and what its AI makes
  std::vector<std::pair<int, int>> aiTechs;     // a tech they research, and what its AI researches
  std::vector<std::pair<int, int>> aiUpgrades;  // an upgrade, and what its AI upgrades
  int nConsole = -1;  // the race whose console it shows, -1 its base's
  int nMusic = -1;    // the race whose music it plays, -1 its base's
  ModRaceRules rules;  // how its workers build, and its advisor ("rules", "advisor")
};
std::vector<Race> g_races;
BYTE g_slotRace[PLAYER_SLOTS_MAX];      // the game's, ModRaceOf
BYTE g_recordedRaces[PLAYER_SLOTS_MAX]; // the replay's or save's read last

const PortLogger Log("hyperion_mods.log");  // port_mods.cpp's too

bool Fail(const std::string &what) {
  g_error = what;
  return false;
}

// What a load passes over and logs, kept for --mod-check's report while it runs.
std::vector<std::string> g_checkNotes;
std::map<int, std::string> g_subFolders;  // each submission's data folder, for the check

void Note(const std::string &what) {
  Log("mods: %s", what);
  if (PortCommandOptions()->szModCheck[0] != '\0')
    g_checkNotes.push_back(what);
}

bool LoadTableFile(int t) {
  const Table &tab = kTables[t];
  ModTable &m = g_tables[t];
  void *pData = NULL;
  DWORD cb = 0;
  if (!SFileLoadFile(tab.pszFile, &pData, &cb, 0, NULL) || !pData)
    return Fail(std::string("the original's ") + tab.pszFile + " does not read");
  m.file.assign(static_cast<BYTE *>(pData), static_cast<BYTE *>(pData) + cb);
  SFileUnloadFile(pData);
  size_t at = 0;
  for (int c = 0; c < tab.nColumns; ++c) {
    const array_data_t &d = tab.pDesc[c];
    Geometry g;
    const DWORD bytes = d.dwCount * d.dwElemSize;
    g.offset = at;
    // 1.17's data holds the element's size at +4, the field gamedata.h names dwCount, and
    // the rows at +8 (units.dat's flingy column: 1 and 228)
    g.elemSize = d.dwCount;
    if (bytes % tab.nRows117 == 0) {
      g.first = 0, g.rows = tab.nRows117;
    } else if (t == SPRITES && bytes % (SPRITE_TYPES_117 - SPRITE_SELECTABLE_FIRST) == 0) {
      g.first = SPRITE_SELECTABLE_FIRST, g.rows = SPRITE_TYPES_117 - SPRITE_SELECTABLE_FIRST;
    } else if (t == UNITS && bytes % 96 == 0) {
      g.first = 106, g.rows = 96;
    } else if (t == UNITS && bytes % 106 == 0) {
      g.first = 0, g.rows = 106;
    } else {
      return Fail(std::string(tab.pszFile) + " has a column of rows this build does not know");
    }
    g.rowBytes = bytes / g.rows;
    g.recBytes = PortDataBytes(d.pDest, g.rowBytes);
    g.recElem = g.recBytes / (g.rowBytes / g.elemSize);
    m.geo.push_back(g);
    at += bytes;
  }
  if (at != m.file.size())
    return Fail(std::string(tab.pszFile) + " is not the size the original's descriptors give it");
  // a .tbl: its count, an offset for each string, the strings
  if (const char *pszTbl = TblOf(t)) {
    if (!SFileLoadFile(pszTbl, &pData, &cb, 0, NULL) || !pData)
      return Fail(std::string("the original's ") + pszTbl + " does not read");
    const BYTE *p = static_cast<BYTE *>(pData);
    const DWORD n = cb >= 2 ? (p[0] | p[1] << 8) : 0;
    for (DWORD i = 0; i < n && 2 + 2 * i + 1 < cb; ++i) {
      const DWORD off = p[2 + 2 * i] | p[3 + 2 * i] << 8;
      DWORD end = off;
      while (end < cb && p[end] != 0)
        ++end;
      m.tbl.push_back(off < cb ? std::string(reinterpret_cast<const char *>(p) + off, end - off) : std::string());
    }
    SFileUnloadFile(pData);
  }
  return true;
}

// A column's index by its name; -1 for none.
int ColumnOf(int t, const char *psz) {
  for (int c = 0; c < kTables[t].nColumns; ++c)
    if (strcmp(kTables[t].pColumns[c].psz, psz) == 0)
      return c;
  return -1;
}

// A path column's value: the name the game reads, held for the run, in the column's width.
void PutPath(std::vector<BYTE> &col, const char *psz) {
  col.assign(col.size(), 0);
  memcpy(col.data(), &psz, col.size() < sizeof psz ? col.size() : sizeof psz);
}

const char *PathOf(const std::vector<BYTE> &col) {
  const char *psz = nullptr;
  memcpy(&psz, col.data(), col.size() < sizeof psz ? col.size() : sizeof psz);
  return psz;
}

int TableIndex(const char *psz) {
  for (int t = 0; t < TABLES; ++t)
    if (strcmp(kTables[t].psz, psz) == 0)
      return t;
  return -1;
}

// A row by name: a mod's record, the newest submission's, which come last; else the original's
// (port_typenames.h).
int Find(int t, const std::string &name) {
  int nFound = -1;
  for (const Record &r : g_tables[t].recs)
    if (r.name == name)
      nFound = r.id;
  return nFound >= 0 ? nFound : TypeNameFind(kTables[t].psz, name.c_str());
}

int Resolve(const char *pszRef, const PortJson &v, const std::string &where) {
  if (v.kind == PortJson::NUMBER)
    return static_cast<int>(v.d);
  if (v.kind != PortJson::STRING || !pszRef)
    return -1;
  if (strcmp(pszRef, "scripts") == 0) {
    auto it = g_scripts.find(v.s);
    return it == g_scripts.end() ? -1 : it->second;
  }
  const int t = TableIndex(pszRef);
  const int n = t < 0 ? -1 : Find(t, v.s);
  if (n < 0)
    Note(where + " names no " + pszRef + " " + v.s);
  return n;
}

// A number a field gives for a row of another table: one of 1.17's rows, the row after them
// where that is the table's none (a unit's subunit, a weapon, an order, a tech, an upgrade), or
// one of the mod's. Past them the game would read beyond the table (sgpImgGroups and the rest).
bool RowExists(int t, long long n) {
  if (t == PORTRAITS && n == 0xFFFF)  // no portrait
    return true;
  const bool bNone = t != IMAGES && t != SPRITES && t != FLINGY && t != SOUNDS && t != PORTRAITS;
  if (n >= 0 && (n < kTables[t].nRows117 || (bNone && n == kTables[t].nRows117)))
    return true;
  for (const Record &r : g_tables[t].recs)
    if (r.id == n)
      return true;
  return false;
}

void Put(std::vector<BYTE> &col, DWORD elemSize, int k, long long v) {
  for (DWORD b = 0; b < elemSize; ++b)
    col[k * elemSize + b] = static_cast<BYTE>(v >> (8 * b) & 0xFF);
}

bool Build(int t, Record &r);

// A sound's file or a portrait's videos, a path in the mod's data folder: as the game reads
// them, hxmod\<mod>\<path> under sound\ or portrait\ (ModDataFilePath). One the folder does
// not have is logged, and plays as a missing file of the original's does: not at all.
// v as a path in the mod's data folder, with backslashes: none that climbs out of it.
bool DataPath(const PortJson &v, const std::string &where, std::string *pPath) {
  if (v.kind != PortJson::STRING || v.s.empty())
    return Fail(where + " is a path in the mod's data folder");
  std::string &path = *pPath = v.s;
  std::replace(path.begin(), path.end(), '/', '\\');
  if (path.find("..") != std::string::npos || path[0] == '\\' || path.find(':') != std::string::npos)
    return Fail(where + " is a path in the mod's data folder");
  return true;
}

bool MediaPath(int t, int c, const PortJson &v, const std::string &where, std::vector<BYTE> *pCol) {
  std::string path;
  if (!DataPath(v, where, &path))
    return false;
  bool bThere = false;
  if (t == SOUNDS) {
    bThere = GetFileAttributesA((g_loadFolder + path).c_str()) != INVALID_FILE_ATTRIBUTES;
  } else {
    bThere = GetFileAttributesA((g_loadFolder + path + "0.smk").c_str()) != INVALID_FILE_ATTRIBUTES;
    for (const char *const *ppszExt = PortVideoExtensions(); *ppszExt && !bThere; ++ppszExt)
      bThere = GetFileAttributesA((g_loadFolder + path + "0" + *ppszExt).c_str()) != INVALID_FILE_ATTRIBUTES;
  }
  if (!bThere)
    Note(where + " names " + path + ", which the mod's folder does not have" +
         (t == SOUNDS ? "" : " (with 0 and .webm, .mkv, .mp4, .m4v, .mov or .smk after it)"));
  PutPath(*pCol, Lasting("hxmod\\" + g_loadKey + "\\" + path));
  (void)c;
  return true;
}

// A unit's own command icon, wireframe and transport wireframe, from its record's icon,
// wireframe and transport_wireframe: each a PNG or a GRP in the mod's data folder, kept as the
// game names a mod's files (hxmod\<mod>\<path>, ModDataFilePath), or a unit whose art it shows.
// Drawn only, so not in the fingerprint. The art is built for each game (ArtBuild).
const char *const kArtFields[3] = {"icon", "wireframe", "transport_wireframe"};
struct UnitArt {
  std::string file[3];
  int anFrom[3] = {-1, -1, -1};
};
std::map<int, UnitArt> g_unitArt;

bool ArtFile(const std::string &s) {
  for (const char *psz : {".png", ".grp"})
    if (s.size() > 4 && _stricmp(s.c_str() + s.size() - 4, psz) == 0)
      return true;
  return false;
}

// A unit record's art fields, over any it had: an edit changes those it gives.
bool UnitArtOf(const Record &r) {
  for (int k = 0; k < 3; ++k) {
    const PortJson &v = (*r.pJson)[kArtFields[k]];
    if (v.kind == PortJson::NONE)
      continue;
    const std::string where = "units " + r.name + "." + kArtFields[k];
    UnitArt &a = g_unitArt[r.id];
    if (v.kind == PortJson::STRING && ArtFile(v.s)) {
      std::string path;
      if (!DataPath(v, where, &path))
        return false;
      if (GetFileAttributesA((g_loadFolder + path).c_str()) == INVALID_FILE_ATTRIBUTES)
        Note(where + " names " + path + ", which the mod's folder does not have");
      a.file[k] = "hxmod\\" + g_loadKey + "\\" + path;
      a.anFrom[k] = -1;
    } else {
      const int n = Resolve("units", v, where);
      if (n < 0)
        return Fail(where + " is a PNG or GRP file in the mod's data folder, or a unit");
      a.file[k].clear();
      a.anFrom[k] = n;
    }
  }
  return true;
}

// The original's row b as its file has it, column by column, each element zero-extended to
// the build's width; a column without row b is zeros.
std::vector<std::vector<BYTE>> FileRow(int t, int b) {
  const ModTable &m = g_tables[t];
  std::vector<std::vector<BYTE>> cols(kTables[t].nColumns);
  for (int c = 0; c < kTables[t].nColumns; ++c) {
    const Geometry &g = m.geo[c];
    cols[c].assign(g.recBytes, 0);
    if (b < g.first || b >= g.first + g.rows)
      continue;
    const BYTE *pFile = &m.file[g.offset + (b - g.first) * g.rowBytes];
    if (c < PathColumns(t)) {
      const DWORD n = pFile[0] | pFile[1] << 8;
      PutPath(cols[c], n != 0 && n <= m.tbl.size() ? Lasting(m.tbl[n - 1]) : nullptr);
      continue;
    }
    for (DWORD k = 0; k < g.rowBytes / g.elemSize; ++k)
      memcpy(&cols[c][k * g.recElem], pFile + k * g.elemSize, g.elemSize);
  }
  return cols;
}

// The newest edit of the original's row b, or null.
const Record *EditOf(int t, int b) {
  const Record *pEdit = nullptr;
  for (const Record &e : g_tables[t].edits)
    if (e.id == b)
      pEdit = &e;
  return pEdit;
}

// A record's row before its fields: its base's, a mod's record or one of the original's rows
// as the mod's edits leave it.
bool BaseRow(int t, const PortJson &base, Record &r) {
  const Table &tab = kTables[t];
  ModTable &m = g_tables[t];
  int b = -1;
  Record *pBase = nullptr;
  if (base.kind == PortJson::STRING) {
    for (Record &o : m.recs)
      if (o.name == base.s)
        pBase = &o;
    if (pBase) {
      if (!Build(t, *pBase))
        return false;
      r.cols = pBase->cols;
      r.base117 = pBase->base117;
      return true;
    }
    b = Find(t, base.s);
  } else if (base.kind == PortJson::NUMBER) {
    b = static_cast<int>(base.d);
  } else if (t == SOUNDS || t == PORTRAITS) {
    b = 0;  // the first row, a sound's "none" (its fields give the rest) or the first portrait
  }
  if (b < 0 || b >= tab.nRows117)
    return Fail(std::string(tab.psz) + " " + r.name + " needs a base, an original row or one of the mod's");
  r.base117 = b;
  const Record *pEdit = EditOf(t, b);
  r.cols = pEdit ? pEdit->cols : FileRow(t, b);
  return true;
}

// An image's fields that describe the sheet its "grp" names (SheetGroup).
bool IsSheetField(const std::string &name) {
  return name == "team" || name == "frame" || name == "origin" || name == "scale" || name == "frames" ||
         name == "directions";
}

WORD AddString(const std::string &s);

// The fields a record gives, by the column names above, over its row.
bool Fields(int t, Record &r) {
  const Table &tab = kTables[t];
  const PortJson &j = *r.pJson;
  for (const auto &f : j.members) {
    // a weapon's, a tech's or an upgrade's own name: a string of the mod's for its label
    if (f.first == "name" && (t == WEAPONS || t == TECHS || t == UPGRADES)) {
      if (f.second.kind != PortJson::STRING || f.second.s.empty())
        return Fail(std::string(tab.psz) + " " + r.name + ".name takes a text");
      const int c = ColumnOf(t, "label");
      Put(r.cols[c], g_tables[t].geo[c].recElem, 0, AddString(f.second.s));
      continue;
    }
    // the fields that are no column: read where they are used
    if (f.first == "base" || f.first == "edit" || f.first == "name" || f.first == "grp" || f.first == "buttons" ||
        f.first == "looks" || f.first == "requires" || f.first == "use_requires" || f.first == "comment")
      continue;
    if (ModStatusField(t, f.first) || ModLuaField(t, f.first))  // statuses, auras, blocks; the scripts' hooks
      continue;
    if (t == UNITS && (f.first == "icon" || f.first == "wireframe" || f.first == "transport_wireframe"))
      continue;
    // a sound's subtitle and its least time between plays, read as it plays (ModSoundSubtitle)
    if (t == SOUNDS && f.first == "subtitle") {
      if (f.second.kind != PortJson::STRING)
        return Fail("sounds " + r.name + ".subtitle takes a text");
      continue;
    }
    if (t == SOUNDS && f.first == "min_interval") {
      if (f.second.kind != PortJson::NUMBER || f.second.d < 0 || f.second.d > 600000)
        return Fail("sounds " + r.name + ".min_interval takes milliseconds, 0 to 600000");
      continue;
    }
    if (t == IMAGES && IsSheetField(f.first))
      continue;
    if (t == PORTRAITS && f.first == "idle_chances") {
      if (f.second.kind != PortJson::ARRAY || f.second.size() != 4)
        return Fail("portraits " + r.name + ".idle_chances takes 4 values");
      for (int k = 0; k < 4; ++k)
        Put(r.cols[2 + k], 1, 0, static_cast<long long>(f.second.At(k).Num(0)));
      continue;
    }
    int c = 0;
    while (c < tab.nColumns && f.first != tab.pColumns[c].psz)
      ++c;
    if (c == tab.nColumns)
      return Fail(std::string(tab.psz) + " " + r.name + " has no field " + f.first);
    const Column &col = tab.pColumns[c];
    const Geometry &g = g_tables[t].geo[c];
    const int nPer = static_cast<int>(g.rowBytes / g.elemSize);
    const std::string where = std::string(tab.psz) + " " + r.name + "." + f.first;
    const PortJson &v = f.second;
    if (c < PathColumns(t)) {
      if (!MediaPath(t, c, v, where, &r.cols[c]))
        return false;
      continue;
    }
    if (t == SOUNDS && f.first == "flags" && v.kind == PortJson::ARRAY) {
      long long bits = 0;
      for (size_t i = 0; i < v.size(); ++i) {
        int k = 0;
        while (k < 6 && (kSoundFlags[k][0] == 0 || v.At(i).Str() != kSoundFlags[k]))
          ++k;
        if (k == 6)
          return Fail(where + " has no flag " + v.At(i).Str());
        bits |= 1ll << k;
      }
      Put(r.cols[c], g.recElem, 0, bits);
      continue;
    }
    if (t == UNITS && f.first == "flags" && v.kind == PortJson::ARRAY) {
      long long bits = 0;
      for (size_t i = 0; i < v.size(); ++i) {
        int k = 0;
        while (k < 32 && v.At(i).Str() != kUnitFlags[k])
          ++k;
        if (k == 32)
          return Fail(where + " has no flag " + v.At(i).Str());
        bits |= 1ll << k;
      }
      Put(r.cols[c], g.recElem, 0, bits);
      continue;
    }
    std::vector<long long> vals;
    if (v.kind == PortJson::ARRAY) {
      for (size_t i = 0; i < v.size(); ++i)
        vals.push_back(Resolve(col.pszRef, v.At(i), where));
    } else if (v.kind == PortJson::BOOL) {
      vals.push_back(v.b ? 1 : 0);
    } else {
      const int n = Resolve(col.pszRef, v, where);
      if (n < 0 && v.kind == PortJson::STRING)
        return Fail(where + " names " + v.s + ", which is no type the mod or the original has");
      vals.push_back(v.kind == PortJson::NUMBER ? static_cast<long long>(v.d) : n);
    }
    if (static_cast<int>(vals.size()) != nPer) {
      char sz[64];
      snprintf(sz, sizeof sz, " takes %d values", nPer);
      return Fail(where + sz);
    }
    const int nRef = col.pszRef != nullptr ? TableIndex(col.pszRef) : -1;
    for (int k = 0; nRef >= 0 && k < nPer; ++k)
      if (!RowExists(nRef, vals[k])) {
        char sz[96];
        snprintf(sz, sizeof sz, " names %s row %lld, which neither the original nor the mod has", col.pszRef,
                 vals[k]);
        return Fail(where + sz);
      }
    for (int k = 0; col.nMost != 0 && k < nPer; ++k)
      if (vals[k] < 0 || vals[k] >= col.nMost) {
        char sz[96];
        snprintf(sz, sizeof sz, " is %lld, where it takes 0 to %lld", vals[k], col.nMost - 1);
        return Fail(where + sz);
      }
    for (int k = 0; k < nPer; ++k)
      Put(r.cols[c], g.recElem, k, vals[k] * col.nScale);
  }
  return true;
}

bool Build(int t, Record &r) {
  if (r.state == 2)
    return true;
  if (r.state == 1)
    return Fail(std::string(kTables[t].psz) + " " + r.name + " is its own base");
  r.state = 1;
  if (!BaseRow(t, (*r.pJson)["base"], r) || !Fields(t, r))
    return false;
  if (t == PORTRAITS && (*r.pJson)["base"].kind == PortJson::NONE) {
    if ((*r.pJson)["idle"].kind == PortJson::NONE)
      return Fail("portraits " + r.name + " needs idle videos, or a base");
    if ((*r.pJson)["talking"].kind == PortJson::NONE)
      r.cols[1] = r.cols[0];
  }
  if (t == SOUNDS && (*r.pJson)["base"].kind == PortJson::NONE && (*r.pJson)["file"].kind == PortJson::NONE)
    return Fail("sounds " + r.name + " needs a file, or a base");
  r.state = 2;
  return true;
}

// A unit whose record names its looks and gives no voices or portrait of its own has its
// looks unit's, as it has its icon and wireframe.
void LooksVoices(Record &r) {
  const PortJson &j = *r.pJson;
  const int nLooks = Resolve("units", j["looks"], "units " + r.name + ".looks");
  if (nLooks < 0 || nLooks >= UNIT_TYPES_117)
    return;
  static const char *const kVoices[] = {"ready_sound",        "what_sound_first",  "what_sound_last",
                                        "pissed_sound_first", "pissed_sound_last", "yes_sound_first",
                                        "yes_sound_last"};
  const Record *pEdit = EditOf(UNITS, nLooks);
  const std::vector<std::vector<BYTE>> looks = pEdit ? pEdit->cols : FileRow(UNITS, nLooks);
  if (std::none_of(std::begin(kVoices), std::end(kVoices), [&](const char *psz) { return j[psz].kind != PortJson::NONE; }))
    for (const char *psz : kVoices)
      r.cols[ColumnOf(UNITS, psz)] = looks[ColumnOf(UNITS, psz)];
  if (j["portrait"].kind == PortJson::NONE)
    r.cols[ColumnOf(UNITS, "portrait")] = looks[ColumnOf(UNITS, "portrait")];
}

// An "edit": the row it names as it stands, with the fields it gives. An earlier submission's
// record (a mod this one depends on, or an earlier push) takes the change itself, which
// pChanged lists for storing; one of the original's rows keeps it with the table's edits. A
// record of the same submission gives its fields itself, so editing it is refused.
bool Edit(int t, Record &e, int nSub, std::vector<Record *> *pChanged) {
  const Table &tab = kTables[t];
  ModTable &m = g_tables[t];
  const PortJson &j = *e.pJson;
  const PortJson &target = j["edit"];
  const std::string where = std::string(tab.psz) + " " + e.name;
  for (const char *psz : {"base", "grp", "buttons", "looks", "team", "frame", "origin", "scale", "frames", "directions"})
    if (j[psz].kind != PortJson::NONE)
      return Fail(where + " is an edit, which keeps its row's " + psz + "; a new record has one of its own");
  // a mod's record by its name, else the row's: a mod's record has it (a number, or one of the
  // original's unused tech and upgrade rows a record took), or it is the original's
  Record *pRec = nullptr;
  int nRow = -1;
  for (Record &o : m.recs)
    if (target.kind == PortJson::STRING && o.name == target.s)
      pRec = &o;
  if (!pRec) {
    nRow = target.kind == PortJson::STRING   ? TypeNameFind(tab.psz, target.s.c_str())
           : target.kind == PortJson::NUMBER ? static_cast<int>(target.d)
                                             : -1;
    for (Record &o : m.recs)
      if (o.id == nRow)
        pRec = &o;
  }
  if (pRec && pRec->sub == nSub)
    return Fail(where + " edits " + pRec->name + ", a record of the same data, which gives its fields itself");
  if (pRec) {
    e.id = pRec->id;
    e.base117 = pRec->base117;
    e.cols = pRec->cols;
  } else {
    if (nRow < 0 || nRow >= tab.nRows117)
      return Fail(where + " edits no row of the original's or of a mod it depends on");
    e.id = e.base117 = nRow;
    const Record *pEdit = EditOf(t, nRow);
    e.cols = pEdit ? pEdit->cols : FileRow(t, nRow);
  }
  if (!Fields(t, e))
    return false;
  e.state = 2;
  if (pRec) {
    pRec->cols = e.cols;
    pChanged->push_back(pRec);
  } else {
    m.edits.push_back(e);
  }
  return true;
}

// Stores a record's row into the table's columns. A column only some of 1.17's rows have
// (units.dat's sounds and add-on positions) has none of a mod's, which come after 1.17's;
// sprites.dat's columns from sprite 130 have every sprite's after it.
bool Store(int t, const Record &r) {
  const Table &tab = kTables[t];
  for (int c = 0; c < tab.nColumns; ++c) {
    const Geometry &g = g_tables[t].geo[c];
    const bool bEvery = g.first + g.rows == tab.nRows117;
    // the build holds a mod's rows of such a column when it grows it (units.dat's sounds)
    if (!bEvery && r.id >= tab.nRows117 && !PortDataGrown(tab.pDesc[c].pDest))
      continue;
    // the original's portrait names are read for each game's portraits (statport_init), after
    // which a mod's edits name theirs (ModPortraitsResolved)
    if (t == PORTRAITS && c < PathColumns(t) && r.id < tab.nRows117)
      continue;
    // and a column that holds only part of 1.17's rows has nothing for a row outside them:
    // units.dat's sounds for a building, its add-on positions for a unit below 106
    if (r.id < g.first || (r.id < tab.nRows117 && r.id - g.first >= g.rows))
      continue;
    // a column the engine never reads stays in 1.17's block with 1.17's rows (units.dat's
    // second subunit and hit counts, orders.dat's labels): a mod's rows have none of it
    if (r.id >= tab.nRows117 && !PortDataGrown(tab.pDesc[c].pDest))
      continue;
    void *p = PortDataRow(tab.pDesc[c].pDest, g.rowBytes, static_cast<DWORD>(r.id - g.first));
    if (!p)
      return Fail(std::string(tab.psz) + " " + r.name + "." + tab.pColumns[c].psz +
                  " is past the rows the build holds");
    // a sound whose file changes is read again when next played
    const bool bNewSound = t == SOUNDS && c == 0 && memcmp(p, r.cols[c].data(), g.recBytes) != 0;
    if (g.recBytes == g.rowBytes)
      PortDataStore(p, r.cols[c].data(), g.rowBytes);
    else
      memcpy(p, r.cols[c].data(), g.recBytes);  // already in the build's widths
    if (bNewSound)
      snd_free_snd(r.id);
  }
  return true;
}

// A .lo file of zero offsets: nFrames frames of nOverlays offsets each.
const lo_file_t *ZeroOverlay(int nFrames, int nOverlays) {
  const DWORD cb = 8 + 4 * nFrames + 2 * nFrames * nOverlays;
  BYTE *p = static_cast<BYTE *>(SMemAlloc(cb, "port_moddata.cpp", 0, 8));
  if (!p)
    return nullptr;
  memset(p, 0, cb);
  reinterpret_cast<DWORD *>(p)[0] = nFrames;
  reinterpret_cast<DWORD *>(p)[1] = nOverlays;
  for (int f = 0; f < nFrames; ++f)
    reinterpret_cast<DWORD *>(p + 8)[f] = 8 + 4 * nFrames + 2 * f * nOverlays;
  return reinterpret_cast<const lo_file_t *>(p);
}

void Hash(BCRYPT_HASH_HANDLE h, const std::string &name, const std::vector<BYTE> &bytes) {
  const DWORD cb = static_cast<DWORD>(bytes.size());
  BCryptHashData(h, reinterpret_cast<PUCHAR>(const_cast<char *>(name.c_str())), static_cast<ULONG>(name.size()), 0);
  BCryptHashData(h, reinterpret_cast<PUCHAR>(const_cast<DWORD *>(&cb)), sizeof cb, 0);
  if (cb)
    BCryptHashData(h, const_cast<PUCHAR>(bytes.data()), cb, 0);
}

// The image's overlay kinds (CImage.cpp's ImageOverlay) and the images.dat column of each.
const int kOverlayColumns[6] = {9, 10, 11, 12, 13, 8};

// A level of an image's sheet, as its "grp" or "team" names it: its scale, and its picture's
// files, one, or parts stacked top to bottom (a picture past what one file may be).
struct SheetFileLevel {
  int nScale = 1;
  std::vector<std::string> parts;
};

// The files an image's "grp" or "team" names: a file, a list of parts, or either for each
// level ({"1": ..., "4": ...}), by their scale, smallest first; the first two at the record's
// "scale", 1 if none.
bool SheetFiles(const Record &r, const char *pszField, std::vector<SheetFileLevel> *pOut) {
  const PortJson &v = (*r.pJson)[pszField];
  const std::string where = "images " + r.name + "." + pszField;
  auto Parts = [&](const PortJson &p, SheetFileLevel *pLevel) {
    if (p.kind == PortJson::STRING)
      pLevel->parts.push_back(p.s);
    for (const PortJson &q : p.kind == PortJson::ARRAY ? p.items : std::vector<PortJson>()) {
      if (q.kind != PortJson::STRING)
        return Fail(where + " lists a sheet's parts by their files");
      pLevel->parts.push_back(q.s);
    }
    if (pLevel->parts.empty())
      return Fail(where + " is a file, a list of its parts, or one of those for each level");
    for (const std::string &f : pLevel->parts)
      if (!PortImageFileName(f))
        return Fail(where + "'s " + f + " is not a picture Hyperion reads (.png, .webp or .tga)");
    return true;
  };
  pOut->clear();
  if (v.kind == PortJson::NONE)
    return true;
  if (v.kind != PortJson::OBJECT) {
    SheetFileLevel l;
    l.nScale = static_cast<int>((*r.pJson)["scale"].Num(1));
    if (l.nScale < 1 || l.nScale > 8)
      return Fail("images " + r.name + ".scale is " + std::to_string(l.nScale) + "; a sheet's is 1 to 8");
    if (!Parts(v, &l))
      return false;
    pOut->push_back(std::move(l));
    return true;
  }
  if ((*r.pJson)["scale"].kind != PortJson::NONE)
    return Fail(where + " names the files of each level, which gives their scales; it has no \"scale\"");
  for (const auto &m : v.members) {
    SheetFileLevel l;
    l.nScale = atoi(m.first.c_str());
    if (l.nScale < 1 || l.nScale > 8 || std::to_string(l.nScale) != m.first)
      return Fail(where + " names each level as \"1\", \"2\" or \"4\" (1 to 8), not " + m.first);
    if (!Parts(m.second, &l))
      return false;
    pOut->push_back(std::move(l));
  }
  std::sort(pOut->begin(), pOut->end(), [](const SheetFileLevel &x, const SheetFileLevel &y) { return x.nScale < y.nScale; });
  for (size_t i = 1; i < pOut->size(); ++i)
    if ((*pOut)[i].nScale == (*pOut)[i - 1].nScale)
      return Fail(where + " names level " + std::to_string((*pOut)[i].nScale) + " twice");
  return true;
}

// An image's sheet, the picture its "grp" names, made classic frames (ModSheetEncode) from
// its smallest level, read part by part, each made scale 1 before the next is read; its other
// levels' files read only for their size and the fingerprint, which has every file.
// HYPERION_MOD_SHEETS=<folder> writes each as a GRP file there, <image>.grp, to look at, when
// its frames fit one. *pArt: what the HD frame draws it from at its own resolution.
bool SheetGroup(const std::string &folder, const Record &r, BCRYPT_HASH_HANDLE hHash, ModSheetFrames *pFrames,
                PortModArtSheet *pArt) {
  const PortJson &j = *r.pJson;
  const std::string where = "images " + r.name;
  ModSheet s;
  const PortJson &frame = j["frame"], &origin = j["origin"];
  // a cell of 1 to 1024 game pixels a side: a sprite's frame is 255 at most in classic
  if (frame.size() != 2 || frame.At(0).Num() < 1 || frame.At(1).Num() < 1 || frame.At(0).Num() > 1024 ||
      frame.At(1).Num() > 1024)
    return Fail(where + " names a sheet, which needs \"frame\": [width, height], a cell's size in game pixels, 1 to 1024");
  s.nFrameW = static_cast<int>(frame.At(0).Num()), s.nFrameH = static_cast<int>(frame.At(1).Num());
  if (origin.kind != PortJson::NONE && origin.size() != 2)
    return Fail(where + ".origin is [x, y], in game pixels from a cell's corner");
  s.fOriginX = origin.size() == 2 ? origin.At(0).Num() : s.nFrameW / 2.0;
  s.fOriginY = origin.size() == 2 ? origin.At(1).Num() : s.nFrameH / 2.0;
  s.nFrames = static_cast<int>(j["frames"].Num(0));
  if (s.nFrames < 0 || s.nFrames > 0x7FFF)
    return Fail(where + ".frames is 1 to 32,767");
  std::vector<SheetFileLevel> files, teams;
  if (!SheetFiles(r, "grp", &files) || !SheetFiles(r, "team", &teams))
    return false;
  bool bAlike = teams.empty() || teams.size() == files.size();
  for (size_t i = 0; bAlike && !teams.empty() && i < files.size(); ++i)
    bAlike = teams[i].nScale == files[i].nScale && teams[i].parts.size() == files[i].parts.size();
  if (!bAlike)
    return Fail(where + ".team names a file for each of its grp's, at the same levels");
  std::vector<BYTE> rgba, team;  // the smallest level at scale 1
  for (size_t i = 0; i < files.size(); ++i) {
    ModSheetLevel l;
    l.nScale = files[i].nScale;
    for (size_t k = 0; k < files[i].parts.size(); ++k) {
      int pw = 0, ph = 0;
      for (int nLayer = 0; nLayer < (teams.empty() ? 1 : 2); ++nLayer) {
        const std::string &name = (nLayer ? teams : files)[i].parts[k];
        std::vector<BYTE> bytes, px;
        std::string error;
        int w = 0, h = 0;
        if (!PortReadFile(folder + name, &bytes))
          return Fail(where + "'s " + name + " does not read");
        Hash(hHash, name, bytes);
        if (!PortImageInfo(bytes.data(), bytes.size(), name, &w, &h, &error))
          return Fail(where + "'s " + name + " " + error);
        if (nLayer ? w != pw || h != ph : (l.w != 0 && w != l.w) || h % (s.nFrameH * l.nScale) != 0)
          return Fail(where + "'s " + name + " is " + std::to_string(w) + " x " + std::to_string(h) +
                      (nLayer ? ", another size than its part of the sheet"
                              : ", not a whole number of rows of cells as wide as the level's other parts"));
        pw = w, ph = h;
        if (nLayer == 0)
          l.w = w, l.h += h;
        if (i > 0)
          continue;
        if (!PortImageDecode(bytes.data(), bytes.size(), name, &px, &w, &h, &error))
          return Fail(where + "'s " + name + " " + error);
        if (l.nScale > 1)
          px = ModSheetScaledDown(px, w, h, l.nScale);
        (nLayer ? team : rgba).insert((nLayer ? team : rgba).end(), px.begin(), px.end());
      }
    }
    s.levels.push_back(l);
  }
  std::string error;
  const bool bShadow = r.cols[5][0] == 10;  // draw_function: classic's shadow
  if (!ModSheetEncode(s, rgba, team, bShadow, pFrames, &error))
    return Fail(where + "'s sheet " + error);
  pArt->folder = folder;
  pArt->nFrameW = s.nFrameW, pArt->nFrameH = s.nFrameH;
  pArt->nFrames = static_cast<int>(pFrames->frames.size());
  pArt->levels.clear();
  for (size_t i = 0; i < files.size(); ++i) {
    PortModArtSheet::Level l;
    l.nScale = files[i].nScale;
    l.parts = files[i].parts;
    if (!teams.empty())
      l.team = teams[i].parts;
    pArt->levels.push_back(l);
  }
  pArt->nCanvasW = pFrames->w, pArt->nCanvasH = pFrames->h;
  pArt->nCellX = pFrames->dx, pArt->nCellY = pFrames->dy;
  pArt->bShadow = bShadow;
  pArt->nTeamBright = ModSheetTeamBright();
  const char *pszDump = SDL_getenv("HYPERION_MOD_SHEETS");
  std::vector<BYTE> file;
  if (pszDump != NULL && pszDump[0] != 0 && ModSheetFile(*pFrames, &file)) {
    SDL_IOStream *f = SDL_IOFromFile((std::string(pszDump) + "/" + r.name + ".grp").c_str(), "wb");
    if (f != NULL) {
      SDL_WriteIO(f, file.data(), file.size());
      SDL_CloseIO(f);
    }
  }
  return true;
}

// An image's frames, from its "grp" in the mod's folder, or else its base's; and its
// overlay offsets: its base's with the base's frames, or offsets of zero with its own. A
// damage overlay (the flames of a damaged building) needs offsets an image of its own
// has none of, so such an image has none. A grp the folder does not have (a mod's art is
// the player's own, made from their files, and not in the package) leaves the image its
// base's frames and its base's script, which those frames fit. A "grp" may be a GRP of
// classic's or a sheet, a picture of the frames (SheetGroup).
bool ImageArt(const std::string &folder, Record &r, BCRYPT_HASH_HANDLE hHash) {
  const unsigned nMod = static_cast<unsigned>(r.id - IMAGE_TYPES_117);
  const PortJson &jGrp = (*r.pJson)["grp"];
  const bool bSheet = jGrp.kind == PortJson::OBJECT || jGrp.kind == PortJson::ARRAY || PortImageFileName(jGrp.Str());
  // a sheet's directions: 17, the others mirrored as classic's are, or 32, none mirrored (CImage.cpp)
  const int nDirections = static_cast<int>((*r.pJson)["directions"].Num(17));
  if (nDirections != 17 && nDirections != 32)
    return Fail("images " + r.name + ".directions is 17 or 32");
  if (nDirections == 32 && !bSheet)
    return Fail("images " + r.name + " has 32 directions, which a sheet gives, not a GRP");
  g_hxModImage32[nMod] = 0;
  PortModArtSet(r.id, NULL);
  std::vector<SheetFileLevel> files, teams;
  if (bSheet && (!SheetFiles(r, "grp", &files) || !SheetFiles(r, "team", &teams)))
    return false;
  if (!bSheet && !jGrp.Str().empty())
    files.push_back(SheetFileLevel{1, {jGrp.Str()}});
  files.insert(files.end(), teams.begin(), teams.end());
  std::string grp = files.empty() ? std::string() : files[0].parts[0];
  bool bMissing = false;
  for (const SheetFileLevel &l : files)
    for (const std::string &f : l.parts)
      if (GetFileAttributesA((folder + f).c_str()) == INVALID_FILE_ATTRIBUTES)
        bMissing = true, grp = f;
  if (bMissing) {
    Note("images " + r.name + " has no " + grp + "; it is drawn as its base");
    const int c = 7;  // the script
    r.cols[c] = FileRow(IMAGES, r.base117)[c];
    BCryptHashData(hHash, reinterpret_cast<PUCHAR>(const_cast<char *>(grp.c_str())), static_cast<ULONG>(grp.size()), 0);
  }
  if (grp.empty() || bMissing) {
    sgpImgGroups[r.id] = sgpImgGroups[r.base117];
    for (int k = 0; k < 5; ++k)
      g_hxModOverlays[k][nMod] = sgpOverlayGroups[k * 999 + r.base117];
    g_hxModOverlays[5][nMod] = sgpShieldOverlayGroups_g[r.base117];
    sgbNumDmgOverlays_g[r.id] = sgbNumDmgOverlays_g[r.base117];
    return true;
  }
  int nFrames = 0;
  if (bSheet) {
    ModSheetFrames frames;
    PortModArtSheet art;
    if (!SheetGroup(folder, r, hHash, &frames, &art))
      return false;
    PortModArtSet(r.id, &art);
    nFrames = static_cast<int>(frames.frames.size());
    GROUP *pGroup = ModSheetGroupNative(frames);
    if (pGroup == NULL)
      return Fail("no memory for images " + r.name);
    sgpImgGroups[r.id] = pGroup;
    g_hxModImage32[nMod] = nDirections == 32;
  } else {
    std::vector<BYTE> bytes;
    if (!PortReadFile(folder + grp, &bytes) || bytes.size() < 6)
      return Fail("images " + r.name + "'s grp " + grp + " does not read");
    Hash(hHash, grp, bytes);
    nFrames = (bytes[0] | bytes[1] << 8) & 0x7FFF;
    void *p = SMemAlloc(static_cast<DWORD>(bytes.size()), "port_moddata.cpp", 0, 0);
    if (!p)
      return Fail("no memory for images " + r.name);
    memcpy(p, bytes.data(), bytes.size());
    sgpImgGroups[r.id] = PortGroupNative(static_cast<GROUP *>(p), true);
  }
  sgbNumDmgOverlays_g[r.id] = 0;
  std::fill(r.cols[kOverlayColumns[1]].begin(), r.cols[kOverlayColumns[1]].end(), 0);
  for (int k = 0; k < 6; ++k) {
    const std::vector<BYTE> &col = r.cols[kOverlayColumns[k]];
    const bool bHas = k != 1 && std::any_of(col.begin(), col.end(), [](BYTE b) { return b != 0; });
    // the shield's offsets are a direction's, 32 of them
    g_hxModOverlays[k][nMod] = bHas ? ZeroOverlay(nFrames > 32 ? nFrames : 32, 8) : nullptr;
  }
  return true;
}

// One of a unit's requirements, in 1.17's form: "broodwar" (an expansion game), a unit the
// player has finished, {"at": unit} (the unit that trains it) or {"tech": tech} (researched);
// and of a tech's use requirements, {"caster": unit} (the unit that casts it) and
// "researched" (the tech itself).
bool Requirement(const PortJson &v, const std::string &where, std::vector<WORD> *pOut) {
  int n = -1;
  if (v.kind == PortJson::STRING && v.s == "broodwar") {
    pOut->push_back(0xFF24);
    return true;
  }
  if (v.kind == PortJson::STRING && v.s == "researched") {
    pOut->push_back(0xFF0F);
    return true;
  }
  const PortJson &at = v["at"].kind != PortJson::NONE ? v["at"] : v["caster"];
  if (v.kind == PortJson::OBJECT && at.kind != PortJson::NONE) {
    pOut->push_back(0xFF02);
    n = Resolve("units", at, where);
  } else if (v.kind == PortJson::OBJECT && v["tech"].kind != PortJson::NONE) {
    pOut->push_back(0xFF25);
    n = Resolve("techs", v["tech"], where);
  } else {
    n = Resolve("units", v, where);
  }
  if (n < 0 || n >= 0xFF00)
    return Fail(where + " names no unit or tech the mod or the original has");
  pOut->push_back(static_cast<WORD>(n));
  return true;
}

// The tests a classic unit's requirements make of the building that makes it: landed, not busy
// with an add-on or a building (the Terran trainers'), and with no add-on and nothing under way
// (the add-ons'). A unit of the mod's keeps its base's, so a unit made from the Marine is not
// trained in the air and one made from the Comsat Station is not built beside an add-on.
std::vector<WORD> BuilderTests(int nBase117) {
  std::vector<WORD> out;
  if (nBase117 < 0 || nBase117 >= UNIT_TYPES_117)
    return out;
  for (int i = 1; i < 546 && sgTree[i] != 0xFFFF;) {
    const int nType = sgTree[i++];
    for (; i < 546 && sgTree[i] != 0xFFFF; ++i)
      if (nType == nBase117 && (sgTree[i] == 0xFF05 || sgTree[i] == 0xFF07 || sgTree[i] == 0xFF08 || sgTree[i] == 0xFF0C) &&
          sgTree[i - 1] != 0xFF01 && (i + 1 >= 546 || sgTree[i + 1] != 0xFF01))
        out.push_back(sgTree[i]);
    ++i;
  }
  return out;
}

// A unit's "requires" or a tech's "use_requires": its requirements in order, each one met;
// an array among them is alternatives, any one of which meets it. A unit's (nBase117, the
// classic row it starts from) takes its base's tests of the building that makes it after
// the requirement that names that building.
bool Tree(const Record &r, const char *pszField, std::map<int, std::vector<WORD>> *pTrees, int nBase117 = -1) {
  const PortJson &req = r.pJson->operator[](pszField);
  if (req.kind == PortJson::NONE)
    return true;
  const std::string where = r.name + "." + pszField;
  if (req.kind != PortJson::ARRAY)
    return Fail(where + " is a list");
  std::vector<WORD> tree(1, static_cast<WORD>(r.id));
  bool bTests = false;
  for (size_t i = 0; i < req.size(); ++i) {
    const PortJson &v = req.At(i);
    const size_t nAt = tree.size();
    if (v.kind == PortJson::ARRAY) {
      for (size_t k = 0; k < v.size(); ++k) {
        if (k)
          tree.push_back(0xFF01);
        if (!Requirement(v.At(k), where, &tree))
          return false;
      }
    } else if (!Requirement(v, where, &tree)) {
      return false;
    }
    if (!bTests && std::find(tree.begin() + nAt, tree.end(), 0xFF02) != tree.end()) {
      bTests = true;
      for (WORD w : BuilderTests(nBase117))
        tree.push_back(w);
    }
  }
  tree.push_back(0xFFFF);
  (*pTrees)[r.id] = tree;
  return true;
}

// A button's text as stat_txt has it: the hotkey, the kind of cost its tooltip shows (1 a
// unit's, 2 an upgrade's, 3 a spell's energy, 4 a tech's research), then the text with the
// hotkey's letter, written <X>, in the highlight colour.
std::string ButtonText(const std::string &hotkey, char cKind, const std::string &text) {
  std::string s = hotkey.substr(0, 1) + cKind;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '<' && i + 2 < text.size() && text[i + 2] == '>') {
      s += "\x03";
      s += text[i + 1];
      s += "\x01";
      i += 2;
    } else {
      s += text[i];
    }
  }
  return s;
}

WORD AddString(const std::string &s) {
  g_strings.push_back(s);
  return static_cast<WORD>(MOD_STRING_FIRST + g_strings.size() - 1);
}

// The workers' build menus by the names "cards" gives them, from MOD_BUILD_MENU_FIRST.
const char *const kBuildMenus[MOD_BUILD_MENUS] = {
    "zerg_basic_structures",    "terran_basic_structures",    "protoss_basic_structures",
    "zerg_advanced_structures", "terran_advanced_structures", "protoss_advanced_structures"};

// "cards": buttons the mod adds to 1.17 units' command cards, by the card's unit, and to the
// workers' build menus, by their names.
bool Cards() {
  for (const auto &card : g_types["cards"].members) {
    const std::string where = "cards " + card.first;
    int nCard = Find(UNITS, card.first);
    int nMenu = -1;
    for (int i = 0; i < MOD_BUILD_MENUS; ++i)
      if (card.first == kBuildMenus[i])
        nMenu = i;
    if (nMenu >= 0)
      nCard = MOD_BUILD_MENU_FIRST + nMenu;
    else if (nCard < 0 || (nCard >= UNIT_TYPES_117 && nCard < UNIT_TYPE_MOD_FIRST))
      return Fail(where + ": a card is an original unit's, a mod's unit's, or a worker's build menu (\"terran_basic_structures\")");
    for (size_t i = 0; i < card.second.size(); ++i) {
      const PortJson &b = card.second.At(i);
      const int nPos = static_cast<int>(b["position"].Num(0));
      if (nPos < 1 || nPos > 9)
        return Fail(where + ": a button's position is 1 to 9");
      ModCardButton m = {};
      m.wCard = static_cast<WORD>(nCard);
      m.wPos = static_cast<WORD>(nPos);
      m.nWords = -1;
      m.nAiCall = -1;
      std::string name;
      if (b["classic"].kind != PortJson::NONE) {
        // the words of the card's own button at the position: the first there, or the one that
        // makes, researches or upgrades the record named
        m.bWords = true;
        m.wUnit = m.wTech = m.wMatch = 0xFFFF;
        if (b["classic"].kind == PortJson::STRING) {
          int n = Find(UNITS, b["classic"].s);
          if (n < 0)
            n = Find(TECHS, b["classic"].s);
          if (n < 0)
            n = Find(UPGRADES, b["classic"].s);
          if (n < 0)
            return Fail(where + ": classic names the unit, tech or upgrade of the card's own button, or is true");
          m.wMatch = static_cast<WORD>(n);
        } else if (!(b["classic"].kind == PortJson::BOOL && b["classic"].b)) {
          return Fail(where + ": classic names the unit, tech or upgrade of the card's own button, or is true");
        }
        for (const char *psz : {"build", "train", "cast", "research", "upgrade", "icon", "ai", "target"})
          if (b[psz].kind != PortJson::NONE)
            return Fail(where + ": a button that changes a classic one's words has only its text, hotkey and disabled");
        if (b["text"].kind == PortJson::STRING || b["hotkey"].kind == PortJson::STRING) {
          Words w;
          w.hotkey = b["hotkey"].Str();
          w.text = b["text"].Str();
          m.nWords = static_cast<short>(g_words.size());
          g_words.push_back(w);
        }
        const std::string dis = b["disabled"].Str();
        m.wDisabledStr = dis.empty() ? 0 : AddString(dis);
        if (m.nWords < 0 && m.wDisabledStr == 0)
          return Fail(where + ": a button that changes a classic one's words gives a text, a hotkey or disabled");
        g_buttons.push_back(m);
        continue;
      }
      if ((nMenu >= 0) != (b["build"].kind != PortJson::NONE))
        return Fail(where + ": a build menu's buttons each build, and only a build menu's do");
      if (b["build"].kind != PortJson::NONE) {
        // a building of the mod's, placed as the menu's race places its own
        const int nUnit = Resolve("units", b["build"], where + ".build");
        if (nUnit < UNIT_TYPE_MOD_FIRST)
          return Fail(where + ": a build button builds one of the mod's units");
        static const unsigned char kPlace[3] = {MOD_PLACE_ZERG, MOD_PLACE_TERRAN, MOD_PLACE_PROTOSS};
        m.wUnit = static_cast<WORD>(nUnit);
        m.bPlace = kPlace[nMenu % 3];
        m.wIcon = static_cast<WORD>(b["icon"].Num(ModUnitLooks(static_cast<unsigned>(nUnit))));
        m.bUnitIcon = b["icon"].kind == PortJson::NONE;
        name = ModUnitName(static_cast<unsigned>(nUnit));
      } else if (b["cast"].kind != PortJson::NONE) {
        // a cast: the order, and its tech from the order's row (orders.dat's tech column)
        const int nOrder = Resolve("orders", b["cast"], where + ".cast");
        const Record *pOrder = nullptr;
        for (const Record &o : g_tables[ORDERS].recs)
          if (o.id == nOrder)
            pOrder = &o;
        if (!pOrder)
          return Fail(where + ": a cast names one of the mod's orders");
        const std::vector<BYTE> &tech = pOrder->cols[14];  // two bytes where the build keeps ids wide
        const int nTech = tech[0] | (tech.size() > 1 ? tech[1] << 8 : 0);
        const Record *pTech = nullptr;
        for (const Record &t : g_tables[TECHS].recs)
          if (t.id == nTech)
            pTech = &t;
        if (!pTech)
          return Fail(where + ": a cast's order names one of the mod's techs");
        m.wUnit = 0xFFFF;
        m.wOrder = static_cast<WORD>(nOrder);
        m.wTech = static_cast<WORD>(nTech);
        m.bTarget = b["target"].kind == PortJson::BOOL && b["target"].b;
        if (b["ai"].kind == PortJson::STRING) {
          for (int f = 0; f < MOD_AI_FINDERS; ++f)
            if (b["ai"].s == kModAiFinders[f])
              m.bAi = static_cast<unsigned char>(f + 1);
          if (m.bAi == 0)
            return Fail(where + ": a cast's ai names one of the original's spells, as \"psionic_storm\"");
        } else if (b["ai"].kind == PortJson::OBJECT) {
          // the mod's own finder: a function of its scripts, asked every so many seconds for
          // each caster a computer or a mode owns
          const std::string call = b["ai"]["call"].Str();
          if (call.empty() || !ModLuaHasFunction(call))
            return Fail(where + ": a cast's ai call names a function of the mod's scripts");
          const double dEvery = b["ai"]["every"].Num(1);
          if (dEvery <= 0 || dEvery > 600)
            return Fail(where + ": a cast's ai every is seconds, above 0 and up to 600");
          m.nAiCall = static_cast<short>(g_cardAiCalls.size());
          g_cardAiCalls.push_back(call);
          m.wAiEvery = static_cast<WORD>((std::max)(1.0, floor(dEvery * 24 + 0.5)));
        }
        m.wIcon = static_cast<WORD>(b["icon"].Num(pTech->cols[6][0] | pTech->cols[6][1] << 8));
        name = pOrder->name;
      } else if (b["upgrade"].kind != PortJson::NONE) {
        // an upgrade: one of the mod's
        const int nUpgrade = Resolve("upgrades", b["upgrade"], where + ".upgrade");
        const Record *pUpgrade = nullptr;
        for (const Record &u : g_tables[UPGRADES].recs)
          if (u.id == nUpgrade)
            pUpgrade = &u;
        if (!pUpgrade)
          return Fail(where + ": an upgrade names one of the mod's upgrades");
        m.wUnit = 0xFFFF;
        m.wTech = static_cast<WORD>(nUpgrade);
        m.bUpgrade = true;
        m.wIcon = static_cast<WORD>(b["icon"].Num(pUpgrade->cols[7][0] | pUpgrade->cols[7][1] << 8));
        name = pUpgrade->name;
      } else if (b["research"].kind != PortJson::NONE) {
        // a research: one of the mod's techs that has research requirements
        const int nTech = Resolve("techs", b["research"], where + ".research");
        const Record *pTech = nullptr;
        for (const Record &t : g_tables[TECHS].recs)
          if (t.id == nTech)
            pTech = &t;
        if (!pTech || pTech->pJson->operator[]("requires").kind == PortJson::NONE)
          return Fail(where + ": a research names one of the mod's techs that has requires");
        m.wUnit = 0xFFFF;
        m.wTech = static_cast<WORD>(nTech);
        m.bResearch = true;
        m.wIcon = static_cast<WORD>(b["icon"].Num(pTech->cols[6][0] | pTech->cols[6][1] << 8));
        name = pTech->name;
      } else {
        const int nUnit = Resolve("units", b["train"], where + ".train");
        if (nUnit < UNIT_TYPE_MOD_FIRST)
          return Fail(where + ": a button trains one of the mod's units, casts one of its orders, or researches or upgrades one of its techs or upgrades");
        m.wUnit = static_cast<WORD>(nUnit);
        m.wIcon = static_cast<WORD>(b["icon"].Num(ModUnitLooks(static_cast<unsigned>(nUnit))));
        m.bUnitIcon = b["icon"].kind == PortJson::NONE;
        name = ModUnitName(static_cast<unsigned>(nUnit));
      }
      const char cKind = m.bUpgrade ? 2 : m.bResearch ? 4 : m.wUnit == 0xFFFF ? 3 : 1;
      m.wStr = AddString(ButtonText(b["hotkey"].Str("?"), cKind, b["text"].Str(name.c_str())));
      const std::string dis = b["disabled"].Str();
      m.wDisabledStr = dis.empty() ? 0 : AddString(dis);
      g_buttons.push_back(m);
    }
  }
  return true;
}

// "races", beside the tables: each race's base, name, start units, own units and the
// units it goes without, in the order of the records' names.
bool Races() {
  for (const auto &rec : g_types["races"].members) {
    const PortJson &j = rec.second;
    const std::string where = "races " + rec.first;
    Race r;
    r.id = rec.first;
    r.name = j["name"].Str(rec.first.c_str());
    const std::string base = j["base"].Str();
    r.nBase = base == "zerg" ? 0 : base == "terran" ? 1 : base == "protoss" ? 2 : -1;
    if (r.nBase < 0)
      return Fail(where + ": its base is zerg, terran or protoss");
    for (const char *psz : {"console", "music"}) {
      if (j[psz].kind == PortJson::NONE)
        continue;
      const std::string look = j[psz].Str();
      const int n = look == "zerg" ? 0 : look == "terran" ? 1 : look == "protoss" ? 2 : -1;
      if (n < 0)
        return Fail(where + ": its " + psz + " is zerg, terran or protoss");
      (std::string(psz) == "console" ? r.nConsole : r.nMusic) = n;
    }
    const PortJson &start = j["start"];
    r.bOwnStart = start.kind != PortJson::NONE;
    for (size_t i = 0; i < start.size(); ++i) {
      const PortJson &s = start.At(i);
      const bool bObject = s.kind == PortJson::OBJECT;
      const int nUnit = Resolve("units", bObject ? s["unit"] : s, where + ".start");
      const int nCount = bObject ? static_cast<int>(s["count"].Num(1)) : 1;
      if (nUnit < 0 || nCount < 1 || nCount > 12)
        return Fail(where + ": a start unit is a unit, and its count 1 to 12");
      r.start.push_back(std::make_pair(nUnit, nCount));
    }
    for (size_t i = 0; i < j["units"].size(); ++i) {
      const int n = Resolve("units", j["units"].At(i), where + ".units");
      if (n < UNIT_TYPE_MOD_FIRST)
        return Fail(where + ": a race's units are the mod's");
      r.units.push_back(n);
    }
    for (size_t i = 0; i < j["without"].size(); ++i) {
      const int n = Resolve("units", j["without"].At(i), where + ".without");
      if (n < 0 || n >= UNIT_TYPES_117)
        return Fail(where + ": what a race goes without is the original's units");
      r.without.push_back(n);
    }
    const PortJson &ai = j["ai"];
    for (size_t i = 0; ai.kind == PortJson::OBJECT && i < ai.members.size(); ++i) {
      PortJson named;
      named.kind = PortJson::STRING;
      named.s = ai.members[i].first;
      // 1.17's unit, tech or upgrade, by the table that has the name
      const char *pszTable = Find(UNITS, named.s) >= 0 ? "units" : Find(TECHS, named.s) >= 0 ? "techs" : "upgrades";
      const int nFrom = Find(TableIndex(pszTable), named.s);
      const int nTo = Resolve(pszTable, ai.members[i].second, where + ".ai");
      const int nMax = pszTable[0] == 'u' && pszTable[1] == 'n' ? UNIT_TYPES_117
                       : pszTable[0] == 't'                     ? TECH_TYPES_117
                                                                : UPGRADE_TYPES_117;
      if (nFrom < 0 || nFrom >= nMax || nTo < 0)
        return Fail(where + ": a race's ai maps the original's units, techs or upgrades to ones of the same kind");
      (pszTable[0] == 't' ? r.aiTechs : pszTable[1] == 'n' ? r.ai : r.aiUpgrades).push_back(std::make_pair(nFrom, nTo));
    }
    std::string why;
    if (!ModRulesRead(j, where, [&](const char *pszTable, const PortJson &v) { return Resolve(pszTable, v, where); },
                      &r.rules, &why))
      return Fail(why);
    g_races.push_back(r);
  }
  std::sort(g_races.begin(), g_races.end(), [](const Race &a, const Race &b) { return a.id < b.id; });
  if (g_races.size() > 250)
    return Fail("races has more than 250 records");
  return true;
}

// A name or string handed to the game, which keeps 1.17's string pointers as long as it likes:
// each text held once for the run, where a push reallocating the records cannot move it.
const char *Lasting(const std::string &text) {
  static std::set<std::string> s_texts;
  return s_texts.insert(text).first->c_str();
}

const Race *RaceAt(int n) {
  return n >= 1 && n <= static_cast<int>(g_races.size()) ? &g_races[n - 1] : nullptr;
}

// ---- the looks a mark wears ------------------------------------------------------------------
//
// types.json's "marks" names the looks the mod's scripts may put on a mark (docs/mod-data.md,
// "The looks a mark wears"): each is blocks of light with their own values, which hx.area's look
// asks for by name. A block or a field this build has no name for stops the mod with the line it
// is on, since a look drawn without one would be drawn wrong and nothing would say why.

std::map<std::string, PortMarkLook> g_markLooks;
std::string g_markLookNames;  // the names declared, separated by ", ", for saying what there is

// The text being read, so that a refusal names the line a value is on.
struct MarkRead {
  const char *pszText;

  bool Bad(const PortJson &v, const std::string &what) const {
    const int nLine = PortJsonLineAt(pszText, v.nAt);
    char sz[32] = "";
    if (nLine > 0)
      snprintf(sz, sizeof sz, " line %d", nLine);
    return Fail("types.json" + std::string(sz) + ": " + what);
  }
};

bool MarkOnly(const MarkRead &r, const PortJson &o, const std::string &at,
              const std::vector<const char *> &keys) {
  if (o.kind != PortJson::OBJECT)
    return r.Bad(o, at + " is an object");
  for (const PortJsonMember &m : o.members) {
    bool bKnown = m.first == "comment" || m.first == "about";
    for (const char *psz : keys)
      bKnown = bKnown || m.first == psz;
    if (!bKnown)
      return r.Bad(m.second, at + " has no field " + m.first);
  }
  return true;
}

// A value a block may leave out, in which case what is there already stands: the defaults are
// the drawing's own, so a look that says nothing of a block's field draws as the engine would.
bool MarkNum(const MarkRead &r, const PortJson &o, const char *pszKey, double dMin, double dMax,
             double *pd, const std::string &at) {
  const PortJson &v = o[pszKey];
  if (v.kind == PortJson::NONE)
    return true;
  if (v.kind != PortJson::NUMBER || v.d < dMin || v.d > dMax) {
    char sz[96];
    snprintf(sz, sizeof sz, " takes a number from %g to %g", dMin, dMax);
    return r.Bad(v, at + "." + pszKey + sz);
  }
  *pd = v.d;
  return true;
}

bool MarkInt(const MarkRead &r, const PortJson &o, const char *pszKey, double dMin, double dMax,
             int *pn, const std::string &at) {
  double d = *pn;
  if (!MarkNum(r, o, pszKey, dMin, dMax, &d, at))
    return false;
  *pn = static_cast<int>(d);
  return true;
}

bool MarkFlag(const MarkRead &r, const PortJson &o, const char *pszKey, bool *pb,
              const std::string &at) {
  const PortJson &v = o[pszKey];
  if (v.kind == PortJson::NONE)
    return true;
  if (v.kind != PortJson::BOOL)
    return r.Bad(v, at + "." + pszKey + " is true or false");
  *pb = v.b;
  return true;
}

bool MarkRgb(const MarkRead &r, const PortJson &o, const char *pszKey, unsigned int *pn,
             const std::string &at) {
  const PortJson &v = o[pszKey];
  if (v.kind == PortJson::NONE)
    return true;
  if (v.kind != PortJson::STRING || !PortMapMarkRgbOf(v.s.c_str(), pn))
    return r.Bad(v, at + "." + pszKey + " is a colour, \"#rrggbb\"");
  return true;
}

// A value named in words, as a script names it: the fade's curve, what answers a crossing, what
// is drawn outside.
bool MarkName(const MarkRead &r, const PortJson &o, const char *pszKey, int (*fn)(const char *),
              int *pn, const std::string &at, const char *pszWhich) {
  const PortJson &v = o[pszKey];
  if (v.kind == PortJson::NONE)
    return true;
  const int n = v.kind == PortJson::STRING ? fn(v.s.c_str()) : -1;
  if (n < 0)
    return r.Bad(v, at + "." + pszKey + " is " + pszWhich);
  *pn = n;
  return true;
}

// Where a block is drawn. The field, the cells and the tear stand up the screen from the edge,
// which the ground cannot be painted with, so they are drawn over the units and take no other
// layer; the light outside lies flat and may be painted or drawn over the game.
bool MarkLayer(const MarkRead &r, const PortJson &o, bool bOverOnly, int *pnLayer,
               const std::string &at) {
  const PortJson &v = o["layer"];
  if (v.kind == PortJson::NONE)
    return true;
  const int n = v.kind == PortJson::STRING ? PortMapMarkLayerOf(v.s.c_str()) : -1;
  if (n < 0)
    return r.Bad(v, at + ".layer is \"ground\" or \"over\"");
  if (bOverOnly && n != MAP_MARK_OVER)
    return r.Bad(v, at + " stands up from the edge, which the ground cannot be painted with, so "
                        "its layer is \"over\"");
  *pnLayer = n;
  return true;
}

// The cells' rows: each row's height, as a part of the field's.
bool MarkRows(const MarkRead &r, const PortJson &o, std::vector<double> *pOut,
              const std::string &at) {
  const PortJson &v = o["rows"];
  if (v.kind == PortJson::NONE)
    return true;
  if (v.kind != PortJson::ARRAY || v.items.empty() || v.items.size() > 8)
    return r.Bad(v, at + ".rows is a list of 1 to 8 heights");
  pOut->clear();
  for (const PortJson &one : v.items) {
    if (one.kind != PortJson::NUMBER || one.d < 0.0 || one.d > 4.0)
      return r.Bad(one, at + ".rows takes a number from 0 to 4");
    pOut->push_back(one.d);
  }
  return true;
}

bool MarkField(const MarkRead &r, const PortJson &o, const std::string &at, PortMarkField *p) {
  int nLayer = MAP_MARK_OVER;
  p->bOn = true;
  return MarkOnly(r, o, at, {"layer", "height", "bands", "alpha", "fade", "add", "lean",
                             "edge_on"}) &&
         MarkLayer(r, o, true, &nLayer, at) &&
         MarkNum(r, o, "height", 0, 4096, &p->fHigh, at) &&
         MarkInt(r, o, "bands", 1, 32, &p->nBands, at) &&
         MarkNum(r, o, "alpha", 0, 1, &p->fAlpha, at) &&
         MarkNum(r, o, "fade", 0, 1, &p->fFade, at) && MarkFlag(r, o, "add", &p->bAdd, at) &&
         MarkNum(r, o, "lean", 0, 4, &p->fLean, at) &&
         MarkNum(r, o, "edge_on", 0, 4, &p->fEdgeOn, at);
}

bool MarkCells(const MarkRead &r, const PortJson &o, const std::string &at, PortMarkCells *p) {
  int nLayer = MAP_MARK_OVER;
  p->bOn = true;
  if (!MarkOnly(r, o, at, {"layer", "shape", "spacing", "stagger", "radius", "squash", "width",
                           "alpha", "least", "base", "swing", "rate", "along", "row", "warm",
                           "rows"}) ||
      !MarkLayer(r, o, true, &nLayer, at))
    return false;
  const PortJson &v = o["shape"];
  if (v.kind != PortJson::NONE && (v.kind != PortJson::STRING || v.s != "hex"))
    return r.Bad(v, at + ".shape is \"hex\", the only cell this build draws");
  return MarkNum(r, o, "spacing", 1, 4096, &p->fStep, at) &&
         MarkNum(r, o, "stagger", 0, 1, &p->fStagger, at) &&
         MarkNum(r, o, "radius", 0, 256, &p->fRadius, at) &&
         MarkNum(r, o, "squash", 0, 4, &p->fSquash, at) &&
         MarkNum(r, o, "width", 0, 64, &p->fWidth, at) &&
         MarkNum(r, o, "alpha", 0, 1, &p->fAlpha, at) &&
         MarkNum(r, o, "least", 0, 1, &p->fLeast, at) &&
         MarkNum(r, o, "base", 0, 1, &p->fBase, at) &&
         MarkNum(r, o, "swing", 0, 1, &p->fSwing, at) &&
         MarkNum(r, o, "rate", 0, 64, &p->fRate, at) &&
         MarkNum(r, o, "along", 0, 64, &p->fAlong, at) &&
         MarkNum(r, o, "row", 0, 64, &p->fRow, at) &&
         MarkNum(r, o, "warm", 0, 1, &p->fWarm, at) && MarkRows(r, o, &p->rows, at);
}

bool MarkFoot(const MarkRead &r, const PortJson &o, const std::string &at, PortMarkFoot *p) {
  p->bOn = true;
  return MarkOnly(r, o, at, {"glow", "alpha"}) && MarkNum(r, o, "glow", 1, 64, &p->fGlow, at) &&
         MarkNum(r, o, "alpha", 0, 1, &p->fAlpha, at);
}

bool MarkTear(const MarkRead &r, const PortJson &o, const std::string &at, PortMarkTear *p) {
  int nLayer = MAP_MARK_OVER;
  p->bOn = true;
  return MarkOnly(r, o, at, {"layer", "life", "width", "grow", "height", "alpha", "lean"}) &&
         MarkLayer(r, o, true, &nLayer, at) && MarkNum(r, o, "life", 0, 60, &p->fLife, at) &&
         MarkNum(r, o, "width", 0, 4096, &p->fWide, at) &&
         MarkNum(r, o, "grow", 0, 4096, &p->fGrow, at) &&
         MarkNum(r, o, "height", 0, 16, &p->fHigh, at) &&
         MarkNum(r, o, "alpha", 0, 1, &p->fAlpha, at) &&
         MarkNum(r, o, "lean", 0, 4, &p->fLean, at);
}

bool MarkRipple(const MarkRead &r, const PortJson &o, const std::string &at, PortMarkRipple *p) {
  return MarkOnly(r, o, at, {"speed", "width", "life", "flash", "flash_life", "side"}) &&
         MarkNum(r, o, "speed", 0, 16384, &p->fSpeed, at) &&
         MarkNum(r, o, "width", 1, 4096, &p->fWide, at) &&
         MarkNum(r, o, "life", 0.01, 60, &p->fLife, at) &&
         MarkNum(r, o, "flash", 1, 4096, &p->fFlash, at) &&
         MarkNum(r, o, "flash_life", 0.01, 60, &p->fFlashLife, at) &&
         MarkNum(r, o, "side", 0, 1, &p->fSide, at);
}

bool MarkOutside(const MarkRead &r, const PortJson &o, const std::string &at, PortMarkOutside *p) {
  return MarkOnly(r, o, at, {"layer", "style", "band", "radius", "alpha", "base", "swing", "beat",
                             "past", "color", "colour", "far_color", "far_colour"}) &&
         MarkLayer(r, o, false, &p->nLayer, at) &&
         MarkName(r, o, "style", PortMapMarkOutsideOf, &p->nStyle, at,
                  "\"tint\", \"ring\", \"tether\" or \"none\"") &&
         MarkNum(r, o, "band", 0, 1 << 20, &p->fBand, at) &&
         MarkNum(r, o, "radius", 1, 4096, &p->fRadius, at) &&
         MarkNum(r, o, "alpha", 0, 1, &p->fAlpha, at) &&
         MarkNum(r, o, "base", 0, 1, &p->fBase, at) &&
         MarkNum(r, o, "swing", 0, 1, &p->fSwing, at) &&
         MarkNum(r, o, "beat", 0, 64, &p->fBeat, at) && MarkNum(r, o, "past", 0, 64, &p->fPast, at) &&
         MarkRgb(r, o, "color", &p->nNear, at) && MarkRgb(r, o, "colour", &p->nNear, at) &&
         MarkRgb(r, o, "far_color", &p->nFar, at) && MarkRgb(r, o, "far_colour", &p->nFar, at);
}

bool MarkMini(const MarkRead &r, const PortJson &o, const std::string &at, PortMarkMini *p) {
  p->bOn = true;
  if (!MarkOnly(r, o, at, {"on", "fill", "edge", "width", "dash", "player", "color", "colour"}) ||
      !MarkFlag(r, o, "on", &p->bOn, at) || !MarkNum(r, o, "fill", 0, 1, &p->fFill, at) ||
      !MarkNum(r, o, "edge", 0, 1, &p->fEdge, at) || !MarkNum(r, o, "width", 0, 64, &p->fWidth, at) ||
      !MarkNum(r, o, "dash", 0, 4096, &p->fDash, at) ||
      !MarkInt(r, o, "player", 0, 31, &p->nPlayer, at))
    return false;
  const unsigned int nWas = p->nRgb;
  if (!MarkRgb(r, o, "color", &p->nRgb, at) || !MarkRgb(r, o, "colour", &p->nRgb, at))
    return false;
  p->bRgb = p->nRgb != nWas || o["color"].kind != PortJson::NONE || o["colour"].kind != PortJson::NONE;
  return true;
}

// One look: the mark's own values it asks for, the colour its blocks answer a unit on its way
// out in, and then the blocks themselves. A block the look leaves out is not drawn at all.
bool MarkLook(const MarkRead &r, const PortJson &o, const std::string &at, PortMarkLook *p) {
  PortMarkAsks &asks = p->asks;
  if (!MarkOnly(r, o, at, {"warn", "fill", "edge", "edge_width", "dash", "soft", "pulse",
                           "pulse_depth", "curve", "crossing", "field", "cells", "foot", "tear",
                           "ripple", "outside", "minimap"}) ||
      !MarkRgb(r, o, "warn", &p->nWarn, at) || !MarkNum(r, o, "fill", 0, 1, &asks.fFill, at) ||
      !MarkNum(r, o, "edge", 0, 1, &asks.fEdge, at) ||
      !MarkNum(r, o, "edge_width", 0, 1024, &asks.fWidth, at) ||
      !MarkNum(r, o, "dash", 0, 4096, &asks.fDash, at) ||
      !MarkNum(r, o, "soft", 0, 4, &asks.fSoft, at) ||
      !MarkNum(r, o, "pulse", 0, 60, &asks.fPulse, at) ||
      !MarkNum(r, o, "pulse_depth", 0, 1, &asks.fDepth, at) ||
      !MarkName(r, o, "curve", PortMapMarkCurveOf, &asks.nCurve, at,
                "\"smooth\", \"even\" or \"sharp\"") ||
      !MarkName(r, o, "crossing", PortMapMarkCrossingOf, &asks.nCross, at,
                "\"ripple\", \"flash\", \"stamp\", \"arc\" or \"none\""))
    return false;
  const PortJson &field = o["field"], &cells = o["cells"], &foot = o["foot"];
  const PortJson &tear = o["tear"], &ripple = o["ripple"], &outside = o["outside"];
  if (field.kind != PortJson::NONE && !MarkField(r, field, at + ".field", &p->field))
    return false;
  if (cells.kind != PortJson::NONE && !MarkCells(r, cells, at + ".cells", &p->cells))
    return false;
  if (foot.kind != PortJson::NONE && !MarkFoot(r, foot, at + ".foot", &p->foot))
    return false;
  if (tear.kind != PortJson::NONE && !MarkTear(r, tear, at + ".tear", &p->tear))
    return false;
  if (ripple.kind != PortJson::NONE && !MarkRipple(r, ripple, at + ".ripple", &p->ripple))
    return false;
  if (outside.kind != PortJson::NONE && !MarkOutside(r, outside, at + ".outside", &p->outside))
    return false;
  const PortJson &mini = o["minimap"];
  return mini.kind == PortJson::NONE || MarkMini(r, mini, at + ".minimap", &p->mini);
}

// types.json's "marks", read into the looks this build holds. A look of a name already loaded,
// the mode's over the mod's, replaces it.
bool MarkLooksRead(const PortJson &marks, const char *pszText) {
  const MarkRead r = {pszText};
  if (marks.kind == PortJson::NONE)
    return true;
  if (marks.kind != PortJson::OBJECT)
    return Fail("types.json's marks is an object, a look to a name");
  for (const PortJsonMember &m : marks.members) {
    if (m.first == "comment")
      continue;
    PortMarkLook look;
    if (!MarkLook(r, m.second, "marks." + m.first, &look))
      return false;
    g_markLooks[m.first] = look;
  }
  g_markLookNames.clear();
  for (const auto &one : g_markLooks)
    g_markLookNames += (g_markLookNames.empty() ? "" : ", ") + one.first;
  return true;
}

// One submission: types.json and iscript.txt from the data folder, or the texts given (a
// push), and the art its images name from the folder.
bool Load(const std::string &key, const std::string &folder, const std::string *pTypes, const std::string *pScript,
          BCRYPT_HASH_HANDLE hHash) {
  g_loadKey = key;
  g_loadFolder = folder;
  {
    std::lock_guard<std::mutex> lock(g_mediaLock);
    g_mediaFolders[key] = folder;
  }
  std::vector<BYTE> text;
  if (pTypes)
    text.assign(pTypes->begin(), pTypes->end());
  else if (!PortReadFile(folder + "types.json", &text))
    return Fail(folder + "types.json does not read");
  Hash(hHash, "types.json", text);
  text.push_back(0);
  std::string error;
  g_typesKept.emplace_back();
  g_pTypes = &g_typesKept.back();
  if (!PortJsonParse(reinterpret_cast<const char *>(text.data()), &g_types, &error) ||
      g_types.kind != PortJson::OBJECT)
    return Fail("types.json does not read: " + error);
  // the player's language's text in place of the mod's own, before anything is made of it
  ModLangApply(key, &g_types);
  const int nSub = ++g_nSubs;
  g_subFolders[nSub] = folder;
  size_t anFrom[TABLES];
  std::vector<Record> edits[TABLES];  // the submission's edits, in the order of their names
  for (const auto &m : g_types.members)
    if (TableIndex(m.first.c_str()) < 0 && m.first != "cards" && m.first != "races" && m.first != "statuses" &&
        m.first != "tilesets" && m.first != "marks" && m.first != "game" && m.first != "generate" &&
        m.first != "comment")
      return Fail("types.json has no table " + m.first);
  // the looks its marks may wear, which nothing else reads, checked here so that a look with a
  // field this build has no name for stops the mod rather than being drawn without it
  if (!MarkLooksRead(g_types["marks"], reinterpret_cast<const char *>(text.data())))
    return false;

  auto byName = [](const Record &a, const Record &b) { return a.name < b.name; };
  for (int t = 0; t < TABLES; ++t) {
    const Table &tab = kTables[t];
    ModTable &m = g_tables[t];
    if (m.file.empty() && !LoadTableFile(t))
      return false;
    const size_t nFrom = anFrom[t] = m.recs.size();
    for (const auto &rec : g_types[tab.psz].members) {
      Record r;
      r.name = rec.first;
      r.pJson = &rec.second;
      r.sub = nSub;
      (rec.second["edit"].kind != PortJson::NONE ? edits[t] : m.recs).push_back(r);
    }
    std::sort(m.recs.begin() + nFrom, m.recs.end(), byName);
    std::sort(edits[t].begin(), edits[t].end(), byName);
    // techs and upgrades too past 1.17's rows, from 45 and 62, no longer in its unused ones,
    // which EUD maps may use
    for (size_t i = nFrom; i < m.recs.size(); ++i) {
      Record &r = m.recs[i];
      if (tab.nModFirst + static_cast<int>(i) < tab.nModMax)
        r.id = tab.nModFirst + static_cast<int>(i);
      if (r.id < 0) {
        char sz[96];
        snprintf(sz, sizeof sz, " has %u records, past the %d the build holds",
                 static_cast<unsigned>(m.recs.size()), tab.nModMax - tab.nModFirst);
        return Fail(std::string(tab.psz) + sz);
      }
    }
  }

  // The scripts, which name the mod's images, sprites and flingy and are named by images.
  std::vector<BYTE> script;
  std::vector<unsigned char> block;
  std::vector<IscriptHeader> headers;
  if (pScript ? !pScript->empty() : PortReadFile(folder + "iscript.txt", &script)) {
    if (pScript)
      script.assign(pScript->begin(), pScript->end());
    Hash(hHash, "iscript.txt", script);
    const std::string s(script.begin(), script.end());
    IscriptNames names = [](const char *pszKind, const std::string &name) {
      const int t = strcmp(pszKind, "image") == 0    ? IMAGES
                    : strcmp(pszKind, "sprite") == 0 ? SPRITES
                    : strcmp(pszKind, "weapon") == 0 ? WEAPONS
                    : strcmp(pszKind, "sound") == 0  ? SOUNDS
                                                     : FLINGY;
      return Find(t, name);
    };
    if (!IscriptCompile(s, ImageScriptsEnd(), names, &block, &headers, &error))
      return Fail(error);
    for (size_t i = 0; i < headers.size(); ++i)
      g_scripts[headers[i].name] = 1000 + g_nScripts + static_cast<int>(i);
  }

  // the edits first, so a new record based on a row an edit changes starts from the change
  std::vector<Record *> changed[TABLES];
  for (int t = 0; t < TABLES; ++t)
    for (Record &e : edits[t])
      if (!Edit(t, e, nSub, &changed[t]))
        return false;
  for (int t = 0; t < TABLES; ++t)
    for (size_t i = anFrom[t]; i < g_tables[t].recs.size(); ++i)
      if (!Build(t, g_tables[t].recs[i]))
        return false;
  for (size_t i = anFrom[IMAGES]; i < g_tables[IMAGES].recs.size(); ++i)
    if (!ImageArt(folder, g_tables[IMAGES].recs[i], hHash))
      return false;
  for (size_t i = anFrom[UNITS]; i < g_tables[UNITS].recs.size(); ++i)
    LooksVoices(g_tables[UNITS].recs[i]);
  // a mod's add-on sits where the classic add-on it looks like sits: the game keeps add-on
  // positions for the classic buildings only (ModRulesAddOnOffset)
  for (size_t i = anFrom[UNITS]; i < g_tables[UNITS].recs.size(); ++i) {
    const Record &r = g_tables[UNITS].recs[i];
    const std::vector<BYTE> &flags = r.cols[ColumnOf(UNITS, "flags")];
    const int nLooks = Resolve("units", (*r.pJson)["looks"], "units " + r.name + ".looks");
    const unsigned looks = static_cast<unsigned>(nLooks >= 0 && nLooks < UNIT_TYPES_117 ? nLooks : r.base117);
    if (flags.empty() || !(flags[0] & 2))
      continue;
    const std::vector<BYTE> classic =
        looks < UNIT_TYPES_117 ? FileRow(UNITS, static_cast<int>(looks))[ColumnOf(UNITS, "flags")] : std::vector<BYTE>();
    if (looks - 106 >= 96 || classic.empty() || !(classic[0] & 2))
      return Fail("units " + r.name + " is an add-on, so it is made from a classic add-on or looks like one, "
                  "whose place beside its building it takes");
  }
  for (size_t i = anFrom[UNITS]; i < g_tables[UNITS].recs.size(); ++i)
    if (!UnitArtOf(g_tables[UNITS].recs[i]))
      return false;
  for (const Record &e : edits[UNITS])
    if (!UnitArtOf(e))
      return false;
  // the requirements: a new record's are the mod's own, and so are an edit's of a mod's
  // record; an edit's of one of the original's rows are kept apart (ModTreeEdit)
  auto trees = [&](int t, const char *pszField, int nKind) {
    for (size_t i = anFrom[t]; i < g_tables[t].recs.size(); ++i)
      if (!Tree(g_tables[t].recs[i], pszField, &g_trees.own[nKind], t == UNITS ? g_tables[t].recs[i].base117 : -1))
        return false;
    for (const Record &e : edits[t]) {
      const bool bOwn = std::any_of(changed[t].begin(), changed[t].end(), [&](const Record *p) { return p->id == e.id; });
      if (!Tree(e, pszField, bOwn ? &g_trees.own[nKind] : &g_trees.edit[nKind], t == UNITS ? e.base117 : -1))
        return false;
    }
    return true;
  };
  if (!trees(UNITS, "requires", MOD_TREE_UNIT) || !trees(TECHS, "use_requires", MOD_TREE_TECH_USE) ||
      !trees(TECHS, "requires", MOD_TREE_TECH_RESEARCH) || !trees(UPGRADES, "requires", MOD_TREE_UPGRADE))
    return false;
  for (const Record &e : edits[UNITS])
    if ((*e.pJson)["name"].kind == PortJson::STRING)
      g_unitNames[e.id] = (*e.pJson)["name"].s;
  // a submission's cards and races are the mod's from then on; the game's slots keep their
  // races by id, as a push in a game may bring them in another order
  std::string slotRaceIds[PLAYER_SLOTS_MAX];
  for (int s = 0; s < PLAYER_SLOTS_MAX; ++s)
    if (const Race *r = RaceAt(g_slotRace[s]))
      slotRaceIds[s] = r->id;
  if (g_types["cards"].kind != PortJson::NONE || nSub == 1) {
    if (!g_bAddCards) {
      g_buttons.clear();
      g_words.clear();
      g_cardAiCalls.clear();
    }
  }
  if (g_types["races"].kind != PortJson::NONE || nSub == 1)
    g_races.clear();
  // the statuses, auras and action blocks, once every record has its id
  std::vector<ModStatusRecord> statusRecs;
  for (int t : {UNITS, WEAPONS, ORDERS}) {
    for (size_t i = anFrom[t]; i < g_tables[t].recs.size(); ++i)
      statusRecs.push_back({t, g_tables[t].recs[i].id, g_tables[t].recs[i].name, g_tables[t].recs[i].pJson});
    for (const Record &e : edits[t])
      statusRecs.push_back({t, e.id, e.name, e.pJson});
  }
  // the scripts first, whose functions the blocks and a card's ai may name
  std::vector<std::pair<std::string, std::string>> luaFiles;
  if (!ModLuaLoad(g_types, folder, nSub, statusRecs, &luaFiles, &error))
    return Fail(error);
  for (const auto &f : luaFiles)
    Hash(hHash, f.first, std::vector<BYTE>(f.second.begin(), f.second.end()));
  if (!ModStatusLoad(g_types, nSub, statusRecs, &error))
    return Fail(error);
  if ((g_types["cards"].kind != PortJson::NONE && !Cards()) || (g_types["races"].kind != PortJson::NONE && !Races()))
    return false;
  for (int t = 0; t < TABLES; ++t) {
    if (t == TECHS || t == UPGRADES)  // 1.17's rows, written for each game that has the mod
      continue;
    for (size_t i = anFrom[t]; i < g_tables[t].recs.size(); ++i)
      if (!Store(t, g_tables[t].recs[i]))
        return false;
    for (const Record *p : changed[t])
      if (!Store(t, *p))
        return false;
  }
  if (!headers.empty()) {
    if (!ImageScriptsAppend(block.data(), static_cast<DWORD>(block.size())))
      return Fail("no memory for the scripts");
    for (const IscriptHeader &h : headers)
      g_scriptHeaders.push_back(h.nHeader);
    g_nScripts += static_cast<int>(headers.size());
  }

  CountsSet();
  // once it has all loaded: a failed push leaves the slots as PutBack leaves the races
  for (int s = 0; s < PLAYER_SLOTS_MAX; ++s) {
    if (slotRaceIds[s].empty())
      continue;
    g_slotRace[s] = 0;
    for (size_t i = 0; i < g_races.size(); ++i)
      if (g_races[i].id == slotRaceIds[s])
        g_slotRace[s] = static_cast<BYTE>(i + 1);
  }
  return true;
}

// What a load changes, kept as it was: before the mod's load at start-up and before each push,
// for when it does not load, and before the first push of a game.
struct Kept {
  bool bKept = false;
  std::vector<Record> recs[TABLES];
  std::vector<Record> edits[TABLES];
  size_t nTypes;
  std::map<std::string, int> scripts;
  Trees trees;
  std::map<int, std::string> unitNames;
  std::map<int, UnitArt> unitArt;
  std::vector<std::string> strings;
  std::vector<ModCardButton> buttons;
  std::vector<Words> words;
  std::vector<std::string> cardAiCalls;
  std::vector<Race> races;
  std::map<std::string, PortMarkLook> markLooks;
  std::string markLookNames;
  HxTypeCounts counts;
  int nSubs;
  std::string id;
  std::string depends;
};

void Keep(Kept *p) {
  p->bKept = true;
  for (int t = 0; t < TABLES; ++t) {
    p->recs[t] = g_tables[t].recs;
    p->edits[t] = g_tables[t].edits;
  }
  p->nTypes = g_typesKept.size();
  p->scripts = g_scripts;
  p->trees = g_trees;
  p->unitNames = g_unitNames;
  p->unitArt = g_unitArt;
  p->strings = g_strings;
  p->buttons = g_buttons;
  p->words = g_words;
  p->cardAiCalls = g_cardAiCalls;
  p->races = g_races;
  p->markLooks = g_markLooks;
  p->markLookNames = g_markLookNames;
  p->counts = g_counts;
  p->nSubs = g_nSubs;
  p->id = g_id;
  p->depends = g_depends;
}

// Back as it was, the rows stored at start-up too, which an edit of an earlier submission's
// record may have changed. The scripts' ids go on from the last, as the image scripts keep
// every script appended, and their table (g_scriptHeaders) every header.
void PutBack(const Kept &k) {
  for (int t = 0; t < TABLES; ++t) {
    g_tables[t].recs = k.recs[t];
    g_tables[t].edits = k.edits[t];
    if (t != TECHS && t != UPGRADES)  // written for each game
      for (const Record &r : g_tables[t].recs)
        Store(t, r);
  }
  while (g_typesKept.size() > k.nTypes)
    g_typesKept.pop_back();
  g_scripts = k.scripts;
  g_trees = k.trees;
  g_unitNames = k.unitNames;
  g_unitArt = k.unitArt;
  g_strings = k.strings;
  g_buttons = k.buttons;
  g_words = k.words;
  g_cardAiCalls = k.cardAiCalls;
  g_races = k.races;
  g_markLooks = k.markLooks;
  g_markLookNames = k.markLookNames;
  g_counts = k.counts;
  MediaCountsSet();
  g_nSubs = k.nSubs;
  ModStatusTrim(k.nSubs);
  ModLuaTrim(k.nSubs);
  g_id = k.id;
  g_depends = k.depends;
}

// The original's rows the edits change, as the edits have them, for a game that has the mod:
// after the game's tables are read (ModDataForGame) and before the map's settings, which keep
// the last word.
void EditsStore() {
  for (int t = 0; t < TABLES; ++t)
    for (const Record &e : g_tables[t].edits)
      Store(t, e);
}

// And as the original has them, for a game without the mod. images.dat is read once, at
// start-up; the other tables are read again for each game, before ModDataForGame.
void EditsPutBack() {
  for (int t = 0; t < TABLES; ++t)
    for (const Record &e : g_tables[t].edits) {
      Record r = e;
      r.cols = FileRow(t, e.id);
      Store(t, r);
    }
}

const unsigned short *TreeOf(const std::map<int, std::vector<WORD>> &trees, unsigned n) {
  auto it = trees.find(static_cast<int>(n));
  return it == trees.end() ? nullptr : it->second.data();
}

}  // namespace

void ModDataStartUp() {
  const std::vector<PortMod> &mods = PortMods();
  const char *pszWanted = PortCommandOptions()->szMod;
  int nMod = PortModActive();
  if (pszWanted[0] != '\0') {
    nMod = -1;
    for (size_t i = 0; i < mods.size(); ++i)
      if (mods[i].id == pszWanted)
        nMod = static_cast<int>(i);
    if (nMod < 0) {
      Log("mods: --mod %s names no mod in %s", pszWanted, PortModsFolder());
      return;
    }
  }
  const PortMod &mod = mods[nMod];
  // the mod's data after the data of the mods it depends on, each a submission, under one
  // fingerprint
  std::vector<int> chain;
  std::string depends;
  bool bOk = PortModChain(nMod, &chain, &g_error);
  std::vector<std::string> langChain;
  for (int k : chain)
    langChain.push_back(mods[k].id);
  ModLangChain(langChain);
  if (bOk && std::all_of(chain.begin(), chain.end(), [&](int k) { return mods[k].data.empty(); }))
    return;
  BCRYPT_HASH_HANDLE hHash = NULL;
  if (BCryptCreateHash(BCRYPT_SHA1_ALG_HANDLE, &hHash, NULL, 0, NULL, 0, 0) != 0)
    return;
  Kept before;
  Keep(&before);
  for (size_t i = 0; bOk && i < chain.size(); ++i) {
    const PortMod &m = mods[chain[i]];
    if (m.data.empty())
      continue;
    bOk = Load(m.id, m.folder + m.data, nullptr, nullptr, hHash);
    if (bOk) {
      bOk = ModTilesetLoad(g_types, m.folder + m.data, &g_error);  // its tilesets' changes
    }
    if (!bOk && chain[i] != nMod)
      g_error = "in " + m.name + ", which it depends on: " + g_error;
    if (bOk && chain[i] != nMod)
      depends += (depends.empty() ? "" : ", ") + m.id + " " + m.version;
  }
  BCryptFinishHash(hHash, g_abFingerprint, sizeof g_abFingerprint, 0);
  BCryptDestroyHash(hHash);
  if (!bOk) {
    Log("mods: %s's data does not load: %s", mod.id, g_error);
    if (PortCommandOptions()->szModCheck[0] == '\0')  // the check reports it
      MessageBoxA(NULL, ("The mod " + mod.name + "'s data does not load:\n\n" + g_error +
                         "\n\nHyperion goes on without it.").c_str(),
                  "Hyperion", MB_OK | MB_ICONWARNING);
    // put back as if it had not been, with no fingerprint, so a LAN game sees a Hyperion
    // without a mod; what it wrote past 1.17's rows (frames, rows) no count reaches
    PutBack(before);
    ModTilesetClear();
    memset(g_abFingerprint, 0, sizeof g_abFingerprint);
    return;
  }
  g_id = mod.id;
  g_name = mod.name;
  g_depends = depends;
  size_t nEdits = 0;
  for (const ModTable &m : g_tables)
    nEdits += m.edits.size();
  char sz[260];
  snprintf(sz, sizeof sz, "%u units, %u weapons, %u flingy, %u sprites, %u images, %u orders, %u techs, %u upgrades, %u sounds, %u portraits, %u scripts, %u races, %u edits",
           static_cast<unsigned>(g_tables[UNITS].recs.size()), static_cast<unsigned>(g_tables[WEAPONS].recs.size()),
           static_cast<unsigned>(g_tables[FLINGY].recs.size()), static_cast<unsigned>(g_tables[SPRITES].recs.size()),
           static_cast<unsigned>(g_tables[IMAGES].recs.size()), static_cast<unsigned>(g_tables[ORDERS].recs.size()),
           static_cast<unsigned>(g_tables[TECHS].recs.size()), static_cast<unsigned>(g_tables[UPGRADES].recs.size()),
           static_cast<unsigned>(g_tables[SOUNDS].recs.size()), static_cast<unsigned>(g_tables[PORTRAITS].recs.size()),
           static_cast<unsigned>(g_scripts.size()),
           static_cast<unsigned>(g_races.size()), static_cast<unsigned>(nEdits));
  Log("mods: %s's data%s%s: %s", mod.id, depends.empty() ? "" : ", with ", depends, sz);
}

const char *ModDataId() {
  return g_id.c_str();
}

void ModDataNote(const char *pszWhat) {
  Note(pszWhat);
}

bool ModDataCheckResult(std::string *pError, std::vector<std::string> *pNotes, std::vector<std::string> *pTables,
                        std::vector<ModCheckFile> *pFiles) {
  *pNotes = g_checkNotes;
  if (!g_error.empty()) {
    *pError = g_error;
    return false;
  }
  char sz[256];
  for (int t = 0; t < TABLES; ++t) {
    const Table &tab = kTables[t];
    const ModTable &m = g_tables[t];
    if (m.recs.empty() && m.edits.empty())
      continue;
    std::string line = tab.psz;
    if (!m.recs.empty()) {
      snprintf(sz, sizeof sz, ": %u new, ids %d to %d of the %d to %d the build holds",
               static_cast<unsigned>(m.recs.size()), m.recs.front().id, m.recs.back().id, tab.nModFirst,
               tab.nModMax - 1);
      line += sz;
    }
    if (!m.edits.empty()) {
      snprintf(sz, sizeof sz, "%s%u edit%s", m.recs.empty() ? ": " : "; ", static_cast<unsigned>(m.edits.size()),
               m.edits.size() == 1 ? "" : "s");
      line += sz;
    }
    pTables->push_back(line);
  }
  if (!g_scripts.empty())
    pTables->push_back("scripts: " + std::to_string(g_scripts.size()));
  if (!g_races.empty())
    pTables->push_back("races: " + std::to_string(g_races.size()));
  for (const std::string &s : ModTilesetSummary())
    pTables->push_back(s);

  // the files the records name, those the folders have (the load noted the others)
  auto Add = [&](const std::string &where, const std::string &path, const std::string &game, ModCheckKind kind) {
    if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES)
      pFiles->push_back(ModCheckFile{where, path, game, kind});
  };
  char szPath[MAX_PATH];
  for (int t : {SOUNDS, PORTRAITS})
    for (const std::vector<Record> *pList : {&g_tables[t].recs, &g_tables[t].edits})
      for (const Record &r : *pList)
        for (int c = 0; c < PathColumns(t); ++c) {
          const char *pszField = kTables[t].pColumns[c].psz;
          const char *pszName = PathOf(r.cols[c]);
          if ((*r.pJson)[pszField].kind == PortJson::NONE || pszName == nullptr)
            continue;
          const std::string where = std::string(kTables[t].psz) + " " + r.name + "." + pszField;
          if (t == SOUNDS) {
            if (ModDataFilePath(pszName, szPath, sizeof szPath))
              Add(where, szPath, std::string("sound\\") + pszName, MOD_CHECK_SOUND);
            continue;
          }
          // a portrait's videos: a variant's digit, then the first of the extensions the folder has
          for (char cDigit = '0'; cDigit <= '3'; ++cDigit) {
            std::vector<const char *> exts;
            for (const char *const *ppszExt = PortVideoExtensions(); *ppszExt; ++ppszExt)
              exts.push_back(*ppszExt);
            exts.push_back(".smk");
            std::string first;
            for (const char *pszExt : exts) {
              std::string name = pszName + std::string(1, cDigit) + pszExt;
              if (!ModDataFilePath(name.c_str(), szPath, sizeof szPath) ||
                  GetFileAttributesA(szPath) == INVALID_FILE_ATTRIBUTES)
                continue;
              name = name.substr(name.find_last_of('\\') + 1);
              if (first.empty()) {
                first = name;
                Add(where, szPath, "", MOD_CHECK_VIDEO);
              } else {
                pNotes->push_back(where + ": " + name + " is passed over for " + first +
                                  ", as .webm, .mkv, .mp4, .m4v, .mov and .smk are tried in that order");
              }
            }
          }
        }
  for (const Record &r : g_tables[IMAGES].recs) {
    const PortJson &grp = (*r.pJson)["grp"];
    if (grp.kind == PortJson::NONE)
      continue;
    const std::string &folder = g_subFolders[r.sub];
    const bool bSheet = grp.kind == PortJson::OBJECT || grp.kind == PortJson::ARRAY || PortImageFileName(grp.Str());
    if (!bSheet) {
      Add("images " + r.name + ".grp", folder + grp.Str(), "", MOD_CHECK_GRP);
      continue;
    }
    for (const char *pszField : {"grp", "team"}) {
      std::vector<SheetFileLevel> levels;
      SheetFiles(r, pszField, &levels);
      for (const SheetFileLevel &l : levels)
        for (const std::string &part : l.parts)
          Add("images " + r.name + "." + pszField, folder + part, "", MOD_CHECK_PICTURE);
    }
  }
  for (const auto &u : g_unitArt)
    for (int k = 0; k < 3; ++k) {
      const std::string &file = u.second.file[k];
      if (file.empty() || !ModDataFilePath(file.c_str(), szPath, sizeof szPath))
        continue;
      std::string name = std::to_string(u.first);
      for (const std::vector<Record> *pList : {&g_tables[UNITS].recs, &g_tables[UNITS].edits})
        for (const Record &r : *pList)
          if (r.id == u.first)
            name = r.name;
      Add("units " + name + "." + kArtFields[k], szPath, "", ArtFile(file) && _stricmp(file.c_str() + file.size() - 4, ".grp") == 0 ? MOD_CHECK_GRP : MOD_CHECK_PICTURE);
    }
  return true;
}

const char *ModDataName() {
  return g_name.c_str();
}

const unsigned char *ModDataFingerprint() {
  return g_abFingerprint;
}

const char *ModDataDepends() {
  return g_depends.c_str();
}

namespace {

Kept g_startUp;  // before the first push of the game, put back as a new game starts
bool g_bPushed;  // the game being played has had a push
std::string g_liveMod;  // the mod pushed last
// ---- a game's mode (port_mode.h) --------------------------------------------------------------------
// A mode's own data loads as a submission over the active mod's as the game's tables are
// written, and the records its generate makes as a second one. Both go away with the game, as a
// push does, so the front end and the next game have the data loaded at start-up.

bool g_bMode;             // the game's mode loaded, so its submissions are put away after it
std::string g_modeRun;    // what its generate returned as run, read by the scripts as hx.run

// The name of a row of a table: a mod's record of that id, else the original's name; "" for
// neither.
std::string NameOfRow(int t, int id) {
  for (const Record &r : g_tables[t].recs)
    if (r.id == id)
      return r.name;
  const char *psz = TypeNameOf(kTables[t].psz, id);
  return psz != nullptr ? psz : std::string();
}

// A column's value of a record's row, as the record's own fields write it.
long long ValueOf(int t, const Record &r, int c) {
  const Geometry &g = g_tables[t].geo[c];
  long long v = 0;
  for (DWORD b = 0; b < g.recElem && b < r.cols[c].size(); ++b)
    v |= static_cast<long long>(r.cols[c][b]) << (8 * b);
  return v;
}

// ctx.row: the row a record would clone, by its record or classic name. Its columns come at the
// record's own scale (hit points in whole points), and a column that points at another table
// carries that type's name too, since a generated record names its base by name.
bool RowOf(const std::string &table, const std::string &name, PortJson *pOut) {
  const int t = TableIndex(table.c_str());
  if (t < 0)
    return false;
  Record r;
  r.name = name;
  PortJson base;
  base.kind = PortJson::STRING;
  base.s = name;
  const std::string errWas = g_error;
  if (!BaseRow(t, base, r)) {
    g_error = errWas;
    return false;
  }
  pOut->kind = PortJson::OBJECT;
  for (int c = 0; c < kTables[t].nColumns; ++c) {
    const Column &col = kTables[t].pColumns[c];
    PortJsonMember m;
    m.first = col.psz;
    if (c < PathColumns(t)) {
      const char *psz = PathOf(r.cols[c]);
      m.second.kind = PortJson::STRING;
      m.second.s = psz != nullptr ? psz : "";
      pOut->members.push_back(m);
      continue;
    }
    const long long v = ValueOf(t, r, c);
    m.second.kind = PortJson::NUMBER;
    m.second.d = static_cast<double>(col.nScale > 1 ? v / col.nScale : v);
    pOut->members.push_back(m);
    if (col.pszRef != nullptr && strcmp(col.pszRef, "scripts") != 0) {
      const int tRef = TableIndex(col.pszRef);
      const std::string ref = tRef >= 0 ? NameOfRow(tRef, static_cast<int>(v)) : std::string();
      PortJsonMember n;
      n.first = std::string(col.psz) + "_name";
      n.second.kind = ref.empty() ? PortJson::NULLV : PortJson::STRING;
      n.second.s = ref;
      pOut->members.push_back(n);
    }
  }
  return true;
}

// A generated record's name starts gen_, so a roll never stands in for one of the mode's own
// records or for a row of the game's. False with why for the first that does not.
bool GenNamesOk(const PortJson &types, std::string *pWhy) {
  for (const PortJsonMember &m : types.members) {
    if (TableIndex(m.first.c_str()) < 0 || m.second.kind != PortJson::OBJECT)
      continue;
    for (const PortJsonMember &r : m.second.members)
      if (r.first.compare(0, 4, "gen_") != 0) {
        *pWhy = "generate returned the " + m.first + " " + r.first +
                ", and a record it makes is named gen_<something>";
        return false;
      }
  }
  return true;
}

// The slots that play, and which of them the mode treats as the enemy base. A slot's start
// comes from the map read, since the map itself is not loaded when a mode's generate runs.
void ModePlayers(ModLuaGenCtx *pCtx, const ModeMapRead *pMap) {
  for (int s = 0; s < PlayerSlots(); ++s) {
    if (s == PLAYER_NEUTRAL || (gPlayerData[s].nType != 1 && gPlayerData[s].nType != 2))
      continue;
    ModLuaGenPlayer p;
    p.nPlayer = s;
    p.kind = gPlayerData[s].nType == 1 ? "computer" : "human";
    const int nModRace = ModRaceOf(s);
    p.race = nModRace > 0 ? ModRaceName(nModRace)
                          : gPlayerData[s].nRace == 0   ? "zerg"
                            : gPlayerData[s].nRace == 1 ? "terran"
                            : gPlayerData[s].nRace == 2 ? "protoss"
                                                        : "random";
    if (pMap != nullptr)
      for (const ModeMapStart &one : pMap->starts)
        if (one.nSlot == s) {
          p.x = one.x;
          p.y = one.y;
        }
    pCtx->players.push_back(p);
  }
  const int nEnemy = PortModeEnemyFind();
  for (size_t i = 0; i < pCtx->players.size() && pCtx->nEnemy < 0; ++i)
    if (pCtx->players[i].nPlayer == nEnemy)
      pCtx->nEnemy = static_cast<int>(i);
}

// The mode's data and its generated records, loaded before the game's tables are written.
void ModeLoad() {
  g_modeRun.clear();
  const char *pszMode = PortModeInGame();
  if (pszMode[0] == '\0' || RulesCampaignMission() != 0)
    return;
  const std::string folder = PortModeFolderOfGame();
  if (folder.empty()) {
    Log("modes: %s has no data folder, so the game is played without it", pszMode);
    return;
  }
  if (!g_startUp.bKept)
    Keep(&g_startUp);
  Kept before;
  Keep(&before);
  BCRYPT_HASH_HANDLE hHash = NULL;
  if (BCryptCreateHash(BCRYPT_SHA1_ALG_HANDLE, &hHash, NULL, 0, NULL, 0, 0) != 0)
    return;
  g_error.clear();
  // the mode's own types.json, scripts and looks, unless the active mod already brought them
  bool bOk = g_id == pszMode || Load(pszMode, folder, nullptr, nullptr, hHash);
  if (bOk)
    bOk = ModTilesetLoad(g_types, folder, &g_error);
  // then the records its generate makes for this game alone
  std::string typesText, looksText;
  if (bOk && ModLuaHasGenerate()) {
    ModLuaGenCtx ctx;
    ctx.dwSeed = PortModeSeedOfGame();
    PortJson settings;
    const std::string &s = PortModeSettingsOfGame();
    if (!s.empty() && PortJsonParse(s.c_str(), &settings) && settings.kind == PortJson::OBJECT)
      ctx.pSettings = &settings;
    ctx.pMap = ModeMapOf(nullptr);
    ModePlayers(&ctx, ctx.pMap);
    std::string who;
    for (const ModLuaGenPlayer &p : ctx.players) {
      char sz[96];
      snprintf(sz, sizeof sz, "%s%d %s %s at %d,%d", who.empty() ? "" : ", ", p.nPlayer, p.kind.c_str(),
               p.race.c_str(), p.x, p.y);
      who += sz;
    }
    Log("modes: the run is rolled for %s; the enemy base is %s", who.c_str(),
        ctx.nEnemy >= 0 ? std::to_string(ctx.players[ctx.nEnemy].nPlayer).c_str() : "none");
    ctx.row = RowOf;
    std::string run;
    bOk = ModLuaGenerate(ctx, &typesText, &run, &g_error);
    if (bOk)
      g_modeRun = run;
  }
  if (bOk && !typesText.empty()) {
    // the looks it returned are looks.json's own shape, not a table of the game's
    PortJson types;
    if (!PortJsonParse(typesText.c_str(), &types) || types.kind != PortJson::OBJECT) {
      g_error = "generate returned text that is not an object";
      bOk = false;
    } else {
      PortJson looks;
      looks.kind = PortJson::OBJECT;
      std::vector<PortJsonMember> kept;
      for (PortJsonMember &m : types.members) {
        if (m.first == "looks") {
          PortJsonMember l;
          l.first = "looks";
          l.second = m.second;
          looks.members.push_back(l);
        } else {
          kept.push_back(m);
        }
      }
      types.members = kept;
      bOk = GenNamesOk(types, &g_error);
      if (bOk && !looks.members.empty())
        looksText = PortJsonWrite(looks);
      const std::string text = bOk ? PortJsonWrite(types) : std::string();
      const PortJson empty = []() {
        PortJson j;
        j.kind = PortJson::OBJECT;
        return j;
      }();
      if (bOk) {
        ModLuaPushTexts(&empty);  // the generated records bring no scripts of their own
        g_bAddCards = true;       // their cards come beside the mode's, not in place of them
        const std::string iscript;
        bOk = Load(std::string(pszMode) + " run", folder, &text, &iscript, hHash);
        g_bAddCards = false;
        ModLuaPushTexts(nullptr);
      }
    }
  }
  BYTE abHash[20];
  BCryptFinishHash(hHash, abHash, sizeof abHash, 0);
  BCryptDestroyHash(hHash);
  if (!bOk) {
    Log("modes: %s's data does not load: %s", pszMode, g_error);
    PutBack(before);
    return;
  }
  PortModeFingerprintSet(abHash);
  CountsSet();
  g_bMode = true;
  g_bPushed = true;  // put away with the game, as a push is
  g_hxModGame = true;
  size_t nRecords = 0;
  for (const ModTable &m : g_tables)
    nRecords += m.recs.size();
  Log("modes: %s loaded, %u records in the tables, run %u bytes", pszMode, static_cast<unsigned>(nRecords),
      static_cast<unsigned>(g_modeRun.size()));
  if (!looksText.empty())
    PortLooksModeLooks(looksText);
  PortLooksModeFile(folder + "looks.json");
}


struct Push {
  int nSub;
  std::string mod;
  std::string at;
  int anRecords[TABLES];
  std::string units;  // its units' names and ids, as JSON
};
std::vector<Push> g_pushes;

// What pushes brought was their game's alone: the data loaded at start-up again, races and
// cards included, before the front end or the next game reads them.
// The console's four groups in a game of a mod with art of its own: the file with the mod's
// frames, each unit's frame in it and the frames that are the mod's.
const char *const kArtGroups[MOD_ART_KINDS] = {"unit\\cmdbtns\\cmdicons.grp", "unit\\wirefram\\wirefram.grp",
                                               "unit\\wirefram\\tranwire.grp", "unit\\wirefram\\grpwire.grp"};
struct ArtGroup {
  std::vector<BYTE> file;      // empty: the original's as it is
  std::map<int, WORD> frames;  // by unit type
  std::set<WORD> own;
};
ArtGroup g_artGroups[MOD_ART_KINDS];

// A frame's picture: w by h of its group's values, -1 where it is clear, at x, y on the canvas.
struct ArtFrame {
  int x = 0, y = 0, w = 0, h = 0;
  std::vector<int> px;
};

// f at w by h, each pixel the first drawn of those it stands for, so that a line one pixel
// wide stays, as PortConsoleSmallWire halves a wireframe.
ArtFrame ArtScale(const ArtFrame &f, int w, int h) {
  ArtFrame to;
  to.w = w, to.h = h;
  to.px.assign(static_cast<size_t>(w) * h, -1);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const int sx0 = x * f.w / w, sy0 = y * f.h / h;
      const int sx1 = (std::max)(sx0 + 1, (x + 1) * f.w / w), sy1 = (std::max)(sy0 + 1, (y + 1) * f.h / h);
      int &v = to.px[static_cast<size_t>(y) * w + x];
      for (int sy = sy0; sy < sy1 && v < 0; ++sy)
        for (int sx = sx0; sx < sx1 && v < 0; ++sx)
          v = f.px[static_cast<size_t>(sy) * f.w + sx];
    }
  return to;
}

// f made to fit in w by h, its shape kept, when it is larger.
ArtFrame ArtFit(const ArtFrame &f, int w, int h) {
  if (f.w <= w && f.h <= h)
    return f;
  if (f.w * h > f.h * w)
    return ArtScale(f, w, (std::max)(1, f.h * w / f.w));
  return ArtScale(f, (std::max)(1, f.w * h / f.h), h);
}

// The first frame of a GRP file, its runs of clear and drawn pixels or its raw bytes.
bool ArtGrpFrame(const std::vector<BYTE> &b, ArtFrame *pOut) {
  if (b.size() < 14 || ((b[0] | b[1] << 8) & 0x7FFF) == 0)
    return false;
  ArtFrame &f = *pOut;
  f.x = b[6], f.y = b[7], f.w = b[8], f.h = b[9];
  DWORD dwOff;
  memcpy(&dwOff, &b[10], sizeof dwOff);
  const size_t at = dwOff & 0x7FFFFFFF;
  if (f.w == 0 || f.h == 0 || at + 2u * f.h > b.size())
    return false;
  f.px.assign(static_cast<size_t>(f.w) * f.h, -1);
  if ((b[at] | b[at + 1] << 8) != 2 * f.h) {
    if (at + f.px.size() > b.size())
      return false;
    for (size_t i = 0; i < f.px.size(); ++i)
      f.px[i] = b[at + i] ? b[at + i] : -1;
    return true;
  }
  for (int y = 0; y < f.h; ++y) {
    size_t p = at + (b[at + 2 * y] | b[at + 2 * y + 1] << 8);
    int *pRow = &f.px[static_cast<size_t>(y) * f.w];
    for (int x = 0; x < f.w;) {
      if (p >= b.size())
        return false;
      const int c = b[p++];
      const int n = c & 0x80 ? c & 0x7F : c & 0x40 ? c & 0x3F : c;
      if (n == 0 || x + n > f.w || p + (c & 0x80 ? 0 : c & 0x40 ? 1 : n) > b.size())
        return false;
      for (int i = 0; i < n && !(c & 0x80); ++i)
        pRow[x + i] = b[c & 0x40 ? p : p + i];
      p += c & 0x80 ? 0 : c & 0x40 ? 1 : n;
      x += n;
    }
  }
  return true;
}

// A PNG's brightness, 0 to 255, where it is opaque, in the box round its opaque pixels; and
// into pRgba, when given, the box's own pixels.
bool ArtPngFrame(const std::vector<BYTE> &b, ArtFrame *pOut, std::vector<BYTE> *pRgba = NULL) {
  SDL_IOStream *pIo = SDL_IOFromConstMem(b.data(), b.size());
  SDL_Surface *pPng = pIo != NULL ? SDL_LoadPNG_IO(pIo, true) : NULL;
  SDL_Surface *p = pPng != NULL ? SDL_ConvertSurface(pPng, SDL_PIXELFORMAT_RGBA32) : NULL;
  if (pPng != NULL)
    SDL_DestroySurface(pPng);
  if (p == NULL)
    return false;
  int x0 = p->w, y0 = p->h, x1 = -1, y1 = -1;
  auto At = [p](int x, int y) { return static_cast<const BYTE *>(p->pixels) + y * p->pitch + x * 4; };
  for (int y = 0; y < p->h; ++y)
    for (int x = 0; x < p->w; ++x)
      if (At(x, y)[3] >= 128)
        x0 = (std::min)(x0, x), y0 = (std::min)(y0, y), x1 = (std::max)(x1, x), y1 = (std::max)(y1, y);
  ArtFrame &f = *pOut;
  f.w = x1 - x0 + 1, f.h = y1 - y0 + 1;
  if (x1 >= 0) {
    f.px.assign(static_cast<size_t>(f.w) * f.h, -1);
    if (pRgba != NULL)
      pRgba->resize(f.px.size() * 4);
    for (int y = 0; y < f.h; ++y)
      for (int x = 0; x < f.w; ++x) {
        const BYTE *c = At(x0 + x, y0 + y);
        if (pRgba != NULL)
          memcpy(&(*pRgba)[(static_cast<size_t>(y) * f.w + x) * 4], c, 4);
        if (c[3] >= 128)
          f.px[static_cast<size_t>(y) * f.w + x] = (c[0] * 77 + c[1] * 150 + c[2] * 29) >> 8;
      }
  }
  SDL_DestroySurface(p);
  return x1 >= 0;
}

// The icons' shades, brightest first (ticon.pcx's first row), by the brightness each gives.
const struct {
  BYTE v, bLum;
} kIconShades[] = {{1, 230}, {4, 200}, {6, 170}, {7, 140}, {8, 123}, {9, 96},
                   {10, 79}, {11, 63}, {12, 35}, {13, 27}, {14, 18}, {15, 7}};

// A PNG's brightness as nKind's values: an icon's shade nearest each pixel's; a wireframe's
// outline (0xC0, or 0xC1 where it is dark), the shields' colours, where a pixel is at its edge,
// and inside it the four parts damage colours (0xD0 to 0xD3), a quarter each.
void ArtValues(int nKind, ArtFrame *pFrame) {
  ArtFrame &f = *pFrame;
  const std::vector<int> lum = f.px;
  auto Clear = [&](int x, int y) {
    return x < 0 || y < 0 || x >= f.w || y >= f.h || lum[static_cast<size_t>(y) * f.w + x] < 0;
  };
  for (int y = 0; y < f.h; ++y)
    for (int x = 0; x < f.w; ++x) {
      int &v = f.px[static_cast<size_t>(y) * f.w + x];
      if (v < 0)
        continue;
      if (nKind == MOD_ART_ICON) {
        const int nLum = v * 230 / 255;
        int nBest = 0;
        for (int k = 1; k < static_cast<int>(SDL_arraysize(kIconShades)); ++k)
          if (abs(kIconShades[k].bLum - nLum) < abs(kIconShades[nBest].bLum - nLum))
            nBest = k;
        v = kIconShades[nBest].v;
      } else if (Clear(x - 1, y) || Clear(x + 1, y) || Clear(x, y - 1) || Clear(x, y + 1)) {
        v = v >= 128 ? 0xC0 : 0xC1;
      } else {
        v = 0xD0 + (x * 2 >= f.w ? 1 : 0) + (y * 2 >= f.h ? 2 : 0);
      }
    }
}

// A w by h RGBA picture at W by H: each pixel the average of the part of the picture it covers,
// its colours weighted by their alpha.
std::vector<BYTE> ArtResampled(const std::vector<BYTE> &src, int w, int h, int W, int H) {
  std::vector<BYTE> out(static_cast<size_t>(W) * H * 4, 0);
  for (int Y = 0; Y < H; ++Y)
    for (int X = 0; X < W; ++X) {
      const double x0 = static_cast<double>(X) * w / W, x1 = static_cast<double>(X + 1) * w / W;
      const double y0 = static_cast<double>(Y) * h / H, y1 = static_cast<double>(Y + 1) * h / H;
      double sum[4] = {0, 0, 0, 0}, area = 0;
      for (int y = static_cast<int>(y0); y < y1 && y < h; ++y) {
        const double wy = (std::min)(y1, y + 1.0) - (std::max)(y0, static_cast<double>(y));
        for (int x = static_cast<int>(x0); x < x1 && x < w; ++x) {
          const double wx = (std::min)(x1, x + 1.0) - (std::max)(x0, static_cast<double>(x));
          const BYTE *p = &src[(static_cast<size_t>(y) * w + x) * 4];
          for (int k = 0; k < 3; ++k)
            sum[k] += wx * wy * p[3] * p[k];
          sum[3] += wx * wy * p[3];
          area += wx * wy;
        }
      }
      BYTE *q = &out[(static_cast<size_t>(Y) * W + X) * 4];
      for (int k = 0; k < 3 && sum[3] > 0; ++k)
        q[k] = static_cast<BYTE>(sum[k] / sum[3] + 0.5);
      q[3] = area > 0 ? static_cast<BYTE>(sum[3] / area + 0.5) : 0;
    }
  return out;
}

// A PNG's picture at the HD scale s, W by H, as PortModUi has it: an icon's brightness as the
// shades ArtValues gives (230 the brightest); a wireframe's edge, s pixels deep, its outline at
// full brightness, bright or dark as ArtValues tells them, and its inside the four parts, a
// quarter each, by their brightness over the brightest's.
void ArtHdValues(int nKind, int s, int W, int H, std::vector<BYTE> *pRgba) {
  const std::vector<BYTE> &p = *pRgba;
  auto Lum = [&](size_t i) { return (p[i * 4] * 77 + p[i * 4 + 1] * 150 + p[i * 4 + 2] * 29) >> 8; };
  auto Solid = [&](int x, int y) {
    return x >= 0 && y >= 0 && x < W && y < H && p[(static_cast<size_t>(y) * W + x) * 4 + 3] >= 128;
  };
  auto Edge = [&](int x, int y) {
    for (int d = 0; d <= s; ++d)
      if (!Solid(x - d, y) || !Solid(x + d, y) || !Solid(x, y - d) || !Solid(x, y + d))
        return true;
    return false;
  };
  int nMost = 1;
  for (int y = 0; y < H && nKind == MOD_ART_WIRE; ++y)
    for (int x = 0; x < W; ++x)
      if (!Edge(x, y))
        nMost = (std::max)(nMost, Lum(static_cast<size_t>(y) * W + x));
  std::vector<BYTE> out(p.size(), 0);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      const size_t i = static_cast<size_t>(y) * W + x;
      if (p[i * 4 + 3] == 0)
        continue;
      int v = (std::min)(255, Lum(i) * 255 / 230), nPart = 0;
      if (nKind == MOD_ART_WIRE && Edge(x, y))
        v = 255, nPart = Lum(i) >= 128 ? 5 : 6;
      else if (nKind == MOD_ART_WIRE)
        v = Lum(i) * 255 / nMost, nPart = 1 + (x * 2 >= W ? 1 : 0) + (y * 2 >= H ? 2 : 0);
      out[i * 4] = static_cast<BYTE>(v);
      out[i * 4 + 1] = static_cast<BYTE>(nPart * 32);
      out[i * 4 + 3] = p[i * 4 + 3];
    }
  pRgba->swap(out);
}

// A GRP frame's numbers as PortModUi's picture, s pixels to each: an icon's shades by their
// brightness, a wireframe's outline and parts at full brightness.
std::vector<BYTE> ArtHdOfValues(int nKind, const ArtFrame &f, int s) {
  const int W = f.w * s;
  std::vector<BYTE> out(static_cast<size_t>(W) * f.h * s * 4, 0);
  for (int y = 0; y < f.h; ++y)
    for (int x = 0; x < f.w; ++x) {
      const int v = f.px[static_cast<size_t>(y) * f.w + x];
      int nBright = 255, nPart = 0;
      if (v < 0) {
        continue;
      } else if (nKind == MOD_ART_ICON) {
        int nBest = 0;
        for (int k = 1; k < static_cast<int>(SDL_arraysize(kIconShades)); ++k)
          if (abs(kIconShades[k].v - v) < abs(kIconShades[nBest].v - v))
            nBest = k;
        nBright = (std::min)(255, kIconShades[nBest].bLum * 255 / 230);
      } else if (v == 0xC0 || v == 0xC1) {
        nPart = v == 0xC0 ? 5 : 6;
      } else if ((v & 0xF4) == 0xD0) {  // 0xD0 to 0xD3, and the shaded 0xD8 to 0xDB
        nPart = 1 + (v & 3);
      } else {
        continue;
      }
      for (int dy = 0; dy < s; ++dy)
        for (int dx = 0; dx < s; ++dx) {
          BYTE *q = &out[(static_cast<size_t>(y * s + dy) * W + x * s + dx) * 4];
          q[0] = static_cast<BYTE>(nBright);
          q[1] = static_cast<BYTE>(nPart * 32);
          q[3] = 255;
        }
    }
  return out;
}

// The file a unit's art names, as nKind's group draws it on a w by h canvas: a GRP's first
// frame as it is, a PNG's by its brightness (ArtValues); larger than the canvas, smaller to fit.
// Into pHd, when given, the frame at the HD scale for the console: a PNG's from its own pixels,
// a GRP's at the scale's pixels each.
bool ArtLoad(const std::string &name, int nKind, int w, int h, ArtFrame *pOut, PortModUiPicture *pHd) {
  char szPath[MAX_PATH];
  std::vector<BYTE> bytes;
  if (!ModDataFilePath(name.c_str(), szPath, sizeof szPath) || !PortReadFile(szPath, &bytes))
    return false;
  ArtFrame f;
  std::vector<BYTE> rgba;
  const bool bGrp = _stricmp(name.c_str() + name.size() - 4, ".grp") == 0;
  if (bGrp ? !ArtGrpFrame(bytes, &f) : !ArtPngFrame(bytes, &f, &rgba))
    return false;
  const int x = f.x, y = f.y, nPngW = f.w, nPngH = f.h;
  f = ArtFit(f, w, h);
  if (!bGrp)
    ArtValues(nKind, &f);
  if (pHd != NULL) {
    const int s = PortHdScale();
    pHd->nW = f.w * s, pHd->nH = f.h * s;
    if (bGrp) {
      pHd->rgba = ArtHdOfValues(nKind, f, s);
    } else {
      pHd->rgba = ArtResampled(rgba, nPngW, nPngH, pHd->nW, pHd->nH);
      ArtHdValues(nKind, s, pHd->nW, pHd->nH, &pHd->rgba);
    }
  }
  // a GRP's own place when it fits the canvas, else centred
  const bool bPlaced = bGrp && x + f.w <= w && y + f.h <= h;
  f.x = bPlaced ? x : (w - f.w) / 2;
  f.y = bPlaced ? y : (h - f.h) / 2;
  *pOut = f;
  return true;
}

// A frame encoded as 1.17's are: each row's offset, then 0x80 | n clear or n bytes drawn.
void ArtEncode(const ArtFrame &f, std::vector<BYTE> *pOut) {
  std::vector<BYTE> &d = *pOut;
  const size_t at = d.size();
  d.resize(at + 2u * f.h);
  for (int y = 0; y < f.h; ++y) {
    const WORD wRow = static_cast<WORD>(d.size() - at);
    memcpy(&d[at + 2u * y], &wRow, 2);
    const int *pRow = &f.px[static_cast<size_t>(y) * f.w];
    for (int x = 0; x < f.w;) {
      int n = 0;
      const bool bClear = pRow[x] < 0;
      while (x + n < f.w && (pRow[x + n] < 0) == bClear && n < (bClear ? 0x7F : 0x3F))
        ++n;
      d.push_back(static_cast<BYTE>(bClear ? 0x80 | n : n));
      for (int i = 0; i < n && !bClear; ++i)
        d.push_back(static_cast<BYTE>(pRow[x + i]));
      x += n;
    }
  }
}

// The group's file with the frames that replace some of its own and those added after them:
// its frame table grown by the added, its data after the table as it was, then the new frames'.
std::vector<BYTE> ArtGroupFile(const std::vector<BYTE> &orig, const std::map<int, ArtFrame> &put,
                               const std::vector<ArtFrame> &add) {
  const size_t n = (orig[0] | orig[1] << 8) & 0x7FFF, nAll = n + add.size();
  const DWORD dwGrow = static_cast<DWORD>(8 * add.size());
  std::vector<BYTE> out(6 + 8 * nAll, 0);
  memcpy(out.data(), orig.data(), 6 + 8 * n);
  out[0] = static_cast<BYTE>(nAll & 0xFF);
  out[1] = static_cast<BYTE>((nAll >> 8 & 0x7F) | (orig[1] & 0x80));
  for (size_t i = 0; i < n; ++i) {
    DWORD dw;
    memcpy(&dw, &out[6 + 8 * i + 4], 4);
    dw = (dw & 0x80000000) | ((dw & 0x7FFFFFFF) + dwGrow);
    memcpy(&out[6 + 8 * i + 4], &dw, 4);
  }
  out.insert(out.end(), orig.begin() + 6 + 8 * n, orig.end());
  auto Put = [&out](size_t i, const ArtFrame &f) {
    BYTE *pHead = &out[6 + 8 * i];
    pHead[0] = static_cast<BYTE>(f.x), pHead[1] = static_cast<BYTE>(f.y);
    pHead[2] = static_cast<BYTE>(f.w), pHead[3] = static_cast<BYTE>(f.h);
    const DWORD dw = static_cast<DWORD>(out.size());
    memcpy(pHead + 4, &dw, 4);
    ArtEncode(f, &out);
  };
  for (const auto &p : put)
    Put(static_cast<size_t>(p.first), p.second);
  for (size_t k = 0; k < add.size(); ++k)
    Put(n + k, add[k]);
  return out;
}

// For a game: a mod's art in the console's groups (ModConsoleGroup), or in a game without the
// mod none. Each file's picture is made for its group: a unit's own, in place of an original
// unit's frame or added after the group's; a wireframe also at half its size for the
// selection's group and, unless the unit has its own, for a transport's. Then the units that
// show another's art, and the buttons that train a unit and give no icon.
void ArtBuild(bool bGame) {
  for (ArtGroup &g : g_artGroups)
    g = ArtGroup();
  std::vector<BYTE> orig[MOD_ART_KINDS];
  int anCount[MOD_ART_KINDS] = {}, anW[MOD_ART_KINDS] = {}, anH[MOD_ART_KINDS] = {};
  bool bFiles = false;
  for (const auto &u : bGame ? g_unitArt : std::map<int, UnitArt>())
    for (const std::string &file : u.second.file)
      bFiles = bFiles || !file.empty();
  for (int k = 0; k < MOD_ART_KINDS && bFiles; ++k) {
    void *pData = NULL;
    DWORD cb = 0;
    if (!SFileLoadFile(kArtGroups[k], &pData, &cb, 0, NULL) || pData == NULL || cb < 6) {
      Log("mods: the original's %s does not read; the mod's icons and wireframes are not drawn", kArtGroups[k]);
      bFiles = false;
    } else {
      orig[k].assign(static_cast<BYTE *>(pData), static_cast<BYTE *>(pData) + cb);
      anCount[k] = (orig[k][0] | orig[k][1] << 8) & 0x7FFF;
      anW[k] = orig[k][2] | orig[k][3] << 8;
      anH[k] = orig[k][4] | orig[k][5] << 8;
    }
    if (pData != NULL)
      SFileUnloadFile(pData);
  }
  std::map<int, ArtFrame> put[MOD_ART_KINDS];
  std::vector<ArtFrame> add[MOD_ART_KINDS];
  auto Place = [&](int k, int t, ArtFrame f, bool bCentre) {
    if (bCentre)
      f.x = (anW[k] - f.w) / 2, f.y = (anH[k] - f.h) / 2;
    WORD wFrame;
    if (t < anCount[k]) {
      put[k][t] = f;
      wFrame = static_cast<WORD>(t);
    } else {
      wFrame = static_cast<WORD>(anCount[k] + add[k].size());
      add[k].push_back(f);
    }
    g_artGroups[k].frames[t] = wFrame;
    g_artGroups[k].own.insert(wFrame);
  };
  static const char *const kWhat[3] = {"an icon", "a wireframe", "a transport wireframe"};
  int nPlaced = 0;
  std::vector<PortModUiPicture> hd;  // the icons and wireframes at the HD scale, for the console
  for (const auto &u : bFiles ? g_unitArt : std::map<int, UnitArt>()) {
    const int t = u.first;
    const UnitArt &a = u.second;
    for (int k = 0; k < 3; ++k) {
      ArtFrame f;
      PortModUiPicture picture;
      if (a.file[k].empty())
        continue;
      // a transport's fits the cargo's slot, 32 by 32, as 1.17's do on tranwire's 64 by 64
      const int nMost = k == MOD_ART_TRANWIRE ? 32 : 0x7FFF;
      if (!ArtLoad(a.file[k], k, (std::min)(anW[k], nMost), (std::min)(anH[k], nMost), &f,
                   k == MOD_ART_TRANWIRE ? NULL : &picture)) {
        Log("mods: %s does not read as %s", a.file[k].substr(a.file[k].find('\\', 6) + 1), kWhat[k]);
        continue;
      }
      Place(k, t, f, false);
      ++nPlaced;
      if (k != MOD_ART_TRANWIRE) {
        picture.nKey = static_cast<unsigned>(k) << 16 | g_artGroups[k].frames[t];
        hd.push_back(std::move(picture));
      }
      if (k != MOD_ART_WIRE)
        continue;
      const ArtFrame half = ArtScale(f, (f.w + 1) / 2, (f.h + 1) / 2);
      if (t < anCount[MOD_ART_GRPWIRE])
        Place(MOD_ART_GRPWIRE, t, half, true);
      else
        g_artGroups[MOD_ART_GRPWIRE].frames[t] = g_artGroups[MOD_ART_WIRE].frames[t];
      if (a.file[MOD_ART_TRANWIRE].empty() && a.anFrom[MOD_ART_TRANWIRE] < 0)
        Place(MOD_ART_TRANWIRE, t, half, true);
    }
  }
  // a unit that shows another's, which may itself show another's
  for (int nPass = 0; nPass < 4; ++nPass)
    for (const auto &u : bGame ? g_unitArt : std::map<int, UnitArt>())
      for (int k = 0; k < 3; ++k) {
        const int nFrom = u.second.anFrom[k];
        if (nFrom < 0)
          continue;
        auto FrameOf = [](int nKind, int n) {
          const auto it = g_artGroups[nKind].frames.find(n);
          return it != g_artGroups[nKind].frames.end() ? it->second : static_cast<WORD>(UnitTypeLooks(n));
        };
        g_artGroups[k].frames[u.first] = FrameOf(k, nFrom);
        if (k == MOD_ART_WIRE) {
          g_artGroups[MOD_ART_GRPWIRE].frames[u.first] = FrameOf(MOD_ART_GRPWIRE, nFrom);
          if (u.second.file[MOD_ART_TRANWIRE].empty() && u.second.anFrom[MOD_ART_TRANWIRE] < 0)
            g_artGroups[MOD_ART_TRANWIRE].frames[u.first] = FrameOf(MOD_ART_TRANWIRE, nFrom);
        }
      }
  for (int k = 0; k < MOD_ART_KINDS; ++k)
    if (!put[k].empty() || !add[k].empty())
      g_artGroups[k].file = ArtGroupFile(orig[k], put[k], add[k]);
  PortModUiSet(hd);
  for (ModCardButton &m : g_buttons)
    if (m.bUnitIcon && m.wUnit != 0xFFFF)
      m.wIcon = static_cast<WORD>(ModUnitArtFrame(MOD_ART_ICON, m.wUnit));
  if (nPlaced != 0)
    Log("mods: %d icons and wireframes of the mod's drawn in the console", nPlaced);
}

void PushesPutAway() {
  if (!g_bPushed)
    return;
  // the original's rows a push's edits changed, as the file has them: the next game writes
  // the start-up's edits again
  EditsPutBack();
  PutBack(g_startUp);
  g_startUp = Kept();
  g_bPushed = false;
  g_bMode = false;
  g_modeRun.clear();
  g_pushes.clear();
  ModeMapForget();
  PortLooksModeFile(std::string());
  PortLooksModeLooks(std::string());
  Log("mods: what the game's pushes and its mode brought is put away (%s)", g_liveMod);
}

}  // namespace

// The seat a mode treats as the enemy base, over any set of seats: the set-up's rows before the
// game is made and the game's own players once it is. One rule in one place, so a mode is
// offered the seat the launch will really give it, and a mode that plays the player itself takes
// the one its generate was given as ctx.enemy (PortModeEnemySlot).
int PortModeEnemyOf(const PortSeat *pSeats, int nSeats, bool bTeams) {
  int nPerson = -1;
  for (int s = 0; s < nSeats && nPerson < 0; ++s)
    if (pSeats[s].nKind == PORT_SEAT_PERSON)
      nPerson = s;
  const int nMine = nPerson < 0 ? 0 : pSeats[nPerson].nTeam;
  for (int s = 0; s < nSeats; ++s)
    if (pSeats[s].nKind == PORT_SEAT_COMPUTER &&
        (!bTeams || nMine == 0 || pSeats[s].nTeam == 0 || pSeats[s].nTeam != nMine))
      return s;
  return -1;
}

int PortModeEnemyFind() {
  // the teams the set-up gave the slots, which a game and a replay of it both have before the
  // map is loaded; the map's alliances are not read, since the map sets them after a mode's
  // records are made. The teams count where the game has them, and in a melee game every other
  // slot plays against this one whatever force the map put it in.
  std::vector<PortSeat> seats(static_cast<size_t>(PlayerSlots()));
  for (size_t s = 0; s < seats.size(); ++s) {
    if (static_cast<int>(s) == PLAYER_NEUTRAL)
      continue;
    seats[s].nKind = gPlayerData[s].nType == 1   ? PORT_SEAT_COMPUTER
                     : gPlayerData[s].nType == 2 ? PORT_SEAT_PERSON
                                                 : PORT_SEAT_NOBODY;
    seats[s].nTeam = gPlayerData[s].nTeam;
  }
  return PortModeEnemyOf(seats.data(), static_cast<int>(seats.size()),
                         IsTeamGame() || (g_hxRuleOptions & HX_OPT_TEAMS) != 0);
}

const PortMarkLook *ModMarkLook(const char *pszId) {
  const auto it = g_markLooks.find(pszId);
  return it == g_markLooks.end() ? nullptr : &it->second;
}

const char *ModMarkLookNames() {
  return g_markLookNames.c_str();
}

bool ModDataPush(const char *pszFolder, const char *pszMod, const std::string *pTypes, const std::string *pScript,
                 std::string *pAnswer) {
  if (!g_startUp.bKept)
    Keep(&g_startUp);
  Kept before;
  Keep(&before);
  BCRYPT_HASH_HANDLE hHash = NULL;
  if (BCryptCreateHash(BCRYPT_SHA1_ALG_HANDLE, &hHash, NULL, 0, NULL, 0, 0) != 0) {
    *pAnswer = "no hash for the push";
    return false;
  }
  std::string folder = pszFolder;
  if (!folder.empty() && folder.back() != '\\' && folder.back() != '/')
    folder += '\\';
  g_error.clear();
  const bool bOk = Load(pszMod, folder, pTypes, pScript, hHash);
  BYTE abHash[20];
  BCryptFinishHash(hHash, abHash, sizeof abHash, 0);
  BCryptDestroyHash(hHash);
  if (!bOk) {
    *pAnswer = g_error.empty() ? "the push does not load" : g_error;
    Log("mods: a push of %s does not load: %s", pszMod, *pAnswer);
    PutBack(before);
    return false;
  }
  CountsSet();
  // the command cards train and cast the newest of each unit and order
  for (ModCardButton &b : g_buttons) {
    if (b.wUnit != 0xFFFF && b.wUnit >= UNIT_TYPE_MOD_FIRST)
      b.wUnit = static_cast<WORD>(ModUnitNewest(b.wUnit));
    if (b.wUnit == 0xFFFF && !b.bResearch && !b.bUpgrade && b.wOrder >= ORDER_TYPE_MOD_FIRST)
      for (const Record &r : g_tables[ORDERS].recs)
        if (r.id == b.wOrder) {
          b.wOrder = static_cast<WORD>(Find(ORDERS, r.name));
          break;
        }
  }
  g_liveMod = pszMod;
  // the game has the mod's types from now on
  g_bPushed = true;
  g_hxModGame = true;
  for (const Record &r : g_tables[TECHS].recs)
    Store(TECHS, r);
  for (const Record &r : g_tables[UPGRADES].recs)
    Store(UPGRADES, r);
  EditsStore();
  g_hxTypes = g_counts;
  ArtBuild(true);
  PortModArtGame(true);
  StatcmdModCards(true);
  ModTechsForGame();
  Push p;
  p.nSub = g_nSubs;
  p.mod = pszMod;
  SYSTEMTIME st;
  GetLocalTime(&st);
  char sz[32];
  snprintf(sz, sizeof sz, "%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
  p.at = sz;
  for (int t = 0; t < TABLES; ++t) {
    p.anRecords[t] = 0;
    for (const Record &r : g_tables[t].recs)
      p.anRecords[t] += r.sub == p.nSub;
  }
  for (const Record &r : g_tables[UNITS].recs)
    if (r.sub == p.nSub)
      p.units += (p.units.empty() ? "{\"" : ",{\"") + std::string("name\":\"") + r.name + "\",\"id\":" + std::to_string(r.id) + "}";
  p.units = "[" + p.units + "]";
  g_pushes.push_back(p);
  char szCounts[240];
  snprintf(szCounts, sizeof szCounts,
           "\"records\":{\"units\":%d,\"weapons\":%d,\"flingy\":%d,\"sprites\":%d,\"images\":%d,\"orders\":%d,"
           "\"techs\":%d,\"upgrades\":%d,\"sounds\":%d,\"portraits\":%d}",
           p.anRecords[UNITS], p.anRecords[WEAPONS], p.anRecords[FLINGY], p.anRecords[SPRITES], p.anRecords[IMAGES],
           p.anRecords[ORDERS], p.anRecords[TECHS], p.anRecords[UPGRADES], p.anRecords[SOUNDS], p.anRecords[PORTRAITS]);
  *pAnswer = "\"submission\":" + std::to_string(p.nSub) + ",\"mod\":\"" + p.mod + "\",\"units\":" + p.units + "," + szCounts;
  Log("mods: pushed %s as submission %s", pszMod, std::to_string(p.nSub));
  return true;
}

std::string ModDataSubmissionsJson() {
  std::string s = "\"live\":" + std::string(g_bPushed ? "true" : "false") + ",\"mod\":\"" + g_id +
                  "\",\"submissions\":[";
  for (size_t i = 0; i < g_pushes.size(); ++i)
    s += (i ? ",{" : "{") + std::string("\"submission\":") + std::to_string(g_pushes[i].nSub) + ",\"mod\":\"" +
         g_pushes[i].mod + "\",\"at\":\"" + g_pushes[i].at + "\",\"units\":" + g_pushes[i].units + "}";
  return s + "]";
}

int ModUnitSubmission(unsigned nType) {
  for (const Record &r : g_tables[UNITS].recs)
    if (r.id == static_cast<int>(nType))
      return r.sub;
  return 0;
}

int ModUnitNewest(unsigned nType) {
  for (const Record &r : g_tables[UNITS].recs)
    if (r.id == static_cast<int>(nType))
      return Find(UNITS, r.name);
  return static_cast<int>(nType);
}

int ModDataLastSubmission() {
  return g_nSubs;
}

void ModDataAfterGame() {
  PushesPutAway();
}

void ModDataForGame() {
  PushesPutAway();  // a game that did not end through ModDataAfterGame
  ModeLoad();       // the game's mode, its data and the records its generate makes
  ModVoiceForGame();
  if (!g_hxModGame || (g_id.empty() && !g_bMode)) {
    g_hxTypes = {UNIT_TYPES_117,  WEAPON_TYPES_117, FLINGY_TYPES_117, SPRITE_TYPES_117,
                 IMAGE_TYPES_117, ORDER_TYPES_117,  TECH_TYPES_117,   UPGRADE_TYPES_117};
    EditsPutBack();
    ArtBuild(false);
    StatcmdModCards(false);
    PortModArtGame(false);
    ModLuaModeForGame(std::string(), std::string(), std::string());
    ModStatusForGame();
    ModLuaForGame();
    PortReadoutLoad();
    PortReadoutForGame();
    return;
  }
  for (const Record &r : g_tables[TECHS].recs)
    Store(TECHS, r);
  for (const Record &r : g_tables[UPGRADES].recs)
    Store(UPGRADES, r);
  EditsStore();
  // an edited upgrade's max_level is each player's most, as maphdr_data_init made the
  // original's before the edit was written; a map's own settings come after
  if (gpLoadGameFile == NULL) {
    const int nPlayers = g_hxPlayers ? PLAYER_SLOTS_MAX : PLAYER_SLOTS_117;
    for (const Record &e : g_tables[UPGRADES].edits)
      for (int p = 0; p < nPlayers; ++p)
        set_max_upgrade_level(p, static_cast<WORD>(e.id), e.cols[10][0]);  // upgrades.dat's max repeats
  }
  g_hxTypes = g_counts;
  ArtBuild(true);
  PortModArtGame(true);
  StatcmdModCards(true);  // after the counts: a mod's unit's card is its own
  ModLuaModeForGame(PortModeInGame(), PortModeSettingsOfGame(), g_modeRun);
  ModStatusForGame();
  ModLuaForGame();
  // the mod's readout.json, read for the game and its effects dropped (port_readout.h)
  PortReadoutLoad();
  PortReadoutForGame();
}

const char *ModUnitName(unsigned nType) {
  auto it = g_unitNames.find(static_cast<int>(nType));
  if (it != g_unitNames.end())
    return Lasting(it->second);
  for (const Record &r : g_tables[UNITS].recs)
    if (r.id == static_cast<int>(nType)) {
      const PortJson &name = r.pJson->operator[]("name");
      return Lasting(name.kind == PortJson::STRING ? name.s : r.name);
    }
  return "";
}

// A sound's member that is no column: the newest record or edit of the row that gives it, in a
// game with the mod.
static const PortJson *SoundMember(unsigned nSound, const char *pszName) {
  if (!g_hxModGame)
    return nullptr;
  const PortJson *p = nullptr;
  for (const std::vector<Record> *pv : {&g_tables[SOUNDS].recs, &g_tables[SOUNDS].edits})
    for (const Record &r : *pv)
      if (r.id == static_cast<int>(nSound) && (*r.pJson)[pszName].kind != PortJson::NONE)
        p = &(*r.pJson)[pszName];
  return p;
}

const char *ModSoundSubtitle(unsigned nSound) {
  const PortJson *p = SoundMember(nSound, "subtitle");
  return p != nullptr && p->kind == PortJson::STRING && !p->s.empty() ? Lasting(p->s) : nullptr;
}

int ModSoundGap(unsigned nSound) {
  const PortJson *p = SoundMember(nSound, "min_interval");
  return p != nullptr && p->kind == PortJson::NUMBER ? static_cast<int>(p->d) : -1;
}

const char *ModUnitEditName(unsigned nType) {
  if (!g_hxModGame || nType >= UNIT_TYPES_117)
    return nullptr;
  auto it = g_unitNames.find(static_cast<int>(nType));
  return it == g_unitNames.end() ? nullptr : Lasting(it->second);
}

unsigned ModUnitLooks(unsigned nType) {
  for (const Record &r : g_tables[UNITS].recs)
    if (r.id == static_cast<int>(nType)) {
      const int n = Resolve("units", r.pJson->operator[]("looks"), "units " + r.name + ".looks");
      return n >= 0 && n < UNIT_TYPES_117 ? static_cast<unsigned>(n) : static_cast<unsigned>(r.base117);
    }
  return 0;
}

unsigned ModUnitButtonset(unsigned nType) {
  for (const Record &r : g_tables[UNITS].recs)
    if (r.id == static_cast<int>(nType)) {
      const int n = Resolve("units", r.pJson->operator[]("buttons"), "units " + r.name + ".buttons");
      return n >= 0 && n < UNIT_TYPES_117 ? static_cast<unsigned>(n) : ModUnitLooks(nType);
    }
  return UNIT_TYPES_117;
}

const unsigned short *ModUnitTree(unsigned nType) {
  return TreeOf(g_trees.own[MOD_TREE_UNIT], nType);
}

const unsigned short *ModTechUseTree(unsigned nTech) {
  return g_hxModGame ? TreeOf(g_trees.own[MOD_TREE_TECH_USE], nTech) : nullptr;
}

int ModOrderHandler(unsigned nOrder, unsigned nTech) {
  if (!g_hxModGame)
    return 0;
  for (const Record &o : g_tables[ORDERS].recs)
    if (o.id == static_cast<int>(nOrder)) {
      switch (o.base117) {
      case 137: case 139: case 141: case 148: case 182: case 184:
        return o.base117;
      }
      break;
    }
  if (nTech == 0)
    return MOD_ORDER_STIM;
  for (const Record &t : g_tables[TECHS].recs)
    if (t.id == static_cast<int>(nTech))
      return t.base117 == 0 ? MOD_ORDER_STIM : 0;
  return 0;
}

const unsigned short *ModTechResearchTree(unsigned nTech) {
  return g_hxModGame ? TreeOf(g_trees.own[MOD_TREE_TECH_RESEARCH], nTech) : nullptr;
}

const unsigned short *ModUpgradeTree(unsigned nUpgrade) {
  return g_hxModGame ? TreeOf(g_trees.own[MOD_TREE_UPGRADE], nUpgrade) : nullptr;
}

const unsigned short *ModTreeEdit(int nKind, unsigned n) {
  if (!g_hxModGame || nKind < 0 || nKind >= MOD_TREE_KINDS)
    return nullptr;
  return TreeOf(g_trees.edit[nKind], n);
}

void ModTechsForGame() {
  if (!g_hxModGame)
    return;
  const int nPlayers = g_hxPlayers ? PLAYER_SLOTS_MAX : PLAYER_SLOTS_117;
  for (const auto &t : g_trees.own[MOD_TREE_TECH_RESEARCH])
    for (int p = 0; p < nPlayers; ++p) {
      set_tech_available(p, static_cast<WORD>(t.first), 1);
      set_tech_researched(p, static_cast<WORD>(t.first), 0);
    }
  // an upgrade at level 0, its most the record's max_level (upgrades.dat's max repeats)
  for (const Record &r : g_tables[UPGRADES].recs)
    for (int p = 0; p < nPlayers; ++p) {
      set_upgrade_level(p, static_cast<WORD>(r.id), 0);
      set_max_upgrade_level(p, static_cast<WORD>(r.id), r.cols[10][0]);
    }
}

bool ModUnitHasCard(unsigned nType) {
  for (const ModCardButton &b : g_buttons)
    if (b.wCard == nType)
      return nType >= UNIT_TYPE_MOD_FIRST;
  return false;
}

bool ModUnitTrainable(unsigned nType) {
  for (const Record &r : g_tables[UNITS].recs)
    if (r.id == static_cast<int>(nType))
      return (r.cols[22][0] & 1) == 0;  // units.dat's flags, bit 0 a building
  return false;
}

const char *ModString(unsigned nId) {
  const unsigned n = nId - MOD_STRING_FIRST;
  return nId >= MOD_STRING_FIRST && n < g_strings.size() ? Lasting(g_strings[n]) : nullptr;
}

const char *const kModAiFinders[MOD_AI_FINDERS] = {
    "psionic_storm", "yamato_gun", "plague", "dark_swarm", "ensnare", "spawn_broodlings",
    "parasite", "stasis_field", "emp_shockwave", "irradiate", "defensive_matrix",
    "disruption_web", "restoration", "optical_flare", "feedback", "mind_control", "maelstrom",
    "lockdown", "hallucination", "enemy_cluster",
};

const ModCardButton *ModCardButtons(int *pnCount) {
  *pnCount = static_cast<int>(g_buttons.size());
  return g_buttons.data();
}

const char *ModCardAiCall(short n) {
  return n >= 0 && n < static_cast<short>(g_cardAiCalls.size()) ? g_cardAiCalls[n].c_str() : nullptr;
}

WORD ModCardWordsString(int nWords, const char *pszOriginal) {
  if (nWords < 0 || nWords >= static_cast<int>(g_words.size()) || !pszOriginal || !pszOriginal[0] || !pszOriginal[1])
    return 0;
  Words &w = g_words[static_cast<size_t>(nWords)];
  if (w.wStr == 0) {
    const std::string key = w.hotkey.empty() ? std::string(1, pszOriginal[0]) : w.hotkey;
    w.wStr = w.text.empty() ? AddString(key.substr(0, 1) + pszOriginal[1] + std::string(pszOriginal + 2))
                            : AddString(ButtonText(key, pszOriginal[1], w.text));
  }
  return w.wStr;
}

int ModTypeByName(const char *pszTable, const char *pszName) {
  const int t = TableIndex(pszTable);
  return t < 0 ? -1 : Find(t, pszName);
}

bool ModDataFilePath(const char *pszName, char *pszPath, size_t cchPath) {
  const char *p = strstr(pszName, "hxmod\\");
  if (p == nullptr || (p != pszName && p[-1] != '\\'))
    return false;
  p += 6;
  const char *pEnd = strchr(p, '\\');
  if (pEnd == nullptr)
    return false;
  std::string path;
  {
    std::lock_guard<std::mutex> lock(g_mediaLock);
    auto it = g_mediaFolders.find(std::string(p, pEnd));
    if (it == g_mediaFolders.end())
      return false;
    path = it->second + (pEnd + 1);
  }
  if (path.size() >= cchPath)
    return false;
  memcpy(pszPath, path.c_str(), path.size() + 1);
  return true;
}

bool ModDataVideoPath(const char *pszName, char *pszPath, size_t cchPath) {
  const char *pDot = strrchr(pszName, '.');
  if (pDot == nullptr)
    return false;
  const std::string base(pszName, pDot);
  std::vector<int> chain;
  std::string why;
  if (!PortModChain(PortModActive(), &chain, &why))
    return false;
  const std::vector<PortMod> &mods = PortMods();
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
    const PortMod &m = mods[*it];
    if (m.data.empty())
      continue;
    std::vector<const char *> exts;
    for (const char *const *ppszExt = PortVideoExtensions(); *ppszExt; ++ppszExt)
      exts.push_back(*ppszExt);
    exts.push_back(".smk");
    for (const char *pszExt : exts) {
      const std::string path = m.folder + m.data + base + pszExt;
      if (path.size() < cchPath && GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        memcpy(pszPath, path.c_str(), path.size() + 1);
        return true;
      }
    }
  }
  return false;
}

unsigned char ModPortraitChance(int nVariant, unsigned nPortrait, const unsigned char *const *ppColumns) {
  if (nPortrait < PORTRAIT_TYPES_117)
    return ppColumns[nVariant][nPortrait];
  const std::vector<Record> &recs = g_tables[PORTRAITS].recs;
  const size_t i = nPortrait - PORTRAIT_TYPE_MOD_FIRST;
  // 1.17 reads a fifth chance past the four when they come to less than its roll
  return i < recs.size() && nVariant < 4 ? recs[i].cols[2 + nVariant][0] : nVariant == 0 ? 100 : 0;
}

unsigned ModUnitArtFrame(int nKind, unsigned nType) {
  const std::map<int, WORD> &frames = g_artGroups[nKind].frames;
  const auto it = frames.find(static_cast<int>(nType));
  return it != frames.end() ? it->second : UnitTypeLooks(nType);
}

bool ModArtOwnFrame(int nKind, unsigned nFrame) {
  return g_artGroups[nKind].own.count(static_cast<WORD>(nFrame)) != 0;
}

GROUP *ModConsoleGroup(int nKind, GROUP *pLoaded) {
  const std::vector<BYTE> &file = g_artGroups[nKind].file;
  if (file.empty())
    return pLoaded;
  void *p = SMemAlloc(static_cast<DWORD>(file.size()), "port_moddata.cpp", __LINE__, 0);
  if (p == NULL)
    return pLoaded;
  memcpy(p, file.data(), file.size());
  if (pLoaded != NULL)
    SMemFree(pLoaded, "port_moddata.cpp", __LINE__, 0);
  return PortGroupNative(static_cast<GROUP *>(p), true);
}

void ModPortraitsResolved() {
  if (!g_hxModGame)
    return;
  const Table &tab = kTables[PORTRAITS];
  for (const Record &e : g_tables[PORTRAITS].edits)
    for (int c = 0; c < PathColumns(PORTRAITS); ++c) {
      const Geometry &g = g_tables[PORTRAITS].geo[c];
      if (void *p = PortDataRow(tab.pDesc[c].pDest, g.rowBytes, static_cast<DWORD>(e.id)))
        memcpy(p, e.cols[c].data(), g.recBytes);
    }
}

int ModRaceCount() {
  return static_cast<int>(g_races.size());
}

const char *ModRaceName(int nRace) {
  const Race *r = RaceAt(nRace);
  return r ? r->name.c_str() : "";
}

int ModRaceBase(int nRace) {
  const Race *r = RaceAt(nRace);
  return r ? r->nBase : -1;
}

int ModRaceByName(const char *psz) {
  for (size_t i = 0; i < g_races.size(); ++i)
    if (lstrcmpiA(g_races[i].id.c_str(), psz) == 0 || lstrcmpiA(g_races[i].name.c_str(), psz) == 0 ||
        (psz[0] != '\0' && lstrcmpiA(ModLangOwnText("race/" + g_races[i].id).c_str(), psz) == 0))
      return static_cast<int>(i) + 1;
  return 0;
}

void ModRaceChoose(int nRace) {
  Session().aRow[0].nModRace = nRace;
}

void ModRaceChooseComputer(int nIndex, int nRace) {
  if (nIndex >= 0 && nIndex < 8)
    Session().aRow[1 + nIndex].nModRace = nRace;
}


void ModRacesRecorded(const unsigned char *pbRaces, int nSlots) {
  memset(g_recordedRaces, 0, sizeof g_recordedRaces);
  if (pbRaces)
    memcpy(g_recordedRaces, pbRaces, nSlots < PLAYER_SLOTS_MAX ? nSlots : PLAYER_SLOTS_MAX);
}

void ModRacesForGame() {
  PushesPutAway();  // the races seated are the start-up's, which ModDataForGame keeps
  memset(g_slotRace, 0, sizeof g_slotRace);
  // the session's choices (port_session.h): yours, and the computers', each given to one slot
  const int nChosen = Session().aRow[0].nModRace;
  int anComputer[8];
  for (int k = 0; k < 8; ++k)
    anComputer[k] = Session().aRow[1 + k].nModRace;
  if (g_hxModGame && !g_races.empty()) {
    if (gPlayback || gpLoadGameFile != NULL) {
      for (int s = 0; s < PLAYER_SLOTS_MAX; ++s)
        g_slotRace[s] = RaceAt(g_recordedRaces[s]) ? g_recordedRaces[s] : 0;
    } else {
      // the chosen race's slot plays its base, as --race set it
      const Race *r = RaceAt(nChosen);
      if (r && g_LocalNationID < PLAYER_SLOTS_MAX && gPlayerData[g_LocalNationID].nRace == r->nBase)
        g_slotRace[g_LocalNationID] = static_cast<BYTE>(nChosen);
      // and the computers': a race to the computer of the row that chose it, as seated
      // (PlayersSeatedComputer), else to one of its base (type 1, or 5 from the lobby), by slot
      for (int s = 0; s < PLAYER_SLOTS_MAX; ++s) {
        const int k = PlayersSeatedComputer(s);
        const Race *c = k >= 0 && k < 8 ? RaceAt(anComputer[k]) : nullptr;
        if (c && gPlayerData[s].nRace == c->nBase) {
          g_slotRace[s] = static_cast<BYTE>(anComputer[k]);
          anComputer[k] = 0;
          Log("mods: slot %s is a computer of %s", std::to_string(s), c->name);
        }
      }
      for (int k = 0; k < 8; ++k)
        if (const Race *c = RaceAt(anComputer[k]))
          for (int s = 0; s < PLAYER_SLOTS_MAX; ++s)
            if ((gPlayerData[s].nType == 1 || gPlayerData[s].nType == 5) && gPlayerData[s].nRace == c->nBase &&
                g_slotRace[s] == 0) {
              g_slotRace[s] = static_cast<BYTE>(anComputer[k]);
              Log("mods: slot %s is a computer of %s", std::to_string(s), c->name);
              break;
            }
    }
  }
  memset(g_recordedRaces, 0, sizeof g_recordedRaces);
}

// The local player's race of the mod: a new game's as chosen (its slot plays the race's base), a
// replay's or save's as recorded, else as the game set it.
static const Race *LocalRace() {
  if (!g_hxModGame || g_races.empty() || g_LocalNationID >= PLAYER_SLOTS_MAX)
    return nullptr;
  if (gPlayback || gpLoadGameFile != NULL)
    return RaceAt(g_recordedRaces[g_LocalNationID] ? g_recordedRaces[g_LocalNationID] : g_slotRace[g_LocalNationID]);
  const Race *r = RaceAt(Session().aRow[0].nModRace);
  return r && gPlayerData[g_LocalNationID].nRace == r->nBase ? r : nullptr;
}

int ModRaceLook(int nWhat, int nRace) {
  const Race *r = LocalRace();
  const int n = !r ? -1 : nWhat == 0 ? r->nConsole : r->nMusic;
  return n >= 0 ? n : nRace;
}

const ModRaceRules *ModRaceRulesOf(int nRace) {
  const Race *r = RaceAt(nRace);
  return r ? &r->rules : nullptr;
}

int ModRaceMusicTrack(int nTrack) {
  // the three races' tracks are 1 to 9, three each from zerg's
  if (nTrack < 1 || nTrack > 9)
    return nTrack;
  const int nMusic = ModRaceLook(1, -1);
  return nMusic < 0 ? nTrack : 1 + 3 * nMusic + (nTrack - 1) % 3;
}

int ModRaceOf(int nSlot) {
  return nSlot >= 0 && nSlot < PLAYER_SLOTS_MAX ? g_slotRace[nSlot] : 0;
}

bool ModRacesAny() {
  for (BYTE b : g_slotRace)
    if (b)
      return true;
  return false;
}

bool ModRaceOwnStart(int nSlot) {
  const Race *r = RaceAt(ModRaceOf(nSlot));
  return r && r->bOwnStart;
}

bool ModRaceStartUnit(int nSlot, int i, int *pnType, int *pnCount) {
  const Race *r = RaceAt(ModRaceOf(nSlot));
  if (!r || i < 0 || i >= static_cast<int>(r->start.size()))
    return false;
  *pnType = r->start[i].first;
  *pnCount = r->start[i].second;
  return true;
}

void ModRacesWithout() {
  for (int s = 0; s < PLAYER_SLOTS_MAX; ++s)
    if (const Race *r = RaceAt(g_slotRace[s]))
      for (int n : r->without)
        PlayerUnitsAvailable[s][n] = 0;
}

int ModRaceAiUnit(int nPlayer, int nType) {
  if (const Race *r = RaceAt(ModRaceOf(nPlayer)))
    for (const std::pair<int, int> &m : r->ai)
      if (m.first == nType)
        return m.second;
  return nType;
}

int ModRaceAiClassic(int nPlayer, int nType) {
  if (const Race *r = RaceAt(ModRaceOf(nPlayer)))
    for (const std::pair<int, int> &m : r->ai)
      if (m.second == nType)
        return m.first;
  return nType;
}

int ModRaceAiTech(int nPlayer, int nTech) {
  if (const Race *r = RaceAt(ModRaceOf(nPlayer)))
    for (const std::pair<int, int> &m : r->aiTechs)
      if (m.first == nTech)
        return m.second;
  return nTech;
}

int ModRaceAiUpgrade(int nPlayer, int nUpgrade) {
  if (const Race *r = RaceAt(ModRaceOf(nPlayer)))
    for (const std::pair<int, int> &m : r->aiUpgrades)
      if (m.first == nUpgrade)
        return m.second;
  return nUpgrade;
}

bool ModScriptHeader(int id, DWORD *pdwHeader) {
  if (id < 1000 || static_cast<size_t>(id - 1000) >= g_scriptHeaders.size())
    return false;
  *pdwHeader = g_scriptHeaders[static_cast<size_t>(id - 1000)];
  return true;
}

bool ModUnitForPlayer(unsigned nType, int nPlayer) {
  bool bNamed = false;
  for (size_t i = 0; i < g_races.size(); ++i)
    for (int n : g_races[i].units)
      if (n == static_cast<int>(nType)) {
        if (ModRaceOf(nPlayer) == static_cast<int>(i) + 1)
          return true;
        bNamed = true;
      }
  return !bNamed;
}

#ifdef HX_DESIGN
// The loader's tables as the studio reads them (design_hooks.h): what the loader starts a record
// from and what it built, so the studio works out a record's values by the loader's own numbers
// and checks its own against the loader's after a push (studio_verify).
bool DesignModTable(int t, const char **ppszName, int *pnRows117, int *pnModFirst, int *pnModMax) {
  if (t < 0 || t >= TABLES)
    return false;
  *ppszName = kTables[t].psz;
  *pnRows117 = kTables[t].nRows117;
  *pnModFirst = kTables[t].nModFirst;
  *pnModMax = kTables[t].nModMax;
  return true;
}

int DesignModColumns(int t) {
  return t >= 0 && t < TABLES ? kTables[t].nColumns : 0;
}

bool DesignModColumnOf(int t, int c, DesignModColumn *pOut) {
  if (t < 0 || t >= TABLES || c < 0 || c >= kTables[t].nColumns)
    return false;
  if (g_tables[t].file.empty() && !LoadTableFile(t))
    return false;
  const Column &col = kTables[t].pColumns[c];
  const Geometry &g = g_tables[t].geo[c];
  pOut->pszName = col.psz;
  pOut->pszRef = col.pszRef ? col.pszRef : "";
  pOut->nScale = col.nScale;
  pOut->nCount = static_cast<int>(g.rowBytes / g.elemSize);
  pOut->bPath = c < PathColumns(t);
  return true;
}

namespace {
// A row's columns, from the build's widths to numbers, element by element.
void Numbers(int t, const std::vector<std::vector<BYTE>> &cols, std::vector<std::vector<long long>> *pOut,
             std::vector<std::string> *pPaths) {
  pOut->assign(cols.size(), std::vector<long long>());
  if (pPaths)
    pPaths->assign(cols.size(), std::string());
  for (size_t c = 0; c < cols.size(); ++c) {
    if (static_cast<int>(c) < PathColumns(t)) {
      const char *psz = PathOf(cols[c]);
      if (pPaths && psz)
        (*pPaths)[c] = psz;
      continue;
    }
    const Geometry &g = g_tables[t].geo[c];
    const DWORD nPer = g.rowBytes / g.elemSize;
    for (DWORD k = 0; k < nPer; ++k) {
      unsigned long long v = 0;
      for (DWORD b = 0; b < g.recElem && b < 8; ++b)
        v |= static_cast<unsigned long long>(cols[c][k * g.recElem + b]) << (8 * b);
      (*pOut)[c].push_back(static_cast<long long>(v));
    }
  }
}
}  // namespace

bool DesignModClassicRow(int t, int nRow, std::vector<std::vector<long long>> *pCols,
                         std::vector<std::string> *pPaths) {
  if (t < 0 || t >= TABLES || nRow < 0 || nRow >= kTables[t].nRows117)
    return false;
  if (g_tables[t].file.empty() && !LoadTableFile(t))
    return false;
  Numbers(t, FileRow(t, nRow), pCols, pPaths);
  return true;
}

bool DesignModLoaded(int t, const std::string &name, int *pnId, int *pnSub,
                     std::vector<std::vector<long long>> *pCols) {
  if (t < 0 || t >= TABLES)
    return false;
  const Record *pFound = nullptr;
  for (const Record &r : g_tables[t].recs)
    if (r.name == name && r.state == 2)
      pFound = &r;
  if (pFound == nullptr)
    return false;
  *pnId = pFound->id;
  *pnSub = pFound->sub;
  Numbers(t, pFound->cols, pCols, nullptr);
  return true;
}
#endif



bool ModDataModeRoll(const char *pszMap, const char *pszSettings, unsigned dwSeed, const char *pszRace,
                     std::string *pTypes, std::string *pRun, std::string *pWhy, int nPlayers) {
  if (!ModLuaHasGenerate()) {
    *pWhy = "it has no generate";
    return false;
  }
  const ModeMapRead *pMap = ModeMapOf(pszMap);
  if (pMap == nullptr) {
    *pWhy = std::string("the map ") + (pszMap != nullptr ? pszMap : "") + " does not read";
    return false;
  }
  PortJson settings;
  ModLuaGenCtx ctx;
  ctx.dwSeed = dwSeed;
  if (pszSettings != nullptr && pszSettings[0] != 0 && PortJsonParse(pszSettings, &settings) &&
      settings.kind == PortJson::OBJECT)
    ctx.pSettings = &settings;
  // the player at the first start, computers of the same race at the starts after it, as many
  // as were asked for and the map has
  for (int i = 0; i < nPlayers && i < static_cast<int>(pMap->starts.size()); ++i) {
    ModLuaGenPlayer p;
    p.nPlayer = i;
    p.race = pszRace;
    p.kind = i == 0 ? "human" : "computer";
    p.x = pMap->starts[i].x;
    p.y = pMap->starts[i].y;
    ctx.players.push_back(p);
  }
  // the last of them is the mode's enemy base, as it is in the game of two the check rolls
  ctx.nEnemy = ctx.players.size() > 1 ? static_cast<int>(ctx.players.size()) - 1 : -1;
  ctx.pMap = pMap;
  ctx.row = RowOf;
  if (!ModLuaGenerate(ctx, pTypes, pRun, pWhy))
    return false;
  PortJson types;
  if (!PortJsonParse(pTypes->c_str(), &types) || types.kind != PortJson::OBJECT) {
    *pWhy = "generate returned text that is not an object";
    return false;
  }
  return GenNamesOk(types, pWhy);
}

int ModDataSpriteImage(int nSprite) {
  const int t = TableIndex("sprites");
  if (t < 0 || nSprite < 0)
    return -1;
  PortJson base;
  base.kind = PortJson::NUMBER;
  base.d = nSprite;
  Record r;
  const std::string errWas = g_error;
  if (!BaseRow(t, base, r)) {
    g_error = errWas;  // asking is not a load's failure
    return -1;
  }
  for (int c = 0; c < kTables[t].nColumns; ++c)
    if (strcmp(kTables[t].pColumns[c].psz, "image") == 0)
      return static_cast<int>(ValueOf(t, r, c));
  return -1;
}

