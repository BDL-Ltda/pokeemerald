#include "global.h"
#include "coop/mailbox.h"
#include "coop/remote_players.h"
#include "coop/virtual_link.h"
#include "field_message_box.h"
#include "link.h"
#include "main.h"
#include "overworld.h"
#include "palette.h"
#include "pokemon.h"
#include "script.h"
#include "sound.h"
#include "string_util.h"
#include "task.h"
#include "trade.h"
#include "constants/characters.h"
#include "constants/coop.h"

// The virtual link (docs/virtual-link.md). Each side's ROM talks to the other
// side's ROM; Lua only relays COOP_MSG_LINK packets to and from the partner.
// The vanilla trade screens keep their own logic: this file replaces only the
// cable transport behind link.c's block, standby and close functions.

// Outgoing packets wait here until the mailbox has room for them. trade.c
// waits for this queue to drain (IsLinkTaskFinished) before most steps, so a
// full queue means the link is broken: the session is canceled.
#define QUEUE_SIZE 4

// START: kind, role, partner ID length, then the ID.
#define START_ROLE      1
#define START_ID_LENGTH 2
#define START_ID        3

// HELLO payload, after the kind byte: ack flag, then the sender's LinkPlayer.
#define HELLO_ACK         0
#define HELLO_LINK_PLAYER 1
#define HELLO_SIZE        (1 + sizeof(struct LinkPlayer))

// A partner ID in the game's charset: '_' takes two bytes.
#define PARTNER_NAME_SIZE (COOP_LINK_PARTNER_ID_MAX * 2 + 1)

enum
{
    LINK_IDLE,
    LINK_CONNECTING, // START accepted: exchanging HELLO in the locked overworld.
    LINK_TRADING,    // trade.c runs over the virtual link.
    LINK_CANCELING,  // Partner gone: leave trade.c at its next safe point.
    LINK_CLOSING,    // Back to the field: still sending ABORT/END to Lua.
};

struct Packet
{
    u16 size;
    u8 bytes[1 + BLOCK_BUFFER_SIZE];
};

// Everything trade.c can change before the save. A cancel puts it back, so a
// canceled trade leaves the game as it was when the session started.
struct Snapshot
{
    struct Pokemon party[PARTY_SIZE];
    u8 partyCount;
    struct Mail mail[MAIL_COUNT];
    struct Pokedex pokedex;
    u8 seen1[NUM_DEX_FLAG_BYTES];
    u8 seen2[NUM_DEX_FLAG_BYTES];
    u8 giftRibbons[GIFT_RIBBONS_COUNT];
    u32 gameStats[NUM_GAME_STATS];
};

static EWRAM_DATA struct
{
    struct Packet queue[QUEUE_SIZE];
    u8 head, tail, count;
    u8 state;
    u8 id; // Multiplayer ID: 0 leads the trade, 1 follows. Set by Lua.
    u8 received; // GetBlockReceivedStatus bits, one per multiplayer ID.
    u8 barrier, remoteBarrier, close, remoteClose; // Generations.
    bool8 gotPeerHello, peerAcked;
    bool8 saveStarted, peerGone, notice;
    u16 silentFrames; // Frames since the last packet from the partner.
    u16 idleFrames; // Frames since the last packet to the partner.
    u8 savedWirelessCommType;
    bool8 savedLinkVSyncDisabled;
    u8 partnerName[PARTNER_NAME_SIZE];
    struct Snapshot snapshot;
} sLink = {0};

static const u8 sText_TradeCanceled[] = _("Trade canceled: {STR_VAR_1}\ndisconnected.");

static void Cancel(bool8 tellPeer);

static void ClearQueue(void)
{
    sLink.head = sLink.tail = sLink.count = 0;
}

static bool8 Queue(u8 kind, const void *data, u16 size)
{
    struct Packet *packet;

    if (size > BLOCK_BUFFER_SIZE || sLink.count == QUEUE_SIZE)
        return FALSE;
    packet = &sLink.queue[sLink.head];
    packet->bytes[0] = kind;
    if (size)
        memcpy(packet->bytes + 1, data, size);
    packet->size = size + 1;
    sLink.head = (sLink.head + 1) % QUEUE_SIZE;
    sLink.count++;
    sLink.idleFrames = 0;
    return TRUE;
}

static bool8 QueueByte(u8 kind, u8 value)
{
    return Queue(kind, &value, 1);
}

