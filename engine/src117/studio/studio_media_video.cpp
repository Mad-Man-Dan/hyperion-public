// studio_media_video.cpp -- the pictures the studio's pages show from the game's files: a portrait's
// or a cinematic's video, played in a loop without its sound, and a console image, a PCX of 8 bits
// a pixel in the game's palette. Each is decoded by the engine's own readers (port_smacker.h for
// Smacker, port_video.h for the rest) into a texture of the panels' (ImGui's user textures), kept
// while a page draws it and let go a few seconds after.
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <SDL3/SDL.h>  // SDL_free
#include "imgui.h"
#include "imgui_internal.h"  // RegisterUserTexture, ImTextureDataQueueUpload
#include "../port/data/port_smacker.h"
#include "../port/data/port_stormapi.h"  // SFileOpenFileEx, PortBmpDecodePcx
#include "../port/data/port_video.h"
#include "../port/front/port_ui.h"  // PortCascRead
#include "studio_draw.h"
#include "studio_media.h"
#include "studio_model.h"
#include "studio_ui.h"  // StudioSay

namespace {

std::string Back(std::string s) {
  std::replace(s.begin(), s.end(), '/', '\\');
  return s;
}

std::string LowerEnd(const std::string &path) {
  const size_t dot = path.find_last_of('.');
  std::string e = dot == std::string::npos ? std::string() : path.substr(dot);
  for (char &c : e)
    c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return e;
}

// ---- textures --------------------------------------------------------------------------------------

std::vector<ImTextureData *> g_dying;

ImTextureData *TexMake(int w, int h) {
  ImTextureData *p = IM_NEW(ImTextureData)();
  p->Create(ImTextureFormat_RGBA32, w, h);
  memset(p->Pixels, 0, static_cast<size_t>(w) * h * 4);
  p->UseColors = true;
  ImGui::RegisterUserTexture(p);
  return p;
}

void TexDrop(ImTextureData *p) {
  if (p == nullptr)
    return;
  p->WantDestroyNextFrame = true;
  g_dying.push_back(p);
}

// New pixels for the whole texture: copied in, and sent to the backend once it has the texture.
void TexPut(ImTextureData *p, const unsigned char *pRGBA) {
  memcpy(p->Pixels, pRGBA, static_cast<size_t>(p->Width) * p->Height * 4);
  if (p->Status == ImTextureStatus_OK || p->Status == ImTextureStatus_WantUpdates)
    ImTextureDataQueueUpload(p, 0, 0, p->Width, p->Height);
}

void TexFreeDead(void) {
  for (auto it = g_dying.begin(); it != g_dying.end();) {
    if ((*it)->Status == ImTextureStatus_Destroyed) {
      ImGui::UnregisterUserTexture(*it);
      IM_DELETE(*it);
      it = g_dying.erase(it);
    } else {
      ++it;
    }
  }
}

// The picture fitted to a..b, its shape kept and centred; where it went in *pp0 and *pp1.
void DrawFitted(ImTextureData *p, int w, int h, ImVec2 a, ImVec2 b, ImVec2 *pp0, ImVec2 *pp1) {
  const float bw = b.x - a.x, bh = b.y - a.y;
  float s = std::min(bw / static_cast<float>(w), bh / static_cast<float>(h));
  // a small picture at a whole scale when that still fits, so its pixels stay square
  if (s >= 1.0f)
    s = std::floor(s);
  const float dw = w * s, dh = h * s;
  const ImVec2 p0(std::floor(a.x + (bw - dw) / 2.0f), std::floor(a.y + (bh - dh) / 2.0f));
  const ImVec2 p1(p0.x + dw, p0.y + dh);
  ImGui::GetWindowDrawList()->AddImage(p->GetTexRef(), p0, p1);
  if (pp0)
    *pp0 = p0;
  if (pp1)
    *pp1 = p1;
}

// ---- reading -----------------------------------------------------------------------------------------

bool ReadDiskFile(const std::string &path, std::vector<unsigned char> *pOut) {
  HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE)
    return false;
  const DWORD cb = GetFileSize(h, nullptr);
  bool bOk = cb != INVALID_FILE_SIZE && cb > 0;
  if (bOk) {
    pOut->resize(cb);
    DWORD n = 0;
    bOk = ReadFile(h, pOut->data(), cb, &n, nullptr) && n == cb;
  }
  CloseHandle(h);
  return bOk;
}

