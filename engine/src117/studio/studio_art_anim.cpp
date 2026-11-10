// studio_art_anim.cpp -- an image's animations played as the game plays them: an animation of its
// script picked (Init, Walking, GndAttkInit and the rest), run tick by tick at Fastest's speed with play,
// pause and step, its timeline showing each tick's frame, waits and what happens (an attack, a sound,
// an order's signal), and the script's lines with the one running marked. The script is classic's
// iscript.bin or the mod's iscript.txt. Only what changes the picture is followed (frames, waits, jumps,
// turns, the flip, overlays, each overlay playing its own script). A jump on a state only a game knows
// (the target in range, the building lifted off) goes as the player picks it ("as if"), and the
// timeline shows the run had it gone the other way.
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <vector>
#include "../port/data/port_iscript.h"  // IscriptAnimCount
#include "studio_draw.h"

// What studio_art.cpp and studio_art_script.cpp give the player.
namespace studio_art {
bool DrawImageAt(ImDrawList *d, ImVec2 a, ImVec2 b, const std::string &image, int nSet, int nFacing, bool bFlip, int nColour,
                 float *pK, ImVec2 *pOff, int nExtentSet);
std::string ImageScriptOf(const std::string &image, bool *pbText);
bool ImageTurns(const std::string &image);
int ImageFrames(const std::string &image);
int ImageStart(const std::string &image);
bool ImageLoPoint(const std::string &image, int nKind, int nSet, int nFacing, int nPoint, ImVec2 *pOut);
bool GameFileRead(const char *pszName, std::vector<unsigned char> *pOut);
int ViewColour(void);
int ViewFacingStill(void);
std::string SpriteImageOf(const std::string &sprite);
std::string ScriptTextOf(const std::string &name);
}  // namespace studio_art

