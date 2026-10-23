#ifndef GUARD_COOP_REMOTE_PLAYERS_H
#define GUARD_COOP_REMOTE_PLAYERS_H

// Remote players drawn as overworld object events. Lua writes COOP_MSG_PRESENCE
// and COOP_MSG_LEAVE messages into the inbound mailbox ring, each naming a
// remote slot; every frame this module applies them and then reconciles
// gObjectEvents with the wanted state. It keeps no "spawned" flag: the object
// for slot n is found by its local ID (COOP_LOCALID_FIRST + n) each time.
// Design: docs/remote-player-design.md in the Emerald Co-Op repository.

// Local IDs reserved for remote players. Map templates use
// 1..OBJECT_EVENT_TEMPLATES_COUNT (64); the special IDs are 127 (camera),
// 236-240 (Berry Blender) and 255 (player). remote_players.c checks this.
#define COOP_REMOTE_SLOTS   4
#define COOP_LOCALID_FIRST  228
#define COOP_LOCALID_LAST   (COOP_LOCALID_FIRST + COOP_REMOTE_SLOTS - 1)

struct ObjectEvent;

// TRUE for an active object event that shows a remote player. The vanilla
// collision and interaction lookups skip these objects.
bool8 CoopRemote_IsRemoteObjectEvent(const struct ObjectEvent *objectEvent);

// Drains the inbound ring, then spawns, moves or removes every remote object.
// Call only while gMain.callback2 == CB2_Overworld.
void CoopRemote_Update(void);

// Removes every remote object and forgets every slot (Lua disconnected).
void CoopRemote_DespawnAll(void);

#endif // GUARD_COOP_REMOTE_PLAYERS_H