bool ReadGameFile(const std::string &name, std::vector<unsigned char> *pOut) {
  void *h = nullptr;
  if (!SFileOpenFile(name.c_str(), &h))
    return false;
  const DWORD cb = SFileGetFileSize(h, nullptr);
  bool bOk = cb != 0xFFFFFFFF && cb > 0;
  if (bOk) {
    pOut->resize(cb);
    DWORD n = 0;
    bOk = SFileReadFile(h, pOut->data(), cb, &n, nullptr) && n == cb;
  }
  SFileCloseFile(h);
  return bOk;
}

// A file's time and size as one number, so a file brought in again under the same name is read again.
unsigned long long Stamp(const std::string &path) {
  WIN32_FILE_ATTRIBUTE_DATA fa;
  if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fa))
    return 0;
  return (static_cast<unsigned long long>(fa.ftLastWriteTime.dwHighDateTime) << 32 | fa.ftLastWriteTime.dwLowDateTime) ^
         (static_cast<unsigned long long>(fa.nFileSizeHigh) << 40 | fa.nFileSizeLow);
}

std::string DiskPath(const std::string &where) {
  return StudioDataFolder() + Back(where);
}

std::string Key(const std::string &where, bool bClassic) {
  return bClassic ? "classic:" + where : DiskPath(where) + "#" + std::to_string(Stamp(DiskPath(where)));
}

// ---- videos ------------------------------------------------------------------------------------------

struct Job;

struct Player {
  PortVideo *pVideo = nullptr;  // a video in full colour
  SmkFile *pSmk = nullptr;      // a Smacker file, read through hFile
  HANDLE hFile = INVALID_HANDLE_VALUE;
  std::vector<unsigned char> index;  // a Smacker frame's palette numbers
  std::vector<unsigned char> rgba;
  ImTextureData *pTex = nullptr;
  int w = 0, h = 0;
  unsigned nFrames = 0;
  double dFrame = 1.0 / 15.0;  // a frame's seconds
  double tStart = 0.0;
  unsigned nDone = 0;  // frames painted since it opened
  double tUsed = 0.0;
  std::string from;    // where it was read from, in words
  std::string why;     // why it did not open
  std::shared_ptr<Job> pJob;  // the opening, on a thread of its own, until it is done
  // its sound: a stream of SDL's own on the default device, fed half a second ahead while the
  // preview is drawn and asks for it, so it stops by itself soon after the page goes
  bool bHasSound = false;
  bool bWantSound = false;
  SDL_AudioSpec spec = {};
  int nFrameBytes = 0;                  // a sample of every channel
  const unsigned char *pPcm = nullptr;  // a video in full colour's whole track (PortVideoSound's)
  size_t cbPcm = 0;
  size_t cbFed = 0;                     // where in it the stream has been fed to
  SDL_AudioStream *pAudio = nullptr;
  double tFrame0 = -1.0, tSound = -1.0;  // when frame 0 was first painted and the sound first put in
};

// A video opened on a thread of its own: a mod's cinematic can be large, and a video in full colour
// has its sound decoded whole as it opens. The page draws "reading..." until it is done.
struct Job {
  std::mutex m;
  bool bDone = false;
  bool bAbandoned = false;  // its player was let go first: the thread closes what it opened
  Player opened;
};

std::map<std::string, Player> g_players;
int g_nTick = -1;

void SoundStop(Player &p) {
  if (p.pAudio)
    SDL_DestroyAudioStream(p.pAudio);
  p.pAudio = nullptr;
  p.cbFed = 0;
}

void Close(Player &p) {
  SoundStop(p);
  if (p.pJob) {
    std::lock_guard<std::mutex> lock(p.pJob->m);
    p.pJob->bAbandoned = true;
    p.pJob.reset();
  }
  if (p.pSmk)
    SmkClose(p.pSmk);
  if (p.hFile != INVALID_HANDLE_VALUE)
    CloseHandle(p.hFile);
  if (p.pVideo)
    PortVideoClose(p.pVideo);
  TexDrop(p.pTex);
  p.pSmk = nullptr;
  p.pVideo = nullptr;
  p.hFile = INVALID_HANDLE_VALUE;
  p.pTex = nullptr;
}

