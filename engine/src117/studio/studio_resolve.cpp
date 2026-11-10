// studio_resolve.cpp -- a record's values and where each comes from (studio_model.h): the loader's
// rules (port_moddata.cpp) followed over the mod and the mods it depends on, without loading them.
//
// Each mod of the chain is a submission, in load order, the open mod last. In a submission every
// edit is done before any record is built: an edit of an earlier submission's record changes that
// record, an edit of a classic row makes the row's newest state. A record starts from its base, a
// mod's record by name (the newest of that name) or a classic row by name or number, which goes
// through its newest edit; its own fields go over that; a unit that names its looks and gives no
// voices or portrait takes its looks unit's. studio_verify checks the result against the loader's
// own rows after a push (studio_commands.cpp).
#include <ctype.h>
#include <map>
#include <string>
#include <vector>
#include "../port/platform/port_json.h"
#include "../design/design_hooks.h"  // DesignModClassicRow
#include "studio_model.h"

namespace {

struct Origin {
  StudioSource src = SS_NONE;
  std::string from;
};

struct Row {
  std::vector<std::vector<long long>> n;  // column by column, the scale taken off
  std::vector<std::string> paths;         // a path column's text
  std::vector<Origin> o;
  bool bOk = false;
  std::string why;
  int nState = 0;  // 0 not built, 1 being built, 2 built
  const PortJson *pJson = nullptr;
  std::string mod;
  int nSub = 0;
};

struct Resolved {
  unsigned nChange = 0;
  std::map<std::string, Row> rows[ST_TABLES];  // by name, the newest submission's
  std::map<int, Row> edits[ST_TABLES];         // a classic row's newest state, by row
};
Resolved g_res;

// The classic rows, read once (they never change).
std::map<int, Row> g_classic[ST_TABLES];

const char *const kVoices[] = {"ready_sound",        "what_sound_first",  "what_sound_last", "pissed_sound_first",
                               "pissed_sound_last", "yes_sound_first",   "yes_sound_last"};

const Row &Classic(int t, int nRow) {
  auto it = g_classic[t].find(nRow);
  if (it != g_classic[t].end())
    return it->second;
  Row r;
  std::vector<std::vector<long long>> cols;
  r.bOk = DesignModClassicRow(t, nRow, &cols, &r.paths);
  const char *pszName = StudioClassicName(t, nRow);
  const std::string name = pszName ? pszName : "row " + std::to_string(nRow);
  if (r.bOk) {
    const std::vector<StudioField> &fields = StudioFields(t);
    r.n = cols;
    for (size_t c = 0; c < r.n.size(); ++c)
      for (const StudioField &f : fields)
        if (f.nColumn == static_cast<int>(c) && f.nScale > 1)
          for (long long &v : r.n[c])
            v /= f.nScale;
    r.o.assign(r.n.size(), Origin{SS_CLASSIC, name});
  } else {
    r.why = std::string(StudioTableName(t)) + " has no classic row " + std::to_string(nRow);
  }
  r.nState = 2;
  return g_classic[t][nRow] = r;
}

// A classic row as the edits so far leave it.
const Row &ClassicNow(int t, int nRow) {
  auto it = g_res.edits[t].find(nRow);
  return it != g_res.edits[t].end() ? it->second : Classic(t, nRow);
}

// A name a field gives for a row of table nRef: a mod's record (the newest built or to be built of
// that name), else a classic row; -1 for none.
long long NameRow(int nRef, const std::string &name, const std::map<std::string, int> &ids) {
  auto it = ids.find(std::string(StudioTableName(nRef)) + "/" + name);
  if (it != ids.end())
    return it->second;
  return StudioClassicRow(nRef, name);
}

struct Pass {
  std::map<std::string, int> ids;      // "table/name": the newest id of a record of that name
  std::map<std::string, int> scripts;  // an image script's Name: its id, the newest of that name
  int nScripts = 0;                    // the scripts so far, whose ids run from 1000
  std::string mod;
  int nSub = 0;
};

// A submission's image scripts, numbered as the loader numbers them: from 1000, in the order of
// their headers, after every earlier submission's (port_moddata.cpp, g_scripts).
void Scripts(const std::string &text, Pass *pPass) {
  bool bHeader = false;
  int nHere = 0;
  size_t at = 0;
  while (at < text.size()) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos)
      end = text.size();
    std::string line = text.substr(at, end - at);
    at = end + 1;
    const size_t hash = line.find('#');
    if (hash != std::string::npos)
      line.erase(hash);
    // the line's words, split at spaces and tabs
    std::vector<std::string> w;
    std::string word;
    for (char c : line + " ") {
      if (isspace(static_cast<unsigned char>(c))) {
        if (!word.empty())
          w.push_back(word);
        word.clear();
      } else {
        word += c;
      }
    }
    if (w.empty())
      continue;
    if (w[0] == ".headerstart") {
      bHeader = true;
    } else if (w[0] == ".headerend") {
      bHeader = false;
    } else if (bHeader && w[0] == "Name" && w.size() > 1) {
      pPass->scripts[w[1]] = 1000 + pPass->nScripts + nHere;
      ++nHere;
    }
  }
  pPass->nScripts += nHere;
}

