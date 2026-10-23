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

// mGBA runs the Lua frame callback at the frame boundary, which can fall in the
// middle of these functions. This compiler barrier keeps the slot accesses on
// the correct side of the head/tail update, so Lua never sees a half-written
// slot and we never read one. No lock is needed: each index has one writer.
#define COOP_MAILBOX_BARRIER() asm volatile("" ::: "memory")

EWRAM_DATA struct CoopMailbox gCoopMailbox = {0};

void CoopMailbox_Init(void)
{
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
