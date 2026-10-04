// SaturnTitle.h - the Sega Saturn build's title menu, drawn in the original
// Resident Evil typeface.
//
// The Saturn mode draws its menu in a typeface of its own. The port keeps the PC/PS1
// look there instead, so in Saturn mode the second title state - the one after
// PRESS START BUTTON - is
//
//     NEW GAME
//     LOAD GAME
//     EXTRA
//
// with EXTRA selectable only once the Battle Game is unlocked. EXTRA opens a
// submenu that lists the extra game modes; today that is
//
//     BATTLE GAME
//     BACK
//
// and every later extra mode is one more entry in the table in SaturnTitle.cpp,
// ahead of BACK.
//
// The text comes from the bitmap font the asset migrator writes as
// SATURN/Data/TITLEFNT.BIN (tools/asset_migrator/src/core/TitleFont.h). When
// that file is absent nothing here is used: the title keeps its previous
// behaviour (the USA sheet's two rows plus a BATTLE GAME row built from the
// port's 8x14 font), so a half-migrated tree still works.
//
// Only reachable with g_bSaturnMode; OG and DC never call any of this.
#pragma once

// g_titleSelectionId values. 0 = PRESS START, 1/2 = NEW/LOAD GAME (as in the
// USA build). 3 is EXTRA, which never leaves the title: it opens the submenu.
// The submenu's entries follow, and the first is the Battle Game - the value
// title_state() acts on.
#define SATURN_TITLE_EXTRA_ITEM     3
#define SATURN_TITLE_EXTRAS_FIRST   4

// Loads data\titlefnt.bin once. Returns 1 when the font is usable.
int saturn_title_font_load(void);

// Writes the three main-menu frames (selection 1, 2, 3 bright) into an already
// loaded 256x256 4bpp USA title sheet, below PRESS START BUTTON's rows. The
// sheet's own palette is used: index 1 = bright, 2 = dim.
void saturn_title_draw_main_frames(unsigned char* usaTimPage, int battleUnlocked);

// Builds the extras submenu sheet as a complete TIM (256x256, 4bpp, the same
// grey palette and CLUT row as the USA sheet). Returns its size, 0 on failure.
unsigned int saturn_title_build_extras_page(unsigned char* out);

// Number of submenu entries, BACK included. Selection SATURN_TITLE_EXTRAS_FIRST
// + n is entry n; the last one is BACK.
int saturn_title_extras_count(void);

// Screen rows within the sheets: vramY of selection 1..3's frame on the main
// sheet, and the frame height shared by both sheets.
#define SATURN_TITLE_FRAME_H        64
#define SATURN_TITLE_MAIN_FRAME_Y   64
