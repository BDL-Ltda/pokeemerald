#ifndef GUARD_COOP_TRADE_H
#define GUARD_COOP_TRADE_H

#include "coop/mailbox.h"

// Script special: the shared Cable Club script checks vanilla eligibility
// before entering. Resumes the script on B or connection loss.
void CoopTrade_Start(void);
// Called by the single mailbox consumer while the overworld is running.
void CoopTrade_HandleMessage(const struct CoopMessage *msg);

#endif // GUARD_COOP_TRADE_H
