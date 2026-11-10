// studio_kit.cpp -- the pieces Hyperion Studio's frame is drawn from (studio_ui.h): the design's
// stroke glyphs, wide spaced capitals, buttons, segmented choices, chips, key caps, pulldowns,
// fields and panes, each drawn as the design's mock draws it (design/56-studio/cs-shell.css and
// design/55-looks-studio/studio.css) over Dear ImGui's items, so keyboard focus, hovering and
// clicks behave as Dear ImGui's own widgets do.
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>
#include "imgui.h"
#include "imgui_internal.h"
#include "studio_ui.h"

namespace {

// ---- glyphs ------------------------------------------------------------------------------------------
// The design's glyphs as its SVG has them (cs-shell.js GL), 16 x 16, stroked 1.6 wide.
struct GlyphDef {
  const char *pszName;
  const char *pszSvg;
};
const GlyphDef kGlyphs[] = {
    {"text", "<path d=\"M3 3.5h10M8 3.5v9.5M6 13h4\"/>"},
    {"chart", "<path d=\"M2 14h12M4 12V8M8 12V3.5M12 12V6.5\"/>"},
    {"status", "<circle cx=\"8\" cy=\"8\" r=\"5.5\"/><path d=\"M8 4.5V8l2.5 1.5\"/>"},
    {"tiles", "<path d=\"M2 2h5v5H2zM9 2h5v5H9zM2 9h5v5H2zM9 9h5v5H9z\"/>"},
    {"unit", "<circle cx=\"8\" cy=\"5\" r=\"2.6\"/><path d=\"M3.2 14c0-3 2.2-5.2 4.8-5.2s4.8 2.2 4.8 5.2\"/>"},
    {"weapon", "<path d=\"M3 13L12.5 3.5M10.5 3l2.5 2.5M3 13l-1 1M4.6 9.4l2 2\"/>"},
    {"action", "<path d=\"M9 1.5L4 9h4l-1 5.5L12 7H8z\"/>"},
    {"upgrade", "<path d=\"M8 13V4M4 8l4-4 4 4M4 13.5h8\"/>"},
    {"tech", "<path d=\"M6 2h4M6.5 2v4L3 13.5h10L9.5 6V2M4.6 10h6.8\"/>"},
    {"sprite", "<rect x=\"2\" y=\"3\" width=\"5\" height=\"10\" rx=\".5\"/><rect x=\"9\" y=\"3\" width=\"5\" height=\"10\" rx=\".5\"/>"},
    {"sound", "<path d=\"M2 6h3l4-3v10l-4-3H2zM11.5 5.5a3.5 3.5 0 0 1 0 5\"/>"},
    {"look", "<circle cx=\"8\" cy=\"8\" r=\"3\"/><circle cx=\"8\" cy=\"8\" r=\"6\" dash=\"1\"/>"},
    {"race", "<path d=\"M8 1.8l5.4 3.1v6.2L8 14.2l-5.4-3.1V4.9z\"/>"},
    {"home", "<path d=\"M2 8l6-5 6 5M4 6.6V14h8V6.6\"/>"},
    {"make", "<rect x=\"2\" y=\"2\" width=\"12\" height=\"12\" rx=\"2\"/><path d=\"M8 5v6M5 8h6\"/>"},
    {"table", "<path d=\"M2 3h12v10H2zM2 6.5h12M2 10h12M6 3v10\"/>"},
    {"image", "<rect x=\"2\" y=\"3\" width=\"12\" height=\"10\" rx=\"1\"/><path d=\"M2 11l3.5-3.5 3 3 2-2L14 12\"/><circle cx=\"10.5\" cy=\"6\" r=\"1\"/>"},
    {"cards", "<path d=\"M2 2h3.5v3.5H2zM6.25 2h3.5v3.5h-3.5zM10.5 2H14v3.5h-3.5zM2 6.25h3.5v3.5H2zM6.25 6.25h3.5v3.5h-3.5zM10.5 6.25H14v3.5h-3.5zM2 10.5h3.5V14H2zM6.25 10.5h3.5V14h-3.5zM10.5 10.5H14V14h-3.5z\"/>"},
    {"fx", "<path d=\"M8 1.5l1.6 4.2 4.4.3-3.4 2.8 1.1 4.3L8 10.7l-3.7 2.4 1.1-4.3L2 6l4.4-.3z\"/>"},
    {"check", "<path d=\"M8 1.5l5.5 2v4.5c0 3-2.5 5.5-5.5 6.5C5 13.5 2.5 11 2.5 8V3.5z\"/><path d=\"M5.5 8l2 2 3.5-4\"/>"},
    {"game", "<rect x=\"2\" y=\"3\" width=\"12\" height=\"9\" rx=\"1\"/><path d=\"M6 14h4\"/>"},
    {"undo", "<path d=\"M5 3L2 6l3 3M2 6h7.5a3.5 3.5 0 0 1 0 7H6\"/>"},
    {"redo", "<path d=\"M11 3l3 3-3 3M14 6H6.5a3.5 3.5 0 0 0 0 7H10\"/>"},
    {"push", "<path d=\"M8 13V3M4 7l4-4 4 4M3 14h10\"/>"},
    {"search", "<circle cx=\"7\" cy=\"7\" r=\"4.5\"/><path d=\"M10.5 10.5L14 14\"/>"},
    {"save", "<path d=\"M3 2h8l2 2v9H3zM5 2v4h5V2M5 13V9h6v4\"/>"},
    {"try", "<circle cx=\"8\" cy=\"8\" r=\"6\"/><path d=\"M6.6 5.4l4 2.6-4 2.6z\"/>"},
    {"filter", "<path d=\"M2 3h12l-4.5 5.5V13l-3-1.5v-3z\"/>"},
    {"needs", "<path d=\"M8 2l6.5 11.5h-13z\"/><path d=\"M8 6.5v3.2M8 11.6v.01\"/>"},
    {"note", "<circle cx=\"8\" cy=\"8\" r=\"6\"/><path d=\"M8 7.4v4M8 5v.01\"/>"},
    {"clash", "<rect x=\"2\" y=\"2\" width=\"12\" height=\"12\" rx=\"1\"/><path d=\"M5 5l6 6M11 5l-6 6\"/>"},
    {"engine", "<circle cx=\"8\" cy=\"8\" r=\"2.5\"/><path d=\"M8 1v2M8 13v2M1 8h2M13 8h2M3 3l1.5 1.5M11.5 11.5L13 13M3 13l1.5-1.5M11.5 4.5L13 3\"/>"},
    {"done", "<circle cx=\"8\" cy=\"8\" r=\"6\"/><path d=\"M5.4 8.2l1.8 1.8 3.4-3.8\"/>"},
    {"link", "<path d=\"M7 9l2-2M6 11l-1.5 1.5a2.5 2.5 0 0 1-3.5-3.5L3.5 7.5M10 5l1.5-1.5a2.5 2.5 0 0 1 3.5 3.5L12.5 8.5\"/>"},
    {"dock", "<rect x=\"2\" y=\"2\" width=\"12\" height=\"12\" rx=\"1\"/><path d=\"M2 10h12\"/>"},
    {"caret", "<path d=\"M6 4l4 4-4 4\"/>"},
    {"down", "<path d=\"M4 6l4 4 4-4\"/>"},
    {"up", "<path d=\"M4 10l4-4 4 4\"/>"},
    {"x", "<path d=\"M4 4l8 8M12 4l-8 8\"/>"},
    {"plus", "<path d=\"M8 3v10M3 8h10\"/>"},
    {"pause", "<path d=\"M5 3v10M11 3v10\"/>"},
    {"play", "<path d=\"M5 3l8 5-8 5z\"/>"},
    {"again", "<path d=\"M13 8a5 5 0 1 1-2-4M13 2v3h-3\"/>"},
    {"copy", "<path d=\"M5 5h8v8H5zM3 11V3h8\"/>"},
    {"pen", "<path d=\"M11 2l3 3-8 8H3v-3z\"/>"},
    {"back", "<path d=\"M6 4L2 8l4 4M2 8h12\"/>"},
    {"claude", "<path d=\"M8 2l1.8 4 4.2.4-3.2 2.8 1 4.2L8 11.2 4.2 13.4l1-4.2L2 6.4 6.2 6z\"/>"},
};

struct Line {
  std::vector<ImVec2> pts;
  bool bClosed = false;
};
typedef std::vector<Line> Glyph;
std::map<std::string, Glyph> g_glyphs;

const float kPi = 3.14159265f;

void ArcPoints(Line *pLine, float cx, float cy, float r, float a0, float a1) {
  const int n = static_cast<int>(ceilf(fabsf(a1 - a0) / (kPi / 12.0f))) + 1;
  for (int i = 1; i <= n; ++i) {
    const float a = a0 + (a1 - a0) * i / n;
    pLine->pts.push_back(ImVec2(cx + r * cosf(a), cy + r * sinf(a)));
  }
}

// An attribute's number in a tag, or 0.
float Attr(const std::string &tag, const char *pszName) {
  const std::string key = std::string(" ") + pszName + "=\"";
  const size_t at = tag.find(key);
  return at == std::string::npos ? 0.0f : static_cast<float>(atof(tag.c_str() + at + key.size()));
}

// An SVG arc from the pen to (x, y), as its points (SVG's endpoint form to its centre form).
void SvgArc(Line *pLine, ImVec2 p0, float rx, float ry, bool bLarge, bool bSweep, ImVec2 p1) {
  if (rx <= 0.0f || ry <= 0.0f) {
    pLine->pts.push_back(p1);
    return;
  }
  const float dx = (p0.x - p1.x) / 2.0f, dy = (p0.y - p1.y) / 2.0f;
  float lambda = (dx * dx) / (rx * rx) + (dy * dy) / (ry * ry);
  if (lambda > 1.0f) {
    rx *= sqrtf(lambda);
    ry *= sqrtf(lambda);
  }
  float num = rx * rx * ry * ry - rx * rx * dy * dy - ry * ry * dx * dx;
  const float den = rx * rx * dy * dy + ry * ry * dx * dx;
  float k = den > 0.0f ? sqrtf(num > 0.0f ? num / den : 0.0f) : 0.0f;
  if (bLarge == bSweep)
    k = -k;
  const float cxp = k * rx * dy / ry, cyp = -k * ry * dx / rx;
  const float cx = cxp + (p0.x + p1.x) / 2.0f, cy = cyp + (p0.y + p1.y) / 2.0f;
  const float a0 = atan2f((dy - cyp) / ry, (dx - cxp) / rx);
  float da = atan2f((-dy - cyp) / ry, (-dx - cxp) / rx) - a0;
  if (bSweep && da < 0.0f)
    da += 2.0f * kPi;
  else if (!bSweep && da > 0.0f)
    da -= 2.0f * kPi;
  const int n = static_cast<int>(fabsf(da) / (kPi / 12.0f)) + 2;
  for (int i = 1; i <= n; ++i) {
    const float a = a0 + da * i / n;
    pLine->pts.push_back(ImVec2(cx + rx * cosf(a), cy + ry * sinf(a)));
  }
}

// A path's d as lines: M L H V C S A Z, each also relative.
void PathLines(const char *p, Glyph *pOut) {
  Line cur;
  ImVec2 pen(0, 0), start(0, 0), ctrl(0, 0);
  char cmd = 'M', last = 0;
  auto flush = [&](bool bClosed) {
    if (cur.pts.size() > 1 || bClosed) {
      cur.bClosed = bClosed;
      pOut->push_back(cur);
    }
    cur = Line();
  };
  auto num = [&](float *pf) {
    while (*p == ' ' || *p == ',')
      ++p;
    char *pEnd = NULL;
    *pf = static_cast<float>(strtod(p, &pEnd));
    const bool bOk = pEnd != p;
    p = pEnd;
    return bOk;
  };
  auto flag = [&]() {
    while (*p == ' ' || *p == ',')
      ++p;
    const bool b = *p == '1';
    if (*p)
      ++p;
    return b;
  };
  while (*p) {
    while (*p == ' ' || *p == ',')
      ++p;
    if (!*p)
      break;
    if (isalpha(static_cast<unsigned char>(*p)))
      cmd = *p++;
    const bool bRel = islower(static_cast<unsigned char>(cmd)) != 0;
    const ImVec2 o = bRel ? pen : ImVec2(0, 0);
    float a, b, c, d, e, f;
    switch (toupper(cmd)) {
    case 'Z':
      if (!cur.pts.empty())
        flush(true);
      pen = start;
      last = 'Z';
      continue;
    case 'M':
      if (!num(&a) || !num(&b))
        return;
      flush(false);
      pen = ImVec2(o.x + a, o.y + b);
      start = pen;
      cmd = bRel ? 'l' : 'L';  // pairs after a move are lines
      break;
    case 'L':
      if (!num(&a) || !num(&b))
        return;
      if (cur.pts.empty())
        cur.pts.push_back(pen);
      pen = ImVec2(o.x + a, o.y + b);
      cur.pts.push_back(pen);
      break;
    case 'H':
      if (!num(&a))
        return;
      if (cur.pts.empty())
        cur.pts.push_back(pen);
      pen.x = o.x + a;
      cur.pts.push_back(pen);
      break;
    case 'V':
      if (!num(&a))
        return;
      if (cur.pts.empty())
        cur.pts.push_back(pen);
      pen.y = o.y + a;
      cur.pts.push_back(pen);
      break;
    case 'C':
    case 'S': {
      ImVec2 c1, c2, end;
      if (toupper(cmd) == 'C') {
        if (!num(&a) || !num(&b) || !num(&c) || !num(&d) || !num(&e) || !num(&f))
          return;
        c1 = ImVec2(o.x + a, o.y + b);
        c2 = ImVec2(o.x + c, o.y + d);
        end = ImVec2(o.x + e, o.y + f);
      } else {
        if (!num(&c) || !num(&d) || !num(&e) || !num(&f))
          return;
        c1 = (toupper(last) == 'C' || toupper(last) == 'S') ? ImVec2(2 * pen.x - ctrl.x, 2 * pen.y - ctrl.y) : pen;
        c2 = ImVec2(o.x + c, o.y + d);
        end = ImVec2(o.x + e, o.y + f);
      }
      if (cur.pts.empty())
        cur.pts.push_back(pen);
      for (int i = 1; i <= 10; ++i) {
        const float t = i / 10.0f, u = 1.0f - t;
        cur.pts.push_back(ImVec2(u * u * u * pen.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * end.x,
                                 u * u * u * pen.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * end.y));
      }
      ctrl = c2;
      pen = end;
      break;
    }
    case 'A': {
      if (!num(&a) || !num(&b) || !num(&c))
        return;
      const bool bLarge = flag(), bSweep = flag();
      if (!num(&e) || !num(&f))
        return;
      if (cur.pts.empty())
        cur.pts.push_back(pen);
      const ImVec2 end(o.x + e, o.y + f);
      SvgArc(&cur, pen, a, b, bLarge, bSweep, end);
      pen = end;
      break;
    }
    default:
      return;
    }
    last = cmd;
  }
  flush(false);
}

const Glyph &GlyphOf(const char *pszName) {
  auto it = g_glyphs.find(pszName);
  if (it != g_glyphs.end())
    return it->second;
  Glyph &g = g_glyphs[pszName];
  const char *pszSvg = NULL;
  for (const GlyphDef &d : kGlyphs)
    if (strcmp(d.pszName, pszName) == 0)
      pszSvg = d.pszSvg;
  if (pszSvg == NULL)
    return g;
  const std::string svg = pszSvg;
  size_t at = 0;
  while ((at = svg.find('<', at)) != std::string::npos) {
    const size_t end = svg.find('>', at);
    const std::string tag = svg.substr(at, end - at);
    at = end;
    if (tag.compare(0, 7, "<circle") == 0) {
      const float cx = Attr(tag, "cx"), cy = Attr(tag, "cy"), r = Attr(tag, "r");
      const bool bDash = tag.find("dash") != std::string::npos;
      const int nSeg = bDash ? 12 : 1;
      for (int s = 0; s < nSeg; ++s) {
        Line l;
        const float a0 = 2 * kPi * s / nSeg, a1 = a0 + 2 * kPi / nSeg * (bDash ? 0.5f : 1.0f);
        l.pts.push_back(ImVec2(cx + r * cosf(a0), cy + r * sinf(a0)));
        const int n = bDash ? 3 : 32;
        for (int i = 1; i <= n; ++i) {
          const float a = a0 + (a1 - a0) * i / n;
          l.pts.push_back(ImVec2(cx + r * cosf(a), cy + r * sinf(a)));
        }
        if (!bDash) {
          l.pts.pop_back();
          l.bClosed = true;
        }
        g.push_back(l);
      }
    } else if (tag.compare(0, 5, "<rect") == 0) {
      const float x = Attr(tag, "x"), y = Attr(tag, "y"), w = Attr(tag, "width"), h = Attr(tag, "height");
      const float r = fminf(Attr(tag, "rx"), fminf(w, h) / 2.0f);
      Line l;
      l.bClosed = true;
      if (r <= 0.01f) {
        l.pts = {ImVec2(x, y), ImVec2(x + w, y), ImVec2(x + w, y + h), ImVec2(x, y + h)};
      } else {
        ArcPoints(&l, x + w - r, y + r, r, -kPi / 2, 0);
        ArcPoints(&l, x + w - r, y + h - r, r, 0, kPi / 2);
        ArcPoints(&l, x + r, y + h - r, r, kPi / 2, kPi);
        ArcPoints(&l, x + r, y + r, r, kPi, 1.5f * kPi);
      }
      g.push_back(l);
    } else if (tag.compare(0, 5, "<path") == 0) {
      const size_t d = tag.find(" d=\"");
      if (d != std::string::npos) {
        const std::string path = tag.substr(d + 4, tag.find('"', d + 4) - d - 4);
        PathLines(path.c_str(), &g);
      }
    }
  }
  return g;
}

// ---- text --------------------------------------------------------------------------------------------

// The next character of UTF-8 text, and its end.
const char *NextChar(const char *p, unsigned *pc) {
  return p + ImTextCharFromUtf8(pc, p, NULL);
}

std::string Upper(const char *psz) {
  std::string s = psz;
  for (char &c : s)
    c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  return s;
}

bool Hit(const char *pszId, ImVec2 size, bool *pbHover, bool *pbHeld, bool bEnabled = true) {
  if (!bEnabled)
    ImGui::BeginDisabled();
  const bool b = ImGui::InvisibleButton(pszId, ImVec2(fmaxf(size.x, 1.0f), fmaxf(size.y, 1.0f)));
  *pbHover = ImGui::IsItemHovered();
  *pbHeld = ImGui::IsItemActive();
  if (!bEnabled)
    ImGui::EndDisabled();
  return b && bEnabled;
}

void Gradient(ImDrawList *pDraw, ImVec2 a, ImVec2 b, ImU32 top, ImU32 bot, float r) {
  if (r <= 0.0f) {
    pDraw->AddRectFilledMultiColor(a, b, top, top, bot, bot);
    return;
  }
  // a rounded box: its middle as a gradient, its rounded ends in the colours nearest them
  pDraw->AddRectFilled(a, ImVec2(b.x, a.y + r * 2), top, r, ImDrawFlags_RoundCornersTop);
  pDraw->AddRectFilled(ImVec2(a.x, b.y - r * 2), b, bot, r, ImDrawFlags_RoundCornersBottom);
  pDraw->AddRectFilledMultiColor(ImVec2(a.x, a.y + r), ImVec2(b.x, b.y - r), top, top, bot, bot);
}

}  // namespace

