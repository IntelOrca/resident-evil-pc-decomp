// SaturnModels.cpp - Sega Saturn entity model selection.
//
// Everything the Saturn adds to the model table is in saturn/SaturnModels.h.
// The two pieces are the table override (two indices) and the Tick loader swap
// (one index, one stage); the Golden Tyrant's palette tint, the Zombie Wesker's
// arena-80C0 type override and the Battle Game's own rooms come later, with
// Phase C, because each of them needs the battle-mode flag that Phase C ports
// first.
#include "SaturnModels.h"

#include "../../Globals.h"

const char* saturn_emd_path(unsigned char modelIndex, unsigned char jillBlock)
{
    switch (modelIndex) {
    // Index 26 = entity type 0x16. The PC's table holds the first em100a filler
    // here because no USA room ever spawns id 0x16 - the Saturn's Tick is the
    // only thing in any release that does.
    case SATURN_EMD_INDEX_TICK:
        return jillBlock ? "enemy/em1116.emd" : "enemy/em1016.emd";
    // Index 27 = entity type 0x17, Zombie Wesker. Not a campaign type either:
    // the Saturn forces it on arena 80C0's first enemy set, in battle mode.
    case SATURN_EMD_INDEX_ZOMBIE_WESKER:
        return jillBlock ? "enemy/em1117.emd" : "enemy/em1017.emd";
    default:
        // Index 35 would be em1018/em1118 (type 0x1F), but that model is not on
        // the disc, so the table entry stands and nothing reads it.
        return NULL;
    }
}

unsigned char saturn_emd_model_index(unsigned char modelIndex)
{
    // The Saturn's loader (GAME5 0x0604EA80) tests the stage byte and the model
    // index together, right where the PC reads g_emdPathTable. g_stageId is the
    // same 0-indexed stage the Saturn's `0x002F8660` holds, and Saturn mode has
    // no arrange stages, so no room_file_stage() fold is wanted here: the test
    // is against the stage the game is actually in.
    if (modelIndex == SATURN_EMD_INDEX_HUNTER && g_stageId == SATURN_TICK_STAGE) {
        return SATURN_EMD_INDEX_TICK;
    }
    return modelIndex;
}

const char* saturn_tick_snd_name(int slot, const char* baseName)
{
    static const char* const kTickSnd[8] = {
        "SAT_TICK_WALKA", "SAT_TICK_WALKB", "SAT_TICK_JUMP", "SAT_TICK_ATT",
        "SAT_TICK_LAND",  "SAT_TICK_SMASH", "SAT_TICK_DAM",  "SAT_TICK_NOUT",
    };
    if (g_stageId != SATURN_TICK_STAGE || slot < 0 || slot >= 8) return NULL;
    // Only the Hunter's own rows: the PC names those He_*, and every other
    // sound in the room (props, footsteps) stays as it is.
    if (baseName == NULL || baseName[0] != 'H' || baseName[1] != 'e' || baseName[2] != '_') {
        return NULL;
    }
    return kTickSnd[slot];
}
