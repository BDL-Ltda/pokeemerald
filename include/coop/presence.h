#ifndef GUARD_COOP_PRESENCE_H
#define GUARD_COOP_PRESENCE_H

// Outbound COOP_MSG_PRESENCE payload. Packed little-endian, exactly 13 bytes;
// client/lib/mailbox.lua and docs/memory-map.md describe the same contract.
// Copy a slot into this struct with memcpy; never cast an unaligned slot.
struct __attribute__((packed)) CoopPresence
{
    u8 mapGroup;
    u8 mapNum;
    s16 x;
    s16 y;
    u8 facing;
    u8 visible;
    u8 gender;
    u32 trainerId;
};

void CoopPresence_Update(void);
void CoopPresence_Reset(void);

#endif // GUARD_COOP_PRESENCE_H
