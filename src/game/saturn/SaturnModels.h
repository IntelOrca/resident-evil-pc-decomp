// EntityModels.h - Sega Saturn entity model selection.
//
// Named SaturnModels.* rather than EntityModels.* on purpose: MSVC's default
// object naming is basename-only, so src/game/dc/EntityModels.cpp and a
// src/game/saturn/EntityModels.cpp would both compile to obj\Release\
// EntityModels.obj. MSB8027 warns, then one object silently overwrites the
// other and the link fails with four missing dc_* symbols (LNK2019). CMake
// does not have this problem - it derives object names from the relative path -
// so the two build systems would disagree about which file is which. See the
// SATURN_EMD_* block below and src/game/saturn/SaturnModels.cpp.
//
// The Saturn ships the same campaign as the PC release - all 348 shared rooms
// decode byte-identical to the PS1's (docs/SATURN_VERSION_ANALYSIS.md 1) - and adds
// two creatures on top of it. Neither is in any room's `enemy_set` list, so
// neither is a data edit: both are selected by the model table and the loader
// (SATURN_VERSION_ANALYSIS.md 4g).
//
// The Saturn's per-character EMD name table (GAME5 `0x0609AD5C`, 53 x 12-byte
// entries per block, the second block at `+0x27C` for Jill; model index = type
// + 4, indices 0-3 the players) is the SAME shape and width as the port's
// g_emdPathTable, and it fills two of the ten placeholder slots the PC leaves
// between index 26 and 35 (`em100a` in the Chris block, `em110a` in Jill's at
// 79-88):
//
//   type  index  Saturn           PC
//   0x16  26     em1016/em1116    placeholder
//   0x17  27     em1017/em1117    placeholder
//   0x1F  35     em1018/em1118    placeholder     <- NOT on the disc; left alone
//
// Because the width matches, this is a two-entry override rather than a new
// table, and the file NAMES are the PC's own - `enemy/em1016.emd` resolves
// through ResolveAssetRoot to SATURN/Enemy/EM1016.EMD in Saturn mode, exactly
// as `dc/EntityModels.cpp` resolves index 26 to the DC's own EM1016. The two
// releases happen to use the same file name for different creatures (the DC's
// is the Forest zombie, the Saturn's is the Tick); the modes are mutually
// exclusive and each has its own overlay folder, so the overlay is what decides
// which one it is. That is the design, not a collision.
//
// Everything here is gated on g_bSaturnMode by the caller, so OG and DC never
// reach it. See docs/SATURN_PORT.md.
#pragma once

// Model-table index of the Tick. Room entities are looked up at
// `entity->id + 4` (the +4 is the type->model offset the Saturn shares with the
// PC), so this is entity type 0x16.
#define SATURN_EMD_INDEX_TICK         26

// Model-table index of Zombie Wesker, entity type 0x17.
#define SATURN_EMD_INDEX_ZOMBIE_WESKER 27

// The Tick is not a type at all - it is a MODEL SWAP in the Saturn's loader
// (GAME5 `0x0604EA80`): when the current stage byte (`0x002F8660`) is 2 and the
// model index is 10, the Hunter's `em1006`, index 26 is loaded instead. The
// entity keeps type 6 and the Hunter's tables, damage rows and skeleton (the
// Tick EMD's PS1 section is the Hunter's), but the module for that stage byte,
// GAME2, has its own Hunter AI - the stab, the walk, the decision layer - which
// is saturn/SaturnTick.h.
#define SATURN_EMD_INDEX_HUNTER       10

// The stage whose Hunters are Ticks: 0-indexed stage 2, the mansion main hall
// (stage 3 in 1-index). Hunters anywhere else in the campaign stay stock.
#define SATURN_TICK_STAGE             2

// Returns the Saturn's path for a model index, or NULL when the port's
// g_emdPathTable entry already is the Saturn's file. `jillBlock` is the block
// selector (0 = Chris scenario, 1 = Jill), i.e. `g_playerEntity.id & 1`.
const char* saturn_emd_path(unsigned char modelIndex, unsigned char jillBlock);

// The Tick swap, applied to the model index before the table lookup. Returns
// the index to load; the identity for every index and stage the Saturn does
// not remap. Call it with the index LoadEntityEMD has after its costume-variant
// fixup, i.e. before the `(g_playerEntity.id & 1) * 53` block base is added.
unsigned char saturn_emd_model_index(unsigned char modelIndex);

// The Tick's own sound effects. The Hunter's eight enemy-sound slots (0-7:
// He_walkA, He_walkB, He_jump, He_att, He_land, He_smash, He_dam, He_Nout) are
// filled, on the Saturn, from the stage-3 room SE bank (/SND/SE308.CDP), which
// the asset migrator renders into SATURN/Sound as SAT_TICK_*. The song a slot
// plays is the one the bank's own table names (core/SaturnMusic.h), not song
// <slot>. The Tick's stab also uses slot 8, empty on the PC. Given a slot, the
// name the PC's room row holds for it and the row itself, returns the Saturn's
// replacement, or NULL when the slot is not a Tick sound or the stage is not the
// Tick's. Saturn mode only - the caller gates on g_bSaturnMode.
const char* saturn_tick_snd_name(int slot, const char* baseName, const char* const* row);
