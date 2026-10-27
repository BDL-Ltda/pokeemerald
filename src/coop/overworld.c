#include "global.h"
#include "main.h"
#include "overworld.h"
#include "coop/mailbox.h"
#include "coop/overworld.h"
#include "coop/presence.h"
#include "coop/remote_players.h"

void CoopOverworld_Update(void)
{
    // Presence must observe the locked frame even if callback2 has changed.
    CoopPresence_Update();
    // Remote players only while the field is fully running: object events are
    // never half built then (not during battle transitions or full-screen menus).
    if (gMain.callback2 == CB2_Overworld)
        CoopRemote_Update();
    // After presence, so the latest local tuple goes out first. Continues a
    // multi-slot message larger than the free part of the outbound ring.
    CoopMailbox_PumpOutbound();
}