// Once a frame: players not drawn for a few seconds closed, textures the backend let go freed.
void Tick(void) {
  if (g_nTick == ImGui::GetFrameCount())
    return;
  g_nTick = ImGui::GetFrameCount();
  const double now = ImGui::GetTime();
  for (auto it = g_players.begin(); it != g_players.end();) {
    if (it->second.pAudio && now - it->second.tUsed > 0.1)
      SoundStop(it->second);
    if (now - it->second.tUsed > 3.0) {
      Close(it->second);
      it = g_players.erase(it);
    } else {
      ++it;
    }
  }
  TexFreeDead();
}

// A Smacker file held as a file of its own: SmkOpen reads through a handle. A file the archives keep
// as it is is read where it is; one they compress goes to a temporary file, gone when it closes.
HANDLE SmkHandle(const std::string &name, const std::vector<unsigned char> *pBytes) {
  if (pBytes == nullptr) {
    void *h = nullptr;
    return SFileOpenFileEx(nullptr, name.c_str(), 4, &h) ? static_cast<HANDLE>(h) : INVALID_HANDLE_VALUE;
  }
  char szDir[MAX_PATH], szFile[MAX_PATH];
  if (!GetTempPathA(MAX_PATH, szDir) || !GetTempFileNameA(szDir, "hxs", 0, szFile))
    return INVALID_HANDLE_VALUE;
  HANDLE w = CreateFileA(szFile, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
  if (w == INVALID_HANDLE_VALUE)
    return INVALID_HANDLE_VALUE;
  DWORD n = 0;
  const bool bOk = WriteFile(w, pBytes->data(), static_cast<DWORD>(pBytes->size()), &n, nullptr) && n == pBytes->size();
  CloseHandle(w);
  if (!bOk) {
    DeleteFileA(szFile);
    return INVALID_HANDLE_VALUE;
  }
  return CreateFileA(szFile, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                     FILE_FLAG_DELETE_ON_CLOSE | FILE_ATTRIBUTE_TEMPORARY, nullptr);
}

bool OpenSmk(Player &p, HANDLE h) {
  if (h == INVALID_HANDLE_VALUE)
    return false;
  // the first track's rate and form: present 0x40000000, 16 bits 0x20000000, stereo 0x10000000
  const DWORD dwAt = SetFilePointer(h, 0, nullptr, FILE_CURRENT);
  unsigned char hd[0x68] = {};
  DWORD n = 0;
  if (dwAt != INVALID_SET_FILE_POINTER && ReadFile(h, hd, sizeof hd, &n, nullptr) && n == sizeof hd) {
    const unsigned r = hd[0x48] | hd[0x49] << 8 | hd[0x4A] << 16 | static_cast<unsigned>(hd[0x4B]) << 24;
    if ((r & 0x40000000) && (r & 0xFFFFFF)) {
      p.bHasSound = true;
      p.spec.freq = static_cast<int>(r & 0xFFFFFF);
      p.spec.channels = (r & 0x10000000) ? 2 : 1;
      p.spec.format = (r & 0x20000000) ? SDL_AUDIO_S16LE : SDL_AUDIO_U8;
      p.nFrameBytes = p.spec.channels * ((r & 0x20000000) ? 2 : 1);
    }
  }
  SetFilePointer(h, static_cast<LONG>(dwAt), nullptr, FILE_BEGIN);
  p.pSmk = SmkOpen(h, 0);  // no DirectSound tracks: its sound is taken a frame at a time (SmkTrackData)
  if (p.pSmk == nullptr) {
    CloseHandle(h);
    return false;
  }
  p.hFile = h;
  p.w = static_cast<int>(SmkWidth(p.pSmk));
  p.h = static_cast<int>(SmkHeight(p.pSmk));
  p.nFrames = SmkFrames(p.pSmk);
  if (SmkInterval(p.pSmk))
    p.dFrame = SmkInterval(p.pSmk) / 100000.0;
  p.index.assign(static_cast<size_t>(p.w) * p.h, 0);
  SmkToBuffer(p.pSmk, 0, 0, static_cast<ULONG>(p.w), p.index.data());
  return true;
}

bool OpenFull(Player &p, const std::vector<unsigned char> &bytes) {
  p.pVideo = PortVideoOpen(bytes.data(), bytes.size());
  if (p.pVideo == nullptr)
    return false;
  p.w = PortVideoWidth(p.pVideo);
  p.h = PortVideoHeight(p.pVideo);
  p.nFrames = PortVideoFrames(p.pVideo);
  if (PortVideoInterval(p.pVideo))
    p.dFrame = PortVideoInterval(p.pVideo) / 100000.0;
  size_t nSamples = 0;
  int nRate = 0, nChannels = 0;
  if (const short *pPcm = PortVideoSound(p.pVideo, &nSamples, &nRate, &nChannels)) {
    if (nSamples && nRate > 0 && nChannels > 0) {
      p.bHasSound = true;
      p.spec.freq = nRate;
      p.spec.channels = nChannels;
      p.spec.format = SDL_AUDIO_S16;
      p.nFrameBytes = nChannels * 2;
      p.pPcm = reinterpret_cast<const unsigned char *>(pPcm);
      p.cbPcm = nSamples * static_cast<size_t>(p.nFrameBytes);
    }
  }
  return p.w > 0 && p.h > 0 && p.nFrames > 0;
}

bool IsSmk(const std::vector<unsigned char> &b) {
  return b.size() > 4 && memcmp(b.data(), "SMK2", 4) == 0;
}

// Opens a video as the game finds it: a classic name in the archives, Remastered's .webm of the same
// name in its place when they have no .smk; or a file of the mod's data folder.
void Open(Player &p, const std::string &where, const std::string &path, bool bClassic) {
  std::vector<unsigned char> bytes;
  if (bClassic) {
    const std::string name = Back(where);
    const bool bSmk = LowerEnd(name) == ".smk";
    if (bSmk && OpenSmk(p, SmkHandle(name, nullptr))) {
      p.from = name + " in the classic game's files";
    } else if (ReadGameFile(name, &bytes)) {
      if (IsSmk(bytes) ? OpenSmk(p, SmkHandle(name, &bytes)) : OpenFull(p, bytes))
        p.from = name + " in the classic game's files";
    }
    if (p.pSmk == nullptr && p.pVideo == nullptr && bSmk) {
      const std::string webm = name.substr(0, name.size() - 4) + ".webm";
      bytes.clear();
      if (ReadGameFile(webm, &bytes) && OpenFull(p, bytes))
        p.from = webm + ", Remastered's in its place";
    }
    if (p.pSmk == nullptr && p.pVideo == nullptr)
      p.why = bytes.empty() ? name + " is not in the game's files" : name + " does not decode";
    return;
  }
  if (LowerEnd(path) == ".smk") {
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (OpenSmk(p, h))
      p.from = where;
    else
      p.why = h == INVALID_HANDLE_VALUE ? where + " is not in the data folder" : where + " is no Smacker file the game reads";
    return;
  }
  if (!ReadDiskFile(path, &bytes)) {
    p.why = where + " is not in the data folder";
    return;
  }
  if (OpenFull(p, bytes)) {
    p.from = where;
    return;
  }
  PortVideoInfo info;
  if (PortVideoProbe(bytes.data(), bytes.size(), &info))
    p.why = std::string("its pictures are ") + info.pszVideo + ", which this computer does not decode";
  else
    p.why = where + " is none of the containers the game reads";
}

bool g_bMuted = false;

bool SoundOpen(Player &p) {
  if (p.pAudio)
    return true;
  if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO))
    return false;
  p.pAudio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &p.spec, nullptr, nullptr);
  if (p.pAudio == nullptr)
    return false;
  SDL_SetAudioStreamGain(p.pAudio, g_bMuted ? 0.0f : 0.9f);
  SDL_ResumeAudioStreamDevice(p.pAudio);
  return true;
}

