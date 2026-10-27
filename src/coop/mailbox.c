#include "global.h"
#include "coop/mailbox.h"

// The Lua side depends on this exact layout (docs/memory-map.md, "Mailbox").
STATIC_ASSERT(sizeof(struct CoopMailboxSlot) == 0x20, CoopMailboxSlotSize)
STATIC_ASSERT(offsetof(struct CoopMailboxSlot, type) == 0x00, CoopMailboxSlotTypeOffset)
STATIC_ASSERT(offsetof(struct CoopMailboxSlot, length) == 0x01, CoopMailboxSlotLengthOffset)
STATIC_ASSERT(offsetof(struct CoopMailboxSlot, payload) == 0x02, CoopMailboxSlotPayloadOffset)

STATIC_ASSERT(sizeof(struct CoopMailboxRing) == 0x202, CoopMailboxRingSize)
STATIC_ASSERT(offsetof(struct CoopMailboxRing, head) == 0x00, CoopMailboxRingHeadOffset)
STATIC_ASSERT(offsetof(struct CoopMailboxRing, tail) == 0x01, CoopMailboxRingTailOffset)
STATIC_ASSERT(offsetof(struct CoopMailboxRing, slots) == 0x02, CoopMailboxRingSlotsOffset)

STATIC_ASSERT(sizeof(struct CoopMailbox) == 0x40A, CoopMailboxSize)
STATIC_ASSERT(offsetof(struct CoopMailbox, magic) == 0x000, CoopMailboxMagicOffset)
STATIC_ASSERT(offsetof(struct CoopMailbox, layoutVersion) == 0x004, CoopMailboxLayoutVersionOffset)
STATIC_ASSERT(offsetof(struct CoopMailbox, flags) == 0x005, CoopMailboxFlagsOffset)
STATIC_ASSERT(offsetof(struct CoopMailbox, outbound) == 0x006, CoopMailboxOutboundOffset)
STATIC_ASSERT(offsetof(struct CoopMailbox, inbound) == 0x208, CoopMailboxInboundOffset)

STATIC_ASSERT(sizeof(COOP_MAILBOX_MAGIC) - 1 == COOP_MAILBOX_MAGIC_LENGTH, CoopMailboxMagicLength)

// Multi-slot messages (docs/memory-map.md, "Multi-slot messages").
STATIC_ASSERT(COOP_FRAGMENT_DATA_SIZE == 26, CoopFragmentDataSize)
STATIC_ASSERT((COOP_MAILBOX_MESSAGE_MAX + COOP_FRAGMENT_DATA_SIZE - 1) / COOP_FRAGMENT_DATA_SIZE <= 256,
              CoopFragmentIndexFitsU8)
STATIC_ASSERT(COOP_MAILBOX_MESSAGE_MAX <= 0xFFFF, CoopFragmentTotalLengthFitsU16)

// One multi-slot message on its way, in either direction. `done` counts the
// data bytes already pushed (outbound) or collected (inbound).
struct CoopMessageStream
{
    bool8 active;
    u8 type;
    u8 nextIndex;
    u16 length;
    u16 done;
    u8 data[COOP_MAILBOX_MESSAGE_MAX];
};

// mGBA runs the Lua frame callback at the frame boundary, which can fall in the
// middle of these functions. This compiler barrier keeps the slot accesses on
// the correct side of the head/tail update, so Lua never sees a half-written
// slot and we never read one. No lock is needed: each index has one writer.
#define COOP_MAILBOX_BARRIER() asm volatile("" ::: "memory")

EWRAM_DATA struct CoopMailbox gCoopMailbox = {0};
static EWRAM_DATA struct CoopMessageStream sOutMessage = {0};
static EWRAM_DATA struct CoopMessageStream sInMessage = {0};
// The last single-slot message CoopMailbox_Receive returned.
static EWRAM_DATA struct CoopMailboxSlot sInSlot = {0};