// A field's own value into a column's numbers, as the loader's Fields reads it.
bool Values(int t, const StudioField &f, const PortJson &v, const Pass &pass, std::vector<long long> *pOut,
            std::string *pWhy) {
  pOut->clear();
  const std::string where = std::string(StudioTableName(t)) + " ." + f.key;
  if (f.key == "flags" && v.kind == PortJson::ARRAY && (t == ST_UNITS || t == ST_SOUNDS)) {
    const std::vector<std::string> &names = StudioFlags(t == ST_UNITS ? "units" : "sounds");
    long long bits = 0;
    for (const PortJson &i : v.items) {
      size_t k = 0;
      while (k < names.size() && names[k] != i.Str())
        ++k;
      if (k == names.size()) {
        *pWhy = f.key + " has no flag " + i.Str();
        return false;
      }
      bits |= 1ll << k;
    }
    pOut->push_back(bits);
    return true;
  }
  auto one = [&](const PortJson &x) -> bool {
    if (x.kind == PortJson::NUMBER)
      pOut->push_back(static_cast<long long>(x.d));
    else if (x.kind == PortJson::BOOL)
      pOut->push_back(x.b ? 1 : 0);
    else if (x.kind == PortJson::STRING && f.bScript) {
      auto it = pass.scripts.find(x.s);
      if (it == pass.scripts.end()) {
        *pWhy = f.key + " names " + x.s + ", which is no script in iscript.txt";
        return false;
      }
      pOut->push_back(it->second);
    } else if (x.kind == PortJson::STRING && f.nRef >= 0) {
      const long long n = NameRow(f.nRef, x.s, pass.ids);
      if (n < 0) {
        *pWhy = f.key + " names " + x.s + ", which is no " + StudioTableName(f.nRef) + " the mod or classic has";
        return false;
      }
      pOut->push_back(n);
    } else {
      *pWhy = f.key + " takes " + (f.nRef >= 0 ? "a name or a number" : "a number");
      return false;
    }
    return true;
  };
  if (v.kind == PortJson::ARRAY) {
    for (const PortJson &i : v.items)
      if (!one(i))
        return false;
  } else if (!one(v)) {
    return false;
  }
  if (static_cast<int>(pOut->size()) != f.nCount) {
    *pWhy = f.key + " takes " + std::to_string(f.nCount) + " values";
    return false;
  }
  (void)where;
  return true;
}

// The fields the loader reads where they are used, not as columns: an image's "grp" names its
// frames' file (its column keeps its base's), a unit's art and an image's sheet are their own.
bool NoColumn(int t, const std::string &key) {
  static const char *const kAny[] = {"base", "edit", "name", "grp", "buttons", "looks", "requires", "use_requires",
                                     "comment"};
  for (const char *psz : kAny)
    if (key == psz)
      return true;
  if (t == ST_UNITS && (key == "icon" || key == "wireframe" || key == "transport_wireframe"))
    return true;
  if (t == ST_IMAGES && (key == "team" || key == "frame" || key == "origin" || key == "scale" || key == "frames" ||
                         key == "directions"))
    return true;
  return false;
}

