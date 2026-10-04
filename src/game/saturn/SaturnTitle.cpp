// SaturnTitle.cpp - the Sega Saturn build's title menu in the original typeface.
// See SaturnTitle.h. The font file layout is documented in
// tools/asset_migrator/src/core/TitleFont.h.
#include "SaturnTitle.h"

#include "../../Globals.h"
#include "../../DebugPrint.h"
#include "../FileLoader.h"
#include "../../system/AssetPath.h"

#include <cstring>

// ---------------------------------------------------------------------------
// The font
// ---------------------------------------------------------------------------
#define FONT_FILE_MAX   16384          // TITLEFNT.BIN is ~4.4 KB
#define FONT_MAX_GLYPH  128

struct TitleGlyph {
    const unsigned char* levels;       // width * height, 0 clear / 1 half / 2 full
    int width;
};

static unsigned char s_fontFile[FONT_FILE_MAX];
static TitleGlyph    s_glyphs[FONT_MAX_GLYPH];
static int           s_fontHeight = 0;
static int           s_fontSpacing = 0;
static int           s_fontSpaceAdvance = 0;
static int           s_fontState = 0;  // 0 = not tried, 1 = loaded, -1 = unusable

int saturn_title_font_load(void)
{
    if (s_fontState != 0) return s_fontState > 0;
    s_fontState = -1;

    // The font lives in the SATURN overlay. In OG the overlay is not searched, so
    // look in it explicitly for the length of the load (the Battle Game is
    // offered in OG when the SATURN assets are imported).
    char savedOverlay[16];
    strncpy(savedOverlay, GetAssetModeName(), sizeof(savedOverlay) - 1);
    savedOverlay[sizeof(savedOverlay) - 1] = '\0';
    const int borrowed = !g_bSaturnMode;
    if (borrowed) SetAssetMode("SATURN");
    const size_t n = LoadFile(GAME_DATA_ROOT "data\\titlefnt.bin", s_fontFile, 0x20);
    if (borrowed) SetAssetMode(savedOverlay);
    if (n == (size_t)-1 || n < 8 || n > FONT_FILE_MAX) {
        dbg_printf("[TITLE] titlefnt.bin not available - Saturn title keeps the old rows\n");
        return 0;
    }
    if (memcmp(s_fontFile, "TFNT", 4) != 0) return 0;

    const int height = s_fontFile[4];
    const int count = s_fontFile[7];
    if (height <= 0 || height > 32 || count <= 0) return 0;

    memset(s_glyphs, 0, sizeof(s_glyphs));
    size_t pos = 8;
    for (int i = 0; i < count; i++) {
        if (pos + 2 > n) return 0;
        const unsigned char ch = s_fontFile[pos];
        const int w = s_fontFile[pos + 1];
        if (w == 0 || pos + 2 + (size_t)w * height > n) return 0;
        if (ch < FONT_MAX_GLYPH) {
            s_glyphs[ch].levels = s_fontFile + pos + 2;
            s_glyphs[ch].width = w;
        }
        pos += 2 + (size_t)w * height;
    }
    if (pos != n) return 0;

    s_fontHeight = height;
    s_fontSpacing = s_fontFile[5];
    s_fontSpaceAdvance = s_fontFile[6];
    s_fontState = 1;
    return 1;
}

static int text_width(const char* text)
{
    int w = 0;
    for (int i = 0; text[i] != '\0'; i++) {
        const unsigned char c = (unsigned char)text[i];
        if (c == ' ' || c >= FONT_MAX_GLYPH || s_glyphs[c].levels == NULL) {
            w += s_fontSpaceAdvance;
        } else {
            w += s_glyphs[c].width + s_fontSpacing;
        }
    }
    return w > 0 ? w - s_fontSpacing : 0;
}

// ---------------------------------------------------------------------------
// Drawing into a 256-wide 4bpp sheet (128 bytes per row, left pixel in the low
// nibble). `bright` text is palette index 1 with index 2 on its half-tone
// corners; dim text is index 2 alone.
// ---------------------------------------------------------------------------
#define SHEET_W       256
#define SHEET_STRIDE  128
#define SHEET_H       256

static void put_pixel(unsigned char* pix, int x, int y, int index)
{
    unsigned char* b = pix + y * SHEET_STRIDE + (x >> 1);
    if (x & 1) *b = (unsigned char)((*b & 0x0F) | (index << 4));
    else       *b = (unsigned char)((*b & 0xF0) | index);
}

static void draw_text(unsigned char* pix, int y, const char* text, int bright)
{
    int x = (SHEET_W - text_width(text)) / 2;
    if (y < 0 || y + s_fontHeight > SHEET_H) return;
    for (int i = 0; text[i] != '\0'; i++) {
        const unsigned char c = (unsigned char)text[i];
        if (c == ' ' || c >= FONT_MAX_GLYPH || s_glyphs[c].levels == NULL) {
            x += s_fontSpaceAdvance;
            continue;
        }
        const TitleGlyph& g = s_glyphs[c];
        for (int gy = 0; gy < s_fontHeight; gy++) {
            for (int gx = 0; gx < g.width; gx++) {
                const int level = g.levels[gy * g.width + gx];
                if (level == 0) continue;
                int index;
                if (bright) index = (level == 2) ? 1 : 2;
                else        index = (level == 2) ? 2 : 0;
                if (index != 0 && x + gx >= 0 && x + gx < SHEET_W) {
                    put_pixel(pix, x + gx, y + gy, index);
                }
            }
        }
        x += g.width + s_fontSpacing;
    }
}

