// studio_commands.cpp -- Hyperion Studio's channel commands (studio_model.h): every change the
// studio makes to a mod, and every read a tool needs, as a command of the live channel. The
// studio's panels run them in-process (DesignCall) and Claude and outside tools over the channel,
// so all of them edit the same way and each edit carries who made it ("sender").
//
//   studio_status                       the open mod, its unsaved edits, an autosave waiting
//   studio_mods                         the installed mods that bring data
//   studio_open {mod | folder}          a mod opened: by its id, or a folder holding a mod.json
//   studio_list {table, classic?}       a table's records; the classic rows too with classic: true
//   studio_get {table, name}            a record: every field's value and where it comes from
//   studio_set {table, name, field, value, sender?, merge?}   a field set; value null reverts it
//   studio_create {table, name, record | base}               a record made
//   studio_delete {table, name}         a record taken away
//   studio_damage {weapon, target, level?}   attacks and seconds to kill a unit, by the classic rules
//   studio_files {ext?}, studio_import {from, folder?}   the data folder's files; a file copied in
//   studio_put {doc, path, value?}      a member of a document set, as a card's buttons; taken away without value
//   studio_make_unit {name, base?, race?, at?, copy_weapon?, button?}   a new unit and what it comes with, one edit
//   studio_raw {table, name, text}      a record written as text (the Raw view)
//   studio_script {text}                the image script written
//   studio_undo, studio_redo            the last group of edits undone or done again
//   studio_history {count?}             the edits, newest last
//   studio_save, studio_recover         the files written; the autosave taken back
//   studio_push {replace?}              the working data pushed into the canvas game (mod_push)
//   studio_pushes                       the pushes and their answers
//   studio_verify                       the last push's rows, as the loader built them, against the studio's
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include "../port/data/port_mods.h"  // PortMods
#include "../port/platform/port_json.h"
#include "../port/tools/port_control.h"  // PortControlCommand
#include "../design/design_engine.h"      // DesignCall
#include "../design/design_hooks.h"       // DesignModLoaded
#include "studio_model.h"

