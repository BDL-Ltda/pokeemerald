#ifndef GUARD_TRADE_H
#define GUARD_TRADE_H

#include "link_rfu.h"
#include "constants/trade.h"

extern struct Mail gTradeMail[PARTY_SIZE];
extern u8 gSelectedTradeMonPositions[2];

extern const u16 gTradePlatform_Tilemap[];
extern const struct WindowTemplate gTradeEvolutionSceneYesNoWindowTemplate;

s32 GetGameProgressForLinkTrade(void);
void CB2_StartCreateTradeMenu(void);
void CB2_LinkTrade(void);
int CanRegisterMonForTradingBoard(struct RfuGameCompatibilityData player, u16 species2, u16 species, bool8 isModernFatefulEncounter);
int GetUnionRoomTradeMessageId(struct RfuGameCompatibilityData player, struct RfuGameCompatibilityData partner, u16 playerSpecies2, u16 partnerSpecies, u8 requestedType, u16 playerSpecies, bool8 isModernFatefulEncounter);
int CanSpinTradeMon(struct Pokemon *mon, u16 monIdx);
void InitTradeSequenceBgGpuRegs(void);
void LinkTradeDrawWindow(void);
void LoadTradeAnimGfx(void);
void DrawTextOnTradeWindow(u8 windowId, const u8 *str, u8 speed);

// Emerald Co-Op virtual link (#81): leaves the cable trade screen at once,
// freeing it, and returns through CoopLink_ReturnToField. FALSE when the
// current screen can't be left yet (summary, evolution, or after the first
// save barrier); try again on a later frame.
bool8 TryLeaveCoopLinkTrade(void);

#endif //GUARD_TRADE_H
