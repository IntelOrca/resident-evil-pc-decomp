#pragma once
// The Saturn's Battle Game music -> <overlay>/Sound, rendered from the disc's
// sequences by core/SaturnSound (the Sega sound driver + SCSP model).
//
// Only the music the PC has no counterpart for is written. In the Battle Game
// most arenas play BGM group 0x40 = /SND/BATTLE.CDP (GAME5 0x0604CA7E), a
// Saturn-only pack; the others reuse campaign groups the PC already ships
// (0x0F rest rooms, 0x06, 0x27, 0x23, and 0x34's Tyrant theme in arena 15 -
// GAME5's arena table 0x0609ACCC). RANKING.PRG adds /SND/NAME.CDP.

#include "core/AudioWriter.h"
#include "core/DiscImage.h"
#include "core/Types.h"

#include <string>
#include <vector>

namespace re1 {

struct SaturnMusicEntry {
    const char* pack;   // /SND/<pack>
    int song;           // song in its sequence bank = the BGM channel
    const char* name;   // Sound/<name>.wav|.ogg, the name the game asks for
    const char* what;
};

inline constexpr SaturnMusicEntry kSaturnMusic[] = {
    {"BATTLE.CDP", 0, "SAT_BATTLE",
     "Battle Game arenas (group 0x40 channel 0, 25.3 s loop)"},
    {"BATTLE.CDP", 1, "SAT_BATTLE_END",
     "Battle Game end-of-run fanfare (group 0x40 channel 1, GAME5 0x06088C10)"},
    {"BGM34B.CDP", 2, "SAT_BATTLE_TYRANT",
     "Battle Game arena 15 Tyrant theme (group 0x34 channel 2, 43.5 s loop)"},
    {"NAME.CDP", 0, "SAT_RANKING", "Battle Game ranking / name entry (RANKING.PRG)"},
};

// The Tick's exclusive sound effects. The Saturn's room SE banks (/SND/SE<stage>
// <room>.CDP, same CD_PACK and Sega driver as the music) hold one single-note
// "song" per voice; song N is the room's enemy-sound slot N. Stage 3 room 8 is
// the Tick's room (the Hunter swap, SATURN_TICK_STAGE), and its songs 0-7 are
// the eight slots the PC fills with He_walkA .. He_Nout for the Hunter. No other
// Saturn bank has those samples, so they are the Tick's own. Written mono: the
// game pans enemy sounds itself (Snd_em -> pan_set).
struct SaturnSfxEntry {
    const char* pack;   // /SND/<pack>
    int song;           // song in its sequence bank = the enemy-sound slot
    const char* name;   // Sound/<name>.wav|.ogg
    const char* what;
};

inline constexpr SaturnSfxEntry kSaturnTickSfx[] = {
    {"SE308.CDP", 0, "SAT_TICK_WALKA", "footstep A (slot 0, Hunter He_walkA)"},
    {"SE308.CDP", 1, "SAT_TICK_WALKB", "footstep B (slot 1, Hunter He_walkB)"},
    {"SE308.CDP", 2, "SAT_TICK_JUMP", "jump (slot 2, Hunter He_jump)"},
    {"SE308.CDP", 3, "SAT_TICK_ATT", "attack (slot 3, Hunter He_att)"},
    {"SE308.CDP", 4, "SAT_TICK_LAND", "landing (slot 4, Hunter He_land)"},
    {"SE308.CDP", 5, "SAT_TICK_SMASH", "smash (slot 5, Hunter He_smash)"},
    {"SE308.CDP", 6, "SAT_TICK_DAM", "damage (slot 6, Hunter He_dam)"},
    {"SE308.CDP", 7, "SAT_TICK_NOUT", "death (slot 7, Hunter He_Nout)"},
};

struct SaturnMusicOptions {
    bool music = true;      // kSaturnMusic
    bool tickSfx = true;    // kSaturnTickSfx
    AudioFormat format = AudioFormat::Wav;
    int oggQuality = 6;
    std::string ffmpegPath = "ffmpeg";
};

// Renders kSaturnMusic (and kSaturnTickSfx) into <overlay>/Sound and writes Sound/PS1AUDIO.TXT
// there, whose presence makes the game mix at 44.1 kHz (the Saturn renders
// are 44.1 kHz; the PC's 22.05 kHz mix would halve them). `written` receives
// overlay-relative paths.
bool migrateSaturnMusic(const DiscImage& img, const std::string& overlay,
                        const SaturnMusicOptions& opts, const Progress& progress,
                        std::vector<std::string>* written, std::string* error);

}  // namespace re1