// The record's own fields over its row.
bool Fields(int t, const std::string &name, const PortJson &j, const Pass &pass, Row *pRow, StudioSource src) {
  for (const PortJsonMember &m : j.members) {
    if (NoColumn(t, m.first))
      continue;
    const StudioField *pf = StudioFieldOf(t, m.first);
    if (pf == nullptr || pf->nColumn < 0 || pf->nColumn >= static_cast<int>(pRow->n.size()))
      continue;  // a field that is no column, read where it is used
    const int c = pf->nColumn;
    if (pf->bPath) {
      if (c >= static_cast<int>(pRow->paths.size()))
        pRow->paths.resize(static_cast<size_t>(c) + 1);
      pRow->paths[c] = m.second.Str();  // a sound's file, a portrait's videos
    } else {
      std::vector<long long> vals;
      std::string why;
      if (!Values(t, *pf, m.second, pass, &vals, &why)) {
        pRow->why = std::string(StudioTableName(t)) + " " + name + "." + why;
        return false;
      }
      pRow->n[c] = vals;
    }
    pRow->o[c] = Origin{src, name};
  }
  return true;
}

void LooksVoices(const std::string &name, const PortJson &j, const Pass &pass, Row *pRow) {
  const PortJson &looks = j["looks"];
  long long nLooks = looks.kind == PortJson::NUMBER ? static_cast<long long>(looks.d)
                     : looks.kind == PortJson::STRING ? NameRow(ST_UNITS, looks.s, pass.ids)
                                                      : -1;
  if (nLooks < 0 || nLooks >= StudioClassicRows(ST_UNITS))
    return;
  const Row &from = ClassicNow(ST_UNITS, static_cast<int>(nLooks));
  if (!from.bOk)
    return;
  bool bAny = false;
  for (const char *psz : kVoices)
    bAny = bAny || j[psz].kind != PortJson::NONE;
  auto take = [&](const char *psz) {
    const StudioField *pf = StudioFieldOf(ST_UNITS, psz);
    if (pf && pf->nColumn >= 0 && pf->nColumn < static_cast<int>(pRow->n.size())) {
      pRow->n[pf->nColumn] = from.n[pf->nColumn];
      pRow->o[pf->nColumn] = from.o[pf->nColumn];
    }
  };
  if (!bAny)
    for (const char *psz : kVoices)
      take(psz);
  if (j["portrait"].kind == PortJson::NONE)
    take("portrait");
  (void)name;
}

bool Build(int t, const std::string &name, Pass &pass);

// The row a record starts from, its base's.
bool BaseRow(int t, const std::string &name, const PortJson &base, Pass &pass, Row *pRow) {
  int nRow = -1;
  if (base.kind == PortJson::STRING) {
    auto it = g_res.rows[t].find(base.s);
    if (it != g_res.rows[t].end() && base.s != name) {
      if (!Build(t, base.s, pass))
        return false;
      const Row &b = g_res.rows[t][base.s];
      if (!b.bOk) {
        pRow->why = b.why;
        return false;
      }
      pRow->n = b.n;
      pRow->paths = b.paths;
      pRow->o = b.o;
      // what the base gives itself shows as its; what it inherited keeps where it came from
      for (Origin &o : pRow->o)
        if (o.src == SS_OWN)
          o.src = SS_BASE;
      return true;
    }
    nRow = StudioClassicRow(t, base.s);
  } else if (base.kind == PortJson::NUMBER) {
    nRow = static_cast<int>(base.d);
  } else if (t == ST_SOUNDS || t == ST_PORTRAITS) {
    nRow = 0;
  }
  if (nRow < 0 || nRow >= StudioClassicRows(t)) {
    pRow->why = std::string(StudioTableName(t)) + " " + name + " needs a base, a classic row or one of the mod's";
    return false;
  }
  const Row &b = ClassicNow(t, nRow);
  pRow->n = b.n;
  pRow->paths = b.paths;
  pRow->o = b.o;
  return b.bOk;
}

