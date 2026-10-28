#define PIXEL_FILL(n) (n)
#define FONT_NORMAL 1
#define TEXT_SKIP_DRAW 0xFF
void AddTextPrinterParameterized3(u8 window, u8 font, u8 x, u8 y, const u8 *colors, s8 speed, const u8 *text);
void SetStandardWindowBorderStyle(u8 window, bool8 copy);
void ClearStdWindowAndFrameToTransparent(u8 window, bool8 copy);
