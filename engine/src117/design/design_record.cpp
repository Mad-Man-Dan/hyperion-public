// design_record.cpp -- clips of the game to share (design_record): a square of the map round a unit
// or a point, or the whole view, taken frame by frame as the window is drawn, without the HUD,
// the menus, the panels, the cursor or the selection's marks. A clip loops: its end fades into its start. ffmpeg, found on the PATH,
// makes it a GIF, a WebM or an MP4; without it, or asked for png, the frames stay as PNGs.
#define NOMINMAX
#include <windows.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <SDL3/SDL.h>
#include "imgui.h"
#include "CUnit.h"
#include "CSprite.h"
#include "../port/platform/port_json.h"
#include "../port/render/port_overlay.h"    // PORT_OVERLAY_LAYER
#include "../port/render/port_selection.h"  // PORT_SELECTION_LOOK
#include "../port/render/port_view.h"       // PortViewCameraMiddle
#include "../port/tools/port_benchinfo.h"   // PortBenchSelected, PortBenchUnitRead
#include "../port/tools/port_control.h"     // PortControlCommand
#include "design_engine.h"
#include "design_hooks.h"
#include "design_ui.h"

namespace {

enum State { REC_NONE, REC_TAKING, REC_MAKING, REC_DONE, REC_FAILED };
const char *const kStates[] = {"none", "taking", "making", "done", "failed"};
enum Around { AROUND_UNIT, AROUND_POINT, AROUND_VIEW };
const char *const kArounds[] = {"unit", "point", "view"};
const char *const kFormats[] = {"gif", "webm", "mp4", "png"};
const int kFormatCount = 4;
const double kWarmMs = 200.0;        // before the first frame: the camera there, the marks and console gone
const int kSideMax = 2048;           // a clip's side at most, in its pixels
const double kBytesMax = 1.5e9;      // the frames held at once at most

struct Clip {
  double fSeconds = 4.0, fFps = 25.0, fFade = 0.5;
  bool bLoop = true, bMarks = false;
  int nAround = AROUND_VIEW;
  int nUnit = -1, nX = 0, nY = 0;  // the unit followed, or the map point
  int nSize = 192;                 // the square's side in map pixels
  double fScale = 0.0;             // the clip's pixels to a map pixel; 0 as the window shows them
  int nFormat = 0;
  std::string name;
};

struct Frame {
  double fMs;                       // from the clip's start
  std::vector<unsigned char> rgb;   // the clip's size, 3 bytes a pixel
};

Clip g_clip;
std::atomic<int> g_nState{REC_NONE};
std::vector<Frame> g_frames;
int g_nW, g_nH;                     // the clip's size, set by its first region
int g_nNext, g_nNeed, g_nMissed;
double g_fWarmMs, g_fStartMs, g_fTakeMs;  // g_fWarmMs: 0 until the first frame asks
int g_nLastX, g_nLastY;             // the unit's last place, kept if it dies
std::mutex g_lock;                  // the strings the maker writes
std::string g_path, g_why;
std::atomic<int> g_nMade{0};        // frames written by the maker

double NowMs(void) {
  return static_cast<double>(SDL_GetTicksNS()) / 1.0e6;
}

double StepMs(void) {
  return 1000.0 / g_clip.fFps;
}

int Frames(double fSeconds) {
  return std::max(1, static_cast<int>(lround(fSeconds * g_clip.fFps)));
}

// the frames the clip shows, and the ones taken past them to fade its end into its start
int Shown(void) {
  return Frames(g_clip.fSeconds);
}

int Fade(void) {
  return g_clip.bLoop ? std::min(Shown() / 2, static_cast<int>(lround(g_clip.fFade * g_clip.fFps))) : 0;
}

// a selection look: no marks while a clip is taken, unless it asked for them
bool ClipMarksShown(int nMark, int nOwner, bool bShown) {
  (void)nMark;
  (void)nOwner;
  return bShown && (g_nState != REC_TAKING || g_clip.bMarks);
}
const PortSelectionLook kClipMarks = {"design clip", ClipMarksShown};
PORT_SELECTION_LOOK(kClipMarks);

// a layer that draws nothing: while it shows, the game draws no cursor into its frame and shows no
// console, the map taking the view's whole height
bool ClipTaking(void) {
  return g_nState == REC_TAKING;
}
const PortOverlayLayer kClipLayer = {"design clip", PORT_OVERLAY_CURSOR - 10,
                                     PORT_OVERLAY_HIDES_CURSOR | PORT_OVERLAY_HIDES_CONSOLE, ClipTaking};
PORT_OVERLAY_LAYER(kClipLayer);

void Fail(const std::string &why) {
  {
    std::lock_guard<std::mutex> hold(g_lock);
    g_why = why;
  }
  g_frames.clear();
  g_frames.shrink_to_fit();
  g_nState = REC_FAILED;
}

std::string Clean(const std::string &name) {
  std::string s;
  for (char c : name)
    if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')
      s += c;
    else if (c == ' ' && !s.empty() && s.back() != '-')
      s += '-';
  return s.empty() ? "clip" : s.substr(0, 48);
}

// ffmpeg's path on the PATH, empty when it is not there
std::string FindFfmpeg(void) {
  char sz[MAX_PATH] = "";
  return SearchPathA(NULL, "ffmpeg.exe", NULL, MAX_PATH, sz, NULL) != 0 ? std::string(sz) : std::string();
}

// ffmpeg run on the frames, its messages in log; true when it made the clip
bool RunFfmpeg(const std::string &exe, const std::string &args, const std::string &log) {
  SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
  HANDLE hLog = CreateFileA(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  STARTUPINFOA si;
  ZeroMemory(&si, sizeof si);
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = NULL;
  si.hStdOutput = hLog;
  si.hStdError = hLog;
  PROCESS_INFORMATION pi;
  std::string line = "\"" + exe + "\" " + args;
  std::vector<char> cmd(line.begin(), line.end());
  cmd.push_back('\0');
  const BOOL bRan = CreateProcessA(NULL, cmd.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
  DWORD dwCode = 1;
  if (bRan) {
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &dwCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
  if (hLog != INVALID_HANDLE_VALUE)
    CloseHandle(hLog);
  return bRan && dwCode == 0;
}

// The maker, on a thread of its own: the frame nearest each of the clip's times, the end faded
// into the start, written as PNGs and handed to ffmpeg.
void Make(Clip clip, std::vector<Frame> frames, int nW, int nH, std::string base) {
  const double fStep = 1000.0 / clip.fFps;
  const int nShown = std::max(1, static_cast<int>(lround(clip.fSeconds * clip.fFps)));
  const int nFade = clip.bLoop ? std::min(nShown / 2, static_cast<int>(lround(clip.fFade * clip.fFps))) : 0;
  std::vector<const Frame *> at(static_cast<size_t>(nShown + nFade));
  size_t k = 0;
  for (size_t j = 0; j < at.size(); ++j) {
    const double t = j * fStep;
    while (k + 1 < frames.size() && fabs(frames[k + 1].fMs - t) <= fabs(frames[k].fMs - t))
      ++k;
    at[j] = &frames[k];
  }
  const std::string dir = base + "-frames\\";
  CreateDirectoryA(dir.c_str(), NULL);
  std::vector<unsigned char> mixed(static_cast<size_t>(nW) * nH * 3);
  for (int j = 0; j < nShown; ++j) {
    const unsigned char *pOut = at[j]->rgb.data();
    if (j < nFade) {
      // the frames past the end over the start, going: the last frame shown runs on into the first
      const float w = static_cast<float>(j) / nFade;
      const unsigned char *pHead = at[j]->rgb.data();
      const unsigned char *pTail = at[nShown + j]->rgb.data();
      for (size_t i = 0; i < mixed.size(); ++i)
        mixed[i] = static_cast<unsigned char>(pHead[i] * w + pTail[i] * (1.0f - w) + 0.5f);
      pOut = mixed.data();
    }
    char sz[32];
    snprintf(sz, sizeof sz, "f%05d.png", j);
    SDL_Surface *pSurface = SDL_CreateSurfaceFrom(nW, nH, SDL_PIXELFORMAT_RGB24, const_cast<unsigned char *>(pOut), nW * 3);
    const bool bSaved = pSurface != NULL && SDL_SavePNG(pSurface, (dir + sz).c_str());
    SDL_DestroySurface(pSurface);
    if (!bSaved) {
      std::lock_guard<std::mutex> hold(g_lock);
      g_why = "a frame could not be written to " + dir;
      g_nState = REC_FAILED;
      return;
    }
    g_nMade = j + 1;
  }
  frames.clear();
  const char *pszFormat = kFormats[clip.nFormat];
  std::string path = dir, why;
  bool bOk = true;
  if (strcmp(pszFormat, "png") != 0) {
    const std::string exe = FindFfmpeg();
    if (exe.empty()) {
      why = "ffmpeg is not on the PATH, so the clip is left as PNG frames";
    } else {
      char szRate[32];
      snprintf(szRate, sizeof szRate, "%g", clip.fFps);
      const std::string out = base + "." + pszFormat;
      std::string args = std::string("-nostdin -y -v error -framerate ") + szRate + " -i \"" + dir + "f%05d.png\" ";
      if (strcmp(pszFormat, "gif") == 0)
        args += "-vf \"split[a][b];[a]palettegen=stats_mode=full[p];[b][p]paletteuse=dither=bayer:bayer_scale=3\" -loop 0 ";
      else if (strcmp(pszFormat, "webm") == 0)
        args += "-c:v libvpx-vp9 -crf 18 -b:v 0 -pix_fmt yuv420p -row-mt 1 ";
      else
        args += "-c:v libx264 -preset slow -crf 14 -pix_fmt yuv420p -movflags +faststart ";
      args += "\"" + out + "\"";
      if (RunFfmpeg(exe, args, base + ".log")) {
        path = out;
        DeleteFileA((base + ".log").c_str());
        for (int j = 0; j < nShown; ++j) {
          char sz[32];
          snprintf(sz, sizeof sz, "f%05d.png", j);
          DeleteFileA((dir + sz).c_str());
        }
        RemoveDirectoryA(dir.c_str());
      } else {
        why = "ffmpeg failed; its messages are in " + base + ".log, the frames in " + dir;
        bOk = false;
      }
    }
  }
  std::lock_guard<std::mutex> hold(g_lock);
  g_path = path;
  g_why = why;
  g_nState = bOk ? REC_DONE : REC_FAILED;
}

void StartMaking(void) {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char sz[48];
  snprintf(sz, sizeof sz, "-%04d%02d%02d-%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  const std::string folder = DesignDataFolder() + "clips\\";
  CreateDirectoryA(folder.c_str(), NULL);
  const std::string base = folder + Clean(g_clip.name) + sz;
  g_nMade = 0;
  g_nState = REC_MAKING;
  std::thread(Make, g_clip, std::move(g_frames), g_nW, g_nH, base).detach();
  g_frames = std::vector<Frame>();
}

std::string StateJson(void) {
  std::lock_guard<std::mutex> hold(g_lock);
  const int nState = g_nState;
  return std::string("\"state\":\"") + kStates[nState] + "\",\"taken\":" + std::to_string(g_frames.size()) +
         ",\"of\":" + std::to_string(g_nNeed) + ",\"made\":" + std::to_string(g_nMade.load()) + ",\"frames\":" +
         std::to_string(Shown()) + ",\"width\":" + std::to_string(g_nW) + ",\"height\":" + std::to_string(g_nH) +
         ",\"fps\":" + std::to_string(g_clip.fFps) + ",\"seconds\":" + std::to_string(g_clip.fSeconds) +
         ",\"format\":\"" + kFormats[g_clip.nFormat] + "\",\"around\":\"" + kArounds[g_clip.nAround] +
         "\",\"missed\":" + std::to_string(g_nMissed) + ",\"path\":" + PortJsonQuote(g_path) + ",\"why\":" +
         PortJsonQuote(g_why);
}

bool Flag(const PortJson &j, const char *pszName, bool bDefault) {
  return j[pszName].kind == PortJson::BOOL ? j[pszName].b : bDefault;
}

// design_record {seconds?, fps?, loop?, fade?, around?, unit?, x?, y?, size?, scale?, marks?,
// format?, name?, centre?}: a clip begun. around is "unit" (the one given, else the first
// selected), "point" (x, y in map pixels) or "view"; with neither a unit nor a point it is the view.
bool CmdRecord(const PortJson &j, std::string *pAnswer) {
  if (g_nState == REC_TAKING || g_nState == REC_MAKING) {
    *pAnswer = "a clip is being taken or made; design_record_stop stops one being taken";
    return false;
  }
  if (!DesignInGame()) {
    *pAnswer = "a clip is taken of a game; start one first";
    return false;
  }
  Clip c;
  c.fSeconds = SDL_clamp(j["seconds"].Num(4.0), 0.5, 30.0);
  c.fFps = SDL_clamp(j["fps"].Num(25.0), 5.0, 60.0);
  c.bLoop = Flag(j, "loop", true);
  c.fFade = SDL_clamp(j["fade"].Num(0.5), 0.0, c.fSeconds / 2);
  c.nSize = SDL_clamp(static_cast<int>(j["size"].Num(192)), 32, 1024);
  c.fScale = SDL_clamp(j["scale"].Num(0.0), 0.0, 8.0);
  c.bMarks = Flag(j, "marks", false);
  c.name = j["name"].Str("clip");
  const std::string format = j["format"].Str("gif");
  c.nFormat = -1;
  for (int i = 0; i < kFormatCount; ++i)
    if (format == kFormats[i])
      c.nFormat = i;
  if (c.nFormat < 0) {
    *pAnswer = "format is gif, webm, mp4 or png";
    return false;
  }
  const std::string around = j["around"].Str("");
  PortBenchUnit unit;
  if (around == "view") {
    c.nAround = AROUND_VIEW;
  } else if (around == "point" || (around.empty() && j["x"].kind == PortJson::NUMBER)) {
    c.nAround = AROUND_POINT;
    c.nX = static_cast<int>(j["x"].Num(0));
    c.nY = static_cast<int>(j["y"].Num(0));
  } else if (j["unit"].kind == PortJson::NUMBER) {
    c.nAround = AROUND_UNIT;
    c.nUnit = static_cast<int>(j["unit"].Num(-1));
    if (!PortBenchUnitRead(c.nUnit, &unit)) {
      *pAnswer = "no unit " + std::to_string(c.nUnit) + " is alive";
      return false;
    }
  } else if (PortBenchSelected(&unit, 1) == 1) {
    c.nAround = AROUND_UNIT;
    c.nUnit = unit.nIndex;
  } else if (around == "unit") {
    *pAnswer = "select a unit first, or give one";
    return false;
  }
  if (c.nAround == AROUND_UNIT) {
    const CSprite *pSprite = CUnit::Head()[c.nUnit].pSprite;
    if (pSprite == NULL) {
      *pAnswer = "that unit has no sprite";
      return false;
    }
    c.nX = static_cast<int>(pSprite->wPosX);
    c.nY = static_cast<int>(pSprite->wPosY);
  }
  if (c.nAround != AROUND_VIEW && Flag(j, "centre", true))
    DesignCall("camera", "\"x\":" + std::to_string(c.nX) + ",\"y\":" + std::to_string(c.nY));
  g_clip = c;
  g_frames.clear();
  g_nW = g_nH = 0;
  g_fWarmMs = 0.0;
  g_nNext = 0;
  g_nMade = 0;
  g_nMissed = 0;
  g_nNeed = Shown() + Fade();
  g_nLastX = c.nX;
  g_nLastY = c.nY;
  {
    std::lock_guard<std::mutex> hold(g_lock);
    g_path.clear();
    g_why.clear();
  }
  g_nState = REC_TAKING;
  *pAnswer = StateJson();
  return true;
}

// design_recorded: how the clip goes, and where it is once made.
bool CmdRecorded(const PortJson &, std::string *pAnswer) {
  *pAnswer = StateJson();
  return true;
}

// design_record_stop: the clip being taken dropped.
bool CmdRecordStop(const PortJson &, std::string *pAnswer) {
  if (g_nState == REC_TAKING) {
    g_frames.clear();
    g_frames.shrink_to_fit();
    g_nState = REC_NONE;
  }
  *pAnswer = StateJson();
  return true;
}

// ---- the panel ----------------------------------------------------------------------------------

Clip g_ask;
int g_nAskAround = 0;  // 0 the selected unit, 1 the view's middle, 2 the whole view
std::string g_said;

std::string AskFields(void) {
  char sz[256];
  snprintf(sz, sizeof sz, "\"seconds\":%g,\"fps\":%g,\"loop\":%s,\"fade\":%g,\"size\":%d,\"scale\":%g,\"marks\":%s,",
           g_ask.fSeconds, g_ask.fFps, g_ask.bLoop ? "true" : "false", g_ask.fFade, g_ask.nSize, g_ask.fScale,
           g_ask.bMarks ? "true" : "false");
  std::string s = std::string(sz) + "\"format\":\"" + kFormats[g_ask.nFormat] + "\",\"name\":" +
                  PortJsonQuote(g_ask.name.empty() ? "clip" : g_ask.name);
  if (g_nAskAround == 0) {
    s += ",\"around\":\"unit\"";
  } else if (g_nAskAround == 1) {
    double fX = 0, fY = 0;
    PortViewCameraMiddle(&fX, &fY);
    s += ",\"around\":\"point\",\"x\":" + std::to_string(static_cast<int>(fX)) + ",\"y\":" +
         std::to_string(static_cast<int>(fY)) + ",\"centre\":false";
  } else {
    s += ",\"around\":\"view\"";
  }
  return s;
}

void Show(const std::string &path, bool bFolder) {
  if (bFolder)
    ShellExecuteA(NULL, "open", "explorer.exe", ("/select,\"" + path + "\"").c_str(), NULL, SW_SHOWNORMAL);
  else
    ShellExecuteA(NULL, "open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

}  // namespace

void DesignRecordInit(void) {
  PortControlCommand("design_record", CmdRecord);
  PortControlCommand("design_recorded", CmdRecorded);
  PortControlCommand("design_record_stop", CmdRecordStop);
  g_ask.name = "clip";
}

bool DesignClipDue(void) {
  if (g_nState != REC_TAKING)
    return false;
  const double fNow = NowMs();
  if (g_fWarmMs == 0.0)
    g_fWarmMs = fNow + kWarmMs;
  if (fNow < g_fWarmMs)
    return false;
  if (g_nNext == 0)
    g_fStartMs = fNow;
  // taken at the first frame drawn near each time due, a tenth of a step early at most
  if (fNow < g_fStartMs + (g_nNext - 0.1) * StepMs())
    return false;
  g_fTakeMs = fNow - g_fStartMs;
  g_nNext = static_cast<int>(g_fTakeMs / StepMs() + 0.1) + 1;
  return true;
}

bool DesignClipRegion(const SDL_FRect &box, int nGameW, int nGameH, int nWinW, int nWinH, int *pnX, int *pnY,
                      int *pnW, int *pnH) {
  DesignCamera cam;
  if (!DesignCameraNow(box, nGameW, nGameH, &cam))
    return false;
  float x0 = std::max(0.0f, cam.afMap[0]), y0 = std::max(0.0f, cam.afMap[1]);
  float x1 = std::min(static_cast<float>(nWinW), cam.afMap[2]), y1 = std::min(static_cast<float>(nWinH), cam.afMap[3]);
  if (x1 - x0 < 2.0f || y1 - y0 < 2.0f)
    return false;
  float fX = x0, fY = y0, fW = x1 - x0, fH = y1 - y0;
  if (g_clip.nAround != AROUND_VIEW) {
    if (g_clip.nAround == AROUND_UNIT) {
      PortBenchUnit unit;
      const CSprite *pSprite = PortBenchUnitRead(g_clip.nUnit, &unit) ? CUnit::Head()[g_clip.nUnit].pSprite : NULL;
      if (pSprite != NULL) {
        g_nLastX = static_cast<int>(pSprite->wPosX);
        g_nLastY = static_cast<int>(pSprite->wPosY);
      }
    }
    float fCx = 0, fCy = 0;
    DesignProject(cam, g_clip.nAround == AROUND_UNIT ? g_nLastX : g_clip.nX,
                  g_clip.nAround == AROUND_UNIT ? g_nLastY : g_clip.nY, 0.0, &fCx, &fCy);
    // the square kept whole within the map's part of the window, moved in from its edges
    const float fSide = g_clip.nSize * cam.fPerMap;
    fW = std::min(fSide, x1 - x0);
    fH = std::min(fSide, y1 - y0);
    fX = SDL_clamp(fCx - fW / 2, x0, x1 - fW);
    fY = SDL_clamp(fCy - fH / 2, y0, y1 - fH);
  }
  *pnX = static_cast<int>(fX);
  *pnY = static_cast<int>(fY);
  *pnW = std::max(2, std::min(static_cast<int>(fW), nWinW - *pnX));
  *pnH = std::max(2, std::min(static_cast<int>(fH), nWinH - *pnY));
  if (g_nW == 0) {
    // the clip's size, from its first frame: as the window shows it, or at the scale asked
    double fW2 = *pnW, fH2 = *pnH;
    if (g_clip.fScale > 0.0) {
      fW2 = fW / cam.fPerMap * g_clip.fScale;
      fH2 = fH / cam.fPerMap * g_clip.fScale;
    }
    const double fFit = std::min(1.0, kSideMax / std::max(fW2, fH2));
    g_nW = std::max(2, static_cast<int>(fW2 * fFit) & ~1);  // even, as the MP4 wants
    g_nH = std::max(2, static_cast<int>(fH2 * fFit) & ~1);
    if (static_cast<double>(g_nW) * g_nH * 3.0 * g_nNeed > kBytesMax) {
      Fail("the clip is too long or too large to hold; make it shorter or smaller");
      return false;
    }
  }
  return true;
}

void DesignClipTake(const void *pPixels, int nWidth, int nHeight, bool bBgra) {
  if (g_nState != REC_TAKING)
    return;
  if (pPixels == NULL || nWidth <= 0 || nHeight <= 0) {
    ++g_nMissed;  // its time is shown by its neighbour
    return;
  }
  Frame f;
  f.fMs = g_fTakeMs;
  f.rgb.resize(static_cast<size_t>(g_nW) * g_nH * 3);
  const unsigned char *pIn = static_cast<const unsigned char *>(pPixels);
  const int nR = bBgra ? 2 : 0, nB = bBgra ? 0 : 2;
  unsigned char *pOut = f.rgb.data();
  for (int y = 0; y < g_nH; ++y) {
    const int sy = std::min(nHeight - 1, static_cast<int>((y + 0.5) * nHeight / g_nH));
    const unsigned char *pRow = pIn + static_cast<size_t>(sy) * nWidth * 4;
    for (int x = 0; x < g_nW; ++x) {
      const int sx = std::min(nWidth - 1, static_cast<int>((x + 0.5) * nWidth / g_nW));
      const unsigned char *p = pRow + sx * 4;
      *pOut++ = p[nR];
      *pOut++ = p[1];
      *pOut++ = p[nB];
    }
  }
  g_frames.push_back(std::move(f));
  if (static_cast<int>(g_frames.size()) >= g_nNeed || g_fTakeMs >= (g_nNeed - 1) * StepMs() - 0.1 * StepMs())
    StartMaking();
}

void DesignRecordPanel(void) {
  ImGui::TextWrapped("A looping clip of the game to share, without the HUD, the console, the cursor or the "
                     "selection's marks.");
  ImGui::RadioButton("The selected unit", &g_nAskAround, 0);
  ImGui::SameLine();
  ImGui::RadioButton("The view's middle", &g_nAskAround, 1);
  ImGui::SameLine();
  ImGui::RadioButton("The whole view", &g_nAskAround, 2);
  float fSeconds = static_cast<float>(g_ask.fSeconds), fFps = static_cast<float>(g_ask.fFps);
  float fFade = static_cast<float>(g_ask.fFade), fScale = static_cast<float>(g_ask.fScale);
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
  if (ImGui::SliderFloat("Seconds", &fSeconds, 0.5f, 30.0f, "%.1f"))
    g_ask.fSeconds = fSeconds;
  ImGui::SameLine();
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
  if (ImGui::SliderFloat("Fps", &fFps, 5.0f, 60.0f, "%.0f"))
    g_ask.fFps = fFps;
  ImGui::Checkbox("Loop", &g_ask.bLoop);
  ImGui::SameLine();
  ImGui::BeginDisabled(!g_ask.bLoop);
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
  if (ImGui::SliderFloat("Fade the end in (s)", &fFade, 0.0f, 2.0f, "%.2f"))
    g_ask.fFade = fFade;
  ImGui::EndDisabled();
  ImGui::BeginDisabled(g_nAskAround == 2);
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
  ImGui::SliderInt("Square (map px)", &g_ask.nSize, 32, 1024);
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
  if (ImGui::SliderFloat("Scale", &fScale, 0.0f, 8.0f, fScale <= 0.0f ? "as shown" : "%.1f x"))
    g_ask.fScale = fScale;
  ImGui::SetNextItemWidth(120);
  ImGui::Combo("Format", &g_ask.nFormat, "GIF\0WebM\0MP4\0PNG frames\0");
  ImGui::SameLine();
  ImGui::Checkbox("Selection marks", &g_ask.bMarks);
  char szName[64];
  snprintf(szName, sizeof szName, "%s", g_ask.name.c_str());
  ImGui::SetNextItemWidth(200);
  if (ImGui::InputText("Name", szName, sizeof szName))
    g_ask.name = szName;

  const int nState = g_nState;
  if (nState == REC_TAKING) {
    if (ImGui::Button("Stop"))
      DesignCall("design_record_stop", "");
    ImGui::SameLine();
    char sz[48];
    snprintf(sz, sizeof sz, "taking %d of %d", static_cast<int>(g_frames.size()), g_nNeed);
    ImGui::ProgressBar(g_nNeed > 0 ? static_cast<float>(g_frames.size()) / g_nNeed : 0.0f, ImVec2(-1, 0), sz);
  } else if (nState == REC_MAKING) {
    char sz[48];
    snprintf(sz, sizeof sz, "making: %d of %d frames", g_nMade.load(), Shown());
    ImGui::ProgressBar(Shown() > 0 ? static_cast<float>(g_nMade.load()) / Shown() : 0.0f, ImVec2(-1, 0), sz);
  } else {
    ImGui::BeginDisabled(!DesignInGame());
    if (ImGui::Button("Record"))
      DesignCall("design_record", AskFields(), [](const PortJson &j) { g_said = j["ok"].b ? "" : j["error"].Str(); });
    ImGui::EndDisabled();
    std::string path, why;
    {
      std::lock_guard<std::mutex> hold(g_lock);
      path = g_path;
      why = g_why;
    }
    if (nState == REC_DONE && !path.empty()) {
      ImGui::SameLine();
      if (ImGui::Button("Open"))
        Show(path, false);
      ImGui::SameLine();
      if (ImGui::Button("Show in folder"))
        Show(path, true);
      ImGui::TextWrapped("%d x %d, %d frames: %s", g_nW, g_nH, Shown(), path.c_str());
    }
    if (!why.empty())
      ImGui::TextWrapped("%s", why.c_str());
    if (!g_said.empty())
      ImGui::TextWrapped("%s", g_said.c_str());
  }
}