bool Build(int t, const std::string &name, Pass &pass) {
  Row &r = g_res.rows[t][name];
  if (r.nState == 2)
    return r.bOk;
  if (r.nState == 1) {
    r.why = std::string(StudioTableName(t)) + " " + name + " is its own base";
    return false;
  }
  r.nState = 1;
  const PortJson &j = *r.pJson;
  Row built;
  built.pJson = r.pJson;
  built.mod = r.mod;
  built.nSub = r.nSub;
  bool bOk = BaseRow(t, name, j["base"], pass, &built) && Fields(t, name, j, pass, &built, SS_OWN);
  if (bOk && t == ST_UNITS)
    LooksVoices(name, j, pass, &built);
  // an image with frames of its own has no damage overlay: its base's would not fit them
  if (bOk && t == ST_IMAGES && j["grp"].kind != PortJson::NONE) {
    const StudioField *pf = StudioFieldOf(t, "damage_overlay");
    if (pf && pf->nColumn >= 0 && pf->nColumn < static_cast<int>(built.n.size())) {
      for (long long &v : built.n[pf->nColumn])
        v = 0;
      built.o[pf->nColumn] = Origin{SS_OWN, name};
    }
  }
  built.bOk = bOk;
  built.nState = 2;
  Row &now = g_res.rows[t][name];
  now = built;
  return bOk;
}

// One submission: its records named and numbered, its edits done, then its records built.
void Submission(const std::string &mod, const PortJson &types, int nSub, Pass &pass,
                const std::map<std::string, std::pair<int, int>> *pIds) {
  pass.mod = mod;
  pass.nSub = nSub;
  for (int t = 0; t < ST_TABLES; ++t)
    for (const PortJsonMember &m : types[StudioTableName(t)].members)
      if (m.second["edit"].kind == PortJson::NONE) {
        auto it = pIds[t].find(m.first);
        if (it != pIds[t].end())
          pass.ids[std::string(StudioTableName(t)) + "/" + m.first] = it->second.first;
      }
  // the edits, every table's, before any record is built
  for (int t = 0; t < ST_TABLES; ++t)
    for (const PortJsonMember &m : types[StudioTableName(t)].members) {
      const PortJson &target = m.second["edit"];
      if (target.kind == PortJson::NONE)
        continue;
      auto rec = target.kind == PortJson::STRING ? g_res.rows[t].find(target.s) : g_res.rows[t].end();
      if (rec != g_res.rows[t].end() && rec->second.nSub < nSub && rec->second.bOk) {
        Fields(t, m.first, m.second, pass, &rec->second, SS_EDIT);
        continue;
      }
      const int nRow = target.kind == PortJson::NUMBER ? static_cast<int>(target.d)
                       : target.kind == PortJson::STRING ? StudioClassicRow(t, target.s)
                                                         : -1;
      if (nRow < 0 || nRow >= StudioClassicRows(t))
        continue;
      Row e = ClassicNow(t, nRow);
      e.pJson = &m.second;
      e.mod = mod;
      e.nSub = nSub;
      if (Fields(t, m.first, m.second, pass, &e, SS_EDIT))
        g_res.edits[t][nRow] = e;
    }
  // then the records
  for (int t = 0; t < ST_TABLES; ++t) {
    for (const PortJsonMember &m : types[StudioTableName(t)].members) {
      if (m.second["edit"].kind != PortJson::NONE)
        continue;
      Row r;
      r.pJson = &m.second;
      r.mod = mod;
      r.nSub = nSub;
      g_res.rows[t][m.first] = r;
    }
  }
  for (int t = 0; t < ST_TABLES; ++t)
    for (const PortJsonMember &m : types[StudioTableName(t)].members)
      if (m.second["edit"].kind == PortJson::NONE)
        Build(t, m.first, pass);
}

void Resolve(void) {
  if (g_res.nChange == StudioChange() && g_res.nChange != 0)
    return;
  g_res = Resolved();
  g_res.nChange = StudioChange();
  if (!StudioIsOpen())
    return;
  // ids as the loader gives them: each submission's records of a table in the order of their
  // names, from the table's first mod id; a name in two submissions finds the newest
  int anNext[ST_TABLES];
  for (int t = 0; t < ST_TABLES; ++t)
    anNext[t] = StudioModFirst(t);
  Pass pass;
  int nSub = 0;
  auto run = [&](const std::string &mod, const PortJson &types, const std::string &script) {
    Scripts(script, &pass);
    std::map<std::string, std::pair<int, int>> ids[ST_TABLES];
    for (int t = 0; t < ST_TABLES; ++t)
      for (const PortJsonMember &m : types[StudioTableName(t)].members)
        if (m.second["edit"].kind == PortJson::NONE)
          ids[t][m.first] = {0, 0};
    for (int t = 0; t < ST_TABLES; ++t)
      for (auto &e : ids[t])  // a std::map's order: the names' bytes, as the loader sorts them
        e.second.first = anNext[t]++;
    Submission(mod, types, ++nSub, pass, ids);
  };
  for (int i = 0; i < StudioDependencies(); ++i)
    run(StudioDependencyId(i), StudioDependencyTypes(i), StudioDependencyScript(i));
  run(StudioModId(), StudioDoc("types"), StudioScript());
}

