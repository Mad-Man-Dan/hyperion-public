// design_bridge.cpp -- the panels' way to the live channel's commands. A panel's request goes on
// the channel's own inbox under the panels' client (DESIGN_CLIENT, port_control.cpp), so it runs
// where every tool's runs, between frames on the game's thread, and the channel's answer comes
// back here instead of to a socket. The panels and the tools driving the design engine thus share
// every command and its checks, and nothing runs in the middle of a frame.
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "../port/platform/port_json.h"
#include "design_engine.h"
#include "design_hooks.h"

namespace {

std::mutex g_lock;
std::vector<std::string> g_answers;  // from Send, on the game's thread; taken by the panels' frame
std::map<long long, DesignAnswerFn> g_waiting;
long long g_nNext = 1;
std::string g_last;

}  // namespace

void DesignCall(const char *pszCmd, const std::string &fields, DesignAnswerFn pfnDone) {
  const long long nId = g_nNext++;
  g_waiting[nId] = pfnDone;
  DesignChannelPost("{\"id\":" + std::to_string(nId) + ",\"cmd\":" + PortJsonQuote(pszCmd) +
                    (fields.empty() ? std::string() : "," + fields) + "}");
}

void DesignChannelAnswer(const std::string &text) {
  std::lock_guard<std::mutex> hold(g_lock);
  g_answers.push_back(text);
}

void DesignBridgeDeliver(void) {
  std::vector<std::string> answers;
  {
    std::lock_guard<std::mutex> hold(g_lock);
    answers.swap(g_answers);
  }
  for (const std::string &text : answers) {
    g_last = text;
    PortJson j;
    if (!PortJsonParse(text.c_str(), &j) || j.kind != PortJson::OBJECT)
      continue;
    const auto it = g_waiting.find(static_cast<long long>(j["id"].Num(-1)));
    if (it == g_waiting.end())
      continue;
    const DesignAnswerFn fn = it->second;
    g_waiting.erase(it);
    if (fn)
      fn(j);
  }
}

const std::string &DesignBridgeLast(void) {
  return g_last;
}

int DesignBridgeWaiting(void) {
  return static_cast<int>(g_waiting.size());
}
