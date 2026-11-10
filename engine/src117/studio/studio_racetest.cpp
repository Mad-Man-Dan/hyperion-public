// studio_racetest.cpp -- a race played on a real map: the mod saved, then Hyperion.exe started as a
// game of its own on a map the modder picks, the race's player against computers of a classic race
// or one of the mod's. The game has a data folder of its own (studio-games\, since one data folder
// takes one copy of Hyperion) whose settings name the studio's mods folder; a thread reads it
// through its live channel (a port of 27140 to 27149, a key the studio gives it). It is in a job
// that ends it with the studio; Stop ends that process alone.
//
//   studio_race_play {race, map, computers?, computer_race?, extra?}   the mod saved, then the game started
//   studio_race_game {resume?}                                  the game's state, its players' units by type
//   studio_race_stop {}                                         the game ended
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdlib.h>
#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <thread>
#include <vector>
#include "../design/design_engine.h"       // DesignCall
#include "../port/data/port_mods.h"        // PortModsFolder
#include "../port/platform/port_paths.h"   // PortDataFolder, PortProgramFolder, PortCommandOptions, PortMapFolder*
#include "../port/tools/port_control.h"    // PortControlCommand
#include "studio_draw.h"

namespace {

using namespace studio_draw;

// the control ports the games take, the first free one
const int kFirstPort = 27140, kLastPort = 27149;

// ---- the game's live channel, a WebSocket client ---------------------------------------------------------

struct Channel {
  SOCKET s = INVALID_SOCKET;
  std::string in;
  int nId = 0;

  void Close(void) {
    if (s != INVALID_SOCKET)
      closesocket(s);
    s = INVALID_SOCKET;
    in.clear();
  }

  bool SendAll(const std::string &data) {
    size_t at = 0;
    while (at < data.size()) {
      const int n = send(s, data.data() + at, static_cast<int>(std::min<size_t>(data.size() - at, 1 << 20)), 0);
      if (n <= 0)
        return false;
      at += static_cast<size_t>(n);
    }
    return true;
  }

  bool More(void) {
    char buf[65536];
    const int n = recv(s, buf, sizeof buf, 0);
    if (n <= 0)
      return false;
    in.append(buf, static_cast<size_t>(n));
    return true;
  }

  bool Open(int nPort, const std::string &key) {
    Close();
    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
      return false;
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<u_short>(nPort));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(s, reinterpret_cast<sockaddr *>(&a), sizeof a) != 0) {
      Close();
      return false;
    }
    DWORD dwWait = 5000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&dwWait), sizeof dwWait);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&dwWait), sizeof dwWait);
    const std::string host = "127.0.0.1:" + std::to_string(nPort);
    if (!SendAll("GET /?t=" + key + " HTTP/1.1\r\nHost: " + host +
                 "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: c3R1ZGlvIHJhY2UgdGVzdA==\r\n"
                 "Sec-WebSocket-Version: 13\r\n\r\n")) {
      Close();
      return false;
    }
    size_t end;
    while ((end = in.find("\r\n\r\n")) == std::string::npos)
      if (in.size() > 16384 || !More()) {
        Close();
        return false;
      }
    if (in.compare(0, 12, "HTTP/1.1 101") != 0) {
      Close();
      return false;
    }
    in.erase(0, end + 4);
    return true;
  }

  // a text frame, masked as a client's are
  bool SendText(const std::string &text) {
    std::string f;
    f += static_cast<char>(0x81);
    const size_t n = text.size();
    if (n < 126) {
      f += static_cast<char>(0x80 | n);
    } else if (n < 65536) {
      f += static_cast<char>(0x80 | 126);
      f += static_cast<char>(n >> 8);
      f += static_cast<char>(n);
    } else {
      f += static_cast<char>(0x80 | 127);
      for (int i = 7; i >= 0; --i)
        f += static_cast<char>(static_cast<unsigned long long>(n) >> (8 * i));
    }
    unsigned char mask[4];
    for (unsigned char &m : mask)
      m = static_cast<unsigned char>(rand());
    f.append(reinterpret_cast<const char *>(mask), 4);
    for (size_t i = 0; i < n; ++i)
      f += static_cast<char>(text[i] ^ mask[i & 3]);
    return SendAll(f);
  }

  // the next whole message
  bool Receive(std::string *pText) {
    std::string message;
    for (;;) {
      const unsigned char *p = reinterpret_cast<const unsigned char *>(in.data());
      size_t at = 2;
      unsigned long long len = 0;
      bool bWhole = in.size() >= 2;
      if (bWhole) {
        len = p[1] & 0x7F;
        if (len == 126) {
          bWhole = in.size() >= 4;
          if (bWhole)
            len = static_cast<unsigned long long>(p[2]) << 8 | p[3];
          at = 4;
        } else if (len == 127) {
          bWhole = in.size() >= 10;
          len = 0;
          for (int i = 0; bWhole && i < 8; ++i)
            len = len << 8 | p[2 + i];
          at = 10;
        }
        bWhole = bWhole && in.size() >= at + len;
      }
      if (!bWhole) {
        if (in.size() > (64u << 20) || !More())
          return false;
        continue;
      }
      const bool bFin = (p[0] & 0x80) != 0;
      const int nOpcode = p[0] & 0x0F;
      const std::string payload = in.substr(at, static_cast<size_t>(len));
      in.erase(0, at + static_cast<size_t>(len));
      if (nOpcode == 8)
        return false;
      if (nOpcode == 9 || nOpcode == 10)
        continue;  // pings: the channel sends none to a client
      message += payload;
      if (bFin) {
        *pText = message;
        return true;
      }
    }
  }

  // a command and its answer: fields are the request's members after its id and cmd
  bool Call(const char *pszCmd, const std::string &fields, PortJson *pOut) {
    const int id = ++nId;
    if (!SendText("{\"id\":" + std::to_string(id) + ",\"cmd\":" + PortJsonQuote(pszCmd) + (fields.empty() ? "" : "," + fields) + "}"))
      return false;
    for (;;) {
      std::string text, err;
      if (!Receive(&text))
        return false;
      PortJson a;
      if (!PortJsonParse(text.c_str(), &a, &err))
        continue;
      if (static_cast<int>(a["id"].Num(-1)) == id) {
        *pOut = a;
        return true;
      }
    }
  }
};

