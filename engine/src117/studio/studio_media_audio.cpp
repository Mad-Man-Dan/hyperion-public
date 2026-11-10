// studio_media_audio.cpp -- the sound, looks, mod page and checks areas' shared parts
// (studio_media.h): a sound record's file found, in the mod's data folder or the classic game's
// archives, decoded as the game would decode it (port_audiodecode.cpp) and played once on the
// mixer (PortSoundOneShot); the game's palette for the colour pickers; files copied in under the
// name the loader looks for.
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "../port/data/port_audiodecode.h"
#include "../port/data/port_mods.h"
#include "../port/data/port_soundshot.h"
#include "../port/data/port_stormapi.h"  // SFileOpenFile
#include "studio_draw.h"
#include "studio_media.h"

const char *const kStudioVideoEnds[6] = {".webm", ".mkv", ".mp4", ".m4v", ".mov", ".smk"};

namespace {

using namespace studio_draw;

std::string Back(std::string s) {
  std::replace(s.begin(), s.end(), '/', '\\');
  return s;
}

bool OnDisk(const std::string &path) {
  const DWORD a = GetFileAttributesA(path.c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// A file on disk, whole.
bool ReadDisk(const std::string &path, std::vector<unsigned char> *pOut) {
  HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE)
    return false;
  LARGE_INTEGER size;
  bool bOk = GetFileSizeEx(h, &size) && size.QuadPart < (256LL << 20);
  if (bOk) {
    pOut->resize(static_cast<size_t>(size.QuadPart));
    DWORD nRead = 0;
    bOk = size.QuadPart == 0 || (ReadFile(h, pOut->data(), static_cast<DWORD>(size.QuadPart), &nRead, nullptr) &&
                                 nRead == static_cast<DWORD>(size.QuadPart));
  }
  CloseHandle(h);
  return bOk;
}

// A file of the classic game's archives, whole; false when they do not have it.
bool ReadArchive(const std::string &name, std::vector<unsigned char> *pOut) {
  void *h = nullptr;
  if (!SFileOpenFile(name.c_str(), &h) || h == nullptr)
    return false;
  const DWORD cb = SFileGetFileSize(h, nullptr);
  bool bOk = cb != 0xFFFFFFFF && cb < (64u << 20);
  if (bOk) {
    pOut->resize(cb);
    DWORD nRead = 0;
    bOk = cb == 0 || (SFileReadFile(h, pOut->data(), cb, &nRead, nullptr) && nRead == cb);
  }
  SFileCloseFile(h);
  return bOk;
}

std::string KindOf(const std::vector<unsigned char> &b) {
  if (b.size() >= 4 && memcmp(b.data(), "RIFF", 4) == 0)
    return "WAV";
  if (b.size() >= 4 && memcmp(b.data(), "fLaC", 4) == 0)
    return "FLAC";
  if (b.size() >= 4 && memcmp(b.data(), "OggS", 4) == 0) {
    const std::string head(reinterpret_cast<const char *>(b.data()), std::min<size_t>(b.size(), 64));
    return head.find("OpusHead") != std::string::npos ? "Opus" : "Ogg";
  }
  if (b.size() >= 3 && (memcmp(b.data(), "ID3", 3) == 0 || (b[0] == 0xFF && (b[1] & 0xE0) == 0xE0)))
    return "MP3";
  return "";
}

// A sound decoded: stereo at the mixer's rate, with what the file was.
struct Decoded {
  std::shared_ptr<const std::vector<float>> pcm;
  StudioSoundInfo info;
  bool bOk = false;
  std::string why;
};

// Decoded sounds by their file, kept while they are few: a sound played again plays at once.
std::map<std::string, Decoded> g_cache;
std::string g_playing;
double g_dPlayingUntil;

Decoded Decode(std::vector<unsigned char> &&bytes, const std::string &name) {
  Decoded out;
  out.info.kind = KindOf(bytes);
  AudioDecoder *p = AudioDecoderOpenMemory(std::move(bytes), name.c_str());
  if (p == nullptr) {
    out.why = name + " does not decode: a sound is a WAV, Ogg (Vorbis or Opus), MP3 or FLAC file";
    return out;
  }
  const int nRate = AudioDecoderRate(p), nCh = AudioDecoderChannels(p);
  std::vector<float> raw;
  std::vector<float> chunk(4096 * static_cast<size_t>(std::max(1, nCh)));
  for (;;) {
    const size_t n = AudioDecoderRead(p, chunk.data(), 4096);
    if (n == 0)
      break;
    raw.insert(raw.end(), chunk.begin(), chunk.begin() + static_cast<long long>(n) * nCh);
    if (raw.size() > static_cast<size_t>(nRate) * nCh * 600)
      break;  // ten minutes is past any sound a unit says
  }
  AudioDecoderClose(p);
  if (nRate <= 0 || nCh <= 0 || raw.empty()) {
    out.why = name + " has no sound in it";
    return out;
  }
  const size_t nFrames = raw.size() / static_cast<size_t>(nCh);
  out.info.nRate = nRate;
  out.info.nChannels = nCh;
  out.info.dSeconds = static_cast<double>(nFrames) / nRate;
  // to the mixer's rate in stereo, a straight line between the file's samples; more than two
  // channels mixed into two, as the loader mixes them
  const int nMix = PortSoundRate() > 0 ? PortSoundRate() : 44100;
  const size_t nOut = static_cast<size_t>(static_cast<double>(nFrames) * nMix / nRate);
  auto pcm = std::make_shared<std::vector<float>>(nOut * 2);
  for (size_t i = 0; i < nOut; ++i) {
    const double t = static_cast<double>(i) * nRate / nMix;
    const size_t k = static_cast<size_t>(t), k1 = k + 1 < nFrames ? k + 1 : k;
    const float f = static_cast<float>(t - static_cast<double>(k));
    for (int ch = 0; ch < 2; ++ch) {
      float a = 0.0f, b = 0.0f;
      if (nCh <= 2) {
        const int c = ch < nCh ? ch : 0;
        a = raw[k * nCh + c];
        b = raw[k1 * nCh + c];
      } else {
        for (int c = ch; c < nCh; c += 2) {
          a += raw[k * nCh + c];
          b += raw[k1 * nCh + c];
        }
        a /= static_cast<float>((nCh - ch + 1) / 2);
        b /= static_cast<float>((nCh - ch + 1) / 2);
      }
      (*pcm)[i * 2 + ch] = a + (b - a) * f;
    }
  }
  out.pcm = pcm;
  out.bOk = true;
  return out;
}

// A sound's file read and decoded once, by where it is.
const Decoded &Load(const StudioSoundWhere &w) {
  // a file on disk by its time too, so one replaced is decoded again
  std::string key = w.disk.empty() ? "mpq:" + w.mpq : "disk:" + w.disk;
  WIN32_FILE_ATTRIBUTE_DATA a;
  if (!w.disk.empty() && GetFileAttributesExA(w.disk.c_str(), GetFileExInfoStandard, &a))
    key += "@" + std::to_string(a.ftLastWriteTime.dwLowDateTime) + "." + std::to_string(a.nFileSizeLow);
  auto it = g_cache.find(key);
  if (it != g_cache.end())
    return it->second;
  if (g_cache.size() > 96)
    g_cache.clear();
  std::vector<unsigned char> bytes;
  Decoded &d = g_cache[key];
  if (!w.disk.empty()) {
    if (!OnDisk(w.disk) || !ReadDisk(w.disk, &bytes))
      d.why = "the data folder has no " + w.shown;
  } else if (!ReadArchive(w.mpq, &bytes)) {
    d.why = "the classic game's archives have no " + w.mpq;
  }
  if (d.why.empty())
    d = Decode(std::move(bytes), w.disk.empty() ? w.mpq : w.disk);
  return d;
}

bool Play(const StudioSoundWhere &w, const std::string &id, StudioSoundInfo *pInfo, std::string *pWhy) {
  if (w.shown.empty()) {
    *pWhy = "it names no file";
    return false;
  }
  const Decoded &d = Load(w);
  if (!d.bOk) {
    *pWhy = d.why;
    return false;
  }
  PortSoundOneShot(d.pcm, 0.8f);
  if (pInfo)
    *pInfo = d.info;
  g_playing = id;
  g_dPlayingUntil = ImGui::GetTime() + d.info.dSeconds;
  return true;
}

}  // namespace

StudioSoundWhere StudioSoundFile(const std::string &sound) {
  StudioSoundWhere w;
  StudioValue v;
  if (!StudioGet(ST_SOUNDS, sound, "file", &v))
    return w;
  w.shown = v.own.Str();
  if (w.shown.empty())
    return w;
  if (v.source == SS_CLASSIC) {
    w.mpq = "sound\\" + Back(w.shown);
    void *h = nullptr;
    w.bThere = SFileOpenFile(w.mpq.c_str(), &h) && h != nullptr;
    if (h)
      SFileCloseFile(h);
    return w;
  }
  StudioRecord rec;
  const std::string mod = StudioRecordOf(ST_SOUNDS, v.from.empty() ? sound : v.from, &rec) ? rec.mod : std::string();
  w.disk = StudioModDataFolder(mod) + Back(w.shown);
  w.bThere = OnDisk(w.disk);
  return w;
}

bool StudioPlaySound(const std::string &sound, StudioSoundInfo *pInfo, std::string *pWhy) {
  StudioRecord rec;
  if (!StudioRecordOf(ST_SOUNDS, sound, &rec)) {
    *pWhy = "no sound " + sound;
    return false;
  }
  return Play(StudioSoundFile(sound), sound, pInfo, pWhy);
}

bool StudioPlayDataFile(const std::string &rel, StudioSoundInfo *pInfo, std::string *pWhy) {
  if (!StudioIsOpen()) {
    *pWhy = "no mod is open";
    return false;
  }
  StudioSoundWhere w;
  w.shown = rel;
  w.disk = StudioDataFolder() + Back(rel);
  w.bThere = OnDisk(w.disk);
  return Play(w, "file:" + rel, pInfo, pWhy);
}

bool StudioSoundInfoOf(const std::string &sound, StudioSoundInfo *pInfo) {
  const StudioSoundWhere w = StudioSoundFile(sound);
  if (w.shown.empty() || !w.bThere)
    return false;
  const Decoded &d = Load(w);
  if (d.bOk)
    *pInfo = d.info;
  return d.bOk;
}

const std::string &StudioSoundPlaying(double *pdUntil) {
  if (pdUntil)
    *pdUntil = g_dPlayingUntil;
  static const std::string s_none;
  return ImGui::GetTime() < g_dPlayingUntil ? g_playing : s_none;
}

const std::vector<unsigned> &StudioPalette(void) {
  static std::vector<unsigned> s_pal;
  static bool s_bRead;
  if (!s_bRead) {
    std::vector<unsigned char> b;
    s_bRead = true;
    if (ReadArchive("tileset\\badlands.wpe", &b) && b.size() >= 1024) {
      s_pal.resize(256);
      for (int i = 0; i < 256; ++i)
        s_pal[i] = static_cast<unsigned>(b[i * 4]) << 16 | static_cast<unsigned>(b[i * 4 + 1]) << 8 | b[i * 4 + 2];
    }
  }
  return s_pal;
}

std::string StudioModDataFolder(const std::string &mod) {
  if (mod.empty() || mod == StudioModId())
    return StudioDataFolder();
  for (const PortMod &m : PortMods())
    if (m.id == mod && !m.folder.empty())
      return Back(m.folder + m.data);
  return StudioDataFolder();
}

bool StudioCopyAs(const std::string &from, const std::string &rel, std::string *pWhy) {
  if (!StudioIsOpen()) {
    *pWhy = "no mod is open";
    return false;
  }
  const std::string sub = Back(rel);
  if (sub.empty() || sub.find("..") != std::string::npos || sub.find(':') != std::string::npos || sub[0] == '\\') {
    *pWhy = "a path inside the data folder";
    return false;
  }
  if (!OnDisk(Back(from))) {
    *pWhy = "no such file: " + from;
    return false;
  }
  const std::string data = StudioDataFolder();
  for (size_t at = sub.find('\\'); at != std::string::npos; at = sub.find('\\', at + 1))
    CreateDirectoryA((data + sub.substr(0, at)).c_str(), nullptr);
  char szFrom[MAX_PATH], szTo[MAX_PATH];
  GetFullPathNameA(Back(from).c_str(), MAX_PATH, szFrom, nullptr);
  GetFullPathNameA((data + sub).c_str(), MAX_PATH, szTo, nullptr);
  if (_stricmp(szFrom, szTo) == 0)
    return true;  // the file is that one already
  if (!CopyFileA(szFrom, szTo, FALSE)) {
    *pWhy = "could not copy " + from + " into the mod (error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  return true;
}

long long StudioDataFileSize(const std::string &rel) {
  WIN32_FILE_ATTRIBUTE_DATA a;
  if (!StudioIsOpen() || rel.empty() || !GetFileAttributesExA((StudioDataFolder() + Back(rel)).c_str(), GetFileExInfoStandard, &a) ||
      (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
    return -1;
  return static_cast<long long>(a.nFileSizeHigh) << 32 | a.nFileSizeLow;
}

std::string StudioFirstVideo(const std::string &relWithoutEnd) {
  for (const char *psz : kStudioVideoEnds)
    if (StudioFileExists(relWithoutEnd + psz))
      return relWithoutEnd + psz;
  return std::string();
}

void StudioMediaHeading(const char *pszTitle, const char *pszWhat, float fW) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 o = ImGui::GetCursorScreenPos();
  const float x = o.x + T(d, STUDIO_FONT_BOLD, 16.0f, o.x, o.y, C(TK_HEAD), pszTitle) + P(10.0f);
  if (pszWhat && *pszWhat)
    T(d, STUDIO_FONT_TEXT, kStudioFsS, x, o.y + P(3.0f), C(TK_INK3), pszWhat, o.x + fW - x);
  ImGui::Dummy(ImVec2(fW, P(28.0f)));
}

void StudioMediaPara(const std::string &text, float fW, unsigned col, float fPx) {
  const ImVec2 o = ImGui::GetCursorScreenPos();
  ImGui::PushTextWrapPos(o.x + fW - ImGui::GetWindowPos().x + ImGui::GetScrollX());
  StudioPushFont(STUDIO_FONT_TEXT, fPx);
  ImGui::PushStyleColor(ImGuiCol_Text, col);
  ImGui::TextWrapped("%s", text.c_str());
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::PopTextWrapPos();
}

bool StudioPlayButton(const char *pszId, const std::string &sound, float fH) {
  const bool bNow = !sound.empty() && StudioSoundPlaying(nullptr) == sound;
  const bool bPressed = StudioButton(pszId, "", bNow ? "pause" : "play", bNow ? SB_ON : SB_PLAIN, fH, fH + 4.0f, kStudioFsXs, !sound.empty());
  if (ImGui::IsItemHovered() && !sound.empty())
    ImGui::SetTooltip("Play %s", StudioShowName(ST_SOUNDS, sound).c_str());
  if (bPressed) {
    StudioSoundInfo info;
    std::string why;
    if (!StudioPlaySound(sound, &info, &why))
      StudioSay(why);
  }
  return bPressed;
}