namespace {

using namespace studio_draw;

// ---- the opcodes ------------------------------------------------------------------------------------

// Each opcode's name and arguments as classic's iscript.bin holds them: b a byte, w a word, l a jump
// (a word offset), i an image, s a sprite, f a flingy, n a sound (words), L a byte count of sound words.
struct OpDef {
  const char *pszName;
  const char *pszArgs;
};
const OpDef kOps[0x45] = {
    {"playfram", "w"},       {"playframtile", "w"},  {"sethorpos", "b"},      {"setvertpos", "b"},     {"setpos", "bb"},
    {"wait", "b"},           {"waitrand", "bb"},     {"goto", "l"},           {"imgol", "ibb"},        {"imgul", "ibb"},
    {"imgolorig", "i"},      {"switchul", "i"},      {nullptr, nullptr},      {"imgoluselo", "ibb"},   {"imguluselo", "ibb"},
    {"sprol", "sbb"},        {"highsprol", "sbb"},   {"lowsprul", "sbb"},     {"uflunstable", "f"},    {"spruluselo", "sbb"},
    {"sprul", "sbb"},        {"sproluselo", "sb"},   {"end", ""},             {"setflipstate", "b"},   {"playsnd", "n"},
    {"playsndrand", "L"},    {"playsndbtwn", "nn"},  {"domissiledmg", ""},    {"attackmelee", "L"},    {"followmaingraphic", ""},
    {"randcondjmp", "bl"},   {"turnccwise", "b"},    {"turncwise", "b"},      {"turn1cwise", ""},      {"turnrand", "b"},
    {"setspawnframe", "b"},  {"sigorder", "b"},      {"attackwith", "b"},     {"attack", ""},          {"castspell", ""},
    {"useweapon", "b"},      {"move", "b"},          {"gotorepeatattk", ""},  {"engframe", "b"},       {"engset", "b"},
    {"__2d", ""},            {"nobrkcodestart", ""}, {"nobrkcodeend", ""},    {"ignorerest", ""},      {"attkshiftproj", "b"},
    {"tmprmgraphicstart", ""}, {"tmprmgraphicend", ""}, {"setfldirect", "b"}, {"call", "l"},           {"return", ""},
    {"setflspeed", "w"},     {"creategasoverlays", "b"}, {"pwrupcondjmp", "l"}, {"trgtrangecondjmp", "wl"}, {"trgtarccondjmp", "wwl"},
    {"curdirectcondjmp", "wwl"}, {"imgulnextid", "bb"}, {nullptr, nullptr},   {"liftoffcondjmp", "l"}, {"warpoverlay", "w"},
    {"orderdone", "b"},      {"grdsprol", "sbb"},    {nullptr, nullptr},      {"dogrddamage", ""},
};

// The animations in a header's order (classic's names).
const char *const kAnims[] = {
    "Init",          "Death",         "GndAttkInit",   "AirAttkInit",   "Unused1",     "GndAttkRpt",  "AirAttkRpt",
    "CastSpell",     "GndAttkToIdle", "AirAttkToIdle", "Unused2",       "Walking",     "WalkingToIdle", "SpecialState1",
    "SpecialState2", "AlmostBuilt",   "Built",         "Landing",       "LiftOff",     "IsWorking",   "WorkingToIdle",
    "WarpIn",        "Unused3",       "StarEditInit",  "Disable",       "Burrow",      "UnBurrow",    "Enable",
};
const int kAnimCount = sizeof kAnims / sizeof kAnims[0];

int OpByName(const std::string &name) {
  for (int i = 0; i < 0x45; ++i)
    if (kOps[i].pszName && Lower(name) == kOps[i].pszName)
      return i;
  return -1;
}

// One instruction, from either form: its opcode, its numbers (a name of the mod's kept as text), the
// instruction it jumps to and the one after it, and its line as the editor would show it.
struct Op {
  int code = -1;
  std::vector<long> n;
  std::vector<std::string> s;  // an argument written as a name
  int jump = -1;
  int next = -1;
  std::string text;
};

struct Program {
  std::vector<Op> ops;
  std::vector<std::pair<std::string, int>> anims;  // name, first op
  std::string why;                                 // why there is none
};

// ---- a script read ----------------------------------------------------------------------------------

std::string Trim(std::string s) {
  while (!s.empty() && isspace(static_cast<unsigned char>(s.back())))
    s.pop_back();
  size_t i = 0;
  while (i < s.size() && isspace(static_cast<unsigned char>(s[i])))
    ++i;
  return s.substr(i);
}

// Classic's script nScript: its header's animations and the code each reaches, decoded from the
// offsets on, every jump followed.
bool ReadClassic(int nScript, Program *pOut) {
  static std::vector<unsigned char> s_bin;
  static bool s_bTried = false;
  if (!s_bTried) {
    s_bTried = true;
    studio_art::GameFileRead("scripts\\iscript.bin", &s_bin);
  }
  const std::vector<unsigned char> &b = s_bin;
  auto w = [&](size_t at) { return at + 1 < b.size() ? static_cast<size_t>(b[at] | b[at + 1] << 8) : 0u; };
  size_t at = w(0), nHead = 0;
  for (int i = 0; i < 4000 && at + 3 < b.size(); ++i, at += 4) {
    if (w(at) == 0xFFFF)
      break;
    if (static_cast<int>(w(at)) == nScript) {
      nHead = w(at + 2);
      break;
    }
  }
  if (nHead == 0 || nHead + 8 > b.size() || memcmp(&b[nHead], "SCPE", 4) != 0) {
    pOut->why = b.empty() ? "the classic iscript.bin does not read" : "classic has no script " + std::to_string(nScript);
    return false;
  }
  const int nAnims = std::min(kAnimCount, std::max(0, IscriptAnimCount(b[nHead + 4])));
  std::map<size_t, int> index;
  std::vector<size_t> todo;
  auto want = [&](size_t off) -> int {
    if (off == 0 || off >= b.size())
      return -1;
    auto it = index.find(off);
    if (it != index.end())
      return it->second;
    const int n = static_cast<int>(pOut->ops.size());
    index[off] = n;
    pOut->ops.push_back(Op());
    todo.push_back(off);
    return n;
  };
  for (int i = 0; i < nAnims; ++i) {
    const size_t off = w(nHead + 8 + 2 * i);
    if (off != 0)
      pOut->anims.push_back({kAnims[i], want(off)});
  }
  while (!todo.empty()) {
    const size_t pc = todo.back();
    todo.pop_back();
    const int nIndex = index[pc];
    Op op;
    const int code = b[pc];
    char sz[32];
    snprintf(sz, sizeof sz, "%04X  ", static_cast<unsigned>(pc));
    op.text = sz;
    if (code >= 0x45 || kOps[code].pszName == nullptr) {
      op.text += "opcode " + std::to_string(code) + ", not known";
      pOut->ops[static_cast<size_t>(nIndex)] = op;
      continue;
    }
    op.code = code;
    op.text += kOps[code].pszName;
    size_t p = pc + 1;
    for (const char *a = kOps[code].pszArgs; *a; ++a) {
      if (*a == 'b') {
        op.n.push_back(p < b.size() ? b[p] : 0);
        p += 1;
      } else if (*a == 'L') {
        const int n = p < b.size() ? b[p] : 0;
        op.n.push_back(n);
        p += 1;
        for (int k = 0; k < n; ++k, p += 2)
          op.n.push_back(static_cast<long>(w(p)));
      } else {
        op.n.push_back(static_cast<long>(w(p)));
        p += 2;
      }
    }
    for (size_t k = 0; k < op.n.size(); ++k) {
      if (strchr(kOps[code].pszArgs, 'l') && k == strlen(kOps[code].pszArgs) - 1) {
        snprintf(sz, sizeof sz, " %04X", static_cast<unsigned>(op.n[k]));
        op.text += sz;
      } else {
        op.text += " " + std::to_string(op.n[k]);
      }
    }
    if (strchr(kOps[code].pszArgs, 'l'))
      op.jump = want(static_cast<size_t>(op.n.back()));
    if (code != 0x07 && code != 0x16 && code != 0x36)  // goto, end and return go nowhere next
      op.next = want(p);
    pOut->ops[static_cast<size_t>(nIndex)] = op;
  }
  return true;
}

// A script of the mod's text by its Name: the file's code, its labels, and the header's animations.
bool ReadText(const std::string &name, Program *pOut) {
  const std::string text = studio_art::ScriptTextOf(name);
  if (text.empty()) {
    pOut->why = "no script named " + name + " in iscript.txt";
    return false;
  }
  std::map<std::string, int> labels;
  std::vector<std::pair<int, std::string>> jumps;  // op, label
  std::vector<std::pair<std::string, std::string>> anims;
  std::string header;
  bool bIn = false;
  size_t at = 0;
  int nLine = 0;
  while (at <= text.size()) {
    const size_t eol = std::min(text.find('\n', at), text.size());
    std::string line = text.substr(at, eol - at);
    ++nLine;
    at = eol + 1;
    const size_t hash = line.find('#');
    if (hash != std::string::npos)
      line = line.substr(0, hash);
    line = Trim(line);
    if (line.compare(0, 12, ".headerstart") == 0) {
      bIn = true;
      header.clear();
    } else if (line.compare(0, 10, ".headerend") == 0) {
      bIn = false;
    } else if (bIn && !line.empty()) {
      const size_t sp = line.find_first_of(" \t");
      const std::string k = line.substr(0, sp), v = sp == std::string::npos ? std::string() : Trim(line.substr(sp));
      if (k == "Name")
        header = v;
      else if (header == name && k != "Type")
        anims.push_back({k, v});
    } else if (!line.empty()) {
      // labels, then an instruction
      size_t colon;
      while ((colon = line.find(':')) != std::string::npos && line.find_first_of(" \t") > colon) {
        labels[line.substr(0, colon)] = static_cast<int>(pOut->ops.size());
        line = Trim(line.substr(colon + 1));
      }
      if (line.empty())
        continue;
      std::vector<std::string> tok;
      size_t p = 0;
      while (p < line.size()) {
        const size_t e = line.find_first_of(" \t", p);
        tok.push_back(line.substr(p, e == std::string::npos ? std::string::npos : e - p));
        p = e == std::string::npos ? line.size() : line.find_first_not_of(" \t", e);
        if (p == std::string::npos)
          break;
      }
      Op op;
      op.code = OpByName(tok[0]);
      op.text = std::to_string(nLine) + "  " + line;
      if (op.code >= 0) {
        const char *pszArgs = kOps[op.code].pszArgs;
        for (size_t k = 1; k < tok.size(); ++k) {
          char *pEnd = nullptr;
          const long v = strtol(tok[k].c_str(), &pEnd, 0);
          const bool bNum = pEnd && *pEnd == '\0' && !tok[k].empty();
          op.n.push_back(bNum ? v : 0);
          op.s.push_back(bNum ? std::string() : tok[k]);
        }
        if (strchr(pszArgs, 'l') && !tok.empty())
          jumps.push_back({static_cast<int>(pOut->ops.size()), tok.back()});
      }
      pOut->ops.push_back(op);
    }
    if (eol >= text.size())
      break;
  }
  for (size_t i = 0; i < pOut->ops.size(); ++i) {
    Op &op = pOut->ops[i];
    if (op.code != 0x07 && op.code != 0x16 && op.code != 0x36 && i + 1 < pOut->ops.size())
      op.next = static_cast<int>(i + 1);
  }
  for (const auto &j : jumps) {
    auto it = labels.find(j.second);
    pOut->ops[static_cast<size_t>(j.first)].jump = it == labels.end() ? -1 : it->second;
  }
  for (const auto &a : anims) {
    auto it = labels.find(a.second);
    if (a.second != "[NONE]" && it != labels.end())
      pOut->anims.push_back({a.first, it->second});
  }
  if (pOut->anims.empty())
    pOut->why = "the script " + name + " names no animation that is there";
  return !pOut->anims.empty();
}

// ---- an animation run --------------------------------------------------------------------------------

struct Overlay {
  std::string image;
  int x = 0, y = 0;
  bool bAbove = true;
  int nTick = 0;  // the tick it was made at
};

// ---- what the conditional jumps test ------------------------------------------------------------------

// The state a script's conditional jumps test, which a picture has no game to know: the player picks
// it ("as if"). A condition's first choice is the one taken when none is picked.
enum { K_RANDOM, K_CARRIED, K_RANGE, K_ARC, K_FACING, K_LIFTED, K_COUNT };
struct CondDef {
  const char *pszKey;   // its name in studio_art_view's as_if
  int code;             // the opcode
  const char *pszWhat;  // the state, in words
  int n;                // its choices
  const char *apszChoice[3];
};
const CondDef kConds[K_COUNT] = {
    {"random", 0x1E, "Random jumps", 3, {"By chance", "Always", "Never"}},
    {"carried", 0x39, "It is", 2, {"On its own", "Carried"}},
    {"range", 0x3A, "The target", 2, {"In range", "Out of range"}},
    {"arc", 0x3B, "The target", 3, {"Ahead", "In the arc", "Outside it"}},
    {"facing", 0x3C, "Its facing", 3, {"As it faces", "In the arc", "Outside it"}},
    {"lifted", 0x3F, "The building", 2, {"Landed", "Lifted off"}},
};

int CondOf(int code) {
  for (int k = 0; k < K_COUNT; ++k)
    if (kConds[k].code == code)
      return k;
  return -1;
}

struct AsIf {
  int choice[K_COUNT] = {0, 0, 0, 0, 0, 0};
  bool operator!=(const AsIf &o) const { return memcmp(choice, o.choice, sizeof choice) != 0; }
};

// A conditional jump the run met: its tick, its instruction, its condition, and whether it jumped.
struct Test {
  int nTick = 0;
  int nOp = -1;
  int nKind = 0;
  bool bTaken = false;
};

// How far apart two directions are, 256 a turn (the game's get_angle).
int AngleApart(int a, int b) {
  const int d = (b - a) & 0xFF;
  return d > 0x80 ? 0x100 - d : d;
}

struct Tick {
  int nSet = 0;
  int nFacing = 0;
  bool bFlip = false;
  int nOp = -1;            // the instruction it stopped at
  std::string events;      // a attack, s sound, o an order's signal, c a spell
  std::string words;       // what happened, in words
  int nOverlays = 0;       // the overlays made by then (the run's first n)
  bool bGone = false;      // the image's script has ended and the game has taken it away; its overlays play on
};

struct Run {
  std::vector<Tick> ticks;
  std::vector<Overlay> overlays;
  std::vector<Test> tests;
  std::vector<int> firstTick;  // each instruction's first tick, -1 never
  int nLoop = -1;              // the tick it goes on from at its end, -1 when it ends
  bool bEnded = false;
  std::string notes;
};

const int kMostTicks = 240;

std::string ImageNamed(const Op &op, size_t k, int t) {
  if (k < op.s.size() && !op.s[k].empty())
    return t == ST_SPRITES ? studio_art::SpriteImageOf(op.s[k]) : op.s[k];
  if (k >= op.n.size())
    return std::string();
  const std::string name = StudioRefName(t, op.n[k]);
  return t == ST_SPRITES ? studio_art::SpriteImageOf(name) : name;
}

// The image's animation from its first instruction, at a facing, a tick at a time: each tick's frame
// set, facing and flip, and what it set off, until it ends, comes back to a tick it was at, or runs
// long. Its conditional jumps go as the choices say.
Run RunAnim(const Program &prog, const std::string &image, int nStart, int nFacing, int nFirstSet, const AsIf &as) {
  Run r;
  r.firstTick.assign(prog.ops.size(), -1);
  int pc = nStart, nSet = nFirstSet, nWait = 0;
  bool bFlip = false;
  std::vector<int> stack;
  unsigned seed = 1;
  auto rnd = [&]() {
    seed = seed * 22695477u + 1u;
    return static_cast<int>((seed >> 16) & 0x7FFF);
  };
  std::map<std::string, int> seen;  // a tick's state at its start, its tick
  std::set<std::string> noted;
  auto note = [&](const std::string &s) {
    if (noted.insert(s).second)
      r.notes += (r.notes.empty() ? "" : "; ") + s;
  };
  for (int t = 0; t < kMostTicks; ++t) {
    Tick k;
    if (nWait == 0 && !r.bEnded) {
      char sz[64];
      snprintf(sz, sizeof sz, "%d/%d/%d/%d/%d/%d", pc, nSet, nFacing, bFlip ? 1 : 0, stack.empty() ? -1 : stack.back(),
               static_cast<int>(r.overlays.size()));
      auto it = seen.find(sz);
      if (it != seen.end()) {
        r.nLoop = it->second;
        break;
      }
      seen[sz] = t;
    }
    if (nWait > 0) {
      --nWait;
    } else {
      for (int nStep = 0; nStep < 400 && !r.bEnded; ++nStep) {
        if (pc < 0 || pc >= static_cast<int>(prog.ops.size())) {
          r.bEnded = true;
          break;
        }
        const Op &op = prog.ops[static_cast<size_t>(pc)];
        if (r.firstTick[static_cast<size_t>(pc)] < 0)
          r.firstTick[static_cast<size_t>(pc)] = t;
        k.nOp = pc;
        auto arg = [&](size_t i) { return i < op.n.size() ? op.n[i] : 0L; };
        int nNext = op.next;
        bool bStop = false;
        switch (op.code) {
        case 0x00: case 0x01: nSet = static_cast<int>(arg(0)); break;
        case 0x05: nWait = std::max(0, static_cast<int>(arg(0)) - 1); bStop = true; break;
        case 0x06: {
          const int lo = static_cast<int>(arg(0)), hi = std::max(lo, static_cast<int>(arg(1)));
          nWait = std::max(0, lo + rnd() % (hi - lo + 1) - 1);
          bStop = true;
          break;
        }
        case 0x07: nNext = op.jump; break;
        case 0x16: r.bEnded = true; bStop = true; break;
        case 0x17: bFlip = arg(0) != 0; break;
        case 0x1F: nFacing = (nFacing - static_cast<int>(arg(0)) + 64) & 31; break;
        case 0x20: nFacing = (nFacing + static_cast<int>(arg(0))) & 31; break;
        case 0x21: nFacing = (nFacing + 1) & 31; break;
        case 0x22: nFacing = (nFacing + (rnd() % 3 - 1) * static_cast<int>(arg(0)) + 64) & 31; break;
        case 0x34: nFacing = static_cast<int>(arg(0)) & 31; break;  // setfldirect: the unit faces a set way
        case 0x30:  // ignorerest: it stays here until the order moves it on
          nNext = pc;
          bStop = true;
          break;
        case 0x08: case 0x09: case 0x0D: case 0x0E: {
          Overlay o;
          o.image = ImageNamed(op, 0, ST_IMAGES);
          o.nTick = t;
          o.bAbove = op.code == 0x08 || op.code == 0x0D;
          if (op.code >= 0x0D) {
            // at a point of the image's .lo file of a kind, read at its frame now
            ImVec2 at;
            if (!studio_art::ImageLoPoint(image, static_cast<int>(arg(1)), nSet, nFacing, static_cast<signed char>(arg(2)), &at))
              note("an overlay point no .lo file gives is the centre");
            o.x = static_cast<int>(at.x);
            o.y = static_cast<int>(at.y);
          } else {
            o.x = static_cast<signed char>(arg(1));
            o.y = static_cast<signed char>(arg(2));
          }
          if (!o.image.empty())
            r.overlays.push_back(o);
          k.words += "overlay " + o.image + "  ";
          break;
        }
        case 0x0A: case 0x0B: {
          Overlay o;
          o.image = ImageNamed(op, 0, ST_IMAGES);
          o.nTick = t;
          o.bAbove = op.code == 0x0A;
          if (!o.image.empty())
            r.overlays.push_back(o);
          k.words += "overlay " + o.image + "  ";
          break;
        }
        case 0x0F: case 0x10: case 0x11: case 0x13: case 0x14: case 0x15: case 0x42: {
          Overlay o;
          o.image = ImageNamed(op, 0, ST_SPRITES);
          o.nTick = t;
          if (op.code == 0x15) {  // sproluselo: the first point of the .lo file of a kind
            ImVec2 at;
            if (!studio_art::ImageLoPoint(image, static_cast<int>(arg(1)), nSet, nFacing, 0, &at))
              note("an overlay point no .lo file gives is the centre");
            o.x = static_cast<int>(at.x);
            o.y = static_cast<int>(at.y);
          } else {
            o.x = static_cast<signed char>(arg(1));
            o.y = static_cast<signed char>(arg(2));
          }
          o.bAbove = op.code == 0x0F || op.code == 0x10 || op.code == 0x15 || op.code == 0x42;
          if (!o.image.empty())
            r.overlays.push_back(o);
          k.words += "sprite " + o.image + "  ";
          break;
        }
        case 0x18: case 0x19: case 0x1A: k.events += 's'; k.words += "sound  "; break;
        case 0x1B: case 0x1C: case 0x25: case 0x26: case 0x28: case 0x44: k.events += 'a'; k.words += "attack  "; break;
        case 0x27: k.events += 'c'; k.words += "spell  "; break;
        case 0x24: case 0x41: k.events += 'o'; k.words += "signal to its order  "; break;
        case 0x1E: case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3F: {
          const int nKind = CondOf(op.code);
          const int c = as.choice[nKind];
          bool bTaken = false;
          switch (nKind) {
          case K_RANDOM: bTaken = c == 1 || (c == 0 && (rnd() & 0xFF) <= arg(0)); break;
          case K_CARRIED: case K_LIFTED: bTaken = c == 1; break;
          case K_RANGE: bTaken = c == 0; break;
          default:  // an arc of directions: the target's taken as straight ahead, or the unit's facing
            bTaken = c == 1 || (c == 0 && AngleApart(nFacing * 8, static_cast<int>(arg(0)) & 0xFF) < arg(1));
            break;
          }
          Test tst;
          tst.nTick = t;
          tst.nOp = pc;
          tst.nKind = nKind;
          tst.bTaken = bTaken;
          r.tests.push_back(tst);
          if (bTaken)
            nNext = op.jump;
          break;
        }
        case 0x35: stack.push_back(op.next); nNext = op.jump; break;
        case 0x36:
          if (stack.empty()) {
            r.bEnded = true;
            bStop = true;
          } else {
            nNext = stack.back();
            stack.pop_back();
          }
          break;
        case -1:
          note("a line that is no instruction stops it");
          r.bEnded = true;
          bStop = true;
          break;
        default: break;  // movement, engines, the order's flow: nothing the picture shows
        }
        if (bStop) {
          pc = nNext;
          break;
        }
        pc = nNext;
      }
    }
    k.nSet = nSet;
    k.nFacing = nFacing;
    k.bFlip = bFlip;
    k.nOverlays = static_cast<int>(r.overlays.size());
    r.ticks.push_back(k);
    if (r.bEnded)
      break;
  }
  if (!r.bEnded && r.nLoop < 0 && static_cast<int>(r.ticks.size()) >= kMostTicks)
    note("shown for its first " + std::to_string(kMostTicks) + " ticks");
  return r;
}

// The frame set an animation starts from: where Init leaves the image, as the game's image has
// played its Init before any other animation; the script's first frame set for Init itself.
int StartingSet(const Program &prog, const std::string &image, const std::string &anim, int nFacing, const AsIf &as) {
  const int nFirst = studio_art::ImageStart(image);
  if (anim == "Init")
    return nFirst;
  for (const auto &a : prog.anims)
    if (a.first == "Init") {
      const Run init = RunAnim(prog, image, a.second, nFacing, nFirst, as);
      return init.ticks.empty() ? nFirst : init.ticks.back().nSet;
    }
  return nFirst;
}

// An overlay's own script from its Init, run as the image's is. Scripts are read once while the mod
// is unchanged.
Run OverlayRun(const std::string &image, int nFacing, const AsIf &as) {
  static std::map<std::string, Program> s_progs;
  static unsigned s_nChange = ~0u;
  if (s_nChange != StudioChange()) {
    s_progs.clear();
    s_nChange = StudioChange();
  }
  bool bText = false;
  const std::string script = studio_art::ImageScriptOf(image, &bText);
  if (script.empty())
    return Run();
  const std::string key = (bText ? "t:" : "c:") + script;
  auto it = s_progs.find(key);
  if (it == s_progs.end()) {
    Program prog;
    if (!(bText ? ReadText(script, &prog) : ReadClassic(atoi(script.c_str()), &prog)))
      prog.anims.clear();
    it = s_progs.emplace(key, prog).first;
  }
  for (const auto &a : it->second.anims)
    if (a.first == "Init")
      return RunAnim(it->second, image, a.second, nFacing, studio_art::ImageStart(image), as);
  return Run();
}

// An overlay's frame set at the image's tick t from its own run (-1 its first when it has none), or
// false once its script has ended, when the game takes it away.
bool OverlaySet(const Overlay &ov, const Run &run, int t, int *pSet) {
  const int nAge = std::max(0, t - ov.nTick), n = static_cast<int>(run.ticks.size());
  *pSet = -1;
  if (n == 0)
    return true;
  if (run.bEnded && nAge >= n - 1)
    return false;
  if (nAge < n)
    *pSet = run.ticks[static_cast<size_t>(nAge)].nSet;
  else if (run.nLoop >= 0)
    *pSet = run.ticks[static_cast<size_t>(run.nLoop + (nAge - run.nLoop) % std::max(1, n - run.nLoop))].nSet;
  else
    *pSet = run.ticks.back().nSet;
  return true;
}

// Every overlay the picture shows, the image's own and those its overlays make in turn: where it is
// from the image's origin and the tick it is made at, both counted from the image's start, its own
// run, and the one that made it (-1 the image). An overlay the image makes is shown once the image's
// run has made it (nOwn its place in run.overlays); one an overlay makes, while that overlay is.
struct Shown {
  Overlay ov;
  Run run;
  int nParent = -1;
  int nOwn = -1;
  int nDepth = 0;
};

const int kMostShown = 48;  // overlays an overlay makes, made again and again, stop at this
const int kMostDepth = 4;

std::vector<Shown> ShownOf(const Run &run, int nFacing, const AsIf &as) {
  std::vector<Shown> all;
  for (size_t i = 0; i < run.overlays.size() && static_cast<int>(all.size()) < kMostShown; ++i) {
    Shown s;
    s.ov = run.overlays[i];
    s.nOwn = static_cast<int>(i);
    s.run = OverlayRun(s.ov.image, nFacing, as);
    all.push_back(s);
  }
  for (size_t i = 0; i < all.size(); ++i) {
    if (all[i].nDepth + 1 >= kMostDepth)
      continue;
    const Shown parent = all[i];
    for (const Overlay &c : parent.run.overlays) {
      if (static_cast<int>(all.size()) >= kMostShown)
        break;
      Shown s;
      s.ov = c;
      s.ov.x += parent.ov.x;
      s.ov.y += parent.ov.y;
      s.ov.nTick += parent.ov.nTick;
      s.nParent = static_cast<int>(i);
      s.nDepth = parent.nDepth + 1;
      s.run = OverlayRun(c.image, nFacing, as);
      all.push_back(s);
    }
  }
  return all;
}

// Whether the shown overlay i is there at the image's tick nNow (nOwnMade the image's overlays made by
// then), and its frame set.
// An overlay is an image of the sprite: once made it stays until its own script ends, whether or not
// the one that made it is still there.
bool ShownAt(const std::vector<Shown> &all, int i, int nNow, int nOwnMade, int *pSet) {
  const Shown &s = all[static_cast<size_t>(i)];
  int nRoot = i;
  while (all[static_cast<size_t>(nRoot)].nParent >= 0)
    nRoot = all[static_cast<size_t>(nRoot)].nParent;
  if (all[static_cast<size_t>(nRoot)].nOwn >= nOwnMade || nNow < s.ov.nTick)
    return false;
  return OverlaySet(s.ov, s.run, nNow, pSet);
}

const int kAfterLooping = 48;  // ticks shown of an overlay that never ends, once the image has

// An image whose script ends leaves its overlays playing: the run goes on, the image gone, until the
// last of them ends.
void PlayOn(Run *pRun, const std::vector<Shown> &shown) {
  if (!pRun->bEnded || pRun->ticks.empty())
    return;
  int nLast = static_cast<int>(pRun->ticks.size());
  for (const Shown &s : shown)
    if (!s.run.ticks.empty())
      nLast = std::max(nLast, s.ov.nTick + (s.run.bEnded ? static_cast<int>(s.run.ticks.size()) : kAfterLooping));
  nLast = std::min(nLast, 2 * kMostTicks);
  Tick gone = pRun->ticks.back();
  gone.bGone = true;
  gone.nOp = -1;
  gone.events.clear();
  gone.words = "the image is gone; its overlays play on";
  while (static_cast<int>(pRun->ticks.size()) < nLast)
    pRun->ticks.push_back(gone);
}

// The run had the conditions it tested gone other ways, from the tick the last of them parts from the
// run it branches from.
struct Lane {
  AsIf alt;          // the choices the lane's run went by
  int nFrom = 0;
  std::string name;
  std::string what;  // the change, in words, for its tooltip
  Run run;
};

const int kMostLanes = 6;

bool SameRun(const Run &a, const Run &b) {
  if (a.ticks.size() != b.ticks.size() || a.nLoop != b.nLoop || a.bEnded != b.bEnded)
    return false;
  for (size_t i = 0; i < a.ticks.size(); ++i)
    if (a.ticks[i].nSet != b.ticks[i].nSet || a.ticks[i].nFacing != b.ticks[i].nFacing || a.ticks[i].bFlip != b.ticks[i].bFlip ||
        a.ticks[i].nOverlays != b.ticks[i].nOverlays)
      return false;
  return true;
}

// The lanes of a run: a run for each other choice of each condition it tests, then for the conditions
// only a lane's run comes to, each a run unlike any made already.
std::vector<Lane> LanesOf(const Program &prog, const std::string &image, int nStart, int nFacing, int nFirstSet, const AsIf &as,
                          const Run &run) {
  std::vector<Lane> lanes;
  lanes.reserve(kMostLanes);
  std::vector<Run> seen = {run};
  struct From {
    Run run;
    AsIf as;
    std::string name, what;
  };
  std::vector<From> froms = {{run, as, "if", ""}};
  for (size_t fi = 0; fi < froms.size() && static_cast<int>(lanes.size()) < kMostLanes; ++fi) {
    const From f = froms[fi];
    const size_t nBefore = lanes.size();
    bool abDone[K_COUNT] = {};
    for (const Test &tst : f.run.tests) {
      if (abDone[tst.nKind])
        continue;
      abDone[tst.nKind] = true;
      if (fi > 0) {  // a condition only this lane's run comes to
        bool bThere = false;
        for (const Test &t0 : run.tests)
          bThere = bThere || t0.nKind == tst.nKind;
        if (bThere || f.as.choice[tst.nKind] != as.choice[tst.nKind])
          continue;
      }
      const CondDef &cd = kConds[tst.nKind];
      for (int c = 0; c < cd.n && static_cast<int>(lanes.size()) < kMostLanes; ++c) {
        if (c == f.as.choice[tst.nKind])
          continue;
        Lane l;
        l.alt = f.as;
        l.alt.choice[tst.nKind] = c;
        l.run = RunAnim(prog, image, nStart, nFacing, nFirstSet, l.alt);
        bool bSeen = false;
        for (const Run &r : seen)
          bSeen = bSeen || SameRun(r, l.run);
        if (bSeen)
          continue;
        l.nFrom = tst.nTick;
        l.name = f.name + (fi > 0 ? ", " : " ") + cd.apszChoice[c];
        l.what = f.what + (f.what.empty() ? "" : ", and ") + cd.pszWhat + " " + Lower(cd.apszChoice[c]);
        seen.push_back(l.run);
        lanes.push_back(l);
      }
    }
    for (size_t li = nBefore; li < lanes.size(); ++li)
      froms.push_back({lanes[li].run, lanes[li].alt, lanes[li].name, lanes[li].what});
  }
  return lanes;
}

// ---- the player ---------------------------------------------------------------------------------------

struct Player {
  bool bHeld = false;  // a tick or a pause set from outside, kept through the next image shown
  std::string image;
  std::string script;  // what the program was read from
  unsigned nChange = ~0u;
  Program prog;
  bool bRead = false;
  int nAnim = 0;
  std::string animName = "Init";
  int nRunFacing = -1;
  int nRunAnim = -1;
  AsIf asIf;     // the choices for the conditional jumps
  AsIf runAsIf;  // the ones the run was made with
  Run run;
  std::vector<Shown> shown;  // the overlays, the image's and theirs, with their runs
  std::vector<Lane> lanes;
  int nTick = 0;
  int nAbs = 0;    // ticks played since it started, going on past a loop's wrap (overlays age by it)
  int nAbsAt = 0;  // the tick nAbs was last kept with: a tick set any other way starts it again
  bool bPlaying = true;
  double dAcc = 0.0;
  int nHold = 0;  // ticks held on an animation's end before it starts again
  float fK = 0.0f;  // the picture's size, kept while one animation plays
  ImVec2 off;
} g_player;

const double kTickS = 42.0 / 1000.0;  // Fastest: a game frame, and a script's tick, each 42 ms

void Prepare(const std::string &image) {
  Player &p = g_player;
  bool bText = false;
  const std::string script = studio_art::ImageScriptOf(image, &bText);
  const std::string key = (bText ? "t:" : "c:") + script;
  if (p.image != image || p.script != key || p.nChange != StudioChange()) {
    const bool bNewImage = p.image != image;
    p.image = image;
    p.script = key;
    p.nChange = StudioChange();
    p.prog = Program();
    p.bRead = (bText ? ReadText(script, &p.prog) : ReadClassic(atoi(script.c_str()), &p.prog)) && !p.prog.anims.empty();
    if (p.prog.anims.empty() && p.prog.why.empty())
      p.prog.why = "its script has no animations";
    p.nRunAnim = -1;
    if (bNewImage && !p.bHeld) {
      p.nTick = 0;
      p.bPlaying = true;
    }
    p.bHeld = false;
    p.nRunFacing = -1;
    // keep the animation picked by its name where the new script has it
    p.nAnim = 0;
    for (size_t i = 0; i < p.prog.anims.size(); ++i)
      if (p.prog.anims[i].first == p.animName)
        p.nAnim = static_cast<int>(i);
  }
  const int nFacing = studio_art::ViewFacingStill();
  if (p.bRead && (p.nRunAnim != p.nAnim || p.nRunFacing != nFacing || p.runAsIf != p.asIf)) {
    const bool bSameAnim = p.nRunAnim == p.nAnim && p.nRunFacing == nFacing;
    p.nRunAnim = p.nAnim;
    p.nRunFacing = nFacing;
    p.runAsIf = p.asIf;
    const int nStart = p.prog.anims[static_cast<size_t>(p.nAnim)].second;
    const int nFirstSet = StartingSet(p.prog, image, p.prog.anims[static_cast<size_t>(p.nAnim)].first, nFacing, p.asIf);
    p.run = RunAnim(p.prog, image, nStart, nFacing, nFirstSet, p.asIf);
    p.shown = ShownOf(p.run, nFacing, p.asIf);
    PlayOn(&p.run, p.shown);
    p.lanes = LanesOf(p.prog, image, nStart, nFacing, nFirstSet, p.asIf, p.run);
    p.nTick = std::min(p.nTick, std::max(0, static_cast<int>(p.run.ticks.size()) - 1));
    if (!bSameAnim)
      p.fK = 0.0f;
  }
}

void SyncAbs(void) {
  Player &p = g_player;
  if (p.nAbsAt != p.nTick)
    p.nAbs = p.nAbsAt = p.nTick;
}

void Advance(void) {
  Player &p = g_player;
  SyncAbs();
  if (p.run.ticks.empty())
    return;
  if (!p.bPlaying) {
    p.dAcc = 0.0;
    return;
  }
  p.dAcc += ImGui::GetIO().DeltaTime;
  while (p.dAcc >= kTickS) {
    p.dAcc -= kTickS;
    if (p.nTick + 1 < static_cast<int>(p.run.ticks.size())) {
      ++p.nTick;
      ++p.nAbs;
    } else if (p.run.nLoop >= 0) {
      p.nTick = p.run.nLoop;
      ++p.nAbs;
    } else if (++p.nHold >= 24) {  // an animation that ends holds its last frame a second, then starts again
      p.nHold = 0;
      p.nTick = p.nAbs = 0;
    }
    p.nAbsAt = p.nTick;
  }
}

const char *EventWord(char c) {
  switch (c) {
  case 'a': return "attack";
  case 's': return "sound";
  case 'c': return "spell";
  default: return "order";
  }
}

ImU32 EventColour(char c) {
  switch (c) {
  case 'a': return C(TK_BAD);
  case 's': return C(TK_SEL_LINE);
  case 'c': return C(TK_CLAUDE);
  default: return C(TK_WARN);
  }
}

// A conditional jump's mark: a small diamond.
void Diamond(ImDrawList *d, ImVec2 c, float r, ImU32 col) {
  d->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), col);
}