// ---- the game started, and what its channel says ----------------------------------------------------------

struct SlotSeen {
  int nSlot = 0;
  int nType = 0;  // 1 a computer, 2 a person
  int nRace = 0;  // the race it plays as: 0 Zerg, 1 Terran, 2 Protoss
  std::string name;
  int nMinerals = 0, nGas = 0, nSupplyUsed = 0, nSupplyMax = 0;
  std::map<int, int> units;  // unit type, how many
  int nUnits = 0;
};

// What the thread and the panel share, behind its lock.
struct Shared {
  std::mutex m;
  bool bStop = false;
  bool bConnected = false;
  bool bExited = false;
  DWORD dwExit = 0;
  std::string state = "starting";  // starting, front end, menus, game, ended
  unsigned nFrame = 0;
  bool bPaused = false;
  std::string mod;
  std::vector<SlotSeen> slots;
  std::vector<std::string> outbox;  // commands for the game: "resume"
  double dSeen = 0.0;               // when the channel last answered, in the thread's seconds
  // the game's own trace, while an area asks for it (script_trace): the rows that have come in
  // since the last read, as the channel gave them, and the last row read
  bool bTrace = false;
  unsigned dwAiAt = 0;
  std::string aiRows;
};

struct Game {
  std::shared_ptr<Shared> shared;
  HANDLE hProcess = NULL;
  HANDLE hJob = NULL;
  DWORD dwPid = 0;
  int nPort = 0;
  std::string mod, race, raceName, map, computerRace, command, folder;
  int nComputers = 1;
};
Game g_game;

double Seconds(void) {
  return GetTickCount64() / 1000.0;
}

void ReadSlots(const PortJson &players, const PortJson &units, std::vector<SlotSeen> *pOut) {
  std::map<int, SlotSeen> by;
  for (const PortJson &p : players["players"].items) {
    const int nSlot = static_cast<int>(p["slot"].Num(-1));
    const int nType = static_cast<int>(p["type"].Num(0));
    if (nSlot < 0 || nSlot > 7 || (nType != 1 && nType != 2))
      continue;
    SlotSeen &s = by[nSlot];
    s.nSlot = nSlot;
    s.nType = nType;
    s.nRace = static_cast<int>(p["race"].Num(0));
    s.name = p["name"].Str();
    s.nMinerals = static_cast<int>(p["minerals"].Num(0));
    s.nGas = static_cast<int>(p["gas"].Num(0));
    // supply in halves, as the game counts it
    s.nSupplyUsed = static_cast<int>(p["supply"].At(0).Num(0)) / 2;
    s.nSupplyMax = static_cast<int>(p["supply"].At(1).Num(0)) / 2;
  }
  for (const PortJson &u : units["units"].items) {
    const auto it = by.find(static_cast<int>(u["player"].Num(-1)));
    if (it == by.end())
      continue;
    ++it->second.units[static_cast<int>(u["type"].Num(0))];
    ++it->second.nUnits;
  }
  pOut->clear();
  // the person first, then the computers
  for (int nType : {2, 1})
    for (auto &e : by)
      if (e.second.nType == nType)
        pOut->push_back(e.second);
}

// The thread that watches one game: its process, and its channel once it listens.
void Watch(std::shared_ptr<Shared> sh, HANDLE hProcess, int nPort, std::string key) {
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
  Channel ch;
  for (;;) {
    std::vector<std::string> outbox;
    {
      std::lock_guard<std::mutex> lock(sh->m);
      if (sh->bStop)
        break;
      outbox.swap(sh->outbox);
    }
    if (WaitForSingleObject(hProcess, 0) == WAIT_OBJECT_0) {
      DWORD dw = 0;
      GetExitCodeProcess(hProcess, &dw);
      std::lock_guard<std::mutex> lock(sh->m);
      sh->bExited = true;
      sh->dwExit = dw;
      sh->bConnected = false;
      sh->state = "ended";
      break;
    }
    if (ch.s == INVALID_SOCKET && !ch.Open(nPort, key)) {
      Sleep(300);
      continue;
    }
    PortJson status, players, units;
    bool bOk = true;
    for (const std::string &cmd : outbox)
      bOk = bOk && ch.Call(cmd.c_str(), "", &status);
    bOk = bOk && ch.Call("status", "", &status);
    const std::string state = status["state"].Str();
    if (bOk && state == "game")
      bOk = ch.Call("players", "", &players) && ch.Call("units", "\"max\":1700", &units);
    // the trace, while an area asks for it: the rows past the last one read
    bool bTrace = false;
    unsigned dwAiAt = 0;
    {
      std::lock_guard<std::mutex> lock(sh->m);
      bTrace = sh->bTrace;
      dwAiAt = sh->dwAiAt;
    }
    PortJson trace;
    bool bTraced = false;
    if (bOk && bTrace && state == "game")
      bTraced = ch.Call("script_trace", "\"on\":true,\"ai_since\":" + std::to_string(dwAiAt), &trace);
    if (!bOk) {
      ch.Close();
      std::lock_guard<std::mutex> lock(sh->m);
      sh->bConnected = false;
      continue;
    }
    std::vector<SlotSeen> slots;
    if (state == "game")
      ReadSlots(players, units, &slots);
    {
      std::lock_guard<std::mutex> lock(sh->m);
      sh->bConnected = true;
      sh->state = state;
      sh->nFrame = static_cast<unsigned>(status["frame"].Num(0));
      sh->bPaused = status["paused"].kind == PortJson::BOOL && status["paused"].b;
      sh->mod = status["mod"].Str();
      if (state == "game")
        sh->slots.swap(slots);
      if (bTraced) {
        const PortJson &ai = trace["ai"];
        for (size_t i = 0; i < ai.size(); ++i) {
          sh->aiRows += (sh->aiRows.empty() ? "" : ",") + PortJsonWrite(ai.At(i));
          // ai_since is the first row wanted, so the next ask starts past this one
          const unsigned dwAt = static_cast<unsigned>(ai.At(i)["at"].Num(0));
          if (dwAt >= sh->dwAiAt)
            sh->dwAiAt = dwAt + 1;
        }
        if (sh->aiRows.size() > (1u << 20))  // what an area has not read in a long while
          sh->aiRows.erase(0, sh->aiRows.size() - (1u << 20));
      }
      sh->dSeen = Seconds();
    }
    for (int i = 0; i < 5; ++i) {
      Sleep(100);
      std::lock_guard<std::mutex> lock(sh->m);
      if (sh->bStop || !sh->outbox.empty())
        break;
    }
  }
  ch.Close();
  CloseHandle(hProcess);
  WSACleanup();
}

