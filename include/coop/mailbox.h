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

#define COOP_MAILBOX_MAGIC          "COOP"
#define COOP_MAILBOX_MAGIC_LENGTH   4
#define COOP_MAILBOX_LAYOUT_VERSION 1
#define COOP_MAILBOX_SLOT_COUNT     16
#define COOP_MAILBOX_PAYLOAD_SIZE   30

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

extern struct CoopMailbox gCoopMailbox;

// Clears the mailbox, writes the magic and layout version, and sets
// COOP_MAILBOX_FLAG_ROM_READY. Called once at boot, before the main loop.
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

#endif // GUARD_COOP_MAILBOX_H
