// studio_schema.cpp -- the studio's fields and the classic game's rows by name (studio_model.h):
// the loader's columns (port_moddata.cpp, through design_hooks.h) with how engine/studio/fields.json
// files, labels and edits each, and the original's rows by the names types.json gives them.
#include <windows.h>
#include <ctype.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>
#include "CUnitMisc.h"  // get_unit_name_from_type
#include "../port/data/port_typenames.h"    // TypeNameFind
#include "../port/data/port_typenames117.h"  // the original's rows by name, read back here too
#include "../port/data/port_hxm_names.h"
#include "../port/platform/port_json.h"
#include "../port/platform/port_util.h"  // PortReadFile
#include "../design/design_hooks.h"      // DesignModTable, DesignModColumn
#include "studio_model.h"

// The loaded stat_txt.tbl (status.cpp), the units' names: there once a game's console is up.
extern void *sgpStatStrs;

namespace {

const char *const kTables[ST_TABLES] = {"units",  "weapons", "flingy",   "sprites", "images",
                                        "orders", "techs",   "upgrades", "sounds",  "portraits"};

struct Schema {
  bool bRead = false;
  std::vector<StudioField> fields[ST_TABLES];
  std::vector<StudioCategory> cats[ST_TABLES];
  std::map<std::string, std::vector<std::pair<int, std::string>>> enums;
  std::map<std::string, std::vector<std::string>> flags;
  std::string error;
};
Schema g_schema;

// The original's names, each table's row by row (port_typenames117.h, port_hxm_names.h).
struct Names {
  bool bMade = false;
  std::vector<const char *> rows[ST_TABLES];
};
Names g_names;

std::string ExeFolder(void) {
  char sz[MAX_PATH] = "";
  const DWORD n = GetModuleFileNameA(NULL, sz, MAX_PATH);
  std::string dir(sz, n);
  const size_t cut = dir.find_last_of("\\/");
  return cut == std::string::npos ? std::string() : dir.substr(0, cut + 1);
}

void Fill(int t, const HxmName *pNames, size_t n) {
  std::vector<const char *> &rows = g_names.rows[t];
  rows.assign(static_cast<size_t>(StudioClassicRows(t)) + 1, nullptr);
  for (size_t i = 0; i < n; ++i)
    if (pNames[i].n >= 0 && pNames[i].n < static_cast<int>(rows.size()) && rows[pNames[i].n] == nullptr)
      rows[pNames[i].n] = pNames[i].psz;
}

void MakeNames(void) {
  if (g_names.bMade)
    return;
  g_names.bMade = true;
#define F(t, a) Fill(t, a, sizeof a / sizeof a[0])
  F(ST_UNITS, kHxmUnits);
  F(ST_WEAPONS, kHxmWeapons);
  F(ST_FLINGY, kHxmFlingy);
  F(ST_SPRITES, kHxmSprites);
  F(ST_IMAGES, kHxmImages);
  F(ST_ORDERS, kHxmOrders);
  F(ST_TECHS, kHxmTechs);
  F(ST_UPGRADES, kHxmUpgrades);
  F(ST_SOUNDS, kHxmSounds);
  F(ST_PORTRAITS, kHxmPortraits);
#undef F
}

// fields.json's fields over the loader's columns: every column is a field, labelled by
// fields.json where it has one and by its name where not; fields.json's fields that are no
// column (name, looks, requires) come too.
void Read(void) {
  if (g_schema.bRead)
    return;
  // the loader's columns need the game's files, which open after the studio starts: until they
  // read, the fields wait
  DesignModColumn probe;
  if (!DesignModColumnOf(0, 0, &probe))
    return;
  g_schema.bRead = true;
  const std::string path = ExeFolder() + "studio\\fields.json";
  std::string text;
  PortJson j;
  std::string error;
  if (!PortReadFile(path, &text))
    g_schema.error = path + " does not read";
  else if (!PortJsonParse(text.c_str(), &j, &error))
    g_schema.error = path + ": " + StudioJsonWhere(text, error);
  for (const PortJsonMember &e : j["enums"].members)
    for (const PortJson &c : e.second.items)
      g_schema.enums[e.first].push_back({static_cast<int>(c["value"].Num()), c["label"].Str()});
  for (const PortJsonMember &f : j["flags"].members)
    for (const PortJson &n : f.second.items)
      g_schema.flags[f.first].push_back(n.Str());
  for (int t = 0; t < ST_TABLES; ++t) {
    const PortJson &tab = j["tables"][kTables[t]];
    for (const PortJson &c : tab["cats"].items)
      g_schema.cats[t].push_back({c["id"].Str(), c["label"].Str()});
    std::vector<std::string> simple;
    for (const PortJson &s : tab["simple"].items)
      simple.push_back(s.Str());
    std::vector<StudioField> &out = g_schema.fields[t];
    // fields.json's order first, which is how the panes list them
    for (const PortJson &f : tab["fields"].items) {
      StudioField sf;
      sf.key = f["key"].Str();
      sf.cat = f["cat"].Str();
      sf.editor = f["editor"].Str("int");
      sf.label = f["label"].Str(sf.key.c_str());
      sf.help = f["help"].Str();
      out.push_back(sf);
    }
    // then every column fields.json does not name, filed as "more"
    const int nColumns = DesignModColumns(t);
    for (int c = 0; c < nColumns; ++c) {
      DesignModColumn col;
      if (!DesignModColumnOf(t, c, &col))
        continue;
      StudioField *p = nullptr;
      for (StudioField &f : out)
        if (f.key == col.pszName)
          p = &f;
      if (p == nullptr) {
        StudioField sf;
        sf.key = col.pszName;
        sf.cat = "more";
        sf.editor = col.pszRef[0] ? std::string("link:") + col.pszRef : "int";
        sf.label = sf.key;
        out.push_back(sf);
        p = &out.back();
      }
      p->nColumn = c;
      p->nRef = StudioTableOf(col.pszRef);
      p->nScale = col.nScale;
      p->nCount = col.nCount;
      p->bPath = col.bPath;
      p->bScript = strcmp(col.pszRef, "scripts") == 0;
    }
    bool bMore = false;
    for (StudioField &f : out) {
      for (const std::string &s : simple)
        if (s == f.key)
          f.bSimple = true;
      bMore = bMore || f.cat == "more";
    }
    if (bMore)
      g_schema.cats[t].push_back({"more", "More"});
  }
}

}  // namespace