namespace {

std::string Q(const std::string &s) {
  return PortJsonQuote(s);
}

const char *SourceName(StudioSource s) {
  switch (s) {
  case SS_OWN: return "own";
  case SS_BASE: return "base";
  case SS_EDIT: return "edit";
  case SS_CLASSIC: return "classic";
  default: return "none";
  }
}

const char *KindName(StudioKind k) {
  switch (k) {
  case SK_OWN: return "own";
  case SK_EDIT: return "edit";
  case SK_DEPENDENCY: return "dependency";
  default: return "classic";
  }
}

std::string Numbers(const std::vector<long long> &n) {
  std::string out = "[";
  for (size_t i = 0; i < n.size(); ++i)
    out += (i ? "," : "") + std::to_string(n[i]);
  return out + "]";
}

bool Table(const PortJson &j, int *pT, std::string *pAnswer) {
  *pT = StudioTableOf(j["table"].Str());
  if (*pT < 0) {
    *pAnswer = "table is one of units, weapons, flingy, sprites, images, orders, techs, upgrades, sounds, portraits";
    return false;
  }
  return true;
}

std::string Sender(const PortJson &j) {
  return j["sender"].Str("claude");  // over the channel, unless it says; the panels say "you"
}

std::string Status(void) {
  std::string when;
  const bool bRecover = StudioRecoverWaiting(&when);
  return "\"open\":" + std::string(StudioIsOpen() ? "true" : "false") + ",\"mod\":" + Q(StudioModId()) +
         ",\"folder\":" + Q(StudioModFolder()) + ",\"data\":" + Q(StudioDataFolder()) +
         ",\"unsaved\":" + std::to_string(StudioUnsaved()) + ",\"history\":" +
         std::to_string(StudioHistory().size()) + ",\"at\":" + std::to_string(StudioHistoryAt()) +
         ",\"change\":" + std::to_string(StudioChange()) + ",\"recover\":" +
         (bRecover ? "{\"when\":" + Q(when) + "}" : std::string("null")) +
         (StudioFieldsError().empty() ? std::string() : ",\"fields_error\":" + Q(StudioFieldsError()));
}

bool CmdStatus(const PortJson &, std::string *pAnswer) {
  *pAnswer = Status();
  return true;
}

bool CmdMods(const PortJson &, std::string *pAnswer) {
  std::string list;
  for (const PortMod &m : PortMods())
    if (!m.bBuiltIn && !m.data.empty())
      list += std::string(list.empty() ? "" : ",") + "{\"id\":" + Q(m.id) + ",\"name\":" + Q(m.name) +
              ",\"version\":" + Q(m.version) + ",\"folder\":" + Q(m.folder) + "}";
  *pAnswer = "\"mods\":[" + list + "]";
  return true;
}

bool CmdOpen(const PortJson &j, std::string *pAnswer) {
  const std::string what = j["mod"].kind == PortJson::STRING ? j["mod"].s : j["folder"].Str();
  if (what.empty()) {
    *pAnswer = "studio_open needs mod (an installed mod's id) or folder";
    return false;
  }
  std::string why;
  if (!StudioOpen(what, &why)) {
    *pAnswer = why;
    return false;
  }
  *pAnswer = Status();
  return true;
}

std::string RecordFields(const StudioRecord &r) {
  // "row", not "id": the channel numbers its requests and answers by id
  return "\"name\":" + Q(r.name) + ",\"kind\":\"" + KindName(r.kind) + "\",\"row\":" + std::to_string(r.nId) +
         ",\"base\":" + Q(r.base) + ",\"mod\":" + Q(r.mod) + ",\"label\":" + Q(StudioShowName(r.t, r.name));
}

bool CmdList(const PortJson &j, std::string *pAnswer) {
  int t = 0;
  if (!Table(j, &t, pAnswer))
    return false;
  std::string list;
  for (const StudioRecord &r : StudioRecords(t))
    list += std::string(list.empty() ? "" : ",") + "{" + RecordFields(r) + "}";
  std::string classic;
  if (j["classic"].kind == PortJson::BOOL && j["classic"].b)
    for (int n = 0; n < StudioClassicRows(t); ++n)
      if (const char *psz = StudioClassicName(t, n))
        classic += std::string(classic.empty() ? "" : ",") + "{\"name\":" + Q(psz) + ",\"row\":" + std::to_string(n) +
                   ",\"label\":" + Q(StudioShowName(t, psz)) + "}";
  *pAnswer = "\"table\":" + Q(StudioTableName(t)) + ",\"records\":[" + list + "]" +
             (classic.empty() ? std::string() : ",\"classic\":[" + classic + "]");
  return true;
}

bool CmdGet(const PortJson &j, std::string *pAnswer) {
  int t = 0;
  if (!Table(j, &t, pAnswer))
    return false;
  const std::string name = j["name"].Str();
  StudioRecord r;
  if (!StudioRecordOf(t, name, &r)) {
    *pAnswer = std::string(StudioTableName(t)) + " has no " + name;
    return false;
  }
  std::string fields;
  for (const StudioField &f : StudioFields(t)) {
    StudioValue v;
    if (!StudioGet(t, name, f.key, &v))
      continue;
    fields += std::string(fields.empty() ? "" : ",") + "{\"key\":" + Q(f.key) + ",\"label\":" + Q(f.label) +
              ",\"cat\":" + Q(f.cat) + ",\"editor\":" + Q(f.editor) + ",\"simple\":" + (f.bSimple ? "true" : "false") +
              ",\"value\":" + Numbers(v.n) + ",\"own\":" + PortJsonWrite(v.own) + ",\"source\":\"" +
              SourceName(v.source) + "\",\"from\":" + Q(v.from) + "}";
  }
  std::vector<StudioValue> row;
  std::string why;
  const bool bOk = StudioRow(t, name, &row, &why);
  *pAnswer = RecordFields(r) + ",\"record\":" + PortJsonWrite(StudioRecordJson(t, name)) + ",\"resolves\":" +
             (bOk ? "true" : "false") + (bOk ? std::string() : ",\"why\":" + Q(why)) + ",\"fields\":[" + fields + "]";
  return true;
}

bool Done(bool bOk, const std::string &why, std::string *pAnswer) {
  if (!bOk) {
    *pAnswer = why;
    return false;
  }
  *pAnswer = Status();
  return true;
}

bool CmdSet(const PortJson &j, std::string *pAnswer) {
  int t = 0;
  if (!Table(j, &t, pAnswer))
    return false;
  std::string why;
  const PortJson &v = j["value"];
  const bool bRevert = v.kind == PortJson::NULLV || (j["revert"].kind == PortJson::BOOL && j["revert"].b);
  if (v.kind == PortJson::NONE && !bRevert) {
    *pAnswer = "studio_set needs value, or revert: true (or value null) to take the field back to its base's";
    return false;
  }
  const bool bOk = bRevert
                       ? StudioRevertField(t, j["name"].Str(), j["field"].Str(), Sender(j), &why)
                       : StudioSetField(t, j["name"].Str(), j["field"].Str(), v, Sender(j), j["merge"].Str(), &why);
  return Done(bOk, why, pAnswer);
}

bool CmdCreate(const PortJson &j, std::string *pAnswer) {
  int t = 0;
  if (!Table(j, &t, pAnswer))
    return false;
  PortJson record = j["record"];
  if (record.kind == PortJson::NONE) {
    record = StudioJsonObject();
    if (j["base"].kind != PortJson::NONE)
      StudioJsonPut(record, "base", j["base"]);
    if (j["edit"].kind != PortJson::NONE)
      StudioJsonPut(record, "edit", j["edit"]);
  }
  std::string why;
  return Done(StudioCreate(t, j["name"].Str(), record, Sender(j), &why), why, pAnswer);
}

// studio_damage {weapon, target, level?}: what a weapon does to a unit by the classic rules
// (studio_calc.cpp): an attack's damage on its life at the target's size and armour, the attacks
// and seconds that kill it, and damage a second; level upgrades both sides.
bool CmdDamage(const PortJson &j, std::string *pAnswer) {
  const std::string w = j["weapon"].Str(), u = j["target"].Str();
  StudioRecord rw, ru;
  if (!StudioRecordOf(ST_WEAPONS, w, &rw) || !StudioRecordOf(ST_UNITS, u, &ru)) {
    *pAnswer = "studio_damage needs a weapon and a target unit, by name";
    return false;
  }
  const int nLevel = static_cast<int>(j["level"].Num(0));
  const long long nHits = std::max(1LL, StudioNumber(ST_WEAPONS, w, "damage_factor", 1));
  const double dHit = StudioHitDamage(w, nLevel, static_cast<int>(StudioNumber(ST_UNITS, u, "size")),
                                      static_cast<int>(StudioNumber(ST_UNITS, u, "armor")) + nLevel);
  const int nAttacks = StudioAttacksToKill(w, nLevel, u, nLevel);
  const double dSeconds = nAttacks > 0 ? (nAttacks - 1) * StudioNumber(ST_WEAPONS, w, "cooldown") / 24.0 : 0.0;
  char sz[160];
  snprintf(sz, sizeof sz, "\"attack_on_life\":%.2f,\"attacks\":%d,\"seconds\":%.2f,\"per_second\":%.2f", dHit * nHits, nAttacks,
           dSeconds, StudioDamagePerSecond(w, nLevel));
  *pAnswer = sz;
  return true;
}

// studio_files {ext?}: the data folder's files, relative to it, those of the extensions given
// ("png,grp"); studio_import {from, folder?}: a file copied into the data folder, its path answered.
bool CmdFiles(const PortJson &j, std::string *pAnswer) {
  std::string list;
  for (const std::string &f : StudioFiles(j["ext"].Str()))
    list += (list.empty() ? "" : ",") + Q(f);
  *pAnswer = "\"folder\":" + Q(StudioDataFolder()) + ",\"files\":[" + list + "]";
  return true;
}

bool CmdImport(const PortJson &j, std::string *pAnswer) {
  std::string rel, why;
  if (!StudioImport(j["from"].Str(), j["folder"].Str(), &rel, &why)) {
    *pAnswer = why;
    return false;
  }
  *pAnswer = "\"path\":" + Q(rel);
  return true;
}

// studio_put {doc, path, value?}: a member of a document set, or taken away without value.
bool CmdPut(const PortJson &j, std::string *pAnswer) {
  std::vector<std::string> path;
  for (const PortJson &k : j["path"].items)
    path.push_back(k.Str());
  std::string why;
  return Done(StudioPut(j["doc"].Str("types"), path, j["value"], Sender(j), "set " + j["doc"].Str("types") + " " +
                        (path.empty() ? std::string() : path.back()), &why),
              why, pAnswer);
}

// A new unit's race in its group flags: the three low bits, Zerg, Terran and Protoss.
long long RaceBit(const std::string &race) {
  return race == "zerg" ? 1 : race == "terran" ? 2 : race == "protoss" ? 4 : 0;
}

// studio_make_unit {name, label?, base?, race?, at?, copy_weapon?, values?, button?: {card, position,
// hotkey, text}}: a new unit and what it comes with, as one edit undone together: its record based
// on base, its race, where it is made, its own copy of its base's weapons, a button that makes it
// on its maker's card. Nothing is made when a part is refused.
bool CmdMakeUnit(const PortJson &j, std::string *pAnswer) {
  const std::string name = j["name"].Str(), base = j["base"].Str(), sender = Sender(j);
  const int nAt = StudioHistoryAt();
  std::string why;
  StudioGroupBegin(sender, "made units " + name);
  PortJson rec = StudioJsonObject();
  if (!base.empty())
    StudioJsonPut(rec, "base", StudioJsonString(base));
  if (!j["label"].Str().empty())
    StudioJsonPut(rec, "name", j["label"]);
  for (const PortJsonMember &m : j["values"].members)
    StudioJsonPut(rec, m.first, m.second);
  const long long nBit = RaceBit(j["race"].Str());
  if (nBit != 0 && !base.empty()) {
    const long long g = StudioNumber(ST_UNITS, base, "group_flags");
    if ((g & 7) != nBit)
      StudioJsonPut(rec, "group_flags", StudioJsonNumber(static_cast<double>((g & ~7LL) | nBit)));
  }
  if (!j["at"].Str().empty()) {
    PortJson at = StudioJsonObject(), req;
    StudioJsonPut(at, "at", j["at"]);
    req.kind = PortJson::ARRAY;
    req.items.push_back(at);
    StudioJsonPut(rec, "requires", req);
  }
  bool bOk = true;
  if (j["copy_weapon"].kind == PortJson::BOOL && j["copy_weapon"].b && !base.empty()) {
    const std::string ground = StudioUnitWeapon(base, false), air = StudioUnitWeapon(base, true);
    auto copy = [&](const std::string &weapon, const std::string &copyName, const char *pszField) {
      if (!bOk || weapon.empty())
        return;
      PortJson w = StudioJsonObject();
      StudioJsonPut(w, "base", StudioJsonString(weapon));
      if (StudioRecordJson(ST_WEAPONS, copyName).kind == PortJson::NONE)
        bOk = StudioCreate(ST_WEAPONS, copyName, w, sender, &why);
      StudioJsonPut(rec, pszField, StudioJsonString(copyName));
    };
    copy(ground, name + "_weapon", "ground_weapon");
    copy(air, air == ground ? name + "_weapon" : name + "_air_weapon", "air_weapon");
  }
  bOk = bOk && StudioCreate(ST_UNITS, name, rec, sender, &why);
  const PortJson &button = j["button"];
  if (bOk && button.kind == PortJson::OBJECT && !button["card"].Str().empty()) {
    const std::string card = button["card"].Str();
    PortJson list = StudioDoc("types")["cards"][card.c_str()];
    if (list.kind != PortJson::ARRAY) {
      list = PortJson();
      list.kind = PortJson::ARRAY;
    }
    PortJson b = StudioJsonObject();
    StudioJsonPut(b, "position", StudioJsonNumber(button["position"].Num(1)));
    StudioJsonPut(b, "train", StudioJsonString(name));
    StudioJsonPut(b, "hotkey", button["hotkey"]);
    StudioJsonPut(b, "text", button["text"]);
    list.items.push_back(b);
    bOk = StudioPut("types", {"cards", card}, list, sender, "a button on the " + card + "'s card", &why);
  }
  StudioGroupEnd();
  if (!bOk) {
    std::string ignore;
    if (StudioHistoryAt() > nAt)
      StudioUndo(&ignore);  // what was made of it taken back, so a refusal leaves the mod as it was
    *pAnswer = why;
    return false;
  }
  return Done(true, why, pAnswer);
}

bool CmdDelete(const PortJson &j, std::string *pAnswer) {
  int t = 0;
  if (!Table(j, &t, pAnswer))
    return false;
  std::string why;
  return Done(StudioDelete(t, j["name"].Str(), Sender(j), &why), why, pAnswer);
}

bool CmdRaw(const PortJson &j, std::string *pAnswer) {
  int t = 0;
  if (!Table(j, &t, pAnswer))
    return false;
  std::string why;
  return Done(StudioSetRaw(t, j["name"].Str(), j["text"].Str(), Sender(j), &why), why, pAnswer);
}

bool CmdScript(const PortJson &j, std::string *pAnswer) {
  if (j["text"].kind != PortJson::STRING) {
    *pAnswer = "studio_script needs text";
    return false;
  }
  std::string why;
  return Done(StudioSetScript(j["text"].s, Sender(j), &why), why, pAnswer);
}

bool CmdUndo(const PortJson &, std::string *pAnswer) {
  std::string why;
  return Done(StudioUndo(&why), why, pAnswer);
}

bool CmdRedo(const PortJson &, std::string *pAnswer) {
  std::string why;
  return Done(StudioRedo(&why), why, pAnswer);
}

bool CmdHistory(const PortJson &j, std::string *pAnswer) {
  const std::vector<StudioEdit> &h = StudioHistory();
  const size_t nCount = static_cast<size_t>(j["count"].Num(50));
  const size_t nFrom = h.size() > nCount ? h.size() - nCount : 0;
  std::string list;
  for (size_t i = nFrom; i < h.size(); ++i) {
    const StudioEdit &e = h[i];
    std::string path;
    for (const std::string &p : e.path)
      path += (path.empty() ? "" : ",") + Q(p);
    list += std::string(list.empty() ? "" : ",") + "{\"time\":" + std::to_string(e.nTime) + ",\"group\":" +
            std::to_string(e.nGroup) + ",\"sender\":" + Q(e.sender) + ",\"label\":" + Q(e.label) + ",\"doc\":" +
            Q(e.doc) + ",\"path\":[" + path + "],\"done\":" +
            (static_cast<int>(i) < StudioHistoryAt() ? "true" : "false") + "}";
  }
  *pAnswer = "\"edits\":[" + list + "],\"at\":" + std::to_string(StudioHistoryAt());
  return true;
}

bool CmdSave(const PortJson &, std::string *pAnswer) {
  std::string why;
  return Done(StudioSave(&why), why, pAnswer);
}

bool CmdRecover(const PortJson &j, std::string *pAnswer) {
  std::string why;
  return Done(StudioRecover(Sender(j), &why), why, pAnswer);
}

// ---- pushes --------------------------------------------------------------------------------------------

struct Push {
  int n;              // the studio's count
  std::string state;  // "asked", "pushed", "refused"
  std::string answer; // mod_push's answer, raw
  int nSubmission = 0;
  unsigned nChange = 0;  // the model's change it pushed
};
std::vector<Push> g_pushes;

bool CmdPush(const PortJson &j, std::string *pAnswer) {
  if (!StudioIsOpen()) {
    *pAnswer = "no mod is open";
    return false;
  }
  StudioAutosave(true);
  // Claude's proposal stays out of the game until it is accepted
  StudioGroupAside(StudioPending(), true);
  std::string fields = "\"mod\":" + Q(StudioModId()) + ",\"folder\":" + Q(StudioDataFolder()) +
                       ",\"types\":" + PortJsonWrite(StudioDoc("types")) + ",\"iscript\":" + Q(StudioScript()) +
                       ",\"scripts\":" + PortJsonWrite(StudioDoc("scripts"));
  StudioGroupAside(StudioPending(), false);
  if (j["replace"].kind != PortJson::NONE)
    fields += ",\"replace\":" + PortJsonWrite(j["replace"]);
  Push p;
  p.n = static_cast<int>(g_pushes.size()) + 1;
  p.state = "asked";
  p.nChange = StudioChange();
  g_pushes.push_back(p);
  const int n = p.n;
  DesignCall("mod_push", fields, [n](const PortJson &a) {
    Push &q = g_pushes[static_cast<size_t>(n) - 1];
    q.state = a["ok"].kind == PortJson::BOOL && a["ok"].b ? "pushed" : "refused";
    q.answer = PortJsonWrite(a);
    q.nSubmission = static_cast<int>(a["submission"].Num(0));
  });
  *pAnswer = "\"push\":" + std::to_string(n) + ",\"state\":\"asked\"";
  return true;
}

}  // namespace

