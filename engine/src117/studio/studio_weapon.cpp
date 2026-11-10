// studio_weapon.cpp -- a weapon's document above its numbers (the design's mock, cs-unit.js weaponHead):
// what it does in a sentence, the units that fire it, its range drawn from above with its splash at
// the target, what an attack does against small, medium and large, and the attacks it takes to kill
// the classic units the studio compares with, without upgrades and with both sides at 3. The
// numbers are the classic rules' (studio_calc.cpp).
#include <algorithm>
#include <utility>
#include <vector>
#include "studio_draw.h"

namespace {

using namespace studio_draw;

// Zealot, Marine, Zergling, Dragoon, Hydralisk, Ultralisk: the units the screens compare with
const int kCompare[] = {65, 0, 37, 66, 38, 39};

struct User {
  std::string unit;
  const char *pszAs;
};

// The units that fire a weapon, the mod's first; looked for again when the mod changes.
const std::vector<User> &UsersOf(const std::string &weapon) {
  static std::vector<User> s_list;
  static std::string s_weapon;
  static unsigned s_nChange = ~0u;
  if (s_nChange == StudioChange() && s_weapon == weapon)
    return s_list;
  s_list.clear();
  s_weapon = weapon;
  s_nChange = StudioChange();
  StudioRecord w;
  if (!StudioRecordOf(ST_WEAPONS, weapon, &w))
    return s_list;
  auto check = [&](const std::string &unit) {
    const long long g = StudioNumber(ST_UNITS, unit, "ground_weapon", -1), a = StudioNumber(ST_UNITS, unit, "air_weapon", -1);
    if (g == w.nId || a == w.nId)
      s_list.push_back({unit, g == w.nId && a == w.nId ? "ground and air" : g == w.nId ? "ground" : "air"});
  };
  for (const StudioRecord &r : StudioRecords(ST_UNITS))
    if (r.kind == SK_OWN)
      check(r.name);
  for (int n = 0; n < StudioClassicRows(ST_UNITS); ++n)
    if (const char *psz = StudioClassicName(ST_UNITS, n))
      check(psz);
  return s_list;
}

std::string EnumWord(const char *pszEnum, long long n) {
  for (const auto &e : StudioEnum(pszEnum))
    if (e.first == n)
      return e.second;
  return std::to_string(n);
}

std::string Range(long long px) {
  if (px >= 4096)
    return "across the map";
  return px <= 32 ? std::string("melee") : Fmt(px / 32.0) + " tiles";
}

// The weapon in a sentence: its attack, how often, a second, how far.
std::string Line(const std::string &w) {
  const long long nHits = std::max(1LL, StudioNumber(ST_WEAPONS, w, "damage_factor", 1));
  const long long nDamage = StudioNumber(ST_WEAPONS, w, "damage"), nCooldown = StudioNumber(ST_WEAPONS, w, "cooldown");
  std::string s = nHits > 1 ? std::to_string(nHits) + " hits of " + std::to_string(nDamage) : std::to_string(nDamage) + " a hit";
  s += " every " + Fmt(nCooldown / 24.0, 2) + " s, " + Fmt(StudioDamagePerSecond(w)) + " a second, " +
       Range(StudioNumber(ST_WEAPONS, w, "max_range"));
  const long long nBonus = StudioNumber(ST_WEAPONS, w, "damage_bonus");
  if (nBonus > 0)
    s += "; +" + std::to_string(nBonus) + " a level of its upgrade";
  s[0] = static_cast<char>(toupper(static_cast<unsigned char>(s[0])));
  return s + ".";
}

void DashedCircle(ImDrawList *d, ImVec2 c, float r, ImU32 col, float th) {
  const int n = std::max(12, static_cast<int>(r / P(3.0f)));
  for (int i = 0; i < n; i += 2) {
    const float a0 = i * 6.2831853f / n, a1 = (i + 1) * 6.2831853f / n;
    d->AddLine(ImVec2(c.x + cosf(a0) * r, c.y + sinf(a0) * r), ImVec2(c.x + cosf(a1) * r, c.y + sinf(a1) * r), col, th);
  }
}

// The range from above: the attacker, its full and minimum range, the splash's three rings at a
// target at the edge of its reach; a square is a tile.
void RangeDrawing(ImDrawList *d, ImVec2 a, ImVec2 b, const std::string &w) {
  d->AddRectFilled(a, b, C(TK_WELL), P(3.0f));
  d->PushClipRect(a, b, true);
  const long long nMax = StudioNumber(ST_WEAPONS, w, "max_range"), nMin = StudioNumber(ST_WEAPONS, w, "min_range");
  const long long anSplash[] = {StudioNumber(ST_WEAPONS, w, "inner_splash"), StudioNumber(ST_WEAPONS, w, "middle_splash"),
                                StudioNumber(ST_WEAPONS, w, "outer_splash")};
  const bool bFar = nMax >= 4096;
  const float fRange = static_cast<float>(bFar ? 160 : std::max(nMax, 16LL));
  const float W = b.x - a.x, H = b.y - a.y;
  const float k = std::min({P(1.4f), (H / 2.0f - P(16.0f)) / fRange, (W / 2.0f - P(16.0f)) / (fRange + anSplash[2])});
  const ImVec2 c(floorf(a.x + W / 2.0f - (anSplash[2] * k) / 2.0f), floorf(a.y + H / 2.0f));
  const float tile = 32.0f * k;
  if (tile >= 4.0f) {
    for (float x = fmodf(c.x - a.x, tile); x < W; x += tile)
      d->AddLine(ImVec2(a.x + x, a.y), ImVec2(a.x + x, b.y), C(TK_LINE, 0.45f));
    for (float y = fmodf(c.y - a.y, tile); y < H; y += tile)
      d->AddLine(ImVec2(a.x, a.y + y), ImVec2(b.x, a.y + y), C(TK_LINE, 0.45f));
  }
  d->AddCircle(c, fRange * k, C(TK_SEL_LINE), 96, P(2.0f));
  if (nMin > 0)
    DashedCircle(d, c, nMin * k, C(TK_WARN), P(1.5f));
  d->AddCircleFilled(c, P(8.0f), C(TK_GOOD));
  const ImVec2 t(c.x + fRange * k, c.y);
  static const float kAlpha[] = {0.45f, 0.3f, 0.18f};
  for (int i = 2; i >= 0; --i)
    if (anSplash[i] > 0)
      d->AddCircleFilled(t, anSplash[i] * k, C(TK_BAD, kAlpha[i]), 48);
  d->AddCircleFilled(t, P(5.0f), C(TK_BAD));
  auto label = [&](const std::string &s, float y) {
    const float tw = TW(STUDIO_FONT_TEXT, kStudioFsXs, s);
    d->AddRectFilled(ImVec2(a.x + P(6.0f), y - P(2.0f)), ImVec2(a.x + P(14.0f) + tw, y + P(15.0f)), C(TK_PANE, 0.85f), P(2.0f));
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, a.x + P(10.0f), y, C(TK_INK), s);
  };
  label(bFar ? std::string("reaches across the map") : Range(nMax) + ", " + std::to_string(nMax) + " px" +
                                                            (nMin > 0 ? "; not nearer than " + std::to_string(nMin) + " px" : ""),
        a.y + P(8.0f));
  if (anSplash[2] > 0)
    label("splash " + std::to_string(anSplash[0]) + " / " + std::to_string(anSplash[1]) + " / " + std::to_string(anSplash[2]) + " px",
          a.y + P(28.0f));
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, a.x + P(10.0f), b.y - P(20.0f), C(TK_INK3), "a square is a tile, 32 px");
  d->PopClipRect();
  d->AddRect(a, b, C(TK_LINE), P(3.0f));
}

}  // namespace