bool Running(void) {
  if (!g_game.shared)
    return false;
  std::lock_guard<std::mutex> lock(g_game.shared->m);
  return !g_game.shared->bExited;
}

// The game ended, by its own process; its thread told to go.
void Stop(void) {
  if (g_game.shared) {
    std::lock_guard<std::mutex> lock(g_game.shared->m);
    g_game.shared->bStop = true;
  }
  DWORD dwExit = 0;
  if (g_game.hProcess != NULL) {
    if (WaitForSingleObject(g_game.hProcess, 0) == WAIT_TIMEOUT) {
      TerminateProcess(g_game.hProcess, 1);
      WaitForSingleObject(g_game.hProcess, 3000);
    }
    GetExitCodeProcess(g_game.hProcess, &dwExit);
    CloseHandle(g_game.hProcess);
    g_game.hProcess = NULL;
  }
  if (g_game.hJob != NULL) {
    CloseHandle(g_game.hJob);
    g_game.hJob = NULL;
  }
  if (g_game.shared) {
    std::lock_guard<std::mutex> lock(g_game.shared->m);
    if (!g_game.shared->bExited) {
      g_game.shared->bExited = true;
      g_game.shared->dwExit = dwExit;
      g_game.shared->state = "ended";
      g_game.shared->bConnected = false;
    }
  }
}

// The game ended when the studio ends: the job does it if the studio is killed, this when it leaves.
struct EndAtExit {
  ~EndAtExit() {
    if (g_game.hProcess != NULL && WaitForSingleObject(g_game.hProcess, 0) == WAIT_TIMEOUT)
      TerminateProcess(g_game.hProcess, 1);
  }
} g_endAtExit;

// ---- where things are -------------------------------------------------------------------------------------

bool FileThere(const std::string &path) {
  const DWORD dw = GetFileAttributesA(path.c_str());
  return dw != INVALID_FILE_ATTRIBUTES && !(dw & FILE_ATTRIBUTE_DIRECTORY);
}

// Hyperion.exe: beside the studio, as a package has it, else the x64 build of the engine the
// studio's game folder is in (testrun\<folder>\ beside src117\build\x64\).
std::string FindHyperion(void) {
  std::string dir = PortProgramFolder();
  if (FileThere(dir + "Hyperion.exe"))
    return dir + "Hyperion.exe";
  for (int i = 0; i < 5 && dir.size() > 3; ++i) {
    for (const char *psz : {"src117\\build\\x64\\Hyperion.exe", "engine\\src117\\build\\x64\\Hyperion.exe"})
      if (FileThere(dir + psz))
        return dir + psz;
    const size_t at = dir.find_last_of('\\', dir.size() - 2);
    if (at == std::string::npos)
      break;
    dir = dir.substr(0, at + 1);
  }
  return std::string();
}

std::string GamesFolder(void) {
  return std::string(PortDataFolder()) + "studio-games\\";
}

// The maps the studio's game folder, its data folder and its map folders (--maps) hold.
struct MapFile {
  std::string name;   // its file's name
  std::string path;
  std::string where;  // the folder it is in
  int nPlayers = 0;   // from its name's "(2)", 0 when it does not say
};
std::vector<MapFile> g_maps;
double g_dMapsAt = -100.0;

int PlayersInName(const std::string &name) {
  if (name.size() < 3 || name[0] != '(')
    return 0;
  const int n = atoi(name.c_str() + 1);
  return n > 0 && n <= 12 ? n : 0;
}

