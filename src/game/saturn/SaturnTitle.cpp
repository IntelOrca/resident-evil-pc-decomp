// SaturnTitle.cpp - the title menu's text in the original option typeface.
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

// LoadFile reports a missing file as (size_t)-1; anything outside the buffer is
// refused the same way.
static int font_read(void)
{
    // Probe first: LoadFile on a missing file is an error path the rest of the
    // engine does not expect to take quietly.
    if (!AssetBaseHasFile(-1, "Data/TITLEFNT.BIN")) return 0;
    const size_t n = LoadFile(GAME_DATA_ROOT "data\\titlefnt.bin", s_fontFile, 0x20);
    return (n != (size_t)-1 && n >= 8 && n <= FONT_FILE_MAX) ? (int)n : 0;
}

void saturn_title_font_forget(void)
{
    s_fontState = 0;
}

int saturn_title_font_load(void)
{
    if (s_fontState != 0) return s_fontState > 0;
    s_fontState = -1;

    // The base tree's copy, read with no overlay in the way so every mode gets
    // the same file.
    char savedOverlay[16];
    strncpy(savedOverlay, GetAssetModeName(), sizeof(savedOverlay) - 1);
    savedOverlay[sizeof(savedOverlay) - 1] = '\0';
    SetAssetMode("");
    int n = font_read();
    // A tree migrated before the font moved has it in the SATURN overlay only.
    if (n == 0 && AssetOverlayHasFile("SATURN", "Data/TITLEFNT.BIN")) {
        SetAssetMode("SATURN");
        const size_t got = LoadFile(GAME_DATA_ROOT "data\\titlefnt.bin", s_fontFile, 0x20);
        n = (got != (size_t)-1 && got >= 8 && got <= FONT_FILE_MAX) ? (int)got : 0;
    }
    SetAssetMode(savedOverlay);

    if (n == 0) {
        dbg_printf("[TITLE] titlefnt.bin not available - the title keeps its own rows "
                   "and has no OPTIONS item (re-run the asset migrator's PC tab)\n");
        return 0;
    }
    if (memcmp(s_fontFile, "TFNT", 4) != 0) return 0;

    const int height = s_fontFile[4];
    const int count = s_fontFile[7];
    if (height <= 0 || height > SATURN_TITLE_STRIP_H || count <= 0) return 0;

    memset(s_glyphs, 0, sizeof(s_glyphs));
    size_t pos = 8;
    for (int i = 0; i < count; i++) {
        if (pos + 2 > (size_t)n) return 0;
        const unsigned char ch = s_fontFile[pos];
        const int w = s_fontFile[pos + 1];
        if (w == 0 || pos + 2 + (size_t)w * height > (size_t)n) return 0;
        if (ch < FONT_MAX_GLYPH) {
            s_glyphs[ch].levels = s_fontFile + pos + 2;
            s_glyphs[ch].width = w;
        }
        pos += 2 + (size_t)w * height;
    }
    if (pos != (size_t)n) return 0;

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

unsigned int saturn_title_build_strips(unsigned char* out,
                                       const char* const* items, int count)
{
    if (out == NULL || items == NULL || s_fontState <= 0) return 0;
    if (count <= 0 || count > SATURN_TITLE_STRIP_MAX) return 0;

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

    for (int i = 0; i < count; i++) {
        const int y = i * SATURN_TITLE_STRIP_PITCH;
        draw_text(p, y, items[i], 0);
        draw_text(p, y + SATURN_TITLE_STRIP_H, items[i], 1);
    }
    return headerBytes + pixBytes;
}
