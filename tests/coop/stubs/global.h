#ifndef COOP_TEST_GLOBAL_H
#define COOP_TEST_GLOBAL_H

// Field doubles, not a host replica of the GBA ABI. The normal ARM build
// checks production headers; these tests compile the real co-op C sources.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "constants/global.h"

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef u8 bool8;
#define TRUE 1
#define FALSE 0
#define EWRAM_DATA
#define STATIC_ASSERT(expr, id) _Static_assert(expr, #id);

struct Coords16 { s16 x, y; };
struct ObjectEvent { bool8 active; u8 localId; struct Coords16 currentCoords; };
struct PlayerAvatar { u8 objectEventId; };
struct WarpData { s8 mapGroup, mapNum; };
struct SaveBlock1 { struct WarpData location; };
struct SaveBlock2 { u8 playerGender; u8 playerTrainerId[TRAINER_ID_LENGTH]; };
struct Main { void (*callback1)(void); void (*callback2)(void); };
extern struct Main gMain;
extern struct ObjectEvent gObjectEvents[OBJECT_EVENTS_COUNT];
extern struct PlayerAvatar gPlayerAvatar;
extern struct SaveBlock1 *gSaveBlock1Ptr;
extern struct SaveBlock2 *gSaveBlock2Ptr;

#endif