void WalkMaps(const std::string &dir, int nDepth, std::set<std::string> *pSeen) {
  if (nDepth > 5 || g_maps.size() > 5000)
    return;
  WIN32_FIND_DATAA fd;
  const HANDLE h = FindFirstFileA((dir + "*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE)
    return;
  std::vector<std::string> folders;
  do {
    const std::string name = fd.cFileName;
    if (name == "." || name == "..")
      continue;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (_stricmp(name.c_str(), "replays") != 0 && _stricmp(name.c_str(), "save") != 0)
        folders.push_back(name);
      continue;
    }
    const size_t dot = name.rfind('.');
    const char *pszExt = dot == std::string::npos ? "" : name.c_str() + dot + 1;
    if (_stricmp(pszExt, "scm") != 0 && _stricmp(pszExt, "scx") != 0 && _stricmp(pszExt, "hxm") != 0)
      continue;
    if (pSeen->insert(Lower(dir + name)).second)
      g_maps.push_back({name, dir + name, dir, PlayersInName(name)});
  } while (FindNextFileA(h, &fd));
  FindClose(h);
  for (const std::string &f : folders)
    WalkMaps(dir + f + "\\", nDepth + 1, pSeen);
}

const std::vector<MapFile> &Maps(bool bFresh) {
  if (!bFresh && Seconds() - g_dMapsAt < 60.0)
    return g_maps;
  g_maps.clear();
  std::set<std::string> seen, roots;
  auto root = [&](std::string dir) {
    if (dir.empty())
      return;
    if (dir.back() != '\\' && dir.back() != '/')
      dir += '\\';
    if (roots.insert(Lower(dir)).second)
      WalkMaps(dir, 0, &seen);
  };
  root(std::string(PortDataFolder()) + "maps\\");
  if (const char *pszGame = getenv("HYPERION_GAME"))
    root(std::string(pszGame) + "maps\\");
  char sz[MAX_PATH];
  for (int i = 0; i < PortMapFolderCount(); ++i)
    if (PortMapFolderPath(i, "maps\\", sz, sizeof sz))
      root(sz);
  std::stable_sort(g_maps.begin(), g_maps.end(), [](const MapFile &a, const MapFile &b) { return Lower(a.name) < Lower(b.name); });
  g_dMapsAt = Seconds();
  return g_maps;
}

// A map by its path, or by its file's name (with or without the extension), or the first whose
// name holds the text.
bool FindMap(const std::string &want, MapFile *pOut) {
  if (want.empty())
    return false;
  if (FileThere(want)) {
    const size_t at = want.find_last_of("\\/");
    pOut->name = at == std::string::npos ? want : want.substr(at + 1);
    pOut->path = want;
    pOut->where = at == std::string::npos ? std::string() : want.substr(0, at + 1);
    pOut->nPlayers = PlayersInName(pOut->name);
    return true;
  }
  const std::string w = Lower(want);
  for (int nPass = 0; nPass < 3; ++nPass)
    for (const MapFile &m : Maps(false)) {
      const std::string n = Lower(m.name), stem = n.substr(0, n.rfind('.'));
      if ((nPass == 0 && n == w) || (nPass == 1 && stem == w) || (nPass == 2 && n.find(w) != std::string::npos)) {
        *pOut = m;
        return true;
      }
    }
  return false;
}

// ---- the races --------------------------------------------------------------------------------------------

const char *const kClassicRaces[] = {"terran", "zerg", "protoss", "random"};
const char *const kBaseWords[] = {"Zerg", "Terran", "Protoss"};  // a slot's race as the game numbers it

// A race of the open mod by its record's name or the name players see; false for none.
bool ModRace(const std::string &want, std::string *pId, std::string *pName) {
  for (const PortJsonMember &m : StudioDoc("types")["races"].members) {
    const std::string name = m.second["name"].Str(m.first.c_str());
    if (Lower(m.first) == Lower(want) || Lower(name) == Lower(want)) {
      *pId = m.first;
      *pName = name;
      return true;
    }
  }
  return false;
}

bool ClassicRace(const std::string &want) {
  for (const char *psz : kClassicRaces)
    if (Lower(want) == psz)
      return true;
  return false;
}

// A unit type's record name in the open mod, or a classic row's; "" for none.
std::string UnitName(int nType) {
  static std::map<int, std::string> s_names;
  static unsigned s_nChange = ~0u;
  static std::string s_mod;
  if (s_nChange != StudioChange() || s_mod != StudioModId()) {
    s_names.clear();
    for (const StudioRecord &r : StudioRecords(ST_UNITS))
      if (r.nId >= 0 && !s_names.count(r.nId))
        s_names[r.nId] = r.name;
    s_nChange = StudioChange();
    s_mod = StudioModId();
  }
  const auto it = s_names.find(nType);
  if (it != s_names.end())
    return it->second;
  const char *psz = nType < StudioClassicRows(ST_UNITS) ? StudioClassicName(ST_UNITS, nType) : nullptr;
  return psz ? psz : "";
}

// ---- starting a game --------------------------------------------------------------------------------------

std::string Quoted(const std::string &s) {
  return s.find_first_of(" \t\"") == std::string::npos && !s.empty() ? s : "\"" + s + "\"";
}

int FreePort(void) {
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
  int nFound = 0;
  for (int p = kFirstPort; p <= kLastPort && nFound == 0; ++p) {
    const SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
      continue;
    BOOL bOne = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&bOne), sizeof bOne);
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<u_short>(p));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, reinterpret_cast<sockaddr *>(&a), sizeof a) == 0)
      nFound = p;
    closesocket(s);
  }
  WSACleanup();
  return nFound;
}

std::string NewKey(void) {
  std::random_device rd;
  static const char kHex[] = "0123456789abcdef";
  std::string key;
  for (int i = 0; i < 32; ++i)
    key += kHex[rd() & 15];
  return key;
}

// The studio's environment for the game: the modder's game to play, so a window of its own and
// none of a tool's run's quiet; the channel's key given. Sorted, as Windows keeps a block.
std::string GameEnvironment(const std::string &key) {
  struct NoCase {
    bool operator()(const std::string &a, const std::string &b) const { return _stricmp(a.c_str(), b.c_str()) < 0; }
  };
  std::map<std::string, std::string, NoCase> vars;
  if (char *pBlock = GetEnvironmentStringsA()) {
    for (const char *p = pBlock; *p != '\0'; p += strlen(p) + 1) {
      const char *pEq = strchr(p + 1, '=');  // "=C:=C:\..." has a name that starts with "="
      if (pEq != nullptr)
        vars[std::string(p, static_cast<size_t>(pEq - p))] = pEq + 1;
    }
    FreeEnvironmentStringsA(pBlock);
  }
  const char *pszWindow = getenv("HYPERION_WINDOW");
  const bool bBackground = pszWindow != nullptr && _stricmp(pszWindow, "background") == 0;
  for (const char *psz : {"HYPERION_QUIET", "HYPERION_DLLS", "HYPERION_TEST_MOUSE", "HYPERION_DESIGN_SIZE", "HYPERION_DESIGN_FOLDER",
                          "HYPERION_CONTROL_KEY", "HYPERION_WINDOW"})
    vars.erase(psz);
  vars["HYPERION_CONTROL_KEY"] = key;
  // a tool's studio, in a window that takes no focus, starts its games the same way
  vars["HYPERION_WINDOW"] = bBackground ? "background" : "windowed";
  std::string block;
  for (const auto &v : vars)
    block += v.first + "=" + v.second + '\0';
  return block + '\0';
}

