#ifndef GUARD_COOP_OVERWORLD_H
#define GUARD_COOP_OVERWORLD_H

// Called after input processing on every CB1_Overworld call, including the
// locked frame before a battle, menu or warp switches the field callback.
void CoopOverworld_Update(void);

#endif // GUARD_COOP_OVERWORLD_H
