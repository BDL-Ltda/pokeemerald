#ifndef GUARD_COOP_SPIKE_H
#define GUARD_COOP_SPIKE_H

// THROWAWAY PROOF for EmeraldCo-Op issue #21. Not production code.
// See docs/remote-player-design.md in the EmeraldCo-Op repository.

#include "constants/global.h"

// Local IDs reserved for remote players. Map templates use 1..OBJECT_EVENT_TEMPLATES_COUNT
// (64); the special IDs are 127 (camera), 236-240 (Berry Blender) and 255 (player).
#define COOP_REMOTE_SLOTS        4
#define COOP_LOCALID_FIRST       228
#define COOP_LOCALID_LAST        (COOP_LOCALID_FIRST + COOP_REMOTE_SLOTS - 1)

#define IsCoopRemoteLocalId(id) ((id) >= COOP_LOCALID_FIRST && (id) <= COOP_LOCALID_LAST)

struct ObjectEvent;

bool8 CoopSpike_IsRemoteObjectEvent(const struct ObjectEvent *objectEvent);
void CoopSpike_Update(void);

#endif // GUARD_COOP_SPIKE_H
