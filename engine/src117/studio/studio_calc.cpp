// studio_calc.cpp -- what a mod's numbers work out to, by the classic rules: damage against a
// target's size and armour, attacks to kill, damage a second, speed. CUnit::Damage is the rule
// (lang/CUnitCombat.cpp): a hit's damage comes off the shields first, less the shields' upgrade;
// armour comes off what is left, unless the damage ignores armour; then the damage type's share
// against the target's size (compute_damage_from_types, the game's own table); a hit that reaches
// life does at least half a point. Values are in 1/256 of a point, as the game keeps them.
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <string>
#include "../h/damage.h"  // compute_damage_from_types
#include "studio_model.h"

namespace {

const int kIgnoreArmour = 4;  // the damage type that ignores armour and the shields' upgrade
const int kByScript = 2;      // a flingy's movement control when the image script moves it

bool NoneRow(int t, long long n) {
  return n < 0 || n == StudioClassicRows(t);  // the row after the classic table's last is "none"
}

}  // namespace

double StudioSizeShare(int nType, int nSize) {
  if (nType < 0 || nType > 4 || nSize < 0 || nSize > 3)
    return 1.0;
  return compute_damage_from_types(256, static_cast<BYTE>(nType), static_cast<BYTE>(nSize)) / 256.0;
}

std::string StudioUnitWeapon(const std::string &unit, bool bAir) {
  StudioValue v;
  if (!StudioGet(ST_UNITS, unit, bAir ? "air_weapon" : "ground_weapon", &v) || v.n.empty() || NoneRow(ST_WEAPONS, v.n[0]))
    return std::string();
  return StudioRefName(ST_WEAPONS, v.n[0]);
}

long long StudioAttackDamage(const std::string &weapon, int nLevel) {
  return StudioNumber(ST_WEAPONS, weapon, "damage") + StudioNumber(ST_WEAPONS, weapon, "damage_bonus") * nLevel;
}

double StudioHitDamage(const std::string &weapon, int nLevel, int nSize, int nArmour) {
  const long long nType = StudioNumber(ST_WEAPONS, weapon, "damage_type", 3);
  long long n = std::max(StudioAttackDamage(weapon, nLevel) * 256, 128LL);
  if (nType != kIgnoreArmour)
    n -= std::min(n, static_cast<long long>(nArmour) * 256);
  n = static_cast<long long>(StudioSizeShare(static_cast<int>(nType), nSize) * 256.0 + 0.5) * n >> 8;
  return std::max(n, 128LL) / 256.0;
}

int StudioAttacksToKill(const std::string &weapon, int nLevel, const std::string &target, int nArmourLevel) {
  if (weapon.empty() || StudioAttackDamage(weapon, nLevel) <= 0)
    return 0;
  const long long nType = StudioNumber(ST_WEAPONS, weapon, "damage_type", 3);
  const long long nHits = std::max(1LL, StudioNumber(ST_WEAPONS, weapon, "damage_factor", 1));
  const long long nPer = std::max(StudioAttackDamage(weapon, nLevel) * 256, 128LL);
  const int nShare = static_cast<int>(StudioSizeShare(static_cast<int>(nType), static_cast<int>(StudioNumber(ST_UNITS, target, "size"))) * 256.0 + 0.5);
  const long long nArmour = (StudioNumber(ST_UNITS, target, "armor") + nArmourLevel) * 256;
  const bool bShields = StudioNumber(ST_UNITS, target, "shields_enabled") != 0;
  long long nLife = std::max(1LL, StudioNumber(ST_UNITS, target, "hit_points")) * 256;
  long long nShields = bShields ? StudioNumber(ST_UNITS, target, "shields") * 256 : 0;
  for (int nAttacks = 1; nAttacks <= 999; ++nAttacks) {
    StudioStrikeNumbers(nPer, static_cast<int>(nHits), static_cast<int>(nType), nShare, nArmour, nArmourLevel * 256LL, &nLife, &nShields);
    if (nLife <= 0)
      return nAttacks;
  }
  return 0;
}

void StudioStrikeNumbers(long long nPer, int nHits, int nType, int nShare, long long nArmour, long long nShieldUp, long long *pnLife,
                         long long *pnShields) {
  for (int h = 0; h < nHits && *pnLife > 0; ++h) {
    long long n = nPer, nTaken = 0;
    if (*pnShields >= 256) {
      if (nType != kIgnoreArmour)
        n = nShieldUp >= n ? 128 : n - nShieldUp;
      nTaken = std::min(n, *pnShields);
      *pnShields -= nTaken;
      n -= nTaken;
    }
    if (nType != kIgnoreArmour)
      n -= std::min(n, nArmour);
    long long nHit = nShare * n >> 8;
    if (nTaken == 0 && nHit < 128)
      nHit = 128;
    *pnLife -= nHit;
  }
}

double StudioDamagePerSecond(const std::string &weapon, int nLevel) {
  const long long nCooldown = StudioNumber(ST_WEAPONS, weapon, "cooldown");
  if (weapon.empty() || nCooldown <= 0)
    return 0.0;
  const long long nHits = std::max(1LL, StudioNumber(ST_WEAPONS, weapon, "damage_factor", 1));
  return static_cast<double>(StudioAttackDamage(weapon, nLevel) * nHits) * 24.0 / static_cast<double>(nCooldown);
}

double StudioUnitSpeed(const std::string &unit) {
  StudioValue v;
  if (!StudioGet(ST_UNITS, unit, "flingy", &v) || v.n.empty())
    return 0.0;
  const std::string flingy = StudioRefName(ST_FLINGY, v.n[0]);
  if (StudioNumber(ST_FLINGY, flingy, "movement_control") == kByScript) {
    // the Walking animation's moves set its speed; the flingy's is no measure of it
    const long long nSprite = StudioNumber(ST_FLINGY, flingy, "sprite", -1);
    if (NoneRow(ST_SPRITES, nSprite))
      return 0.0;
    const long long nImage = StudioNumber(ST_SPRITES, StudioRefName(ST_SPRITES, nSprite), "image", -1);
    if (NoneRow(ST_IMAGES, nImage))
      return 0.0;
    return StudioScriptWalkSpeed(StudioRefName(ST_IMAGES, nImage)) * 24.0;
  }
  return StudioNumber(ST_FLINGY, flingy, "top_speed") / 256.0 * 24.0;
}