void SoundStarted(Player &p) {
  if (p.tSound >= 0.0)
    return;
  p.tSound = ImGui::GetTime();
  char sz[200];
  snprintf(sz, sizeof sz, "preview %s: frame 0 at %.3f s, its sound put in at %.3f s (%+.0f ms)", p.from.c_str(), p.tFrame0, p.tSound,
           (p.tSound - p.tFrame0) * 1000.0);
  StudioLog("info", sz);
}

// A video in full colour's sound kept with the frame shown, dAt seconds into it: what is heard is
// where it was fed to less what waits in the stream; more than a seventh of a second apart (a
// loop, a pause, a page drawn again) it is fed again from the frame's place. Fed half a second ahead.
void SoundFeed(Player &p, double dAt) {
  if (!SoundOpen(p))
    return;
  const size_t cbSecond = static_cast<size_t>(p.spec.freq) * p.nFrameBytes;
  size_t cbQueued = static_cast<size_t>((std::max)(0, SDL_GetAudioStreamQueued(p.pAudio)));
  const size_t cbHeard = p.cbFed > cbQueued ? p.cbFed - cbQueued : 0;
  const size_t cbWant = (std::min)(p.cbPcm, static_cast<size_t>(dAt * cbSecond) / p.nFrameBytes * p.nFrameBytes);
  const double dApart = (static_cast<double>(cbHeard) - static_cast<double>(cbWant)) / cbSecond;
  if (p.cbFed == 0 || dApart > 0.15 || dApart < -0.15) {
    SDL_ClearAudioStream(p.pAudio);
    p.cbFed = cbWant;
    cbQueued = 0;
  }
  while (cbQueued < cbSecond / 2 && p.cbFed < p.cbPcm) {
    const size_t cb = (std::min)(cbSecond / 4 / p.nFrameBytes * p.nFrameBytes, p.cbPcm - p.cbFed);
    if (!SDL_PutAudioStreamData(p.pAudio, p.pPcm + p.cbFed, static_cast<int>(cb)))
      break;
    p.cbFed += cb;
    cbQueued += cb;
    SoundStarted(p);
  }
}

