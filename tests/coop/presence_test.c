#include "global.h"
#include "coop/mailbox.h"
#include "coop/overworld.h"
#include "coop/presence.h"
#include "fieldmap.h"
#include "constants/event_objects.h"
#include <assert.h>
#include <stdio.h>

struct Main gMain;
struct ObjectEvent gObjectEvents[OBJECT_EVENTS_COUNT];
struct PlayerAvatar gPlayerAvatar;
static struct SaveBlock1 sSave1;
static struct SaveBlock2 sSave2;
struct SaveBlock1 *gSaveBlock1Ptr = &sSave1;
struct SaveBlock2 *gSaveBlock2Ptr = &sSave2;
static bool8 sLocked;
static u8 sFacing;
static unsigned sReads;
static unsigned sCases;
static unsigned sRemoteCalls;

void CB1_Overworld(void) {}
void CB2_Overworld(void) {}
static void OtherCallback(void) {}
// This suite tests the publisher and shared hook, not the #25 renderer.
void CoopRemote_Update(void) { sRemoteCalls++; }
bool8 ArePlayerFieldControlsLocked(void) { return sLocked; }
u8 GetPlayerFacingDirection(void) { sReads++; return sFacing; }

static void Setup(void)
{
    CoopMailbox_Init();
    CoopPresence_Reset();
    memset(gObjectEvents, 0, sizeof(gObjectEvents));
    memset(&sSave1, 0, sizeof(sSave1));
    memset(&sSave2, 0, sizeof(sSave2));
    gSaveBlock1Ptr = &sSave1;
    gSaveBlock2Ptr = &sSave2;
    gMain.callback1 = CB1_Overworld;
    gMain.callback2 = CB2_Overworld;
    gPlayerAvatar.objectEventId = 0;
    gObjectEvents[0].active = TRUE;
    gObjectEvents[0].localId = LOCALID_PLAYER;
    gObjectEvents[0].currentCoords.x = 7 + MAP_OFFSET;
    gObjectEvents[0].currentCoords.y = -1 + MAP_OFFSET;
    sSave1.location.mapNum = 9;
    sSave2.playerGender = FEMALE;
    memcpy(sSave2.playerTrainerId, "\xef\xbe\xad\xde", TRAINER_ID_LENGTH);
    sFacing = DIR_EAST;
    sLocked = FALSE;
    sReads = 0;
    sRemoteCalls = 0;
}

static bool8 Empty(void)
{
    return gCoopMailbox.outbound.head == gCoopMailbox.outbound.tail;
}

static struct CoopPresence Take(void)
{
    struct CoopMailboxRing *ring = &gCoopMailbox.outbound;
    struct CoopMailboxSlot *slot = &ring->slots[ring->tail];
    struct CoopPresence presence;
    unsigned i;
    assert(!Empty());
    assert(slot->type == COOP_MSG_PRESENCE);
    assert(slot->length == sizeof(presence));
    memcpy(&presence, slot->payload, sizeof(presence));
    for (i = sizeof(presence); i < sizeof(slot->payload); i++)
        assert(slot->payload[i] == 0);
    ring->tail = (ring->tail + 1) % COOP_MAILBOX_SLOT_COUNT;
    return presence;
}

static void TestReadinessAndTitle(void)
{
    Setup();
    memset(&gCoopMailbox, 0, sizeof(gCoopMailbox));
    CoopOverworld_Update();
    assert(Empty() && sReads == 0);
    CoopMailbox_Init();
    gMain.callback1 = OtherCallback;
    CoopOverworld_Update();
    assert(Empty() && sReads == 0);
    gMain.callback1 = CB1_Overworld;
    sLocked = TRUE;
    CoopOverworld_Update();
    assert(Empty() && sReads == 0); // No snapshot during the new-game intro.
    sCases++;
}

static void TestPayloadAndIdle(void)
{
    static const u8 expected[] = {0, 9, 7, 0, 255, 255, 4, 1, 1, 239, 190, 173, 222};
    struct CoopPresence presence;
    unsigned i;
    Setup();
    CoopOverworld_Update();
    assert(memcmp(gCoopMailbox.outbound.slots[0].payload, expected, sizeof(expected)) == 0);
    presence = Take();
    assert(presence.x == 7 && presence.y == -1 && presence.trainerId == 0xDEADBEEF);
    // Same bytes as the Lua codec's existing little-endian fixture.
    for (i = 0; i < 3600; i++)
        CoopOverworld_Update();
    assert(Empty());
    sCases++;
}

static void TestAllFields(void)
{
    Setup();
    CoopOverworld_Update(); Take();
    gObjectEvents[0].currentCoords.x++;
    CoopOverworld_Update(); assert(Take().x == 8);
    gObjectEvents[0].currentCoords.y++;
    CoopOverworld_Update(); assert(Take().y == 0);
    sFacing = DIR_NORTH;
    CoopOverworld_Update(); assert(Take().facing == DIR_NORTH);
    sSave1.location.mapGroup = 1;
    CoopOverworld_Update(); assert(Take().mapGroup == 1);
    sSave1.location.mapNum = 2;
    CoopOverworld_Update(); assert(Take().mapNum == 2);
    sSave2.playerGender = MALE;
    CoopOverworld_Update(); assert(Take().gender == MALE);
    memset(sSave2.playerTrainerId, 255, TRAINER_ID_LENGTH);
    CoopOverworld_Update(); assert(Take().trainerId == UINT32_MAX);
    assert(Empty());
    sCases++;
}