// The game's data folder made, its settings naming the studio's mods folder so the mod is found.
bool GamesFolderReady(std::string *pWhy) {
  const std::string folder = GamesFolder();
  CreateDirectoryA(folder.c_str(), NULL);
  const std::string path = folder + "settings.json";
  PortJson settings = StudioJsonObject();
  if (FILE *f = fopen(path.c_str(), "rb")) {
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
      text.append(buf, n);
    fclose(f);
    std::string err;
    PortJson read;
    if (PortJsonParse(text.c_str(), &read, &err) && read.kind == PortJson::OBJECT)
      settings = read;
  }
  PortJson hyperion = settings["hyperion"].kind == PortJson::OBJECT ? settings["hyperion"] : StudioJsonObject();
  StudioJsonPut(hyperion, "modsFolder", StudioJsonString(PortModsFolder()));
  StudioJsonPut(settings, "hyperion", hyperion);
  const std::string text = StudioJsonText(settings);
  FILE *f = fopen(path.c_str(), "wb");
  if (f == nullptr) {
    *pWhy = "could not write " + path;
    return false;
  }
  fwrite(text.data(), 1, text.size(), f);
  fclose(f);
  return true;
}

// The mod saved, then the game started; false with why. extra is put on the game's command line
// as it is, for what another area asks for (the Mode workspace's Test: --mode and its settings).
bool Play(const std::string &raceWant, const std::string &mapWant, int nComputers, const std::string &computerWant,
          const std::string &extra, std::string *pWhy) {
  if (!StudioIsOpen()) {
    *pWhy = "no mod is open";
    return false;
  }
  std::string raceId, raceName;
  if (!ModRace(raceWant, &raceId, &raceName)) {
    if (!ClassicRace(raceWant)) {
      *pWhy = "race is one of the mod's races, by its record's name or the name players see, or terran, zerg, protoss or random";
      return false;
    }
    raceId = raceName = Lower(raceWant);
  }
  std::string computerRace;
  if (!computerWant.empty()) {
    std::string id;
    if (ModRace(computerWant, &id, &computerRace)) {
    } else if (ClassicRace(computerWant)) {
      computerRace = Lower(computerWant);
    } else {
      *pWhy = "computer_race is one of the mod's races, or terran, zerg, protoss or random";
      return false;
    }
  }
  MapFile map;
  if (!FindMap(mapWant, &map)) {
    *pWhy = mapWant.empty() ? "map is a map's path or its file's name" : "no map " + mapWant + " among the studio's maps";
    return false;
  }
  if (map.nPlayers > 0 && nComputers > map.nPlayers - 1) {
    *pWhy = map.name + " has " + std::to_string(map.nPlayers) + " starts: " + std::to_string(map.nPlayers - 1) + " computers at most";
    return false;
  }
  const std::string exe = FindHyperion();
  if (exe.empty()) {
    *pWhy = "Hyperion.exe is neither beside the studio nor in the engine's x64 build";
    return false;
  }
  if (!StudioSave(pWhy))
    return false;
  if (!GamesFolderReady(pWhy))
    return false;
  const int nPort = FreePort();
  if (nPort == 0) {
    *pWhy = "no control port free in " + std::to_string(kFirstPort) + " to " + std::to_string(kLastPort);
    return false;
  }
  Stop();  // one game at a time
  const std::string folder = GamesFolder();
  std::string command = Quoted(exe) + " " + Quoted("--data=" + folder.substr(0, folder.size() - 1)) + " --control=" +
                        std::to_string(nPort) + " --mod " + Quoted(StudioModId()) + " --map " + Quoted(map.path) + " --race " +
                        Quoted(raceName) + " --computers " + std::to_string(nComputers);
  if (!computerRace.empty())
    command += " --computer-race " + Quoted(computerRace);
  if (!extra.empty())
    command += " " + extra;
  const PortOptions *pOptions = PortCommandOptions();
  for (int i = 0; pOptions != nullptr && i < pOptions->nMaps; ++i) {
    std::string m = pOptions->aszMaps[i];
    while (!m.empty() && (m.back() == '\\' || m.back() == '/'))
      m.pop_back();
    command += " --maps " + Quoted(m);
  }
  const std::string key = NewKey();
  std::string env = GameEnvironment(key);
  std::vector<char> line(command.begin(), command.end());
  line.push_back('\0');
  STARTUPINFOA si = {};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi = {};
  if (!CreateProcessA(exe.c_str(), line.data(), NULL, NULL, FALSE, CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP, &env[0], folder.c_str(), &si,
                      &pi)) {
    *pWhy = "Hyperion.exe did not start (error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  // in a job that ends it when the studio ends, however the studio ends
  HANDLE hJob = CreateJobObjectA(NULL, NULL);
  if (hJob != NULL) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = {};
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &li, sizeof li) || !AssignProcessToJobObject(hJob, pi.hProcess)) {
      CloseHandle(hJob);
      hJob = NULL;
      StudioLog("warning", "the game could not be put in the studio's job: it outlives the studio if the studio is killed");
    }
  }
  ResumeThread(pi.hThread);
  CloseHandle(pi.hThread);
  g_game = Game();
  g_game.shared = std::make_shared<Shared>();
  g_game.hProcess = pi.hProcess;
  g_game.hJob = hJob;
  g_game.dwPid = pi.dwProcessId;
  g_game.nPort = nPort;
  g_game.mod = StudioModId();
  g_game.race = raceId;
  g_game.raceName = raceName;
  g_game.map = map.path;
  g_game.nComputers = nComputers;
  g_game.computerRace = computerRace;
  g_game.command = command;
  g_game.folder = folder;
  HANDLE hWatch = NULL;
  DuplicateHandle(GetCurrentProcess(), pi.hProcess, GetCurrentProcess(), &hWatch, SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0);
  std::thread(Watch, g_game.shared, hWatch, nPort, key).detach();
  StudioLog("info", "playing " + raceName + " on " + map.name + " against " + std::to_string(nComputers) +
                        (nComputers == 1 ? " computer" : " computers") + ", process " + std::to_string(pi.dwProcessId) + ", port " +
                        std::to_string(nPort));
  return true;
}

// ---- the panel's state ------------------------------------------------------------------------------------

std::string g_mapPath;        // the map picked
int g_nComputers = 1;
std::string g_computerRace;   // "" as the game picks
std::map<std::string, std::pair<int, std::string>> g_mapInfo;  // map_info's players and title by path
std::set<std::string> g_mapAsked;
std::string g_said;           // what Play last said when it refused