// Paints the frames due since the last call: a Smacker file only goes forward, frame after frame; a
// video in full colour is asked for the frame due.
void Advance(Player &p) {
  const double now = ImGui::GetTime();
  const unsigned nDue = static_cast<unsigned>((now - p.tStart) / p.dFrame);
  if (p.pTex != nullptr && nDue < p.nDone)
    return;
  // a player drawn again after a while picks up where it was rather than running to catch up
  if (p.pTex != nullptr && nDue > p.nDone + 4)
    p.tStart = now - p.nDone * p.dFrame;
  if (p.pTex == nullptr)
    p.pTex = TexMake(p.w, p.h);
  p.rgba.resize(static_cast<size_t>(p.w) * p.h * 4);
  const unsigned nShown = p.nFrames ? p.nDone % p.nFrames : 0;
  if (p.nDone == 0)
    p.tFrame0 = now;
  if (p.pAudio && !(p.bWantSound && p.bHasSound))
    SoundStop(p);
  if (p.pSmk) {
    SmkDoFrame(p.pSmk);
    // the frame's own sound, a frame's worth, put in as it is painted; the stream starts again with the loop
    if (p.bWantSound && p.bHasSound) {
      DWORD cb = 0;
      const BYTE *pb = SmkTrackData(p.pSmk, 0, &cb);
      if (nShown == 0 && p.pAudio)
        SDL_ClearAudioStream(p.pAudio);
      if (pb && cb && SoundOpen(p) && SDL_PutAudioStreamData(p.pAudio, pb, static_cast<int>(cb)))
        SoundStarted(p);
    }
    const unsigned char *pal = SmkPaletteRGB(p.pSmk);
    for (size_t k = 0; k < p.index.size(); ++k) {
      const unsigned char *e = pal + p.index[k] * 3;
      unsigned char *o = &p.rgba[k * 4];
      o[0] = e[0];
      o[1] = e[1];
      o[2] = e[2];
      o[3] = 255;
    }
    SmkNextFrame(p.pSmk);
  } else if (!PortVideoFrame(p.pVideo, p.nDone % p.nFrames, p.rgba.data(), p.w * 4)) {
    ++p.nDone;
    return;
  }
  TexPut(p.pTex, p.rgba.data());
  if (p.pVideo && p.bWantSound && p.bHasSound)
    SoundFeed(p, nShown * p.dFrame);
  ++p.nDone;
}

