#include <assert.h>
#include <stdio.h>
#include "global.h"
#include "coop/mailbox.h"
#include "coop/virtual_link.h"
#include "constants/characters.h"

// Host test for the virtual link (issue #81). It compiles the real
// src/coop/virtual_link.c with doubles for the game and the mailbox, and
// plays the partner's ROM through COOP_MSG_LINK packets.

struct Main gMain;
static struct SaveBlock1 save1;
static struct SaveBlock2 save2;
struct SaveBlock1 *gSaveBlock1Ptr = &save1;
struct SaveBlock2 *gSaveBlock2Ptr = &save2;
struct Pokemon gPlayerParty[PARTY_SIZE];
u8 gPlayerPartyCount;
struct LinkPlayer gLinkPlayers[5];
u8 gBlockSendBuffer[256], gWirelessCommType;
u16 gBlockRecvBuffer[5][128], gLinkType;
bool8 gLinkVSyncDisabled, gReceivedRemoteLinkPlayers, gSoftResetDisabled;
void (*gFieldCallback)(void);
struct PaletteFadeControl gPaletteFade;
u8 gStringVar1[64];

static bool8 locked, luaConnected, sending, accept, leaveOk, entered, returned, hidden;
static bool8 boxHidden, musicRestarted;
static u8 partyCount, leaveCalls, applied, tasks;
static void (*task)(u8);
static const u8 *shownText;

// Packets the ROM sent to Lua, oldest first.
#define MAX_SENT 256
static u8 sent[MAX_SENT][257];
static u16 sentSize[MAX_SENT];
static u16 sentCount;

// Messages Lua has for the ROM.
#define MAX_INBOX 8
static struct { u8 type; u16 length; u8 bytes[300]; } inbox[MAX_INBOX];
static u8 inboxHead, inboxCount;

void CB1_Overworld(void) {}
void CB2_Overworld(void) {}
static void TradeScreen(void) {}
static void LeavingScreen(void) {}
void SetMainCallback1(void (*callback)(void)) { gMain.callback1 = callback; }
bool8 ArePlayerFieldControlsLocked(void) { return locked; }
void LockPlayerFieldControls(void) { locked = TRUE; }
void UnlockPlayerFieldControls(void) { locked = FALSE; }
u8 CalculatePlayerPartyCount(void) { return partyCount; }
void CoopRemote_HideAll(void) { hidden = TRUE; }
void CoopRemote_ApplyMessage(const struct CoopMessage *msg) { (void)msg; applied++; }
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
void CB2_ReturnToField(void) { returned = TRUE; gMain.callback2 = CB2_Overworld; }
void CB2_StartCreateTradeMenu(void) { entered = TRUE; gMain.callback2 = TradeScreen; gMain.callback1 = NULL; }
bool8 TryLeaveCoopLinkTrade(void) {
    leaveCalls++;
    if (!leaveOk || gMain.callback2 != TradeScreen) return FALSE;
    gMain.callback2 = LeavingScreen;
    return TRUE;
}
void StopMapMusic(void) {}
void Overworld_PlaySpecialMapMusic(void) { musicRestarted = TRUE; }
u8 *StringCopy(u8 *dest, const u8 *src) {
    while (*src != EOS) *dest++ = *src++;
    *dest = EOS;
    return dest;
}
bool8 ShowFieldMessage(const u8 *str) { shownText = str; boxHidden = FALSE; return TRUE; }
bool8 IsFieldMessageBoxHidden(void) { return boxHidden; }
void HideFieldMessageBox(void) { boxHidden = TRUE; shownText = NULL; }
u8 CreateTask(void (*func)(u8), u8 priority) { (void)priority; task = func; tasks++; return 0; }
void DestroyTask(u8 taskId) { (void)taskId; task = NULL; }

bool8 CoopMailbox_IsLuaConnected(void) { return luaConnected; }
bool8 CoopMailbox_IsSending(void) { return sending; }
void CoopMailbox_PumpOutbound(void) {}
bool8 CoopMailbox_SendMessage(u8 type, const void *data, u16 size) {
    assert(type == COOP_MSG_LINK && size >= 1 && size <= 257);
    if (!accept || !luaConnected || sentCount == MAX_SENT) return FALSE;
    memcpy(sent[sentCount], data, size);
    sentSize[sentCount++] = size;
    return TRUE;
}
bool8 CoopMailbox_Receive(struct CoopMessage *msg) {
    if (!inboxCount) return FALSE;
    msg->type = inbox[inboxHead].type;
    msg->length = inbox[inboxHead].length;
    msg->data = inbox[inboxHead].bytes;
    inboxHead = (inboxHead + 1) % MAX_INBOX;
    inboxCount--;
    return TRUE;
}
void CoopMailbox_DiscardInbound(void) { inboxHead = inboxCount = 0; }