void AskMapInfo(const std::string &path) {
  if (path.empty() || g_mapAsked.count(path))
    return;
  g_mapAsked.insert(path);
  DesignCall("map_info", "\"path\":" + Q(path), [path](const PortJson &a) {
    if (a["ok"].kind == PortJson::BOOL && a["ok"].b)
      g_mapInfo[path] = {static_cast<int>(a["players"].Num(0)), a["title"].Str()};
  });
}

int MapPlayers(const std::string &path, int nFromName) {
  const auto it = g_mapInfo.find(path);
  return it != g_mapInfo.end() && it->second.first > 0 ? it->second.first : nFromName;
}

void Heading(ImDrawList *d, const char *pszHead, const char *pszFine, float W) {
  ImGui::Dummy(ImVec2(1, P(14.0f)));
  const ImVec2 p = ImGui::GetCursorScreenPos();
  d->AddLine(ImVec2(p.x, p.y - P(6.0f)), ImVec2(p.x + W, p.y - P(6.0f)), C(TK_LINE));
  const float x = p.x + Wide(d, 10.5f, p.x, p.y + P(4.0f), C(TK_HEAD), pszHead) + P(12.0f);
  if (pszFine)
    T(d, STUDIO_FONT_TEXT, kStudioFsS, x, p.y + P(2.0f), C(TK_INK3), pszFine, p.x + W - x);
  ImGui::Dummy(ImVec2(W, P(26.0f)));
}

void Small(const char *psz) {
  StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_LABEL), psz);
}

// a label in a row of fields fH high, in its middle
void RowLabel(const char *psz, float fH) {
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float w = T(ImGui::GetWindowDrawList(), STUDIO_FONT_TEXT, kStudioFsXs, p.x, Mid(p.y, P(fH), kStudioFsXs), C(TK_LABEL), psz);
  ImGui::Dummy(ImVec2(w, P(fH)));
}

std::string UnitsInWords(const std::map<int, int> &units) {
  std::vector<std::pair<int, std::string>> list;
  for (const auto &u : units) {
    const std::string name = UnitName(u.first);
    list.push_back({u.second, std::to_string(u.second) + " " + (name.empty() ? "unit " + std::to_string(u.first) : StudioShowName(ST_UNITS, name))});
  }
  std::stable_sort(list.begin(), list.end(), [](const std::pair<int, std::string> &a, const std::pair<int, std::string> &b) { return a.first > b.first; });
  std::string s;
  for (const auto &e : list)
    s += (s.empty() ? "" : ", ") + e.second;
  return s.empty() ? std::string("no units") : s;
}

void GameView(ImDrawList *d, float W) {
  const std::shared_ptr<Shared> sh = g_game.shared;
  std::string state;
  bool bConnected, bExited, bPaused;
  unsigned nFrame;
  DWORD dwExit;
  std::vector<SlotSeen> slots;
  {
    std::lock_guard<std::mutex> lock(sh->m);
    state = sh->state;
    bConnected = sh->bConnected;
    bExited = sh->bExited;
    bPaused = sh->bPaused;
    nFrame = sh->nFrame;
    dwExit = sh->dwExit;
    slots = sh->slots;
  }
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const char *pszChip = bExited ? "ended" : !bConnected ? "starting" : state == "game" ? (bPaused ? "paused" : "playing") : "in the menus";
  const int kind = bExited ? CH_PLAIN : state == "game" ? (bPaused ? CH_WARN : CH_GOOD) : CH_PLAIN;
  const float cw = StudioChip(d, ImVec2(p.x, p.y + P(4.0f)), pszChip, kind);
  std::string line = g_game.raceName + " on " + g_game.map.substr(g_game.map.find_last_of("\\/") + 1);
  if (bExited)
    line += ", ended" + (dwExit != 0 && dwExit != 1 ? " with " + std::to_string(dwExit) : std::string());
  else if (state == "game")
    line += ", frame " + std::to_string(nFrame);
  line += ", process " + std::to_string(g_game.dwPid) + ", port " + std::to_string(g_game.nPort);
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, p.x + cw + P(8.0f), p.y + P(5.0f), C(TK_INK2), line, W - cw - P(8.0f));
  ImGui::Dummy(ImVec2(W, P(26.0f)));
  if (!bExited) {
    if (StudioButton("##rtstop", "Stop", "x", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs))
      Stop();
    if (state == "game" && bPaused) {
      ImGui::SameLine(0.0f, P(6.0f));
      if (StudioButton("##rtgo", "Go on", "play", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs)) {
        std::lock_guard<std::mutex> lock(sh->m);
        sh->outbox.push_back("resume");
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The game holds while its window has no focus");
    }
  }
  for (const SlotSeen &s : slots) {
    const ImVec2 q = ImGui::GetCursorScreenPos();
    const std::string who = s.nType == 2 ? "You, " + g_game.raceName
                                         : "Computer, " + (g_game.computerRace.empty() ? kBaseWords[s.nRace < 0 || s.nRace > 2 ? 0 : s.nRace] : g_game.computerRace) +
                                               " (" + s.name + ")";
    const float x = q.x + T(d, STUDIO_FONT_BOLD, kStudioFsS, q.x, q.y + P(2.0f), C(s.nType == 2 ? TK_GOOD : TK_INK), who) + P(10.0f);
    T(d, STUDIO_FONT_MONO, kStudioFsXs, x, q.y + P(3.0f), C(TK_INK3),
      std::to_string(s.nMinerals) + " minerals, " + std::to_string(s.nGas) + " gas, supply " + std::to_string(s.nSupplyUsed) + "/" +
          std::to_string(s.nSupplyMax));
    ImGui::Dummy(ImVec2(W, P(20.0f)));
    ImGui::SetCursorScreenPos(ImVec2(q.x + P(10.0f), ImGui::GetCursorScreenPos().y));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + W - P(10.0f));
    StudioPushFont(STUDIO_FONT_TEXT, kStudioFsXs);
    ImGui::PushStyleColor(ImGuiCol_Text, C(TK_INK2));
    ImGui::TextWrapped("%s", UnitsInWords(s.units).c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::PopTextWrapPos();
    ImGui::SetCursorScreenPos(ImVec2(q.x, ImGui::GetCursorScreenPos().y + P(4.0f)));
    ImGui::Dummy(ImVec2(W, 0.0f));
  }
}

}  // namespace