void StudioGlyph(ImDrawList *pDraw, const char *pszGlyph, ImVec2 pos, float fSize, ImU32 col) {
  const Glyph &g = GlyphOf(pszGlyph);
  const float k = fSize / 16.0f;
  const float fStroke = fmaxf(1.0f, 1.6f * k);
  std::vector<ImVec2> pts;
  for (const Line &l : g) {
    pts.clear();
    for (const ImVec2 &p : l.pts)
      pts.push_back(ImVec2(pos.x + p.x * k, pos.y + p.y * k));
    if (pts.size() == 2 && fabsf(pts[0].x - pts[1].x) + fabsf(pts[0].y - pts[1].y) < 0.5f) {
      pDraw->AddCircleFilled(pts[0], fStroke * 0.6f, col);  // a dot
      continue;
    }
    pDraw->AddPolyline(pts.data(), static_cast<int>(pts.size()), col, l.bClosed ? ImDrawFlags_Closed : 0, fStroke);
    // round the open ends and the joins a little, as the design's round caps do
    if (!l.bClosed && fStroke > 1.2f) {
      pDraw->AddCircleFilled(pts.front(), fStroke * 0.45f, col);
      pDraw->AddCircleFilled(pts.back(), fStroke * 0.45f, col);
    }
  }
}

