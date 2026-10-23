#include "global.h"
#include "coop/mailbox.h"
#include "coop/presence.h"
#include "field_player_avatar.h"
#include "fieldmap.h"
#include "main.h"
#include "overworld.h"
#include "script.h"
#include "constants/event_objects.h"

STATIC_ASSERT(sizeof(struct CoopPresence) == 13, CoopPresenceSize)
STATIC_ASSERT(offsetof(struct CoopPresence, mapGroup) == 0, CoopPresenceMapGroupOffset)
STATIC_ASSERT(offsetof(struct CoopPresence, mapNum) == 1, CoopPresenceMapNumOffset)
STATIC_ASSERT(offsetof(struct CoopPresence, x) == 2, CoopPresenceXOffset)
STATIC_ASSERT(offsetof(struct CoopPresence, y) == 4, CoopPresenceYOffset)
STATIC_ASSERT(offsetof(struct CoopPresence, facing) == 6, CoopPresenceFacingOffset)
STATIC_ASSERT(offsetof(struct CoopPresence, visible) == 7, CoopPresenceVisibleOffset)
STATIC_ASSERT(offsetof(struct CoopPresence, gender) == 8, CoopPresenceGenderOffset)
STATIC_ASSERT(offsetof(struct CoopPresence, trainerId) == 9, CoopPresenceTrainerIdOffset)
STATIC_ASSERT(sizeof(struct CoopPresence) <= COOP_MAILBOX_PAYLOAD_SIZE, CoopPresenceFitsSlot)

// RAM is cleared on boot/soft reset, just like the mailbox. Only a successful
// push advances this snapshot: a full ring must not lose the latest change.
static EWRAM_DATA struct CoopPresence sLastPresence = {0};
static EWRAM_DATA bool8 sHasLastPresence = FALSE;

void CoopPresence_Reset(void)
{
    sHasLastPresence = FALSE;
}

static bool8 BuildLocalPresence(struct CoopPresence *presence)
{
    const struct ObjectEvent *player;
    const u8 *trainerId;

    if (gMain.callback2 != CB2_Overworld || ArePlayerFieldControlsLocked())
    {
        // Never sample a loading map or scripted position. Hide the last
        // published tuple once, then keep it stable until controls return.
        if (!sHasLastPresence)
            return FALSE;
        *presence = sLastPresence;
        presence->visible = FALSE;
        return TRUE;
    }

    if (gSaveBlock1Ptr == NULL || gSaveBlock2Ptr == NULL
     || gPlayerAvatar.objectEventId >= OBJECT_EVENTS_COUNT)
        return FALSE;
    player = &gObjectEvents[gPlayerAvatar.objectEventId];
    if (!player->active || player->localId != LOCALID_PLAYER
     || gSaveBlock1Ptr->location.mapGroup < 0 || gSaveBlock1Ptr->location.mapNum < 0)
        return FALSE;

    presence->facing = GetPlayerFacingDirection();
    if (presence->facing < DIR_SOUTH || presence->facing > DIR_EAST)
        return FALSE;
    presence->mapGroup = gSaveBlock1Ptr->location.mapGroup;
    presence->mapNum = gSaveBlock1Ptr->location.mapNum;
    presence->x = player->currentCoords.x - MAP_OFFSET;
    presence->y = player->currentCoords.y - MAP_OFFSET;
    presence->visible = TRUE;
    presence->gender = gSaveBlock2Ptr->playerGender;
    trainerId = gSaveBlock2Ptr->playerTrainerId;
    // The save stores four bytes, not an aligned u32. Keep the full ID and
    // cast before shifting so IDs with bit 31 set have no signed overflow.
    presence->trainerId = (u32)trainerId[0] | ((u32)trainerId[1] << 8)
                       | ((u32)trainerId[2] << 16) | ((u32)trainerId[3] << 24);
    return TRUE;
}

static bool8 PresenceEquals(const struct CoopPresence *a, const struct CoopPresence *b)
{
    // The packed layout has no padding and BuildLocalPresence fills all bytes.
    return memcmp(a, b, sizeof(*a)) == 0;
}

void CoopPresence_Update(void)
{
    struct CoopPresence presence;

    if (!(gCoopMailbox.flags & COOP_MAILBOX_FLAG_ROM_READY))
    {
        CoopPresence_Reset();
        return;
    }
    if (gMain.callback1 != CB1_Overworld || !BuildLocalPresence(&presence))
        return;
    if (sHasLastPresence && PresenceEquals(&presence, &sLastPresence))
        return;
    if (CoopMailbox_Push(COOP_MSG_PRESENCE, &presence, sizeof(presence)))
    {
        sLastPresence = presence;
        sHasLastPresence = TRUE;
    }
}