// Lines sit 18 rows apart, as NEW GAME / LOAD GAME do in the original art
// (rows 84 and 102), and the first one a row below the frame's top, which is
// what the positions in the title table assume.
#define LINE_PITCH  18
#define LINE_TOP    1

// The USA sheet's copyright strip: where it is, how tall, and where in a frame
// it goes. The original cell has it at 83 + 48 = 131, straight under LOAD GAME;
// with a third line the strip drops to the frame's last 9 rows (55..63) so EXTRA
// (rows 37..47) keeps a gap above it.
#define COPYRIGHT_SRC_Y    131
#define COPYRIGHT_ROWS     9
#define COPYRIGHT_FRAME_Y  (SATURN_TITLE_FRAME_H - COPYRIGHT_ROWS)

static void draw_frame(unsigned char* pix, int frameTop, const char* const* items,
                       int count, int selected)
{
    for (int i = 0; i < count; i++) {
        draw_text(pix, frameTop + LINE_TOP + i * LINE_PITCH, items[i], i == selected);
    }
}

// ---------------------------------------------------------------------------
// The menus
// ---------------------------------------------------------------------------
static const char* const kMainItems[3] = { "NEW GAME", "LOAD GAME", "EXTRA" };

// The extra game modes. Add a mode by inserting its entry before BACK; the
// title's confirm path decides what it starts (see title_state()).
static const char* const kExtraItems[] = { "BATTLE GAME", "BACK" };
#define EXTRA_COUNT  ((int)(sizeof(kExtraItems) / sizeof(kExtraItems[0])))

int saturn_title_extras_count(void)
{
    return EXTRA_COUNT;
}

void saturn_title_draw_main_frames(unsigned char* usaTimPage, int battleUnlocked)
{
    (void)battleUnlocked;   // EXTRA is always drawn; the cursor simply never lands on
                            // it while locked (title_menu_last_item())
    unsigned char* pix = usaTimPage + 0x40;

    // Each of the original option cells carries the copyright line under its
    // text (rows 131..139 of the sheet, 48 rows below NEW GAME's cell top at 83).
    // Keep one copy; the frames below overwrite the rows it sits in.
    unsigned char footer[COPYRIGHT_ROWS * SHEET_STRIDE];
    memcpy(footer, pix + COPYRIGHT_SRC_Y * SHEET_STRIDE, sizeof(footer));

    memset(pix + SATURN_TITLE_MAIN_FRAME_Y * SHEET_STRIDE, 0,
           (SHEET_H - SATURN_TITLE_MAIN_FRAME_Y) * SHEET_STRIDE);
    for (int sel = 0; sel < 3; sel++) {
        const int top = SATURN_TITLE_MAIN_FRAME_Y + sel * SATURN_TITLE_FRAME_H;
        draw_frame(pix, top, kMainItems, 3, sel);
        memcpy(pix + (top + COPYRIGHT_FRAME_Y) * SHEET_STRIDE, footer, sizeof(footer));
    }
}

// A complete 256x256 4bpp TIM in the USA sheet's grey palette and CLUT row, with
// `frames` frames of SATURN_TITLE_FRAME_H rows, frame f drawing `items` with item
// f bright. The extras submenu and, where the USA sheet is not the base (the
// Director's Cut), the main menu are both this.
static unsigned int build_sheet(unsigned char* out, const char* const* items,
                                int count, int frames)
{
    if (out == NULL || s_fontState <= 0) return 0;
    if (frames * SATURN_TITLE_FRAME_H > SHEET_H) return 0;   // 4 frames at most

    const unsigned int pixBytes = SHEET_STRIDE * SHEET_H;
    const unsigned int headerBytes = 0x40;
    memset(out, 0, headerBytes + pixBytes);

    unsigned char* p = out;
    *(unsigned int*)p = 0x10;                 p += 4;   // TIM magic
    *(unsigned int*)p = 0x08;                 p += 4;   // 4bpp + CLUT
    *(unsigned int*)p = 12 + 32;              p += 4;   // CLUT block length
    *(unsigned short*)p = 0;                  p += 2;   // clutX
    *(unsigned short*)p = 0x1E0;              p += 2;   // clutY, the row the title selects
    *(unsigned short*)p = 16;                 p += 2;
    *(unsigned short*)p = 1;                  p += 2;
    // The USA sheet's greys: 0 clear, 1 = (115,115,115), 2 = (57,57,57).
    for (int i = 0; i < 16; i++) {
        unsigned short c = 0;
        if (i == 1) c = 0x39CE;
        if (i == 2) c = 0x1CE7;
        *(unsigned short*)p = c;              p += 2;
    }
    *(unsigned int*)p = 12 + pixBytes;        p += 4;
    *(unsigned short*)p = 0;                  p += 2;
    *(unsigned short*)p = 0;                  p += 2;
    *(unsigned short*)p = SHEET_W / 4;        p += 2;
    *(unsigned short*)p = SHEET_H;            p += 2;

    for (int f = 0; f < frames; f++) {
        draw_frame(p, f * SATURN_TITLE_FRAME_H, items, count, f);
    }
    return headerBytes + pixBytes;
}

unsigned int saturn_title_build_extras_page(unsigned char* out)
{
    return build_sheet(out, kExtraItems, EXTRA_COUNT, EXTRA_COUNT);
}

unsigned int saturn_title_build_main_page(unsigned char* out)
{
    return build_sheet(out, kMainItems, 3, 3);
}
