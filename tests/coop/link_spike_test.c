#include <assert.h>
#include <stdio.h>
#include "global.h"
#include "coop/mailbox.h"
#include "coop/link_spike.h"
#include "constants/flags.h"

struct Main gMain;
struct SaveBlock save, *gSaveBlock1Ptr = &save;
struct LinkPlayer gLinkPlayers[5];
u8 gBlockSendBuffer[256], gWirelessCommType;
u16 gBlockRecvBuffer[5][128], gLinkType;
struct Pokemon gPlayerParty[6];
bool8 gLinkVSyncDisabled, gReceivedRemoteLinkPlayers;
void (*gFieldCallback)(void);
static bool8 locked, busy, accept, entered, returned;
static u8 partyCount;
static u8 fixtureCalls;
static u8 fixtureFlagCalls;
static struct CoopMessage incoming;
static u8 incomingBytes[257], outgoing[257];
static u16 outSize;
void CB1_Overworld(void) {}
void CB2_Overworld(void) {}
void SetMainCallback1(void (*callback)(void)) { gMain.callback1 = callback; }
bool8 ArePlayerFieldControlsLocked(void) { return locked; }
u8 CalculatePlayerPartyCount(void) { return partyCount; }
void CoopRemote_DespawnAll(void) {}
void ZeroPlayerPartyMons(void) { memset(gPlayerParty, 0, sizeof(gPlayerParty)); }
void CreateMon(struct Pokemon *mon, u16 species, u8 level, u8 iv, bool8 fixed, u32 personality, u8 ot, u32 id) {
    (void)mon; (void)species; (void)level; (void)iv; (void)fixed; (void)personality; (void)ot; (void)id;
    fixtureCalls++;
}
void LocalLinkPlayerToBlock(void) {
    struct LinkPlayerBlock local = {0};
    local.linkPlayer.trainerId = 1234;
    local.linkPlayer.linkType = LINKTYPE_TRADE;
    memcpy(gBlockSendBuffer, &local, sizeof(local));
}
void ConvertLinkPlayerName(struct LinkPlayer *p) { p->progressFlagsCopy = p->progressFlags; }
void SetDynamicWarpWithCoords(int a, s8 b, s8 c, s8 d, s8 e, s8 f) {
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
}
void ScriptContext_Init(void) {}
u8 FlagSet(u16 id) {
    assert(id == FLAG_SYS_POKEMON_GET);
    fixtureFlagCalls++;
    return 0;
}
void UnlockPlayerFieldControls(void) { locked = FALSE; }
void CB2_ReturnToField(void) { returned = TRUE; }
void CB2_StartCreateTradeMenu(void) { entered = TRUE; }
bool8 CoopMailbox_IsSending(void) { return busy; }
void CoopMailbox_PumpOutbound(void) {}
bool8 CoopMailbox_Push(u8 type, const void *data, u8 size) {
    (void)type; (void)data; (void)size; return TRUE;
}
bool8 CoopMailbox_SendMessage(u8 type, const void *data, u16 size) {
    assert(type == COOP_MSG_LINK_SPIKE);
    if (!accept) return FALSE;
    outSize = size; memcpy(outgoing, data, size); return TRUE;
}
bool8 CoopMailbox_Receive(struct CoopMessage *msg) {
    if (!incoming.length) return FALSE;
    *msg = incoming; incoming.length = 0; return TRUE;
}

// Include the real adapter so tests can reset its private experiment state.
#include "../../src/coop/link_spike.c"

