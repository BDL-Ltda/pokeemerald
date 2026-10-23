#ifndef GUARD_CONSTANTS_COOP_H
#define GUARD_CONSTANTS_COOP_H

// Emerald Co-Op mailbox message types (the `type` byte of a mailbox slot).
// This file is the source of truth: the Lua client mirrors these values, and
// docs/memory-map.md in the Emerald Co-Op repository documents the mapping.
// Never renumber a type; add new ones at the end. 0 is reserved as invalid.
#define COOP_MSG_NONE     0
#define COOP_MSG_PRESENCE 1
#define COOP_MSG_LEAVE    2 // Inbound only: a remote player left; frees its slot.

#endif // GUARD_CONSTANTS_COOP_H
