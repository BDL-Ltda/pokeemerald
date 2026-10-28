#ifndef COOP_SPIKE_TEST_GLOBAL_H
#define COOP_SPIKE_TEST_GLOBAL_H
// Test doubles for the experimental adapter, not a replacement GBA ABI.
#include <stdint.h>
#include <string.h>
#include <stddef.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef u8 bool8;
#define TRUE 1
#define FALSE 0
#define EWRAM_DATA
#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define GUARD_LINK_H
#define GUARD_MAIN_H
#define GUARD_OVERWORLD_H
#define GUARD_POKEMON_H
#define GUARD_SCRIPT_H
#define GUARD_TRADE_H
#define GUARD_EVENT_DATA_H
#define GUARD_COOP_REMOTE_PLAYERS_H
#define BLOCK_BUFFER_SIZE 256
#define LINKTYPE_TRADE 0x1111
#define USE_RANDOM_IVS 32
#define OT_ID_PLAYER_ID 0
struct Pokemon { u8 bytes[100]; };
extern struct Pokemon gPlayerParty[6];
void ZeroPlayerPartyMons(void);
void CreateMon(struct Pokemon *mon, u16 species, u8 level, u8 iv, bool8 fixed, u32 personality, u8 ot, u32 id);
struct LinkPlayer {
    u16 version, flags; u32 trainerId; u8 name[8];
    u8 progressFlags, unused, progressFlagsCopy, gender;
    u32 linkType; u16 id, language;
};
struct LinkPlayerBlock { char magic1[16]; struct LinkPlayer linkPlayer; char magic2[16]; };
struct Main { void (*callback1)(void); void (*callback2)(void); void (*savedCallback)(void); };
struct SaveBlock { struct { s8 mapGroup, mapNum; } location; struct { u16 x, y; } pos; };
extern struct Main gMain;
extern struct SaveBlock *gSaveBlock1Ptr;
extern struct LinkPlayer gLinkPlayers[5];
extern u8 gBlockSendBuffer[256];
extern u16 gBlockRecvBuffer[5][128];
extern u8 gWirelessCommType;
extern bool8 gLinkVSyncDisabled, gReceivedRemoteLinkPlayers;
extern u16 gLinkType;
extern void (*gFieldCallback)(void);
void CB1_Overworld(void);
void CB2_Overworld(void);
void SetMainCallback1(void (*callback)(void));
bool8 ArePlayerFieldControlsLocked(void);
u8 CalculatePlayerPartyCount(void);
void CoopRemote_DespawnAll(void);
void LocalLinkPlayerToBlock(void);
void ConvertLinkPlayerName(struct LinkPlayer *player);
void SetDynamicWarpWithCoords(int unused, s8 group, s8 number, s8 warp, s8 x, s8 y);
void ScriptContext_Init(void);
u8 FlagSet(u16 id);
void UnlockPlayerFieldControls(void);
void CB2_ReturnToField(void);
void CB2_StartCreateTradeMenu(void);
#endif
