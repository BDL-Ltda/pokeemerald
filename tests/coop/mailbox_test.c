#include "global.h"
#include "coop/mailbox.h"
#include <assert.h>
#include <stdio.h>

// Host tests for mailbox layout v2 (multi-slot messages). The helpers below
// play the Lua side of each ring, following docs/memory-map.md.

static unsigned sCases;
static u8 sPattern[COOP_MAILBOX_MESSAGE_MAX];

static void Setup(void)
{
    unsigned i;

    CoopMailbox_Init();
    gCoopMailbox.flags |= COOP_MAILBOX_FLAG_LUA_CONNECTED;
    for (i = 0; i < sizeof(sPattern); i++)
        sPattern[i] = (u8)(i * 7 + i / 256 + 3);
}

// Lua reading the outbound ring.
static bool8 LuaPop(struct CoopMailboxSlot *slot)
{
    struct CoopMailboxRing *ring = &gCoopMailbox.outbound;

    if (ring->tail == ring->head)
        return FALSE;
    memcpy(slot, &ring->slots[ring->tail], sizeof(*slot));
    ring->tail = (ring->tail + 1) % COOP_MAILBOX_SLOT_COUNT;
    return TRUE;
}

// Lua writing the inbound ring.
static bool8 LuaPush(u8 type, const u8 *payload, u8 len)
{
    struct CoopMailboxRing *ring = &gCoopMailbox.inbound;
    struct CoopMailboxSlot *slot;
    u8 next = (ring->head + 1) % COOP_MAILBOX_SLOT_COUNT;

    if (next == ring->tail)
        return FALSE;
    slot = &ring->slots[ring->head];
    memset(slot, 0, sizeof(*slot));
    slot->type = type;
    slot->length = len;
    memcpy(slot->payload, payload, len);
    ring->head = next;
    return TRUE;
}

static u8 FragmentCount(u16 length)
{
    return (length + COOP_FRAGMENT_DATA_SIZE - 1) / COOP_FRAGMENT_DATA_SIZE;
}

// Pushes fragment `index` of a message; `size` overrides the data size if not 0.
static bool8 LuaPushFragment(u8 type, u8 index, u16 length, const u8 *data, u8 size)
{
    u8 payload[COOP_MAILBOX_PAYLOAD_SIZE];
    u16 offset = index * COOP_FRAGMENT_DATA_SIZE;

    if (size == 0)
        size = length - offset < COOP_FRAGMENT_DATA_SIZE ? length - offset : COOP_FRAGMENT_DATA_SIZE;
    payload[COOP_FRAGMENT_TYPE] = type;
    payload[COOP_FRAGMENT_INDEX] = index;
    payload[COOP_FRAGMENT_TOTAL_LENGTH] = length & 0xFF;
    payload[COOP_FRAGMENT_TOTAL_LENGTH + 1] = length >> 8;
    memcpy(&payload[COOP_FRAGMENT_HEADER_SIZE], &data[offset], size);
    return LuaPush(COOP_MSG_FRAGMENT, payload, COOP_FRAGMENT_HEADER_SIZE + size);
}

static void TestHeader(void)
{
    CoopMailbox_Init();
    assert(memcmp(gCoopMailbox.magic, "COOP", 4) == 0);
    assert(gCoopMailbox.layoutVersion == 2);
    assert(gCoopMailbox.flags == COOP_MAILBOX_FLAG_ROM_READY);
    sCases++;
}

// Streams one message to the fake Lua reader, pushing a presence slot every
// frame, and checks every fragment header and the reassembled bytes.
static void SendAndCollect(u16 length)
{
    static u8 received[COOP_MAILBOX_MESSAGE_MAX];
    static const u8 presence[13] = {0, 9, 7, 0, 255, 255, 4, 1, 1, 239, 190, 173, 222};
    struct CoopMailboxSlot slot;
    u16 done = 0;
    u8 nextIndex = 0;
    unsigned frames = 0, presences = 0;

    Setup();
    assert(CoopMailbox_SendMessage(COOP_MSG_DEBUG_ECHO, sPattern, length));
    while (done < length)
    {
        assert(++frames < 100);
        // Lua drains at most a few slots per frame, so the sender has to wait.
        for (unsigned i = 0; i < 6 && LuaPop(&slot); i++)
        {
            if (slot.type == COOP_MSG_PRESENCE)
            {
                assert(slot.length == sizeof(presence));
                assert(memcmp(slot.payload, presence, sizeof(presence)) == 0);
                presences++;
                continue;
            }
            assert(slot.type == COOP_MSG_FRAGMENT);
            assert(slot.payload[COOP_FRAGMENT_TYPE] == COOP_MSG_DEBUG_ECHO);
            assert(slot.payload[COOP_FRAGMENT_INDEX] == nextIndex);
            assert((slot.payload[COOP_FRAGMENT_TOTAL_LENGTH] | (slot.payload[COOP_FRAGMENT_TOTAL_LENGTH + 1] << 8))
                   == length);
            assert(slot.length > COOP_FRAGMENT_HEADER_SIZE);
            for (unsigned b = slot.length; b < COOP_MAILBOX_PAYLOAD_SIZE; b++)
                assert(slot.payload[b] == 0);
            memcpy(&received[done], &slot.payload[COOP_FRAGMENT_HEADER_SIZE], slot.length - COOP_FRAGMENT_HEADER_SIZE);
            done += slot.length - COOP_FRAGMENT_HEADER_SIZE;
            nextIndex++;
        }
        // One frame of ROM work: presence first, then the stream.
        CoopMailbox_Push(COOP_MSG_PRESENCE, presence, sizeof(presence));
        CoopMailbox_PumpOutbound();
    }
    assert(done == length);
    assert(nextIndex == FragmentCount(length));
    assert(memcmp(received, sPattern, length) == 0);
    assert(!CoopMailbox_IsSending());
    if (length > 15 * COOP_FRAGMENT_DATA_SIZE)
        assert(frames > 1 && presences > 0); // Past the ring's capacity, with presence flowing.
}

