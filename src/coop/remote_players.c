#include "global.h"
#include "event_object_movement.h"
#include "fieldmap.h"
#include "link.h"
#include "script.h"
#include "sprite.h"
#include "coop/mailbox.h"
#include "coop/remote_players.h"
#include "constants/coop.h"
#include "constants/event_object_movement.h"
#include "constants/event_objects.h"

#if COOP_LOCALID_FIRST <= OBJECT_EVENT_TEMPLATES_COUNT || COOP_LOCALID_FIRST <= LOCALID_CAMERA
#error "Co-op local IDs overlap map template or camera local IDs"
#endif
#if COOP_LOCALID_LAST > LOCALID_BERRY_BLENDER_PLAYER_END - MAX_RFU_PLAYERS
#error "Co-op local IDs overlap Berry Blender local IDs"
#endif
#if COOP_LOCALID_LAST >= LOCALID_PLAYER
#error "Co-op local IDs overlap the player local ID"
#endif

// A remote player is spawned only if this many object event slots stay free
// afterwards, so map NPCs and scripts keep room on busy maps.
#define RESERVED_OBJECT_EVENTS 2

// Presence payload, docs/memory-map.md ("Presence payload"). Little-endian and
// unaligned inside the slot, so it is read byte by byte.
#define PRESENCE_MAP_GROUP          0x00
#define PRESENCE_MAP_NUM            0x01
#define PRESENCE_X                  0x02
#define PRESENCE_Y                  0x04
#define PRESENCE_FACING             0x06
#define PRESENCE_VISIBLE            0x07
#define PRESENCE_GENDER             0x08
#define PRESENCE_SLOT               0x0D
#define PRESENCE_INBOUND_LENGTH     0x0E

// Leave payload: one byte, the slot to free.
#define LEAVE_SLOT                  0x00
#define LEAVE_LENGTH                0x01

struct CoopRemote
{
    bool8 used;
    bool8 visible;
    u8 mapGroup;
    u8 mapNum;
    s16 x; // Map coordinates, without MAP_OFFSET.
    s16 y;
    u8 facing; // DIR_*
    u8 gender; // MALE / FEMALE
};

static EWRAM_DATA struct CoopRemote sRemotes[COOP_REMOTE_SLOTS] = {0};

bool8 CoopRemote_IsRemoteObjectEvent(const struct ObjectEvent *objectEvent)
{
    return objectEvent->active
        && objectEvent->localId >= COOP_LOCALID_FIRST
        && objectEvent->localId <= COOP_LOCALID_LAST;
}

static s16 ReadS16(const u8 *bytes)
{
    return (s16)(bytes[0] | (bytes[1] << 8));
}

static void ApplyPresence(const struct CoopMailboxSlot *msg)
{
    struct CoopRemote *remote;
    u8 facing;

    if (msg->length < PRESENCE_INBOUND_LENGTH || msg->payload[PRESENCE_SLOT] >= COOP_REMOTE_SLOTS)
        return;

    remote = &sRemotes[msg->payload[PRESENCE_SLOT]];
    remote->used = TRUE;
    remote->mapGroup = msg->payload[PRESENCE_MAP_GROUP];
    remote->mapNum = msg->payload[PRESENCE_MAP_NUM];
    remote->x = ReadS16(&msg->payload[PRESENCE_X]);
    remote->y = ReadS16(&msg->payload[PRESENCE_Y]);
    facing = msg->payload[PRESENCE_FACING];
    remote->facing = (facing >= DIR_SOUTH && facing <= DIR_EAST) ? facing : DIR_SOUTH;
    remote->visible = msg->payload[PRESENCE_VISIBLE] != 0;
    remote->gender = msg->payload[PRESENCE_GENDER] == FEMALE ? FEMALE : MALE;
}