int StudioPushCount(void) {
  return static_cast<int>(g_pushes.size());
}

bool StudioPushInfo(int n, std::string *pState, std::string *pAnswer, unsigned *pnChange) {
  if (n < 1 || n > static_cast<int>(g_pushes.size()))
    return false;
  const Push &p = g_pushes[static_cast<size_t>(n) - 1];
  *pState = p.state;
  *pAnswer = p.answer;
  *pnChange = p.nChange;
  return true;
}

namespace {

std::string PushFields(const Push &p) {
  return "{\"push\":" + std::to_string(p.n) + ",\"state\":" + Q(p.state) + ",\"submission\":" +
         std::to_string(p.nSubmission) + ",\"answer\":" + (p.answer.empty() ? std::string("null") : p.answer) + "}";
}

bool CmdPushes(const PortJson &, std::string *pAnswer) {
  std::string list;
  for (const Push &p : g_pushes)
    list += std::string(list.empty() ? "" : ",") + PushFields(p);
  *pAnswer = "\"pushes\":[" + list + "]";
  return true;
}

// The last push's rows, as the loader built them, against what the studio works out for each of
// the open mod's records: the loader is the judge, and a difference is the studio's to fix.
bool CmdVerify(const PortJson &, std::string *pAnswer) {
  const Push *pLast = nullptr;
  for (const Push &p : g_pushes)
    if (p.state == "pushed")
      pLast = &p;
  if (pLast == nullptr) {
    *pAnswer = "studio_verify needs a push the game took: studio_push first";
    return false;
  }
  if (pLast->nChange != StudioChange()) {
    *pAnswer = "the mod changed since the last push: push again first";
    return false;
  }
  // a push's records take ids after every earlier submission's, so a column naming another record
  // is compared by the name it reaches: the loader's ids of this push's records, by name
  std::map<long long, std::string> loaderNames[ST_TABLES];
  for (int t = 0; t < ST_TABLES; ++t)
    for (const StudioRecord &r : StudioRecords(t)) {
      int nId = -1, nSub = 0;
      std::vector<std::vector<long long>> cols;
      if (r.kind == SK_OWN && DesignModLoaded(t, r.name, &nId, &nSub, &cols) && nSub == pLast->nSubmission)
        loaderNames[t][nId] = r.name;
    }
  auto named = [&](int nRef, long long n) {
    auto it = loaderNames[nRef].find(n);
    if (it != loaderNames[nRef].end())
      return it->second;
    const char *psz = StudioClassicName(nRef, static_cast<int>(n));
    return psz ? std::string(psz) : std::to_string(n);
  };
  int nRecords = 0, nColumns = 0;
  std::string diffs;
  for (int t = 0; t < ST_TABLES; ++t)
    for (const StudioRecord &r : StudioRecords(t)) {
      if (r.kind != SK_OWN)
        continue;
      int nId = -1, nSub = 0;
      std::vector<std::vector<long long>> loaded;
      if (!DesignModLoaded(t, r.name, &nId, &nSub, &loaded) || nSub != pLast->nSubmission) {
        diffs += std::string(diffs.empty() ? "" : ",") + "{\"table\":" + Q(StudioTableName(t)) + ",\"name\":" +
                 Q(r.name) + ",\"why\":\"the push has no such record\"}";
        continue;
      }
      ++nRecords;
      std::vector<StudioValue> row;
      std::string why;
      if (!StudioRow(t, r.name, &row, &why)) {
        diffs += std::string(diffs.empty() ? "" : ",") + "{\"table\":" + Q(StudioTableName(t)) + ",\"name\":" +
                 Q(r.name) + ",\"why\":" + Q(why) + "}";
        continue;
      }
      size_t k = 0;
      for (const StudioField &f : StudioFields(t)) {
        if (f.nColumn < 0)
          continue;
        const StudioValue &v = row[k++];
        if (f.bPath || f.nColumn >= static_cast<int>(loaded.size()))
          continue;
        ++nColumns;
        std::vector<long long> want = loaded[static_cast<size_t>(f.nColumn)];
        for (long long &x : want)
          x /= f.nScale > 0 ? f.nScale : 1;
        bool bSame = want == v.n;
        if (!bSame && f.nRef >= 0 && want.size() == v.n.size()) {
          bSame = true;
          for (size_t i = 0; i < want.size(); ++i)
            bSame = bSame && named(f.nRef, want[i]) == StudioRefName(f.nRef, v.n[i]);
        }
        // a mod's script is numbered after every earlier submission's: both a mod's, the same
        // name reaches it
        if (!bSame && f.bScript && want.size() == 1 && v.n.size() == 1 && want[0] >= 1000 && v.n[0] >= 1000)
          bSame = true;
        // a record's own name is a string the loader adds, numbered after the game's
        if (!bSame && f.key == "label" && want.size() == 1 && want[0] >= 0xF000 && StudioRecordJson(t, r.name)["name"].kind == PortJson::STRING)
          bSame = true;
        if (!bSame)
          diffs += std::string(diffs.empty() ? "" : ",") + "{\"table\":" + Q(StudioTableName(t)) + ",\"name\":" +
                   Q(r.name) + ",\"field\":" + Q(f.key) + ",\"loader\":" + Numbers(want) + ",\"studio\":" +
                   Numbers(v.n) + "}";
      }
    }
  *pAnswer = "\"records\":" + std::to_string(nRecords) + ",\"columns\":" + std::to_string(nColumns) +
             ",\"same\":" + (diffs.empty() ? "true" : "false") + ",\"differences\":[" + diffs + "]";
  return true;
}

}  // namespace