// One lane of the timeline: a cell a tick from nFrom, its frame set written where it changes, a mark
// for each event and each conditional jump (bright where it jumped), the tick it goes on from at its end.
void DrawLane(ImDrawList *d, const Run &run, int nFrom, int nFirst, int nShown, float cx, float y, float h, float cw) {
  const int n = static_cast<int>(run.ticks.size());
  int nLastSet = -1;
  float labelEnd = cx;  // a frame's number is written only where the last one ended
  for (int i = std::max(nFirst, nFrom); i < n && i < nFirst + nShown; ++i) {
    const Tick &k = run.ticks[static_cast<size_t>(i)];
    const float x = cx + (i - nFirst) * cw;
    if (k.bGone) {  // the image gone, its overlays still playing: shaded, no frame sets
      d->AddRectFilled(ImVec2(x, y + P(2.0f)), ImVec2(x + cw, y + h - P(2.0f)), C(TK_LINE));
      continue;
    }
    if (k.nSet != nLastSet) {
      d->AddLine(ImVec2(x, y + P(2.0f)), ImVec2(x, y + h - P(2.0f)), C(TK_LINE2));
      if (x >= labelEnd)
        labelEnd = x + P(2.0f) + T(d, STUDIO_FONT_MONO, 9.0f, x + P(2.0f), y + P(3.0f), C(TK_INK2), std::to_string(k.nSet)) + P(3.0f);
      nLastSet = k.nSet;
    }
    const size_t nDots = h > P(30.0f) ? 3 : 1;
    for (size_t e = 0; e < k.events.size() && e < nDots; ++e)
      d->AddCircleFilled(ImVec2(x + cw / 2.0f, y + h - P(7.0f) - e * P(8.0f)), P(3.0f), EventColour(k.events[e]));
    for (const Test &tst : run.tests)
      if (tst.nTick == i)
        Diamond(d, ImVec2(x + cw / 2.0f, y + h * 0.55f), P(4.0f), tst.bTaken ? C(TK_GOOD) : C(TK_INK3));
    if (i == run.nLoop)
      StudioGlyph(d, "again", ImVec2(x, y + h * 0.3f), P(12.0f), C(TK_GOOD));
  }
}