void CoopMailbox_Init(void)
{
    sOutMessage.active = FALSE;
    sInMessage.active = FALSE;
    memset(&gCoopMailbox, 0, sizeof(gCoopMailbox));
    gCoopMailbox.layoutVersion = COOP_MAILBOX_LAYOUT_VERSION;
    gCoopMailbox.flags = COOP_MAILBOX_FLAG_ROM_READY;
    // Write the magic last, so a valid magic means the header is complete.
    COOP_MAILBOX_BARRIER();
    memcpy(gCoopMailbox.magic, COOP_MAILBOX_MAGIC, COOP_MAILBOX_MAGIC_LENGTH);
}

bool8 CoopMailbox_Push(u8 type, const void *payload, u8 len)
{
    struct CoopMailboxRing *ring = &gCoopMailbox.outbound;
    struct CoopMailboxSlot *slot;
    u8 head = ring->head;
    u8 next = (head + 1) % COOP_MAILBOX_SLOT_COUNT;

    if (len > COOP_MAILBOX_PAYLOAD_SIZE)
        return FALSE;
    COOP_MAILBOX_BARRIER(); // Read the tail Lua last wrote.
    if (next == ring->tail)
        return FALSE;

    slot = &ring->slots[head];
    slot->type = type;
    slot->length = len;
    if (len != 0)
        memcpy(slot->payload, payload, len);
    memset(slot->payload + len, 0, COOP_MAILBOX_PAYLOAD_SIZE - len);

    COOP_MAILBOX_BARRIER(); // Publish the slot before the new head.
    ring->head = next;
    return TRUE;
}

bool8 CoopMailbox_Pop(struct CoopMailboxSlot *msg)
{
    struct CoopMailboxRing *ring = &gCoopMailbox.inbound;
    u8 tail = ring->tail;

    COOP_MAILBOX_BARRIER(); // Read the head Lua last wrote.
    if (tail == ring->head)
        return FALSE;

    COOP_MAILBOX_BARRIER(); // Read the slot only after seeing its head.
    memcpy(msg, &ring->slots[tail], sizeof(*msg));
    // The slot came from Lua: never let a bad length send a caller past the payload.
    if (msg->length > COOP_MAILBOX_PAYLOAD_SIZE)
        msg->length = COOP_MAILBOX_PAYLOAD_SIZE;

    COOP_MAILBOX_BARRIER(); // Finish reading before releasing the slot.
    ring->tail = (tail + 1) % COOP_MAILBOX_SLOT_COUNT;
    return TRUE;
}

bool8 CoopMailbox_IsLuaConnected(void)
{
    COOP_MAILBOX_BARRIER(); // Lua sets and clears this bit between frames.
    return (gCoopMailbox.flags & COOP_MAILBOX_FLAG_LUA_CONNECTED) != 0;
}

static bool8 IsMessageType(u8 type)
{
    return type != COOP_MSG_NONE && type != COOP_MSG_FRAGMENT;
}

static u16 FragmentSize(u16 length, u16 done)
{
    return length - done < COOP_FRAGMENT_DATA_SIZE ? length - done : COOP_FRAGMENT_DATA_SIZE;
}

bool8 CoopMailbox_SendMessage(u8 type, const void *data, u16 len)
{
    if (sOutMessage.active || !CoopMailbox_IsLuaConnected() || !IsMessageType(type)
     || len == 0 || len > COOP_MAILBOX_MESSAGE_MAX)
        return FALSE;
    sOutMessage.type = type;
    sOutMessage.nextIndex = 0;
    sOutMessage.length = len;
    sOutMessage.done = 0;
    memcpy(sOutMessage.data, data, len);
    sOutMessage.active = TRUE;
    CoopMailbox_PumpOutbound();
    return TRUE;
}