Player *PlayerFor(const std::string &where, bool bClassic) {
  Tick();
  if (!StudioIsOpen() && !bClassic)
    return nullptr;
  const std::string key = Key(where, bClassic);
  auto it = g_players.find(key);
  if (it == g_players.end()) {
    // a few at once: the oldest let go first
    while (g_players.size() >= 4) {
      auto old = std::min_element(g_players.begin(), g_players.end(),
                                  [](const std::pair<const std::string, Player> &x, const std::pair<const std::string, Player> &y) {
                                    return x.second.tUsed < y.second.tUsed;
                                  });
      Close(old->second);
      g_players.erase(old);
    }
    it = g_players.emplace(key, Player()).first;
    std::shared_ptr<Job> pJob = std::make_shared<Job>();
    it->second.pJob = pJob;
    const std::string path = bClassic ? std::string() : DiskPath(where);
    std::thread([pJob, where, path, bClassic]() {
      Player opened;
      Open(opened, where, path, bClassic);
      std::lock_guard<std::mutex> lock(pJob->m);
      if (pJob->bAbandoned) {
        Close(opened);
      } else {
        pJob->opened = std::move(opened);
        pJob->bDone = true;
      }
    }).detach();
  }
  Player &p = it->second;
  p.tUsed = ImGui::GetTime();
  bool bArrived = false;
  if (p.pJob) {
    std::lock_guard<std::mutex> lock(p.pJob->m);
    bArrived = p.pJob->bDone;
    if (bArrived) {
      Player &o = p.pJob->opened;
      p.pVideo = o.pVideo;
      p.pSmk = o.pSmk;
      p.hFile = o.hFile;
      p.index = std::move(o.index);
      p.w = o.w;
      p.h = o.h;
      p.nFrames = o.nFrames;
      p.dFrame = o.dFrame;
      p.from = o.from;
      p.why = o.why;
      p.bHasSound = o.bHasSound;
      p.spec = o.spec;
      p.nFrameBytes = o.nFrameBytes;
      p.pPcm = o.pPcm;
      p.cbPcm = o.cbPcm;
      o.pVideo = nullptr;
      o.pSmk = nullptr;
      o.hFile = INVALID_HANDLE_VALUE;
      p.tStart = ImGui::GetTime();
    }
  }
  if (bArrived)
    p.pJob.reset();
  return &p;
}

// ---- console images ----------------------------------------------------------------------------------

struct Picture {
  ImTextureData *pTex = nullptr;
  int w = 0, h = 0;
  double tUsed = 0.0;
  std::string from;
  std::string why;
};

std::map<std::string, Picture> g_pictures;

void PicturesTick(void) {
  const double now = ImGui::GetTime();
  for (auto it = g_pictures.begin(); it != g_pictures.end();) {
    if (now - it->second.tUsed > 5.0) {
      TexDrop(it->second.pTex);
      it = g_pictures.erase(it);
    } else {
      ++it;
    }
  }
}

void OpenPcx(Picture &pic, const std::string &where, bool bClassic) {
  std::vector<unsigned char> bytes;
  const bool bRead = bClassic ? ReadGameFile(Back(where), &bytes) : ReadDiskFile(DiskPath(where), &bytes);
  if (!bRead) {
    pic.why = bClassic ? where + " is not in the game's files" : where + " is not in the data folder";
    return;
  }
  const std::vector<unsigned> &pal = StudioPalette();
  if (pal.size() != 256) {
    pic.why = "the game's palette is not in its files";
    return;
  }
  unsigned char *pIndex = nullptr;
  int w = 0, h = 0;
  if (!PortBmpDecodePcx(bytes.data(), bytes.size(), &pIndex, &w, &h)) {
    pic.why = "it is no PCX of 8 bits a pixel";
    return;
  }
  std::vector<unsigned char> rgba(static_cast<size_t>(w) * h * 4);
  for (size_t k = 0; k < static_cast<size_t>(w) * h; ++k) {
    const unsigned c = pal[pIndex[k]];
    rgba[k * 4] = static_cast<unsigned char>(c >> 16);
    rgba[k * 4 + 1] = static_cast<unsigned char>(c >> 8);
    rgba[k * 4 + 2] = static_cast<unsigned char>(c);
    rgba[k * 4 + 3] = pIndex[k] == 0 ? 0 : 255;  // 0 is where the map shows through
  }
  SMemFree(pIndex, "studio_media_video.cpp", __LINE__, 0);
  pic.pTex = TexMake(w, h);
  memcpy(pic.pTex->Pixels, rgba.data(), rgba.size());
  pic.w = w;
  pic.h = h;
  pic.from = bClassic ? where + " in the classic game's files" : where;
}