#include "../../src/coop/virtual_link.c"

static void Reset(void) {
    memset(&sLink, 0, sizeof(sLink));
    memset(gBlockRecvBuffer, 0, sizeof(gBlockRecvBuffer));
    memset(gPlayerParty, 0, sizeof(gPlayerParty));
    gPlayerParty[0].bytes[0] = 0x11;
    gPlayerPartyCount = partyCount = 2;
    gMain.callback1 = CB1_Overworld; gMain.callback2 = CB2_Overworld;
    gMain.newKeys = 0; gPaletteFade.active = FALSE;
    gWirelessCommType = 1; gLinkVSyncDisabled = FALSE; gReceivedRemoteLinkPlayers = FALSE;
    locked = sending = entered = returned = hidden = musicRestarted = FALSE;
    luaConnected = accept = leaveOk = boxHidden = TRUE;
    leaveCalls = applied = tasks = 0; task = NULL; shownText = NULL;
    sentCount = 0; inboxHead = inboxCount = 0;
}

static void Push(u8 type, const void *data, u16 size) {
    u8 slot = (inboxHead + inboxCount) % MAX_INBOX;
    assert(inboxCount < MAX_INBOX && size <= sizeof(inbox[0].bytes));
    inbox[slot].type = type;
    inbox[slot].length = size;
    memcpy(inbox[slot].bytes, data, size);
    inboxCount++;
}

// A packet from the partner (or Lua) during a session: delivered by CoopLink_Update.
static void Peer(u8 kind, const void *data, u16 size) {
    u8 bytes[300];
    bytes[0] = kind;
    if (size) memcpy(bytes + 1, data, size);
    Push(COOP_MSG_LINK, bytes, size + 1);
    CoopLink_Update();
}

static void Frames(unsigned n) { while (n--) CoopLink_Update(); }

static const u8 *Last(void) { assert(sentCount > 0); return sent[sentCount - 1]; }

static bool8 SentKind(u8 kind) {
    u16 i;
    for (i = 0; i < sentCount; i++) if (sent[i][0] == kind) return TRUE;
    return FALSE;
}

// The START message Lua would send, outside a session (CoopRemote_Update).
static void Start(u8 role, const char *id) {
    u8 bytes[32];
    struct CoopMessage msg;
    u8 length = (u8)strlen(id);
    bytes[0] = COOP_LINK_START; bytes[1] = role; bytes[2] = length;
    memcpy(bytes + 3, id, length);
    msg.type = COOP_MSG_LINK; msg.length = 3 + length; msg.data = bytes;
    CoopLink_HandleIdleMessage(&msg);
}

static void PeerHello(u8 ack) {
    u8 bytes[1 + sizeof(struct LinkPlayer)] = {0};
    struct LinkPlayer peer = {0};
    peer.trainerId = 5678;
    peer.linkType = LINKTYPE_TRADE;
    bytes[0] = ack;
    memcpy(bytes + 1, &peer, sizeof(peer));
    Peer(COOP_LINK_HELLO, bytes, sizeof(bytes));
}

// Starts a session as `role` and completes the HELLO handshake.
static void Connect(u8 role) {
    Reset();
    Start(role, "davi_2");
    assert(CoopLink_IsActive() && sLink.state == LINK_CONNECTING);
    CoopLink_Update();
    PeerHello(0);
    PeerHello(1);
    CoopLink_Update();
    assert(entered && sLink.state == LINK_TRADING && gReceivedRemoteLinkPlayers);
}