float StudioTextW(int nFont, float fPx, const char *psz, float fTrack, bool bUpper) {
  ImFont *pFont = StudioFont(nFont);
  const float fSize = StudioPx(fPx) * StudioFontEm(nFont);
  const float fEm = StudioPx(fPx);
  const std::string up = bUpper ? Upper(psz) : std::string();
  const char *p = bUpper ? up.c_str() : psz;
  if (fTrack == 0.0f)
    return pFont->CalcTextSizeA(fSize, FLT_MAX, 0.0f, p).x;
  float w = 0.0f;
  while (*p) {
    unsigned c = 0;
    const char *pNext = NextChar(p, &c);
    w += pFont->CalcTextSizeA(fSize, FLT_MAX, 0.0f, p, pNext).x + fTrack * fEm;
    p = pNext;
  }
  return w;
}

float StudioText(ImDrawList *pDraw, int nFont, float fPx, ImVec2 pos, ImU32 col, const char *psz, float fTrack,
                 bool bUpper, float fMaxW) {
  ImFont *pFont = StudioFont(nFont);
  const float fSize = StudioPx(fPx) * StudioFontEm(nFont);
  const float fEm = StudioPx(fPx);
  const std::string up = bUpper ? Upper(psz) : std::string();
  const char *p = bUpper ? up.c_str() : psz;
  pos.x = floorf(pos.x);
  pos.y = floorf(pos.y - (fSize - fEm) / 2.0f);
  const float fFull = StudioTextW(nFont, fPx, psz, fTrack, bUpper);
  const bool bCut = fMaxW > 0.0f && fFull > fMaxW;
  const float fDots = bCut ? pFont->CalcTextSizeA(fSize, FLT_MAX, 0.0f, "...").x : 0.0f;
  float x = pos.x;
  if (fTrack == 0.0f && !bCut) {
    pDraw->AddText(pFont, fSize, pos, col, p);
    return fFull;
  }
  while (*p) {
    unsigned c = 0;
    const char *pNext = NextChar(p, &c);
    const float w = pFont->CalcTextSizeA(fSize, FLT_MAX, 0.0f, p, pNext).x;
    if (bCut && x + w + fDots > pos.x + fMaxW) {
      pDraw->AddText(pFont, fSize, ImVec2(x, pos.y), col, "...");
      return fMaxW;
    }
    pDraw->AddText(pFont, fSize, ImVec2(x, pos.y), col, p, pNext);
    x += w + fTrack * fEm;
    p = pNext;
  }
  return x - pos.x;
}

