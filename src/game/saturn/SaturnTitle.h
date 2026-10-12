// SaturnTitle.h - the title menu's text, drawn in the original Resident Evil
// option typeface.
//
// This started as the Sega Saturn mode's title menu (hence the file's place and
// name) and is now the title menu of every mode. The second title state - the
// one after PRESS START BUTTON - is
//
//     NEW GAME
//     LOAD GAME
//     EXTRA          only while the Battle Game is offered
//     OPTIONS
//
// EXTRA opens a submenu that lists the extra game modes (BATTLE GAME, BACK) and
// OPTIONS chains the options state (GameOptions.h). The item lists and what
// each one does live in TitleScreen.cpp; this file only turns text into art.
//
// The text comes from the bitmap font the asset migrator writes into the BASE
// tree as Data/TITLEFNT.BIN (tools/asset_migrator/src/core/TitleFont.h). The
// PC tab writes it; the Saturn tab used to write it into the SATURN overlay
// instead, and a tree migrated that way is still read. When neither file is
// there nothing here is used: the title keeps the art the release shipped with
// (NEW GAME / LOAD GAME, plus a BATTLE GAME row built from the 8x14 font) and
// has no OPTIONS item.
#pragma once

// g_titleSelectionId values outside the Director's Cut. 0 = PRESS START, 1/2 =
// NEW/LOAD GAME (as in the USA build). 3 is EXTRA, which never leaves the
// title: it opens the submenu. The submenu's entries follow, and the first is
// the Battle Game - the value title_state() acts on. (The Director's Cut has
// its difficulty cells at 3-6 and numbers these from 8; see TitleScreen.cpp.)
#define SATURN_TITLE_EXTRA_ITEM     3
#define SATURN_TITLE_EXTRAS_FIRST   4

// Loads data\titlefnt.bin once. Returns 1 when the font is usable.
int saturn_title_font_load(void);

// The menu sheet: a 256x256 4bpp TIM in the USA title sheet's grey palette and
// CLUT row, holding two strips per item - the text centred on the sheet's 256
// columns, once dim and once bright:
//
//     item i, dim     rows i * 24      .. i * 24 + 11
//     item i, bright  rows i * 24 + 12 .. i * 24 + 23
//
// The title draws one strip per menu line, so a menu of any length fits on the
// screen without a frame per cursor position. Returns the TIM's size, 0 when the
// font is not loaded or there are more than SATURN_TITLE_STRIP_MAX items.
#define SATURN_TITLE_STRIP_H      12
#define SATURN_TITLE_STRIP_PITCH  24
#define SATURN_TITLE_STRIP_MAX    10
unsigned int saturn_title_build_strips(unsigned char* out,
                                       const char* const* items, int count);

// Makes the next saturn_title_font_load() look for the file again. The answer
// is cached, and the options menu can change the asset region under it.
void saturn_title_font_forget(void);