// One packet per frame: the mailbox streams a single multi-slot message at a time.
static void PumpQueue(void)
{
    struct Packet *packet;

    CoopMailbox_PumpOutbound();
    if (sLink.count && !CoopMailbox_IsSending())
    {
        packet = &sLink.queue[sLink.tail];
        if (CoopMailbox_SendMessage(COOP_MSG_LINK, packet->bytes, packet->size))
        {
            sLink.tail = (sLink.tail + 1) % QUEUE_SIZE;
            sLink.count--;
        }
    }
}

// A generation counter has been reached once the other side's is equal or
// ahead of it (it may already be waiting at the next barrier).
static bool8 Reached(u8 remote, u8 local)
{
    return (u8)(remote - local) < 0x80;
}

// Protocol player IDs are ASCII letters, digits, '_' and '-'.
static bool8 SetPartnerName(const u8 *id, u8 length)
{
    u8 name[PARTNER_NAME_SIZE];
    u8 i, j, c;

    for (i = 0, j = 0; i < length; i++)
    {
        c = id[i];
        if (c >= 'A' && c <= 'Z')
            name[j++] = CHAR_A + c - 'A';
        else if (c >= 'a' && c <= 'z')
            name[j++] = CHAR_a + c - 'a';
        else if (c >= '0' && c <= '9')
            name[j++] = CHAR_0 + c - '0';
        else if (c == '-')
            name[j++] = CHAR_HYPHEN;
        else if (c == '_')
        {
            name[j++] = CHAR_EXTRA_SYMBOL;
            name[j++] = CHAR_UNDERSCORE;
        }
        else
            return FALSE;
    }
    name[j] = EOS;
    memcpy(sLink.partnerName, name, j + 1);
    return TRUE;
}

static void TakeSnapshot(void)
{
    struct Snapshot *snapshot = &sLink.snapshot;

    memcpy(snapshot->party, gPlayerParty, sizeof(snapshot->party));
    snapshot->partyCount = gPlayerPartyCount;
    memcpy(snapshot->mail, gSaveBlock1Ptr->mail, sizeof(snapshot->mail));
    snapshot->pokedex = gSaveBlock2Ptr->pokedex;
    memcpy(snapshot->seen1, gSaveBlock1Ptr->seen1, sizeof(snapshot->seen1));
    memcpy(snapshot->seen2, gSaveBlock1Ptr->seen2, sizeof(snapshot->seen2));
    memcpy(snapshot->giftRibbons, gSaveBlock1Ptr->giftRibbons, sizeof(snapshot->giftRibbons));
    memcpy(snapshot->gameStats, gSaveBlock1Ptr->gameStats, sizeof(snapshot->gameStats));
}

static void RestoreSnapshot(void)
{
    const struct Snapshot *snapshot = &sLink.snapshot;

    memcpy(gPlayerParty, snapshot->party, sizeof(snapshot->party));
    gPlayerPartyCount = snapshot->partyCount;
    memcpy(gSaveBlock1Ptr->mail, snapshot->mail, sizeof(snapshot->mail));
    gSaveBlock2Ptr->pokedex = snapshot->pokedex;
    memcpy(gSaveBlock1Ptr->seen1, snapshot->seen1, sizeof(snapshot->seen1));
    memcpy(gSaveBlock1Ptr->seen2, snapshot->seen2, sizeof(snapshot->seen2));
    memcpy(gSaveBlock1Ptr->giftRibbons, snapshot->giftRibbons, sizeof(snapshot->giftRibbons));
    memcpy(gSaveBlock1Ptr->gameStats, snapshot->gameStats, sizeof(snapshot->gameStats));
}

static void SendHello(void)
{
    u8 bytes[HELLO_SIZE];

    bytes[HELLO_ACK] = sLink.gotPeerHello;
    memcpy(&bytes[HELLO_LINK_PLAYER], &gLinkPlayers[sLink.id], sizeof(struct LinkPlayer));
    Queue(COOP_LINK_HELLO, bytes, sizeof(bytes)); // Repeated until acknowledged.
}