const char *StudioTableName(int t) {
  return t >= 0 && t < ST_TABLES ? kTables[t] : "";
}

int StudioTableOf(const std::string &name) {
  for (int t = 0; t < ST_TABLES; ++t)
    if (name == kTables[t])
      return t;
  return -1;
}

const std::vector<StudioField> &StudioFields(int t) {
  static const std::vector<StudioField> kNone;
  Read();
  return t >= 0 && t < ST_TABLES ? g_schema.fields[t] : kNone;
}

const std::vector<StudioCategory> &StudioCategories(int t) {
  static const std::vector<StudioCategory> kNone;
  Read();
  return t >= 0 && t < ST_TABLES ? g_schema.cats[t] : kNone;
}

const StudioField *StudioFieldOf(int t, const std::string &key) {
  for (const StudioField &f : StudioFields(t))
    if (f.key == key)
      return &f;
  return nullptr;
}

const std::vector<std::pair<int, std::string>> &StudioEnum(const std::string &name) {
  static const std::vector<std::pair<int, std::string>> kNone;
  Read();
  auto it = g_schema.enums.find(name);
  return it == g_schema.enums.end() ? kNone : it->second;
}

const std::vector<std::string> &StudioFlags(const std::string &name) {
  static const std::vector<std::string> kNone;
  Read();
  auto it = g_schema.flags.find(name);
  return it == g_schema.flags.end() ? kNone : it->second;
}

const std::string &StudioFieldsError(void) {
  Read();
  return g_schema.error;
}

int StudioClassicRows(int t) {
  int nRows = 0, nFirst = 0, nMax = 0;
  const char *psz = nullptr;
  return DesignModTable(t, &psz, &nRows, &nFirst, &nMax) ? nRows : 0;
}

int StudioModFirst(int t) {
  int nRows = 0, nFirst = 0, nMax = 0;
  const char *psz = nullptr;
  return DesignModTable(t, &psz, &nRows, &nFirst, &nMax) ? nFirst : 0;
}

const char *StudioClassicName(int t, int nRow) {
  if (t < 0 || t >= ST_TABLES)
    return nullptr;
  MakeNames();
  const std::vector<const char *> &rows = g_names.rows[t];
  return nRow >= 0 && nRow < static_cast<int>(rows.size()) ? rows[nRow] : nullptr;
}

int StudioClassicRow(int t, const std::string &name) {
  return t >= 0 && t < ST_TABLES ? TypeNameFind(kTables[t], name.c_str()) : -1;
}

std::string StudioShowName(int t, const std::string &name) {
  if (t == ST_UNITS && sgpStatStrs != NULL) {
    const int nRow = StudioClassicRow(t, name);
    if (nRow >= 0 && nRow < StudioClassicRows(t)) {
      const char *psz = get_unit_name_from_type(static_cast<WORD>(nRow));
      if (psz != NULL && psz[0] != '\0')
        return psz;
    }
  }
  std::string out = name;
  bool bStart = true;
  for (char &c : out) {
    if (c == '_') {
      c = ' ';
      bStart = true;
    } else if (bStart) {
      c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
      bStart = false;
    }
  }
  return out;
}