static void TestSendStreams(void)
{
    SendAndCollect(600);
    SendAndCollect(1);
    SendAndCollect(COOP_FRAGMENT_DATA_SIZE);
    SendAndCollect(COOP_FRAGMENT_DATA_SIZE + 1);
    SendAndCollect(COOP_MAILBOX_MESSAGE_MAX);
    sCases++;
}

static void TestSendRejects(void)
{
    Setup();
    assert(!CoopMailbox_SendMessage(COOP_MSG_NONE, sPattern, 10));
    assert(!CoopMailbox_SendMessage(COOP_MSG_FRAGMENT, sPattern, 10));
    assert(!CoopMailbox_SendMessage(COOP_MSG_DEBUG_ECHO, sPattern, 0));
    assert(!CoopMailbox_SendMessage(COOP_MSG_DEBUG_ECHO, sPattern, COOP_MAILBOX_MESSAGE_MAX + 1));
    assert(gCoopMailbox.outbound.head == gCoopMailbox.outbound.tail);
    gCoopMailbox.flags &= ~COOP_MAILBOX_FLAG_LUA_CONNECTED;
    assert(!CoopMailbox_SendMessage(COOP_MSG_DEBUG_ECHO, sPattern, 10));
    gCoopMailbox.flags |= COOP_MAILBOX_FLAG_LUA_CONNECTED;
    assert(CoopMailbox_SendMessage(COOP_MSG_DEBUG_ECHO, sPattern, 600));
    assert(CoopMailbox_IsSending());
    assert(!CoopMailbox_SendMessage(COOP_MSG_DEBUG_ECHO, sPattern, 10)); // Busy.
    sCases++;
}

static void TestDisconnectAbandons(void)
{
    struct CoopMailboxSlot slot;

    Setup();
    assert(CoopMailbox_SendMessage(COOP_MSG_DEBUG_ECHO, sPattern, 600));
    while (LuaPop(&slot))
        ;
    gCoopMailbox.flags &= ~COOP_MAILBOX_FLAG_LUA_CONNECTED;
    CoopMailbox_PumpOutbound();
    assert(!CoopMailbox_IsSending());
    assert(gCoopMailbox.outbound.head == gCoopMailbox.outbound.tail);
    gCoopMailbox.flags |= COOP_MAILBOX_FLAG_LUA_CONNECTED;
    assert(CoopMailbox_SendMessage(COOP_MSG_DEBUG_ECHO, sPattern, 30));
    assert(LuaPop(&slot) && slot.payload[COOP_FRAGMENT_INDEX] == 0); // The next message starts over.
    assert(LuaPop(&slot) && slot.payload[COOP_FRAGMENT_INDEX] == 1);
    assert(!LuaPop(&slot) && !CoopMailbox_IsSending());
    // Init (soft reset) forgets the message in progress too.
    assert(CoopMailbox_SendMessage(COOP_MSG_DEBUG_ECHO, sPattern, 600));
    assert(CoopMailbox_IsSending());
    CoopMailbox_Init();
    assert(!CoopMailbox_IsSending());
    sCases++;
}

static void TestReceiveReassembles(void)
{
    static const u8 leave[1] = {2};
    struct CoopMessage msg;
    u8 index = 0, count = FragmentCount(600);
    unsigned frames = 0;
    bool8 gotLeave = FALSE, gotMessage = FALSE;

    Setup();
    while (!gotMessage)
    {
        assert(++frames < 100);
        // One Lua frame: a single-slot message, then as many fragments as fit.
        assert(LuaPush(COOP_MSG_LEAVE, leave, sizeof(leave)));
        while (index < count && LuaPushFragment(COOP_MSG_DEBUG_ECHO, index, 600, sPattern, 0))
            index++;
        // One ROM frame.
        while (CoopMailbox_Receive(&msg))
        {
            if (msg.type == COOP_MSG_LEAVE)
            {
                assert(msg.length == 1 && msg.data[0] == 2);
                gotLeave = TRUE;
                continue;
            }
            assert(!gotMessage && msg.type == COOP_MSG_DEBUG_ECHO && msg.length == 600);
            assert(memcmp(msg.data, sPattern, 600) == 0);
            gotMessage = TRUE;
        }
    }
    assert(gotLeave && frames > 1 && index == count);
    assert(!CoopMailbox_Receive(&msg));
    sCases++;
}