// ---- subtitles ---------------------------------------------------------------------------------------

struct Cue {
  double t0 = 0.0, t1 = 0.0;
  std::string text;  // its lines, a newline between
};

struct Subtitles {
  std::vector<Cue> cues;
  std::string from;
};

std::map<std::string, Subtitles> g_subtitles;

// A file that is not UTF-8 read as Windows-1252, as the game reads its subtitles (port_subtitles.cpp).
std::string Utf8Of(const std::string &in) {
  bool bUtf8 = true;
  for (size_t i = 0; i < in.size() && bUtf8;) {
    const unsigned char c = static_cast<unsigned char>(in[i]);
    const size_t n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
    if (n == 0 || i + n > in.size())
      bUtf8 = false;
    for (size_t k = 1; k < n && bUtf8; ++k)
      bUtf8 = (static_cast<unsigned char>(in[i + k]) & 0xC0) == 0x80;
    i += n == 0 ? 1 : n;
  }
  if (bUtf8)
    return in;
  static const unsigned short kau1252[32] = {
      0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
      0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178};
  std::string out;
  for (const char ch : in) {
    const unsigned char c = static_cast<unsigned char>(ch);
    const unsigned u = c < 0x80 ? c : c < 0xA0 ? kau1252[c - 0x80] : c;
    if (u < 0x80) {
      out += static_cast<char>(u);
    } else if (u < 0x800) {
      out += static_cast<char>(0xC0 | (u >> 6));
      out += static_cast<char>(0x80 | (u & 0x3F));
    } else {
      out += static_cast<char>(0xE0 | (u >> 12));
      out += static_cast<char>(0x80 | ((u >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (u & 0x3F));
    }
  }
  return out;
}

bool SrtTime(const std::string &s, size_t at, double *pd) {
  int h, m, sec, ms;
  if (sscanf_s(s.c_str() + at, "%d:%d:%d,%d", &h, &m, &sec, &ms) != 4)
    return false;
  *pd = h * 3600.0 + m * 60.0 + sec + ms / 1000.0;
  return true;
}

// SubRip: blocks parted by an empty line, each a number, "from --> to" and the lines shown.
std::vector<Cue> ParseSrt(const std::string &text) {
  std::vector<Cue> cues;
  size_t at = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
  Cue cue;
  int nPart = 0;  // 0 the number, 1 the times, 2 the lines, 3 skipped to the next empty line
  while (at <= text.size()) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos)
      end = text.size();
    std::string line = text.substr(at, end - at);
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    at = end + 1;
    if (line.empty()) {
      if (nPart == 2 && !cue.text.empty())
        cues.push_back(cue);
      cue = Cue();
      nPart = 0;
    } else if (nPart == 0) {
      nPart = 1;
    } else if (nPart == 1) {
      const size_t arrow = line.find("-->");
      size_t to = arrow == std::string::npos ? std::string::npos : arrow + 3;
      while (to != std::string::npos && to < line.size() && line[to] == ' ')
        ++to;
      nPart = to != std::string::npos && SrtTime(line, 0, &cue.t0) && SrtTime(line, to, &cue.t1) ? 2 : 3;
    } else if (nPart == 2) {
      cue.text += (cue.text.empty() ? "" : "\n") + line;
    }
  }
  if (nPart == 2 && !cue.text.empty())
    cues.push_back(cue);
  return cues;
}

}  // namespace