void StudioLabel(int nFont, float fPx, ImU32 col, const char *psz, float fTrack, bool bUpper) {
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float w = StudioText(ImGui::GetWindowDrawList(), nFont, fPx, pos, col, psz, fTrack, bUpper);
  ImGui::Dummy(ImVec2(w, StudioPx(fPx) * 1.3f));
}

ImVec2 StudioButtonSize(const char *pszLabel, const char *pszGlyph, float fH, float fFs) {
  const float fPad = fH <= 24.0f ? 9.0f : 11.0f;
  float w = fPad * 2;
  if (pszLabel && *pszLabel)
    w += StudioTextW(STUDIO_FONT_TEXT, fFs, pszLabel) / StudioPx(1.0f);
  if (pszGlyph)
    w += (pszLabel && *pszLabel) ? 14.0f + 6.0f : 14.0f;
  if (!(pszLabel && *pszLabel) && pszGlyph)
    w = fH;
  return ImVec2(StudioPx(w), StudioPx(fH));
}

bool StudioButton(const char *pszId, const char *pszLabel, const char *pszGlyph, int nKind, float fH, float fW,
                  float fFs, bool bEnabled) {
  ImVec2 size = StudioButtonSize(pszLabel, pszGlyph, fH, fFs);
  if (fW > 0.0f)
    size.x = StudioPx(fW);
  ImVec2 a = ImGui::GetCursorScreenPos();
  bool bHover = false, bHeld = false;
  const bool bClicked = Hit(pszId, size, &bHover, &bHeld, bEnabled);
  const bool bFocus = ImGui::IsItemFocused() && ImGui::GetIO().NavVisible;
  ImDrawList *pDraw = ImGui::GetWindowDrawList();
  const float r = StudioPx(3.0f);
  ImVec2 b(a.x + size.x, a.y + size.y);
  const float fAlpha = bEnabled ? 1.0f : 0.38f;
  ImU32 ink = StudioCol(TK_BUTTON, fAlpha);
  if (nKind != SB_FLAT) {
    ImU32 top = StudioCol(TK_BTN_TOP, fAlpha), bot = StudioCol(TK_BTN_BOT, fAlpha), rim = StudioCol(TK_BTN_RIM, fAlpha);
    ink = ImGui::GetColorU32(ImVec4(0x9F / 255.0f, 0xD1 / 255.0f, 0xEB / 255.0f, fAlpha));
    switch (nKind) {
    case SB_ON:
      top = StudioCol(TK_ON_TOP, fAlpha);
      bot = StudioCol(TK_ON_BOT, fAlpha);
      rim = StudioCol(TK_BTN_RIM_HI, fAlpha);
      ink = StudioCol(TK_HI, fAlpha);
      break;
    case SB_GOOD:
      top = StudioCol(TK_GOOD_TOP, fAlpha);
      bot = StudioCol(TK_GOOD_BOT, fAlpha);
      rim = StudioCol(TK_GOOD, 0.7f * fAlpha);
      ink = StudioLight() ? StudioCol(TK_GOOD, fAlpha) : ImGui::GetColorU32(ImVec4(0.85f, 1.0f, 0.89f, fAlpha));
      break;
    case SB_GOLD:
      top = StudioCol(TK_GOLD_TOP, fAlpha);
      bot = StudioCol(TK_GOLD_BOT, fAlpha);
      rim = StudioCol(TK_GOLD_RIM, fAlpha);
      ink = StudioLight() ? StudioCol(TK_CHANGED, fAlpha) : ImGui::GetColorU32(ImVec4(1.0f, 0.95f, 0.78f, fAlpha));
      break;
    case SB_CLAUDE:
      top = ImGui::GetColorU32(ImVec4(0x12 / 255.0f, 0x03 / 255.0f, 0x1F / 255.0f, fAlpha));
      bot = ImGui::GetColorU32(ImVec4(0x3A / 255.0f, 0x12 / 255.0f, 0x60 / 255.0f, fAlpha));
      rim = StudioCol(TK_CLAUDE_RIM, fAlpha);
      ink = StudioCol(TK_CLAUDE, fAlpha);
      break;
    default:
      break;
    }
    if ((bHover || bFocus) && nKind == SB_PLAIN) {
      rim = StudioCol(TK_BTN_RIM_HI);
      ink = StudioCol(TK_HI);
    }
    if (bHeld) {
      const float k = StudioPx(0.5f);
      a = ImVec2(a.x + k, a.y + k);
      b = ImVec2(b.x - k, b.y - k);
    }
    pDraw->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1), StudioCol(TK_BLACK), r + 1);
    Gradient(pDraw, a, b, top, bot, r);
    pDraw->AddLine(ImVec2(a.x + r, b.y - StudioPx(1.0f)), ImVec2(b.x - r, b.y - StudioPx(1.0f)), StudioCol(TK_BTN_LIP),
                   StudioPx(2.0f));
    pDraw->AddRect(a, b, (bHover || bFocus) && nKind != SB_PLAIN ? StudioCol(TK_BTN_RIM_HI) : rim, r);
  } else {
    if (bHover || bFocus) {
      pDraw->AddRectFilled(a, b, StudioCol(TK_ROW), r);
      ink = StudioCol(TK_HI);
    }
  }
  // the glyph and the label, centred
  const bool bLabel = pszLabel && *pszLabel;
  const float fG = StudioPx(14.0f), fGap = StudioPx(6.0f);
  const float tw = bLabel ? StudioTextW(STUDIO_FONT_TEXT, fFs, pszLabel) : 0.0f;
  const float total = (pszGlyph ? fG : 0.0f) + (pszGlyph && bLabel ? fGap : 0.0f) + tw;
  float x = floorf((a.x + b.x - total) / 2.0f);
  const float cy = (a.y + b.y) / 2.0f;
  if (pszGlyph) {
    StudioGlyph(pDraw, pszGlyph, ImVec2(x, cy - fG / 2.0f), fG, ink);
    x += fG + (bLabel ? fGap : 0.0f);
  }
  if (bLabel)
    StudioText(pDraw, STUDIO_FONT_TEXT, fFs, ImVec2(x, cy - StudioPx(fFs) * 0.5f), ink, pszLabel);
  return bClicked;
}

