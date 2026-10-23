#ifndef GUARD_COOP_OVERWORLD_H
#define GUARD_COOP_OVERWORLD_H

// Entry point of the Emerald Co-Op overworld code. Called from CB1_Overworld on
// every frame, whether or not gMain.callback2 is CB2_Overworld. Each part
// decides when it runs (see docs/remote-player-design.md, section 5).
void CoopOverworld_Update(void);

#endif // GUARD_COOP_OVERWORLD_H
