// port_modlua_lint.cpp -- a mod's Lua read before it runs (port_modlua.h): parsed by Luau's own
// parser, checked against the rules that keep a game the same on every machine, its functions at
// the top listed, and compiled to bytecode.
//
// Built apart from the port's other files, as Luau's parser wants C++17 (build_engine_x64.py);
// it names no game global.
#include <string.h>
#include <string>
#include <vector>
#include "Luau/Ast.h"
#include "Luau/Allocator.h"
#include "Luau/Lexer.h"
#include "Luau/ParseOptions.h"
#include "Luau/ParseResult.h"
#include "Luau/Parser.h"
#include "luacode.h"
#include "port_modlua.h"

namespace {

// The globals a mod's scripts have none of: the system, the clock, files, the collector and the
// libraries whose state or numbers are not the same everywhere.
const char *const kRefused[] = {"os",       "io",      "debug",   "buffer",   "coroutine", "vector",
                                "collectgarbage", "gcinfo", "getfenv", "setfenv", "loadstring", "load",
                                "dofile",   "loadfile", "require", "newproxy"};

struct Lint : Luau::AstVisitor {
  std::vector<ModLuaProblem> *pOut;
  int nDepth = 0;  // functions the visit is inside

  void Add(const Luau::Location &at, const std::string &what) {
    ModLuaProblem p;
    p.nLine = static_cast<int>(at.begin.line) + 1;
    p.nCol = static_cast<int>(at.begin.column);
    p.nEndLine = static_cast<int>(at.end.line) + 1;
    p.nEndCol = static_cast<int>(at.end.column);
    p.what = what;
    pOut->push_back(p);
  }

  // What a write inside a function reaches that outlives the call: a global, a local of the
  // script's top, or a field of either. Empty when it is the call's own.
  std::string Kept(Luau::AstExpr *e) {
    while (true) {
      if (auto *g = e->as<Luau::AstExprGlobal>())
        return std::string("the global ") + g->name.value;
      if (auto *l = e->as<Luau::AstExprLocal>())
        return l->upvalue && l->local->functionDepth == 0 ? std::string("the script's local ") + l->local->name.value
                                                          : std::string();
      if (auto *n = e->as<Luau::AstExprIndexName>()) {
        e = n->expr;
        continue;
      }
      if (auto *x = e->as<Luau::AstExprIndexExpr>()) {
        e = x->expr;
        continue;
      }
      return std::string();
    }
  }

  void Write(Luau::AstExpr *target) {
    if (nDepth == 0)
      return;
    const std::string what = Kept(target);
    if (!what.empty())
      Add(target->location, "a function cannot change " + what +
                                ", which would outlive the call: keep values with hx.set or hx.game_set");
  }

  bool visit(Luau::AstExprFunction *f) override {
    ++nDepth;
    f->body->visit(this);
    --nDepth;
    return false;
  }
  bool visit(Luau::AstStatFunction *s) override {
    Write(s->name);
    s->func->visit(this);
    return false;
  }
  bool visit(Luau::AstStatAssign *s) override {
    for (Luau::AstExpr *v : s->vars)
      Write(v);
    return true;
  }
  bool visit(Luau::AstStatCompoundAssign *s) override {
    Write(s->var);
    if (s->op == Luau::AstExprBinary::Pow)
      Add(s->location, "^ is the system's power function, which differs between machines: multiply, or use hx.pow(x, n)");
    return true;
  }
  bool visit(Luau::AstExprBinary *b) override {
    if (b->op == Luau::AstExprBinary::Pow)
      Add(b->location, "^ is the system's power function, which differs between machines: multiply, or use hx.pow(x, n)");
    return true;
  }
  bool visit(Luau::AstExprGlobal *g) override {
    for (const char *psz : kRefused)
      if (strcmp(g->name.value, psz) == 0)
        Add(g->location, std::string(psz) + " is not in a mod's scripts: a game has no clock, files or system "
                                            "of its own, and plays the same on every machine");
    return true;
  }
};

std::string ArgsOf(Luau::AstExprFunction *f) {
  std::string s;
  for (Luau::AstLocal *a : f->args)
    s += std::string(s.empty() ? "" : ", ") + a->name.value;
  if (f->vararg)
    s += std::string(s.empty() ? "" : ", ") + "...";
  return s;
}

}  // namespace

bool ModLuaLint(const std::string &text, std::vector<ModLuaProblem> *pProblems, std::vector<ModLuaFunction> *pFunctions) {
  Luau::Allocator allocator;
  Luau::AstNameTable names(allocator);
  Luau::ParseOptions options;
  const Luau::ParseResult r = Luau::Parser::parse(text.data(), text.size(), names, allocator, options);
  const size_t nBefore = pProblems->size();
  for (const Luau::ParseError &e : r.errors) {
    ModLuaProblem p;
    p.nLine = static_cast<int>(e.getLocation().begin.line) + 1;
    p.nCol = static_cast<int>(e.getLocation().begin.column);
    p.nEndLine = static_cast<int>(e.getLocation().end.line) + 1;
    p.nEndCol = static_cast<int>(e.getLocation().end.column);
    p.what = e.getMessage();
    pProblems->push_back(p);
  }
  if (r.root == nullptr)
    return false;
  if (r.errors.empty()) {
    Lint lint;
    lint.pOut = pProblems;
    r.root->visit(&lint);
  }
  // the functions the script's top gives globals, which hooks and blocks name
  for (Luau::AstStat *s : r.root->body) {
    if (auto *f = s->as<Luau::AstStatFunction>()) {
      if (auto *g = f->name->as<Luau::AstExprGlobal>())
        pFunctions->push_back({g->name.value, ArgsOf(f->func), static_cast<int>(f->location.begin.line) + 1});
    } else if (auto *a = s->as<Luau::AstStatAssign>()) {
      for (size_t i = 0; i < a->vars.size && i < a->values.size; ++i) {
        auto *g = a->vars.data[i]->as<Luau::AstExprGlobal>();
        auto *fn = a->values.data[i]->as<Luau::AstExprFunction>();
        if (g && fn)
          pFunctions->push_back({g->name.value, ArgsOf(fn), static_cast<int>(a->location.begin.line) + 1});
      }
    }
  }
  return pProblems->size() == nBefore;
}

// The math library's members the compiler is not to turn into the VM's own built-in calls, as the
// scripts' math has deterministic ones of its own in their place (port_modlua.cpp).
static const char *const kOwnMath[] = {
    "math.sin",  "math.cos",   "math.tan",  "math.asin", "math.acos",  "math.atan",  "math.atan2", "math.exp",
    "math.log",  "math.log10", "math.pow",  "math.sinh", "math.cosh",  "math.tanh",  "math.noise", "math.ldexp",
    "math.frexp", "math.random", "math.randomseed", nullptr};

bool ModLuaCompile(const std::string &chunk, const std::string &text, std::string *pBytecode, std::string *pWhy) {
  lua_CompileOptions o;
  memset(&o, 0, sizeof o);
  o.optimizationLevel = 1;
  o.debugLevel = 1;
  o.disabledBuiltins = kOwnMath;
  size_t n = 0;
  char *p = luau_compile(text.data(), text.size(), &o, &n);
  if (p == nullptr) {
    *pWhy = chunk + ": does not compile";
    return false;
  }
  pBytecode->assign(p, n);
  free(p);
  // a failed compile is bytecode version 0 followed by the message
  if (n > 0 && (*pBytecode)[0] == 0) {
    *pWhy = chunk + pBytecode->substr(1);
    return false;
  }
  return true;
}