// The timeline: the run as picked, and under it a lane for each condition it tested, the run had that
// condition gone the other way from the tick it was first tested. The tick shown is framed; a click
// goes to a tick, and a click on a lane picks its way. Returns its height.
float Timeline(ImVec2 o, float W) {
  Player &p = g_player;
  ImDrawList *d = ImGui::GetWindowDrawList();
  const int n = static_cast<int>(p.run.ticks.size());
  int nAll = n;
  for (const auto &l : p.lanes)
    nAll = std::max(nAll, static_cast<int>(l.run.ticks.size()));
  const float gutter = p.lanes.empty() ? 0.0f : P(124.0f);  // the lanes' names
  const float cx = o.x + gutter, cwAll = W - gutter;
  const float h = P(46.0f), lh = P(26.0f), gap = P(4.0f);
  const float cw = std::max(P(7.0f), std::min(P(22.0f), cwAll / std::max(1, nAll)));
  const int nShown = std::max(1, static_cast<int>(cwAll / cw));
  const int nFirst = nAll <= nShown ? 0 : std::max(0, std::min(nAll - nShown, p.nTick - nShown / 2));
  const float H = h + p.lanes.size() * (lh + gap);
  d->AddRectFilled(o, ImVec2(o.x + W, o.y + h), C(TK_WELL), P(3.0f));
  DrawLane(d, p.run, 0, nFirst, nShown, cx, o.y, h, cw);
  if (p.nTick >= nFirst && p.nTick < nFirst + nShown) {
    const float x = cx + (p.nTick - nFirst) * cw;
    d->AddRect(ImVec2(x, o.y), ImVec2(x + cw, o.y + h), C(TK_HI), 0.0f, 0, P(2.0f));
  }
  d->AddRect(o, ImVec2(o.x + W, o.y + h), C(TK_LINE), P(3.0f));
  if (gutter > 0.0f)
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, o.x + P(8.0f), o.y + h / 2.0f - P(7.0f), C(TK_INK2), "as picked", gutter - P(12.0f));
  ImGui::SetCursorScreenPos(o);
  ImGui::InvisibleButton("##timeline", ImVec2(W, h));
  if (ImGui::IsItemHovered() && ImGui::GetIO().MousePos.x >= cx) {
    const int i = nFirst + static_cast<int>((ImGui::GetIO().MousePos.x - cx) / cw);
    if (i >= 0 && i < n) {
      const Tick &k = p.run.ticks[static_cast<size_t>(i)];
      std::string tests;
      for (const Test &tst : p.run.tests)
        if (tst.nTick == i)
          tests += std::string("\n") + kOps[p.prog.ops[static_cast<size_t>(tst.nOp)].code].pszName + (tst.bTaken ? ": jumps" : ": goes on");
      ImGui::SetTooltip("tick %d: frames from %d, facing %d%s%s%s%s", i, k.nSet, k.nFacing, k.bFlip ? ", flipped" : "",
                        k.words.empty() ? "" : "\n", k.words.c_str(), tests.c_str());
      if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        p.nTick = i;
        p.bPlaying = false;
      }
    }
  }
  // the other ways, each from where it parts from the run as picked
  for (size_t li = 0; li < p.lanes.size(); ++li) {
    const Lane &l = p.lanes[li];
    const float y = o.y + h + gap + li * (lh + gap);
    d->AddRectFilled(ImVec2(o.x, y), ImVec2(o.x + W, y + lh), C(TK_WELL), P(3.0f));
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, o.x + P(8.0f), y + lh / 2.0f - P(7.0f), C(TK_INK3), l.name, gutter - P(12.0f));
    if (l.nFrom >= nFirst && l.nFrom < nFirst + nShown) {
      const float x = cx + (l.nFrom - nFirst) * cw + cw / 2.0f;
      d->AddLine(ImVec2(x, o.y + h - P(2.0f)), ImVec2(x, y + P(2.0f)), C(TK_INK3), P(1.5f));
    }
    DrawLane(d, l.run, l.nFrom, nFirst, nShown, cx, y, lh, cw);
    d->AddRect(ImVec2(o.x, y), ImVec2(o.x + W, y + lh), C(TK_LINE), P(3.0f));
    ImGui::SetCursorScreenPos(ImVec2(o.x, y));
    ImGui::InvisibleButton((std::string("##lane") + std::to_string(li)).c_str(), ImVec2(W, lh));
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s: the run had it so from tick %d (%d ticks%s). Click to play it.", l.what.c_str(), l.nFrom,
                        static_cast<int>(l.run.ticks.size()),
                        l.run.bEnded ? ", then it ends" : l.run.nLoop >= 0 ? ", then it goes on again" : "");
    if (ImGui::IsItemClicked())
      p.asIf = l.alt;
  }
  return H;
}

}  // namespace

