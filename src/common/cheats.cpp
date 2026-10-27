// Cheats the game has no codes for, switched in the settings panel's CHEATS tab and kept in the settings file. Battle
// units live in an array the pointer at 0x29EFB0 holds, 0x2B0 bytes each, the player's officer first: life as its
// maximum (+0x224) and the current value in sixteenths (+0x226, +0x228 the bar's), the musou gauge the same way
// (+0x22A maximum, +0x22C current in sixteenths). sub_001096A0 adds to a unit's life, a hit as a negative amount.
// +0x210 is the musou rage's frames left, -1 when off, +0x212 what it started from. +0x23C and +0x240 are the two
// speeds the Speed Scroll raises (12 and 6 to start with, held under 24 and 12.8 by sub_0015FF70 and sub_0015FFA0).
// The battle's clock counts frames up at 0x391934 and the time is up once it reaches 0x391938.
//
// The progress the save holds is 0x9FB0 bytes at 0x3EEF60 (sub_00101620 copies it into the file's buffer, sub_00101930
// makes a new one, +0x4 its version, 2). Officers from +0x150, 0x50 bytes each, 48 of them: +0x0 playable, +0x12 the
// weapon held, +0x2 life, +0x4 musou, +0x6 attack, +0x7 defense, +0x8 experience (the level from it, and the costumes
// and item slots from the level), four weapon slots of 0xE bytes from +0x14 (the id, 4 per officer in order, 0xDC an
// empty slot; a byte 1; five attribute pairs, 0x20 none). Items from +0x1050, a byte each for the 32: 0xFF not found,
// else its level, which the first ten (the stat items) raise to 0x13 and the rest keep at 0 (sub_00150FA0 caps it so).
// Stages from +0x1070, 0xC bytes each, 63 of them: +0x0 open in Free Mode. Bodyguards from +0x1364, 0x2C bytes each,
// 8 of them: +0x0 hired, +0x1C experience, then life and seven abilities. The maxima are the ones the game holds
// these to when it writes them (sub_00152D10 the officers', sub_00103680 the bodyguards').
// Two bitsets the unlocks set through tables: the battles' music at +0x108 (44 bits, 0x25A388) and 71 bits at +0x110
// (0x25A3B8, the endings' movies among them).
#include <jni.h>

#include <cstdint>

#include "app/app.h"
#include "common/options.h"
#include "core/input.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace game {

namespace {

constexpr uint32_t kUnits = 0x29EFB0;  // the battle's unit array, the player's officer at index 0
constexpr uint32_t kUnitSize = 0x2B0;
constexpr uint32_t kLifeMax = 0x224, kLife = 0x226, kLifeShown = 0x228;
constexpr uint32_t kMusouMax = 0x22A, kMusou = 0x22C;
constexpr uint32_t kRage = 0x210, kRageStart = 0x212;
constexpr uint32_t kSpeed = 0x23C, kSpeedB = 0x240;
constexpr float kSpeedMax = 24.0f, kSpeedBMax = 12.8f;
constexpr uint32_t kClock = 0x391934, kTimeLimit = 0x391938;

constexpr uint32_t kProgress = 0x3EEF60, kProgressVersion = 2;
constexpr uint32_t kOfficers = kProgress + 0x150, kOfficerSize = 0x50, kOfficerCount = 48;
constexpr uint32_t kOfficerLife = 0x2, kOfficerMusou = 0x4, kOfficerAttack = 0x6, kOfficerDefense = 0x7;
constexpr uint32_t kOfficerExp = 0x8;
constexpr uint32_t kWeapons = 0x14, kWeaponSize = 0xE, kNoWeapon = 0xDC;
constexpr uint32_t kItems = kProgress + 0x1050, kItemCount = 32, kLevelledItems = 10, kItemLevelMax = 0x13;
constexpr uint32_t kStages = kProgress + 0x1070, kStageSize = 0xC, kStageCount = 63;
constexpr uint32_t kGuards = kProgress + 0x1364, kGuardSize = 0x2C, kGuardCount = 8;
constexpr uint32_t kGuardExp = 0x1C, kGuardLife = 0x1E, kGuardAbilities = 0x20;
constexpr uint8_t kGuardAbilityMax[] = {200, 200, 200, 60, 200, 100, 160};
constexpr uint32_t kFlagsA = kProgress + 0x108, kFlagsACount = 44;
constexpr uint32_t kFlagsB = kProgress + 0x110, kFlagsBCount = 71;

enum Cheat : int {
    kInfiniteHealth, kInfiniteMusou, kUnlockAll, kAllWeapons, kMaxOfficers, kMaxBodyguards, kInfiniteRage, kFreezeTimer,
    kMaxSpeed, kCheatCount
};
static_assert(kCheatCount == sizeof(Options::cheats) / sizeof(bool));

bool On(Cheat c) { return Opt().cheats[c]; }

// the player's officer, 0 outside a battle (no array, or an empty slot)
uint32_t Player() {
    uint32_t units = kernel::R32(kUnits);
    if (!units || !kernel::R16(units + kLifeMax)) return 0;
    return units;
}

void SetBits(uint32_t va, uint32_t count) {
    for (uint32_t i = 0; i < count; i += 32) {
        uint32_t n = count - i < 32 ? count - i : 32;
        kernel::W32(va + i / 8, kernel::R32(va + i / 8) | (n == 32 ? 0xFFFFFFFFu : (1u << n) - 1));
    }
}

// written again each poll: loading a save puts its own copy back over the block
void Unlock() {
    if (kernel::R16(kProgress + 4) != kProgressVersion) return;  // no progress made yet, or a block not the game's
    if (On(kUnlockAll)) {
        for (uint32_t i = 0; i < kOfficerCount; i++) kernel::G<uint8_t>(kOfficers + i * kOfficerSize) = 1;
        for (uint32_t i = 0; i < kStageCount; i++) kernel::G<uint8_t>(kStages + i * kStageSize) = 1;
        for (uint32_t i = 0; i < kItemCount; i++) {
            uint8_t& level = kernel::G<uint8_t>(kItems + i);
            uint8_t max = i < kLevelledItems ? kItemLevelMax : 0;
            if (level == 0xFF || level < max) level = max;
        }
        SetBits(kFlagsA, kFlagsACount);
        SetBits(kFlagsB, kFlagsBCount);
    }
    if (On(kAllWeapons)) {
        // an empty slot gets the weapon the game would put there, at the level a new one has; a weapon already won
        // keeps its attributes
        for (uint32_t i = 0; i < kOfficerCount; i++)
            for (uint32_t k = 0; k < 4; k++) {
                uint32_t slot = kOfficers + i * kOfficerSize + kWeapons + k * kWeaponSize;
                if (kernel::R16(slot) != kNoWeapon) continue;
                kernel::W16(slot + 2, 1);
                for (uint32_t a = 0; a < 5; a++) kernel::W16(slot + 4 + a * 2, 0x20);
                kernel::W16(slot, uint16_t(i * 4 + k));
            }
    }
    if (On(kMaxOfficers))
        for (uint32_t i = 0; i < kOfficerCount; i++) {
            uint32_t o = kOfficers + i * kOfficerSize;
            kernel::W16(o + kOfficerLife, 400);
            kernel::W16(o + kOfficerMusou, 200);
            kernel::G<uint8_t>(o + kOfficerAttack) = 200;
            kernel::G<uint8_t>(o + kOfficerDefense) = 200;
            kernel::W16(o + kOfficerExp, 60000);
        }
    if (On(kMaxBodyguards))
        for (uint32_t i = 0; i < kGuardCount; i++) {
            uint32_t g = kGuards + i * kGuardSize;
            if (!kernel::G<uint8_t>(g)) continue;
            kernel::W16(g + kGuardExp, 10000);
            kernel::W16(g + kGuardLife, 400);
            for (uint32_t a = 0; a < sizeof(kGuardAbilityMax); a++)
                kernel::G<uint8_t>(g + kGuardAbilities + a) = kGuardAbilityMax[a];
        }
}

// the time left when the clock was stopped, kept by moving the limit along with the clock: the clock itself runs on,
// so what a stage sets off at a given time still happens
uint32_t g_time_left;
uint32_t g_clock_seen;
bool g_clock_held;

void HoldClock() {
    uint32_t clock = kernel::R32(kClock), limit = kernel::R32(kTimeLimit);
    if (!g_clock_held || clock < g_clock_seen) g_time_left = limit > clock ? limit - clock : 0;  // a new battle
    g_clock_held = true;
    g_clock_seen = clock;
    if (g_time_left) kernel::W32(kTimeLimit, clock + g_time_left);
}

}  // namespace