static void TestGuardedStart(void) {
    Reset(); locked = TRUE; Start(0, "davi"); assert(!CoopLink_IsActive());
    CoopLink_Update(); assert(sentCount == 1 && Last()[0] == COOP_LINK_END && Last()[1] == COOP_LINK_END_REFUSED);
    Reset(); partyCount = 0; Start(0, "davi"); assert(!CoopLink_IsActive());
    Reset(); Start(2, "davi"); assert(!CoopLink_IsActive());
    Reset(); Start(0, "da vi"); assert(!CoopLink_IsActive());
    Reset(); Start(0, "abcdefghijklmnopq"); assert(!CoopLink_IsActive());
    Reset(); gMain.callback2 = TradeScreen; Start(0, "davi"); assert(!CoopLink_IsActive());
    Reset(); sLink.notice = TRUE; Start(0, "davi"); assert(!CoopLink_IsActive());
    // Late packets of an ended session are dropped, not refused.
    Reset();
    {
        u8 late[] = {COOP_LINK_BLOCK, 1, 2};
        struct CoopMessage msg = {COOP_MSG_LINK, sizeof(late), late};
        CoopLink_HandleIdleMessage(&msg);
        CoopLink_Update(); assert(sentCount == 0 && !CoopLink_IsActive());
    }
}

static void TestHandshake(void) {
    Reset();
    Start(1, "Leo-1");
    assert(CoopLink_IsActive() && locked && hidden && gWirelessCommType == 0 && gLinkVSyncDisabled);
    assert(gLinkPlayers[1].trainerId == 1234 && gLinkPlayers[1].id == 1);
    accept = FALSE; CoopLink_Update(); assert(sentCount == 0);
    accept = TRUE; CoopLink_Update();
    assert(sentCount == 1 && Last()[0] == COOP_LINK_HELLO && Last()[1] == 0 && sentSize[0] == 2 + sizeof(struct LinkPlayer));
    // Retransmitted until the partner answers (its Lua may not be relaying yet).
    Frames(COOP_LINK_KEEPALIVE_FRAMES + 1);
    assert(sentCount == 2 && Last()[0] == COOP_LINK_HELLO);
    PeerHello(0);
    assert(gLinkPlayers[0].trainerId == 5678 && gLinkPlayers[0].id == 0 && !entered);
    CoopLink_Update(); assert(Last()[0] == COOP_LINK_HELLO && Last()[1] == 1 && !entered);
    PeerHello(1); CoopLink_Update();
    assert(entered && gReceivedRemoteLinkPlayers && gMain.savedCallback == CoopLink_ReturnToField);
    // HELLO after entering changes nothing.
    PeerHello(1); assert(sLink.state == LINK_TRADING);
}

static void TestBlocksAndBarriers(void) {
    u8 bytes[256], generation;
    memset(bytes, 0xAB, sizeof(bytes));
    Connect(0);
    assert(!CoopLink_SendBlock(bytes, 257, FALSE));
    assert(CoopLink_SendBlock(bytes, 256, FALSE));
    assert(CoopLink_GetBlockReceivedStatus() == 1 && !memcmp(gBlockRecvBuffer[0], bytes, 256));
    assert(!CoopLink_IsTaskFinished());
    sending = TRUE; CoopLink_Update(); assert(Last()[0] != COOP_LINK_BLOCK);
    sending = FALSE; CoopLink_Update(); assert(Last()[0] == COOP_LINK_BLOCK && sentSize[sentCount - 1] == 257);
    assert(CoopLink_IsTaskFinished());
    CoopLink_ResetBlockReceivedFlags(3); assert(CoopLink_GetBlockReceivedStatus() == 0);
    Peer(COOP_LINK_BLOCK, bytes, 220);
    assert(CoopLink_GetBlockReceivedStatus() == 2 && !memcmp(gBlockRecvBuffer[1], bytes, 220));
    Peer(COOP_LINK_BLOCK, NULL, 0); // Empty blocks are ignored.

    CoopLink_SetBarrier(FALSE); CoopLink_Update();
    assert(Last()[0] == COOP_LINK_BARRIER && !CoopLink_IsTaskFinished());
    generation = 1; Peer(COOP_LINK_BARRIER, &generation, 1); assert(CoopLink_IsTaskFinished());
    // A partner already waiting at the next barrier has passed this one.
    CoopLink_SetBarrier(FALSE); CoopLink_Update();
    generation = 3; Peer(COOP_LINK_BARRIER, &generation, 1); assert(CoopLink_IsTaskFinished());

    CoopLink_SetBarrier(TRUE); CoopLink_Update(); assert(gReceivedRemoteLinkPlayers);
    generation = 1; Peer(COOP_LINK_CLOSE, &generation, 1); assert(!gReceivedRemoteLinkPlayers);
    CoopLink_Open(); assert(gReceivedRemoteLinkPlayers && sLink.close == 0);

    // The follower answers a request with its prepared buffer.
    Connect(1);
    memset(gBlockSendBuffer, 0xCD, sizeof(gBlockSendBuffer));
    Peer(COOP_LINK_REQUEST, bytes, 200);
    CoopLink_Update();
    assert(CoopLink_GetBlockReceivedStatus() == 3 && Last()[0] == COOP_LINK_BLOCK && sentSize[sentCount - 1] == 201);
    assert(((u8 *)gBlockRecvBuffer[0])[0] == 0xAB && ((u8 *)gBlockRecvBuffer[1])[0] == 0xCD);
}