namespace studio_art {

std::string AnimSummary(void);

// The player for an image, W wide, at the cursor; the cursor ends below it.
void AnimPanel(const std::string &image, float W) {
  Player &p = g_player;
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 o = ImGui::GetCursorScreenPos();
  Prepare(image);
  float hx = o.x + T(d, STUDIO_FONT_BOLD, 16.0f, o.x, o.y, C(TK_HEAD), "Its animations") + P(10.0f);
  T(d, STUDIO_FONT_TEXT, kStudioFsS, hx, o.y + P(3.0f), C(TK_INK3),
    "its script played as the game plays it, a tick each 42 ms at Fastest", W - (hx - o.x));
  float y = o.y + P(30.0f);
  if (!p.bRead) {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, o.x, y, C(TK_WARN), p.prog.why.empty() ? std::string("no script to play") : p.prog.why, W);
    ImGui::SetCursorScreenPos(ImVec2(o.x, y + P(26.0f)));
    ImGui::Dummy(ImVec2(0, 0));
    return;
  }
  Advance();
  // the controls
  ImGui::SetCursorScreenPos(ImVec2(o.x, y));
  const std::string cur = p.prog.anims[static_cast<size_t>(p.nAnim)].first;
  if (StudioSelectBegin("##anim", cur.c_str(), 190.0f, 26.0f)) {
    for (size_t i = 0; i < p.prog.anims.size(); ++i)
      if (StudioItem(p.prog.anims[i].first.c_str(), static_cast<int>(i) == p.nAnim)) {
        p.nAnim = static_cast<int>(i);
        p.animName = p.prog.anims[i].first;
        p.nTick = 0;
        p.bPlaying = true;
      }
    StudioSelectEnd();
  }
  ImGui::SameLine(0.0f, P(8.0f));
  if (StudioButton("##play", p.bPlaying ? "Pause" : "Play", p.bPlaying ? "pause" : "play", p.bPlaying ? SB_ON : SB_GOOD, 26.0f, 0.0f, kStudioFsXs))
    p.bPlaying = !p.bPlaying;
  ImGui::SameLine(0.0f, P(4.0f));
  const int n = static_cast<int>(p.run.ticks.size());
  if (StudioButton("##back", "", "back", SB_PLAIN, 26.0f, 28.0f, kStudioFsXs, n > 0)) {
    p.bPlaying = false;
    p.nTick = (p.nTick + n - 1) % std::max(1, n);
  }
  ImGui::SameLine(0.0f, P(4.0f));
  if (StudioButton("##step", "", "caret", SB_PLAIN, 26.0f, 28.0f, kStudioFsXs, n > 0)) {
    p.bPlaying = false;
    p.nTick = p.nTick + 1 < n ? p.nTick + 1 : (p.run.nLoop >= 0 ? p.run.nLoop : 0);
  }
  ImGui::SameLine(0.0f, P(4.0f));
  if (StudioButton("##again", "", "again", SB_PLAIN, 26.0f, 28.0f, kStudioFsXs)) {
    p.nTick = 0;
    p.bPlaying = true;
  }
  ImGui::SameLine(0.0f, P(12.0f));
  ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + P(5.0f)));
  StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK2), AnimSummary().c_str());
  y += P(36.0f);
  // the state the run's conditional jumps test, as picked
  bool abTested[K_COUNT] = {};
  bool bAny = false;
  for (const Test &tst : p.run.tests)
    abTested[tst.nKind] = bAny = true;
  for (const Lane &l : p.lanes)
    for (const Test &tst : l.run.tests)
      abTested[tst.nKind] = bAny = true;
  if (bAny) {
    const float x0 = o.x + P(58.0f);
    float x = x0;
    T(d, STUDIO_FONT_BOLD, kStudioFsS, o.x, y + P(5.0f), C(TK_INK2), "As if");
    for (int k = 0; k < K_COUNT; ++k) {
      if (!abTested[k])
        continue;
      const CondDef &cd = kConds[k];
      const float tw = TW(STUDIO_FONT_TEXT, kStudioFsS, cd.pszWhat), sw = StudioSegWidth(cd.apszChoice, cd.n, kStudioFsXs);
      if (x > x0 && x + tw + sw + P(8.0f) > o.x + W) {
        x = x0;
        y += P(30.0f);
      }
      T(d, STUDIO_FONT_TEXT, kStudioFsS, x, y + P(5.0f), C(TK_INK3), cd.pszWhat);
      x += tw + P(8.0f);
      ImGui::SetCursorScreenPos(ImVec2(x, y));
      p.asIf.choice[k] = StudioSeg((std::string("##asif") + cd.pszKey).c_str(), cd.apszChoice, cd.n, p.asIf.choice[k], 24.0f, kStudioFsXs);
      x += sw + P(20.0f);
    }
    y += P(34.0f);
  }
  // the picture at the left, the script's lines at the right
  const float pw = std::min(P(320.0f), W * 0.42f), ph = P(250.0f);
  const ImVec2 pa(o.x, y), pb(o.x + pw, y + ph);
  d->AddRectFilled(pa, pb, C(TK_WELL), P(3.0f));
  d->AddRect(pa, pb, C(TK_LINE), P(3.0f));
  if (n > 0) {
    const Tick &k = p.run.ticks[static_cast<size_t>(std::min(p.nTick, n - 1))];
    const int nColour = studio_art::ViewColour();
    const ImVec2 ia(pa.x + 1, pa.y + 1), ib(pb.x - 1, pb.y - P(22.0f));
    if (p.fK <= 0.0f)
      studio_art::DrawImageAt(d, ia, ib, image, k.nSet, k.nFacing, k.bFlip, nColour, &p.fK, &p.off, p.run.ticks[0].nSet);
    // the overlays under it, the image, the overlays over it, each at its own script's frame and the
    // image's facing, moved by its offset; one whose script has ended is gone
    SyncAbs();
    const int nNow = p.nAbs;
    // an overlay's own under-overlays go before it and its over-overlays after it
    std::function<void(int, bool)> overlays = [&](int nParent, bool bAbove) {
      for (int i = 0; i < static_cast<int>(p.shown.size()); ++i) {
        const Shown &s = p.shown[static_cast<size_t>(i)];
        if (s.nParent != nParent || s.ov.bAbove != bAbove || (nParent < 0 && s.ov.image == image))
          continue;
        int nSet = -1;
        overlays(i, false);  // what it made stays when it has gone
        if (ShownAt(p.shown, i, nNow, k.nOverlays, &nSet)) {
          float fK = p.fK;
          ImVec2 off(p.off.x + s.ov.x * p.fK, p.off.y + s.ov.y * p.fK);
          studio_art::DrawImageAt(d, ia, ib, s.ov.image, nSet, k.nFacing, k.bFlip, nColour, &fK, &off, -1);
        }
        overlays(i, true);
      }
    };
    overlays(-1, false);
    if (!k.bGone)
      studio_art::DrawImageAt(d, ia, ib, image, k.nSet, k.nFacing, k.bFlip, nColour, &p.fK, &p.off, p.run.ticks[0].nSet);
    overlays(-1, true);
    std::string line = "tick " + std::to_string(p.nTick) +
                       (k.bGone ? std::string("  the image is gone, its overlays play on")
                                : "  frames " + std::to_string(k.nSet) + "  facing " + std::to_string(k.nFacing));
    T(d, STUDIO_FONT_MONO, kStudioFsXs, pa.x + P(8.0f), pb.y - P(18.0f), C(TK_INK2), line, pw - P(16.0f));
    float ex = pb.x - P(8.0f);
    for (char c : k.events) {
      const float w = StudioChipW(EventWord(c));
      ex -= w;
      StudioChip(d, ImVec2(ex, pa.y + P(6.0f)), EventWord(c), c == 'a' ? CH_BAD : CH_PLAIN);
      ex -= P(4.0f);
    }
  }
  // the lines the animation runs, the one it is at marked
  const float lx = pb.x + P(14.0f), lw = W - pw - P(14.0f);
  ImGui::SetCursorScreenPos(ImVec2(lx, y));
  ImGui::BeginChild("##animops", ImVec2(lw, ph), ImGuiChildFlags_None);
  {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const int nAt = n > 0 ? p.run.ticks[static_cast<size_t>(std::min(p.nTick, n - 1))].nOp : -1;
    std::vector<std::pair<int, int>> order;  // first tick, op
    for (size_t i = 0; i < p.run.firstTick.size(); ++i)
      if (p.run.firstTick[i] >= 0)
        order.push_back({p.run.firstTick[i], static_cast<int>(i)});
    std::sort(order.begin(), order.end());
    const float rh = P(19.0f);
    for (const auto &e : order) {
      const ImVec2 r = ImGui::GetCursorScreenPos();
      const Op &op = p.prog.ops[static_cast<size_t>(e.second)];
      const bool bAt = e.second == nAt;
      if (bAt) {
        dl->AddRectFilled(r, ImVec2(r.x + lw, r.y + rh), C(TK_SEL));
        if (p.bPlaying)
          ImGui::SetScrollHereY(0.5f);
      }
      T(dl, STUDIO_FONT_MONO, kStudioFsXs, r.x + P(4.0f), r.y + P(3.0f), C(TK_INK3), std::to_string(e.first));
      T(dl, STUDIO_FONT_MONO, kStudioFsXs, r.x + P(40.0f), r.y + P(3.0f), bAt ? C(TK_HI) : op.code < 0 ? C(TK_WARN) : C(TK_INK),
        op.text, lw - P(48.0f));
      ImGui::Dummy(ImVec2(lw - P(12.0f), rh - ImGui::GetStyle().ItemSpacing.y));
    }
  }
  ImGui::EndChild();
  y += ph + P(10.0f);
  y += Timeline(ImVec2(o.x, y), W) + P(6.0f);
  float kx = o.x;
  for (char c : std::string("asco")) {
    d->AddCircleFilled(ImVec2(kx + P(4.0f), y + P(7.0f)), P(3.5f), EventColour(c));
    kx += P(12.0f) + T(d, STUDIO_FONT_TEXT, kStudioFsXs, kx + P(12.0f), y, C(TK_INK3), EventWord(c)) + P(10.0f);
  }
  if (!p.run.tests.empty())
    for (int nTaken = 1; nTaken >= 0; --nTaken) {
      Diamond(d, ImVec2(kx + P(4.0f), y + P(7.0f)), P(4.0f), nTaken ? C(TK_GOOD) : C(TK_INK3));
      kx += P(12.0f) + T(d, STUDIO_FONT_TEXT, kStudioFsXs, kx + P(12.0f), y, C(TK_INK3), nTaken ? "jumps" : "goes on") + P(10.0f);
    }
  if (!p.run.notes.empty())
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, kx + P(10.0f), y, C(TK_INK3), p.run.notes, o.x + W - kx - P(10.0f));
  ImGui::SetCursorScreenPos(ImVec2(o.x, y + P(22.0f)));
  ImGui::Dummy(ImVec2(0, 0));
}