static void TestLocksAndTransitions(void)
{
    unsigned i;
    for (i = 0; i < 4; i++) // Menu, dialogue/cutscene, warp fade and battle.
    {
        struct CoopPresence hidden, returned;
        Setup();
        CoopOverworld_Update(); Take();
        sLocked = TRUE;
        if (i == 3) gMain.callback2 = OtherCallback; // Battle transition.
        sReads = 0;
        // Invalid pointers catch any attempt to sample during a transition.
        gSaveBlock1Ptr = NULL;
        gSaveBlock2Ptr = NULL;
        gPlayerAvatar.objectEventId = OBJECT_EVENTS_COUNT;
        CoopOverworld_Update();
        hidden = Take();
        assert(!hidden.visible && hidden.x == 7 && hidden.y == -1);
        assert(hidden.mapGroup == 0 && hidden.mapNum == 9 && sReads == 0);
        for (unsigned frame = 0; frame < 300; frame++) CoopOverworld_Update();
        assert(Empty());
        gMain.callback1 = NULL; // Full load/menu/battle: no field hook.
        CoopOverworld_Update();
        assert(Empty());
        gMain.callback1 = CB1_Overworld;
        gMain.callback2 = CB2_Overworld;
        gSaveBlock1Ptr = &sSave1;
        gSaveBlock2Ptr = &sSave2;
        gPlayerAvatar.objectEventId = 0;
        sSave1.location.mapNum = 16;
        gObjectEvents[0].currentCoords.x = 10 + MAP_OFFSET;
        sLocked = FALSE;
        CoopOverworld_Update();
        returned = Take();
        assert(returned.visible && returned.mapNum == 16 && returned.x == 10);
        assert(Empty());
    }
    sCases++;
}

static void TestFullRingRetriesLatest(void)
{
    struct CoopPresence presence;
    Setup();
    CoopOverworld_Update(); Take();
    for (unsigned i = 0; i < 15; i++)
        assert(CoopMailbox_Push(COOP_MSG_NONE, NULL, 0));
    gObjectEvents[0].currentCoords.x++;
    CoopOverworld_Update(); // Rejected, must not advance the snapshot.
    gObjectEvents[0].currentCoords.x++;
    CoopOverworld_Update(); // Coalesce to the latest tile, not a FIFO backlog.
    gCoopMailbox.outbound.tail = gCoopMailbox.outbound.head;
    CoopOverworld_Update();
    assert(Take().x == 9);
    assert(Empty());
    for (unsigned i = 0; i < 15; i++)
        assert(CoopMailbox_Push(COOP_MSG_NONE, NULL, 0));
    sLocked = TRUE;
    CoopOverworld_Update(); // Hidden transition must also be retried.
    gCoopMailbox.outbound.tail = gCoopMailbox.outbound.head;
    CoopOverworld_Update();
    presence = Take();
    assert(!presence.visible && presence.x == 9);
    CoopOverworld_Update();
    assert(Empty());
    sCases++;
}

static void TestInvalidPlayerAndReset(void)
{
    Setup();
    gPlayerAvatar.objectEventId = OBJECT_EVENTS_COUNT;
    CoopOverworld_Update(); assert(Empty());
    gPlayerAvatar.objectEventId = 0;
    gObjectEvents[0].active = FALSE;
    CoopOverworld_Update(); assert(Empty());
    gObjectEvents[0].active = TRUE;
    gObjectEvents[0].localId = 1;
    CoopOverworld_Update(); assert(Empty());
    gObjectEvents[0].localId = LOCALID_PLAYER;
    sSave1.location.mapNum = -1;
    CoopOverworld_Update(); assert(Empty());
    sSave1.location.mapNum = 9;
    sFacing = DIR_NONE;
    CoopOverworld_Update(); assert(Empty());
    sFacing = DIR_EAST;
    CoopOverworld_Update(); Take();
    CoopPresence_Reset();
    CoopOverworld_Update(); Take(); // Even an identical fresh session publishes.
    gCoopMailbox.flags = 0;
    CoopOverworld_Update(); assert(Empty());
    CoopMailbox_Init();
    sLocked = TRUE;
    CoopOverworld_Update(); assert(Empty()); // No stale hidden tuple after reset.
    sLocked = FALSE;
    CoopOverworld_Update(); assert(Take().visible);
    sCases++;
}

static void TestSharedHook(void)
{
    Setup();
    CoopOverworld_Update();
    assert(Take().visible && sRemoteCalls == 1);
    gMain.callback2 = OtherCallback;
    CoopOverworld_Update();
    assert(!Take().visible && sRemoteCalls == 1);
    // Outbound still publishes the hide; rendering is not called during
    // battle transitions/full-screen menus. Both paths resume in the field.
    gMain.callback2 = CB2_Overworld;
    CoopOverworld_Update();
    assert(Take().visible && sRemoteCalls == 2);
    sCases++;
}

int main(void)
{
    TestReadinessAndTitle();
    TestPayloadAndIdle();
    TestAllFields();
    TestLocksAndTransitions();
    TestFullRingRetriesLatest();
    TestInvalidPlayerAndReset();
    TestSharedHook();
    printf("PASS coop presence: %u test groups (ASan/UBSan)\n", sCases);
    return 0;
}