float StudioSegWidth(const char *const *ppszLabels, int n, float fFs) {
  float w = 0.0f;
  for (int i = 0; i < n; ++i)
    w += StudioTextW(STUDIO_FONT_TEXT, fFs, ppszLabels[i]) + StudioPx(20.0f);
  return w + StudioPx(2.0f);
}

int StudioSeg(const char *pszId, const char *const *ppszLabels, int n, int nSel, float fH, float fFs) {
  ImDrawList *pDraw = ImGui::GetWindowDrawList();
  const ImVec2 a = ImGui::GetCursorScreenPos();
  const float h = StudioPx(fH);
  const float W = StudioSegWidth(ppszLabels, n, fFs);
  const float r = StudioPx(3.0f);
  pDraw->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(a.x + W + 1, a.y + h + 1), StudioCol(TK_BLACK), r + 1);
  Gradient(pDraw, a, ImVec2(a.x + W, a.y + h), StudioCol(TK_DD_TOP), StudioCol(TK_DD_BOT), r);
  int nOut = nSel;
  ImGui::PushID(pszId);
  float x = a.x + StudioPx(1.0f);
  for (int i = 0; i < n; ++i) {
    const float w = StudioTextW(STUDIO_FONT_TEXT, fFs, ppszLabels[i]) + StudioPx(20.0f);
    ImGui::SetCursorScreenPos(ImVec2(x, a.y));
    bool bHover = false, bHeld = false;
    ImGui::PushID(i);
    if (Hit("##s", ImVec2(w, h), &bHover, &bHeld))
      nOut = i;
    ImGui::PopID();
    const bool bOn = i == nSel;
    if (bOn) {
      Gradient(pDraw, ImVec2(x, a.y + 1), ImVec2(x + w, a.y + h - 1), StudioCol(TK_ON_TOP), StudioCol(TK_ON_BOT), 0.0f);
      pDraw->AddLine(ImVec2(x, a.y + h - StudioPx(2.0f)), ImVec2(x + w, a.y + h - StudioPx(2.0f)),
                     StudioCol(TK_SEL_LINE2), StudioPx(2.0f));
    }
    if (i > 0)
      pDraw->AddLine(ImVec2(x, a.y + 1), ImVec2(x, a.y + h - 1), StudioCol(TK_DD_RIM, 0.4f));
    StudioText(pDraw, STUDIO_FONT_TEXT, fFs, ImVec2(x + StudioPx(10.0f), a.y + (h - StudioPx(fFs)) / 2.0f),
               bOn || bHover ? StudioCol(TK_HI) : StudioCol(TK_BUTTON), ppszLabels[i]);
    x += w;
  }
  ImGui::PopID();
  pDraw->AddRect(a, ImVec2(a.x + W, a.y + h), StudioCol(TK_DD_RIM), r);
  ImGui::SetCursorScreenPos(a);
  ImGui::Dummy(ImVec2(W, h));
  return nOut;
}

