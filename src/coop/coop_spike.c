// THROWAWAY PROOF for EmeraldCo-Op issue #21. Not production code.
//
// Spawns one remote-player object event (Brendan) at a fixed tile in Littleroot Town and
// paces it one tile east and west, so the walk animation, collision bypass, camera culling
// and map re-entry can be checked in an emulator.

#include "global.h"
#include "event_object_movement.h"
#include "fieldmap.h"
#include "field_player_avatar.h"
#include "coop/coop_spike.h"
#include "constants/event_object_movement.h"
#include "constants/event_objects.h"
#include "constants/maps.h"
#include "link.h"
#include "sprite.h"

#if COOP_LOCALID_FIRST <= OBJECT_EVENT_TEMPLATES_COUNT || COOP_LOCALID_FIRST <= LOCALID_CAMERA
#error "Co-op local IDs overlap map template or camera local IDs"
#endif
#if COOP_LOCALID_LAST > LOCALID_BERRY_BLENDER_PLAYER_END - MAX_RFU_PLAYERS
#error "Co-op local IDs overlap Berry Blender local IDs"
#endif

#define SPIKE_SLOT       0
#define SPIKE_X          10 // map coordinates, without MAP_OFFSET
#define SPIKE_Y          12
#define SPIKE_PACE_TILES 1
#define SPIKE_STEP_DELAY 48 // frames between steps

static EWRAM_DATA u8 sSpikeTimer = 0;
static EWRAM_DATA s8 sSpikeOffset = 0;
static EWRAM_DATA u8 sSpikeDirection = DIR_EAST;

bool8 CoopSpike_IsRemoteObjectEvent(const struct ObjectEvent *objectEvent)
{
    return objectEvent->active && IsCoopRemoteLocalId(objectEvent->localId);
}

// Looks the slot up by local ID only: after a seamless map crossing the object keeps the
// old map's mapNum/mapGroup, so the game's map-aware lookups would miss it.
static u8 FindRemoteObjectEvent(u8 slot)
{
    u8 i;
    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
    {
        if (gObjectEvents[i].active && gObjectEvents[i].localId == COOP_LOCALID_FIRST + slot)
            return i;
    }
    return OBJECT_EVENTS_COUNT;
}

// Same as the static RemoveObjectEvent in event_object_movement.c. Never use
// RemoveObjectEventByLocalIdAndMap here: it sets the template's hide flag, and a
// remote player has no template (NULL dereference).
static void DespawnRemoteObjectEvent(u8 slot)
{
    struct SpriteFrameImage image;
    struct ObjectEvent *objectEvent;
    u8 objectEventId = FindRemoteObjectEvent(slot);

    if (objectEventId == OBJECT_EVENTS_COUNT)
        return;
    objectEvent = &gObjectEvents[objectEventId];
    objectEvent->active = FALSE;
    image.size = GetObjectEventGraphicsInfo(objectEvent->graphicsId)->size;
    gSprites[objectEvent->spriteId].images = &image;
    DestroySprite(&gSprites[objectEvent->spriteId]);
}

static bool8 IsTileInView(s16 x, s16 y)
{
    // Same rectangle RemoveObjectEventIfOutsideView keeps (MAP_OFFSET-adjusted coords).
    return x >= gSaveBlock1Ptr->pos.x - 2 && x <= gSaveBlock1Ptr->pos.x + 17
        && y >= gSaveBlock1Ptr->pos.y && y <= gSaveBlock1Ptr->pos.y + 16;
}

void CoopSpike_Update(void)
{
    u8 objectEventId;
    struct ObjectEvent *objectEvent;
    s16 targetX, targetY;

    if (gSaveBlock1Ptr->location.mapGroup != MAP_GROUP(MAP_LITTLEROOT_TOWN)
     || gSaveBlock1Ptr->location.mapNum != MAP_NUM(MAP_LITTLEROOT_TOWN))
    {
        DespawnRemoteObjectEvent(SPIKE_SLOT);
        return;
    }

    if (++sSpikeTimer >= SPIKE_STEP_DELAY)
    {
        sSpikeTimer = 0;
        if (sSpikeOffset >= SPIKE_PACE_TILES)
            sSpikeDirection = DIR_WEST;
        else if (sSpikeOffset <= -SPIKE_PACE_TILES)
            sSpikeDirection = DIR_EAST;
        sSpikeOffset += (sSpikeDirection == DIR_EAST) ? 1 : -1;
    }

    targetX = SPIKE_X + sSpikeOffset + MAP_OFFSET;
    targetY = SPIKE_Y + MAP_OFFSET;

    objectEventId = FindRemoteObjectEvent(SPIKE_SLOT);
    if (objectEventId == OBJECT_EVENTS_COUNT)
    {
        // Not spawned, or removed by camera culling / a map load: spawn again once in view.
        if (!IsTileInView(targetX, targetY))
            return;
        objectEventId = SpawnSpecialObjectEventParameterized(
            gSaveBlock2Ptr->playerGender == MALE ? OBJ_EVENT_GFX_BRENDAN_NORMAL : OBJ_EVENT_GFX_RIVAL_BRENDAN_NORMAL,
            MOVEMENT_TYPE_NONE, COOP_LOCALID_FIRST + SPIKE_SLOT, targetX, targetY, ELEVATION_DEFAULT);
        if (objectEventId != OBJECT_EVENTS_COUNT)
            ObjectEventTurn(&gObjectEvents[objectEventId], sSpikeDirection);
        return;
    }

    objectEvent = &gObjectEvents[objectEventId];
    ObjectEventClearHeldMovementIfFinished(objectEvent);
    if (ObjectEventIsMovementOverridden(objectEvent))
        return; // still walking

    if (objectEvent->currentCoords.x == targetX && objectEvent->currentCoords.y == targetY)
        return;

    if (objectEvent->currentCoords.y == targetY && (objectEvent->currentCoords.x - targetX == 1 || targetX - objectEvent->currentCoords.x == 1))
    {
        u8 direction = targetX > objectEvent->currentCoords.x ? DIR_EAST : DIR_WEST;
        ObjectEventSetHeldMovement(objectEvent, GetWalkNormalMovementAction(direction));
    }
    else
    {
        MoveObjectEventToMapCoords(objectEvent, targetX, targetY);
    }
}
