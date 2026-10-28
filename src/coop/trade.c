#include "global.h"
#include "main.h"
#include "event_data.h"
#include "menu.h"
#include "script.h"
#include "string_util.h"
#include "task.h"
#include "window.h"
#include "coop/mailbox.h"
#include "coop/trade.h"
#include "constants/characters.h"
#include "constants/coop.h"

// Wire values are shared with client/lib/trade.lua; see docs/memory-map.md.
#define TRADE_ID_LENGTH 16
#define TRADE_PEERS 4
#define TRADE_HEADER_SIZE 21
#define COMMAND_OPEN 1
#define COMMAND_INVITE 2
#define COMMAND_ACCEPT 3
#define COMMAND_DECLINE 4
#define COMMAND_CANCEL 5
#define STATE_CLOSED 0
#define STATE_WAITING 1
#define STATE_INVITING 2
#define STATE_INVITED 3
#define STATE_READY 4
#define NOTICE_DISCONNECTED 5

static const u8 sText_Waiting[] = _("Waiting for a trade partner...");
static const u8 sText_Inviting[] = _("Waiting for a reply...");
static const u8 sText_Invited[] = _("wants to trade. Accept?");
static const u8 sText_Ready[] = _("Trade partner ready.\nTrading is not available yet.");
static const u8 sText_InviteHelp[] = _("A: Invite    B: Cancel");
static const u8 sText_AcceptHelp[] = _("A: Accept    B: Decline");
static const u8 sText_CancelHelp[] = _("B: Cancel");
static const u8 sText_NoPartners[] = _("No partners waiting.");
static const u8 sText_Cursor[] = _(">");
static const u8 sText_Notices[][32] = {
    _(""), _("Invitation declined."), _("Invitation timed out."),
    _("That player is busy."), _("Invitation canceled."), _("Partner disconnected."),
};

struct CoopTradeUi
{
    bool8 active;
    bool8 dirty;
    bool8 closing;
    u8 windowId;
    u8 state;
    u8 notice;
    u8 count;
    u8 cursor;
    u8 pendingLength;
    u8 pending[3 + TRADE_ID_LENGTH];
    u8 partner[TRADE_ID_LENGTH]; // ASCII, never passed directly to a text printer.
    u8 ids[TRADE_PEERS][TRADE_ID_LENGTH];
};

static EWRAM_DATA struct CoopTradeUi sUi = {0};
static EWRAM_DATA u16 sToken = 0;

static void Task_Trade(u8 taskId);