float StudioChipW(const char *psz, float fH, float fFs) {
  (void)fH;
  return StudioTextW(STUDIO_FONT_WIDE, fFs, psz, 0.06f, true) + StudioPx(12.0f);
}

float StudioChip(ImDrawList *pDraw, ImVec2 pos, const char *psz, int nKind, float fH, float fFs) {
  const float w = StudioChipW(psz, fH, fFs), h = StudioPx(fH);
  ImU32 ink = StudioCol(TK_INK2), rim = StudioCol(TK_LINE2);
  switch (nKind) {
  case CH_GOOD: ink = rim = StudioCol(TK_GOOD); break;
  case CH_WARN: ink = rim = StudioCol(TK_WARN); break;
  case CH_BAD: ink = rim = StudioCol(TK_BAD); break;
  case CH_CHANGED: ink = StudioCol(TK_CHANGED); rim = StudioCol(TK_CHANGED_RIM); break;
  case CH_CLAUDE: ink = StudioCol(TK_CLAUDE); rim = StudioCol(TK_CLAUDE_RIM); break;
  case CH_GOLD:
    ink = StudioCol(TK_CHANGED);
    rim = StudioCol(TK_GOLD_RIM);
    Gradient(pDraw, pos, ImVec2(pos.x + w, pos.y + h), StudioCol(TK_GOLD_TOP), StudioCol(TK_GOLD_BOT), 0.0f);
    break;
  default: break;
  }
  pDraw->AddRect(pos, ImVec2(pos.x + w, pos.y + h), rim, StudioPx(2.0f));
  StudioText(pDraw, STUDIO_FONT_WIDE, fFs, ImVec2(pos.x + StudioPx(6.0f), pos.y + (h - StudioPx(fFs)) / 2.0f),
             ink, psz, 0.06f, true);
  return w;
}

float StudioKbd(ImDrawList *pDraw, ImVec2 pos, const char *psz, float fH) {
  const float fs = fH < 17.0f ? 9.5f : 10.5f;
  const float w = fmaxf(StudioPx(fH), StudioTextW(STUDIO_FONT_MONO, fs, psz) + StudioPx(8.0f));
  const float h = StudioPx(fH);
  pDraw->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), StudioCol(TK_KBD_BG), StudioPx(3.0f));
  pDraw->AddRect(pos, ImVec2(pos.x + w, pos.y + h), StudioCol(TK_LINE2), StudioPx(3.0f));
  pDraw->AddLine(ImVec2(pos.x + StudioPx(2.0f), pos.y + h - 1), ImVec2(pos.x + w - StudioPx(2.0f), pos.y + h - 1),
                 StudioCol(TK_LINE2), StudioPx(1.5f));
  const float tw = StudioTextW(STUDIO_FONT_MONO, fs, psz);
  StudioText(pDraw, STUDIO_FONT_MONO, fs, ImVec2(pos.x + (w - tw) / 2.0f, pos.y + (h - StudioPx(fs)) / 2.0f),
             StudioCol(TK_INK2), psz);
  return w;
}

bool StudioCheck(const char *pszId, const char *pszLabel, bool *pbOn) {
  const ImVec2 a = ImGui::GetCursorScreenPos();
  const float box = StudioPx(16.0f), h = StudioPx(26.0f);
  const float w = box + StudioPx(7.0f) + StudioTextW(STUDIO_FONT_TEXT, kStudioFsS, pszLabel) + StudioPx(8.0f);
  bool bHover = false, bHeld = false;
  const bool b = Hit(pszId, ImVec2(w, h), &bHover, &bHeld);
  if (b)
    *pbOn = !*pbOn;
  ImDrawList *pDraw = ImGui::GetWindowDrawList();
  const ImVec2 p(a.x + StudioPx(4.0f), a.y + (h - box) / 2.0f);
  Gradient(pDraw, p, ImVec2(p.x + box, p.y + box), StudioCol(TK_DD_TOP), StudioCol(TK_DD_BOT), 0.0f);
  pDraw->AddRect(p, ImVec2(p.x + box, p.y + box), bHover ? StudioCol(TK_BTN_RIM_HI) : StudioCol(TK_DD_RIM), StudioPx(2.0f));
  if (*pbOn) {
    const ImVec2 pts[] = {ImVec2(p.x + box * 0.25f, p.y + box * 0.52f), ImVec2(p.x + box * 0.43f, p.y + box * 0.7f),
                          ImVec2(p.x + box * 0.76f, p.y + box * 0.3f)};
    pDraw->AddPolyline(pts, 3, StudioCol(TK_FILL), 0, StudioPx(2.4f));
  }
  StudioText(pDraw, STUDIO_FONT_TEXT, kStudioFsS, ImVec2(p.x + box + StudioPx(7.0f), a.y + (h - StudioPx(kStudioFsS)) / 2.0f),
             bHover ? StudioCol(TK_HI) : StudioCol(TK_BUTTON), pszLabel);
  return b;
}