// Ends the session: restores the link globals and tells Lua. The queue is
// still sent (LINK_CLOSING) so an ABORT reaches the partner before END.
static void Finish(u8 outcome)
{
    gReceivedRemoteLinkPlayers = FALSE;
    gWirelessCommType = sLink.savedWirelessCommType;
    gLinkVSyncDisabled = sLink.savedLinkVSyncDisabled;
    if (!QueueByte(COOP_LINK_END, outcome))
    {
        ClearQueue();
        QueueByte(COOP_LINK_END, outcome);
    }
    sLink.state = LINK_CLOSING;
}

// The partner is gone (timeout, left, Lua lost, or the partner's ABORT).
static void Cancel(bool8 tellPeer)
{
    if (sLink.state != LINK_CONNECTING && sLink.state != LINK_TRADING)
        return;
    if (sLink.peerGone)
        return;
    // Pending packets no longer matter; the partner only needs to hear ABORT.
    ClearQueue();
    if (tellPeer)
        Queue(COOP_LINK_ABORT, NULL, 0);
    if (sLink.saveStarted)
    {
        // Too late to cancel: release the barriers and finish the local save.
        sLink.peerGone = TRUE;
        return;
    }
    sLink.notice = TRUE;
    if (sLink.state == LINK_CONNECTING)
    {
        // Still in the overworld: nothing changed yet.
        UnlockPlayerFieldControls();
        Finish(COOP_LINK_END_CANCELED);
    }
    else
    {
        sLink.state = LINK_CANCELING;
    }
}

static bool8 TryStart(const struct CoopMessage *msg)
{
    struct LinkPlayerBlock local;
    struct LinkPlayer *player;
    u8 role, length;

    if (sLink.state != LINK_IDLE || sLink.notice || msg->length <= START_ID)
        return FALSE;
    role = msg->data[START_ROLE];
    length = msg->data[START_ID_LENGTH];
    if (role > 1 || length == 0 || length > COOP_LINK_PARTNER_ID_MAX || msg->length != START_ID + length)
        return FALSE;
    // Only from the free overworld: a debug trigger until the attendant (#82/#83).
    if (gMain.callback1 != CB1_Overworld || gMain.callback2 != CB2_Overworld
     || ArePlayerFieldControlsLocked() || CalculatePlayerPartyCount() == 0)
        return FALSE;
    if (!SetPartnerName(&msg->data[START_ID], length))
        return FALSE;

    sLink.state = LINK_CONNECTING;
    sLink.id = role;
    sLink.received = 0;
    sLink.barrier = sLink.remoteBarrier = sLink.close = sLink.remoteClose = 0;
    sLink.gotPeerHello = sLink.peerAcked = FALSE;
    sLink.saveStarted = sLink.peerGone = FALSE;
    sLink.silentFrames = sLink.idleFrames = 0;
    TakeSnapshot();
    LockPlayerFieldControls();
    CoopRemote_HideAll();
    sLink.savedWirelessCommType = gWirelessCommType;
    sLink.savedLinkVSyncDisabled = gLinkVSyncDisabled;
    gWirelessCommType = 0; // The cable branch of trade.c; RFU is not emulated.
    gLinkVSyncDisabled = TRUE;
    gLinkType = LINKTYPE_TRADE;
    // The vanilla initializer fills name, trainer ID, version, language and progress.
    LocalLinkPlayerToBlock();
    memcpy(&local, gBlockSendBuffer, sizeof(local));
    gLinkPlayers[role] = local.linkPlayer;
    player = &gLinkPlayers[role];
    player->id = role;
    player->progressFlagsCopy = player->progressFlags;
    // The trade's save continues the game here, not in a Cable Club room.
    SetDynamicWarpWithCoords(0, gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum,
                             -1, gSaveBlock1Ptr->pos.x, gSaveBlock1Ptr->pos.y);
    SendHello();
    return TRUE;
}

void CoopLink_HandleIdleMessage(const struct CoopMessage *msg)
{
    // Anything but START is a late packet from an ended session.
    if (msg->length == 0 || msg->data[0] != COOP_LINK_START)
        return;
    if (!TryStart(msg))
        QueueByte(COOP_LINK_END, COOP_LINK_END_REFUSED);
}

static void ReceiveBlock(const u8 *data, u16 size)
{
    if (size > 0 && size <= BLOCK_BUFFER_SIZE)
    {
        memcpy(gBlockRecvBuffer[sLink.id ^ 1], data, size);
        sLink.received |= 1 << (sLink.id ^ 1);
    }
}

