// Execute the real attendant task and decoder against the real mailbox. Only
// the engine's window/task/script APIs are doubled; the ARM build checks ABI.
#include "../../src/coop/trade.c"
#include <assert.h>
#include <stdio.h>

struct Main gMain;
u16 gSpecialVar_Result;
static TaskFunc sTask;
static unsigned sResumed, sRemoved, sDraws;
static u8 sTaskCount, sWindow;
static u8 sPrintedId[33];

u8 CreateTask(TaskFunc fn, u8 priority) { (void)priority; sTask = fn; return 0; }
void DestroyTask(u8 id) { (void)id; sTask = NULL; }
u8 GetTaskCount(void) { return sTaskCount; }
void ScriptContext_Enable(void) { sResumed++; }
struct WindowTemplate CreateWindowTemplate(u8 bg, u8 x, u8 y, u8 w, u8 h, u8 palette, u16 base)
{
    (void)bg; (void)x; (void)y; (void)w; (void)h; (void)palette; (void)base;
    return (struct WindowTemplate){0};
}
u16 AddWindow(const struct WindowTemplate *window) { (void)window; return sWindow; }
void RemoveWindow(u8 window) { (void)window; sRemoved++; }
void PutWindowTilemap(u8 window) { (void)window; }
void FillWindowPixelBuffer(u8 window, u8 fill) { (void)window; (void)fill; }
void CopyWindowToVram(u8 window, u8 mode) { (void)window; (void)mode; sDraws++; }
void SetStandardWindowBorderStyle(u8 window, bool8 copy) { (void)window; (void)copy; }
void ClearStdWindowAndFrameToTransparent(u8 window, bool8 copy) { (void)window; (void)copy; }
void AddTextPrinterParameterized3(u8 window, u8 font, u8 x, u8 y, const u8 *colors, s8 speed, const u8 *text)
{
    u8 i;
    (void)window; (void)font; (void)colors; (void)speed;
    if (x != 16 || y != 24) return;
    for (i = 0; i < sizeof(sPrintedId); i++)
    {
        sPrintedId[i] = text[i];
        if (text[i] == EOS) return;
    }
    assert(0 && "ID text was not terminated");
}

static void Step(u16 keys) { gMain.newKeys = keys; assert(sTask); sTask(0); }
static void Drain(void) { gCoopMailbox.outbound.tail = gCoopMailbox.outbound.head; }
static const struct CoopMailboxSlot *Last(void)
{
    return &gCoopMailbox.outbound.slots[(gCoopMailbox.outbound.head + COOP_MAILBOX_SLOT_COUNT - 1) % COOP_MAILBOX_SLOT_COUNT];
}
static void Setup(void)
{
    memset(&sUi, 0, sizeof(sUi));
    sToken = sResumed = sRemoved = sDraws = sTaskCount = sWindow = 0;
    CoopMailbox_Init();
    gCoopMailbox.flags |= COOP_MAILBOX_FLAG_LUA_CONNECTED;
    CoopTrade_Start();
    assert(gSpecialVar_Result == 1);
    Step(0);
    assert(Last()->type == COOP_MSG_TRADE_COMMAND && Last()->length == 3);
    assert(Last()->payload[0] == COMMAND_OPEN && Last()->payload[1] == 1);
    Drain();
}
static void Snapshot(u8 state, u8 count, const char *id1, const char *id2)
{
    u8 bytes[TRADE_HEADER_SIZE + TRADE_PEERS * TRADE_ID_LENGTH] = {0};
    struct CoopMessage msg = {COOP_MSG_TRADE_STATE, TRADE_HEADER_SIZE + count * TRADE_ID_LENGTH, bytes};
    bytes[0] = sToken & 0xFF;
    bytes[1] = sToken >> 8;
    bytes[2] = state;
    bytes[4] = count;
    if (state != STATE_WAITING) memcpy(bytes + 5, "partner", 7);
    if (id1) memcpy(bytes + TRADE_HEADER_SIZE, id1, strlen(id1));
    if (id2) memcpy(bytes + TRADE_HEADER_SIZE + TRADE_ID_LENGTH, id2, strlen(id2));
    CoopTrade_HandleMessage(&msg);
}

