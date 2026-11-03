#ifndef COOP_LINK_TEST_GLOBAL_H
#define COOP_LINK_TEST_GLOBAL_H
// Game doubles for the virtual-link host test, not a replacement GBA ABI. The
// ARM build checks the production headers; the test compiles the real
// src/coop/virtual_link.c against these declarations.
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include "constants/global.h"
#include "constants/game_stat.h"
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef u8 bool8;
#define TRUE 1
#define FALSE 0
#define EWRAM_DATA
#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define _(x) x
#define GUARD_FIELD_MESSAGE_BOX_H
#define GUARD_LINK_H
#define GUARD_MAIN_H
#define GUARD_OVERWORLD_H
#define GUARD_PALETTE_H
#define GUARD_POKEMON_H
#define GUARD_SCRIPT_H
#define GUARD_SOUND_H
#define GUARD_STRING_UTIL_H
#define GUARD_TASK_H
#define GUARD_TRADE_H
#define BLOCK_BUFFER_SIZE 256
#define LINKTYPE_TRADE 0x1111
#define NUM_DEX_FLAG_BYTES 52
#define A_BUTTON 1
#define B_BUTTON 2
#define JOY_NEW(button) ((gMain.newKeys & (button)) != 0)

struct Pokemon { u8 bytes[100]; };
struct Mail { u8 bytes[36]; };
struct Pokedex { u8 order; u32 unownPersonality; u8 owned[NUM_DEX_FLAG_BYTES]; u8 seen[NUM_DEX_FLAG_BYTES]; };
struct LinkPlayer {
    u16 version, lp_field_2; u32 trainerId; u8 name[8];
    u8 progressFlags, neverRead, progressFlagsCopy, gender;
    u32 linkType; u16 id, language;
};
struct LinkPlayerBlock { char magic1[16]; struct LinkPlayer linkPlayer; char magic2[16]; };
struct Main {
    void (*callback1)(void); void (*callback2)(void); void (*savedCallback)(void);
    u16 newKeys; u8 state;
};
struct SaveBlock1 {
    struct { s8 mapGroup, mapNum; } location; struct { u16 x, y; } pos;
    u8 seen1[NUM_DEX_FLAG_BYTES]; u32 gameStats[NUM_GAME_STATS];
    struct Mail mail[MAIL_COUNT]; u8 giftRibbons[GIFT_RIBBONS_COUNT]; u8 seen2[NUM_DEX_FLAG_BYTES];
};
struct SaveBlock2 { struct Pokedex pokedex; };
struct PaletteFadeControl { bool8 active; };

extern struct Main gMain;
extern struct SaveBlock1 *gSaveBlock1Ptr;
extern struct SaveBlock2 *gSaveBlock2Ptr;
extern struct Pokemon gPlayerParty[PARTY_SIZE];
extern u8 gPlayerPartyCount;
extern struct LinkPlayer gLinkPlayers[5];
extern u8 gBlockSendBuffer[256];
extern u16 gBlockRecvBuffer[5][128];
extern u8 gWirelessCommType;
extern bool8 gLinkVSyncDisabled, gReceivedRemoteLinkPlayers, gSoftResetDisabled;
extern u16 gLinkType;
extern void (*gFieldCallback)(void);
extern struct PaletteFadeControl gPaletteFade;
extern u8 gStringVar1[];

void CB1_Overworld(void);
void CB2_Overworld(void);
void SetMainCallback1(void (*callback)(void));
bool8 ArePlayerFieldControlsLocked(void);
void LockPlayerFieldControls(void);
void UnlockPlayerFieldControls(void);
u8 CalculatePlayerPartyCount(void);
void LocalLinkPlayerToBlock(void);
void ConvertLinkPlayerName(struct LinkPlayer *player);
void SetDynamicWarpWithCoords(int unused, s8 group, s8 number, s8 warp, s8 x, s8 y);
void ScriptContext_Init(void);
void CB2_ReturnToField(void);
void CB2_StartCreateTradeMenu(void);
bool8 TryLeaveCoopLinkTrade(void);
void StopMapMusic(void);
void Overworld_PlaySpecialMapMusic(void);
u8 *StringCopy(u8 *dest, const u8 *src);
bool8 ShowFieldMessage(const u8 *str);
bool8 IsFieldMessageBoxHidden(void);
void HideFieldMessageBox(void);
u8 CreateTask(void (*func)(u8), u8 priority);
void DestroyTask(u8 taskId);
#endif