// The player set from outside (studio_art_view): an animation by name, a tick, playing or paused
// (-1 leaves each as it is).
void AnimSet(const std::string &anim, int nTick, int nPlaying) {
  Player &p = g_player;
  if (!anim.empty()) {
    p.animName = anim;
    p.script.clear();  // read again, the animation found by its name
  }
  if (nTick >= 0)
    p.nTick = nTick;
  if (nPlaying >= 0)
    p.bPlaying = nPlaying != 0;
  p.bHeld = nTick >= 0 || nPlaying >= 0;
}

// A choice for a conditional jump by its name in studio_art_view's as_if; false when there is none.
bool AnimAsIf(const std::string &key, int nChoice) {
  for (int k = 0; k < K_COUNT; ++k)
    if (key == kConds[k].pszKey) {
      g_player.asIf.choice[k] = std::max(0, std::min(kConds[k].n - 1, nChoice));
      return true;
    }
  return false;
}

// "tick 4 of 26, goes on from 8" and the like.
std::string AnimSummary(void) {
  const Player &p = g_player;
  const int n = static_cast<int>(p.run.ticks.size());
  std::string s = "tick " + std::to_string(p.nTick) + " of " + std::to_string(n);
  if (p.run.nLoop >= 0)
    s += ", goes on from " + std::to_string(p.run.nLoop);
  else if (p.run.bEnded) {
    int nOwn = n;
    while (nOwn > 0 && p.run.ticks[static_cast<size_t>(nOwn - 1)].bGone)
      --nOwn;
    s += nOwn < n ? ", it ends at " + std::to_string(nOwn - 1) + " and its overlays play on" : std::string(", then it ends");
  }
  return s;
}