bool StudioSelectBegin(const char *pszId, const char *pszPreview, float fW, float fH, float fFs) {
  const ImVec2 a = ImGui::GetCursorScreenPos();
  const ImVec2 size(StudioPx(fW), StudioPx(fH));
  bool bHover = false, bHeld = false;
  ImGui::PushID(pszId);
  const bool bClick = Hit("##box", size, &bHover, &bHeld);
  const ImGuiID popup = ImGui::GetID("##list");
  ImGui::PopID();
  ImDrawList *pDraw = ImGui::GetWindowDrawList();
  const ImVec2 b(a.x + size.x, a.y + size.y);
  Gradient(pDraw, a, b, StudioCol(TK_DD_TOP), StudioCol(TK_DD_BOT), 0.0f);
  const bool bOpen = ImGui::IsPopupOpen(popup, 0);
  pDraw->AddRect(a, b, bHover || bOpen ? StudioCol(TK_BTN_RIM_HI) : StudioCol(TK_DD_RIM), StudioPx(2.0f));
  StudioText(pDraw, STUDIO_FONT_TEXT, fFs, ImVec2(a.x + StudioPx(9.0f), a.y + (size.y - StudioPx(fFs)) / 2.0f),
             bHover ? StudioCol(TK_HI) : StudioCol(TK_BUTTON), pszPreview, 0.0f, false, size.x - StudioPx(30.0f));
  StudioGlyph(pDraw, "down", ImVec2(b.x - StudioPx(19.0f), a.y + (size.y - StudioPx(12.0f)) / 2.0f), StudioPx(12.0f),
              StudioCol(TK_INK2));
  if (bClick)
    ImGui::OpenPopupEx(popup);
  ImGui::SetNextWindowPos(ImVec2(a.x, b.y + StudioPx(2.0f)));
  ImGui::SetNextWindowSizeConstraints(ImVec2(size.x, 0), ImVec2(fmaxf(size.x, StudioPx(320.0f)), StudioPx(420.0f)));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(StudioPx(4.0f), StudioPx(4.0f)));
  const bool bList = ImGui::BeginPopupEx(popup, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
  ImGui::PopStyleVar();
  return bList;
}

void StudioSelectEnd(void) {
  ImGui::EndPopup();
}

bool StudioItem(const char *pszLabel, bool bSelected, const char *pszKeys, bool bEnabled) {
  const ImVec2 a = ImGui::GetCursorScreenPos();
  const float h = StudioPx(28.0f);
  const float fKeys = pszKeys ? StudioTextW(STUDIO_FONT_MONO, kStudioFsXs, pszKeys) + StudioPx(24.0f) : 0.0f;
  const float wText = StudioTextW(STUDIO_FONT_TEXT, kStudioFsS, pszLabel) + StudioPx(40.0f) + fKeys;
  const float w = fmaxf(ImGui::GetContentRegionAvail().x, fmaxf(wText, StudioPx(220.0f)));
  bool bHover = false, bHeld = false;
  ImGui::PushID(pszLabel);
  const bool b = Hit("##it", ImVec2(w, h), &bHover, &bHeld, bEnabled);
  ImGui::PopID();
  ImDrawList *pDraw = ImGui::GetWindowDrawList();
  if ((bHover || (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible)) && bEnabled)
    pDraw->AddRectFilled(a, ImVec2(a.x + w, a.y + h), StudioCol(TK_MENU_SEL), StudioPx(2.0f));
  const float fAlpha = bEnabled ? 1.0f : 0.4f;
  const float ty = a.y + (h - StudioPx(kStudioFsS)) / 2.0f;
  if (bSelected)
    StudioGlyph(pDraw, "done", ImVec2(a.x + StudioPx(7.0f), a.y + (h - StudioPx(13.0f)) / 2.0f), StudioPx(13.0f),
                StudioCol(TK_FILL, fAlpha));  // the text faces have no check mark
  StudioText(pDraw, STUDIO_FONT_TEXT, kStudioFsS, ImVec2(a.x + StudioPx(26.0f), ty),
             bHover && bEnabled ? StudioCol(TK_HI) : StudioCol(TK_INK, fAlpha), pszLabel);
  if (pszKeys)
    StudioText(pDraw, STUDIO_FONT_MONO, kStudioFsXs,
               ImVec2(a.x + w - StudioTextW(STUDIO_FONT_MONO, kStudioFsXs, pszKeys) - StudioPx(10.0f), ty + StudioPx(1.0f)),
               StudioCol(TK_INK3, fAlpha), pszKeys);
  if (b && GImGui->BeginPopupStack.Size > 0)
    ImGui::CloseCurrentPopup();
  return b;
}

void StudioItemSeparator(void) {
  const ImVec2 a = ImGui::GetCursorScreenPos();
  const float w = ImGui::GetContentRegionAvail().x;
  ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x + StudioPx(6.0f), a.y + StudioPx(4.0f)),
                                      ImVec2(a.x + w - StudioPx(6.0f), a.y + StudioPx(4.0f)), StudioCol(TK_LINE));
  ImGui::Dummy(ImVec2(w, StudioPx(9.0f)));
}