void CoopMailbox_PumpOutbound(void)
{
    u8 fragment[COOP_MAILBOX_PAYLOAD_SIZE];
    u16 size;

    if (!sOutMessage.active)
        return;
    if (!CoopMailbox_IsLuaConnected())
    {
        // Nobody reads it. Lua drops the fragments it already has as soon
        // as the next message starts with fragment 0.
        sOutMessage.active = FALSE;
        return;
    }
    // At most one ring's worth: Push fails as soon as the ring is full.
    while (sOutMessage.done < sOutMessage.length)
    {
        size = FragmentSize(sOutMessage.length, sOutMessage.done);
        fragment[COOP_FRAGMENT_TYPE] = sOutMessage.type;
        fragment[COOP_FRAGMENT_INDEX] = sOutMessage.nextIndex;
        fragment[COOP_FRAGMENT_TOTAL_LENGTH] = sOutMessage.length & 0xFF;
        fragment[COOP_FRAGMENT_TOTAL_LENGTH + 1] = sOutMessage.length >> 8;
        memcpy(&fragment[COOP_FRAGMENT_HEADER_SIZE], &sOutMessage.data[sOutMessage.done], size);
        if (!CoopMailbox_Push(COOP_MSG_FRAGMENT, fragment, COOP_FRAGMENT_HEADER_SIZE + size))
            return; // Ring full: the same fragment is retried next frame.
        sOutMessage.done += size;
        sOutMessage.nextIndex++;
    }
    sOutMessage.active = FALSE;
}

bool8 CoopMailbox_IsSending(void)
{
    return sOutMessage.active;
}

// Adds one inbound fragment. Returns TRUE when it completes sInMessage. Any
// fragment that doesn't continue the message in progress drops it; a first
// fragment (index 0) always starts a new one.
static bool8 AddFragment(const struct CoopMailboxSlot *slot)
{
    const u8 *payload = slot->payload;
    u8 type, index;
    u16 length, size;

    if (slot->length <= COOP_FRAGMENT_HEADER_SIZE)
    {
        sInMessage.active = FALSE;
        return FALSE;
    }
    type = payload[COOP_FRAGMENT_TYPE];
    index = payload[COOP_FRAGMENT_INDEX];
    length = payload[COOP_FRAGMENT_TOTAL_LENGTH] | (payload[COOP_FRAGMENT_TOTAL_LENGTH + 1] << 8);
    size = slot->length - COOP_FRAGMENT_HEADER_SIZE;

    if (index == 0)
    {
        sInMessage.active = FALSE;
        if (!IsMessageType(type) || length == 0 || length > COOP_MAILBOX_MESSAGE_MAX)
            return FALSE;
        sInMessage.type = type;
        sInMessage.length = length;
        sInMessage.nextIndex = 0;
        sInMessage.done = 0;
        sInMessage.active = TRUE;
    }
    else if (!sInMessage.active || type != sInMessage.type || length != sInMessage.length
          || index != sInMessage.nextIndex)
    {
        sInMessage.active = FALSE;
        return FALSE;
    }

    // Every fragment but the last is full, so its size is known in advance.
    if (size != FragmentSize(sInMessage.length, sInMessage.done))
    {
        sInMessage.active = FALSE;
        return FALSE;
    }
    memcpy(&sInMessage.data[sInMessage.done], &payload[COOP_FRAGMENT_HEADER_SIZE], size);
    sInMessage.done += size;
    sInMessage.nextIndex++;
    if (sInMessage.done < sInMessage.length)
        return FALSE;
    sInMessage.active = FALSE;
    return TRUE;
}

bool8 CoopMailbox_Receive(struct CoopMessage *msg)
{
    u8 count;

    for (count = 0; count < COOP_MAILBOX_SLOT_COUNT; count++)
    {
        if (!CoopMailbox_Pop(&sInSlot))
            return FALSE;
        if (sInSlot.type != COOP_MSG_FRAGMENT)
        {
            msg->type = sInSlot.type;
            msg->length = sInSlot.length;
            msg->data = sInSlot.payload;
            return TRUE;
        }
        if (AddFragment(&sInSlot))
        {
            msg->type = sInMessage.type;
            msg->length = sInMessage.length;
            msg->data = sInMessage.data;
            return TRUE;
        }
    }
    return FALSE;
}

void CoopMailbox_DiscardInbound(void)
{
    u8 count;

    // The ring holds at most COOP_MAILBOX_SLOT_COUNT - 1 slots; the cap only
    // bounds the loop if Lua keeps writing while it runs.
    for (count = 0; count < COOP_MAILBOX_SLOT_COUNT && CoopMailbox_Pop(&sInSlot); count++)
        ;
    sInMessage.active = FALSE;
}
