#ifndef GUARD_COOP_LINK_SPIKE_H
#define GUARD_COOP_LINK_SPIKE_H

// Issue #80 throwaway experiment. Not the production #81 backend.
struct CoopMessage;
bool8 CoopLinkSpike_IsActive(void);
u8 CoopLinkSpike_Id(void);
void CoopLinkSpike_Update(void);
void CoopLinkSpike_Start(const struct CoopMessage *msg);
bool8 CoopLinkSpike_Send(const void *data, u16 size, bool8 request);
u8 CoopLinkSpike_Received(void);
void CoopLinkSpike_ResetReceived(u8 mask);
bool8 CoopLinkSpike_Finished(void);
void CoopLinkSpike_Barrier(bool8 close);
void CoopLinkSpike_Open(void);
void CoopLinkSpike_ReturnToField(void);
void CoopLinkSpike_Trace(u8 stage, u16 value);

#endif