bool StudioInput(const char *pszId, char *psz, int nSize, float fW, float fH, const char *pszHint,
                 const char *pszGlyph, const char *pszKeys, bool bMono, float fFs) {
  ImDrawList *pDraw = ImGui::GetWindowDrawList();
  const ImVec2 a = ImGui::GetCursorScreenPos();
  const ImVec2 size(StudioPx(fW), StudioPx(fH));
  const ImVec2 b(a.x + size.x, a.y + size.y);
  pDraw->AddRectFilled(a, b, StudioCol(TK_WELL), StudioPx(2.0f));
  const float fLeft = pszGlyph ? StudioPx(28.0f) : StudioPx(8.0f);
  float fRight = StudioPx(6.0f);
  if (pszKeys) {
    // the key caps at the right, "Ctrl K" as two
    std::string keys = pszKeys;
    std::vector<std::string> caps;
    size_t s = 0;
    while (s <= keys.size()) {
      const size_t e = keys.find(' ', s);
      caps.push_back(keys.substr(s, e == std::string::npos ? std::string::npos : e - s));
      if (e == std::string::npos)
        break;
      s = e + 1;
    }
    float x = b.x - StudioPx(6.0f);
    for (size_t i = caps.size(); i-- > 0;) {
      const float w = fmaxf(StudioPx(15.0f), StudioTextW(STUDIO_FONT_MONO, 9.5f, caps[i].c_str()) + StudioPx(8.0f));
      x -= w;
      StudioKbd(pDraw, ImVec2(x, a.y + (size.y - StudioPx(15.0f)) / 2.0f), caps[i].c_str(), 15.0f);
      x -= StudioPx(3.0f);
    }
    fRight = b.x - x + StudioPx(6.0f);
  }
  if (pszGlyph)
    StudioGlyph(pDraw, pszGlyph, ImVec2(a.x + StudioPx(8.0f), a.y + (size.y - StudioPx(14.0f)) / 2.0f), StudioPx(14.0f),
                StudioCol(TK_INK3));
  const int nFont = bMono ? STUDIO_FONT_MONO : STUDIO_FONT_TEXT;
  const float fText = StudioPx(fFs) * StudioFontEm(nFont);
  ImGui::SetCursorScreenPos(ImVec2(a.x + fLeft, a.y));
  StudioPushFont(nFont, fFs);
  float fPadX = 0.0f;
  if (bMono) {
    // right-aligned: the padding that puts the text's end at the right
    const float tw = StudioTextW(nFont, fFs, psz[0] == '\0' && pszHint ? pszHint : psz);  // an empty field shows its hint
    fPadX = fmaxf(0.0f, size.x - fLeft - fRight - tw - StudioPx(4.0f));
  }
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(fPadX, (size.y - fText) / 2.0f - StudioPx(1.0f)));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Text, StudioCol(TK_HI));
  ImGui::PushStyleColor(ImGuiCol_TextDisabled, StudioCol(TK_INK3));
  ImGui::SetNextItemWidth(size.x - fLeft - fRight);
  const bool bChanged = pszHint ? ImGui::InputTextWithHint(pszId, pszHint, psz, nSize)
                                : ImGui::InputText(pszId, psz, nSize);
  const bool bActive = ImGui::IsItemActive();
  const bool bHover = ImGui::IsItemHovered();
  const ImGuiLastItemData input = ImGui::GetCurrentContext()->LastItemData;
  ImGui::PopStyleColor(5);
  ImGui::PopStyleVar();
  ImGui::PopFont();
  pDraw->AddRect(a, b, bActive ? StudioCol(TK_BTN_RIM_HI) : bHover ? StudioCol(TK_LINE2) : StudioCol(TK_DD_RIM),
                 StudioPx(2.0f));
  ImGui::SetCursorScreenPos(a);
  ImGui::Dummy(size);
  // the last item's state the text's, not the frame's, so a caller's IsItemActive and
  // IsItemDeactivatedAfterEdit see the typing; its rectangle stays the whole field's
  ImGuiLastItemData &last = ImGui::GetCurrentContext()->LastItemData;
  last.ID = input.ID;
  last.ItemFlags = input.ItemFlags;
  last.StatusFlags = input.StatusFlags;
  return bChanged;
}

bool StudioNumber(const char *pszId, double *pd, float fW, float fH, bool bMine, float fFs) {
  // the text being typed, kept per field while it has the keyboard
  static std::map<ImGuiID, std::string> s_typing;
  const ImGuiID id = ImGui::GetID(pszId);
  char sz[64];
  auto it = s_typing.find(id);
  if (it != s_typing.end()) {
    snprintf(sz, sizeof sz, "%s", it->second.c_str());
  } else if (*pd == floor(*pd)) {
    snprintf(sz, sizeof sz, "%.0f", *pd);
  } else {
    snprintf(sz, sizeof sz, "%g", *pd);
  }
  if (!bMine)
    ImGui::BeginDisabled();
  StudioInput(pszId, sz, sizeof sz, fW, fH, nullptr, nullptr, nullptr, true, fFs);
  if (!bMine)
    ImGui::EndDisabled();
  const bool bTyping = GImGui->ActiveId == id;
  bool bDone = false;
  if (bTyping) {
    s_typing[id] = sz;
  } else if (it != s_typing.end() && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    s_typing.erase(id);  // Escape: the number as it was
  } else if (it != s_typing.end()) {
    // it lost the keyboard: the number typed, when it is one and differs
    char *pEnd = NULL;
    const double d = strtod(it->second.c_str(), &pEnd);
    if (pEnd != it->second.c_str() && d != *pd) {
      *pd = d;
      bDone = true;
    }
    s_typing.erase(id);
  }
  return bDone;
}

void StudioFill(ImDrawList *pDraw, ImVec2 a, ImVec2 b, ImU32 col, const ImVec4 *pHole) {
  if (pHole == nullptr || pHole->z <= 0.0f || pHole->w <= 0.0f) {
    pDraw->AddRectFilled(a, b, col);
    return;
  }
  const float hx0 = pHole->x, hy0 = pHole->y, hx1 = pHole->x + pHole->z, hy1 = pHole->y + pHole->w;
  pDraw->AddRectFilled(a, ImVec2(b.x, hy0), col);                     // above
  pDraw->AddRectFilled(ImVec2(a.x, hy1), b, col);                     // below
  pDraw->AddRectFilled(ImVec2(a.x, hy0), ImVec2(hx0, hy1), col);      // left
  pDraw->AddRectFilled(ImVec2(hx1, hy0), ImVec2(b.x, hy1), col);      // right
}

void StudioPane(ImDrawList *pDraw, ImVec2 a, ImVec2 b, float fHead, const ImVec4 *pHole) {
  StudioFill(pDraw, a, b, StudioCol(TK_PANE), pHole);
  if (fHead > 0.0f) {
    const float h = StudioPx(fHead);
    pDraw->AddRectFilled(a, ImVec2(b.x, a.y + h), StudioCol(TK_PANE2));
    pDraw->AddLine(ImVec2(a.x, a.y + h - 0.5f), ImVec2(b.x, a.y + h - 0.5f), StudioCol(TK_LINE));
  }
  pDraw->AddRect(a, b, StudioCol(TK_LINE), StudioPx(3.0f));
}

void StudioDashed(ImDrawList *pDraw, ImVec2 a, ImVec2 b, ImU32 col) {
  const float d = StudioPx(4.0f), g = StudioPx(3.0f);
  for (float x = a.x; x < b.x; x += d + g) {
    pDraw->AddLine(ImVec2(x, a.y), ImVec2(fminf(x + d, b.x), a.y), col);
    pDraw->AddLine(ImVec2(x, b.y), ImVec2(fminf(x + d, b.x), b.y), col);
  }
  for (float y = a.y; y < b.y; y += d + g) {
    pDraw->AddLine(ImVec2(a.x, y), ImVec2(a.x, fminf(y + d, b.y)), col);
    pDraw->AddLine(ImVec2(b.x, y), ImVec2(b.x, fminf(y + d, b.y)), col);
  }
}
