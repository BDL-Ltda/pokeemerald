#ifndef GUARD_CONSTANTS_COOP_H
#define GUARD_CONSTANTS_COOP_H

// Emerald Co-Op mailbox message types (the `type` byte of a mailbox slot).
// This file is the source of truth: the Lua client mirrors these values, and
// docs/memory-map.md in the Emerald Co-Op repository documents the mapping.
// Never renumber a type; add new ones at the end. 0 is reserved as invalid.
#define COOP_MSG_NONE     0
#define COOP_MSG_PRESENCE 1
#define COOP_MSG_LEAVE    2 // Inbound only: a remote player left; frees its slot.
// Mailbox layout v2. Both directions: one slot of a multi-slot message. The
// payload starts with a fragment header naming the whole message's type.
#define COOP_MSG_FRAGMENT 3
// Debug, both directions: the ROM sends a received message of this type back
// to Lua unchanged, as a multi-slot message (client/debug/echo.lua).
#define COOP_MSG_DEBUG_ECHO 4

#endif // GUARD_CONSTANTS_COOP_H