// For studio_art_anim: an animation run by name at a facing, its ticks in words.
bool AnimDescribe(const std::string &image, const std::string &anim, int nFacing, std::string *pAnswer) {
  bool bText = false;
  const std::string script = ImageScriptOf(image, &bText);
  Program prog;
  if (!(bText ? ReadText(script, &prog) : ReadClassic(atoi(script.c_str()), &prog))) {
    *pAnswer = prog.why;
    return false;
  }
  std::string names;
  int nStart = -1;
  for (const auto &a : prog.anims) {
    names += std::string(names.empty() ? "" : ",") + PortJsonQuote(a.first);
    if (a.first == anim)
      nStart = a.second;
  }
  if (nStart < 0) {
    *pAnswer = "\"animations\":[" + names + "]";
    return true;
  }
  const Run r = RunAnim(prog, image, nStart, nFacing, StartingSet(prog, image, anim, nFacing, g_player.asIf), g_player.asIf);
  Run played = r;
  PlayOn(&played, ShownOf(r, nFacing, g_player.asIf));
  std::string ticks;
  for (const Tick &k : r.ticks)
    ticks += std::string(ticks.empty() ? "" : ",") + "[" + std::to_string(k.nSet) + "," + std::to_string(k.nFacing) + "," +
             PortJsonQuote(k.events) + "]";
  *pAnswer = "\"animations\":[" + names + "],\"script\":" + PortJsonQuote(script) + ",\"ticks\":[" + ticks + "],\"loop\":" +
             std::to_string(r.nLoop) + ",\"ended\":" + (r.bEnded ? "true" : "false") + ",\"overlays\":" +
             std::to_string(r.overlays.size()) + ",\"played\":" + std::to_string(played.ticks.size()) + ",\"notes\":" +
             PortJsonQuote(r.notes);
  // the conditional jumps it met, and the choices they went by
  std::string tests, asIf;
  for (const Test &tst : r.tests)
    tests += std::string(tests.empty() ? "" : ",") + "[" + std::to_string(tst.nTick) + "," + PortJsonQuote(kConds[tst.nKind].pszKey) +
             "," + (tst.bTaken ? "true" : "false") + "]";
  for (int k = 0; k < K_COUNT; ++k)
    asIf += std::string(asIf.empty() ? "" : ",") + PortJsonQuote(kConds[k].pszKey) + ":" +
            PortJsonQuote(kConds[k].apszChoice[g_player.asIf.choice[k]]);
  *pAnswer += ",\"tests\":[" + tests + "],\"as_if\":{" + asIf + "}";
  std::string lanes;
  for (const Lane &l : LanesOf(prog, image, nStart, nFacing, StartingSet(prog, image, anim, nFacing, g_player.asIf), g_player.asIf, r))
    lanes += std::string(lanes.empty() ? "" : ",") + "{\"name\":" + PortJsonQuote(l.name) + ",\"from\":" + std::to_string(l.nFrom) +
             ",\"ticks\":" + std::to_string(l.run.ticks.size()) + "}";
  *pAnswer += ",\"lanes\":[" + lanes + "]";
  // each overlay: its image, where and when it was made, and its own run's frame sets
  std::string ovs;
  const std::vector<Shown> all = ShownOf(r, nFacing, g_player.asIf);
  for (const Shown &s : all) {
    const Overlay &ov = s.ov;
    const Run &orun = s.run;
    std::string sets;
    for (const Tick &k : orun.ticks)
      sets += std::string(sets.empty() ? "" : ",") + std::to_string(k.nSet);
    ovs += std::string(ovs.empty() ? "" : ",") + "{\"image\":" + PortJsonQuote(ov.image) + ",\"tick\":" + std::to_string(ov.nTick) +
           ",\"at\":[" + std::to_string(ov.x) + "," + std::to_string(ov.y) + "],\"above\":" + (ov.bAbove ? "true" : "false") +
           ",\"sets\":[" + sets + "],\"ended\":" + (orun.bEnded ? "true" : "false") + ",\"made_by\":" +
           std::to_string(s.nParent) + "}";
  }
  *pAnswer += ",\"overlay_runs\":[" + ovs + "]";
  return true;
}

}  // namespace studio_art