static void TestListAndTarget(void)
{
    Setup();
    Snapshot(STATE_WAITING, 2, "alpha", "zebra");
    Step(DPAD_DOWN);
    assert(sUi.cursor == 1);
    // A list refresh preserves the selected identity, even if its index moves.
    Snapshot(STATE_WAITING, 1, "zebra", NULL);
    assert(sUi.cursor == 0);
    Step(A_BUTTON);
    Step(0);
    assert(Last()->payload[0] == COMMAND_INVITE && Last()->length == 8);
    assert(!memcmp(Last()->payload + 3, "zebra", 5));
    Snapshot(STATE_INVITED, 0, NULL, NULL);
    Step(A_BUTTON); Step(0);
    assert(Last()->payload[0] == COMMAND_ACCEPT);
    Step(B_BUTTON);
    assert(Last()->payload[0] == COMMAND_DECLINE && sUi.active);
    Snapshot(STATE_WAITING, 0, NULL, NULL);
    Step(B_BUTTON);
    assert(Last()->payload[0] == COMMAND_CANCEL && !sUi.active && sResumed == 1);
}

static void TestFullRingCancelAndDisconnect(void)
{
    Setup();
    while (CoopMailbox_Push(COOP_MSG_DEBUG_ECHO, NULL, 0)) ;
    Step(B_BUTTON);
    assert(sUi.active && sUi.closing && !sResumed);
    Drain(); Step(0);
    assert(!sUi.active && sResumed == 1 && sRemoved == 1);
    assert(Last()->payload[0] == COMMAND_CANCEL);
    Setup();
    gCoopMailbox.flags &= ~COOP_MAILBOX_FLAG_LUA_CONNECTED;
    Step(0);
    assert(!sUi.active && sResumed == 1 && gSpecialVar_Result == NOTICE_DISCONNECTED);
}

static void TestUntrustedSnapshotsAndGlyphs(void)
{
    u8 bad[85] = {1, 0, STATE_WAITING, 0, 5};
    struct CoopMessage msg = {COOP_MSG_TRADE_STATE, sizeof(bad), bad};
    unsigned i;
    Setup();
    CoopTrade_HandleMessage(&msg);
    assert(sUi.count == 0);
    bad[4] = 4;
    CoopTrade_HandleMessage(&msg); // Empty IDs are invalid.
    assert(sUi.count == 0);
    bad[4] = 0;
    for (i = 0; i < TRADE_HEADER_SIZE; i++)
    {
        msg.length = i;
        CoopTrade_HandleMessage(&msg);
        assert(sUi.count == 0);
    }
    Snapshot(STATE_WAITING, 1, "ABCDEFGHIJKLMNOP", NULL);
    Step(0);
    assert(sPrintedId[15] == CHAR_A + 15 && sPrintedId[16] == EOS);
    Snapshot(STATE_WAITING, 1, "________________", NULL);
    Step(0);
    for (i = 0; i < 32; i += 2)
        assert(sPrintedId[i] == CHAR_EXTRA_SYMBOL && sPrintedId[i + 1] == CHAR_UNDERSCORE);
    assert(sPrintedId[32] == EOS);
    Snapshot(STATE_WAITING, 1, "bad!", NULL);
    assert(sUi.ids[0][0] == '_');
    sToken++; // Snapshot from a previous attendant visit is ignored.
    msg.length = TRADE_HEADER_SIZE;
    CoopTrade_HandleMessage(&msg);
    assert(sUi.count == 1);
}

static void TestAllocationFailures(void)
{
    Setup(); Step(B_BUTTON);
    sWindow = WINDOW_NONE;
    CoopTrade_Start();
    assert(!sUi.active && gSpecialVar_Result == 0 && !sTask);
    sWindow = 0; sTaskCount = NUM_TASKS;
    CoopTrade_Start();
    assert(!sUi.active && gSpecialVar_Result == 0 && !sTask);
}

int main(void)
{
    TestListAndTarget();
    TestFullRingCancelAndDisconnect();
    TestUntrustedSnapshotsAndGlyphs();
    TestAllocationFailures();
    puts("4 trade task/decoder test groups passed");
    return 0;
}