void StudioCommandsInit(void) {
  PortControlCommand("studio_status", CmdStatus);
  PortControlCommand("studio_mods", CmdMods);
  PortControlCommand("studio_open", CmdOpen);
  PortControlCommand("studio_list", CmdList);
  PortControlCommand("studio_get", CmdGet);
  PortControlCommand("studio_set", CmdSet);
  PortControlCommand("studio_create", CmdCreate);
  PortControlCommand("studio_delete", CmdDelete);
  PortControlCommand("studio_put", CmdPut);
  PortControlCommand("studio_files", CmdFiles);
  PortControlCommand("studio_import", CmdImport);
  PortControlCommand("studio_damage", CmdDamage);
  PortControlCommand("studio_make_unit", CmdMakeUnit);
  PortControlCommand("studio_raw", CmdRaw);
  PortControlCommand("studio_script", CmdScript);
  PortControlCommand("studio_undo", CmdUndo);
  PortControlCommand("studio_redo", CmdRedo);
  PortControlCommand("studio_history", CmdHistory);
  PortControlCommand("studio_save", CmdSave);
  PortControlCommand("studio_recover", CmdRecover);
  PortControlCommand("studio_push", CmdPush);
  PortControlCommand("studio_pushes", CmdPushes);
  PortControlCommand("studio_verify", CmdVerify);
}