// ---- the contract (studio_ui.h) ------------------------------------------------------------------------------

void StudioRaceTestPanel(const std::string &race, float fW) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  ImGui::PushID("racetest");
  const float x0 = ImGui::GetCursorScreenPos().x;
  Heading(d, "Play it", "on a real map against computers, in a game of its own; the mod is saved first", fW);
  const std::vector<MapFile> &maps = Maps(false);
  if (g_mapPath.empty())
    for (const MapFile &m : maps)
      if (m.nPlayers >= 2) {
        g_mapPath = m.path;
        break;
      }
  MapFile now;
  const bool bMap = !g_mapPath.empty() && FindMap(g_mapPath, &now);
  AskMapInfo(bMap ? now.path : std::string());
  const int nPlayers = bMap ? MapPlayers(now.path, now.nPlayers) : 0;
  // the map, searched by name
  Small("Map");
  static char s_szFind[64];
  const std::string preview = bMap ? now.name + (nPlayers ? "   " + std::to_string(nPlayers) + " players" : std::string()) : "a map";
  const float mw = std::min(fW / P(1.0f), 420.0f);
  if (StudioSelectBegin("##rtmap", preview.c_str(), mw, 26.0f)) {
    if (ImGui::IsWindowAppearing()) {
      s_szFind[0] = '\0';
      ImGui::SetKeyboardFocusHere();
    }
    StudioInput("##rtfind", s_szFind, sizeof s_szFind, 380.0f, 24.0f, "Find a map", "search");
    ImGui::BeginChild("##rtlist", ImVec2(P(380.0f), P(320.0f)));
    std::vector<int> rows;
    for (size_t i = 0; i < maps.size(); ++i)
      if (Has(maps[i].name, s_szFind) || Has(maps[i].where, s_szFind))
        rows.push_back(static_cast<int>(i));
    ImGuiListClipper clip;
    clip.Begin(static_cast<int>(rows.size()), P(28.0f));
    while (clip.Step())
      for (int k = clip.DisplayStart; k < clip.DisplayEnd; ++k) {
        const MapFile &m = maps[static_cast<size_t>(rows[static_cast<size_t>(k)])];
        ImGui::PushID(rows[static_cast<size_t>(k)]);
        const std::string players = m.nPlayers ? std::to_string(m.nPlayers) + " players" : std::string();
        if (StudioItem(m.name.c_str(), m.path == g_mapPath, players.empty() ? nullptr : players.c_str()))
          g_mapPath = m.path;
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("%s", m.path.c_str());
        ImGui::PopID();
      }
    if (rows.empty())
      StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK3), maps.empty() ? "No maps: the studio is given none (--maps)." : "None by that name.");
    ImGui::EndChild();
    StudioSelectEnd();
  }
  ImGui::SameLine(0.0f, P(6.0f));
  if (StudioButton("##rtfresh", "", "again", SB_FLAT, 26.0f, 26.0f))
    Maps(true);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Look for maps again");
  if (bMap) {
    const auto it = g_mapInfo.find(now.path);
    const std::string where = (it != g_mapInfo.end() && !it->second.second.empty() ? it->second.second + ", " : std::string()) + now.where;
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), where.c_str());
  }
  ImGui::Dummy(ImVec2(1, P(4.0f)));
  // the computers, how many and their race
  RowLabel("Computers", 24.0f);
  ImGui::SameLine(0.0f, P(8.0f));
  const int nMost = nPlayers > 1 ? nPlayers - 1 : 7;
  g_nComputers = std::max(1, std::min(g_nComputers, nMost));
  double dn = g_nComputers;
  if (StudioNumber("##rtcomp", &dn, 48.0f, 24.0f, true, 12.0f))
    g_nComputers = std::max(1, std::min(nMost, static_cast<int>(dn)));
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Up to %d on this map", nMost);
  ImGui::SameLine(0.0f, P(14.0f));
  RowLabel("their race", 24.0f);
  ImGui::SameLine(0.0f, P(8.0f));
  if (StudioSelectBegin("##rtcrace", g_computerRace.empty() ? "as the game picks" : g_computerRace.c_str(), 200.0f, 24.0f)) {
    if (StudioItem("as the game picks", g_computerRace.empty()))
      g_computerRace.clear();
    for (const char *psz : {"Terran", "Zerg", "Protoss", "Random"})
      if (StudioItem(psz, Lower(g_computerRace) == Lower(psz)))
        g_computerRace = psz;
    StudioItemSeparator();
    for (const PortJsonMember &m : StudioDoc("types")["races"].members) {
      const std::string name = m.second["name"].Str(m.first.c_str());
      if (StudioItem((name + "  (mod)").c_str(), g_computerRace == name))
        g_computerRace = name;
    }
    StudioSelectEnd();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("A computer of the mod's race plays its base's scripts with the race's stand-ins");
  ImGui::Dummy(ImVec2(1, P(4.0f)));
  const std::string exe = FindHyperion();
  const bool bRun = Running();
  std::string raceId, raceName;
  ModRace(race, &raceId, &raceName);
  if (StudioButton("##rtplay", bRun ? "Play again" : "Play", "play", SB_GOOD, 28.0f, 0.0f, 13.0f, bMap && !exe.empty() && StudioIsOpen())) {
    std::string why;
    g_said = Play(race, g_mapPath, g_nComputers, g_computerRace, std::string(), &why) ? std::string() : why;
    if (!g_said.empty())
      StudioSay("Play: " + g_said);
  }
  ImGui::SameLine(0.0f, P(10.0f));
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const std::string fine = exe.empty() ? "Hyperion.exe is neither beside the studio nor in the engine's x64 build."
                           : !g_said.empty() ? g_said
                                             : "--race " + Quoted(raceName.empty() ? race : raceName) + ": its start units are what the game starts it with.";
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, p.x, p.y + P(7.0f), exe.empty() || !g_said.empty() ? C(TK_BAD) : C(TK_INK3), fine,
    x0 + fW - p.x);
  ImGui::Dummy(ImVec2(1, P(28.0f)));
  if (g_game.shared && g_game.race == raceId) {
    ImGui::Dummy(ImVec2(1, P(4.0f)));
    GameView(d, fW);
  }
  ImGui::PopID();
}