static void TestKeepaliveAndTimeout(void) {
    unsigned i;
    Connect(0);
    sentCount = 0;
    Frames(COOP_LINK_KEEPALIVE_FRAMES);
    assert(sentCount == 1 && Last()[0] == COOP_LINK_KEEPALIVE && sentSize[0] == 1);
    Peer(COOP_LINK_KEEPALIVE, NULL, 0);
    // Any partner packet restarts the silence count.
    for (i = 0; i < 3; i++)
    {
        Frames(COOP_LINK_TIMEOUT_FRAMES - 10);
        Peer(COOP_LINK_KEEPALIVE, NULL, 0);
    }
    assert(sLink.state == LINK_TRADING);
    Frames(COOP_LINK_TIMEOUT_FRAMES - 1);
    assert(sLink.state == LINK_TRADING);
    leaveOk = FALSE;
    Frames(2);
    assert(sLink.state == LINK_CANCELING && SentKind(COOP_LINK_ABORT) && leaveCalls > 0);
}

static void TestCancelRestoresAndNotifies(void) {
    u8 calls;
    Connect(0);
    // trade.c swaps the party before a cancel.
    gPlayerParty[0].bytes[0] = 0x99;
    gPlayerPartyCount = 1;
    gSaveBlock1Ptr->giftRibbons[0] = 7;
    gSaveBlock2Ptr->pokedex.owned[3] = 1;
    leaveOk = FALSE;
    {
        u8 reason = COOP_LINK_CANCEL_LEFT;
        Peer(COOP_LINK_CANCEL, &reason, 1);
    }
    assert(sLink.state == LINK_CANCELING && SentKind(COOP_LINK_ABORT));
    // Waits for a screen trade.c can leave (summary, evolution...).
    calls = leaveCalls;
    Frames(3); assert(leaveCalls == calls + 3 && gMain.callback2 == TradeScreen);
    // Partner silence while canceling doesn't re-cancel.
    Frames(COOP_LINK_TIMEOUT_FRAMES + 1); assert(sLink.state == LINK_CANCELING);
    leaveOk = TRUE; CoopLink_Update(); assert(gMain.callback2 == LeavingScreen);
    // trade.c's leave callback ends here.
    gSoftResetDisabled = TRUE;
    CoopLink_ReturnToField();
    assert(returned && !CoopLink_IsActive() && !gReceivedRemoteLinkPlayers && !gSoftResetDisabled);
    assert(gPlayerParty[0].bytes[0] == 0x11 && gPlayerPartyCount == 2);
    assert(gSaveBlock1Ptr->giftRibbons[0] == 0 && gSaveBlock2Ptr->pokedex.owned[3] == 0);
    assert(gMain.callback1 == CB1_Overworld && !locked && musicRestarted);
    assert(gWirelessCommType == 1 && !gLinkVSyncDisabled);
    CoopLink_Update();
    assert(Last()[0] == COOP_LINK_END && Last()[1] == COOP_LINK_END_CANCELED);
    assert(sLink.state == LINK_IDLE && shownText != NULL);
    // "Trade canceled: X disconnected", with the partner ID in the game's charset.
    assert(gStringVar1[0] == CHAR_d && gStringVar1[3] == CHAR_i);
    assert(gStringVar1[4] == CHAR_EXTRA_SYMBOL && gStringVar1[5] == CHAR_UNDERSCORE);
    assert(gStringVar1[6] == CHAR_2 && gStringVar1[7] == EOS);
    assert(locked && task != NULL && tasks == 1);
    CoopLink_Update(); assert(tasks == 1); // Shown once.
    task(0); assert(locked);
    boxHidden = TRUE; // The text finished printing.
    gMain.newKeys = A_BUTTON; task(0); assert(!locked && task == NULL && shownText == NULL);
}

