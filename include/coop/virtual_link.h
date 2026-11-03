#ifndef GUARD_COOP_VIRTUAL_LINK_H
#define GUARD_COOP_VIRTUAL_LINK_H

// Emerald Co-Op virtual link (issue #81): the link-cable calls of the vanilla
// trade screens (trade.c) carried over COOP_MSG_LINK mailbox messages, which
// Lua relays to the trade partner as protocol trade_link messages. It is
// active only during a co-op trade session; outside one every hooked function
// in link.c behaves as vanilla. Packets, message flow, timeout and cancel are
// documented in docs/virtual-link.md in the Emerald Co-Op repository.

// Peer silence, in frames, after which the partner counts as gone: 10 s at the
// GBA's ~60 frames per second. Every packet from the partner restarts the
// count, and each ROM sends a keepalive after COOP_LINK_KEEPALIVE_FRAMES
// without other traffic. Tunable; keep it well above the keepalive interval.
#define COOP_LINK_TIMEOUT_FRAMES   (10 * 60)
#define COOP_LINK_KEEPALIVE_FRAMES 60

// First byte of every COOP_MSG_LINK message: the packet kind. The Lua client
// mirrors these values (client/lib/virtuallink.lua). Never renumber one.
#define COOP_LINK_START     0 // Lua to ROM: role, partner ID length, partner ID (ASCII).
#define COOP_LINK_HELLO     1 // Peer: ack flag, then struct LinkPlayer.
#define COOP_LINK_BLOCK     2 // Peer: 1-256 bytes of a SendBlock.
#define COOP_LINK_REQUEST   3 // Peer, leader to follower: the leader's block; answer with a BLOCK.
#define COOP_LINK_BARRIER   4 // Peer: standby generation.
#define COOP_LINK_CLOSE     5 // Peer: close generation.
#define COOP_LINK_CANCEL    6 // Lua to ROM: reason byte; the partner is gone.
#define COOP_LINK_ABORT     7 // Peer: the sender canceled its session.
#define COOP_LINK_END       8 // ROM to Lua: outcome byte; the session is over.
#define COOP_LINK_KEEPALIVE 9 // Peer: no payload.

// COOP_LINK_END outcomes.
#define COOP_LINK_END_DONE     0 // trade.c finished: saved, or canceled from its menu.
#define COOP_LINK_END_CANCELED 1 // Canceled before the save; nothing changed.
#define COOP_LINK_END_REFUSED  2 // START refused: not in the free overworld, or invalid.

// COOP_LINK_CANCEL reasons. The ROM shows the same message for each.
#define COOP_LINK_CANCEL_LEFT   1 // The partner left the room.
#define COOP_LINK_CANCEL_BROKEN 2 // Lua lost or reordered link data.

// Longest partner ID: protocol playerId rules (1-16 of A-Z a-z 0-9 _ -).
#define COOP_LINK_PARTNER_ID_MAX 16

struct CoopMessage;

// TRUE from an accepted START until the session returns to the field. While
// TRUE the link.c functions below are routed here.
bool8 CoopLink_IsActive(void);

// Handles a COOP_MSG_LINK message outside a session (from CoopRemote_Update):
// START begins a session, anything else is a stale packet and is dropped.
void CoopLink_HandleIdleMessage(const struct CoopMessage *msg);

// Called every frame from the main loop, before the main callbacks, so the
// link keeps running while trade, summary or evolution screens own callback2.
// During a session it owns the inbound ring and passes non-link messages to
// CoopRemote_ApplyMessage.
void CoopLink_Update(void);

// link.c backends, valid while CoopLink_IsActive().
u8 CoopLink_GetMultiplayerId(void);
bool8 CoopLink_SendBlock(const void *data, u16 size, bool8 request);
u8 CoopLink_GetBlockReceivedStatus(void);
void CoopLink_ResetBlockReceivedFlags(u8 mask);
bool8 CoopLink_IsTaskFinished(void);
void CoopLink_SetBarrier(bool8 close);
void CoopLink_Open(void);

// trade.c: called just before the first save barrier. From then on the trade
// can no longer be canceled: a partner lost after this point releases the
// remaining barriers and the local save completes.
void CoopLink_OnSaveStart(void);

// trade.c's exit callback for a co-op session (completed, canceled from the
// menu, or left after a cancel): restores the party on a cancel, ends the
// session and returns to the local field.
void CoopLink_ReturnToField(void);

#endif // GUARD_COOP_VIRTUAL_LINK_H
