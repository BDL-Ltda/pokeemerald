#include "global.h"
#include "coop/link_spike.h"
#include "coop/mailbox.h"
#include "coop/remote_players.h"
#include "event_data.h"
#include "link.h"
#include "main.h"
#include "overworld.h"
#include "pokemon.h"
#include "script.h"
#include "trade.h"
#include "constants/species.h"

// Temporary mailbox message 5, documented in docs/virtual-link-spike.md.
// The ordinary bridge never sends START: only the explicit spike script does.
#define SPIKE_MSG COOP_MSG_LINK_SPIKE
#define START 0
#define HELLO 1
#define BLOCK 2
#define REQUEST 3
#define BARRIER 4
#define CLOSE 5
#define QUEUE_SIZE 4

struct Packet
{
    u16 size;
    u8 bytes[BLOCK_BUFFER_SIZE + 1];
};

static EWRAM_DATA struct {
    struct Packet queue[QUEUE_SIZE];
    u8 head, tail, count;
    u8 id, received;
    u8 barrier, remoteBarrier, close, remoteClose;
    bool8 active, hello, entered, failed, returning;
    u16 trace[9];
    u16 traced;
} sSpike = {0};

void CoopLinkSpike_Trace(u8 stage, u16 value)
{
    u8 bytes[3];
    if ((!sSpike.active && !sSpike.returning) || stage >= ARRAY_COUNT(sSpike.trace))
        return;
    if ((sSpike.traced & (1 << stage)) && sSpike.trace[stage] == value)
        return;
    bytes[0] = stage;
    bytes[1] = value;
    bytes[2] = value >> 8;
    if (CoopMailbox_Push(COOP_MSG_SPIKE_TRACE, bytes, sizeof(bytes)))
    {
        sSpike.trace[stage] = value;
        sSpike.traced |= 1 << stage;
    }
}

static bool8 Queue(u8 type, const void *data, u16 size)
{
    struct Packet *packet;
    if (size > BLOCK_BUFFER_SIZE || sSpike.count == QUEUE_SIZE)
    {
        sSpike.failed = TRUE; // Freeze the experiment rather than overwrite data.
        return FALSE;
    }
    packet = &sSpike.queue[sSpike.head];
    packet->bytes[0] = type;
    if (size)
        memcpy(packet->bytes + 1, data, size);
    packet->size = size + 1;
    sSpike.head = (sSpike.head + 1) % QUEUE_SIZE;
    sSpike.count++;
    return TRUE;
}

bool8 CoopLinkSpike_IsActive(void) { return sSpike.active; }
u8 CoopLinkSpike_Id(void) { return sSpike.id; }
u8 CoopLinkSpike_Received(void) { return sSpike.received; }
void CoopLinkSpike_ResetReceived(u8 mask) { sSpike.received &= ~mask; }

bool8 CoopLinkSpike_Finished(void)
{
    return !sSpike.failed && sSpike.count == 0 && !CoopMailbox_IsSending()
        && sSpike.remoteBarrier == sSpike.barrier;
}

void CoopLinkSpike_Barrier(bool8 close)
{
    u8 generation;
    if (close)
    {
        generation = ++sSpike.close;
        Queue(CLOSE, &generation, 1);
    }
    else
    {
        generation = ++sSpike.barrier;
        Queue(BARRIER, &generation, 1);
    }
}

void CoopLinkSpike_Open(void)
{
    sSpike.received = 0;
    sSpike.close = sSpike.remoteClose = 0;
    gReceivedRemoteLinkPlayers = TRUE;
}

bool8 CoopLinkSpike_Send(const void *data, u16 size, bool8 request)
{
    if (!data || !size || size > BLOCK_BUFFER_SIZE)
        return FALSE;
    if (!Queue(request ? REQUEST : BLOCK, data, size))
        return FALSE;
    memcpy(gBlockRecvBuffer[sSpike.id], data, size);
    sSpike.received |= 1 << sSpike.id; // The cable also echoes the local block.
    return TRUE;
}