// GameInfo::pad, each poll on the game's thread: the gauges kept full, a musou attack never draining its own
void Cheats() {
    Unlock();
    uint32_t p = Player();
    if (!p || !On(kFreezeTimer)) g_clock_held = false;
    if (!p) return;
    if (On(kFreezeTimer)) HoldClock();
    if (On(kMaxSpeed)) {
        kernel::G<float>(p + kSpeed) = kSpeedMax;
        kernel::G<float>(p + kSpeedB) = kSpeedBMax;
    }
    if (On(kInfiniteRage) && int16_t(kernel::R16(p + kRage)) > 0) kernel::W16(p + kRage, kernel::R16(p + kRageStart));
    if (On(kInfiniteHealth)) {
        uint16_t full = uint16_t(kernel::R16(p + kLifeMax) << 4);
        kernel::W16(p + kLife, full);
        kernel::W16(p + kLifeShown, full);
    }
    if (On(kInfiniteMusou)) kernel::W16(p + kMusou, uint16_t(kernel::R16(p + kMusouMax) << 4));
}

}  // namespace game

// a hit on the player's officer takes nothing while life is on: between two polls a blow could empty it, and the
// game counts the officer dead the moment it reaches zero
X86_EXTERN_FUNC(__imp__sub_001096A0);
X86_FUNC(sub_001096A0) {
    if (game::On(game::kInfiniteHealth) && ctx.edx == 0 && int16_t(ctx.eax) < 0) ctx.eax &= 0xFFFF0000u;
    __imp__sub_001096A0(ctx, base);
}

extern "C" JNIEXPORT jbooleanArray JNICALL Java_com_datfooldive_dw5_DW5Activity_nativeCheats(JNIEnv* env, jclass) {
    jboolean v[game::kCheatCount];
    for (int i = 0; i < game::kCheatCount; i++) v[i] = game::Opt().cheats[i];
    jbooleanArray arr = env->NewBooleanArray(game::kCheatCount);
    if (arr) env->SetBooleanArrayRegion(arr, 0, game::kCheatCount, v);
    return arr;
}

extern "C" JNIEXPORT void JNICALL Java_com_datfooldive_dw5_DW5Activity_nativeCheatSet(JNIEnv*, jclass, jint i,
                                                                                     jboolean on) {
    if (i < 0 || i >= game::kCheatCount) return;
    game::Opt().cheats[i] = on;
    app::Save();
}