static void ApplyMessage(const struct CoopMailboxSlot *msg)
{
    switch (msg->type)
    {
    case COOP_MSG_PRESENCE:
        ApplyPresence(msg);
        break;
    case COOP_MSG_LEAVE:
        if (msg->length >= LEAVE_LENGTH && msg->payload[LEAVE_SLOT] < COOP_REMOTE_SLOTS)
            memset(&sRemotes[msg->payload[LEAVE_SLOT]], 0, sizeof(struct CoopRemote));
        break;
    }
}

static bool8 IsWanted(u8 slot)
{
    const struct CoopRemote *remote = &sRemotes[slot];

    return remote->used && remote->visible
        && remote->mapGroup == gSaveBlock1Ptr->location.mapGroup
        && remote->mapNum == gSaveBlock1Ptr->location.mapNum;
}

// Searches by local ID only: after a seamless map crossing an object keeps the
// old map's mapNum/mapGroup, so the game's map-aware lookups would miss it.
static u8 FindObjectEvent(u8 slot)
{
    u8 i;

    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
    {
        if (gObjectEvents[i].active && gObjectEvents[i].localId == COOP_LOCALID_FIRST + slot)
            return i;
    }
    return OBJECT_EVENTS_COUNT;
}

// The player graphics for the local player's gender, the rival graphics for the
// other one: Brendan and May share PALSLOT_PLAYER, so spawning the opposite
// player graphics would repaint the local player.
static u8 GetGraphicsId(u8 gender)
{
    bool8 sameGender = (gender == gSaveBlock2Ptr->playerGender);

    if (gender == FEMALE)
        return sameGender ? OBJ_EVENT_GFX_MAY_NORMAL : OBJ_EVENT_GFX_RIVAL_MAY_NORMAL;
    return sameGender ? OBJ_EVENT_GFX_BRENDAN_NORMAL : OBJ_EVENT_GFX_RIVAL_BRENDAN_NORMAL;
}

// Same rectangle as RemoveObjectEventIfOutsideView (MAP_OFFSET-adjusted
// coordinates), so a remote player at the edge isn't spawned and culled in turn.
static bool8 IsTileInView(s16 x, s16 y)
{
    return x >= gSaveBlock1Ptr->pos.x - 2 && x <= gSaveBlock1Ptr->pos.x + 17
        && y >= gSaveBlock1Ptr->pos.y && y <= gSaveBlock1Ptr->pos.y + 16;
}

static u8 CountFreeObjectEvents(void)
{
    u8 i, count = 0;

    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
    {
        if (!gObjectEvents[i].active)
            count++;
    }
    return count;
}

// Same as the static RemoveObjectEvent in event_object_movement.c. Never use
// RemoveObjectEventByLocalIdAndMap here: it sets the template's hide flag, and a
// remote player has no template (NULL dereference).
static void Despawn(struct ObjectEvent *objectEvent)
{
    struct SpriteFrameImage image;

    objectEvent->active = FALSE;
    image.size = GetObjectEventGraphicsInfo(objectEvent->graphicsId)->size;
    gSprites[objectEvent->spriteId].images = &image;
    DestroySprite(&gSprites[objectEvent->spriteId]);
}

static void TrySpawn(u8 slot)
{
    const struct CoopRemote *remote = &sRemotes[slot];
    s16 x = remote->x + MAP_OFFSET;
    s16 y = remote->y + MAP_OFFSET;
    u8 objectEventId;

    // New remote players wait while a script runs, and give way to NPCs when
    // object slots run out. If the sprite can't be created either, the spawn
    // returns OBJECT_EVENTS_COUNT and this runs again next frame.
    if (ArePlayerFieldControlsLocked()
     || !IsTileInView(x, y)
     || CountFreeObjectEvents() <= RESERVED_OBJECT_EVENTS)
        return;

    objectEventId = SpawnSpecialObjectEventParameterized(GetGraphicsId(remote->gender), MOVEMENT_TYPE_NONE,
                                                         COOP_LOCALID_FIRST + slot, x, y, ELEVATION_DEFAULT);
    if (objectEventId != OBJECT_EVENTS_COUNT)
        ObjectEventTurn(&gObjectEvents[objectEventId], remote->facing);
}

