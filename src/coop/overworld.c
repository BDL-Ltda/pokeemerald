#include "global.h"
#include "coop/overworld.h"
#include "coop/presence.h"

void CoopOverworld_Update(void)
{
    CoopPresence_Update();
    // Remote rendering belongs to #25, guarded by callback2 == CB2_Overworld.
}