static void Receive(const struct CoopMessage *msg)
{
    const u8 *data = msg->data + 1;
    u16 size = msg->length - 1;
    u8 peer = sLink.id ^ 1;

    if (msg->length == 0)
        return;
    switch (msg->data[0])
    {
    case COOP_LINK_CANCEL: // From Lua, not from the partner.
        Cancel(TRUE);
        return;
    case COOP_LINK_START:
    case COOP_LINK_END:
        return;
    }
    if (sLink.peerGone)
        return;
    sLink.silentFrames = 0;
    switch (msg->data[0])
    {
    case COOP_LINK_HELLO:
        if (size != HELLO_SIZE || sLink.state != LINK_CONNECTING)
            break;
        if (!sLink.gotPeerHello)
        {
            memcpy(&gLinkPlayers[peer], &data[HELLO_LINK_PLAYER], sizeof(struct LinkPlayer));
            gLinkPlayers[peer].id = peer;
            ConvertLinkPlayerName(&gLinkPlayers[peer]);
            sLink.gotPeerHello = TRUE;
            SendHello(); // Acknowledge at once.
        }
        if (data[HELLO_ACK])
            sLink.peerAcked = TRUE;
        break;
    case COOP_LINK_ABORT:
        Cancel(FALSE);
        break;
    case COOP_LINK_KEEPALIVE:
        break;
    case COOP_LINK_REQUEST:
        if (sLink.state != LINK_TRADING || size == 0 || size > BLOCK_BUFFER_SIZE)
            break;
        // Answer with this side's prepared buffer, like the cable's block request.
        CoopLink_SendBlock(gBlockSendBuffer, size, FALSE);
        // The request also carries the leader's own block.
        ReceiveBlock(data, size);
        break;
    case COOP_LINK_BLOCK:
        if (sLink.state == LINK_TRADING)
            ReceiveBlock(data, size);
        break;
    case COOP_LINK_BARRIER:
        if (sLink.state == LINK_TRADING && size == 1)
            sLink.remoteBarrier = data[0];
        break;
    case COOP_LINK_CLOSE:
        if (sLink.state == LINK_TRADING && size == 1)
            sLink.remoteClose = data[0];
        break;
    }
}

bool8 CoopLink_IsActive(void)
{
    return sLink.state == LINK_CONNECTING || sLink.state == LINK_TRADING || sLink.state == LINK_CANCELING;
}

u8 CoopLink_GetMultiplayerId(void)
{
    return sLink.id;
}

u8 CoopLink_GetBlockReceivedStatus(void)
{
    return sLink.received;
}

void CoopLink_ResetBlockReceivedFlags(u8 mask)
{
    sLink.received &= ~mask;
}

bool8 CoopLink_IsTaskFinished(void)
{
    if (sLink.peerGone)
        return TRUE;
    if (sLink.state != LINK_TRADING)
        return FALSE;
    return sLink.count == 0 && !CoopMailbox_IsSending() && Reached(sLink.remoteBarrier, sLink.barrier);
}

bool8 CoopLink_SendBlock(const void *data, u16 size, bool8 request)
{
    if (!data || !size || size > BLOCK_BUFFER_SIZE || sLink.state != LINK_TRADING)
        return FALSE;
    if (!sLink.peerGone && !Queue(request ? COOP_LINK_REQUEST : COOP_LINK_BLOCK, data, size))
    {
        Cancel(TRUE);
        return FALSE;
    }
    memcpy(gBlockRecvBuffer[sLink.id], data, size);
    sLink.received |= 1 << sLink.id; // The cable also echoes the local block.
    return TRUE;
}

void CoopLink_SetBarrier(bool8 close)
{
    u8 generation;

    if (close)
        generation = ++sLink.close;
    else
        generation = ++sLink.barrier;
    if (sLink.peerGone)
    {
        sLink.remoteClose = sLink.close;
        sLink.remoteBarrier = sLink.barrier;
    }
    else if (sLink.state == LINK_TRADING && !QueueByte(close ? COOP_LINK_CLOSE : COOP_LINK_BARRIER, generation))
    {
        Cancel(TRUE);
    }
}

void CoopLink_Open(void)
{
    sLink.received = 0;
    sLink.close = sLink.remoteClose = 0;
    gReceivedRemoteLinkPlayers = TRUE;
}

void CoopLink_OnSaveStart(void)
{
    if (sLink.state == LINK_TRADING)
        sLink.saveStarted = TRUE;
}