// ---- the commands ---------------------------------------------------------------------------------------------

namespace {

std::string GameJson(void) {
  if (!g_game.shared)
    return "\"running\":false,\"state\":\"none\"";
  const std::shared_ptr<Shared> sh = g_game.shared;
  std::lock_guard<std::mutex> lock(sh->m);
  std::string players;
  for (const SlotSeen &s : sh->slots) {
    std::string units;
    for (const auto &u : s.units) {
      const std::string name = UnitName(u.first);
      units += std::string(units.empty() ? "" : ",") + "{\"type\":" + std::to_string(u.first) + ",\"name\":" + Q(name) + ",\"show\":" +
               Q(name.empty() ? std::string() : StudioShowName(ST_UNITS, name)) + ",\"count\":" + std::to_string(u.second) + "}";
    }
    players += std::string(players.empty() ? "" : ",") + "{\"slot\":" + std::to_string(s.nSlot) + ",\"who\":" +
               Q(s.nType == 2 ? "you" : "computer") + ",\"race\":" + Q(s.nType == 2 ? g_game.raceName : g_game.computerRace) +
               ",\"base\":" + std::to_string(s.nRace) + ",\"minerals\":" + std::to_string(s.nMinerals) + ",\"gas\":" + std::to_string(s.nGas) +
               ",\"supply\":[" + std::to_string(s.nSupplyUsed) + "," + std::to_string(s.nSupplyMax) + "],\"count\":" + std::to_string(s.nUnits) +
               ",\"units\":[" + units + "]}";
  }
  return std::string("\"running\":") + (sh->bExited ? "false" : "true") + ",\"connected\":" + (sh->bConnected ? "true" : "false") +
         ",\"state\":" + Q(sh->bExited ? "ended" : sh->bConnected ? sh->state : "starting") + ",\"frame\":" + std::to_string(sh->nFrame) +
         ",\"paused\":" + (sh->bPaused ? "true" : "false") + ",\"pid\":" + std::to_string(g_game.dwPid) + ",\"port\":" +
         std::to_string(g_game.nPort) + ",\"mod\":" + Q(g_game.mod) + ",\"race\":" + Q(g_game.raceName) + ",\"map\":" + Q(g_game.map) +
         ",\"computers\":" + std::to_string(g_game.nComputers) + ",\"computer_race\":" + Q(g_game.computerRace) +
         ",\"exit_code\":" + (sh->bExited ? std::to_string(sh->dwExit) : std::string("null")) +
         ",\"folder\":" + Q(g_game.folder) + ",\"command\":" + Q(g_game.command) + ",\"players\":[" + players + "]";
}

// studio_race_play {race, map, computers?, computer_race?}: the mod saved, then Hyperion.exe
// started on the map, the player of the race (one of the mod's, or a classic one) against
// computers (1 unless given) of computer_race (the game's pick unless given); any game it started
// before is ended first. The answer comes once the process runs; studio_race_game follows it.
bool CmdPlay(const PortJson &j, std::string *pAnswer) {
  const int nComputers = static_cast<int>(j["computers"].Num(1));
  if (nComputers < 1 || nComputers > 63) {
    *pAnswer = "computers is 1 to 63";
    return false;
  }
  std::string why;
  if (!Play(j["race"].Str(), j["map"].Str(), nComputers, j["computer_race"].Str(), j["extra"].Str(), &why)) {
    *pAnswer = why;
    return false;
  }
  g_mapPath = g_game.map;
  g_nComputers = nComputers;
  g_computerRace = g_game.computerRace;
  *pAnswer = GameJson();
  return true;
}

// studio_race_game {resume?}: the game's state as its channel last said it, twice a second: its
// frame, and each player's resources and units by type; with resume, a game holding while its
// window has no focus goes on.
bool CmdGame(const PortJson &j, std::string *pAnswer) {
  if (j["resume"].kind == PortJson::BOOL && j["resume"].b && g_game.shared) {
    std::lock_guard<std::mutex> lock(g_game.shared->m);
    g_game.shared->outbox.push_back("resume");
  }
  *pAnswer = GameJson();
  return true;
}

// studio_race_stop {}: the game the studio started ended, by its process.
bool CmdStop(const PortJson &, std::string *pAnswer) {
  const bool bWas = Running();
  Stop();
  *pAnswer = std::string("\"stopped\":") + (bWas ? "true" : "false") + "," + GameJson();
  return true;
}

}  // namespace

bool StudioPlayWith(const std::string &race, const std::string &map, int nComputers, const std::string &computerRace,
                    const std::string &extra, std::string *pWhy) {
  if (!Play(race, map, nComputers < 1 ? 1 : nComputers, computerRace, extra, pWhy))
    return false;
  g_mapPath = g_game.map;
  g_nComputers = g_game.nComputers;
  g_computerRace = g_game.computerRace;
  return true;
}

void StudioTraceWant(bool bOn) {
  if (g_game.shared == nullptr)
    return;
  std::lock_guard<std::mutex> lock(g_game.shared->m);
  g_game.shared->bTrace = bOn;
}

bool StudioTraceRows(std::string *pRows) {
  pRows->clear();
  if (g_game.shared == nullptr)
    return false;
  std::lock_guard<std::mutex> lock(g_game.shared->m);
  pRows->swap(g_game.shared->aiRows);
  return !pRows->empty();
}

const char *StudioPlayingWith(int *pnPort) {
  if (g_game.hProcess == NULL)
    return nullptr;
  if (pnPort != nullptr)
    *pnPort = g_game.nPort;
  return g_game.map.c_str();
}

void StudioRaceTestInit(void) {
  PortControlCommand("studio_race_play", CmdPlay);
  PortControlCommand("studio_race_game", CmdGame);
  PortControlCommand("studio_race_stop", CmdStop);
}