void CoopLinkSpike_Start(const struct CoopMessage *msg)
{
    struct LinkPlayerBlock local;
    struct LinkPlayer *player;
    u8 id;
    if (sSpike.active || msg->length != 3 || msg->data[0] != START || msg->data[1] > 1 || msg->data[2] > 1
     || gMain.callback1 != CB1_Overworld || gMain.callback2 != CB2_Overworld
     || ArePlayerFieldControlsLocked() || CalculatePlayerPartyCount() == 0)
        return;
    id = msg->data[1];
    memset(&sSpike, 0, sizeof(sSpike));
    sSpike.active = TRUE;
    sSpike.id = id;
    // Explicit test-only opt-in. Never use fixtures with a valuable save: the
    // vanilla trade will later save this party. Normal START keeps the party.
    if (msg->data[2])
    {
        ZeroPlayerPartyMons();
        CreateMon(&gPlayerParty[0], id == 0 ? SPECIES_KADABRA : SPECIES_MACHOKE,
                  25, USE_RANDOM_IVS, FALSE, 0, OT_ID_PLAYER_ID, 0);
        CreateMon(&gPlayerParty[1], SPECIES_ZIGZAGOON, 10, USE_RANDOM_IVS, FALSE, 0, OT_ID_PLAYER_ID, 0);
        CalculatePlayerPartyCount();
    }
    CoopRemote_DespawnAll();
    gWirelessCommType = 0;
    gLinkVSyncDisabled = TRUE;
    gLinkType = LINKTYPE_TRADE;
    // Let the vanilla initializer fill name, trainer ID, language and progress.
    LocalLinkPlayerToBlock();
    memcpy(&local, gBlockSendBuffer, sizeof(local));
    gLinkPlayers[id] = local.linkPlayer;
    player = &gLinkPlayers[id];
    player->id = id;
    player->progressFlagsCopy = player->progressFlags;
    SetDynamicWarpWithCoords(0, gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum,
                             -1, gSaveBlock1Ptr->pos.x, gSaveBlock1Ptr->pos.y);
    Queue(HELLO, player, sizeof(*player));
}

static void Receive(const struct CoopMessage *msg)
{
    const u8 *data = msg->data + 1;
    u16 size = msg->length - 1;
    u8 peer = sSpike.id ^ 1;
    switch (msg->data[0])
    {
    case HELLO:
        if (size == sizeof(struct LinkPlayer) && !sSpike.entered)
        {
            memcpy(&gLinkPlayers[peer], data, size);
            gLinkPlayers[peer].id = peer;
            ConvertLinkPlayerName(&gLinkPlayers[peer]);
            sSpike.hello = TRUE;
        }
        break;
    case REQUEST:
        if (size > 0 && size <= BLOCK_BUFFER_SIZE)
            CoopLinkSpike_Send(gBlockSendBuffer, size, FALSE);
        // The request also contains the leader's block.
        // fall through
    case BLOCK:
        if (size > 0 && size <= BLOCK_BUFFER_SIZE)
        {
            memcpy(gBlockRecvBuffer[peer], data, size);
            sSpike.received |= 1 << peer;
        }
        break;
    case BARRIER:
        if (size == 1) sSpike.remoteBarrier = data[0];
        break;
    case CLOSE:
        if (size == 1) sSpike.remoteClose = data[0];
        break;
    }
}

void CoopLinkSpike_ReturnToField(void)
{
    sSpike.active = FALSE;
    sSpike.returning = TRUE;
    CoopLinkSpike_Trace(6, 1);
    gReceivedRemoteLinkPlayers = FALSE;
    gMain.callback1 = NULL;
    gFieldCallback = NULL;
    ScriptContext_Init();
    UnlockPlayerFieldControls();
    CB2_ReturnToField();
}

void CoopLinkSpike_Update(void)
{
    struct CoopMessage msg;
    u8 count;
    struct Packet *packet;
    if (sSpike.returning && gMain.callback2 == CB2_Overworld)
    {
        CoopLinkSpike_Trace(6, 2);
        sSpike.returning = FALSE;
    }
    if (!sSpike.active)
        return;
    // This runs from the main loop, including summary/animation/evolution/save.
    // Never rely on CB1_Overworld to service a trade's mailbox.
    for (count = 0; count < COOP_MAILBOX_SLOT_COUNT && CoopMailbox_Receive(&msg); count++)
        if (msg.type == SPIKE_MSG && msg.length > 0)
            Receive(&msg);
    CoopMailbox_PumpOutbound();
    if (sSpike.count && !CoopMailbox_IsSending())
    {
        packet = &sSpike.queue[sSpike.tail];
        if (CoopMailbox_SendMessage(SPIKE_MSG, packet->bytes, packet->size))
        {
            sSpike.tail = (sSpike.tail + 1) % QUEUE_SIZE;
            sSpike.count--;
        }
    }
    if (!sSpike.entered && sSpike.hello && CoopLinkSpike_Finished())
    {
        sSpike.entered = TRUE;
        CoopLinkSpike_Open();
        gMain.savedCallback = CoopLinkSpike_ReturnToField;
        CB2_StartCreateTradeMenu();
    }
    if (sSpike.close && sSpike.close == sSpike.remoteClose && CoopLinkSpike_Finished())
        gReceivedRemoteLinkPlayers = FALSE;
}