void CoopLink_ReturnToField(void)
{
    bool8 canceled = (sLink.state == LINK_CANCELING);

    if (canceled)
        RestoreSnapshot();
    Finish(canceled ? COOP_LINK_END_CANCELED : COOP_LINK_END_DONE);
    gSoftResetDisabled = FALSE;
    // ReturnToFieldLocal rebuilds the map but keeps callback1. Restore the
    // input callback cleared by the animation (or left by the trade menu).
    SetMainCallback1(CB1_Overworld);
    gFieldCallback = NULL;
    ScriptContext_Init();
    UnlockPlayerFieldControls();
    // The trade screens play their own music; resume the map's.
    StopMapMusic();
    Overworld_PlaySpecialMapMusic();
    CB2_ReturnToField();
}

static void Task_TradeCanceledNotice(u8 taskId)
{
    // The box stays drawn after the text finishes printing.
    if (IsFieldMessageBoxHidden() && JOY_NEW(A_BUTTON | B_BUTTON))
    {
        HideFieldMessageBox();
        UnlockPlayerFieldControls();
        DestroyTask(taskId);
    }
}

// "Trade canceled: X disconnected." once the field is back and free.
static void TryShowNotice(void)
{
    if (!sLink.notice || gMain.callback1 != CB1_Overworld || gMain.callback2 != CB2_Overworld
     || ArePlayerFieldControlsLocked() || gPaletteFade.active || !IsFieldMessageBoxHidden())
        return;
    StringCopy(gStringVar1, sLink.partnerName);
    LockPlayerFieldControls();
    ShowFieldMessage(sText_TradeCanceled);
    CreateTask(Task_TradeCanceledNotice, 80);
    sLink.notice = FALSE;
}

void CoopLink_Update(void)
{
    struct CoopMessage msg;
    u8 count;

    if (!CoopLink_IsActive())
    {
        // Outside a session only END/ABORT (or a refusal) are left to send.
        if (!CoopMailbox_IsLuaConnected())
            ClearQueue();
        PumpQueue();
        if (sLink.state == LINK_CLOSING && sLink.count == 0)
            sLink.state = LINK_IDLE;
        TryShowNotice();
        return;
    }

    if (!CoopMailbox_IsLuaConnected())
    {
        // No relay: the partner can't be reached. Old inbound slots are stale.
        CoopMailbox_DiscardInbound();
        Cancel(FALSE);
    }
    else
    {
        // The ring holds at most COOP_MAILBOX_SLOT_COUNT - 1 slots.
        for (count = 0; count < COOP_MAILBOX_SLOT_COUNT && CoopMailbox_Receive(&msg); count++)
        {
            if (msg.type == COOP_MSG_LINK)
                Receive(&msg);
            else
                CoopRemote_ApplyMessage(&msg); // Presence keeps flowing during the session.
        }
    }

    if ((sLink.state == LINK_CONNECTING || sLink.state == LINK_TRADING) && !sLink.peerGone
     && ++sLink.silentFrames > COOP_LINK_TIMEOUT_FRAMES)
        Cancel(TRUE);

    PumpQueue();

    if ((sLink.state == LINK_CONNECTING || sLink.state == LINK_TRADING) && !sLink.peerGone
     && sLink.count == 0 && ++sLink.idleFrames >= COOP_LINK_KEEPALIVE_FRAMES)
    {
        if (sLink.state == LINK_CONNECTING)
            SendHello();
        else
            Queue(COOP_LINK_KEEPALIVE, NULL, 0);
    }

    switch (sLink.state)
    {
    case LINK_CONNECTING:
        if (sLink.gotPeerHello && sLink.peerAcked && sLink.count == 0 && !CoopMailbox_IsSending())
        {
            sLink.state = LINK_TRADING;
            CoopLink_Open();
            gMain.savedCallback = CoopLink_ReturnToField;
            CB2_StartCreateTradeMenu();
        }
        break;
    case LINK_TRADING:
        if (sLink.close && Reached(sLink.remoteClose, sLink.close) && CoopLink_IsTaskFinished())
            gReceivedRemoteLinkPlayers = FALSE;
        break;
    case LINK_CANCELING:
        // trade.c frees its screen and then calls CoopLink_ReturnToField.
        TryLeaveCoopLinkTrade();
        break;
    }
}