static void StepToward(u8 slot, struct ObjectEvent *objectEvent)
{
    const struct CoopRemote *remote = &sRemotes[slot];
    s16 x = remote->x + MAP_OFFSET;
    s16 y = remote->y + MAP_OFFSET;
    s16 dx = x - objectEvent->currentCoords.x;
    s16 dy = y - objectEvent->currentCoords.y;

    // While a script, battle setup or the start menu holds the field, start no
    // step, turn or teleport. A step already in progress finishes on its own:
    // FreezeObjectEvent skips objects with heldMovementActive, and clearing the
    // held movement now would stop the sprite between two tiles while
    // currentCoords already names the target. Catch up once the lock ends.
    if (ArePlayerFieldControlsLocked())
        return;

    ObjectEventClearHeldMovementIfFinished(objectEvent);
    if (ObjectEventIsMovementOverridden(objectEvent))
        return; // Still walking.

    if (dx == 0 && dy == 0)
    {
        if (objectEvent->facingDirection != remote->facing)
            ObjectEventTurn(objectEvent, remote->facing);
    }
    else if ((dx == 1 || dx == -1) && dy == 0)
    {
        ObjectEventSetHeldMovement(objectEvent, GetWalkNormalMovementAction(dx > 0 ? DIR_EAST : DIR_WEST));
    }
    else if ((dy == 1 || dy == -1) && dx == 0)
    {
        ObjectEventSetHeldMovement(objectEvent, GetWalkNormalMovementAction(dy > 0 ? DIR_SOUTH : DIR_NORTH));
    }
    else if (IsTileInView(x, y))
    {
        MoveObjectEventToMapCoords(objectEvent, x, y);
        ObjectEventTurn(objectEvent, remote->facing);
    }
    else
    {
        // Comes back through TrySpawn once the tile is in view.
        Despawn(objectEvent);
    }
}

void CoopRemote_DespawnAll(void)
{
    u8 i;

    // Every object with a co-op local ID, including ones restored from a save.
    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
    {
        if (CoopRemote_IsRemoteObjectEvent(&gObjectEvents[i]))
            Despawn(&gObjectEvents[i]);
    }
    memset(sRemotes, 0, sizeof(sRemotes));
}

void CoopRemote_Update(void)
{
    struct CoopMailboxSlot msg;
    u8 slot, objectEventId, count;

    if (!CoopMailbox_IsLuaConnected())
    {
        CoopRemote_DespawnAll();
        return;
    }

    // The ring holds at most COOP_MAILBOX_SLOT_COUNT - 1 messages; the cap only
    // bounds the loop if Lua keeps writing while it runs.
    for (count = 0; count < COOP_MAILBOX_SLOT_COUNT && CoopMailbox_Pop(&msg); count++)
        ApplyMessage(&msg);

    for (slot = 0; slot < COOP_REMOTE_SLOTS; slot++)
    {
        objectEventId = FindObjectEvent(slot);
        if (!IsWanted(slot))
        {
            // Also removes objects restored by Continue from a saved session.
            if (objectEventId != OBJECT_EVENTS_COUNT)
                Despawn(&gObjectEvents[objectEventId]);
        }
        else if (objectEventId == OBJECT_EVENTS_COUNT)
        {
            TrySpawn(slot);
        }
        else if (gObjectEvents[objectEventId].graphicsId != GetGraphicsId(sRemotes[slot].gender))
        {
            // Another player took the slot, or the object came from a save:
            // respawn it with the right graphics.
            Despawn(&gObjectEvents[objectEventId]);
        }
        else
        {
            StepToward(slot, &gObjectEvents[objectEventId]);
        }
    }
}