static void Reset(void) {
    memset(&sSpike, 0, sizeof(sSpike));
    memset(gBlockRecvBuffer, 0, sizeof(gBlockRecvBuffer));
    gMain.callback1 = CB1_Overworld; gMain.callback2 = CB2_Overworld;
    busy = locked = entered = returned = FALSE; accept = TRUE; partyCount = 2;
    incoming.length = outSize = 0;
    fixtureCalls = 0;
    fixtureFlagCalls = 0;
}
static void Input(u8 type, const void *data, u16 size) {
    incomingBytes[0] = type;
    if (size) memcpy(incomingBytes + 1, data, size);
    incoming.type = COOP_MSG_LINK_SPIKE;
    incoming.data = incomingBytes; incoming.length = size + 1;
    CoopLinkSpike_Update();
}
static void Start(u8 role) {
    u8 bytes[] = {0, role, 0};
    struct CoopMessage msg = {COOP_MSG_LINK_SPIKE, sizeof(bytes), bytes};
    CoopLinkSpike_Start(&msg);
}
static void TradeCallback(void) {}
static void CheckFieldReturn(void (*previousCallback)(void)) {
    Reset(); Start(0);
    // Animation clears callback1; cancel leaves the trade's link callback.
    gMain.callback1 = previousCallback;
    gMain.callback2 = TradeCallback;
    gFieldCallback = TradeCallback;
    locked = TRUE;
    gReceivedRemoteLinkPlayers = TRUE;
    CoopLinkSpike_ReturnToField();
    assert(returned && !sSpike.active && !gReceivedRemoteLinkPlayers);
    assert(gMain.callback1 == CB1_Overworld && !locked && gFieldCallback == NULL);
    CoopLinkSpike_Update(); assert(sSpike.returning);
    // Drawing the field alone is insufficient: wait for its fade to unlock input.
    gMain.callback2 = CB2_Overworld;
    locked = TRUE;
    CoopLinkSpike_Update(); assert(sSpike.returning);
    locked = FALSE;
    CoopLinkSpike_Update(); assert(!sSpike.returning);
}
int main(void) {
    u8 bytes[256], generation = 1;
    struct LinkPlayer peer = {0};
    memset(bytes, 0xAB, sizeof(bytes));
    Reset(); locked = TRUE; Start(0); assert(!sSpike.active);
    locked = FALSE; partyCount = 0; Start(0); assert(!sSpike.active);
    partyCount = 2; Start(2); assert(!sSpike.active);
    Start(0); assert(sSpike.active && !entered && fixtureCalls == 0 && fixtureFlagCalls == 0);
    accept = FALSE; CoopLinkSpike_Update(); assert(sSpike.count == 1);
    accept = TRUE; CoopLinkSpike_Update();
    assert(outSize == sizeof(peer) + 1 && outgoing[0] == HELLO);
    peer.linkType = LINKTYPE_TRADE;
    Input(HELLO, &peer, sizeof(peer));
    assert(entered && gReceivedRemoteLinkPlayers && gLinkPlayers[0].trainerId == 1234);
    assert(!CoopLinkSpike_Send(bytes, 257, FALSE));
    assert(CoopLinkSpike_Send(bytes, 256, FALSE));
    assert(sSpike.received == 1 && !memcmp(gBlockRecvBuffer[0], bytes, 256));
    busy = TRUE; CoopLinkSpike_Update(); assert(sSpike.count == 1);
    busy = FALSE; CoopLinkSpike_Update(); assert(outSize == 257 && outgoing[0] == BLOCK);
    CoopLinkSpike_ResetReceived(3); assert(sSpike.received == 0);
    Input(BLOCK, bytes, 220); assert(sSpike.received == 2);
    assert(!memcmp(gBlockRecvBuffer[1], bytes, 220));
    CoopLinkSpike_Barrier(FALSE); CoopLinkSpike_Update();
    assert(!CoopLinkSpike_Finished());
    Input(BARRIER, &generation, 1); assert(CoopLinkSpike_Finished());
    CoopLinkSpike_Barrier(TRUE); CoopLinkSpike_Update();
    assert(gReceivedRemoteLinkPlayers);
    Input(CLOSE, &generation, 1); assert(!gReceivedRemoteLinkPlayers);
    CoopLinkSpike_Open(); assert(gReceivedRemoteLinkPlayers && sSpike.close == 0);
    CheckFieldReturn(NULL);
    CheckFieldReturn(TradeCallback);
    Reset(); Start(1); CoopLinkSpike_Update();
    memset(gBlockSendBuffer, 0xCD, sizeof(gBlockSendBuffer));
    Input(REQUEST, bytes, 200);
    assert(sSpike.received == 3 && outgoing[0] == BLOCK && outSize == 201);
    assert(((u8 *)gBlockRecvBuffer[1])[0] == 0xCD && ((u8 *)gBlockRecvBuffer[0])[0] == 0xAB);
    Input(BLOCK, NULL, 0); assert(sSpike.received == 3);
    for (int i = 0; i < QUEUE_SIZE; i++) assert(CoopLinkSpike_Send(bytes, 8, FALSE));
    assert(!CoopLinkSpike_Send(bytes, 8, FALSE) && sSpike.failed);
    Reset();
    {
        u8 start[] = {0, 0, 2};
        struct CoopMessage msg = {COOP_MSG_LINK_SPIKE, sizeof(start), start};
        partyCount = 0; // Fixtures also work before the original save has a starter.
        CoopLinkSpike_Start(&msg); assert(!sSpike.active && fixtureCalls == 0 && fixtureFlagCalls == 0);
        start[2] = 1;
        CoopLinkSpike_Start(&msg); assert(sSpike.active && fixtureCalls == 2 && fixtureFlagCalls == 1);
    }
    puts("PASS coop link spike: guarded entry, identity, backpressure, self echo, requests, barriers, close/reopen, return and bounds");
    return 0;
}
