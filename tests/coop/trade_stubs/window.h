#define WINDOW_NONE 0xFF
#define COPYWIN_FULL 3
struct WindowTemplate { u8 unused; };
struct WindowTemplate CreateWindowTemplate(u8 bg, u8 x, u8 y, u8 width, u8 height, u8 palette, u16 base);
u16 AddWindow(const struct WindowTemplate *window);
void RemoveWindow(u8 window);
void PutWindowTilemap(u8 window);
void FillWindowPixelBuffer(u8 window, u8 fill);
void CopyWindowToVram(u8 window, u8 mode);