std::string StudioSubtitleAt(const std::string &cinematic, bool bOwn, double dSeconds, std::string *pFrom) {
  const std::string own = "smk/" + cinematic + ".srt";
  const bool bOwnSrt = bOwn && StudioIsOpen() && StudioFileExists(own);
  const std::string key = cinematic + (bOwnSrt ? "#" + std::to_string(Stamp(DiskPath(own))) : std::string("#classic"));
  auto it = g_subtitles.find(key);
  if (it == g_subtitles.end()) {
    Subtitles subs;
    std::vector<unsigned char> bytes;
    if (bOwnSrt && ReadDiskFile(DiskPath(own), &bytes)) {
      subs.from = own;
    } else {
      // Remastered's, in English, when its install is there
      size_t cb = 0;
      const std::string name = "locales\\enUS\\Assets\\Smk\\" + cinematic + ".srt";
      if (void *pData = PortCascRead(name.c_str(), &cb)) {
        bytes.assign(static_cast<unsigned char *>(pData), static_cast<unsigned char *>(pData) + cb);
        SDL_free(pData);
        subs.from = "Remastered's " + cinematic + ".srt";
      }
    }
    if (!bytes.empty())
      subs.cues = ParseSrt(Utf8Of(std::string(bytes.begin(), bytes.end())));
    it = g_subtitles.emplace(key, subs).first;
  }
  if (pFrom)
    *pFrom = it->second.cues.empty() ? std::string() : it->second.from;
  for (const Cue &c : it->second.cues)
    if (dSeconds >= c.t0 && dSeconds < c.t1)
      return c.text;
  return std::string();
}

void StudioVideoMute(bool bMuted) {
  g_bMuted = bMuted;
  for (auto &kv : g_players)
    if (kv.second.pAudio)
      SDL_SetAudioStreamGain(kv.second.pAudio, bMuted ? 0.0f : 0.9f);
}

bool StudioVideoMuted(void) {
  return g_bMuted;
}

bool StudioVideoDraw(const std::string &where, bool bClassic, ImVec2 a, ImVec2 b, StudioVideoState *pState, bool bSound) {
  Player *p = where.empty() ? nullptr : PlayerFor(where, bClassic);
  if (p)
    p->bWantSound = bSound;
  if (pState) {
    *pState = StudioVideoState();
    if (p && p->pJob) {
      pState->bReading = true;
      pState->why = "reading...";
    } else if (p) {
      pState->from = p->from;
      pState->why = p->why;
      pState->w = p->w;
      pState->h = p->h;
      pState->nFrames = p->nFrames;
      pState->dSeconds = p->nFrames * p->dFrame;
      pState->nFrame = p->nFrames ? (p->nDone ? p->nDone - 1 : 0) % p->nFrames : 0;
      pState->bSmacker = p->pSmk != nullptr;
      pState->dAt = pState->nFrame * p->dFrame;
      pState->bHasSound = p->bHasSound;
      pState->bSoundOn = p->pAudio != nullptr && !g_bMuted;
      pState->dFrame0At = p->tFrame0;
      pState->dSoundAt = p->tSound;
    }
  }
  if (p == nullptr || (p->pSmk == nullptr && p->pVideo == nullptr))
    return false;
  Advance(*p);
  DrawFitted(p->pTex, p->w, p->h, a, b, nullptr, nullptr);
  return true;
}

bool StudioPcxDraw(const std::string &where, bool bClassic, ImVec2 a, ImVec2 b, StudioVideoState *pState) {
  Tick();
  PicturesTick();
  if (pState)
    *pState = StudioVideoState();
  if (where.empty())
    return false;
  const std::string key = Key(where, bClassic);
  auto it = g_pictures.find(key);
  if (it == g_pictures.end()) {
    it = g_pictures.emplace(key, Picture()).first;
    OpenPcx(it->second, where, bClassic);
  }
  Picture &pic = it->second;
  pic.tUsed = ImGui::GetTime();
  if (pState) {
    pState->from = pic.from;
    pState->why = pic.why;
    pState->w = pic.w;
    pState->h = pic.h;
  }
  if (pic.pTex == nullptr)
    return false;
  ImVec2 p0, p1;
  DrawFitted(pic.pTex, pic.w, pic.h, a, b, &p0, &p1);
  return true;
}
