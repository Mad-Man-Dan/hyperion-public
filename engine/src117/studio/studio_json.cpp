// studio_json.cpp -- the studio's JSON: changing a tree in place, and writing one as the files the
// studio saves have it (studio_model.h). Reading is the port's (port_json.h).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include "../port/platform/port_json.h"
#include "studio_model.h"

namespace {

bool Scalar(const PortJson &v) {
  return v.kind != PortJson::ARRAY && v.kind != PortJson::OBJECT;
}

// An array of numbers, names and booleans goes on one line.
bool Flat(const PortJson &v) {
  for (const PortJson &i : v.items)
    if (!Scalar(i))
      return false;
  return true;
}

void Text(const PortJson &v, int nDepth, std::string *pOut) {
  std::string &out = *pOut;
  const std::string in(static_cast<size_t>(nDepth + 1) * 2, ' ');
  const std::string back(static_cast<size_t>(nDepth) * 2, ' ');
  if (v.kind == PortJson::OBJECT) {
    if (v.members.empty()) {
      out += "{}";
      return;
    }
    out += "{\n";
    for (size_t i = 0; i < v.members.size(); ++i) {
      out += in + PortJsonQuote(v.members[i].first) + ": ";
      Text(v.members[i].second, nDepth + 1, pOut);
      out += i + 1 < v.members.size() ? ",\n" : "\n";
    }
    out += back + "}";
  } else if (v.kind == PortJson::ARRAY) {
    if (v.items.empty()) {
      out += "[]";
      return;
    }
    if (Flat(v)) {
      out += "[";
      for (size_t i = 0; i < v.items.size(); ++i)
        out += (i ? ", " : "") + PortJsonWrite(v.items[i]);
      out += "]";
      return;
    }
    out += "[\n";
    for (size_t i = 0; i < v.items.size(); ++i) {
      out += in;
      Text(v.items[i], nDepth + 1, pOut);
      out += i + 1 < v.items.size() ? ",\n" : "\n";
    }
    out += back + "]";
  } else {
    out += PortJsonWrite(v);
  }
}

}  // namespace

std::string StudioJsonText(const PortJson &v) {
  std::string out;
  Text(v, 0, &out);
  return out + "\n";
}

PortJson StudioJsonNumber(double d) {
  PortJson v;
  v.kind = PortJson::NUMBER;
  v.d = d;
  return v;
}

PortJson StudioJsonString(const std::string &s) {
  PortJson v;
  v.kind = PortJson::STRING;
  v.s = s;
  return v;
}

PortJson StudioJsonObject(void) {
  PortJson v;
  v.kind = PortJson::OBJECT;
  return v;
}

PortJson *StudioJsonMember(PortJson &obj, const std::string &key) {
  if (obj.kind != PortJson::OBJECT)
    return nullptr;
  for (PortJsonMember &m : obj.members)
    if (m.first == key)
      return &m.second;
  return nullptr;
}

void StudioJsonPut(PortJson &obj, const std::string &key, const PortJson &v, int nIndex) {
  if (obj.kind != PortJson::OBJECT) {
    obj = PortJson();
    obj.kind = PortJson::OBJECT;
  }
  if (PortJson *p = StudioJsonMember(obj, key)) {
    *p = v;
    return;
  }
  PortJsonMember m;
  m.first = key;
  m.second = v;
  if (nIndex < 0 || nIndex > static_cast<int>(obj.members.size()))
    obj.members.push_back(m);
  else
    obj.members.insert(obj.members.begin() + nIndex, m);
}

int StudioJsonErase(PortJson &obj, const std::string &key) {
  if (obj.kind != PortJson::OBJECT)
    return -1;
  for (size_t i = 0; i < obj.members.size(); ++i)
    if (obj.members[i].first == key) {
      obj.members.erase(obj.members.begin() + static_cast<long long>(i));
      return static_cast<int>(i);
    }
  return -1;
}

std::string StudioJsonWhere(const std::string &text, const std::string &error) {
  // PortJsonParse says "... at byte N"; a line is what a person looks for
  static const char kAt[] = " at byte ";
  const size_t at = error.rfind(kAt);
  if (at == std::string::npos)
    return error;
  const long long nOff = atoll(error.c_str() + at + sizeof kAt - 1);
  if (nOff < 0)
    return error;
  int nLine = 1;
  for (size_t i = 0; i < text.size() && i < static_cast<size_t>(nOff); ++i)
    if (text[i] == '\n')
      ++nLine;
  return "line " + std::to_string(nLine) + ": " + error.substr(0, at);
}
