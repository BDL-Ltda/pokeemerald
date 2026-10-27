#ifndef GUARD_COOP_MAILBOX_H
#define GUARD_COOP_MAILBOX_H

#include "constants/coop.h"

// Shared-memory mailbox between the ROM and the Emerald Co-Op Lua client.
// The Lua script finds gCoopMailbox through client/addresses.lua and reads or
// writes it between frames. The byte layout is a binary contract with the Lua
// side, documented in docs/memory-map.md ("Mailbox"); the STATIC_ASSERTs in
// mailbox.c lock it. Any layout change must bump COOP_MAILBOX_LAYOUT_VERSION.
//
// Each ring has one writer and one reader:
//   outbound (ROM -> Lua): ROM writes head, Lua writes tail.
//   inbound  (Lua -> ROM): Lua writes head, ROM writes tail.
// The writer fills slots[head] and then advances head; the reader consumes
// slots[tail] while tail != head and then advances tail. A ring is full when
// (head + 1) % COOP_MAILBOX_SLOT_COUNT == tail, so it holds at most 15 messages;
// a write to a full ring is dropped.
//
// Layout v2 adds multi-slot messages on top of the same rings: a message of up
// to COOP_MAILBOX_MESSAGE_MAX bytes travels as consecutive COOP_MSG_FRAGMENT
// slots, each with a fragment header (message type, fragment index, total
// length) and up to COOP_FRAGMENT_DATA_SIZE data bytes. Single-slot messages
// may sit between the fragments. The sender streams: it pushes fragments while
// the ring has room and continues on later frames, so a message can be larger
// than the ring. A fragment that doesn't continue the message in progress, and
// a new first fragment, drop the incomplete message.

#define COOP_MAILBOX_MAGIC          "COOP"
#define COOP_MAILBOX_MAGIC_LENGTH   4
#define COOP_MAILBOX_LAYOUT_VERSION 2
#define COOP_MAILBOX_SLOT_COUNT     16
#define COOP_MAILBOX_PAYLOAD_SIZE   30

// Fragment header, at the start of a COOP_MSG_FRAGMENT payload.
#define COOP_FRAGMENT_TYPE          0x00 // u8: type of the whole message.
#define COOP_FRAGMENT_INDEX         0x01 // u8: 0 for the first fragment.
#define COOP_FRAGMENT_TOTAL_LENGTH  0x02 // u16, little-endian: whole message length.
#define COOP_FRAGMENT_HEADER_SIZE   4
#define COOP_FRAGMENT_DATA_SIZE     (COOP_MAILBOX_PAYLOAD_SIZE - COOP_FRAGMENT_HEADER_SIZE)
// Largest multi-slot message, in bytes (40 fragments).
#define COOP_MAILBOX_MESSAGE_MAX    1024

// Bits of CoopMailbox.flags.
#define COOP_MAILBOX_FLAG_LUA_CONNECTED (1 << 0) // Set and cleared by Lua.
#define COOP_MAILBOX_FLAG_ROM_READY     (1 << 1) // Set by the ROM in CoopMailbox_Init.

// The structs are packed: the modern build's -mabi=apcs-gnu would otherwise
// align every struct to 4 bytes and insert padding before each slots array.
// Packed structs have 1-byte alignment, so copy slots with memcpy or by field.

struct __attribute__((packed)) CoopMailboxSlot
{
    /*0x00*/ u8 type; // COOP_MSG_* from constants/coop.h
    /*0x01*/ u8 length; // Number of valid payload bytes, 0 to COOP_MAILBOX_PAYLOAD_SIZE.
    /*0x02*/ u8 payload[COOP_MAILBOX_PAYLOAD_SIZE];
}; /*size = 0x20*/

struct __attribute__((packed)) CoopMailboxRing
{
    /*0x00*/ u8 head; // Next slot the writer fills.
    /*0x01*/ u8 tail; // Next slot the reader consumes.
    /*0x02*/ struct CoopMailboxSlot slots[COOP_MAILBOX_SLOT_COUNT];
}; /*size = 0x202*/

struct __attribute__((packed)) CoopMailbox
{
    /*0x000*/ char magic[COOP_MAILBOX_MAGIC_LENGTH]; // "COOP", no terminator.
    /*0x004*/ u8 layoutVersion;
    /*0x005*/ u8 flags; // COOP_MAILBOX_FLAG_*
    /*0x006*/ struct CoopMailboxRing outbound; // ROM -> Lua
    /*0x208*/ struct CoopMailboxRing inbound; // Lua -> ROM
}; /*size = 0x40A*/

// A received message: a single slot, or a reassembled multi-slot message.
// data stays valid until the next CoopMailbox_Receive or CoopMailbox_DiscardInbound.
struct CoopMessage
{
    u8 type; // COOP_MSG_*, never COOP_MSG_FRAGMENT
    u16 length;
    const u8 *data;
};

extern struct CoopMailbox gCoopMailbox;

// Clears the mailbox, writes the magic and layout version, and sets
// COOP_MAILBOX_FLAG_ROM_READY. Also forgets any multi-slot message in either
// direction. Called once at boot, before the main loop.
void CoopMailbox_Init(void);

// Queues one outbound message for Lua. Returns FALSE, writing nothing, if the
// ring is full or len exceeds COOP_MAILBOX_PAYLOAD_SIZE. payload may be NULL
// when len is 0.
bool8 CoopMailbox_Push(u8 type, const void *payload, u8 len);

// Takes the oldest inbound message from Lua and copies it to *msg.
// Returns FALSE, leaving *msg untouched, if the inbound ring is empty.
bool8 CoopMailbox_Pop(struct CoopMailboxSlot *msg);

// TRUE while the Lua client reports it is attached to the mailbox.
bool8 CoopMailbox_IsLuaConnected(void);

// Starts sending a multi-slot message of 1 to COOP_MAILBOX_MESSAGE_MAX bytes
// to Lua. The data is copied, and the first fragments are pushed right away.
// Returns FALSE, sending nothing, if another message is still being sent, Lua
// is not connected, type is COOP_MSG_NONE or COOP_MSG_FRAGMENT, or len is out
// of range.
bool8 CoopMailbox_SendMessage(u8 type, const void *data, u16 len);

// Pushes the next fragments of the message being sent while the outbound ring
// has room. Call once per frame. If Lua is no longer connected, the message is
// abandoned; Lua drops the incomplete part.
void CoopMailbox_PumpOutbound(void);

// TRUE while a multi-slot message is still being sent.
bool8 CoopMailbox_IsSending(void);

// Takes the next complete inbound message: a single-slot message, or a
// multi-slot message whose last fragment just arrived. Fragments are collected
// on the way. Returns FALSE when no complete message is waiting (it pops at
// most COOP_MAILBOX_SLOT_COUNT slots per call).
bool8 CoopMailbox_Receive(struct CoopMessage *msg);

// Pops and drops every inbound slot, and any incomplete multi-slot message.
void CoopMailbox_DiscardInbound(void);

#endif // GUARD_COOP_MAILBOX_H