// Pushes a whole message (at most 15 fragments) and expects it, alone, next.
static void ExpectDelivered(u16 length)
{
    struct CoopMessage msg;
    u8 i;

    for (i = 0; i < FragmentCount(length); i++)
        assert(LuaPushFragment(COOP_MSG_DEBUG_ECHO, i, length, sPattern, 0));
    assert(CoopMailbox_Receive(&msg));
    assert(msg.type == COOP_MSG_DEBUG_ECHO && msg.length == length);
    assert(memcmp(msg.data, sPattern, length) == 0);
    assert(!CoopMailbox_Receive(&msg));
}

static void ExpectNothing(void)
{
    struct CoopMessage msg;

    while (CoopMailbox_Receive(&msg))
        assert(FALSE);
    assert(gCoopMailbox.inbound.head == gCoopMailbox.inbound.tail);
}

static void TestReceiveDropsIncomplete(void)
{
    u8 i;

    Setup();
    // The sender stops after 5 of 24 fragments; the next message starts over.
    for (i = 0; i < 5; i++)
        assert(LuaPushFragment(COOP_MSG_DEBUG_ECHO, i, 600, sPattern, 0));
    ExpectNothing();
    ExpectDelivered(100);

    // A gap (fragment 2 missing) drops the message; so do the fragments after it.
    assert(LuaPushFragment(COOP_MSG_DEBUG_ECHO, 0, 100, sPattern, 0));
    assert(LuaPushFragment(COOP_MSG_DEBUG_ECHO, 1, 100, sPattern, 0));
    assert(LuaPushFragment(COOP_MSG_DEBUG_ECHO, 3, 100, sPattern, 0));
    ExpectNothing();
    ExpectDelivered(27);

    // A continuation with another total length, another type or no first fragment.
    assert(LuaPushFragment(COOP_MSG_DEBUG_ECHO, 0, 100, sPattern, 0));
    assert(LuaPushFragment(COOP_MSG_DEBUG_ECHO, 1, 101, sPattern, 0));
    assert(LuaPushFragment(COOP_MSG_DEBUG_ECHO, 0, 100, sPattern, 0));
    assert(LuaPushFragment(COOP_MSG_PRESENCE, 1, 100, sPattern, 0));
    assert(LuaPushFragment(COOP_MSG_DEBUG_ECHO, 2, 100, sPattern, 0));
    ExpectNothing();
    ExpectDelivered(52);

    // Short fragments and bad first fragments.
    assert(LuaPushFragment(COOP_MSG_DEBUG_ECHO, 0, 100, sPattern, 20)); // Not full.
    assert(LuaPushFragment(COOP_MSG_DEBUG_ECHO, 0, 0, sPattern, 1));
    assert(LuaPushFragment(COOP_MSG_DEBUG_ECHO, 0, COOP_MAILBOX_MESSAGE_MAX + 1, sPattern, 26));
    assert(LuaPushFragment(COOP_MSG_NONE, 0, 10, sPattern, 0));
    assert(LuaPushFragment(COOP_MSG_FRAGMENT, 0, 10, sPattern, 0));
    assert(LuaPush(COOP_MSG_FRAGMENT, sPattern, COOP_FRAGMENT_HEADER_SIZE)); // Header only.
    ExpectNothing();
    ExpectDelivered(1);

    // A disconnect drops the message in progress.
    for (i = 0; i < 3; i++)
        assert(LuaPushFragment(COOP_MSG_DEBUG_ECHO, i, 100, sPattern, 0));
    CoopMailbox_DiscardInbound();
    assert(gCoopMailbox.inbound.head == gCoopMailbox.inbound.tail);
    assert(LuaPushFragment(COOP_MSG_DEBUG_ECHO, 3, 100, sPattern, 0));
    ExpectNothing();
    ExpectDelivered(100);

    // A partial message received before Init is gone after it.
    for (i = 0; i < 3; i++)
        assert(LuaPushFragment(COOP_MSG_DEBUG_ECHO, i, 100, sPattern, 0));
    ExpectNothing();
    Setup();
    assert(LuaPushFragment(COOP_MSG_DEBUG_ECHO, 3, 100, sPattern, 0));
    ExpectNothing();
    sCases++;
}

int main(void)
{
    TestHeader();
    TestSendStreams();
    TestSendRejects();
    TestDisconnectAbandons();
    TestReceiveReassembles();
    TestReceiveDropsIncomplete();
    printf("PASS coop mailbox: %u test groups (ASan/UBSan)\n", sCases);
    return 0;
}