static bool8 IsId(const u8 *id)
{
    u8 i;
    bool8 ended = FALSE;
    if (id[0] == 0)
        return FALSE;
    for (i = 0; i < TRADE_ID_LENGTH; i++)
    {
        u8 c = id[i];
        if (c == 0)
            ended = TRUE;
        else if (ended || !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
              || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return FALSE;
    }
    return TRUE;
}

static void Print(u8 x, u8 y, const u8 *text)
{
    const u8 colors[] = {TEXT_COLOR_WHITE, TEXT_COLOR_DARK_GRAY, TEXT_COLOR_LIGHT_GRAY};
    AddTextPrinterParameterized3(sUi.windowId, FONT_NORMAL, x, y, colors, TEXT_SKIP_DRAW, text);
}

static void PrintId(u8 x, u8 y, const u8 *id)
{
    u8 text[2 * TRADE_ID_LENGTH + 1];
    u8 i, c, j = 0;
    for (i = 0; i < TRADE_ID_LENGTH && id[i]; i++)
    {
        c = id[i];
        if (c >= 'A' && c <= 'Z') text[j++] = CHAR_A + c - 'A';
        else if (c >= 'a' && c <= 'z') text[j++] = CHAR_a + c - 'a';
        else if (c >= '0' && c <= '9') text[j++] = CHAR_0 + c - '0';
        else if (c == '-') text[j++] = CHAR_HYPHEN;
        else { text[j++] = CHAR_EXTRA_SYMBOL; text[j++] = CHAR_UNDERSCORE; }
    }
    text[j] = EOS;
    Print(x, y, text);
}

static void Draw(void)
{
    u8 i;
    FillWindowPixelBuffer(sUi.windowId, PIXEL_FILL(1));
    if (sUi.state == STATE_WAITING)
    {
        Print(4, 2, sText_Waiting);
        if (!sUi.count) Print(4, 32, sText_NoPartners);
        for (i = 0; i < sUi.count; i++)
        {
            if (sUi.cursor == i) Print(4, 24 + 16 * i, sText_Cursor);
            PrintId(16, 24 + 16 * i, sUi.ids[i]);
        }
        Print(4, 108, sText_InviteHelp);
    }
    else
    {
        PrintId(4, 2, sUi.partner);
        Print(4, 24, sUi.state == STATE_INVITED ? sText_Invited
            : sUi.state == STATE_READY ? sText_Ready : sText_Inviting);
        Print(4, 108, sUi.state == STATE_INVITED ? sText_AcceptHelp : sText_CancelHelp);
    }
    if (sUi.notice < ARRAY_COUNT(sText_Notices)) Print(4, 88, sText_Notices[sUi.notice]);
    CopyWindowToVram(sUi.windowId, COPYWIN_FULL);
    sUi.dirty = FALSE;
}

static void QueueCommand(u8 command, const u8 *id)
{
    u8 length = 0;
    sUi.pending[0] = command;
    sUi.pending[1] = sToken & 0xFF;
    sUi.pending[2] = sToken >> 8;
    if (id)
        while (length < TRADE_ID_LENGTH && id[length])
        {
            sUi.pending[3 + length] = id[length];
            length++;
        }
    sUi.pendingLength = 3 + length;
}

static void Finish(u8 taskId, u16 result)
{
    ClearStdWindowAndFrameToTransparent(sUi.windowId, TRUE);
    RemoveWindow(sUi.windowId);
    sUi.active = FALSE;
    sUi.pendingLength = 0;
    gSpecialVar_Result = result;
    DestroyTask(taskId);
    ScriptContext_Enable();
}

static void Task_Trade(u8 taskId)
{
    u16 keys = gMain.newKeys;
    if (!CoopMailbox_IsLuaConnected() || sUi.state == STATE_CLOSED)
    {
        Finish(taskId, NOTICE_DISCONNECTED);
        return;
    }
    // A cancel is retried until the ring accepts it; never leave an advertised
    // attendant behind just because presence filled the outbound ring.
    if (keys & B_BUTTON)
    {
        if (sUi.state == STATE_INVITED && !sUi.closing)
            QueueCommand(COMMAND_DECLINE, NULL);
        else
        {
            sUi.closing = TRUE;
            QueueCommand(COMMAND_CANCEL, NULL);
        }
    }
    if (sUi.pendingLength)
    {
        if (!CoopMailbox_Push(COOP_MSG_TRADE_COMMAND, sUi.pending, sUi.pendingLength)) return;
        sUi.pendingLength = 0;
        if (sUi.closing) { Finish(taskId, 0); return; }
    }
    if (sUi.dirty) Draw();
    if (keys & B_BUTTON) return;
    if (sUi.state == STATE_WAITING)
    {
        if (sUi.count && (keys & DPAD_UP))
            sUi.cursor = (sUi.cursor + sUi.count - 1) % sUi.count;
        else if (sUi.count && (keys & DPAD_DOWN))
            sUi.cursor = (sUi.cursor + 1) % sUi.count;
        else if (sUi.count && (keys & A_BUTTON))
            QueueCommand(COMMAND_INVITE, sUi.ids[sUi.cursor]);
        if (keys & (DPAD_UP | DPAD_DOWN)) sUi.dirty = TRUE;
    }
    else if (sUi.state == STATE_INVITED && (keys & A_BUTTON))
        QueueCommand(COMMAND_ACCEPT, NULL);
}

void CoopTrade_Start(void)
{
    struct WindowTemplate window = CreateWindowTemplate(0, 1, 1, 27, 16, 15, 100);
    u8 taskId;
    gSpecialVar_Result = 0;
    if (sUi.active || GetTaskCount() >= NUM_TASKS) return;
    memset(&sUi, 0, sizeof(sUi));
    if (++sToken == 0) sToken = 1;
    taskId = CreateTask(Task_Trade, 8);
    sUi.windowId = AddWindow(&window);
    if (sUi.windowId == WINDOW_NONE)
    {
        DestroyTask(taskId);
        return;
    }
    sUi.active = sUi.dirty = TRUE;
    sUi.state = STATE_WAITING;
    PutWindowTilemap(sUi.windowId);
    SetStandardWindowBorderStyle(sUi.windowId, TRUE);
    QueueCommand(COMMAND_OPEN, NULL);
    gSpecialVar_Result = 1;
}

void CoopTrade_HandleMessage(const struct CoopMessage *msg)
{
    u8 i, count;
    const u8 *data = msg->data;
    u8 selected[TRADE_ID_LENGTH];
    if (!sUi.active || sUi.closing || msg->type != COOP_MSG_TRADE_STATE || msg->length < TRADE_HEADER_SIZE)
        return;
    if ((data[0] | (data[1] << 8)) != sToken || data[2] > STATE_READY || data[3] >= ARRAY_COUNT(sText_Notices))
        return;
    count = data[4];
    if (count > TRADE_PEERS || msg->length != TRADE_HEADER_SIZE + count * TRADE_ID_LENGTH)
        return;
    if ((data[2] == STATE_INVITING || data[2] == STATE_INVITED || data[2] == STATE_READY) && !IsId(data + 5))
        return;
    for (i = 0; i < count; i++)
        if (!IsId(data + TRADE_HEADER_SIZE + i * TRADE_ID_LENGTH)) return;
    memcpy(selected, sUi.ids[sUi.cursor], TRADE_ID_LENGTH);
    sUi.state = data[2];
    sUi.notice = data[3];
    sUi.count = count;
    sUi.cursor = 0;
    memcpy(sUi.partner, data + 5, TRADE_ID_LENGTH);
    for (i = 0; i < count; i++)
    {
        memcpy(sUi.ids[i], data + TRADE_HEADER_SIZE + i * TRADE_ID_LENGTH, TRADE_ID_LENGTH);
        if (!memcmp(selected, sUi.ids[i], TRADE_ID_LENGTH)) sUi.cursor = i;
    }
    sUi.dirty = TRUE;
}