// A unit that its image script walks (movement control 2) moves at the speed the Walking animation's
// last move set, a pixel a frame for each, held through each wait (CImage's move: ForceSpeed of
// move << 8). The speed over the part that repeats (the whole when it ends instead), in pixels a
// frame; 0 when it has no moves.
double StudioScriptWalkSpeed(const std::string &image) {
  bool bText = false;
  const std::string script = studio_art::ImageScriptOf(image, &bText);
  Program prog;
  if (script.empty() || !(bText ? ReadText(script, &prog) : ReadClassic(atoi(script.c_str()), &prog)))
    return 0.0;
  int pc = -1;
  for (const auto &a : prog.anims)
    if (a.first == "Walking")
      pc = a.second;
  double dSpeed = 0.0, dDistance = 0.0, dFrames = 0.0;
  bool bMoved = false;
  // the distance and frames when each instruction was first reached, so the loop is measured alone
  std::vector<int> stack;
  std::vector<std::pair<double, double>> at(prog.ops.size(), {-1.0, -1.0});
  for (int nSteps = 0; nSteps < 2000 && pc >= 0 && pc < static_cast<int>(prog.ops.size()); ++nSteps) {
    const std::pair<double, double> &first = at[static_cast<size_t>(pc)];
    if (first.second >= 0.0) {
      // back at an instruction already run: the loop is from there to here
      if (dFrames - first.second > 0.0) {
        dDistance -= first.first;
        dFrames -= first.second;
      }
      break;
    }
    at[static_cast<size_t>(pc)] = {dDistance, dFrames};
    const Op &op = prog.ops[static_cast<size_t>(pc)];
    auto arg = [&](size_t i) { return i < op.n.size() ? static_cast<double>(op.n[i]) : 0.0; };
    int nNext = op.next;
    switch (op.code) {
    case 0x29: dSpeed = arg(0); bMoved = true; break;  // move
    case 0x05: dDistance += dSpeed * arg(0); dFrames += arg(0); break;  // wait
    case 0x06: {  // waitrand: its middle
      const double d = (arg(0) + std::max(arg(0), arg(1))) / 2.0;
      dDistance += dSpeed * d;
      dFrames += d;
      break;
    }
    case 0x07: nNext = op.jump; break;  // goto
    case 0x35: stack.push_back(op.next); nNext = op.jump; break;  // call
    case 0x36:  // return
      if (stack.empty())
        nNext = -1;
      else {
        nNext = stack.back();
        stack.pop_back();
      }
      break;
    case 0x16: nNext = -1; break;  // end
    default: break;
    }
    pc = nNext;
  }
  return bMoved && dFrames > 0.0 ? dDistance / dFrames : 0.0;
}