// A record's row: the mod's (its own or a dependency's), an edited classic row, or a classic row.
const Row *RowOf(int t, const std::string &name) {
  Resolve();
  if (t < 0 || t >= ST_TABLES)
    return nullptr;
  auto it = g_res.rows[t].find(name);
  if (it != g_res.rows[t].end())
    return &it->second;
  // an edit record by its own name shows the row it edits
  const PortJson &j = StudioRecordJson(t, name);
  const PortJson &target = j["edit"];
  int nRow = target.kind == PortJson::NUMBER   ? static_cast<int>(target.d)
             : target.kind == PortJson::STRING ? StudioClassicRow(t, target.s)
                                               : StudioClassicRow(t, name);
  if (nRow < 0)
    return nullptr;
  return &ClassicNow(t, nRow);
}

StudioValue ValueOf(int t, const Row &r, const StudioField &f, const std::string &name) {
  StudioValue v;
  const PortJson &j = StudioRecordJson(t, name);
  v.own = j[f.key.c_str()];
  if (f.nColumn >= 0 && f.nColumn < static_cast<int>(r.n.size())) {
    v.n = r.n[f.nColumn];
    v.source = r.o[f.nColumn].src;
    v.from = r.o[f.nColumn].from;
    if (f.bPath && f.nColumn < static_cast<int>(r.paths.size()) && v.own.kind == PortJson::NONE)
      v.own = StudioJsonString(r.paths[f.nColumn]);  // shown as its text, inherited
    if (j[f.key.c_str()].kind != PortJson::NONE) {
      v.source = SS_OWN;
      v.from = name;
    }
    return v;
  }
  // a field that is no column: the record's own, else its bases'
  if (v.own.kind != PortJson::NONE) {
    v.source = SS_OWN;
    v.from = name;
    return v;
  }
  std::string at = j["base"].Str();
  for (int i = 0; i < 16 && !at.empty(); ++i) {
    const PortJson &b = StudioRecordJson(t, at);
    if (b.kind == PortJson::NONE)
      break;
    if (b[f.key.c_str()].kind != PortJson::NONE) {
      v.own = PortJson();
      v.source = SS_BASE;
      v.from = at;
      return v;
    }
    at = b["base"].Str();
  }
  return v;
}

}  // namespace

bool StudioGet(int t, const std::string &name, const std::string &key, StudioValue *pOut) {
  const StudioField *pf = StudioFieldOf(t, key);
  const Row *pRow = RowOf(t, name);
  if (pf == nullptr || pRow == nullptr || !pRow->bOk)
    return false;
  *pOut = ValueOf(t, *pRow, *pf, name);
  return true;
}

bool StudioRow(int t, const std::string &name, std::vector<StudioValue> *pOut, std::string *pWhy) {
  pOut->clear();
  const Row *pRow = RowOf(t, name);
  if (pRow == nullptr) {
    *pWhy = std::string(StudioTableName(t)) + " has no " + name;
    return false;
  }
  if (!pRow->bOk) {
    *pWhy = pRow->why;
    return false;
  }
  for (const StudioField &f : StudioFields(t))
    if (f.nColumn >= 0)
      pOut->push_back(ValueOf(t, *pRow, f, name));
  return true;
}

long long StudioNumber(int t, const std::string &name, const std::string &key, long long nDefault) {
  StudioValue v;
  return StudioGet(t, name, key, &v) && !v.n.empty() ? v.n[0] : nDefault;
}

std::string StudioRefName(int nRef, long long nRow) {
  if (nRef < 0 || nRef >= ST_TABLES)
    return std::to_string(nRow);
  if (const char *psz = StudioClassicName(nRef, static_cast<int>(nRow)))
    return psz;
  for (const StudioRecord &r : StudioRecords(nRef))
    if (r.nId == nRow && r.kind != SK_EDIT)
      return r.name;
  return std::to_string(nRow);
}