void StudioWeaponHead(const std::string &name) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  StudioRecord rec;
  if (!StudioRecordOf(ST_WEAPONS, name, &rec))
    return;
  const float w = ImGui::GetContentRegionAvail().x;
  ImVec2 o = ImGui::GetCursorScreenPos();
  o.x += P(22.0f);
  o.y += P(16.0f);
  const float W = w - P(44.0f);
  // the drawing at the right, the words at the left
  const float dw = P(340.0f), dh = P(230.0f);
  const bool bSide = W > P(760.0f);
  const float lw = bSide ? W - dw - P(24.0f) : W;
  float y = o.y;
  T(d, STUDIO_FONT_BOLD, 24.0f, o.x, y, C(TK_HI), StudioShowName(ST_WEAPONS, name), lw);
  y += P(36.0f);
  float cx = o.x;
  if (rec.kind == SK_CLASSIC)
    cx += StudioChip(d, ImVec2(cx, y), "classic, read only", CH_GOLD) + P(6.0f);
  else if (!rec.base.empty())
    cx += StudioChip(d, ImVec2(cx, y), ("based on " + Short(ST_WEAPONS, rec.base)).c_str()) + P(6.0f);
  cx += StudioChip(d, ImVec2(cx, y), EnumWord("damage_type", StudioNumber(ST_WEAPONS, name, "damage_type", 3)).c_str()) + P(6.0f);
  StudioChip(d, ImVec2(cx, y), EnumWord("explosion", StudioNumber(ST_WEAPONS, name, "explosion")).c_str());
  y += P(28.0f);
  ImGui::SetCursorScreenPos(ImVec2(o.x, y));
  ImGui::PushTextWrapPos(o.x + lw - ImGui::GetWindowPos().x);
  StudioPushFont(STUDIO_FONT_TEXT, 15.0f);
  ImGui::PushStyleColor(ImGuiCol_Text, C(TK_INK));
  ImGui::TextWrapped("%s", Line(name).c_str());
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::PopTextWrapPos();
  y = ImGui::GetCursorScreenPos().y + P(12.0f);
  // the units that fire it, each a chip that opens it
  Wide(d, 9.5f, o.x, y + P(5.0f), C(TK_INK3), "Used by", 0.1f);
  float ux = o.x + P(78.0f), uy = y;
  const std::vector<User> &users = UsersOf(name);
  if (users.empty())
    T(d, STUDIO_FONT_TEXT, kStudioFsS, ux, y + P(2.0f), C(TK_INK3), "nothing yet");
  int nShown = 0;
  for (const User &u : users) {
    const std::string label = StudioShowName(ST_UNITS, u.unit) + (strcmp(u.pszAs, "ground") ? std::string(", ") + u.pszAs : std::string());
    const float bw = StudioButtonSize(label.c_str(), "unit", 24.0f, kStudioFsXs).x;
    if (ux + bw > o.x + lw) {
      ux = o.x + P(78.0f);
      uy += P(30.0f);
    }
    if (++nShown > 12) {
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, ux, uy + P(5.0f), C(TK_INK3), "and " + std::to_string(users.size() - 12) + " more");
      break;
    }
    ImGui::SetCursorScreenPos(ImVec2(ux, uy));
    ImGui::PushID(u.unit.c_str());
    if (StudioButton("##user", label.c_str(), "unit", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs))
      StudioOpenDoc(ST_UNITS, u.unit, 0);
    ImGui::PopID();
    ux += bw + P(6.0f);
  }
  y = uy + P(36.0f);
  float top = y;
  if (bSide)
    RangeDrawing(d, ImVec2(o.x + W - dw, o.y), ImVec2(o.x + W, o.y + dh), name);
  else {
    RangeDrawing(d, ImVec2(o.x, y), ImVec2(o.x + std::min(W, dw), y + dh), name);
    y += dh + P(16.0f);
  }
  y = std::max(y, bSide ? o.y + dh + P(18.0f) : y);
  (void)top;
  // against each size: an attack at armour 0, a second, a bar against its full damage, at armour 2
  const long long nHits = std::max(1LL, StudioNumber(ST_WEAPONS, name, "damage_factor", 1));
  const double dCooldown = std::max(1LL, StudioNumber(ST_WEAPONS, name, "cooldown")) / 24.0;
  const double dFull = std::max(1LL, StudioAttackDamage(name, 0)) * static_cast<double>(nHits);
  float hx = o.x + T(d, STUDIO_FONT_BOLD, 16.0f, o.x, y, C(TK_HEAD), "Against small, medium and large") + P(10.0f);
  T(d, STUDIO_FONT_TEXT, kStudioFsS, hx, y + P(3.0f), C(TK_INK3), "an attack after the damage type and size, at armour 0, 2, and with its upgrade at 3");
  y += P(30.0f);
  static const char *const kSize[] = {"", "Small", "Medium", "Large"};
  const float rh = P(34.0f);
  for (int nSize = 1; nSize <= 3; ++nSize) {
    const double d0 = StudioHitDamage(name, 0, nSize, 0) * nHits, d2 = StudioHitDamage(name, 0, nSize, 2) * nHits;
    const double d3 = StudioHitDamage(name, 3, nSize, 0) * nHits;
    d->AddLine(ImVec2(o.x, y + rh - 0.5f), ImVec2(o.x + W, y + rh - 0.5f), C(TK_ROW));
    T(d, STUDIO_FONT_TEXT, 14.0f, o.x + P(4.0f), Mid(y, rh, 14.0f), C(TK_INK), kSize[nSize]);
    float x = o.x + P(90.0f);
    x += T(d, STUDIO_FONT_BOLD, 17.0f, x, Mid(y, rh, 17.0f), C(TK_HI), Fmt(d0)) + P(6.0f);
    T(d, STUDIO_FONT_TEXT, kStudioFsS, x, Mid(y, rh, kStudioFsS), C(TK_INK2), "an attack, " + Fmt(d0 / dCooldown) + " a second");
    const float bx = o.x + P(330.0f), bw = std::max(P(60.0f), std::min(P(260.0f), W - P(620.0f)));
    d->AddRectFilled(ImVec2(bx, y + rh / 2 - P(4.0f)), ImVec2(bx + bw, y + rh / 2 + P(4.0f)), C(TK_WELL), P(3.0f));
    d->AddRectFilled(ImVec2(bx, y + rh / 2 - P(4.0f)), ImVec2(bx + bw * static_cast<float>(std::min(1.0, d0 / dFull)), y + rh / 2 + P(4.0f)),
                     C(TK_SEL_LINE), P(3.0f));
    T(d, STUDIO_FONT_TEXT, kStudioFsS, bx + bw + P(16.0f), Mid(y, rh, kStudioFsS), C(TK_INK2),
      "armour 2: " + Fmt(d2) + "    upgraded: " + Fmt(d3), o.x + W - bx - bw - P(16.0f));
    y += rh;
  }
  // the attacks that kill one of each classic unit compared with
  y += P(18.0f);
  hx = o.x + T(d, STUDIO_FONT_BOLD, 16.0f, o.x, y, C(TK_HEAD), "Kills one") + P(10.0f);
  T(d, STUDIO_FONT_TEXT, kStudioFsS, hx, y + P(3.0f), C(TK_INK3), "attacks and seconds, without upgrades and with both sides at 3; shields take hits first");
  y += P(30.0f);
  const int nCols = W > P(900.0f) ? 3 : W > P(560.0f) ? 2 : 1;
  const float cw = (W - P(8.0f) * (nCols - 1)) / nCols, ch = P(52.0f);
  int i = 0;
  for (int nUnit : kCompare) {
    const char *psz = StudioClassicName(ST_UNITS, nUnit);
    if (psz == nullptr)
      continue;
    const ImVec2 at(o.x + (i % nCols) * (cw + P(8.0f)), y + (i / nCols) * (ch + P(8.0f)));
    ++i;
    d->AddRectFilled(at, ImVec2(at.x + cw, at.y + ch), C(TK_PANE2), P(3.0f));
    d->AddRect(at, ImVec2(at.x + cw, at.y + ch), C(TK_LINE), P(3.0f));
    T(d, STUDIO_FONT_BOLD, 14.0f, at.x + P(12.0f), at.y + P(8.0f), C(TK_INK), Short(ST_UNITS, psz));
    auto say = [&](int nLevel) {
      const int n = StudioAttacksToKill(name, nLevel, psz, nLevel);
      return n == 0 ? std::string("cannot") : std::to_string(n) + (n == 1 ? " attack, " : " attacks, ") + Fmt((n - 1) * dCooldown) + " s";
    };
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, at.x + P(12.0f), at.y + P(29.0f), C(TK_INK2), say(0) + "    at 3: " + say(3), cw - P(24.0f));
  }
  y += ((i + nCols - 1) / nCols) * (ch + P(8.0f)) + P(14.0f);
  d->AddLine(ImVec2(o.x, y), ImVec2(o.x + W, y), C(TK_LINE));
  ImGui::SetCursorScreenPos(ImVec2(o.x - P(22.0f), y + P(4.0f)));
  ImGui::Dummy(ImVec2(1, 1));
}
