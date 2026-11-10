// studio_files.cpp -- the files of the open mod's data folder: listed for the pickers (sounds, art,
// portraits' videos, icons, cinematics), and a file from elsewhere copied in. Files are not edits:
// a file copied in stays when the edit that named it is undone, as a modder's own files do.
#include <windows.h>
#include <algorithm>
#include <string>
#include <vector>
#include "studio_model.h"

namespace {

std::string LowerExt(const std::string &path) {
  const size_t dot = path.find_last_of('.');
  std::string e = dot == std::string::npos ? std::string() : path.substr(dot + 1);
  for (char &c : e)
    c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return e;
}

bool ExtIn(const std::string &path, const std::string &exts) {
  if (exts.empty())
    return true;
  const std::string e = LowerExt(path);
  size_t at = 0;
  while (at <= exts.size()) {
    const size_t comma = exts.find(',', at);
    const std::string one = exts.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
    if (!one.empty() && one == e)
      return true;
    if (comma == std::string::npos)
      break;
    at = comma + 1;
  }
  return false;
}

void Walk(const std::string &root, const std::string &rel, const std::string &exts, std::vector<std::string> *pOut, int nDepth) {
  if (nDepth > 8 || pOut->size() > 4000)
    return;
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA((root + rel + "*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE)
    return;
  do {
    const std::string name = fd.cFileName;
    if (name == "." || name == ".." || name == ".studio")
      continue;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
      Walk(root, rel + name + "\\", exts, pOut, nDepth + 1);
    else if (ExtIn(name, exts))
      pOut->push_back(rel + name);
  } while (FindNextFileA(h, &fd));
  FindClose(h);
}

std::string Forward(std::string s) {
  std::replace(s.begin(), s.end(), '\\', '/');
  return s;
}

std::string Back(std::string s) {
  std::replace(s.begin(), s.end(), '/', '\\');
  return s;
}

}  // namespace

std::vector<std::string> StudioFiles(const std::string &exts) {
  std::vector<std::string> out;
  if (!StudioIsOpen())
    return out;
  Walk(StudioDataFolder(), std::string(), exts, &out, 0);
  for (std::string &s : out)
    s = Forward(s);
  std::sort(out.begin(), out.end());
  return out;
}

bool StudioFileExists(const std::string &rel) {
  if (!StudioIsOpen() || rel.empty())
    return false;
  const DWORD a = GetFileAttributesA((StudioDataFolder() + Back(rel)).c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool StudioImport(const std::string &from, const std::string &folder, std::string *pRel, std::string *pWhy) {
  if (!StudioIsOpen()) {
    *pWhy = "no mod is open";
    return false;
  }
  const std::string data = StudioDataFolder();
  char szFull[MAX_PATH];
  if (GetFullPathNameA(Back(from).c_str(), MAX_PATH, szFull, nullptr) == 0) {
    *pWhy = "no such file: " + from;
    return false;
  }
  const std::string full = szFull;
  const DWORD a = GetFileAttributesA(full.c_str());
  if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_DIRECTORY)) {
    *pWhy = "no such file: " + from;
    return false;
  }
  // a file in the data folder already is named where it is
  if (full.size() > data.size() && _strnicmp(full.c_str(), data.c_str(), data.size()) == 0) {
    *pRel = Forward(full.substr(data.size()));
    return true;
  }
  std::string sub = Back(folder);
  if (sub.find("..") != std::string::npos || sub.find(':') != std::string::npos) {
    *pWhy = "a folder inside the data folder";
    return false;
  }
  while (!sub.empty() && (sub.back() == '\\'))
    sub.pop_back();
  // the folders made as they are needed
  std::string at = data;
  size_t start = 0;
  while (start < sub.size()) {
    const size_t slash = sub.find('\\', start);
    at += sub.substr(start, slash == std::string::npos ? std::string::npos : slash - start + 1);
    if (slash == std::string::npos)
      at += '\\';
    CreateDirectoryA(at.c_str(), NULL);
    if (slash == std::string::npos)
      break;
    start = slash + 1;
  }
  const size_t cut = full.find_last_of('\\');
  const std::string name = cut == std::string::npos ? full : full.substr(cut + 1);
  const std::string rel = (sub.empty() ? std::string() : sub + "\\") + name;
  if (!CopyFileA(full.c_str(), (data + rel).c_str(), FALSE)) {
    *pWhy = "could not copy " + full + " into the mod (error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  *pRel = Forward(rel);
  return true;
}