static void TestPeerAbortAndLuaLoss(void) {
    Connect(1);
    sentCount = 0;
    Peer(COOP_LINK_ABORT, NULL, 0);
    assert(gMain.callback2 == LeavingScreen && !SentKind(COOP_LINK_ABORT));

    Connect(0);
    sentCount = 0;
    Push(COOP_MSG_PRESENCE, "\x01", 1);
    luaConnected = FALSE;
    CoopLink_Update();
    assert(gMain.callback2 == LeavingScreen && inboxCount == 0 && applied == 0);

    // Presence still reaches the remote-player state during a session.
    Connect(0);
    Push(COOP_MSG_PRESENCE, "\x01", 1);
    Push(COOP_MSG_LEAVE, "\x00", 1);
    CoopLink_Update();
    assert(applied == 2);
}

static void TestCancelWhileConnecting(void) {
    Reset();
    Start(0, "davi");
    CoopLink_Update();
    Frames(COOP_LINK_TIMEOUT_FRAMES - 1);
    assert(CoopLink_IsActive() && locked);
    CoopLink_Update(); // The partner never answered: 10 s of silence.
    assert(!CoopLink_IsActive() && !locked && !entered && sLink.notice);
    assert(SentKind(COOP_LINK_ABORT));
    Frames(2);
    assert(Last()[0] == COOP_LINK_END && Last()[1] == COOP_LINK_END_CANCELED && sLink.state == LINK_IDLE);
    assert(shownText != NULL && locked);
}

static void TestNoCancelAfterSaveStart(void) {
    u8 generation;
    Connect(0);
    gPlayerParty[0].bytes[0] = 0x99; // The received Pokemon.
    CoopLink_OnSaveStart();
    CoopLink_SetBarrier(FALSE); CoopLink_Update();
    assert(!CoopLink_IsTaskFinished());
    Frames(COOP_LINK_TIMEOUT_FRAMES + 1);
    // The partner is gone, but the save goes on: barriers are released.
    assert(sLink.state == LINK_TRADING && sLink.peerGone && leaveCalls == 0);
    assert(CoopLink_IsTaskFinished());
    CoopLink_SetBarrier(FALSE); assert(CoopLink_IsTaskFinished());
    CoopLink_SetBarrier(TRUE); CoopLink_Update(); assert(!gReceivedRemoteLinkPlayers);
    generation = 9; Peer(COOP_LINK_BARRIER, &generation, 1); // Ignored now.
    CoopLink_ReturnToField();
    assert(gPlayerParty[0].bytes[0] == 0x99 && !sLink.notice);
    CoopLink_Update();
    assert(Last()[0] == COOP_LINK_END && Last()[1] == COOP_LINK_END_DONE);
}

static void TestQueueOverflowCancels(void) {
    u8 bytes[8] = {0}, i;
    Connect(0);
    accept = FALSE;
    leaveOk = FALSE;
    for (i = 0; i < QUEUE_SIZE; i++) assert(CoopLink_SendBlock(bytes, sizeof(bytes), FALSE));
    assert(!CoopLink_SendBlock(bytes, sizeof(bytes), FALSE));
    assert(sLink.state == LINK_CANCELING && sLink.count == 1 && sLink.queue[sLink.tail].bytes[0] == COOP_LINK_ABORT);
    // No more blocks while canceling.
    assert(!CoopLink_SendBlock(bytes, sizeof(bytes), FALSE));
}

int main(void) {
    TestGuardedStart();
    TestHandshake();
    TestBlocksAndBarriers();
    TestKeepaliveAndTimeout();
    TestCancelRestoresAndNotifies();
    TestPeerAbortAndLuaLoss();
    TestCancelWhileConnecting();
    TestNoCancelAfterSaveStart();
    TestQueueOverflowCancels();
    puts("PASS coop virtual link: guarded start, handshake, blocks, barriers, keepalive, timeout, cancel, restore, notice, save boundary, bounds");
    return 0;
}
